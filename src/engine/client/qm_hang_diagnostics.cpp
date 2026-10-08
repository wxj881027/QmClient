#include "qm_hang_diagnostics.h"

#include <base/system.h>
#include <base/windows.h>

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

	const char *DumpStageName(EDumpStage Stage)
	{
		switch(Stage)
		{
		case EDumpStage::LOAD_LIBRARY: return "load_library";
		case EDumpStage::FIND_WRITER: return "find_writer";
		case EDumpStage::OPEN_FILE: return "open_file";
		case EDumpStage::WRITE_DUMP: return "write_dump";
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
	SDumpResult WriteDump(const char *pFilename)
	{
		using MiniDumpWriteDumpFunc = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
			const MINIDUMP_EXCEPTION_INFORMATION *, const MINIDUMP_USER_STREAM_INFORMATION *, const MINIDUMP_CALLBACK_INFORMATION *);

		HMODULE pDbgHelp = LoadLibraryA("dbghelp.dll");
		if(pDbgHelp == nullptr)
			return {EDumpStage::LOAD_LIBRARY, GetLastError()};

		auto pMiniDumpWriteDump = (MiniDumpWriteDumpFunc)GetProcAddress(pDbgHelp, "MiniDumpWriteDump");
		if(pMiniDumpWriteDump == nullptr)
		{
			const DWORD Error = GetLastError();
			FreeLibrary(pDbgHelp);
			return {EDumpStage::FIND_WRITER, Error};
		}

		const std::wstring Filename = windows_utf8_to_wide(pFilename);
		HANDLE FileHandle = CreateFileW(Filename.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(FileHandle == INVALID_HANDLE_VALUE)
		{
			const DWORD Error = GetLastError();
			FreeLibrary(pDbgHelp);
			return {EDumpStage::OPEN_FILE, Error};
		}

		const MINIDUMP_TYPE DumpType = MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
		const BOOL Result = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, DumpType, nullptr, nullptr, nullptr);
		// 必须在关闭句柄和卸载 DLL 前保存原始错误码。
		const DWORD Error = Result ? ERROR_SUCCESS : GetLastError();
		CloseHandle(FileHandle);
		FreeLibrary(pDbgHelp);
		return {Result ? EDumpStage::COMPLETE : EDumpStage::WRITE_DUMP, Error};
	}
#endif
}
