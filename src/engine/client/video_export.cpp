#include "video_export.h"

#include <base/log.h>

#include <engine/shared/protocol.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <cstdio>
#include <cstring>

// 段切分：按 SERVER_TICK_SPEED 把 [FirstTick, LastTick) 切成固定时长片段。
std::vector<CVideoExportTask::CSegment> CVideoExportTask::PlanSegments(int FirstTick, int LastTick, int SegmentSeconds)
{
	std::vector<CSegment> vSegments;
	if(LastTick <= FirstTick)
		return vSegments;

	int TicksPerSegment = SegmentSeconds <= 0 ? 0 : SegmentSeconds * SERVER_TICK_SPEED;
	if(TicksPerSegment <= 0 || LastTick - FirstTick <= TicksPerSegment)
	{
		CSegment All;
		All.m_StartTick = FirstTick;
		All.m_EndTick = LastTick;
		vSegments.push_back(All);
		return vSegments;
	}

	for(int Start = FirstTick; Start < LastTick; Start += TicksPerSegment)
	{
		CSegment Segment;
		Segment.m_StartTick = Start;
		Segment.m_EndTick = std::min(Start + TicksPerSegment, LastTick);
		vSegments.push_back(Segment);
	}
	return vSegments;
}

CVideoExportTask::CVideoExportTask(const char *pName, const char *pDemoPath, int FirstTick, int LastTick, int SegmentSeconds,
	int Fps, int Width, int Height, int Crf, int Preset, int WarmupTicks) :
	m_FirstTick(FirstTick),
	m_LastTick(LastTick),
	m_WarmupTicks(WarmupTicks),
	m_Fps(Fps),
	m_Width(Width),
	m_Height(Height),
	m_Crf(Crf),
	m_Preset(Preset),
	m_SegmentSeconds(SegmentSeconds)
{
	str_copy(m_aName, pName);
	str_copy(m_aDemoPath, pDemoPath);
	m_vSegments = PlanSegments(FirstTick, LastTick, SegmentSeconds);
	m_aFingerprint[0] = '\0';
}

void CVideoExportTask::ComputeFingerprint(uint64_t DemoFileSize, int64_t DemoMtime)
{
	char aInput[1024];
	str_format(aInput, sizeof(aInput), "%" PRIu64 "|%" PRIi64 "|%s|%d|%dx%d|%d|%d|%d",
		DemoFileSize, DemoMtime, m_aDemoPath, m_Fps, m_Width, m_Height, m_Crf, m_Preset, m_SegmentSeconds);
	MD5_DIGEST Digest = md5(aInput, str_length(aInput));
	md5_str(Digest, m_aFingerprint, sizeof(m_aFingerprint));
}

bool CVideoExportTask::AllDone() const
{
	for(const CSegment &Segment : m_vSegments)
	{
		if(!Segment.m_Done)
			return false;
	}
	return !m_vSegments.empty();
}

int CVideoExportTask::NextPendingSegment() const
{
	for(size_t i = 0; i < m_vSegments.size(); ++i)
	{
		if(!m_vSegments[i].m_Done)
			return (int)i;
	}
	return -1;
}

void CVideoExportTask::SegmentFilename(int SegmentIndex, char *pBuffer, size_t BufferSize) const
{
	str_format(pBuffer, BufferSize, "%s_p%03d", m_aName, SegmentIndex + 1);
}

void CVideoExportTask::FinalOutputName(char *pBuffer, size_t BufferSize) const
{
	str_copy(pBuffer, m_aName, BufferSize);
}

// 进度文件：videos/<name>.progress，一行一个 key=value 的纯文本键值对。
bool CVideoExportTask::LoadProgress(const char *pProgressPath)
{
	IOHANDLE File = io_open(pProgressPath, IOFLAG_READ);
	if(!File)
		return false;

	char *pContents = io_read_all_str(File);
	io_close(File);
	if(!pContents)
		return false;

	bool VersionOk = false;
	bool FingerprintOk = false;
	std::vector<bool> vDone(m_vSegments.size(), false);
	// 逐行扫描（\n 或 \r\n 分隔），每行格式 key=value
	char *pLine = pContents;
	while(pLine && *pLine)
	{
		char *pEnd = strchr(pLine, '\n');
		if(pEnd)
			*pEnd = '\0';
		if(size_t Len = strlen(pLine); Len > 0 && pLine[Len - 1] == '\r')
			pLine[Len - 1] = '\0';

		const char *pEq = str_find(pLine, "=");
		if(pEq)
		{
			char aKey[64];
			str_truncate(aKey, sizeof(aKey), pLine, pEq - pLine);
			const char *pValue = pEq + 1;
			if(str_comp(aKey, "qmexport_version") == 0)
			{
				VersionOk = str_toint(pValue) == 1;
			}
			else if(str_comp(aKey, "fingerprint") == 0)
			{
				FingerprintOk = str_length(m_aFingerprint) > 0 && str_comp(pValue, m_aFingerprint) == 0;
			}
			else if(str_comp(aKey, "segment_count") == 0)
			{
				if(str_toint(pValue) != (int)m_vSegments.size())
				{
					free(pContents);
					return false; // 段数不一致（参数变更），整单重置
				}
			}
			else if(str_startswith(aKey, "segment_") && str_endswith(aKey, "_done"))
			{
				const int Index = str_toint(aKey + str_length("segment_"));
				if(Index >= 0 && Index < (int)vDone.size())
					vDone[Index] = str_toint(pValue) != 0;
			}
		}

		pLine = pEnd ? pEnd + 1 : nullptr;
	}
	free(pContents);

	if(!VersionOk || !FingerprintOk)
		return false;

	for(size_t i = 0; i < m_vSegments.size(); ++i)
		m_vSegments[i].m_Done = vDone[i];
	return true;
}

bool CVideoExportTask::SaveProgress(const char *pProgressPath) const
{
	IOHANDLE File = io_open(pProgressPath, IOFLAG_WRITE);
	if(!File)
	{
		log_error("video_export", "Could not write progress file '%s'", pProgressPath);
		return false;
	}

	char aLine[256];
	str_copy(aLine, "qmexport_version=1\n");
	io_write(File, aLine, str_length(aLine));
	str_format(aLine, sizeof(aLine), "fingerprint=%s\n", m_aFingerprint);
	io_write(File, aLine, str_length(aLine));
	str_format(aLine, sizeof(aLine), "segment_count=%d\n", (int)m_vSegments.size());
	io_write(File, aLine, str_length(aLine));
	for(size_t i = 0; i < m_vSegments.size(); ++i)
	{
		str_format(aLine, sizeof(aLine), "segment_%d_done=%d\n", (int)i, m_vSegments[i].m_Done ? 1 : 0);
		io_write(File, aLine, str_length(aLine));
	}
	io_close(File);
	return true;
}

CVideoExportTask::EAction CVideoExportTask::UpdatePlayback(int CurrentTick)
{
	if(m_State == EState::IDLE || m_CurrentSegment < 0 || m_CurrentSegment >= (int)m_vSegments.size())
		return ACT_NONE;

	const CSegment &Segment = m_vSegments[m_CurrentSegment];
	if(m_State == EState::WARMING_UP)
	{
		if(CurrentTick >= Segment.m_StartTick)
			return ACT_START_RECORD;
	}
	else if(m_State == EState::RECORDING)
	{
		if(CurrentTick >= Segment.m_EndTick)
			return ACT_STOP_RECORD;
	}
	return ACT_NONE;
}

void CVideoExportTask::OnRecordingStopped()
{
	// 推进到下一个待渲染段；CClient 依据状态决定 seek 预热或收尾拼接。
	m_CurrentSegment = NextPendingSegment();
	if(m_CurrentSegment >= 0)
		m_State = EState::WARMING_UP;
	else
		m_State = EState::IDLE;
}

// 段文件完整性验证：容器可打开且含视频流即认为该段封装完整（可拼接）。
bool CVideoExportTask::VerifySegmentFile(const char *pPath)
{
	AVFormatContext *pContext = nullptr;
	const int OpenResult = avformat_open_input(&pContext, pPath, nullptr, nullptr);
	if(OpenResult < 0)
		return false;
	bool HasVideo = false;
	for(size_t i = 0; i < pContext->nb_streams; ++i)
	{
		if(pContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			HasVideo = true;
	}
	avformat_close_input(&pContext);
	return HasVideo;
}

// —— 流复制拼接：把参数一致的片段按顺序拷贝封装为单个输出文件。——
// 阶段一：以首个片段为基准建立输出流并写入表头；阶段二：逐段拷贝包并平移时间戳。
// 时间轴：CFR 网格步长已知时按「包序 × 步长」重建 dts（matroska 段首缺 dts 不丢包），
// pts 保留真实值；步长未知时回退「段首 dts 归零 + 全局累计偏移」。流参数不一致时失败（调用方保留独立片段）。
bool ConcatVideoFiles(const std::vector<std::string> &vInputs, const char *pOutput, const char *pFormatName)
{
	if(vInputs.empty())
		return false;

	AVFormatContext *pOutContext = nullptr;
	const int AllocResult = avformat_alloc_output_context2(&pOutContext, nullptr, pFormatName, pOutput);
	if(AllocResult < 0 || !pOutContext)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(AllocResult, aError, sizeof(aError));
		log_error("video_export", "Could not allocate concat output context: %s", aError);
		return false;
	}

	// 阶段一：用首个片段建立输出流结构
	AVStream *pOutVideo = nullptr;
	AVStream *pOutAudio = nullptr;
	{
		AVFormatContext *pFirstContext = nullptr;
		const int OpenResult = avformat_open_input(&pFirstContext, vInputs[0].c_str(), nullptr, nullptr);
		if(OpenResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(OpenResult, aError, sizeof(aError));
			log_error("video_export", "Could not open first segment '%s': %s", vInputs[0].c_str(), aError);
			avformat_free_context(pOutContext);
			return false;
		}
		// 与 ffmpeg CLI 流复制一致：补齐流信息（旧段无 DefaultDuration 时由解码推断帧率；
		// matroska demuxer 的 dts 生成策略也依赖它）
		if(avformat_find_stream_info(pFirstContext, nullptr) < 0)
			log_warn("video_export", "Could not probe stream info of '%s'; concatenating with header metadata only", vInputs[0].c_str());

		for(size_t i = 0; i < pFirstContext->nb_streams; ++i)
		{
			const AVStream *pInStream = pFirstContext->streams[i];
			if(pInStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !pOutVideo)
			{
				pOutVideo = avformat_new_stream(pOutContext, nullptr);
				if(!pOutVideo || avcodec_parameters_copy(pOutVideo->codecpar, pInStream->codecpar) < 0)
				{
					avformat_close_input(&pFirstContext);
					avformat_free_context(pOutContext);
					return false;
				}
				pOutVideo->codecpar->codec_tag = 0;
				pOutVideo->time_base = pInStream->time_base;
			}
			else if(pInStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !pOutAudio)
			{
				pOutAudio = avformat_new_stream(pOutContext, nullptr);
				if(!pOutAudio || avcodec_parameters_copy(pOutAudio->codecpar, pInStream->codecpar) < 0)
				{
					avformat_close_input(&pFirstContext);
					avformat_free_context(pOutContext);
					return false;
				}
				pOutAudio->codecpar->codec_tag = 0;
				pOutAudio->time_base = pInStream->time_base;
			}
		}
		avformat_close_input(&pFirstContext);

		if(!pOutVideo && !pOutAudio)
		{
			log_error("video_export", "First segment '%s' contains no streams", vInputs[0].c_str());
			avformat_free_context(pOutContext);
			return false;
		}
	}

	// 打开输出文件并写表头（全部流已就绪）
	if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
	{
		const int OpenFileResult = avio_open(&pOutContext->pb, pOutput, AVIO_FLAG_WRITE);
		if(OpenFileResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(OpenFileResult, aError, sizeof(aError));
			log_error("video_export", "Could not open concat output '%s': %s", pOutput, aError);
			avformat_free_context(pOutContext);
			return false;
		}
	}
	const int HeaderResult = avformat_write_header(pOutContext, nullptr);
	if(HeaderResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(HeaderResult, aError, sizeof(aError));
		log_error("video_export", "Could not write concat header: %s", aError);
		if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
			avio_closep(&pOutContext->pb);
		avformat_free_context(pOutContext);
		return false;
	}

	// 阶段二：逐段拷贝
	AVPacket *pPacket = av_packet_alloc();
	if(!pPacket)
	{
		if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
			avio_closep(&pOutContext->pb);
		avformat_free_context(pOutContext);
		return false;
	}

	int64_t VideoOffset = 0;
	int64_t AudioOffset = 0;
	bool AnyFailure = false;
	for(const std::string &Input : vInputs)
	{
		AVFormatContext *pInContext = nullptr;
		const int OpenResult = avformat_open_input(&pInContext, Input.c_str(), nullptr, nullptr);
		if(OpenResult < 0)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(OpenResult, aError, sizeof(aError));
			log_error("video_export", "Could not open segment '%s': %s", Input.c_str(), aError);
			AnyFailure = true;
			break;
		}
		if(avformat_find_stream_info(pInContext, nullptr) < 0)
			log_warn("video_export", "Could not probe stream info of '%s'; concatenating with header metadata only", Input.c_str());

		AVStream *pInVideo = nullptr;
		AVStream *pInAudio = nullptr;
		for(size_t i = 0; i < pInContext->nb_streams; ++i)
		{
			if(pInContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !pInVideo)
				pInVideo = pInContext->streams[i];
			else if(pInContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !pInAudio)
				pInAudio = pInContext->streams[i];
		}

		// 流参数一致性校验（不一致时保留独立片段由调用方处理）
		const bool VideoMismatch = (pInVideo == nullptr) != (pOutVideo == nullptr) ||
					   (pInVideo && (pInVideo->codecpar->codec_id != pOutVideo->codecpar->codec_id ||
								pInVideo->codecpar->width != pOutVideo->codecpar->width ||
								pInVideo->codecpar->height != pOutVideo->codecpar->height));
		const bool AudioMismatch = (pInAudio == nullptr) != (pOutAudio == nullptr) ||
					   (pInAudio && (pInAudio->codecpar->codec_id != pOutAudio->codecpar->codec_id ||
								pInAudio->codecpar->sample_rate != pOutAudio->codecpar->sample_rate));
		if(VideoMismatch || AudioMismatch)
		{
			log_error("video_export", "Segment '%s' stream parameters do not match the first segment", Input.c_str());
			avformat_close_input(&pInContext);
			AnyFailure = true;
			break;
		}

		// 段间偏移步长（CFR 网格）：视频取帧率倒数的“有理数”形式（mkv 流时基 1ms 粗于帧间隔，
		// 必须逐包 av_rescale_q 精确重标定，整数步长会累积 1% 段长漂移）；音频按包序号 × frame_size。
		// 步长未知时回退「段内以段首 dts 为原点归零 + 全局累计偏移」的平移逻辑。
		AVRational VideoFrameStep = {0, 0}; // 帧间隔（秒），num>0 = 网格可用
		int AudioSamplesPerPacket = 0;
		int AudioSampleRate = 0;
		// 只认 avg_frame_rate（新段由 DefaultDuration 权威提供）。find_stream_info 对无
		// DefaultDuration 的旧段会用解码猜出 r_frame_rate（实测 60fps 被猜成 62.5），
		// 用它做步长会累积段长漂移——此类段走 pts 锚定回退路径。
		if(pOutVideo && pInVideo && pInVideo->avg_frame_rate.num > 0)
			VideoFrameStep = av_inv_q(pInVideo->avg_frame_rate);
		if(pOutAudio && pInAudio)
		{
			AudioSamplesPerPacket = pInAudio->codecpar->frame_size;
			AudioSampleRate = pInAudio->codecpar->sample_rate;
		}

		// 网格重建的背景：matroska 段不存 dts，demuxer 对段首 B 帧序列常给出 dts=NOPTS，
		// 「dts 平移」逻辑会静默丢包。网格路径忽略输入 dts，按包序重建严格单调 dts，
		// pts 保留真实值（B 帧显示顺序不破坏）并以段首 pts 为原点平移。
		const bool VideoGrid = pOutVideo && pInVideo && VideoFrameStep.num > 0;
		const bool AudioGrid = pOutAudio && pInAudio && AudioSamplesPerPacket > 0 && AudioSampleRate > 0;
		int64_t VideoIndex = 0;
		int64_t AudioIndex = 0;
		int64_t BaseVideoDts = AV_NOPTS_VALUE; // 回退路径用
		int64_t BaseAudioDts = AV_NOPTS_VALUE;
		int64_t FirstVideoPts = AV_NOPTS_VALUE;
		int64_t FirstAudioPts = AV_NOPTS_VALUE;
		int64_t LastVideoDts = AV_NOPTS_VALUE;
		int64_t LastAudioDts = AV_NOPTS_VALUE;
		int ReadResult = 0;
		while(!AnyFailure && (ReadResult = av_read_frame(pInContext, pPacket)) >= 0)
		{
			AVStream *pInStream = pInContext->streams[pPacket->stream_index];
			AVStream *pOutStream = nullptr;
			bool IsVideo = false;
			bool Grid = false;
			int64_t *pOffset = nullptr;
			int64_t *pBaseDts = nullptr;
			int64_t *pFirstPts = nullptr;
			int64_t *pLastDts = nullptr;
			int64_t *pIndex = nullptr;
			if(pInVideo && pInStream->index == pInVideo->index && pOutVideo)
			{
				pOutStream = pOutVideo;
				IsVideo = true;
				Grid = VideoGrid;
				pOffset = &VideoOffset;
				pBaseDts = &BaseVideoDts;
				pFirstPts = &FirstVideoPts;
				pLastDts = &LastVideoDts;
				pIndex = &VideoIndex;
			}
			else if(pInAudio && pInStream->index == pInAudio->index && pOutAudio)
			{
				pOutStream = pOutAudio;
				Grid = AudioGrid;
				pOffset = &AudioOffset;
				pBaseDts = &BaseAudioDts;
				pFirstPts = &FirstAudioPts;
				pLastDts = &LastAudioDts;
				pIndex = &AudioIndex;
			}
			else
			{
				av_packet_unref(pPacket);
				continue;
			}

			if(IsVideo && Grid && *pIndex == 0)
				log_info("video_export", "Concat first video packet of '%s': raw pts=%lld dts=%lld flags=%d; step=%d/%d out_tb=%d/%d",
					Input.c_str(), (long long)pPacket->pts, (long long)pPacket->dts, pPacket->flags,
					VideoFrameStep.num, VideoFrameStep.den, pOutStream->time_base.num, pOutStream->time_base.den);

			// 单位转换到输出流时基，再按路径处理时间戳。
			// dts 一律不合成：NOPTS 保持 NOPTS，muxer 按输出流 video_delay（find_stream_info
			// 经参数拷贝带入）用重排序窗口从 pts 推导（与 ffmpeg CLI 流复制同机制）；
			// 自行按包序合成 dts 会与 B 帧真实 pts 冲突（pts < dts → muxer EINVAL）。
			av_packet_rescale_ts(pPacket, pInStream->time_base, pOutStream->time_base);
			if(Grid)
			{
				if(*pFirstPts == AV_NOPTS_VALUE && pPacket->pts != AV_NOPTS_VALUE)
					*pFirstPts = pPacket->pts;
				const int64_t PtsShift = *pOffset - (*pFirstPts != AV_NOPTS_VALUE ? *pFirstPts : 0);
				if(pPacket->pts == AV_NOPTS_VALUE && pPacket->dts == AV_NOPTS_VALUE)
				{
					// pts/dts 双缺（段首偶发的无时间戳小包）：放到当前网格槽位，
					// 保持后续包时间轴连续（保留包内容，不丢帧）
					int64_t GridSlot;
					if(IsVideo)
						GridSlot = *pOffset + av_rescale_q(*pIndex, VideoFrameStep, pOutStream->time_base);
					else
						GridSlot = *pOffset + av_rescale_q(*pIndex * AudioSamplesPerPacket,
									      AVRational{1, AudioSampleRate}, pOutStream->time_base);
					pPacket->pts = GridSlot;
					pPacket->dts = GridSlot;
				}
				else
				{
					if(pPacket->pts != AV_NOPTS_VALUE)
						pPacket->pts += PtsShift;
					if(pPacket->dts != AV_NOPTS_VALUE)
						pPacket->dts += PtsShift; // 与 pts 同步平移，保持既有 pts/dts 关系
				}
				(*pIndex)++;
			}
			else
			{
				// 回退路径（步长未知，如旧版 mkv 段）：锚定 pts——matroska 段可能整流无 dts。
				// dts 必须原样透传（NOPTS 允许，与 ffmpeg CLI 流复制一致）：按 pts 重建 dts 会产生
				// 解码序倒退（B 帧 pts 非单调），FFmpeg 9 interleaved mux 对 dts 倒退硬性 EINVAL。
				if(*pBaseDts == AV_NOPTS_VALUE)
					*pBaseDts = pPacket->pts != AV_NOPTS_VALUE ? pPacket->pts : pPacket->dts;
				if(*pBaseDts == AV_NOPTS_VALUE)
				{
					// pts/dts 全缺：无法定位时间轴，丢弃（正常导出段不会出现）
					av_packet_unref(pPacket);
					continue;
				}
				const int64_t Shift = *pOffset - *pBaseDts;
				if(pPacket->pts != AV_NOPTS_VALUE)
				{
					pPacket->pts += Shift;
					// 段末以最大 pts 定义（dts 可能全缺且 B 帧解码序 pts 非单调）
					if(*pLastDts == AV_NOPTS_VALUE || pPacket->pts > *pLastDts)
						*pLastDts = pPacket->pts;
				}
				if(pPacket->dts != AV_NOPTS_VALUE)
					pPacket->dts += Shift; // 有 dts 的包同步平移保持自洽
			}

			pPacket->stream_index = pOutStream->index;
			pPacket->pos = -1;

			const int WriteResult = av_interleaved_write_frame(pOutContext, pPacket);
			if(WriteResult < 0)
			{
				char aError[AV_ERROR_MAX_STRING_SIZE];
				av_strerror(WriteResult, aError, sizeof(aError));
				log_error("video_export", "Could not write concat packet from '%s': %s (stream %d, pts=%lld dts=%lld, video pkts so far %d)",
					Input.c_str(), aError, (int)pPacket->stream_index,
					(long long)pPacket->pts, (long long)pPacket->dts,
					(int)VideoIndex);
				AnyFailure = true;
			}
		}

		if(!AnyFailure)
		{
			log_info("video_export", "Concat '%s': video=%d pkts (grid %d), audio=%d pkts (grid %d)",
				Input.c_str(), (int)VideoIndex, VideoGrid ? 1 : 0, (int)AudioIndex, AudioGrid ? 1 : 0);
			// 段间偏移推进：网格路径 = 精确有理重标定的段内容时长（包数 × 步长）；
			// 回退路径 = 本段末包全局 dts + 1（LastDts 已含段内偏移；步长未知时的旧保底行为）。
			if(pOutVideo)
			{
				if(VideoGrid && VideoIndex > 0)
					VideoOffset += av_rescale_q(VideoIndex, VideoFrameStep, pOutVideo->time_base);
				else if(LastVideoDts != AV_NOPTS_VALUE)
					VideoOffset = LastVideoDts + 1;
			}
			if(pOutAudio)
			{
				if(AudioGrid && AudioIndex > 0)
					AudioOffset += av_rescale_q(AudioIndex * AudioSamplesPerPacket,
						AVRational{1, AudioSampleRate}, pOutAudio->time_base);
				else if(LastAudioDts != AV_NOPTS_VALUE)
					AudioOffset = LastAudioDts + 1;
			}
		}
		avformat_close_input(&pInContext);
		if(AnyFailure)
			break;

		// 读循环异常终止（非正常 EOF）：段文件损坏，宁失败不产出静默截断的输出
		if(ReadResult != AVERROR_EOF)
		{
			char aError[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ReadResult, aError, sizeof(aError));
			log_error("video_export", "Error reading segment '%s' (output discarded): %s", Input.c_str(), aError);
			AnyFailure = true;
			break;
		}
	}

	av_packet_free(&pPacket);
	if(AnyFailure)
	{
		if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
			avio_closep(&pOutContext->pb);
		avformat_free_context(pOutContext);
		return false;
	}

	const int TrailerResult = av_write_trailer(pOutContext);
	if(TrailerResult < 0)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(TrailerResult, aError, sizeof(aError));
		log_error("video_export", "Could not write concat trailer: %s", aError);
		if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
			avio_closep(&pOutContext->pb);
		avformat_free_context(pOutContext);
		return false;
	}

	if(!(pOutContext->oformat->flags & AVFMT_NOFILE))
		avio_closep(&pOutContext->pb);
	avformat_free_context(pOutContext);
	log_info("video_export", "Concatenated %d segment(s) into '%s'", (int)vInputs.size(), pOutput);
	return true;
}
