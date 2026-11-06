#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_SESSION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_SESSION_H

#include <game/client/components/tclient/map_history.h>

#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace QmMapHistory
{
	// 空地图仍成功输出空ID；资源不足保留调用方原输出，避免与离线状态混淆。
	inline bool TrySessionMapId(const char *pMapName, const char *pMapPath, std::string &Output) noexcept
	{
		try
		{
			std::string Candidate;
			if(pMapName && pMapName[0])
				Candidate = pMapPath && pMapPath[0] ? std::string("path:") + pMapPath : std::string("name:") + pMapName;
			Output.swap(Candidate);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	class CSession
	{
		bool m_Active = false;
		int64_t m_StartTime = 0;
		std::string m_MapId;

	public:
		bool Active() const noexcept { return m_Active; }
		int64_t StartTime() const noexcept { return m_StartTime; }
		const std::string &MapId() const noexcept { return m_MapId; }
		void Reset() noexcept
		{
			m_Active = false;
			m_StartTime = 0;
			m_MapId.clear();
		}

		// 组件先结算旧会话。记录准备成功后读取原计时起点，发布阶段不再分配。
		template<typename TGetStart>
		bool TryStart(CMapHistory &History, std::string_view MapName, std::string &&MapId, int64_t NowUnix, std::string_view Date, TGetStart &&GetStart) noexcept
		{
			static_assert(std::is_nothrow_invocable_r_v<int64_t, TGetStart &>);
			if(m_Active || MapName.empty() || MapId.empty())
				return false;
			try
			{
				History.RecordVisit(MapName, MapId, NowUnix, Date);
				const int64_t Start = GetStart();
				m_MapId.swap(MapId);
				m_StartTime = Start;
				m_Active = true;
				return true;
			}
			catch(const std::bad_alloc &)
			{
				return false;
			}
			catch(const std::length_error &)
			{
				return false;
			}
		}
	};
}

#endif
