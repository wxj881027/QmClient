#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_TRACKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ONLINE_TRACKER_H

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qm_friend_notify
{
	struct CFriend
	{
		std::string m_Key;
		std::string m_Name;
		std::string m_Map;
		std::string m_Server;
		bool m_IsFriend = true;
	};

	class COnlineTracker
	{
		struct CState
		{
			CFriend m_Friend;
			int m_MissingSnapshots = 0;
		};

		std::unordered_map<std::string, CState> m_Online;
		std::unordered_set<std::string> m_ObservedServers;

		template<typename TCopyFriend>
		std::vector<CFriend> UpdateCandidate(const std::vector<CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers, const std::unordered_set<std::string> &ObservedServers, const TCopyFriend &CopyFriend)
		{
			std::vector<CFriend> vNotifications;
			std::unordered_set<std::string> Seen;
			for(const CFriend &Friend : vFriends)
			{
				auto It = m_Online.find(Friend.m_Key);
				bool Known = It != m_Online.end();
				if(!Known)
				{
					// 同服同名只改变战队时迁移身份，不产生新的上线事件。
					It = std::find_if(m_Online.begin(), m_Online.end(), [&](const auto &Entry) {
						return Entry.second.m_Friend.m_Name == Friend.m_Name && Entry.second.m_Friend.m_Server == Friend.m_Server;
					});
					Known = It != m_Online.end();
					if(Known)
						m_Online.erase(It);
				}
				if(!Known && Friend.m_IsFriend && ObservedServers.find(Friend.m_Server) != ObservedServers.end())
					vNotifications.push_back(Friend);
				m_Online[Friend.m_Key] = {CopyFriend(Friend), 0};
				Seen.insert(Friend.m_Key);
			}

			for(auto It = m_Online.begin(); It != m_Online.end();)
			{
				if(Seen.find(It->first) != Seen.end())
				{
					++It;
					continue;
				}
				if(AvailableServers.find(It->second.m_Friend.m_Server) == AvailableServers.end())
				{
					// 整服缺失或资料不完整不能证明玩家离线，也不能延续缺席证据。
					It->second.m_MissingSnapshots = 0;
					++It;
				}
				else if(++It->second.m_MissingSnapshots >= 2)
					It = m_Online.erase(It);
				else
					++It;
			}

			return vNotifications;
		}

	public:
		void Reset() noexcept
		{
			m_Online.clear();
			m_ObservedServers.clear();
		}

		// 每次传入完整快照；准备失败不发布通知、不改变身份、缺席证据或各服基线。
		template<typename TCopyFriend>
		std::optional<std::vector<CFriend>> Update(const std::vector<CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers, const TCopyFriend &CopyFriend) noexcept
		{
			try
			{
				COnlineTracker Candidate;
				Candidate.m_Online = m_Online;
				const bool NewServers = std::any_of(AvailableServers.begin(), AvailableServers.end(), [&](const auto &Server) {
					return m_ObservedServers.find(Server) == m_ObservedServers.end();
				});
				if(NewServers)
				{
					// 仅首次观察到完整服务器时准备新基线，常规刷新复用原集合。
					Candidate.m_ObservedServers = m_ObservedServers;
					Candidate.m_ObservedServers.insert(AvailableServers.begin(), AvailableServers.end());
				}
				std::optional<std::vector<CFriend>> Notifications(Candidate.UpdateCandidate(vFriends, AvailableServers, m_ObservedServers, CopyFriend));
				m_Online.swap(Candidate.m_Online);
				if(NewServers)
					m_ObservedServers.swap(Candidate.m_ObservedServers);
				return Notifications;
			}
			catch(...)
			{
				return std::nullopt;
			}
		}

		std::optional<std::vector<CFriend>> Update(const std::vector<CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers) noexcept
		{
			return Update(vFriends, AvailableServers, [](const CFriend &Friend) { return Friend; });
		}
	};
}

#endif
