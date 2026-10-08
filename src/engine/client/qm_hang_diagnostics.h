#ifndef ENGINE_CLIENT_QM_HANG_DIAGNOSTICS_H
#define ENGINE_CLIENT_QM_HANG_DIAGNOSTICS_H

#include <base/detect.h>
#include <base/types.h>

#include <engine/client.h>

#include <atomic>
#include <cstdint>
#include <mutex>

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

	public:
		// 只在复制快照时持锁，调用方的字符串整理、文件写入和转储均在锁外。
		void Publish(const SSnapshot &Snapshot);
		SSnapshot Read() const;
		int64_t LastHeartbeat() const;
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
	SDumpResult WriteDump(const char *pFilename);
#endif
}

#endif
