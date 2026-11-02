#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_HUMMING_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_HUMMING_STATE_H

#include "music_lyrics_model.h"

#include <base/str.h>

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

	inline bool IsHummingCredit(const char *pText)
	{
		// 署名标签可能带空白或括号，只在标签后有分隔符时匹配，避免误伤“录音机”等歌词。
		while(*pText != '\0')
		{
			const char *pNext = pText;
			const int Code = str_utf8_decode(&pNext);
			if(!str_utf8_isspace(Code) && Code != '[' && Code != '(' && Code != 0x3010 && Code != 0xff08)
				break;
			pText = pNext;
		}
		constexpr const char *apCreditLabels[] = {
			"作词", "作詞", "作词人", "作詞人", "作曲", "编曲", "編曲", "词曲", "詞曲", "填词", "填詞", "词", "詞", "曲",
			"制作人", "製作人", "制作", "製作", "监制", "監製", "出品", "发行", "發行", "策划", "策劃",
			"录音", "錄音", "混音", "缩混", "縮混", "母带", "母帶", "和声", "和聲", "和音", "配唱",
			"演唱", "原唱", "翻唱", "吉他", "贝斯", "貝斯", "贝司", "貝司", "钢琴", "鋼琴", "键盘", "鍵盤", "弦乐", "弦樂", "鼓",
			"lyrics by", "words by", "music by", "written by", "composed by", "arranged by", "produced by", "recorded by", "mixed by", "mastered by",
			"lyricist", "composer", "arranger", "producer", "mixing", "mastering"};
		for(const char *pLabel : apCreditLabels)
		{
			const char *pSuffix = str_startswith_nocase(pText, pLabel);
			if(pSuffix == nullptr)
				continue;
			const int Code = str_utf8_decode(&pSuffix);
			if(Code == 0 || str_utf8_isspace(Code) || Code == '/' || Code == '\\' || Code == '-' || Code == '|' ||
				Code == '(' || Code == ')' || Code == ']' || Code == 0xff0f || Code == 0xff08 || Code == 0xff09 || Code == 0x3011)
				return true;
		}
		return false;
	}

	inline std::string HummingMessage(std::string_view Text)
	{
		if(!NeteaseLyrics::IsValidUtf8(Text) || Text.find(':') != std::string_view::npos || Text.find("：") != std::string_view::npos)
			return {};
		std::string Clean(Text);
		for(char &Ch : Clean)
			if(static_cast<unsigned char>(Ch) < 0x20 || Ch == 0x7f)
				Ch = ' ';
		const size_t Start = Clean.find_first_not_of(' ');
		if(Start == std::string::npos)
			return {};
		Clean = Clean.substr(Start, Clean.find_last_not_of(' ') - Start + 1);
		if(IsHummingCredit(Clean.c_str()))
			return {};
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
			m_NextMs = NowMs + std::clamp(DelayMs, 90000, 270000);
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
