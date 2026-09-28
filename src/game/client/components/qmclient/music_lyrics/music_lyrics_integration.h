#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_INTEGRATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_INTEGRATION_H

#include "qm_soda_hook_provider.h"

#include <game/client/component.h>

#include <qm/hooks/soda/qm_soda_protocol.h>

#include <cstdint>
#include <memory>
#include <string>

// Windows 音乐客户端（汽水音乐、酷狗和 QQ 音乐）歌词集成。
// 标准媒体状态由 CSystemMediaControls 提供;本组件消费各来源私有快照
// (歌曲身份/进度/歌词文件路径),读取歌词 JSON 文件后解析为统一时间轴,
// 并向 HUD 提供当前句。关闭展示时后台采集仍可继续(由 helper 自行维护)。
class CMusicLyricsIntegration : public CComponent
{
public:
	CMusicLyricsIntegration();
	~CMusicLyricsIntegration() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnUpdate() override;

	// 当前句歌词(用于 HUD 歌词岛);无有效歌词时返回 false。
	bool GetCurrentLyric(char *pBuffer, size_t BufferSize) const;
	bool HasCurrentLyric() const;
	bool HasActiveLyrics() const;
	// 当前歌曲身份的稳定数字标记；内部切歌以完整文本和版本判断。
	uint64_t CurrentSongId() const;
	bool GetStatus(char *pBuffer, size_t BufferSize) const;
	int ActiveSource() const;
	bool RunKugouSetup(bool Restore);

private:
	void SyncHookConfiguration();
	void ClearForStaleMedia();
	void ProcessLyricLoadJob();
	void LoadLyricFile(const char *pPath);

	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;
};

#endif
