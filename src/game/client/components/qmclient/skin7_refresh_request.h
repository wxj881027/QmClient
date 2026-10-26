#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_REQUEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_REQUEST_H

#include "skin7_info_job.h"
#include "skin7_load_completion.h"

#include <base/color.h>
#include <base/time.h>

#include <engine/engine.h>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// 仅主线程管理刷新请求；引擎接收前不触碰已发布资源或活动任务。
template<typename TPart>
class CQmSkin7RefreshRequest
{
public:
	using TCallback = std::function<void()>;
	static constexpr std::chrono::milliseconds SCAN_RETRY_DELAY{250};
	struct SPlan
	{
		std::shared_ptr<CQmSkin7InfoJob> m_pJob;
		std::array<std::vector<TPart>, protocol7::NUM_SKINPARTS> m_aParts;
		TCallback m_Callback;
	};

private:
	bool m_Pending = false;
	TCallback m_Callback;
	std::chrono::nanoseconds m_NextAttempt{0};

public:
	void Request(TCallback &&Callback) noexcept
	{
		m_Callback = std::move(Callback);
		m_Pending = true;
		m_NextAttempt = std::chrono::nanoseconds(0);
	}

	void Cancel() noexcept
	{
		m_Pending = false;
		m_Callback = {};
		m_NextAttempt = std::chrono::nanoseconds(0);
	}

	bool IsPending() const { return m_Pending; }

	bool RetryFailedScan(std::shared_ptr<CQmSkin7InfoJob> &pJob, CQmSkin7LoadCompletion &Completion,
		std::chrono::nanoseconds Now) noexcept
	{
		if(!pJob || pJob->State() != IJob::STATE_DONE || pJob->Succeeded())
			return false;
		if(!m_Pending)
		{
			Request(Completion.TakeForRetry());
			m_NextAttempt = Now > std::chrono::nanoseconds::max() - SCAN_RETRY_DELAY ? std::chrono::nanoseconds::max() : Now + SCAN_RETRY_DELAY;
		}
		else
			Completion.Cancel();
		pJob.reset();
		return true;
	}

	template<typename TFactory>
	std::optional<SPlan> TryStart(IEngine &Engine, int StandardFlags, TFactory &&Factory,
		std::chrono::nanoseconds Now = time_get_nanoseconds()) noexcept
	{
		static_assert(std::is_nothrow_move_constructible_v<SPlan>);
		if(!m_Pending || Now < m_NextAttempt)
			return std::nullopt;
		try
		{
			SPlan Plan;
			// 两个可选部件的空项也先分配，接收任务后不再为它们扩容。
			for(const int Part : {protocol7::SKINPART_MARKING, protocol7::SKINPART_DECORATION})
			{
				TPart Empty;
				Empty.m_Type = Part;
				Empty.m_Flags = StandardFlags;
				Empty.m_aName[0] = '\0';
				Empty.m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				Plan.m_aParts[Part].emplace_back(std::move(Empty));
			}
			Plan.m_pJob = std::forward<TFactory>(Factory)();
			if(!Plan.m_pJob || !Engine.TryAddJob(Plan.m_pJob))
				return std::nullopt;
			Plan.m_Callback = std::move(m_Callback);
			m_Pending = false;
			return std::optional<SPlan>(std::move(Plan));
		}
		catch(...)
		{
			return std::nullopt;
		}
	}

	std::optional<SPlan> TryStart(IEngine &Engine, IStorage *pStorage, int StandardFlags,
		std::chrono::nanoseconds Now = time_get_nanoseconds()) noexcept
	{
		return TryStart(Engine, StandardFlags, [pStorage] { return std::make_shared<CQmSkin7InfoJob>(pStorage); }, Now);
	}
};

#endif
