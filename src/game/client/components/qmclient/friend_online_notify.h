#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_NOTIFY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_NOTIFY_H

#include "friend_online_tracker.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qm_friend_notify
{
	class COnlineNotifier
	{
		COnlineTracker m_Tracker;
		std::unordered_map<std::string, float> m_LastSeen;

	public:
		void Reset() noexcept
		{
			m_Tracker.Reset();
			m_LastSeen.clear();
		}

		void Prune(float Now) noexcept
		{
			constexpr float OfflineTimeout = 10.0f;
			for(auto It = m_LastSeen.begin(); It != m_LastSeen.end();)
			{
				if(Now - It->second > OfflineTimeout)
					It = m_LastSeen.erase(It);
				else
					++It;
			}
		}

		// 返回值只表示状态准备与提交成功，单条输出失败不会重放整批观察。
		template<typename TNotify, typename TCopyFriend>
		bool Update(const std::vector<CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers, float Now, const TNotify &Notify, const TCopyFriend &CopyFriend) noexcept
		{
			try
			{
				auto NextSeen = m_LastSeen;
				for(const CFriend &Friend : vFriends)
					if(Friend.m_IsFriend)
						NextSeen[Friend.m_Key] = Now;
				auto Notifications = m_Tracker.Update(vFriends, AvailableServers, CopyFriend);
				if(!Notifications)
					return false;
				m_LastSeen.swap(NextSeen);

				// 聊天入口可能已产生部分输出；单条失败后继续，不重放已提交的观察。
				for(const CFriend &Friend : *Notifications)
				{
					if(NextSeen.find(Friend.m_Key) != NextSeen.end())
						continue;
					try
					{
						Notify(Friend);
					}
					catch(...)
					{
					}
				}
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

		template<typename TNotify>
		bool Update(const std::vector<CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers, float Now, const TNotify &Notify) noexcept
		{
			return Update(vFriends, AvailableServers, Now, Notify, [](const CFriend &Friend) { return Friend; });
		}
	};
}

#endif
