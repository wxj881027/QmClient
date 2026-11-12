#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_DISPLAY_H

#include "music_lyrics_metadata.h"
#include "music_lyrics_model.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace QmMusicLyrics
{
	enum class EDisplayLanguage
	{
		ORIGINAL,
		TRANSLATION,
		BILINGUAL,
	};

	struct SDisplayRows
	{
		std::array<std::string_view, 2> m_aRows{};
		int m_Count = 0;
	};

	inline SDisplayRows DisplayRows(std::string_view Original, std::string_view Translation, int Language)
	{
		if(Original.empty() || IsLyricCredit(Original))
			return {};
		if(IsLyricCredit(Translation))
			Translation = {};
		if(Language == static_cast<int>(EDisplayLanguage::TRANSLATION) && !Translation.empty())
			return {{{Translation, {}}}, 1};
		if(Language == static_cast<int>(EDisplayLanguage::BILINGUAL) && !Translation.empty() && Translation != Original)
			return {{{Original, Translation}}, 2};
		return {{{Original, {}}}, 1};
	}

	inline std::string_view TranslationAt(const SLyricsData &Data, int Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Data.m_vTranslations.size() ?
			       std::string_view(Data.m_vTranslations[Index]) :
			       std::string_view();
	}

	// 借用的数据只在当前主线程更新期间有效，展示组件复制文字后才跨帧保存。
	struct SSourceSnapshot
	{
		uint64_t m_SongId = 0;
		uint64_t m_Generation = 0;
		uint32_t m_ProducerPid = 0;
		bool m_Active = false;
		const SLyricsData *m_pData = nullptr;
		std::string_view m_Original;
		std::string_view m_Translation;
		// 相同歌词可以在不同时间重复，用行时间戳区分每次出现。
		int64_t m_LineStartMs = -1;
		int64_t m_LineEndMs = -1;
		int64_t m_PositionMs = -1;
		// 平台真实曲目 ID；本地歌曲哈希不能用于社区歌词查询。
		std::string_view m_CatalogId;
		bool m_HasWordTiming = false;
	};

	inline float LyricProgress(const SSourceSnapshot &Source)
	{
		if(!Source.m_Active || Source.m_Original.empty() || Source.m_PositionMs < 0 || Source.m_LineStartMs < 0)
			return -1.0f;
		int64_t End = Source.m_LineEndMs;
		if(End <= Source.m_LineStartMs && Source.m_pData != nullptr)
		{
			const auto &Lines = Source.m_pData->m_Timeline.m_vLines;
			const auto Next = std::upper_bound(Lines.begin(), Lines.end(), Source.m_LineStartMs,
				[](int64_t Start, const NeteaseLyrics::SLine &Line) { return Start < Line.m_StartMs; });
			if(Next != Lines.end())
				End = Next->m_StartMs;
		}
		if(End <= Source.m_LineStartMs)
			return -1.0f;
		return static_cast<float>(std::clamp((static_cast<double>(Source.m_PositionMs) - Source.m_LineStartMs) /
							     (static_cast<double>(End) - Source.m_LineStartMs),
			0.0, 1.0));
	}
}

#endif
