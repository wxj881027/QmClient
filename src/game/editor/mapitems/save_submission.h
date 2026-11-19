#ifndef GAME_EDITOR_MAPITEMS_SAVE_SUBMISSION_H
#define GAME_EDITOR_MAPITEMS_SAVE_SUBMISSION_H

namespace EditorMapSave
{
	enum class EOutcome
	{
		PREPARATION_FAILED,
		PREPARATION_REJECTED,
		TRACKING_FAILED,
		QUEUE_FAILED,
		SUBMITTED,
	};

	// 队列先登记，后台才能接管；Enqueue 返回失败或抛异常时须保证作业尚未被接收。
	// Jobs 的追加须具备强异常保证，移除尾项不得抛异常。
	// Prepare 返回空表示它已报告具体原因，其他失败由 OnFailure 报告一次。
	template<typename TJobs, typename TPrepare, typename TEnqueue, typename TOnFailure>
	EOutcome Submit(TJobs &Jobs, const TPrepare &Prepare, const TEnqueue &Enqueue, const TOnFailure &OnFailure) noexcept
	{
		EOutcome Outcome = EOutcome::PREPARATION_FAILED;
		bool Tracked = false;
		try
		{
			auto pJob = Prepare();
			if(!pJob)
				Outcome = EOutcome::PREPARATION_REJECTED;
			else
			{
				Outcome = EOutcome::TRACKING_FAILED;
				Jobs.push_back(pJob);
				Tracked = true;
				Outcome = EOutcome::QUEUE_FAILED;
				if(Enqueue(pJob))
					Outcome = EOutcome::SUBMITTED;
			}
		}
		catch(...)
		{
		}
		if(Tracked && Outcome != EOutcome::SUBMITTED)
			Jobs.pop_back();
		if(Outcome != EOutcome::SUBMITTED && Outcome != EOutcome::PREPARATION_REJECTED)
		{
			try
			{
				OnFailure(Outcome);
			}
			catch(...)
			{
			}
		}
		return Outcome;
	}
}

#endif
