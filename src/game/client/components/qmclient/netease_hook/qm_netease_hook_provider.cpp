#include "qm_netease_hook_provider.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	// v4 在共享块头部加入了运行时启停控制字段，不能与旧 DLL 共用映射。
	constexpr wchar_t SHARED_MAPPING_NAME[] = L"Local\\QmClient.NeteaseHook.v4";
	constexpr int STOP_ACK_TIMEOUT_MS = 3000;
	constexpr int STOP_MAPPING_WAIT_MS = 500;

	int64_t ReadActiveSequence(const volatile int64_t *pSequence)
	{
		// 映射可能只读，不能用 InterlockedCompareExchange64（即使交换值相同也带写入语义）。
		const int64_t Sequence = *pSequence;
		MemoryBarrier();
		return Sequence;
	}

	std::wstring Utf8ToWide(const char *pText)
	{
		if(pText == nullptr || pText[0] == '\0')
			return {};
		const int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pText, -1, nullptr, 0);
		if(Length <= 1)
			return {};
		std::wstring Result((size_t)Length, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pText, -1, Result.data(), Length);
		Result.resize((size_t)Length - 1);
		return Result;
	}

	std::wstring DefaultHelperPath()
	{
		wchar_t aPath[MAX_PATH];
		const DWORD Length = GetModuleFileNameW(nullptr, aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
			return {};
		std::wstring Path(aPath, Length);
		const size_t Slash = Path.find_last_of(L"\\/");
		if(Slash == std::wstring::npos)
			return L"qm-nmt-helper.exe";
		Path.resize(Slash + 1);
		Path.append(L"qm-nmt-helper.exe");
		return Path;
	}

	HANDLE LaunchHelper(const char *pHelperPath)
	{
#if defined(CONF_QMCLIENT_TEST_STORAGE)
		// 测试存储构建（集成/冒烟用的隔离客户端）不启动外部 hook 助手：
		// 它会向玩家正在运行的音乐客户端注入 DLL，既污染测试主机，
		// 也会因为新生成的无签名可执行文件被系统安全策略拦截而弹窗。
		return nullptr;
#endif
		std::wstring Path = Utf8ToWide(pHelperPath);
		if(Path.empty())
			Path = DefaultHelperPath();
		if(Path.empty())
			return nullptr;
		std::filesystem::path HelperPath(Path);
		std::filesystem::path HookPath = HelperPath.parent_path() / L"qm-nmt-hook64.dll";
		if(HookPath.empty())
			HookPath = L"qm-nmt-hook64.dll";
		std::wstring CommandLine = L"\"" + Path + L"\" --watch --hook \"" + HookPath.wstring() + L"\" --parent-pid " + std::to_wstring(GetCurrentProcessId());
		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		PROCESS_INFORMATION Process{};
		std::vector<wchar_t> Mutable(CommandLine.begin(), CommandLine.end());
		Mutable.push_back(L'\0');
		if(CreateProcessW(Path.c_str(), Mutable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &Startup, &Process))
		{
			CloseHandle(Process.hThread);
			return Process.hProcess;
		}
		return nullptr;
	}
}

struct CQmNeteaseHookProvider::SImpl
{
	HANDLE m_hMapping = nullptr;
	void *m_pView = nullptr;
	HANDLE m_hMappingV5 = nullptr;
	void *m_pViewV5 = nullptr;
	bool m_MappingWritable = false;
	uint64_t m_LastSequence = 0;
	uint32_t m_LastProducerPid = 0;
	uint64_t m_LastValidTick = 0;
	bool m_SawSequenceReset = false;
	bool m_HelperStarted = false;
	HANDLE m_hHelperProcess = nullptr;
	uint64_t m_LastV5Sequence = 0;
	uint32_t m_LastV5Pid = 0;

	// 以下字段跨越主线程与后台 Hook 线程，统一由 m_Mutex 保护。
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::thread m_Worker;
	bool m_StopRequested = false;
	bool m_DesiredEnabled = false;
	std::string m_DesiredHelperPath;
	int m_DesiredTimeoutMs = 1500;
	uint64_t m_ConfigGeneration = 0;
	std::string m_ActiveHelperPath;
	bool m_PublishedV5Valid = false;
	QmNeteaseHook::SSnapshotV5 m_PublishedV5{};
	std::atomic_bool m_Running{false};

	bool OpenV5()
	{
		if(m_pViewV5 != nullptr)
			return true;
		m_hMappingV5 = OpenFileMappingA(FILE_MAP_READ, FALSE, QmNeteaseHook::PROTOCOL_MAPPING_NAME_V5);
		if(m_hMappingV5 == nullptr)
			return false;
		m_pViewV5 = MapViewOfFile(m_hMappingV5, FILE_MAP_READ, 0, 0, sizeof(QmNeteaseHook::SSharedBlockV5));
		if(m_pViewV5 == nullptr)
		{
			CloseHandle(m_hMappingV5);
			m_hMappingV5 = nullptr;
			return false;
		}
		return true;
	}

	void CloseV5()
	{
		if(m_pViewV5 != nullptr)
		{
			UnmapViewOfFile(m_pViewV5);
			m_pViewV5 = nullptr;
		}
		if(m_hMappingV5 != nullptr)
		{
			CloseHandle(m_hMappingV5);
			m_hMappingV5 = nullptr;
		}
		m_LastV5Sequence = 0;
		m_LastV5Pid = 0;
	}

	bool OpenMapping(DWORD Access)
	{
		if(m_pView != nullptr)
			return true;
		m_hMapping = OpenFileMappingW(Access, FALSE, SHARED_MAPPING_NAME);
		if(m_hMapping == nullptr)
			return false;
		m_pView = MapViewOfFile(m_hMapping, Access, 0, 0, sizeof(QmNeteaseHook::SSharedBlock));
		if(m_pView == nullptr)
		{
			CloseHandle(m_hMapping);
			m_hMapping = nullptr;
			return false;
		}
		m_MappingWritable = (Access & FILE_MAP_WRITE) != 0;
		return true;
	}

	void CloseMapping()
	{
		if(m_pView != nullptr)
		{
			UnmapViewOfFile(m_pView);
			m_pView = nullptr;
		}
		if(m_hMapping != nullptr)
		{
			CloseHandle(m_hMapping);
			m_hMapping = nullptr;
		}
		m_MappingWritable = false;
	}

	bool EnsureWritableMapping()
	{
		if(m_pView != nullptr && m_MappingWritable)
			return true;
		CloseMapping();
		return OpenMapping(FILE_MAP_READ | FILE_MAP_WRITE);
	}

	bool RequestStop()
	{
		if(!EnsureWritableMapping())
			return false;
		auto *pShared = static_cast<volatile QmNeteaseHook::SSharedBlock *>(m_pView);
		InterlockedOr(reinterpret_cast<volatile LONG *>(&pShared->m_ControlFlags), (LONG)QmNeteaseHook::CONTROL_STOP_REQUESTED);
		MemoryBarrier();
		return true;
	}

	bool Resume()
	{
		if(!EnsureWritableMapping())
			return false;
		auto *pShared = static_cast<volatile QmNeteaseHook::SSharedBlock *>(m_pView);
		const LONG ClearMask = ~((LONG)QmNeteaseHook::CONTROL_STOP_REQUESTED | (LONG)QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED);
		InterlockedAnd(reinterpret_cast<volatile LONG *>(&pShared->m_ControlFlags), ClearMask);
		MemoryBarrier();
		return true;
	}

	bool WaitForStopAcknowledged(int TimeoutMs)
	{
		if(m_pView == nullptr || !m_MappingWritable)
			return false;
		const uint64_t Deadline = GetTickCount64() + (uint64_t)std::max(0, TimeoutMs);
		auto *pShared = static_cast<volatile QmNeteaseHook::SSharedBlock *>(m_pView);
		for(;;)
		{
			const uint32_t Flags = (uint32_t)InterlockedCompareExchange(
				reinterpret_cast<volatile LONG *>(&pShared->m_ControlFlags), 0, 0);
			if((Flags & QmNeteaseHook::CONTROL_STOP_ACKNOWLEDGED) != 0)
				return true;
			if(GetTickCount64() >= Deadline)
				return false;
			Sleep(10);
		}
	}

	void PublishV5(bool Valid, const QmNeteaseHook::SSnapshotV5 &Snapshot)
	{
		std::scoped_lock Lock(m_Mutex);
		m_PublishedV5Valid = Valid;
		if(Valid)
			m_PublishedV5 = Snapshot;
	}

	void StopHelper()
	{
		// 读取器可能尚未打开映射；给刚注入的 DLL 一个很短的初始化窗口。
		bool StopRequested = RequestStop();
		if(!StopRequested)
		{
			const uint64_t Deadline = GetTickCount64() + STOP_MAPPING_WAIT_MS;
			while(GetTickCount64() < Deadline && !StopRequested)
			{
				Sleep(10);
				StopRequested = RequestStop();
			}
		}
		if(StopRequested)
			WaitForStopAcknowledged(STOP_ACK_TIMEOUT_MS);

		if(m_hHelperProcess != nullptr)
		{
			if(WaitForSingleObject(m_hHelperProcess, 0) == WAIT_TIMEOUT)
			{
				// Helper 是本客户端创建的子进程；关闭 Hook 数据源时结束它的监听循环。
				TerminateProcess(m_hHelperProcess, 0);
				WaitForSingleObject(m_hHelperProcess, 3000);
			}
			CloseHandle(m_hHelperProcess);
			m_hHelperProcess = nullptr;
		}
		CloseMapping();
		CloseV5();
		m_LastSequence = 0;
		m_LastProducerPid = 0;
		m_LastValidTick = 0;
		m_SawSequenceReset = false;
		m_HelperStarted = false;
		m_ActiveHelperPath.clear();
		m_Running.store(false, std::memory_order_release);
		PublishV5(false, {});
	}

	bool ReadV5Direct(QmNeteaseHook::SSnapshotV5 *pSnapshot, int TimeoutMs)
	{
		if(pSnapshot == nullptr || !OpenV5())
			return false;
		QmNeteaseHook::SSnapshotV5 Candidate{};
		for(int Attempt = 0; Attempt < 3; ++Attempt)
		{
			auto *pShared = static_cast<const volatile QmNeteaseHook::SSharedBlockV5 *>(m_pViewV5);
			const uint64_t Begin = *reinterpret_cast<const volatile uint64_t *>(&pShared->m_Sequence);
			MemoryBarrier();
			if(Begin == 0 || (Begin & 1) != 0)
				continue;
			std::memcpy(&Candidate, (const void *)&pShared->m_Snapshot, sizeof(Candidate));
			MemoryBarrier();
			const uint64_t End = *reinterpret_cast<const volatile uint64_t *>(&pShared->m_Sequence);
			if(!QmNeteaseHook::IsStableSequenceV5(Begin, End) || Candidate.m_Sequence != End)
				continue;
			if(!QmNeteaseHook::ValidateSnapshotV5(Candidate))
				continue;
			const uint64_t Now = GetTickCount64();
			if(QmNeteaseHook::IsStaleV5(Candidate, Now, (uint64_t)std::max(1, TimeoutMs)))
				return false;
			if(m_LastV5Pid != 0 && Candidate.m_CloudMusicPid != m_LastV5Pid)
				m_LastV5Sequence = 0;
			if(m_LastV5Sequence != 0 && Candidate.m_Sequence < m_LastV5Sequence)
				m_LastV5Sequence = 0;
			m_LastV5Pid = Candidate.m_CloudMusicPid;
			m_LastV5Sequence = Candidate.m_Sequence;
			*pSnapshot = Candidate;
			return true;
		}
		return false;
	}

	void WorkerMain()
	{
		for(;;)
		{
			bool Enabled;
			std::string HelperPath;
			int TimeoutMs;
			uint64_t Generation;
			{
				std::scoped_lock Lock(m_Mutex);
				Enabled = m_DesiredEnabled;
				HelperPath = m_DesiredHelperPath;
				TimeoutMs = m_DesiredTimeoutMs;
				Generation = m_ConfigGeneration;
				if(m_StopRequested)
					break;
			}

			if(!Enabled)
			{
				if(m_HelperStarted || m_pView != nullptr || m_pViewV5 != nullptr)
					StopHelper();
				else
					PublishV5(false, {});
			}
			else
			{
				if(m_HelperStarted && m_ActiveHelperPath != HelperPath)
					StopHelper();
				if(!m_HelperStarted)
				{
					// 建映射与 CreateProcessW 都可能被系统安全扫描拖到数百毫秒，
					// 这里位于后台线程，不阻塞渲染与逻辑帧。
					Resume();
					m_hHelperProcess = LaunchHelper(HelperPath.c_str());
					m_HelperStarted = true;
					m_ActiveHelperPath = HelperPath;
				}

				QmNeteaseHook::SSnapshotV5 Snapshot{};
				const bool ReadOk = ReadV5Direct(&Snapshot, TimeoutMs);
				m_Running.store(m_pViewV5 != nullptr, std::memory_order_release);
				PublishV5(ReadOk, Snapshot);
			}

			std::unique_lock Lock(m_Mutex);
			m_Wake.wait_for(Lock, std::chrono::milliseconds(50), [this, Generation] {
				return m_StopRequested || m_ConfigGeneration != Generation;
			});
		}
		StopHelper();
	}
};

CQmNeteaseHookProvider::CQmNeteaseHookProvider() :
	m_pImpl(std::make_unique<SImpl>()) {}

CQmNeteaseHookProvider::~CQmNeteaseHookProvider()
{
	Stop();
}

void CQmNeteaseHookProvider::Start(const char *pHelperPath, int TimeoutMs)
{
	if(!m_pImpl)
		return;
	{
		std::scoped_lock Lock(m_pImpl->m_Mutex);
		m_pImpl->m_DesiredEnabled = true;
		m_pImpl->m_DesiredHelperPath = pHelperPath != nullptr ? pHelperPath : "";
		m_pImpl->m_DesiredTimeoutMs = TimeoutMs;
		m_pImpl->m_StopRequested = false;
		++m_pImpl->m_ConfigGeneration;
	}
	if(!m_pImpl->m_Worker.joinable())
	{
		try
		{
			m_pImpl->m_Worker = std::thread([this] { m_pImpl->WorkerMain(); });
		}
		catch(...)
		{
			return;
		}
	}
	m_pImpl->m_Wake.notify_all();
}

void CQmNeteaseHookProvider::RequestStop()
{
	if(!m_pImpl)
		return;
	{
		std::scoped_lock Lock(m_pImpl->m_Mutex);
		m_pImpl->m_DesiredEnabled = false;
		++m_pImpl->m_ConfigGeneration;
	}
	m_pImpl->m_Wake.notify_all();
}

void CQmNeteaseHookProvider::Stop()
{
	if(!m_pImpl)
		return;
	{
		std::scoped_lock Lock(m_pImpl->m_Mutex);
		m_pImpl->m_DesiredEnabled = false;
		m_pImpl->m_StopRequested = true;
		++m_pImpl->m_ConfigGeneration;
	}
	m_pImpl->m_Wake.notify_all();
	if(m_pImpl->m_Worker.joinable())
		m_pImpl->m_Worker.join();
	// 后台线程退出时已完成清理；这里只兜底处理线程从未启动的情况。
	if(m_pImpl->m_HelperStarted || m_pImpl->m_pView != nullptr || m_pImpl->m_pViewV5 != nullptr)
		m_pImpl->StopHelper();
}

bool CQmNeteaseHookProvider::Read(QmNeteaseHook::SSnapshot *pSnapshot, int TimeoutMs)
{
	if(pSnapshot == nullptr || !m_pImpl)
		return false;
	if(m_pImpl->m_pView == nullptr)
	{
		// 读取阶段优先保留写权限，关闭时才能向 DLL 发送协作式停止请求。
		if(!m_pImpl->OpenMapping(FILE_MAP_READ | FILE_MAP_WRITE))
			m_pImpl->OpenMapping(FILE_MAP_READ);
		if(m_pImpl->m_pView == nullptr)
			return false;
	}

	QmNeteaseHook::SSnapshot Candidate{};
	bool Stable = false;
	for(int Attempt = 0; Attempt < 3; ++Attempt)
	{
		const auto *pShared = static_cast<const volatile QmNeteaseHook::SSharedBlock *>(m_pImpl->m_pView);
		const int64_t Begin = ReadActiveSequence(&pShared->m_ActiveSequence);
		if(Begin <= 0 || (Begin & 1) != 0)
		{
			if(Begin <= 0)
				m_pImpl->m_SawSequenceReset = true;
			continue;
		}
		const size_t Slot = (size_t)((Begin / 2) & 1);
		MemoryBarrier();
		std::memcpy(&Candidate, (const void *)&pShared->m_aSnapshots[Slot], sizeof(Candidate));
		MemoryBarrier();
		const int64_t End = ReadActiveSequence(&pShared->m_ActiveSequence);
		if(QmNeteaseHook::IsStableSequence((uint64_t)Begin, (uint64_t)End) && Candidate.m_Sequence == (uint64_t)End)
		{
			Stable = true;
			break;
		}
		Candidate = {};
	}
	if(!Stable || !QmNeteaseHook::ValidateSnapshot(Candidate))
		return false;
	const uint64_t Now = GetTickCount64();
	if(m_pImpl->m_LastProducerPid != 0 && Candidate.m_ProducerPid != m_pImpl->m_LastProducerPid)
	{
		m_pImpl->m_LastSequence = 0;
		m_pImpl->m_LastValidTick = 0;
		m_pImpl->m_SawSequenceReset = false;
	}
	m_pImpl->m_LastProducerPid = Candidate.m_ProducerPid;
	if(m_pImpl->m_LastSequence != 0 && Candidate.m_Sequence < m_pImpl->m_LastSequence)
	{
		if(!m_pImpl->m_SawSequenceReset)
			return false;
		m_pImpl->m_LastSequence = 0;
		m_pImpl->m_LastValidTick = 0;
	}
	m_pImpl->m_SawSequenceReset = false;
	if(Candidate.m_Sequence != m_pImpl->m_LastSequence)
	{
		m_pImpl->m_LastSequence = Candidate.m_Sequence;
		m_pImpl->m_LastValidTick = Now;
	}
	else if(m_pImpl->m_LastValidTick == 0 || Now - m_pImpl->m_LastValidTick > (uint64_t)std::max(1, TimeoutMs))
	{
		return false;
	}
	*pSnapshot = Candidate;
	return true;
}

bool CQmNeteaseHookProvider::ReadV5(QmNeteaseHook::SSnapshotV5 *pSnapshot, int TimeoutMs)
{
	if(pSnapshot == nullptr || !m_pImpl)
		return false;
	std::scoped_lock Lock(m_pImpl->m_Mutex);
	if(!m_pImpl->m_PublishedV5Valid)
		return false;
	if(QmNeteaseHook::IsStaleV5(m_pImpl->m_PublishedV5, GetTickCount64(), (uint64_t)std::max(1, TimeoutMs)))
		return false;
	*pSnapshot = m_pImpl->m_PublishedV5;
	return true;
}

bool CQmNeteaseHookProvider::IsRunning() const
{
	return m_pImpl != nullptr && m_pImpl->m_Running.load(std::memory_order_acquire);
}

#else

struct CQmNeteaseHookProvider::SImpl
{
	void CloseV5() {}
};

CQmNeteaseHookProvider::CQmNeteaseHookProvider() :
	m_pImpl(std::make_unique<SImpl>()) {}
CQmNeteaseHookProvider::~CQmNeteaseHookProvider() = default;
void CQmNeteaseHookProvider::Start(const char *, int) {}
void CQmNeteaseHookProvider::RequestStop() {}
void CQmNeteaseHookProvider::Stop() {}
bool CQmNeteaseHookProvider::Read(QmNeteaseHook::SSnapshot *, int) { return false; }
bool CQmNeteaseHookProvider::ReadV5(QmNeteaseHook::SSnapshotV5 *, int) { return false; }
bool CQmNeteaseHookProvider::IsRunning() const { return false; }

#endif
