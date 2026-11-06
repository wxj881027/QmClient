#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_TRACKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_TRACKER_H

#include <engine/friends.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qm_friend_notify
{
	enum class EPresenceEvent
	{
		NONE,
		ENTER,
		LEAVE,
	};

	struct CPresenceMessage
	{
		EPresenceEvent m_Event = EPresenceEvent::NONE;
		std::string m_Name;
	};

	inline CPresenceMessage ParsePresenceMessage(int Sender, int Team, std::string_view Text)
	{
		if(Sender != -1 || Team != 0 || Text.empty() || Text.front() != '\'')
			return {};

		const auto ExtractName = [&](size_t End, EPresenceEvent Event) -> CPresenceMessage {
			const std::string_view Name = Text.substr(1, End - 1);
			if(Name.empty() || Name.size() >= MAX_NAME_LENGTH || Name.find_first_of("\r\n") != std::string_view::npos)
				return {};
			return {Event, std::string(Name)};
		};
		static constexpr std::string_view s_aJoinSuffixes[] = {
			"' entered and joined the game",
			"' joined the game",
			"' entered and joined the red team",
			"' entered and joined the blue team",
			"' entered and joined the spectators",
		};
		for(const auto Suffix : s_aJoinSuffixes)
			if(Text.size() > Suffix.size() && Text.ends_with(Suffix))
				return ExtractName(Text.size() - Suffix.size(), EPresenceEvent::ENTER);

		static constexpr std::string_view s_aLeaveMarkers[] = {"' has left the game", "' 离开了游戏"};
		for(const auto Marker : s_aLeaveMarkers)
		{
			const size_t End = Text.find(Marker, 1);
			if(End == std::string_view::npos)
				continue;
			const std::string_view Reason = Text.substr(End + Marker.size());
			if(Reason.empty() || (Reason.starts_with(" (") && Reason.ends_with(")")) ||
				(Reason.starts_with("（") && Reason.ends_with("）")))
				return ExtractName(End, EPresenceEvent::LEAVE);
		}
		// 改名后旧昵称已不在场，新昵称不视作一次入服。
		static constexpr std::string_view s_aRenameMarkers[] = {"' changed name to '", "' 改名为 '"};
		for(const auto Marker : s_aRenameMarkers)
		{
			const size_t End = Text.find(Marker, 1);
			if(End != std::string_view::npos && Text.ends_with("'") && Text.size() > End + Marker.size() + 1)
				return ExtractName(End, EPresenceEvent::LEAVE);
		}
		return {};
	}

	// 只有服务端出入提示改变状态，不再从玩家快照和本地计时推断入服。
	class CEnterTracker
	{
		std::vector<std::string> m_vPresentNames;

	public:
		void Reset() noexcept { m_vPresentNames.clear(); }
		void Swap(CEnterTracker &Other) noexcept { m_vPresentNames.swap(Other.m_vPresentNames); }

		CPresenceMessage OnMessage(int Sender, int Team, std::string_view Text, const IFriends &Friends,
			std::string_view MainName, std::string_view DummyName)
		{
			CPresenceMessage Message = ParsePresenceMessage(Sender, Team, Text);
			if(Message.m_Event == EPresenceEvent::LEAVE)
			{
				m_vPresentNames.erase(std::remove(m_vPresentNames.begin(), m_vPresentNames.end(), Message.m_Name), m_vPresentNames.end());
				return Message;
			}
			if(Message.m_Event != EPresenceEvent::ENTER || Message.m_Name == MainName || Message.m_Name == DummyName ||
				std::find(m_vPresentNames.begin(), m_vPresentNames.end(), Message.m_Name) != m_vPresentNames.end())
				return {};

			// 入服提示没有战队信息；本功能按完整昵称匹配个人好友。
			for(int Index = 0; Index < Friends.NumFriends(); ++Index)
			{
				if(Message.m_Name == Friends.GetFriend(Index)->m_aName)
				{
					m_vPresentNames.push_back(Message.m_Name);
					return Message;
				}
			}
			return {};
		}
	};

	// 问候只等待聊天发送额度，离服提示取消相应的待发对象。
	class CGreetQueue
	{
		std::vector<std::string> m_vNames;

	public:
		void Reset() noexcept { m_vNames.clear(); }
		void Swap(CGreetQueue &Other) noexcept { m_vNames.swap(Other.m_vNames); }

		void Add(const std::string &Name)
		{
			if(!Name.empty() && std::find(m_vNames.begin(), m_vNames.end(), Name) == m_vNames.end())
				m_vNames.push_back(Name);
		}

		void Remove(const std::string &Name)
		{
			m_vNames.erase(std::remove(m_vNames.begin(), m_vNames.end(), Name), m_vNames.end());
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
