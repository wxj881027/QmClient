#if defined(CONF_VIDEORECORDER)

#include "video.h"

#include "video_encoder.h"

#include <base/log.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
};

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

// This code is mostly stolen from https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/muxing.c

static const enum AVColorSpace COLOR_SPACE = AVCOL_SPC_BT709;
// AVCodecContext->colorspace is an enum AVColorSpace but sws_getCoefficients
// wants an SWS_CS_* macro. Both sets of constants follow H.273 numbering
// and hence agree, but we assert that they're equal here to be sure.
static_assert(COLOR_SPACE == SWS_CS_ITU709);

static LEVEL AvLevelToLogLevel(int Level)
{
	switch(Level)
	{
	case AV_LOG_PANIC:
	case AV_LOG_FATAL:
	case AV_LOG_ERROR:
		return LEVEL_ERROR;
	case AV_LOG_WARNING:
		return LEVEL_WARN;
	case AV_LOG_INFO:
		return LEVEL_INFO;
	case AV_LOG_VERBOSE:
	case AV_LOG_DEBUG:
		return LEVEL_DEBUG;
	case AV_LOG_TRACE:
		return LEVEL_TRACE;
	default:
		dbg_assert_failed("invalid log level: %d", Level);
	}
}

[[gnu::format(printf, 3, 0)]] static void AvLogCallback(void *pUser, int Level, const char *pFormat, va_list VarArgs)
{
	const LEVEL LogLevel = AvLevelToLogLevel(Level);
	if(LogLevel <= LEVEL_INFO)
	{
		char aLog[4096]; // Longest log line length
		int Length = str_format_v(aLog, sizeof(aLog), pFormat, VarArgs);
		if(Length > 0)
		{
			if(aLog[Length - 1] == '\n')
			{
				aLog[Length - 1] = '\0';
			}
			log_log(LogLevel, "videorecorder/libav", "%s", aLog);
		}
	}
}

void CVideo::Init()
{
	av_log_set_callback(AvLogCallback);
}

CVideo::CVideo(IGraphics *pGraphics, ISound *pSound, IStorage *pStorage, int Width, int Height, const char *pName, int OutputWidth, int OutputHeight) :
	m_pGraphics(pGraphics),
	m_pStorage(pStorage),
	m_pSound(pSound),
	m_FrameCapture(Width, Height)
{
	m_pFormatContext = nullptr;
	m_pFormat = nullptr;
	m_pOptDict = nullptr;

	m_pVideoCodec = nullptr;
	m_pAudioCodec = nullptr;

	m_Width = Width;
	m_Height = Height;
	// 编码输出尺寸（M7 超采样）：未指定或非法（> 渲染尺寸）时跟随渲染尺寸。
	// 奇数在 OpenVideo 里按编码器要求向下取偶（与旧行为一致）。
	m_OutputWidth = OutputWidth > 0 && OutputWidth <= Width ? OutputWidth : Width;
	m_OutputHeight = OutputHeight > 0 && OutputHeight <= Height ? OutputHeight : Height;
	str_copy(m_aName, pName);

	m_FPS = g_Config.m_ClVideoRecorderFPS;

	m_Recording = false;
	m_Started = false;
	m_Stopped = false;
	m_ProcessingAudioFrame = 0;

	m_HasAudio = m_pSound->IsSoundEnabled() && g_Config.m_ClVideoSndEnable;

	dbg_assert(ms_pCurrentVideo == nullptr, "ms_pCurrentVideo is NOT set to nullptr while creating a new Video.");

	ms_TickTime = time_freq() / m_FPS;
	ms_pCurrentVideo = this;
}

CVideo::~CVideo()
{
	ms_pCurrentVideo = nullptr;
}

bool CVideo::Start()
{
	dbg_assert(!m_Started, "Already started");

	// wait for the graphic thread to idle
	m_pGraphics->WaitForIdle();

	m_AudioStream = {};
	m_VideoStream = {};

	char aWholePath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = m_pStorage->OpenFile(m_aName, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE, aWholePath, sizeof(aWholePath));
	if(File)
	{
		io_close(File);
	}
	else
	{
		log_error("videorecorder", "Could not open file '%s'", aWholePath);
		return false;
	}

	// 容器格式按输出文件名后缀推断（M6）：.mp4/.mov/.mkv 由 qm_video_container 决定，
	// format_name 传 nullptr 让 libavformat 自行匹配，避免容器与后缀不一致。
	const int FormatAllocResult = avformat_alloc_output_context2(&m_pFormatContext, nullptr, nullptr, aWholePath);
	if(FormatAllocResult < 0 || !m_pFormatContext)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(FormatAllocResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not create format context: %s", aError);
		return false;
	}

	m_pFormat = m_pFormatContext->oformat;

#if defined(CONF_ARCH_IA32) || defined(CONF_ARCH_ARM) || defined(CONF_ARCH_WASM)
	// 32 位上固定最小资源占用，避免全尺寸缓冲与线程过多
	m_ConvertThreads = 2;
	m_AudioThreads = 2;
#else
	// 转换线程固定上限（可配置，配置系统已 clamp），不随 CPU 核数线性增长
	m_ConvertThreads = std::max<size_t>(1, (size_t)g_Config.m_QmVideoConvertThreads);
	// 音频帧轻量，线程数同样固定上限
	m_AudioThreads = std::min<size_t>((std::thread::hardware_concurrency() / 2) + 2, 4);
#endif

	m_CurAudioThreadIndex = 0;

	// 有界捕获槽队列：内存占用 = 槽数 × 全尺寸 RGBA，与核数无关
	const size_t QueueFrames = std::max<size_t>(2, (size_t)g_Config.m_QmVideoQueueFrames);
	const size_t VideoBufferSize = (size_t)4 * m_Width * m_Height * sizeof(uint8_t);
	m_vVideoSlots.resize(QueueFrames);
	for(size_t i = 0; i < QueueFrames; ++i)
	{
		m_vVideoSlots[i].m_vRGBA.resize(VideoBufferSize);
	}
	m_vFreeSlots.resize(QueueFrames);
	for(size_t i = 0; i < QueueFrames; ++i)
	{
		m_vFreeSlots[i] = QueueFrames - 1 - i; // 栈：先归还后复用，降低读写冲突
	}
	m_vPendingSlots.clear();
	m_NextVideoFrameToCapture = 0;
	m_NextVideoFrameToSubmit = 0;
	m_VideoStop = false;
	m_VideoPipelineError = false;

	m_vAudioBuffers.resize(m_AudioThreads);

	/* Add the audio and video streams using the default format codecs
	 * and initialize the codecs. */
	if(m_pFormat->video_codec != AV_CODEC_ID_NONE)
	{
		if(!AddStream(&m_VideoStream, m_pFormatContext, &m_pVideoCodec, m_pFormat->video_codec))
			return false;
	}
	else
	{
		log_error("videorecorder", "Could not determine default video stream codec");
		return false;
	}

	if(m_HasAudio)
	{
		// 音频统一 AAC：muxer 默认音频 codec 不可靠（matroska 默认 AC3，未编入），
		// AAC 由内置编码器支持且 mp4/mov/mkv 全兼容。
		if(!AddStream(&m_AudioStream, m_pFormatContext, &m_pAudioCodec, AV_CODEC_ID_AAC))
			return false;
	}

	m_vpAudioThreads.resize(m_AudioThreads);
	for(size_t i = 0; i < m_AudioThreads; ++i)
	{
		m_vpAudioThreads[i] = std::make_unique<CAudioRecorderThread>();
	}
	for(size_t i = 0; i < m_AudioThreads; ++i)
	{
		std::unique_lock<std::mutex> Lock(m_vpAudioThreads[i]->m_Mutex);
		m_vpAudioThreads[i]->m_Thread = std::thread([this, i]() REQUIRES(!m_WriteLock) { RunAudioThread(i == 0 ? (m_AudioThreads - 1) : (i - 1), i); });
		m_vpAudioThreads[i]->m_Cond.wait(Lock, [this, i]() -> bool { return m_vpAudioThreads[i]->m_Started; });
	}

	// 转换线程：从就绪队列取帧，乱序转换，按帧号顺序提交编码
	m_vpConvertThreads.resize(m_ConvertThreads);
	for(size_t i = 0; i < m_ConvertThreads; ++i)
	{
		m_vpConvertThreads[i] = std::make_unique<std::thread>([this, i]() REQUIRES(!m_WriteLock) { RunVideoConvertThread(i); });
	}

	/* Now that all the parameters are set, we can open the audio and
	 * video codecs and allocate the necessary encode buffers. */
	if(!OpenVideo())
		return false;

	if(m_HasAudio && !OpenAudio())
		return false;

	/* open the output file, if needed */
	if(!(m_pFormat->flags & AVFMT_NOFILE))
	{
		const int OpenResult = avio_open(&m_pFormatContext->pb, aWholePath, AVIO_FLAG_WRITE);
		if(OpenResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(OpenResult, aError, sizeof(aError));
			log_error("videorecorder", "Could not open file '%s': %s", aWholePath, aError);
			return false;
		}
	}

	m_VideoStream.m_vpSwsContexts.reserve(m_ConvertThreads);

	for(size_t i = 0; i < m_ConvertThreads; ++i)
	{
		if(m_VideoStream.m_vpSwsContexts.size() <= i)
			m_VideoStream.m_vpSwsContexts.emplace_back(nullptr);

		if(!m_VideoStream.m_vpSwsContexts[i])
		{
			// 转换目标跟随协商出的编码像素格式（M6）：RGBA → YUV420P/NV12/YUV422P10LE/...
			// 由 swscale 直接输出到编码帧，替代写死 YUV420P（非 420 格式会布局错配）。
			// 输入是渲染（readback）尺寸；输出是编码尺寸（M7 超采样下二者不同，swscale 缩小）。
			const bool Downscaling = m_Width != m_VideoStream.m_pCodecContext->width || m_Height != m_VideoStream.m_pCodecContext->height;
			m_VideoStream.m_vpSwsContexts[i] = sws_getCachedContext(
				m_VideoStream.m_vpSwsContexts[i],
				m_Width, m_Height, AV_PIX_FMT_RGBA,
				m_VideoStream.m_pCodecContext->width, m_VideoStream.m_pCodecContext->height, m_VideoStream.m_pCodecContext->pix_fmt,
				Downscaling ? (SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP) : (SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND | SWS_BITEXACT),
				nullptr, nullptr, nullptr);
			if(!m_VideoStream.m_vpSwsContexts[i])
			{
				log_error("videorecorder", "Could not allocate video color conversion context");
				return false;
			}

			const int *pMatrixCoefficients = sws_getCoefficients(COLOR_SPACE);
			if(sws_setColorspaceDetails(m_VideoStream.m_vpSwsContexts[i], pMatrixCoefficients, 0, pMatrixCoefficients, 0, 0, 1 << 16, 1 << 16) < 0)
			{
				log_error("videorecorder", "Could not configure video color conversion context");
				return false;
			}
		}
	}

	/* Write the stream header, if any. */
	const int WriteHeaderResult = avformat_write_header(m_pFormatContext, &m_pOptDict);
	if(WriteHeaderResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(WriteHeaderResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not write header: %s", aError);
		return false;
	}

	m_Recording = true;
	m_Started = true;
	m_Stopped = false;
	ms_Time = time_get();
	return true;
}

void CVideo::Pause(bool Pause)
{
	if(ms_pCurrentVideo)
		m_Recording = !Pause;
}

void CVideo::Stop()
{
	dbg_assert(!m_Stopped, "Already stopped");
	m_Stopped = true;

	// 生命周期不变量（与 backend_threaded.cpp 的图形线程回调对应）：
	// WaitForIdle 与图形线程共用 m_BufferSwapMutex，返回时图形线程已退出
	// NextVideoFrameThread 并回到 wait()；且本函数执行期间主线程不会提交新
	// RunBuffer，图形线程不会再次进入录制回调。因此此后的转换线程 join 与
	// delete 不会与图形线程的录制读回并发。
	m_pGraphics->WaitForIdle();

	// 视频转换管线：置停止标志并唤醒全部等待者，join 即代表队列已排空且
	// 所有在途帧按序提交完成（线程退出前完成各自帧的提交）。
	{
		std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);
		m_VideoStop = true;
	}
	m_VideoSlotReadyCond.notify_all();
	m_VideoSlotFreeCond.notify_all();
	m_VideoSubmitCond.notify_all();

	for(auto &pThread : m_vpConvertThreads)
		pThread->join();
	m_vpConvertThreads.clear();

	for(auto &pAudioThread : m_vpAudioThreads)
	{
		{
			std::unique_lock<std::mutex> Lock(pAudioThread->m_Mutex);
			pAudioThread->m_Finished = true;
			pAudioThread->m_Cond.notify_all();
		}

		pAudioThread->m_Thread.join();
	}
	m_vpAudioThreads.clear();

	while(m_ProcessingAudioFrame > 0)
		std::this_thread::sleep_for(10us);

	m_Recording = false;

	// 初始化失败时只释放资源，不能向尚未写入文件头的输出刷新编码帧。
	if(m_Started)
	{
		FinishFrames(&m_VideoStream);
		if(m_HasAudio)
			FinishFrames(&m_AudioStream);
		// 排空失败同样可能留下无 trailer 的截断文件；失败必须显式落锁，供停录方
		// 区分"正常收尾"与"收尾期错误"（错误段不得被记成已完成）。
		if(m_pFormatContext && av_write_trailer(m_pFormatContext) < 0)
		{
			log_error("videorecorder", "Could not write trailer; output may be truncated");
			m_EncodeError.store(true, std::memory_order_release);
		}
	}

	CloseStream(&m_VideoStream);

	if(m_HasAudio)
		CloseStream(&m_AudioStream);

	if(m_pFormatContext)
	{
		if(!(m_pFormat->flags & AVFMT_NOFILE))
		{
			// 关闭输出失败同样意味着文件不完整（缓冲未落盘），按收尾失败处理。
			if(avio_closep(&m_pFormatContext->pb) < 0)
			{
				log_error("videorecorder", "Could not close output file; output may be incomplete");
				m_EncodeError.store(true, std::memory_order_release);
			}
		}

		avformat_free_context(m_pFormatContext);
	}

	// HasError() 此时已含录制期 + 排空/trailer/关闭的全部失败；Stop 即将自删除，
	// 结果闭锁给调用方（分段收尾据此决定是否把该段记为完成）。
	IVideo::ms_LastStopFailed = HasError();

	ISound *volatile pSound = m_pSound;

	pSound->PauseAudioDevice();
	delete ms_pCurrentVideo;
	pSound->UnpauseAudioDevice();
}

void CVideo::NextVideoFrameThread()
{
	if(!IsRecording())
		return;

	m_VideoFrameIndex += 1;
	if(m_VideoFrameIndex < 2)
		return;

	size_t SlotIndex;
	{
		std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);
		// 反压：捕获槽耗尽时渲染端等待，离线导出不丢帧、不无界缓冲
		m_VideoSlotFreeCond.wait(Lock, [this]() -> bool { return !m_vFreeSlots.empty() || m_VideoStop || m_VideoPipelineError; });
		if(m_VideoStop || m_VideoPipelineError || m_vFreeSlots.empty())
			return;

		SlotIndex = m_vFreeSlots.back();
		m_vFreeSlots.pop_back();
	}

	// GPU 读回的耗时操作在队列锁外执行；槽尚未发布给转换线程，无数据竞争
	if(!UpdateVideoBufferFromGraphics(SlotIndex))
	{
		// 读回失败：归还槽；捕获错误已由 CVideoFrameCapture 置位，后续帧停止录制
		std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);
		m_vFreeSlots.push_back(SlotIndex);
		m_VideoSlotFreeCond.notify_one();
		return;
	}

	{
		std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);
		m_vVideoSlots[SlotIndex].m_FrameNumber = m_NextVideoFrameToCapture++;
		m_vPendingSlots.push_back(SlotIndex);
	}
	m_VideoSlotReadyCond.notify_one();
}

void CVideo::NextVideoFrame()
{
	if(IsRecording())
	{
		ms_Time += ms_TickTime;
		ms_LocalTime = (ms_Time - ms_LocalStartTime) / (float)time_freq();
	}
}

void CVideo::NextAudioFrameTimeline(ISoundMixFunc Mix)
{
	if(IsRecording() && m_HasAudio)
	{
		double SamplesPerFrame = (double)m_AudioStream.m_pCodecContext->sample_rate / m_FPS;
		while(IsRecording() && m_AudioStream.m_SamplesFrameCount >= m_AudioStream.m_SamplesCount)
		{
			NextAudioFrame(Mix);
		}
		m_AudioStream.m_SamplesFrameCount += SamplesPerFrame;
	}
}

void CVideo::NextAudioFrame(ISoundMixFunc Mix)
{
	if(IsRecording() && m_HasAudio)
	{
		m_AudioFrameIndex += 1;

		m_ProcessingAudioFrame.fetch_add(1);

		size_t NextAudioThreadIndex = m_CurAudioThreadIndex + 1;
		if(NextAudioThreadIndex == m_AudioThreads)
			NextAudioThreadIndex = 0;

		// always wait for the next Audio thread too, to prevent a dead lock

		{
			auto *pAudioThread = m_vpAudioThreads[NextAudioThreadIndex].get();
			std::unique_lock<std::mutex> Lock(pAudioThread->m_Mutex);

			if(pAudioThread->m_HasAudioFrame)
			{
				pAudioThread->m_Cond.wait(Lock, [&pAudioThread]() -> bool { return !pAudioThread->m_HasAudioFrame; });
			}
		}

		// after reading the graphic libraries' frame buffer, go threaded
		{
			auto *pAudioThread = m_vpAudioThreads[m_CurAudioThreadIndex].get();

			std::unique_lock<std::mutex> Lock(pAudioThread->m_Mutex);

			if(pAudioThread->m_HasAudioFrame)
			{
				pAudioThread->m_Cond.wait(Lock, [&pAudioThread]() -> bool { return !pAudioThread->m_HasAudioFrame; });
			}

			Mix(m_vAudioBuffers[m_CurAudioThreadIndex].m_aBuffer, std::size(m_vAudioBuffers[m_CurAudioThreadIndex].m_aBuffer) / 2 / 2); // two channels

			int64_t DstNbSamples = av_rescale_rnd(
				swr_get_delay(m_AudioStream.m_vpSwrContexts[m_CurAudioThreadIndex], m_AudioStream.m_pCodecContext->sample_rate) +
					m_AudioStream.m_vpFrames[m_CurAudioThreadIndex]->nb_samples,
				m_AudioStream.m_pCodecContext->sample_rate,
				m_AudioStream.m_pCodecContext->sample_rate, AV_ROUND_UP);

			pAudioThread->m_SampleCountStart = m_AudioStream.m_SamplesCount;
			m_AudioStream.m_SamplesCount += DstNbSamples;

			pAudioThread->m_HasAudioFrame = true;
			{
				std::unique_lock<std::mutex> LockParent(pAudioThread->m_AudioFillMutex);
				pAudioThread->m_AudioFrameToFill = m_AudioFrameIndex;
			}
			pAudioThread->m_Cond.notify_all();
		}

		++m_CurAudioThreadIndex;
		if(m_CurAudioThreadIndex == m_AudioThreads)
			m_CurAudioThreadIndex = 0;
	}
}

void CVideo::RunAudioThread(size_t ParentThreadIndex, size_t ThreadIndex)
{
	auto *pThreadData = m_vpAudioThreads[ThreadIndex].get();
	auto *pParentThreadData = m_vpAudioThreads[ParentThreadIndex].get();
	std::unique_lock<std::mutex> Lock(pThreadData->m_Mutex);
	pThreadData->m_Started = true;
	pThreadData->m_Cond.notify_all();

	while(!pThreadData->m_Finished)
	{
		pThreadData->m_Cond.wait(Lock, [&pThreadData]() -> bool { return pThreadData->m_HasAudioFrame || pThreadData->m_Finished; });
		pThreadData->m_Cond.notify_all();

		if(pThreadData->m_HasAudioFrame)
		{
			FillAudioFrame(ThreadIndex);
			// check if we need to wait for the parent to finish
			{
				std::unique_lock<std::mutex> LockParent(pParentThreadData->m_AudioFillMutex);
				if(pParentThreadData->m_AudioFrameToFill != 0 && pThreadData->m_AudioFrameToFill >= pParentThreadData->m_AudioFrameToFill)
				{
					// wait for the parent to finish its frame
					pParentThreadData->m_AudioFillCond.wait(LockParent, [&pParentThreadData]() -> bool { return pParentThreadData->m_AudioFrameToFill == 0; });
				}
			}
			{
				std::unique_lock<std::mutex> LockAudio(pThreadData->m_AudioFillMutex);

				{
					const CLockScope LockScope(m_WriteLock);
					m_AudioStream.m_vpFrames[ThreadIndex]->pts = av_rescale_q(pThreadData->m_SampleCountStart, AVRational{1, m_AudioStream.m_pCodecContext->sample_rate}, m_AudioStream.m_pCodecContext->time_base);
					WriteFrame(&m_AudioStream, ThreadIndex);
				}

				pThreadData->m_AudioFrameToFill = 0;
				pThreadData->m_AudioFillCond.notify_all();
				pThreadData->m_Cond.notify_all();
			}
			m_ProcessingAudioFrame.fetch_sub(1);

			pThreadData->m_HasAudioFrame = false;
		}
	}
}

void CVideo::FillAudioFrame(size_t ThreadIndex)
{
	const int FillArrayResult = av_samples_fill_arrays(
		(uint8_t **)m_AudioStream.m_vpTmpFrames[ThreadIndex]->data,
		nullptr, // pointer to linesize (int*)
		(const uint8_t *)m_vAudioBuffers[ThreadIndex].m_aBuffer,
		2, // channels
		m_AudioStream.m_vpTmpFrames[ThreadIndex]->nb_samples,
		AV_SAMPLE_FMT_S16,
		0 // align
	);
	if(FillArrayResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(FillArrayResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not fill audio frame: %s", aError);
		return;
	}

	const int MakeWritableResult = av_frame_make_writable(m_AudioStream.m_vpFrames[ThreadIndex]);
	if(MakeWritableResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(MakeWritableResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not make audio frame writeable: %s", aError);
		return;
	}

	/* convert to destination format */
	const int ConvertResult = swr_convert(
		m_AudioStream.m_vpSwrContexts[ThreadIndex],
		m_AudioStream.m_vpFrames[ThreadIndex]->data,
		m_AudioStream.m_vpFrames[ThreadIndex]->nb_samples,
		(const uint8_t **)m_AudioStream.m_vpTmpFrames[ThreadIndex]->data,
		m_AudioStream.m_vpTmpFrames[ThreadIndex]->nb_samples);
	if(ConvertResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ConvertResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not convert audio frame: %s", aError);
		return;
	}
}

void CVideo::RunVideoConvertThread(size_t ThreadIndex)
{
	for(;;)
	{
		size_t SlotIndex;
		uint64_t FrameNumber;
		{
			std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);
			m_VideoSlotReadyCond.wait(Lock, [this]() -> bool { return !m_vPendingSlots.empty() || m_VideoStop || m_VideoPipelineError; });

			if(m_vPendingSlots.empty())
			{
				if(m_VideoStop || m_VideoPipelineError)
					return;
				continue; // 虚假唤醒
			}

			SlotIndex = m_vPendingSlots.front();
			m_vPendingSlots.pop_front();
			FrameNumber = m_vVideoSlots[SlotIndex].m_FrameNumber;
		}

		// 像素转换在队列锁外执行（sws 上下文与编码目标帧均为线程私有）
		FillVideoFrame(ThreadIndex, SlotIndex);

		{
			std::unique_lock<std::mutex> Lock(m_VideoQueueMutex);

			// 转换完成，槽内 RGBA 数据已消费，归还空闲栈供渲染端复用
			m_vFreeSlots.push_back(SlotIndex);
			m_VideoSlotFreeCond.notify_one();

			// 按帧号严格按序提交；编码错误置位后全部等待者丢弃退出（Stop 亦兜底唤醒）
			m_VideoSubmitCond.wait(Lock, [this, FrameNumber]() -> bool { return m_NextVideoFrameToSubmit == FrameNumber || m_VideoPipelineError; });
			if(m_VideoPipelineError)
				continue;

			{
				const CLockScope LockScope(m_WriteLock);
				// PTS 由本类维护的提交计数器生成，替代读取编码器内部 frame_num，
				// 保证 flush 阶段与取消路径的时间戳不受编码器状态影响。
				m_VideoStream.m_vpFrames[ThreadIndex]->pts = m_VideoFramesEncoded++;
				WriteFrame(&m_VideoStream, ThreadIndex);
			}

			m_NextVideoFrameToSubmit++;

			if(m_EncodeError.load(std::memory_order_acquire))
			{
				// 编码/封装失败：扩散到管线错误态并唤醒全部等待者，不再产出后续帧
				m_VideoPipelineError = true;
				m_VideoSubmitCond.notify_all();
				m_VideoSlotFreeCond.notify_all();
				m_VideoSlotReadyCond.notify_all();
				continue;
			}

			m_VideoSubmitCond.notify_all();
		}
	}
}

void CVideo::FillVideoFrame(size_t ThreadIndex, size_t SlotIndex)
{
	// 输入行长与行数来自渲染（readback）尺寸（M7 超采样下大于编码输出尺寸，由 swscale 缩小）。
	const int InLineSize = 4 * m_Width;
	auto *pRGBAData = m_vVideoSlots[SlotIndex].m_vRGBA.data();
	sws_scale(m_VideoStream.m_vpSwsContexts[ThreadIndex], (const uint8_t *const *)&pRGBAData, &InLineSize, 0,
		m_Height, m_VideoStream.m_vpFrames[ThreadIndex]->data, m_VideoStream.m_vpFrames[ThreadIndex]->linesize);
}

bool CVideo::UpdateVideoBufferFromGraphics(size_t SlotIndex)
{
	if(!m_FrameCapture.Read(m_pGraphics->GetReadPresentedImageDataFuncUnsafe(), m_vVideoSlots[SlotIndex].m_vRGBA))
	{
		log_error("videorecorder", "%s (expected %d x %d)", m_FrameCapture.ErrorDescription(), m_Width, m_Height);
		return false;
	}
	return true;
}

AVFrame *CVideo::AllocPicture(enum AVPixelFormat PixFmt, int Width, int Height)
{
	AVFrame *pPicture = av_frame_alloc();
	if(!pPicture)
	{
		log_error("videorecorder", "Could not allocate video frame");
		return nullptr;
	}

	pPicture->format = PixFmt;
	pPicture->width = Width;
	pPicture->height = Height;

	/* allocate the buffers for the frame data */
	const int FrameBufferAllocResult = av_frame_get_buffer(pPicture, 32);
	if(FrameBufferAllocResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(FrameBufferAllocResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not allocate video frame buffer: %s", aError);
		av_frame_free(&pPicture);
		return nullptr;
	}

	return pPicture;
}

AVFrame *CVideo::AllocAudioFrame(enum AVSampleFormat SampleFmt, uint64_t ChannelLayout, int SampleRate, int NbSamples)
{
	AVFrame *pFrame = av_frame_alloc();
	if(!pFrame)
	{
		log_error("videorecorder", "Could not allocate audio frame");
		return nullptr;
	}

	pFrame->format = SampleFmt;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
	dbg_assert(av_channel_layout_from_mask(&pFrame->ch_layout, ChannelLayout) == 0, "Failed to set channel layout");
#else
	pFrame->channel_layout = ChannelLayout;
#endif
	pFrame->sample_rate = SampleRate;
	pFrame->nb_samples = NbSamples;

	if(NbSamples)
	{
		const int FrameBufferAllocResult = av_frame_get_buffer(pFrame, 0);
		if(FrameBufferAllocResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(FrameBufferAllocResult, aError, sizeof(aError));
			log_error("videorecorder", "Could not allocate audio frame buffer: %s", aError);
			av_frame_free(&pFrame);
			return nullptr;
		}
	}

	return pFrame;
}

bool CVideo::OpenVideo()
{
	AVCodecContext *pContext = m_VideoStream.m_pCodecContext;
	AVDictionary *pOptions = nullptr;
	av_dict_copy(&pOptions, m_pOptDict, 0);

	/* open the codec */
	const int VideoOpenResult = avcodec_open2(pContext, m_pVideoCodec, &pOptions);
	av_dict_free(&pOptions);
	if(VideoOpenResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(VideoOpenResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not open video codec: %s", aError);
		return false;
	}

	m_VideoStream.m_vpFrames.clear();
	m_VideoStream.m_vpFrames.reserve(m_ConvertThreads);

	/* allocate and init a re-usable frame */
	for(size_t i = 0; i < m_ConvertThreads; ++i)
	{
		m_VideoStream.m_vpFrames.emplace_back(nullptr);
		m_VideoStream.m_vpFrames[i] = AllocPicture(pContext->pix_fmt, pContext->width, pContext->height);
		if(!m_VideoStream.m_vpFrames[i])
		{
			return false;
		}
	}

	/* The swscale context converts RGBA directly to the negotiated encoder pixel
	 * format (M6); no intermediate YUV420P buffer is needed. */
	m_VideoStream.m_vpTmpFrames.clear();
	m_VideoStream.m_vpTmpFrames.reserve(m_ConvertThreads);

	/* copy the stream parameters to the muxer */
	const int AudioStreamCopyResult = avcodec_parameters_from_context(m_VideoStream.m_pStream->codecpar, pContext);
	if(AudioStreamCopyResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(AudioStreamCopyResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not copy video stream parameters: %s", aError);
		return false;
	}
	m_VideoFrameIndex = 0;
	return true;
}

bool CVideo::OpenAudio()
{
	AVCodecContext *pContext = m_AudioStream.m_pCodecContext;
	AVDictionary *pOptions = nullptr;
	av_dict_copy(&pOptions, m_pOptDict, 0);

	/* open it */
	const int AudioOpenResult = avcodec_open2(pContext, m_pAudioCodec, &pOptions);
	av_dict_free(&pOptions);
	if(AudioOpenResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(AudioOpenResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not open audio codec: %s", aError);
		return false;
	}

	int NbSamples;
	if(pContext->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
		NbSamples = 10000;
	else
		NbSamples = pContext->frame_size;

	m_AudioStream.m_vpFrames.clear();
	m_AudioStream.m_vpFrames.reserve(m_AudioThreads);

	m_AudioStream.m_vpTmpFrames.clear();
	m_AudioStream.m_vpTmpFrames.reserve(m_AudioThreads);

	/* allocate and init a re-usable frame */
	for(size_t i = 0; i < m_AudioThreads; ++i)
	{
		m_AudioStream.m_vpFrames.emplace_back(nullptr);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
		m_AudioStream.m_vpFrames[i] = AllocAudioFrame(pContext->sample_fmt, pContext->ch_layout.u.mask, pContext->sample_rate, NbSamples);
#else
		m_AudioStream.m_vpFrames[i] = AllocAudioFrame(pContext->sample_fmt, pContext->channel_layout, pContext->sample_rate, NbSamples);
#endif
		if(!m_AudioStream.m_vpFrames[i])
		{
			return false;
		}

		m_AudioStream.m_vpTmpFrames.emplace_back(nullptr);
		m_AudioStream.m_vpTmpFrames[i] = AllocAudioFrame(AV_SAMPLE_FMT_S16, AV_CH_LAYOUT_STEREO, m_pSound->MixingRate(), NbSamples);
		if(!m_AudioStream.m_vpTmpFrames[i])
		{
			return false;
		}
	}

	/* copy the stream parameters to the muxer */
	const int AudioStreamCopyResult = avcodec_parameters_from_context(m_AudioStream.m_pStream->codecpar, pContext);
	if(AudioStreamCopyResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(AudioStreamCopyResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not copy audio stream parameters: %s", aError);
		return false;
	}

	/* create resampling context */
	m_AudioStream.m_vpSwrContexts.clear();
	m_AudioStream.m_vpSwrContexts.resize(m_AudioThreads);
	for(size_t i = 0; i < m_AudioThreads; ++i)
	{
		m_AudioStream.m_vpSwrContexts[i] = swr_alloc();
		if(!m_AudioStream.m_vpSwrContexts[i])
		{
			log_error("videorecorder", "Could not allocate resampling context");
			return false;
		}

		/* set options */
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
		dbg_assert(av_opt_set_chlayout(m_AudioStream.m_vpSwrContexts[i], "in_chlayout", &pContext->ch_layout, 0) == 0, "invalid option");
#else
		dbg_assert(av_opt_set_int(m_AudioStream.m_vpSwrContexts[i], "in_channel_count", pContext->channels, 0) == 0, "invalid option");
#endif
		if(av_opt_set_int(m_AudioStream.m_vpSwrContexts[i], "in_sample_rate", m_pSound->MixingRate(), 0) != 0)
		{
			log_error("videorecorder", "Could not set audio sample rate to %d", m_pSound->MixingRate());
			return false;
		}
		dbg_assert(av_opt_set_sample_fmt(m_AudioStream.m_vpSwrContexts[i], "in_sample_fmt", AV_SAMPLE_FMT_S16, 0) == 0, "invalid option");
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
		dbg_assert(av_opt_set_chlayout(m_AudioStream.m_vpSwrContexts[i], "out_chlayout", &pContext->ch_layout, 0) == 0, "invalid option");
#else
		dbg_assert(av_opt_set_int(m_AudioStream.m_vpSwrContexts[i], "out_channel_count", pContext->channels, 0) == 0, "invalid option");
#endif
		dbg_assert(av_opt_set_int(m_AudioStream.m_vpSwrContexts[i], "out_sample_rate", pContext->sample_rate, 0) == 0, "invalid option");
		dbg_assert(av_opt_set_sample_fmt(m_AudioStream.m_vpSwrContexts[i], "out_sample_fmt", pContext->sample_fmt, 0) == 0, "invalid option");

		/* initialize the resampling context */
		const int ResamplingContextInitResult = swr_init(m_AudioStream.m_vpSwrContexts[i]);
		if(ResamplingContextInitResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ResamplingContextInitResult, aError, sizeof(aError));
			log_error("videorecorder", "Could not initialize resampling context: %s", aError);
			return false;
		}
	}

	m_AudioFrameIndex = 0;
	return true;
}

/* Add an output stream. */
bool CVideo::AddStream(COutputStream *pStream, AVFormatContext *pFormatContext, const AVCodec **ppCodec, enum AVCodecID CodecId) const
{
	/* find the encoder */
	if(pStream == &m_VideoStream && g_Config.m_QmVideoEncoder[0] != '\0')
	{
		// 编码器按名称选择（M4）：视频流按流身份判断（不依赖 muxer 默认 codec 恰为 H264）；
		// 探测不可用时明确失败，不静默回退软件编码器。
		const video_encoder::SProbeResult *pProbed = video_encoder::FindProbed(g_Config.m_QmVideoEncoder);
		if(!pProbed)
		{
			log_error("videorecorder", "Video encoder '%s' is unknown; clear qm_video_encoder or pick one from the export settings.", g_Config.m_QmVideoEncoder);
			return false;
		}
		if(!pProbed->m_Available)
		{
			log_error("videorecorder", "Video encoder '%s' is not usable on this system (%s); clear qm_video_encoder or pick another encoder.", g_Config.m_QmVideoEncoder, pProbed->m_aFailReason);
			return false;
		}
		*ppCodec = avcodec_find_encoder_by_name(g_Config.m_QmVideoEncoder);
	}
	else
	{
		*ppCodec = avcodec_find_encoder(CodecId);
	}
	if(!(*ppCodec))
	{
		log_error("videorecorder", "Could not find encoder for codec '%s'", avcodec_get_name(CodecId));
		return false;
	}

	pStream->m_pStream = avformat_new_stream(pFormatContext, nullptr);
	if(!pStream->m_pStream)
	{
		log_error("videorecorder", "Could not allocate stream");
		return false;
	}
	pStream->m_pStream->id = pFormatContext->nb_streams - 1;
	AVCodecContext *pContext = avcodec_alloc_context3(*ppCodec);
	if(!pContext)
	{
		log_error("videorecorder", "Could not allocate encoding context");
		return false;
	}
	pStream->m_pCodecContext = pContext;

#if defined(CONF_ARCH_IA32) || defined(CONF_ARCH_ARM) || defined(CONF_ARCH_WASM)
	// use only 1 ffmpeg thread on 32-bit to save memory
	pContext->thread_count = 1;
#endif

	switch((*ppCodec)->type)
	{
	case AVMEDIA_TYPE_AUDIO:
	{
		const AVSampleFormat *pSampleFormats = nullptr;
		const int *pSampleRates = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
		avcodec_get_supported_config(pContext, *ppCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **)&pSampleFormats, nullptr);
		avcodec_get_supported_config(pContext, *ppCodec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void **)&pSampleRates, nullptr);
#else
		pSampleFormats = (*ppCodec)->sample_fmts;
		pSampleRates = (*ppCodec)->supported_samplerates;
#endif
		pContext->sample_fmt = pSampleFormats ? pSampleFormats[0] : AV_SAMPLE_FMT_FLTP;
		if(pSampleRates)
		{
			pContext->sample_rate = pSampleRates[0];
			for(int i = 0; pSampleRates[i]; i++)
			{
				if(pSampleRates[i] == m_pSound->MixingRate())
				{
					pContext->sample_rate = m_pSound->MixingRate();
					break;
				}
			}
		}
		else
		{
			pContext->sample_rate = m_pSound->MixingRate();
		}
		pContext->bit_rate = pContext->sample_rate * 2 * 16;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
		dbg_assert(av_channel_layout_from_mask(&pContext->ch_layout, AV_CH_LAYOUT_STEREO) == 0, "Failed to set channel layout");
#else
		pContext->channels = 2;
		pContext->channel_layout = AV_CH_LAYOUT_STEREO;
#endif

		pStream->m_pStream->time_base.num = 1;
		pStream->m_pStream->time_base.den = pContext->sample_rate;
		break;
	}

	case AVMEDIA_TYPE_VIDEO:
		pContext->codec_id = (*ppCodec)->id;

		pContext->bit_rate = 400000;
		/* Resolution must be a multiple of two (chroma-subsampled formats require even dims). */
		pContext->width = m_OutputWidth % 2 == 0 ? m_OutputWidth : m_OutputWidth - 1;
		pContext->height = m_OutputHeight % 2 == 0 ? m_OutputHeight : m_OutputHeight - 1;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		 * of which frame timestamps are represented. For fixed-fps content,
		 * timebase should be 1/framerate and timestamp increments should be
		 * identical to 1. */
		pStream->m_pStream->time_base.num = 1;
		pStream->m_pStream->time_base.den = m_FPS;
		pContext->time_base = pStream->m_pStream->time_base;
		// CFR 帧率元数据：matroska muxer 写 DefaultDuration 只看流级 avg_frame_rate
		// （缺失时段文件读回 avg_frame_rate=0/0，拼接网格无从武装）；codec 侧一并设置。
		pStream->m_pStream->avg_frame_rate = AVRational{m_FPS, 1};
		pContext->framerate = AVRational{m_FPS, 1};

		// GOP 可配置（M4）：默认 60（原 12 为 FFmpeg 示例遗留，偏小）。
		pContext->gop_size = g_Config.m_QmVideoGop > 0 ? g_Config.m_QmVideoGop : video_encoder::DEFAULT_GOP;
		// 分段拼接要求无 B 帧（仅限分段导出；整段导出保留 B 帧压缩收益）：
		// matroska 段不存储 dts（只有 block timecode），demuxer 对段首 B 帧序列给出
		// dts=AV_NOPTS_VALUE；拼接时 muxer 需经重排序窗口从 pts 推导 dts，而该窗口
		// 在段边界携带上一段尾部状态，导致段 2 首包推导出倒退 dts（EINVAL）。
		// 无 B 帧时 pts==dts==解码序，网格严格单调，拼接天然无重排序。
		if(g_Config.m_QmVideoSegmentSeconds > 0)
			pContext->max_b_frames = 0;
		// 像素格式协商（M4/M6）：按编码器家族的期望列表协商（剪辑格式 422 10bit、FFV1 444 优先）。
		pContext->pix_fmt = video_encoder::NegotiatePixelFormat(*ppCodec, pContext, video_encoder::WantedPixelFormatsFor((*ppCodec)->name));
		if(pContext->pix_fmt == AV_PIX_FMT_NONE)
		{
			log_error("videorecorder", "Encoder '%s' does not support any usable pixel format", (*ppCodec)->name);
			return false;
		}
		pContext->colorspace = COLOR_SPACE;
		// 完整色彩元数据（M6）：剪辑软件（Premiere/Resolve）导入颜色一致所需，
		// 容器写 colr/nclx 标记。导出管线 sRGB → BT.709 limited range。
		pContext->color_primaries = AVCOL_PRI_BT709;
		pContext->color_trc = AVCOL_TRC_BT709;
		pContext->color_range = AVCOL_RANGE_MPEG;
		if(pContext->codec_id == AV_CODEC_ID_MPEG2VIDEO)
		{
			/* just for testing, we also add B-frames */
			pContext->max_b_frames = 2;
		}
		if(pContext->codec_id == AV_CODEC_ID_MPEG1VIDEO)
		{
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			 * This does not happen with normal video, it just happens here as
			 * the motion of the chroma plane does not match the luma plane. */
			pContext->mb_decision = 2;
		}
		// 质量参数按编码器家族映射（M4）：x264 用 CRF/preset，NVENC 用 CQ，AMF/QSV/VideoToolbox 用各自参数。
		// qm_video_quality >= 0 时为统一质量值，-1 沿用 cl_video_crf（既有行为）。
		// 选项被编码器拒绝时明确失败（原 dbg_assert 会在非法组合下崩溃）。
		{
			const int QualityValue = g_Config.m_QmVideoQuality >= 0 ? g_Config.m_QmVideoQuality : g_Config.m_ClVideoX264Crf;
			int QualityKind = (int)video_encoder::EQualityKind::NONE;
			if(!video_encoder::ApplyQualityOptions(pContext, (*ppCodec)->name, QualityValue, g_Config.m_ClVideoX264Preset, QualityKind))
			{
				log_error("videorecorder", "Encoder '%s' rejected the quality options (quality %d, preset %d)", (*ppCodec)->name, QualityValue, g_Config.m_ClVideoX264Preset);
				return false;
			}
		}
		break;

	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if(pFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		pContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	return true;
}

int CVideo::SendFrameDraining(COutputStream *pStream, AVFrame *pFrame, AVPacket *pPacket, const char *pContext)
{
	int SendResult = avcodec_send_frame(pStream->m_pCodecContext, pFrame);
	while(SendResult == AVERROR(EAGAIN))
	{
		// 编码器输出未排空：先收走已就绪的包再重试提交（FFmpeg 编码 API 背压约定）。
		if(!ReceiveAndWritePackets(pStream, pPacket, pContext))
			return SendResult;
		SendResult = avcodec_send_frame(pStream->m_pCodecContext, pFrame);
	}
	// flush 语义：进入 drain 状态后再次提交空帧返回 EOF 表示已无输出，属正常终止。
	if(pFrame == nullptr && SendResult == AVERROR_EOF)
		return 0;
	return SendResult;
}

bool CVideo::ReceiveAndWritePackets(COutputStream *pStream, AVPacket *pPacket, const char *pContext)
{
	while(true)
	{
		const int RecvResult = avcodec_receive_packet(pStream->m_pCodecContext, pPacket);
		if(RecvResult == AVERROR(EAGAIN) || RecvResult == AVERROR_EOF)
			return true;
		if(RecvResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(RecvResult, aError, sizeof(aError));
			log_error("videorecorder", "Could not receive %s packet from encoder: %s", pContext, aError);
			m_EncodeError.store(true, std::memory_order_release);
			return false;
		}

		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(pPacket, pStream->m_pCodecContext->time_base, pStream->m_pStream->time_base);
		pPacket->stream_index = pStream->m_pStream->index;

		const int WriteFrameResult = av_interleaved_write_frame(m_pFormatContext, pPacket);
		if(WriteFrameResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(WriteFrameResult, aError, sizeof(aError));
			log_error("videorecorder", "Could not write %s packet to container: %s", pContext, aError);
			m_EncodeError.store(true, std::memory_order_release);
			return false;
		}
	}
}

void CVideo::WriteFrame(COutputStream *pStream, size_t ThreadIndex)
{
	AVPacket *pPacket = av_packet_alloc();
	if(pPacket == nullptr)
	{
		log_error("videorecorder", "Could not allocate packet");
		m_EncodeError.store(true, std::memory_order_release);
		return;
	}

	pPacket->data = nullptr;
	pPacket->size = 0;

	const char *pContext = pStream == &m_VideoStream ? "video" : "audio";
	const int SendResult = SendFrameDraining(pStream, pStream->m_vpFrames[ThreadIndex], pPacket, pContext);
	if(SendResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(SendResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not send %s frame to encoder: %s", pContext, aError);
		m_EncodeError.store(true, std::memory_order_release);
	}
	else
	{
		ReceiveAndWritePackets(pStream, pPacket, pContext);
	}

	av_packet_free(&pPacket);
}

void CVideo::FinishFrames(COutputStream *pStream)
{
	if(!pStream->m_pCodecContext || !avcodec_is_open(pStream->m_pCodecContext))
	{
		return;
	}

	AVPacket *pPacket = av_packet_alloc();
	if(pPacket == nullptr)
	{
		log_error("videorecorder", "Could not allocate packet");
		m_EncodeError.store(true, std::memory_order_release);
		return;
	}

	pPacket->data = nullptr;
	pPacket->size = 0;

	const int SendResult = SendFrameDraining(pStream, nullptr, pPacket, "flushed");
	if(SendResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(SendResult, aError, sizeof(aError));
		log_error("videorecorder", "Could not flush encoder: %s", aError);
		m_EncodeError.store(true, std::memory_order_release);
	}
	else
	{
		ReceiveAndWritePackets(pStream, pPacket, "flushed");
	}

	av_packet_free(&pPacket);
}

void CVideo::CloseStream(COutputStream *pStream)
{
	avcodec_free_context(&pStream->m_pCodecContext);

	for(auto *pFrame : pStream->m_vpFrames)
		av_frame_free(&pFrame);
	pStream->m_vpFrames.clear();

	for(auto *pFrame : pStream->m_vpTmpFrames)
		av_frame_free(&pFrame);
	pStream->m_vpTmpFrames.clear();

	for(auto *pSwsContext : pStream->m_vpSwsContexts)
		sws_freeContext(pSwsContext);
	pStream->m_vpSwsContexts.clear();

	for(auto *pSwrContext : pStream->m_vpSwrContexts)
		swr_free(&pSwrContext);
	pStream->m_vpSwrContexts.clear();
}

#endif
