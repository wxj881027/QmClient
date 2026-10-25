#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_DISPLAY_H

#include "music_lyrics_model.h"

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
		if(Original.empty())
			return {};
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
	};
}

#endif
