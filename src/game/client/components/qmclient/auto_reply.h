#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AUTO_REPLY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AUTO_REPLY_H

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <array>
#include <cstdint>
#include <string>

namespace qm_auto_reply
{
	enum class EKind
	{
		KEYWORD,
		MUTED,
		AWAY,
	};

	inline std::string Whisper(const char *pName, const char *pText)
	{
		std::string Result = "/w \"";
		for(const char *pCursor = pName; *pCursor; ++pCursor)
		{
			if(*pCursor == '\\' || *pCursor == '"')
				Result += '\\';
			Result += *pCursor;
		}
		return Result + "\" " + pText;
	}

	// 同一公屏消息只处理当前连接的一份；收到的私聊始终由来源连接处理。
	inline bool ShouldHandle(int Connection, int ActiveConnection, int Team, bool OwnMessage)
	{
		return Connection >= 0 && Connection < 2 && !OwnMessage &&
		       (Team == TEAM_WHISPER_RECV || ((Team == 0 || Team == 1) && Connection == ActiveConnection));
	}

	class CSendState
	{
		struct SReply
		{
			EKind m_Kind = EKind::KEYWORD;
			std::string m_Identity;
			std::string m_Text;
		};
		std::array<std::array<SReply, MAX_CLIENTS>, 2> m_aReplies;
		int64_t m_LastSent = -1;
		bool m_MutedEnabled = false;
		bool m_Away = false;

	public:
		void Reset()
		{
			m_aReplies = {};
			m_LastSent = -1;
			m_MutedEnabled = false;
			m_Away = false;
		}

		void UpdateStatus(bool MutedEnabled, bool Away)
		{
			if(m_MutedEnabled == MutedEnabled && m_Away == Away)
				return;
			m_MutedEnabled = MutedEnabled;
			m_Away = Away;
			for(auto &aConnection : m_aReplies)
				for(auto &Reply : aConnection)
					if((Reply.m_Kind == EKind::MUTED && !MutedEnabled) || (Reply.m_Kind == EKind::AWAY && !Away))
						Reply = {};
		}

		bool Ready(int64_t Now, int64_t Cooldown) const
		{
			return m_LastSent < 0 || (Now >= m_LastSent && Now - m_LastSent >= Cooldown);
		}

		template<typename F>
		bool TrySend(int Connection, int Sender, const std::string &Identity, EKind Kind, const std::string &Text, int64_t Now, int64_t Cooldown, F &&Send)
		{
			if(Connection < 0 || Connection >= 2 || Sender < 0 || Sender >= MAX_CLIENTS || Text.empty() || !Ready(Now, Cooldown))
				return false;
			auto &Previous = m_aReplies[Connection][Sender];
			if(Kind != EKind::KEYWORD && Previous.m_Kind == Kind && Previous.m_Identity == Identity && Previous.m_Text == Text)
				return false;
			if(!Send())
				return false;
			m_LastSent = Now;
			if(Kind != EKind::KEYWORD)
				Previous = {Kind, Identity, Text};
			return true;
		}
	};
}

#endif
