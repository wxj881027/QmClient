#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATED_GAME_MESSAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATED_GAME_MESSAGE_H

#include <generated/protocol.h>

#include <type_traits>
#include <variant>

// 返回对象只供当前消息分发借用；下次Reset后不得继续使用原指针。
// 直接持有原生类型，让存储大小、对齐和对象构造随消息类型确定。
class CQmTranslatedGameMessage
{
	std::variant<CNetMsg_Sv_Motd, CNetMsg_Sv_Broadcast, CNetMsg_Cl_SetTeam,
		CNetMsg_Sv_WeaponPickup, CNetMsg_Sv_RaceFinish, CNetMsg_Sv_DDRaceTime,
		CNetMsg_Sv_CommandInfoRemove, CNetMsg_Sv_CommandInfo,
		CNetMsg_Sv_VoteClearOptions, CNetMsg_Sv_VoteOptionAdd, CNetMsg_Sv_VoteOptionRemove,
		CNetMsg_Sv_VoteOptionListAdd, CNetMsg_Sv_VoteSet, CNetMsg_Sv_VoteStatus,
		CNetMsg_Sv_ReadyToEnter, CNetMsg_Sv_Emoticon, CNetMsg_Sv_KillMsg, CNetMsg_Sv_Chat>
		m_Message;

public:
	template<typename TMessage>
	TMessage *Reset() noexcept
	{
		static_assert(std::is_nothrow_default_constructible_v<TMessage>);
		static_assert(std::is_trivially_destructible_v<TMessage>);
		return &m_Message.emplace<TMessage>();
	}
};

#endif
