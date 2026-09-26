#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_INTEGRATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_INTEGRATION_H

#include "netease_lyric_state.h"

#include <game/client/component.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>

#include <cstddef>
#include <cstdint>
#include <string>

class CUIRect;
class ColorRGBA;

// 网易云专用歌词集成。标准媒体状态由 CSystemMediaControls 提供，本组件只消费
// v5 songId/精确进度/当前句，并负责展示开关与切歌清理。
class CNeteaseIntegration : public CComponent
{
public:
	CNeteaseIntegration();
	~CNeteaseIntegration() override = default;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnUpdate() override;

	bool GetCurrentLyric(char *pBuffer, size_t BufferSize, ColorRGBA *pColor = nullptr) const;
	bool HasCurrentLyric() const;
	bool HasActiveLyrics() const;
	uint64_t CurrentSongId() const;
	uint64_t CurrentGeneration() const;
	const NeteaseLyrics::SCurrentState &Snapshot() const { return m_LyricState.Snapshot(); }

private:
	void ClearForStaleMedia();

	NeteaseLyrics::CLyricState m_LyricState;
	uint64_t m_LastBridgeSequence = 0;
	uint64_t m_LastBridgeGeneration = 0;
	uint64_t m_LastBridgeSnapshotTick = 0;
	int64_t m_LastBridgePositionMs = 0;
	uint32_t m_LastCloudMusicPid = 0;
	uint64_t m_BlockedBridgeSongId = 0;
	uint64_t m_BlockedBridgeGeneration = 0;
	std::string m_LastMediaTitle;
	std::string m_LastMediaArtist;
	bool m_WaitingForBridgeIdentity = false;
	uint64_t m_BridgeSyncWaitStartTick = 0;
	uint64_t m_LastSmtcAlignedBridgeSongId = 0;
	uint64_t m_LastSmtcAlignedBridgeGeneration = 0;
	bool m_ActiveLyrics = false;
	bool m_LastBridgePositionValid = false;
	bool m_Initialized = false;
};

#endif
