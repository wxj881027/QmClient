#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_HUMMING_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_HUMMING_STATE_H

#include "music_lyrics_model.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace QmMusicLyrics
{
	struct SHummingIdentity
	{
		int m_Source = 0;
		uint64_t m_SongId = 0;
		uint64_t m_Generation = 0;
		uint32_t m_ProducerPid = 0;
		int m_Connection = 0;
		int m_LocalId = -1;

		bool operator==(const SHummingIdentity &Other) const = default;
	};

	inline std::string HummingMessage(std::string_view Text)
	{
		if(!NeteaseLyrics::IsValidUtf8(Text))
			return {};
		std::string Clean(Text);
		for(char &Ch : Clean)
			if(static_cast<unsigned char>(Ch) < 0x20 || Ch == 0x7f)
				Ch = ' ';
		const size_t Start = Clean.find_first_not_of(' ');
		if(Start == std::string::npos)
			return {};
		Clean = Clean.substr(Start, Clean.find_last_not_of(' ') - Start + 1);
		// 固定音符前缀使以斜杠开头的歌词也只能作为聊天文字发送。
		return "♪ " + NeteaseLyrics::TruncateUtf8(Clean, 251);
	}

	class CHummingState
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

		bool NeedsSchedule() const { return m_Available && !m_Scheduled; }
		bool GroupDue(int64_t NowMs) const { return m_Available && m_Scheduled && m_LineCount == 0 && NowMs >= m_NextMs; }

		void Schedule(int64_t NowMs, int DelayMs)
		{
			m_aMessages = {};
			m_LineCount = 0;
			m_NextLine = 0;
			m_NextMs = NowMs + std::clamp(DelayMs, 30000, 90000);
			m_Scheduled = m_Available;
		}

		bool BeginGroup(const NeteaseLyrics::STimeline &Timeline, size_t Choice, int64_t NowMs)
		{
			if(!GroupDue(NowMs))
				return false;
			std::vector<std::string> vMessages;
			for(const auto &Line : Timeline.m_vLines)
			{
				std::string Message = HummingMessage(Line.m_Text);
				if(!Message.empty())
					vMessages.push_back(std::move(Message));
			}
			if(vMessages.size() < m_aMessages.size())
				return false;
			const size_t Start = Choice % (vMessages.size() - m_aMessages.size() + 1);
			for(size_t Index = 0; Index < m_aMessages.size(); ++Index)
				m_aMessages[Index] = std::move(vMessages[Start + Index]);
			m_LineCount = static_cast<int>(m_aMessages.size());
			m_NextMs = NowMs;
			return true;
		}

		const std::string *PendingLine(int64_t NowMs) const
		{
			return m_Available && m_NextLine < m_LineCount && NowMs >= m_NextMs ? &m_aMessages[m_NextLine] : nullptr;
		}

		void FinishLine(bool Sent, int64_t NowMs, int NextGroupDelayMs)
		{
			if(PendingLine(NowMs) == nullptr)
				return;
			if(!Sent || ++m_NextLine == m_LineCount)
				Schedule(NowMs, NextGroupDelayMs);
			else
				m_NextMs = NowMs + 3000;
		}

	private:
		SHummingIdentity m_Identity;
		bool m_Available = false;
		bool m_Scheduled = false;
		int64_t m_NextMs = 0;
		std::array<std::string, 3> m_aMessages;
		int m_LineCount = 0;
		int m_NextLine = 0;
	};
}

#endif
