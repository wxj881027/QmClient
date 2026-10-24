#ifndef ENGINE_CLIENT_QM_HANG_DIAGNOSTICS_H
#define ENGINE_CLIENT_QM_HANG_DIAGNOSTICS_H

#include <base/detect.h>
#include <base/types.h>

#include <engine/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace QmHangDiagnostics
{
	struct SSnapshot
	{
		int m_State = IClient::STATE_OFFLINE;
		char m_aCurrentMap[IO_MAX_PATH_LENGTH] = "";
		char m_aServerAddr[NETADDR_MAXSTRSIZE] = "";
		int64_t m_LastHeartbeat = 0;
	};

	class CSnapshotStore
	{
		mutable std::mutex m_Mutex;
		SSnapshot m_Snapshot;
		std::atomic<int64_t> m_LastHeartbeat{0};
		bool m_WatchdogActive = false;
		bool m_HaveHeartbeat = false;
		bool m_ReportClaimed = false;

	public:
		// 快照和报告状态操作仅短暂持锁，调用方的文件写入和转储均在锁外。
		void Publish(const SSnapshot &Snapshot);
		SSnapshot Read() const;
		int64_t LastHeartbeat() const;
		void StartWatchdog();
		void StopWatchdog();
		// 超时、停止与单次报告领取共用快照锁；返回的副本可在锁外写盘。
		std::optional<SSnapshot> TryClaimReport(int64_t Now, int64_t TimeoutNanoseconds);
	};

	// 生命周期操作由调用方串行管理；Stop 等待报告回调结束，回调不借用更短命的对象。
	class CWatchdog
	{
	public:
		using FReport = void (*)(void *, int64_t, const SSnapshot &);
		using FCreateThread = std::thread (*)(std::function<void()>);

	private:
		CSnapshotStore &m_Store;
		std::atomic<bool> m_Stop{true};
		std::thread m_Thread;

		void Run(int64_t TimeoutNanoseconds, FReport pfnReport, void *pUser, std::chrono::milliseconds PollInterval) noexcept;

	public:
		explicit CWatchdog(CSnapshotStore &Store) noexcept :
			m_Store(Store) {}
		~CWatchdog();
		CWatchdog(const CWatchdog &) = delete;
		CWatchdog &operator=(const CWatchdog &) = delete;

		bool Started() const noexcept { return m_Thread.joinable(); }
		bool Start(const SSnapshot &Initial, int64_t TimeoutNanoseconds, FReport pfnReport, void *pUser,
			std::chrono::milliseconds PollInterval = std::chrono::seconds(1), FCreateThread pfnCreate = nullptr) noexcept;
		void Stop();
	};

	enum class EDumpStage
	{
		LOAD_LIBRARY,
		FIND_WRITER,
		OPEN_FILE,
		WRITE_DUMP,
		COMPLETE,
	};

	struct SDumpResult
	{
		EDumpStage m_Stage;
		unsigned long m_Error;
		bool Written() const { return m_Stage == EDumpStage::COMPLETE && m_Error == 0; }
	};

	const char *DumpStageName(EDumpStage Stage);
	// 原始卡死报告先落盘；转储返回后再追加结果，避免转储阻塞时连文本证据也丢失。
	bool AppendDumpResult(const char *pReportPath, const char *pDumpPath, const SDumpResult &Result);

#if defined(CONF_FAMILY_WINDOWS)
	using FEncodeFilename = std::wstring (*)(const char *);
	SDumpResult WriteDump(const char *pFilename, FEncodeFilename pfnEncode = nullptr) noexcept;
#endif
}

#endif
