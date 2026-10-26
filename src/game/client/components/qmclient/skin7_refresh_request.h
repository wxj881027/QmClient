#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_REQUEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_REFRESH_REQUEST_H

#include "skin7_info_job.h"

#include <base/color.h>

#include <engine/engine.h>

#include <array>
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
	struct SPlan
	{
		std::shared_ptr<CQmSkin7InfoJob> m_pJob;
		std::array<std::vector<TPart>, protocol7::NUM_SKINPARTS> m_aParts;
		TCallback m_Callback;
	};

private:
	bool m_Pending = false;
	TCallback m_Callback;

public:
	void Request(TCallback &&Callback) noexcept
	{
		m_Callback = std::move(Callback);
		m_Pending = true;
	}

	void Cancel() noexcept
	{
		m_Pending = false;
		m_Callback = {};
	}

	bool IsPending() const { return m_Pending; }

	template<typename TFactory>
	std::optional<SPlan> TryStart(IEngine &Engine, int StandardFlags, TFactory &&Factory) noexcept
	{
		static_assert(std::is_nothrow_move_constructible_v<SPlan>);
		if(!m_Pending)
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

	std::optional<SPlan> TryStart(IEngine &Engine, IStorage *pStorage, int StandardFlags) noexcept
	{
		return TryStart(Engine, StandardFlags, [pStorage] { return std::make_shared<CQmSkin7InfoJob>(pStorage); });
	}
};

#endif
