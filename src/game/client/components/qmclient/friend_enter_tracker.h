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
				// 在场时的资料变化保持静默；缺席后的槽位复用要核对身份。
				const bool Known = std::any_of(m_vClients.begin(), m_vClients.end(), [&](const CTrackedClient &Tracked) {
					return (!Tracked.m_Missing && Tracked.m_Client.m_ClientId == Client.m_ClientId) || SameIdentity(Tracked.m_Client, Client, IgnoreClan);
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

	// 问候只等待聊天发送额度，名单更新时丢弃已经离服的待发对象。
	class CGreetQueue
	{
		std::vector<std::string> m_vNames;

	public:
		void Reset() { m_vNames.clear(); }

		void Update(const std::vector<std::string> &vNewNames, const std::vector<CEnterTracker::CClient> &vClients)
		{
			for(const auto &Name : vNewNames)
				if(std::find(m_vNames.begin(), m_vNames.end(), Name) == m_vNames.end())
					m_vNames.push_back(Name);
			m_vNames.erase(std::remove_if(m_vNames.begin(), m_vNames.end(), [&](const std::string &Name) {
				return std::none_of(vClients.begin(), vClients.end(), [&](const CEnterTracker::CClient &Client) {
					return Client.m_Name == Name && Client.m_IsFriend && !Client.m_IsLocal;
				});
			}),
				m_vNames.end());
		}

		template<typename F>
		bool TrySend(F &&Send)
		{
			if(m_vNames.empty())
				return false;
			std::string Names;
			for(const auto &Name : m_vNames)
			{
				if(!Names.empty())
					Names += ' ';
				Names += Name;
			}
			if(!Send(Names))
				return false;
			Reset();
			return true;
		}
	};
}

#endif
