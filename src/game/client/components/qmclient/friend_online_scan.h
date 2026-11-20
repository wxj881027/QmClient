#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_SCAN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_SCAN_H

#include "friend_online_tracker.h"

#include <base/system.h>

#include <engine/friends.h>
#include <engine/serverbrowser.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace qm_friend_notify
{
	class COnlineScanner
	{
	public:
		struct CSnapshot
		{
			std::vector<CFriend> m_vFriends;
			std::unordered_set<std::string> m_AvailableServers;
		};

	private:
		CSnapshot m_Candidate;
		std::unordered_set<std::string> m_NamedFriends;
		const IFriends *m_pFriends = nullptr;
		const IServerBrowser *m_pBrowser = nullptr;
		uint64_t m_HttpRevision = 0;
		uint64_t m_FriendsRevision = 0;
		bool m_IgnoreClan = false;
		bool m_Running = false;
		int m_NextIndex = 0;

	public:
		bool Running() const { return m_Running; }
		void Reset() noexcept
		{
			m_Candidate.m_vFriends.clear();
			m_Candidate.m_AvailableServers.clear();
			m_NamedFriends.clear();
			m_pFriends = nullptr;
			m_pBrowser = nullptr;
			m_HttpRevision = 0;
			m_FriendsRevision = 0;
			m_IgnoreClan = false;
			m_Running = false;
			m_NextIndex = 0;
		}

		// 来源在主线程的两次调用之间发布；只借出完整结果，借用有效期到下次 Step/Reset。
		const CSnapshot *Step(const IFriends &Friends, const IServerBrowser &Browser, bool IgnoreClan) noexcept
		{
			try
			{
				const uint64_t HttpRevision = Browser.HttpListRevision();
				const uint64_t FriendsRevision = Friends.Revision();
				if(!m_Running || m_pFriends != &Friends || m_pBrowser != &Browser ||
					m_HttpRevision != HttpRevision || m_FriendsRevision != FriendsRevision || m_IgnoreClan != IgnoreClan)
				{
					Reset();
					m_pFriends = &Friends;
					m_pBrowser = &Browser;
					m_HttpRevision = HttpRevision;
					m_FriendsRevision = FriendsRevision;
					m_IgnoreClan = IgnoreClan;
					m_Running = true;
					for(int Index = 0; Index < Friends.NumFriends(); ++Index)
					{
						const CFriendInfo *pFriend = Friends.GetFriend(Index);
						if(pFriend->m_aName[0] != '\0')
							m_NamedFriends.insert(pFriend->m_aName);
					}
				}
				const int NumServers = Browser.NumHttpServers();
				if(NumServers <= 0)
				{
					Reset();
					return nullptr;
				}

				constexpr int ServersPerStep = 32;
				int Processed = 0;
				std::string Key;
				Key.reserve(MAX_NAME_LENGTH + MAX_CLAN_LENGTH + 1);
				while(m_NextIndex < NumServers && Processed < ServersPerStep)
				{
					const CServerInfo *pEntry = Browser.HttpGet(m_NextIndex);
					++m_NextIndex;
					++Processed;
					if(pEntry == nullptr || pEntry->m_NumAddresses <= 0)
						continue;
					char aAddress[NETADDR_MAXSTRSIZE];
					net_addr_str(&pEntry->m_aAddresses[0], aAddress, sizeof(aAddress), true);
					if(pEntry->m_NumClients >= 0 && pEntry->m_vClients.size() >= static_cast<size_t>(pEntry->m_NumClients))
						m_Candidate.m_AvailableServers.insert(aAddress);
					for(const CServerInfo::CClient &Client : pEntry->m_vClients)
					{
						if(Client.m_aName[0] == '\0' || m_NamedFriends.find(Client.m_aName) == m_NamedFriends.end())
							continue;
						Key = Client.m_aName;
						if(!IgnoreClan)
						{
							Key.push_back('\t');
							Key.append(Client.m_aClan);
						}
						m_Candidate.m_vFriends.push_back({Key, Client.m_aName, pEntry->m_aMap, aAddress,
							Friends.IsFriend(Client.m_aName, Client.m_aClan, true)});
					}
				}
				if(m_NextIndex < NumServers)
					return nullptr;
				m_Running = false;
				return &m_Candidate;
			}
			catch(...)
			{
				Reset();
				return nullptr;
			}
		}
	};
}

#endif
