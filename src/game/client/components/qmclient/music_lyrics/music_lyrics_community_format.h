#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_COMMUNITY_FORMAT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_COMMUNITY_FORMAT_H

#include "music_lyrics_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace QmMusicLyrics
{
	enum class ECommunitySource
	{
		NONE,
		NETEASE,
		QQMUSIC,
		SPOTIFY,
	};

	struct SCommunitySong
	{
		ECommunitySource m_Source = ECommunitySource::NONE;
		std::string m_Id;
		bool operator==(const SCommunitySong &) const = default;
	};

	inline constexpr size_t COMMUNITY_LYRICS_MAX_BYTES = 512 * 1024;

	SCommunitySong CommunitySong(int SourceId, uint64_t SongId, std::string_view CatalogId);
	std::string CommunityCachePath(const SCommunitySong &Song);
	std::string CommunityUrl(const SCommunitySong &Song);
	// 社区 YRC 必须包含完整可用的词级时间轴，解析失败会清空输出。
	bool ParseCommunityLyrics(std::string_view Text, SLyricsData *pOut);
	bool HasWordTiming(const SLyricsData &Data);
}

#endif
