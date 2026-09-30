#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PIE_MENU_POINTS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PIE_MENU_POINTS_H

#include <game/client/components/player_points.h>

#include <optional>
#include <string>

namespace qm_pie_menu
{
	struct SPointsNotification
	{
		std::string m_Name;
		SPlayerPointsResult m_Result;
	};

	// 只管理本次查分的通知归属；请求、退避和缓存仍由计分板的 CPlayerPoints 维护。
	class CPointsRequest
	{
		std::string m_Name;
		double m_Deadline = 0.0;

	public:
		void Start(const char *pName, double Now)
		{
			m_Name = pName != nullptr ? pName : "";
			m_Deadline = Now + 60.0;
		}

		const std::string &Name() const { return m_Name; }

		std::optional<SPointsNotification> Poll(const char *pName, SPlayerPointsResult Result, double Now)
		{
			if(m_Name.empty() || pName == nullptr || m_Name != pName)
				return std::nullopt;
			if(Result.m_Status != EPointsStatus::READY && Result.m_Status != EPointsStatus::FAILED)
			{
				if(Now < m_Deadline)
					return std::nullopt;
				Result = {EPointsStatus::FAILED, 0};
			}
			SPointsNotification Notification{m_Name, Result};
			m_Name.clear();
			return Notification;
		}
	};
}

#endif
