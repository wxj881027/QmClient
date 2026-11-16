#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_COMMUNITY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_COMMUNITY_H

#include "music_lyrics_display.h"

#include <memory>

class IEngine;
class IHttp;
class IHttpRequest;
class IStorage;

namespace QmMusicLyrics
{
	// 原来源缺少逐字时间时，异步读取本地缓存或 AMLL；始终使用原播放器的进度。
	class CCommunityLyrics
	{
	public:
		using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);
		explicit CCommunityLyrics(TRequestFactory CreateRequest = nullptr);
		~CCommunityLyrics();
		CCommunityLyrics(const CCommunityLyrics &) = delete;
		CCommunityLyrics &operator=(const CCommunityLyrics &) = delete;

		void Update(int SourceId, const SSourceSnapshot &Source, bool Enabled, int64_t NowMs,
			IStorage *pStorage, IEngine *pEngine, IHttp *pHttp);
		SSourceSnapshot Apply(int SourceId, const SSourceSnapshot &Source) const;
		void Reset();

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_pImpl;
	};
}

#endif
