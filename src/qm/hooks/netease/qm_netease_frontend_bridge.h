#ifndef QM_HOOKS_NETEASE_QM_NETEASE_FRONTEND_BRIDGE_H
#define QM_HOOKS_NETEASE_QM_NETEASE_FRONTEND_BRIDGE_H

#include "qm_netease_cdp_client.h"

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>
#include <game/client/components/qmclient/netease/netease_lyric_state.h>
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_v5_writer.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace QmNeteaseBridge
{
	// Helper 内的单一歌词 owner。它接收 frontend adapter 的不可变报告，
	// 在自己的时钟线程选择当前句，并把最小结果发布到 v5 ABI。
	class CFrontendLyricBridge
	{
	public:
		CFrontendLyricBridge();
		~CFrontendLyricBridge();
		CFrontendLyricBridge(const CFrontendLyricBridge &) = delete;
		CFrontendLyricBridge &operator=(const CFrontendLyricBridge &) = delete;

		bool Start(uint32_t CloudMusicPid, std::wstring CommandLine);
		void Stop();
		bool IsConnected() const;

	private:
		void OnFrontendReport(std::string_view Report);
		void Tick();
		void Publish(bool Force = false, bool PositionAnchored = false);
		void ClearLyricsLocked();
		bool SwitchSongLocked(uint64_t SongId);
		void ClearPendingLyricsLocked();

		std::atomic_bool m_Stop{false};
		std::thread m_TickThread;
		QmNeteaseCdp::CFrontendBridgeWorker m_Worker;
		QmNeteaseHook::CV5Writer m_Writer;
		mutable std::mutex m_Mutex;
		uint32_t m_CloudMusicPid = 0;
		uint64_t m_SongId = 0;
		uint64_t m_Generation = 0;
		uint64_t m_LastReportTick = 0;
		uint64_t m_LastProgressTick = 0;
		bool m_HasSong = false;
		bool m_PositionValid = false;
		bool m_PlayingHint = false;
		bool m_PlayingHintKnown = false;
		int64_t m_PositionMs = 0;
		NeteaseLyrics::SPlaybackAnchor m_Anchor;
		NeteaseLyrics::STimeline m_Timeline;
		uint64_t m_PendingLyricsSongId = 0;
		NeteaseLyrics::STimeline m_PendingTimeline;
		QmNeteaseHook::ENeteaseLyricSource m_Source = QmNeteaseHook::ENeteaseLyricSource::None;
		std::string m_CurrentLyric;
		int64_t m_LineStartMs = -1;
		int64_t m_LineEndMs = -1;
	};
}

#endif
