#ifndef QM_HOOKS_NETEASE_QM_NETEASE_FRONTEND_BRIDGE_H
#define QM_HOOKS_NETEASE_QM_NETEASE_FRONTEND_BRIDGE_H

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>
#include <game/client/components/qmclient/netease/netease_lyric_state.h>
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_hook_v5_writer.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace QmNeteaseBridge
{
	// Helper 内的单一歌词 owner。它接收 frontend adapter 的不可变报告，
	// 在自己的时钟线程选择当前句，并把最小结果发布到 v5 ABI。
	// Start/Stop 由 helper 主线程串行调用，报告与 Tick 不能反向销毁本对象。
	class CFrontendLyricBridge
	{
	public:
		// 仅替换原生传输、映射、时钟和线程；歌词状态仍由本 owner 管理。
		class COperations
		{
		public:
			using TReportCallback = std::function<void(std::string_view)>;
			virtual ~COperations() = default;
			virtual bool OpenWriter() = 0;
			virtual void CloseWriter() noexcept = 0;
			virtual bool ReadSnapshot(QmNeteaseHook::SSnapshotV5 &Snapshot) = 0;
			virtual bool PublishSnapshot(QmNeteaseHook::SSnapshotV5 Snapshot) = 0;
			virtual bool StartReports(uint32_t Pid, const std::wstring &CommandLine, TReportCallback Callback) = 0;
			// 返回前所有报告回调必须结束，后续不能再调用借用的 owner。
			virtual void StopReports() noexcept = 0;
			virtual bool ReportsRunning() const noexcept = 0;
			virtual bool IsConnected() const noexcept = 0;
			virtual uint64_t NowTick() const;
			// 返回独占的可 join 线程，或在没有遗留线程时报告失败。
			virtual std::thread CreateTickThread(std::function<void()> Run);
			virtual void WaitForTick(const std::atomic_bool &Stop);
		};
		CFrontendLyricBridge();
		// 借用的操作对象必须存活到本 owner 停止并析构之后。
		explicit CFrontendLyricBridge(COperations &Operations);
		~CFrontendLyricBridge();
		CFrontendLyricBridge(const CFrontendLyricBridge &) = delete;
		CFrontendLyricBridge &operator=(const CFrontendLyricBridge &) = delete;

		bool Start(uint32_t CloudMusicPid, const std::wstring &CommandLine) noexcept;
		void Stop() noexcept;
		bool IsRunning() const noexcept;
		bool IsConnected() const;

	private:
		void OnFrontendReport(std::string_view Report) noexcept;
		void ApplyFrontendReport(std::string_view Report);
		void RunTick() noexcept;
		void Tick();
		void Publish(bool Force = false, bool PositionAnchored = false);
		void ClearLyricsLocked();
		bool SwitchSongLocked(uint64_t SongId);
		void ClearPendingLyricsLocked();

		std::atomic_bool m_Stop{true};
		std::atomic_bool m_TickFinished{true};
		std::thread m_TickThread;
		std::unique_ptr<COperations> m_pOwnedOperations;
		COperations *m_pOperations = nullptr;
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
