#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_MESSAGE_FILTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_MESSAGE_FILTER_H

#include <base/system.h>

#include <generated/protocol.h>

namespace QmChatMessageFilter
{
	struct SSettings
	{
		bool m_RelatedOnly = false;
		bool m_FriendsOnly = false;
		bool m_TeamOnly = false;
	};

	struct SMessage
	{
		int m_ClientId = -1;
		int m_Team = 0;
		bool m_CurrentLocal = false;
		bool m_Local = false;
		bool m_Blocked = false;
		bool m_Friend = false;
		bool m_OtherTeam = false;
		bool m_Mentioned = false;
	};

	inline bool MentionsName(const char *pLine, const char *pName, bool AllowAtPrefix = false)
	{
		if(pName[0] == '\0')
			return false;

		// 大小写映射可能改变 UTF-8 字节数，边界必须使用实际匹配终点。
		const char *pEnd = nullptr;
		const char *pHit = str_utf8_find_nocase(pLine, pName, &pEnd);
		while(pHit)
		{
			const bool NameBoundary = pLine == pHit || pHit[-1] == ' ';
			const bool AtBoundary = AllowAtPrefix && pHit > pLine && pHit[-1] == '@' && (pHit == pLine + 1 || pHit[-2] == ' ');
			if((NameBoundary || AtBoundary) && (*pEnd == 0 || *pEnd == ' ' || *pEnd == '.' || *pEnd == '!' || *pEnd == ',' || *pEnd == '?' || *pEnd == ':'))
				return true;
			pHit = str_utf8_find_nocase(pHit + 1, pName, &pEnd);
		}
		return false;
	}

	inline bool ShouldShow(const SSettings &Settings, const SMessage &Message)
	{
		// 系统与客户端提示继续由各自的显示开关控制。
		if(Message.m_ClientId < 0)
			return true;
		if(Message.m_Blocked)
			return false;
		if(Settings.m_RelatedOnly)
			return Message.m_Local || Message.m_Team == TEAM_WHISPER_SEND || Message.m_Team == TEAM_WHISPER_RECV || Message.m_Mentioned;

		// 关闭新模式后保留原有规则，当前角色之外的分身仍按原过滤条件处理。
		return Message.m_CurrentLocal || ((!Settings.m_FriendsOnly || Message.m_Friend) && (!Settings.m_TeamOnly || !Message.m_OtherTeam));
	}
}

#endif
