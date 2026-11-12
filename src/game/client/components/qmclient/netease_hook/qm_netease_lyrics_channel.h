#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_LYRICS_CHANNEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_LYRICS_CHANNEL_H

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <cstdint>
#include <memory>
#include <string>

namespace QmNeteaseHook
{
	// 完整歌词使用独立可选通路，当前句继续使用原有 v5 布局。
	class CLyricsChannel
	{
	public:
		explicit CLyricsChannel(std::wstring Endpoint = L"Local\\QmClient.NeteaseHook.Lyrics.v1");
		~CLyricsChannel();
		CLyricsChannel(const CLyricsChannel &) = delete;
		CLyricsChannel &operator=(const CLyricsChannel &) = delete;

		bool Publish(uint32_t Pid, uint64_t SongId, uint64_t Generation, const NeteaseLyrics::SRawLyrics &Raw);
		bool Read(uint32_t Pid, uint64_t SongId, uint64_t Generation, uint64_t &Revision, NeteaseLyrics::SRawLyrics &Raw);
		void Close();

	private:
		bool Open(bool Writer);
		struct SImpl;
		std::unique_ptr<SImpl> m_pImpl;
	};
}

#endif
