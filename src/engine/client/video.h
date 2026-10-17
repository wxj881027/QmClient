#ifndef ENGINE_CLIENT_VIDEO_H
#define ENGINE_CLIENT_VIDEO_H

#include <base/lock.h>

#include <engine/client/video_frame_capture.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
};

#include <engine/shared/video.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class IGraphics;
class ISound;
class IStorage;

// a wrapper around a single output AVStream
class COutputStream
{
public:
	AVStream *m_pStream = nullptr;
	AVCodecContext *m_pCodecContext = nullptr;

	/* pts of the next frame that will be generated */
	int64_t m_SamplesCount = 0;
	int64_t m_SamplesFrameCount = 0;

	std::vector<AVFrame *> m_vpFrames;
	std::vector<AVFrame *> m_vpTmpFrames;

	std::vector<struct SwsContext *> m_vpSwsContexts;
	std::vector<struct SwrContext *> m_vpSwrContexts;
};

class CVideo : public IVideo
{
public:
	// Width/Height 是渲染（readback）尺寸；OutputWidth/OutputHeight 是编码输出尺寸，
	// 0 表示跟随渲染尺寸。输出小于渲染时 swscale 以高质量滤波下采样（超采样抗锯齿，M7）。
	CVideo(IGraphics *pGraphics, ISound *pSound, IStorage *pStorage, int Width, int Height, const char *pName, int OutputWidth = 0, int OutputHeight = 0);
	~CVideo() override;

	bool Start() override REQUIRES(!m_WriteLock);
	void Stop() override;
	void Pause(bool Pause) override;
	bool IsRecording() const override { return m_Recording && !HasError(); }
	bool HasError() const override { return m_FrameCapture.HasError() || m_EncodeError.load(std::memory_order_acquire); }

	void NextVideoFrame() override;
	void NextVideoFrameThread() override;

	void NextAudioFrame(ISoundMixFunc Mix) override;
	void NextAudioFrameTimeline(ISoundMixFunc Mix) override;

	static void Init();

private:
	void RunVideoConvertThread(size_t ThreadIndex) REQUIRES(!m_WriteLock);
	void FillVideoFrame(size_t ThreadIndex, size_t SlotIndex) REQUIRES(!m_WriteLock);
	bool UpdateVideoBufferFromGraphics(size_t SlotIndex);

	void RunAudioThread(size_t ParentThreadIndex, size_t ThreadIndex) REQUIRES(!m_WriteLock);
	void FillAudioFrame(size_t ThreadIndex);

	bool OpenVideo();
	bool OpenAudio();
	AVFrame *AllocPicture(enum AVPixelFormat PixFmt, int Width, int Height);
	AVFrame *AllocAudioFrame(enum AVSampleFormat SampleFmt, uint64_t ChannelLayout, int SampleRate, int NbSamples);

	void WriteFrame(COutputStream *pStream, size_t ThreadIndex) REQUIRES(m_WriteLock);
	void FinishFrames(COutputStream *pStream);
	void CloseStream(COutputStream *pStream);

	// 提交一帧到编码器；返回 EAGAIN 时先排空编码器输出再重试（FFmpeg 编码 API 背压约定）。
	// 返回负值表示提交失败，错误详情已记录且 m_EncodeError 已置位。
	int SendFrameDraining(COutputStream *pStream, AVFrame *pFrame, AVPacket *pPacket, const char *pContext) REQUIRES(m_WriteLock);
	// 从编码器收取所有就绪的包并写入封装器。返回 false 表示发生错误，m_EncodeError 已置位。
	bool ReceiveAndWritePackets(COutputStream *pStream, AVPacket *pPacket, const char *pContext) REQUIRES(m_WriteLock);

	bool AddStream(COutputStream *pStream, AVFormatContext *pFormatContext, const AVCodec **ppCodec, enum AVCodecID CodecId) const;

	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	ISound *m_pSound;

	int m_Width;
	int m_Height;
	// 编码输出尺寸（M7 超采样）：≤ m_Width/m_Height 时捕获帧经 swscale 下采样。
	int m_OutputWidth;
	int m_OutputHeight;
	CVideoFrameCapture m_FrameCapture;
	char m_aName[IO_MAX_PATH_LENGTH];
	uint64_t m_VideoFrameIndex = 0;
	uint64_t m_AudioFrameIndex = 0;
	// 编码/封装错误状态：录制线程与主线程置位，IsRecording/HasError 消费，
	// 主线程据此停止回放并经 m_DemoPlayer.Stop 向用户显示导出失败。
	std::atomic<bool> m_EncodeError{false};

	int m_FPS;

	bool m_Started;
	bool m_Stopped;
	std::atomic<bool> m_Recording;

	CLock m_WriteLock;
	// 视频 PTS 权威计数器：已提交编码的视频帧数，在 m_WriteLock 下递增，
	// 不依赖编码器内部 frame_num，保证 flush 与取消路径时间戳一致。
	int64_t m_VideoFramesEncoded = 0;
	size_t m_AudioThreads = 2;
	size_t m_CurAudioThreadIndex = 0;

	// ---- Demo 导出转换管线（M2）----
	// 固定数量转换线程 + 有界捕获槽队列：渲染线程读回 RGBA 后占用一个空槽并推入就绪队列，
	// 队列满时生产端等待（反压，不丢帧）；转换线程乱序完成像素转换，按帧号严格按序提交编码。
	// 槽生命周期：空闲 -> 就绪队列 -> 转换中（仍被转换线程持有）-> 转换完成后归还空闲。
	class CVideoFrameSlot
	{
	public:
		std::vector<uint8_t> m_vRGBA;
		uint64_t m_FrameNumber = 0;
	};
	std::vector<CVideoFrameSlot> m_vVideoSlots;
	size_t m_ConvertThreads = 2;
	std::vector<std::unique_ptr<std::thread>> m_vpConvertThreads;

	std::mutex m_VideoQueueMutex;
	std::condition_variable m_VideoSlotFreeCond; // 生产端等待空闲槽
	std::condition_variable m_VideoSlotReadyCond; // 转换线程等待就绪帧
	std::condition_variable m_VideoSubmitCond; // 按序提交等待
	std::deque<size_t> m_vPendingSlots; // 就绪队列（槽索引，FIFO）
	std::vector<size_t> m_vFreeSlots; // 空闲槽索引栈
	uint64_t m_NextVideoFrameToCapture = 0; // 渲染线程下一个捕获帧号（仅渲染线程访问）
	uint64_t m_NextVideoFrameToSubmit = 0; // 下一个允许提交编码的帧号
	bool m_VideoStop = false; // 停止标志（排空队列后线程退出）
	bool m_VideoPipelineError = false; // m_EncodeError 的队列锁内镜像，用于唤醒全部等待者

	class CAudioRecorderThread
	{
	public:
		std::thread m_Thread;
		std::mutex m_Mutex;
		std::condition_variable m_Cond;

		bool m_Started = false;
		bool m_Finished = false;
		bool m_HasAudioFrame = false;

		std::mutex m_AudioFillMutex;
		std::condition_variable m_AudioFillCond;
		uint64_t m_AudioFrameToFill = 0;
		int64_t m_SampleCountStart = 0;
	};

	std::vector<std::unique_ptr<CAudioRecorderThread>> m_vpAudioThreads;

	std::atomic<int32_t> m_ProcessingAudioFrame;

	bool m_HasAudio;

	class CAudioBuffer
	{
	public:
		int16_t m_aBuffer[4096];
	};
	std::vector<CAudioBuffer> m_vAudioBuffers;

	COutputStream m_VideoStream;
	COutputStream m_AudioStream;

	const AVCodec *m_pVideoCodec;
	const AVCodec *m_pAudioCodec;

	AVDictionary *m_pOptDict;

	AVFormatContext *m_pFormatContext;
	const AVOutputFormat *m_pFormat;
};

#endif
