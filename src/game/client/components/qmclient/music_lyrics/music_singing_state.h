#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_SINGING_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_SINGING_STATE_H

#include "music_humming_state.h"

namespace QmMusicLyrics
{
	class CSingingState
	{
	public:
		void Reset() { *this = {}; }

		void Synchronize(bool Available, const SHummingIdentity &Identity)
		{
			if(!Available || !m_Available || !(Identity == m_Identity))
			{
				Reset();
				m_Available = Available;
				m_Identity = Identity;
			}
		}

		std::string OnStroke(bool Pressed, int64_t NowMs, std::string_view CurrentLyric)
		{
			if(!Pressed)
			{
				m_Held = false;
				return {};
			}
			if(m_Held)
				return {};
			m_Held = true;
			if(!m_Available)
				return {};
			if(!m_Armed || NowMs < m_FirstPressMs || NowMs - m_FirstPressMs > 400)
			{
				m_Armed = true;
				m_FirstPressMs = NowMs;
				return {};
			}
			// 每两次真实按下只消费一次，取第二次按下时的歌词，不缓存待发消息。
			m_Armed = false;
			return LyricChatMessage(CurrentLyric);
		}

	private:
		SHummingIdentity m_Identity;
		bool m_Available = false;
		bool m_Held = false;
		bool m_Armed = false;
		int64_t m_FirstPressMs = 0;
	};
}

#endif
