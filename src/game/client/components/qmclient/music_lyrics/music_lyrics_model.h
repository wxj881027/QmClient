#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_MODEL_H

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

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
		for(const auto &Line : Main.m_vLines)
		{
			std::string Matched;
			int64_t BestDelta = INT64_MAX;
			for(const auto &TransLine : Translation.m_vLines)
			{
				const int64_t Delta = TransLine.m_StartMs >= Line.m_StartMs ? TransLine.m_StartMs - Line.m_StartMs : Line.m_StartMs - TransLine.m_StartMs;
				if(Delta < BestDelta)
				{
					BestDelta = Delta;
					Matched = TransLine.m_Text;
				}
			}
			// 各来源的原文与翻译时间戳允许有 150 毫秒误差。
			pOut->push_back(BestDelta <= 150 ? Matched : std::string());
		}
	}
} // namespace QmMusicLyrics

#endif
