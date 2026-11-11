#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_INTEGRATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_INTEGRATION_H

#include "music_lyrics_display.h"
#include "qm_lxmusic_parser.h"

#include <game/client/component.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// LX Music 歌词集成(纯本机 HTTP,无需注入):
// 轮询 LX Music 开放 API(/status 取播放状态,/lyric-all 取整首 LRC 与翻译),
// 按标题+歌手识别切歌,向 HUD 歌词岛提供当前句。
// 开放 API 默认关闭且端口可配;端口被其他服务(如 LX 数据同步服务)占用时
// 会以 /status 的 HTTP 状态区分,并在状态文本中说明。
class CLxMusicIntegration : public CComponent
{
public:
	CLxMusicIntegration();
	~CLxMusicIntegration() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnUpdate() override;

	// 当前句歌词(用于 HUD 歌词岛);无有效歌词时返回 false。
	bool GetCurrentLyric(char *pBuffer, size_t BufferSize) const;
	bool HasCurrentLyric() const;
	bool HasActiveLyrics() const;
	QmMusicLyrics::SSourceSnapshot LyricsSnapshot() const;
	// 当前歌曲身份的稳定数字标记(标题+歌手哈希)。
	uint64_t CurrentSongId() const;
	// 面向设置页的一行状态说明。
	bool GetStatus(char *pBuffer, size_t BufferSize) const;
	// 只有在「配置里明确关闭」时才需要「一键开启」按钮;
	// 已开启或用户手动指定端口的情况下按钮没有意义。
	bool NeedsOpenApiSetup() const;
	// 替用户打开 LX Music 的开放 API(需 LX 未运行);结果写入状态行。
	bool EnableOpenApi();

private:
	void RefreshClientConfig();
	int ResolvePort() const;
	void PollStatusPipeline();
	void PollLyricPipeline();
	void ApplyStatus(const QmLxMusic::SStatus &Status);

	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;
};

#endif
