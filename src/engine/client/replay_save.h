#ifndef ENGINE_CLIENT_REPLAY_SAVE_H
#define ENGINE_CLIENT_REPLAY_SAVE_H

#include <engine/demo.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <cstdint>

namespace replay_save
{
	inline int StartTick(int EndTick, int Seconds, int TickSpeed)
	{
		const int64_t Start = static_cast<int64_t>(EndTick) - static_cast<int64_t>(Seconds) * TickSpeed;
		return static_cast<int>(std::clamp<int64_t>(Start, MIN_TICK, MAX_TICK - 1));
	}

	enum class EOutcome
	{
		PREPARATION_FAILED,
		STOP_FAILED,
		QUEUE_FAILED,
		SUBMITTED,
	};

	struct CResult
	{
		EOutcome m_Outcome = EOutcome::PREPARATION_FAILED;
		bool m_RestartFailed = false;
	};

	template<typename TJobs, typename TNotify>
	bool ConsumeCompleted(TJobs &Jobs, const TNotify &Notify) noexcept
	{
		if(Jobs.empty() || Jobs.front()->State() != IJob::STATE_DONE)
			return false;
		auto pJob = Jobs.front();
		Jobs.pop_front();
		try
		{
			Notify(pJob);
		}
		catch(...)
		{
		}
		return true;
	}

	// 先准备完整作业和观察队列，再停止原记录器；后台执行前不修改目标文件。
	template<typename TJobs, typename TCreate, typename TEnqueue, typename TRestart>
	CResult Submit(IDemoRecorder &Recorder, TJobs &Jobs, const TCreate &Create, const TEnqueue &Enqueue, const TRestart &Restart) noexcept
	{
		CResult Result;
		bool Tracked = false;
		bool StopAttempted = false;
		try
		{
			auto pJob = Create();
			if(!pJob)
				return Result;
			Jobs.push_back(pJob);
			Tracked = true;
			StopAttempted = true;
			Result.m_Outcome = EOutcome::STOP_FAILED;
			if(Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE) == 0 && !Recorder.IsRecording() && !Recorder.HasWriteError())
			{
				Result.m_Outcome = EOutcome::QUEUE_FAILED;
				if(Enqueue(pJob))
					Result.m_Outcome = EOutcome::SUBMITTED;
			}
		}
		catch(...)
		{
		}
		if(Tracked && Result.m_Outcome != EOutcome::SUBMITTED)
			Jobs.pop_back();
		if(StopAttempted)
		{
			try
			{
				Result.m_RestartFailed = !Restart();
			}
			catch(...)
			{
				Result.m_RestartFailed = true;
			}
		}
		return Result;
	}
}

#endif
