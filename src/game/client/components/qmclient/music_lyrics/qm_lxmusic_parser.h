#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_PARSER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_PARSER_H

#include "music_lyrics_model.h"

#include <cstdint>
#include <string>
#include <string_view>

// LX Music 开放 API(v2.7.0+)的响应解析。纯逻辑,不依赖客户端状态与网络。
// 端点与字段来自上游 src/main/modules/openApi/index.ts:
//   GET /status?filter=status,name,singer,albumName,picUrl,progress,duration,playbackRate
//   GET /lyric-all  ->  { lyric, tlyric, rlyric, lxlyric }
// LX 的 progress/duration 是秒(浮点),这里统一换算为毫秒;lyric/tlyric 是标准 LRC。
namespace QmLxMusic
{
	struct SStatus
	{
		bool m_Valid = false;
		// playing / paused / error / stoped;上游拼写就是 stoped。
		std::string m_PlaybackStatus;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_CoverUrl;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		double m_PlaybackRate = 1.0;

		bool IsPlaying() const { return m_PlaybackStatus == "playing"; }
	};

	// 只取主轨与翻译轨;lxlrc(逐字)当前无展示方消费,解析后丢弃。
	struct SLyricPayload
	{
		bool m_Valid = false;
		std::string m_Lyric;
		std::string m_TranslationLrc;
	};

	// 解析失败(非 JSON、缺字段类型不符)返回 false 并保持输出不变。
	bool ParseStatus(std::string_view Json, SStatus *pOut);
	bool ParseLyricAll(std::string_view Json, SLyricPayload *pOut);

	// 把主轨 LRC 与翻译 LRC 合成统一时间轴;主轨无有效时间戳时返回 false。
	bool BuildLyrics(const SStatus &Status, const SLyricPayload &Payload, QmMusicLyrics::SLyricsData *pOut);

	// 歌曲身份哈希(FNV-1a 64):标题 + 分隔符 + 歌手。
	uint64_t IdentityHash(std::string_view Title, std::string_view Artist);

	// 歌词重试节奏:歌词通常比播放状态晚就绪,前几次快重试,之后放慢;
	// 总次数设上限,避免音源确实无词时无限请求。抽成常量便于单测钉住策略。
	constexpr int MAX_FAST_LYRIC_RETRIES = 4;
	constexpr int MAX_LYRIC_RETRIES = 20;
	constexpr int64_t LYRIC_RETRY_INTERVAL_MS = 1200;
	constexpr int64_t LYRIC_SLOW_RETRY_INTERVAL_MS = 5000;
	constexpr int64_t LyricRetryDelayMs(int RetryCount)
	{
		return RetryCount < MAX_FAST_LYRIC_RETRIES ? LYRIC_RETRY_INTERVAL_MS : LYRIC_SLOW_RETRY_INTERVAL_MS;
	}
}

#endif
