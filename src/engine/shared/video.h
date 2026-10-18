#ifndef ENGINE_SHARED_VIDEO_H
#define ENGINE_SHARED_VIDEO_H

#include <base/system.h>

#include <functional>

typedef std::function<void(short *pFinalOut, unsigned Frames)> ISoundMixFunc;

class IVideo
{
public:
	virtual ~IVideo() = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual void Pause(bool Pause) = 0;
	virtual bool IsRecording() const = 0;
	virtual bool HasError() const = 0;

	virtual void NextVideoFrame() = 0;
	virtual void NextVideoFrameThread() = 0;

	virtual void NextAudioFrame(ISoundMixFunc Mix) = 0;
	virtual void NextAudioFrameTimeline(ISoundMixFunc Mix) = 0;

	// 最近一次 Stop() 的最终结果（对象在 Stop 内自删除，调用方只能静态查询）：
	// Stop 会排空编码器、写 trailer 并关闭文件，这些都可能失败；HasError() 在
	// Stop() 之前只覆盖录制期错误，收尾失败由此闭锁在 Stop 完成后暴露。
	// 读取语义：TakeStopResult 清零并返回上一值，保证一次收尾只消费一次。
	static bool LastStopFailed() { return ms_LastStopFailed; }
	static bool TakeStopResult()
	{
		const bool Failed = ms_LastStopFailed;
		ms_LastStopFailed = false;
		return Failed;
	}

	static IVideo *Current() { return ms_pCurrentVideo; }

	static int64_t Time() { return ms_Time; }
	static float LocalTime() { return ms_LocalTime; }
	static void SetLocalStartTime(int64_t LocalStartTime) { ms_LocalStartTime = LocalStartTime; }
	static void SetFPS(int FPS) { ms_TickTime = time_freq() / FPS; }

protected:
	static IVideo *ms_pCurrentVideo;
	static bool ms_LastStopFailed;
	static int64_t ms_Time;
	static int64_t ms_LocalStartTime;
	static float ms_LocalTime;
	static int64_t ms_TickTime;
};

#endif
