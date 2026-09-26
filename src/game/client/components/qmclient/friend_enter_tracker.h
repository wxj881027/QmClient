#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_TRACKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_TRACKER_H

#include <algorithm>
#include <string>
#include <vector>

namespace qm_friend_notify
{
	class CEnterTracker
	{
	public:
		struct CClient
		{
			int m_ClientId;
			std::string m_Name;
			std::string m_Clan;
			bool m_IsFriend;
			bool m_IsLocal;
		};

	private:
		struct CTrackedClient
		{
			CClient m_Client;
			bool m_Missing = false;
			double m_MissingSince = 0.0;
		};

		std::vector<CTrackedClient> m_vClients;
		bool m_Initialized = false;

		static bool SameIdentity(const CClient &Left, const CClient &Right, bool IgnoreClan)
		{
			return Left.m_Name == Right.m_Name && (IgnoreClan || Left.m_Clan == Right.m_Clan);
		}

	public:
		void Reset()
		{
			m_vClients.clear();
			m_Initialized = false;
		}

		// 调用方只传入完整有效的玩家列表；切换连接或好友配置后先重置基线。
		std::vector<std::string> Update(const std::vector<CClient> &vClients, double Now, bool IgnoreClan)
		{
			static constexpr double AbsenceGraceSeconds = 3.0;
			for(auto &Tracked : m_vClients)
			{
				const bool SlotPresent = std::any_of(vClients.begin(), vClients.end(), [&](const CClient &Client) {
					return Client.m_ClientId == Tracked.m_Client.m_ClientId;
				});
				if(!Tracked.m_Missing && !SlotPresent)
				{
					Tracked.m_Missing = true;
					Tracked.m_MissingSince = Now;
				}
			}
			m_vClients.erase(std::remove_if(m_vClients.begin(), m_vClients.end(), [&](const CTrackedClient &Tracked) {
				return Tracked.m_Missing && Now - Tracked.m_MissingSince >= AbsenceGraceSeconds;
			}),
				m_vClients.end());

			std::vector<std::string> vNewFriends;
			std::vector<CTrackedClient> vNextClients;
			vNextClients.reserve(vClients.size() + m_vClients.size());
			for(const auto &Client : vClients)
			{
				// 同槽位资料变化、短暂缺席和身份换槽都延续原来的在服状态。
				const bool Known = std::any_of(m_vClients.begin(), m_vClients.end(), [&](const CTrackedClient &Tracked) {
					return Tracked.m_Client.m_ClientId == Client.m_ClientId || SameIdentity(Tracked.m_Client, Client, IgnoreClan);
				});
				if(m_Initialized && !Known && Client.m_IsFriend && !Client.m_IsLocal)
					vNewFriends.push_back(Client.m_Name);
				vNextClients.push_back({Client});
			}
			for(const auto &Tracked : m_vClients)
			{
				const bool Present = std::any_of(vClients.begin(), vClients.end(), [&](const CClient &Client) {
					return Tracked.m_Client.m_ClientId == Client.m_ClientId || SameIdentity(Tracked.m_Client, Client, IgnoreClan);
				});
				if(!Present)
					vNextClients.push_back(Tracked);
			}
			m_vClients.swap(vNextClients);
			m_Initialized = true;
			return vNewFriends;
		}
	};
}

#endif
