#include "qm_hang_diagnostics.h"

#include <base/crash_output.h>
#include <base/crash_output_windows.h>
#include <base/system.h>
#include <base/windows.h>

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <dbghelp.h>
#endif

namespace QmHangDiagnostics
{
	void CSnapshotStore::Publish(const SSnapshot &Snapshot)
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Snapshot = Snapshot;
		m_HaveHeartbeat = true;
		m_LastHeartbeat.store(Snapshot.m_LastHeartbeat, std::memory_order_release);
	}

	SSnapshot CSnapshotStore::Read() const
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		return m_Snapshot;
	}

	int64_t CSnapshotStore::LastHeartbeat() const
	{
		return m_LastHeartbeat.load(std::memory_order_acquire);
	}

	void CSnapshotStore::StartWatchdog()
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		m_WatchdogActive = true;
		m_HaveHeartbeat = false;
		m_ReportClaimed = false;
		m_Snapshot = {};
		m_LastHeartbeat.store(0, std::memory_order_release);
	}

	void CSnapshotStore::StopWatchdog()
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		m_WatchdogActive = false;
	}

	std::optional<SSnapshot> CSnapshotStore::TryClaimReport(int64_t Now, int64_t TimeoutNanoseconds)
	{
		const std::lock_guard<std::mutex> Lock(m_Mutex);
		if(!m_WatchdogActive || !m_HaveHeartbeat || m_ReportClaimed || Now < m_Snapshot.m_LastHeartbeat || Now - m_Snapshot.m_LastHeartbeat < TimeoutNanoseconds)
			return std::nullopt;
		m_ReportClaimed = true;
		return m_Snapshot;
	}

	CWatchdog::~CWatchdog()
	{
		Stop();
	}

	bool CWatchdog::Start(const SSnapshot &Initial, int64_t TimeoutNanoseconds, FReport pfnReport, void *pUser, std::chrono::milliseconds PollInterval, FCreateThread pfnCreate) noexcept
	{
		if(Started())
			return true;
		if(pfnReport == nullptr || TimeoutNanoseconds <= 0 || PollInterval.count() <= 0)
			return false;
		try
		{
			m_Stop.store(false, std::memory_order_release);
			m_Store.StartWatchdog();
			m_Store.Publish(Initial);
			std::function<void()> Entry = [this, TimeoutNanoseconds, pfnReport, pUser, PollInterval] {
				Run(TimeoutNanoseconds, pfnReport, pUser, PollInterval);
			};
			m_Thread = pfnCreate ? pfnCreate(std::move(Entry)) : std::thread(std::move(Entry));
			if(Started())
				return true;
		}
		catch(...)
		{
		}
		// std::function/线程申请失败或创建器返回空线程时，不留下活动报告状态。
		m_Stop.store(true, std::memory_order_release);
		m_Store.StopWatchdog();
		return false;
	}

	void CWatchdog::Run(int64_t TimeoutNanoseconds, FReport pfnReport, void *pUser, std::chrono::milliseconds PollInterval) noexcept
	{
		try
		{
			while(!m_Stop.load(std::memory_order_acquire))
			{
				std::this_thread::sleep_for(PollInterval);
				if(m_Stop.load(std::memory_order_acquire))
					break;
				// 单调时钟不依赖主循环 set_new_tick，主线程阻塞时仍能识别超时。
				const int64_t Now = time_get_nanoseconds().count();
				const auto Snapshot = m_Store.TryClaimReport(Now, TimeoutNanoseconds);
				if(!Snapshot.has_value())
					continue;
				try
				{
					pfnReport(pUser, Now, *Snapshot);
				}
				catch(...)
				{
					// 报告是尽力采集；失败也保留单次领取，不能从后台终止客户端。
				}
			}
		}
		catch(...)
		{
			m_Stop.store(true, std::memory_order_release);
			m_Store.StopWatchdog();
		}
	}

	void CWatchdog::Stop()
	{
		m_Store.StopWatchdog();
		m_Stop.store(true, std::memory_order_release);
		if(m_Thread.joinable())
			m_Thread.join();
	}

	const char *DumpStageName(EDumpStage Stage)
	{
		switch(Stage)
		{
		case EDumpStage::LOAD_LIBRARY: return "load_library";
		case EDumpStage::FIND_WRITER: return "find_writer";
		case EDumpStage::OPEN_FILE: return "open_file";
		case EDumpStage::WRITE_DUMP: return "write_dump";
		case EDumpStage::FLUSH_FILE: return "flush_file";
		case EDumpStage::CLOSE_FILE: return "close_file";
		case EDumpStage::PUBLISH_FILE: return "publish_file";
		case EDumpStage::COMPLETE: return "complete";
		}
		return "unknown";
	}

	bool AppendDumpResult(const char *pReportPath, const char *pDumpPath, const SDumpResult &Result)
	{
		IOHANDLE File = io_open(pReportPath, IOFLAG_APPEND);
		if(!File)
			return false;

		char aBuf[IO_MAX_PATH_LENGTH + 256];
		str_format(aBuf, sizeof(aBuf),
			"Minidump status: %s\n"
			"Minidump path: %s\n"
			"Minidump stage: %s\n"
			"Minidump error: %lu\n",
			Result.Written() ? "written" : "failed", pDumpPath, DumpStageName(Result.m_Stage), Result.m_Error);
		const unsigned Length = str_length(aBuf);
		const bool Written = io_write(File, aBuf, Length) == Length;
		const bool Synced = io_sync(File) == 0;
		const bool Closed = io_close(File) == 0;
		return Written && Synced && Closed;
	}

#if defined(CONF_FAMILY_WINDOWS)
	SDumpResult WriteDump(const char *pFilename, FEncodeFilename pfnEncode, const crash_output::SWindowsFileOperations *pOperations) noexcept
	{
		using MiniDumpWriteDumpFunc = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
			const MINIDUMP_EXCEPTION_INFORMATION *, const MINIDUMP_USER_STREAM_INFORMATION *, const MINIDUMP_CALLBACK_INFORMATION *);
		if(!pFilename || !pFilename[0] || !str_utf8_check(pFilename))
			return {EDumpStage::OPEN_FILE, ERROR_INVALID_PARAMETER};
		EDumpStage Stage = EDumpStage::LOAD_LIBRARY;
		try
		{
			std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> Module(LoadLibraryA("dbghelp.dll"), &FreeLibrary);
			if(!Module)
				return {Stage, GetLastError()};
			Stage = EDumpStage::FIND_WRITER;
			auto pMiniDumpWriteDump = (MiniDumpWriteDumpFunc)GetProcAddress(Module.get(), "MiniDumpWriteDump");
			if(pMiniDumpWriteDump == nullptr)
				return {Stage, GetLastError()};
			Stage = EDumpStage::OPEN_FILE;
			char aCandidate[IO_MAX_PATH_LENGTH];
			if(!crash_output::AppendPathSuffix(pFilename, ".tmp", aCandidate, sizeof(aCandidate)))
				return {Stage, ERROR_FILENAME_EXCED_RANGE};
			const std::wstring Filename = pfnEncode ? pfnEncode(pFilename) : windows_utf8_to_wide(pFilename);
			const std::wstring Candidate = pfnEncode ? pfnEncode(aCandidate) : windows_utf8_to_wide(aCandidate);
			HANDLE FileHandle = CreateFileW(Candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(FileHandle == INVALID_HANDLE_VALUE)
				return {Stage, GetLastError()};
			crash_output::CWindowsFile File(FileHandle, pOperations);
			crash_output::CWriter Output(File);
			Stage = EDumpStage::WRITE_DUMP;
			const MINIDUMP_TYPE DumpType = MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
			const BOOL Result = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, DumpType, nullptr, nullptr, nullptr);
			if(!Result)
				File.Fail(GetLastError());
			if(!Output.Finish(Result != FALSE))
			{
				using EOperation = crash_output::CWindowsFile::EOperation;
				if(File.FailedOperation() == EOperation::FLUSH)
					Stage = EDumpStage::FLUSH_FILE;
				else if(File.FailedOperation() == EOperation::CLOSE)
					Stage = EDumpStage::CLOSE_FILE;
				return {Stage, File.Error()};
			}
			if(!crash_output::PublishWindowsOutput(Candidate.c_str(), Filename.c_str()))
			{
				File.Fail(GetLastError(), crash_output::CWindowsFile::EOperation::PUBLISH);
				return {EDumpStage::PUBLISH_FILE, File.Error()};
			}
			return {EDumpStage::COMPLETE, ERROR_SUCCESS};
		}
		catch(const std::bad_alloc &)
		{
			return {Stage, ERROR_NOT_ENOUGH_MEMORY};
		}
		catch(...)
		{
			return {Stage, ERROR_GEN_FAILURE};
		}
	}
#endif
}
