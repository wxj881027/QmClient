#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_METADATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_METADATA_H

#include <base/str.h>

#include <algorithm>
#include <string_view>

namespace QmMusicLyrics
{
	inline bool IsLyricCredit(std::string_view Text)
	{
		// 只检查有边界的署名标签，避免误伤“录音机”“我想为你作词”等正常歌词。
		const auto FirstCodepoint = [](std::string_view Value, size_t &Length) {
			char aPrefix[5] = {};
			Value.copy(aPrefix, std::min<size_t>(4, Value.size()));
			const char *pNext = aPrefix;
			const int Code = str_utf8_decode(&pNext);
			Length = std::min<size_t>(pNext - aPrefix, Value.size());
			return Code;
		};
		while(!Text.empty())
		{
			size_t Length;
			const int Code = FirstCodepoint(Text, Length);
			if(!str_utf8_isspace(Code) && Code != '[' && Code != '(' && Code != 0x3010 && Code != 0xff08)
				break;
			Text.remove_prefix(Length);
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
			const size_t Length = str_length(pLabel);
			if(Text.size() < Length || str_comp_nocase_num(Text.data(), pLabel, Length) != 0)
				continue;
			size_t SuffixLength;
			const int Code = FirstCodepoint(Text.substr(Length), SuffixLength);
			if(Code == 0 || str_utf8_isspace(Code) || Code == ':' || Code == 0xff1a || Code == '/' || Code == '\\' || Code == '-' || Code == '|' ||
				Code == '(' || Code == ')' || Code == ']' || Code == 0xff0f || Code == 0xff08 || Code == 0xff09 || Code == 0x3011)
				return true;
		}
		return false;
	}
}

#endif
