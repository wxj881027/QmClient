#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_MODEL_H

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 多音乐客户端(酷狗/汽水/QQ)歌词的统一数据模型。
// 复用 NeteaseLyrics 的 STimeline/SLine/SWord 承载行级 + 词级时间轴,
// 另加歌曲信息与翻译轨,供各客户端采集链路输出与 HUD 消费。
namespace QmMusicLyrics
{
	// 歌曲信息。各字段可能缺失,由采集链路按可得性填充。
	struct SSongInfo
	{
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_CoverPath; // 本地封面文件路径(可选)
		int64_t m_DurationMs = 0;
	};

	// 歌词数据。m_Timeline 承载完整歌词与逐字时间轴;
	// 翻译轨 m_vTranslations 与 m_Timeline.m_vLines 按下标对齐(行级,可为空)。
	struct SLyricsData
	{
		SSongInfo m_Song;
		NeteaseLyrics::STimeline m_Timeline;
		std::vector<std::string> m_vTranslations;

		bool HasLyrics() const { return m_Timeline.m_HasTiming && !m_Timeline.m_vLines.empty(); }
		bool HasTranslation() const
		{
			if(m_vTranslations.size() != m_Timeline.m_vLines.size())
				return false;
			for(const std::string &Text : m_vTranslations)
				if(!Text.empty())
					return true;
			return false;
		}
	};
	inline void AlignTranslations(const NeteaseLyrics::STimeline &Main, std::string_view TranslationLrc, std::vector<std::string> *pOut)
	{
		if(pOut == nullptr)
			return;
		pOut->clear();
		NeteaseLyrics::STimeline Translation;
		if(TranslationLrc.empty() || !NeteaseLyrics::ParseLrc(TranslationLrc, &Translation))
			return;
		const auto StartsBefore = [](const NeteaseLyrics::SLine &Line, int64_t Start) { return Line.m_StartMs < Start; };
		for(const auto &Line : Main.m_vLines)
		{
			const auto Next = std::lower_bound(Translation.m_vLines.begin(), Translation.m_vLines.end(), Line.m_StartMs, StartsBefore);
			const NeteaseLyrics::SLine *pMatched = Next != Translation.m_vLines.end() ? &*Next : nullptr;
			// 有序值转为无符号数后相减，跨越 int64 全域也不会发生有符号溢出。
			uint64_t BestDelta = pMatched ? static_cast<uint64_t>(pMatched->m_StartMs) - static_cast<uint64_t>(Line.m_StartMs) : UINT64_MAX;
			if(Next != Translation.m_vLines.begin())
			{
				// 同一时间组取稳定排序的首行；同距时保留较早时间组。
				const auto Previous = std::lower_bound(Translation.m_vLines.begin(), Next, (Next - 1)->m_StartMs, StartsBefore);
				const uint64_t Delta = static_cast<uint64_t>(Line.m_StartMs) - static_cast<uint64_t>(Previous->m_StartMs);
				if(Delta <= BestDelta)
				{
					BestDelta = Delta;
					pMatched = &*Previous;
				}
			}
			// 各来源的原文与翻译时间戳允许有 150 毫秒误差。
			if(pMatched != nullptr && BestDelta <= 150)
				pOut->push_back(pMatched->m_Text);
			else
				pOut->emplace_back();
		}
	}
} // namespace QmMusicLyrics

#endif
