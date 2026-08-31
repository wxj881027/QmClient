#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_PARSER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_PARSER_H

#include "music_lyrics_model.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Spotify 各接口响应的纯解析模块(可单测,不发起网络请求):
// color-lyrics 歌词、pathfinder//v1/search 搜索、server-time、token、lrclib 兜底。
namespace QmSpotify
{
	// 搜索候选歌曲。
	struct STrackCandidate
	{
		std::string m_Id;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_Isrc;
	};

	// color-lyrics 响应 → 统一歌词数据。
	// 支持 LINE_SYNCED(行级)与 SYLLABLE_SYNCED(音节级,按 numChars 切词映射词级时间轴);
	// alternatives[0] 作为翻译轨(按下标与主歌词行对齐)。
	bool ParseColorLyrics(std::string_view Json, QmMusicLyrics::SLyricsData *pOut);

	// pathfinder searchDesktop 或 /v1/search 响应 → 候选列表。
	bool ParseSearchResponse(std::string_view Json, std::vector<STrackCandidate> *pOut);

	// open.spotify.com/api/server-time 响应。
	bool ParseServerTime(std::string_view Json, int64_t *pOut);

	// open.spotify.com/api/token 响应(accessToken + 过期时间戳)。
	bool ParseTokenResponse(std::string_view Json, std::string *pAccessToken, int64_t *pExpirationMs);

	// lrclib.net 响应(syncedLyrics LRC → 时间轴,并填充歌曲信息)。
	bool ParseLrclib(std::string_view Json, QmMusicLyrics::SLyricsData *pOut);
} // namespace QmSpotify

#endif
