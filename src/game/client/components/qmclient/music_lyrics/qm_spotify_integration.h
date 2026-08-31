#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_INTEGRATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_INTEGRATION_H

#include <game/client/component.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Spotify 歌词链路集成(纯网络,无需注入):
// sp_dc → TOTP token → 搜索当前曲目 → 官方内置 color-lyrics 歌词(主源),
// LRCLIB(ISRC/文本)兜底。标准媒体状态由 CSystemMediaControls 提供,
// 仅当 SMTC 来源为 Spotify 时激活,并向 HUD 歌词岛提供当前句。
class CSpotifyIntegration : public CComponent
{
public:
	CSpotifyIntegration();
	~CSpotifyIntegration() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnUpdate() override;

	// 当前句歌词(用于 HUD 歌词岛);无有效歌词时返回 false。
	bool GetCurrentLyric(char *pBuffer, size_t BufferSize) const;
	bool HasCurrentLyric() const;
	bool HasActiveLyrics() const;
	// 当前歌曲身份(标题+歌手的稳定哈希)。
	uint64_t CurrentSongId() const;

private:
	void PollTokenPipeline();
	void PollSongPipeline();
	void PollSongPipelineLyrics();
	void PollSongPipelineLrclib();
	void StartLrclibFallback();

	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;
};

#endif
