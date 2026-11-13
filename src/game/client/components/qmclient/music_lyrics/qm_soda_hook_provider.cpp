#include "qm_soda_hook_provider.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr uint64_t HELPER_RETRY_MS = 5000;

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

	std::wstring DefaultHelperPath(bool MusicHelper)
	{
		wchar_t aPath[MAX_PATH];
		const DWORD Length = GetModuleFileNameW(nullptr, aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
			return {};
		std::wstring Path(aPath, Length);
		const size_t Slash = Path.find_last_of(L"\\/");
		if(Slash == std::wstring::npos)
			return MusicHelper ? L"qm-music-helper.exe" : L"qm-soda-helper.exe";
		Path.resize(Slash + 1);
		Path.append(MusicHelper ? L"qm-music-helper.exe" : L"qm-soda-helper.exe");
		return Path;
	}

	struct SPreparedHelperLaunch
	{
		std::wstring m_Path;
		std::vector<wchar_t> m_CommandLine;
		std::string m_Error;
		DWORD m_PathError = ERROR_SUCCESS;
	};

	SPreparedHelperLaunch PrepareHelperLaunch(const char *pHelperPath, const std::string &Source)
	{
		SPreparedHelperLaunch Prepared;
		Prepared.m_Path = Utf8ToWide(pHelperPath);
		if(Prepared.m_Path.empty())
			Prepared.m_Path = DefaultHelperPath(Source != "soda");
		if(Prepared.m_Path.empty())
			Prepared.m_PathError = GetLastError();
		else
		{
			const std::wstring CommandLine = L"\"" + Prepared.m_Path + L"\" --parent-pid " + std::to_wstring(GetCurrentProcessId()) +
							 (Source == "soda" ? L"" : L" --source " + Utf8ToWide(Source.c_str()));
			Prepared.m_CommandLine.assign(CommandLine.begin(), CommandLine.end());
			Prepared.m_CommandLine.push_back(L'\0');
		}
		// 在启动进程前备齐错误文字的可写空间，之后只缩短并交换所有权。
		Prepared.m_Error.resize(128);
		return Prepared;
	}

	HANDLE LaunchPreparedHelper(const std::wstring &Path, std::vector<wchar_t> &CommandLine, DWORD &Error)
	{
		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		PROCESS_INFORMATION Process{};
		if(CreateProcessW(Path.c_str(), CommandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &Startup, &Process))
		{
			CloseHandle(Process.hThread);
			return Process.hProcess;
		}
		Error = GetLastError();
		return nullptr;
	}

	void PublishLaunchError(std::string &Error, std::string &PreparedError, const char *pFormat, DWORD Code)
	{
		std::snprintf(PreparedError.data(), PreparedError.size(), pFormat, (unsigned long)Code);
		PreparedError.resize(std::strlen(PreparedError.c_str()));
		Error.swap(PreparedError);
	}
}

struct CQmSodaHookProvider::SImpl
{
	HANDLE m_hMapping = nullptr;
	void *m_pView = nullptr;
	HANDLE m_hHelperProcess = nullptr;
	std::string m_Source = "soda";
	std::string m_Error;
	uint64_t m_NextLaunchTick = 0;
	uint64_t m_LastSequence = 0;
	uint32_t m_LastPid = 0;

	bool OpenMapping(DWORD Access)
	{
		if(m_pView != nullptr)
			return true;
		m_hMapping = OpenFileMappingW(Access, FALSE, m_Source == "kugou" ? L"Local\\QmClient.KugouHook.v1" : m_Source == "qqmusic" ? L"Local\\QmClient.QQMusicHook.v1" :
																	     QmSodaHook::PROTOCOL_MAPPING_NAME_W);
		if(m_hMapping == nullptr)
			return false;
		m_pView = MapViewOfFile(m_hMapping, Access, 0, 0, sizeof(QmSodaHook::SSharedBlock));
		if(m_pView == nullptr)
		{
			CloseHandle(m_hMapping);
			m_hMapping = nullptr;
			return false;
		}
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
		m_LastSequence = 0;
		m_LastPid = 0;
	}
};

CQmSodaHookProvider::CQmSodaHookProvider() :
	m_pImpl(std::make_unique<SImpl>()) {}

CQmSodaHookProvider::~CQmSodaHookProvider()
{
	Stop();
}

bool CQmSodaHookProvider::Start(const char *pHelperPath, const char *pSource)
try
{
	const std::string_view Source = pSource != nullptr ? pSource : "soda";
	const bool SourceChanged = Source != m_pImpl->m_Source;
	if(!SourceChanged && m_pImpl->m_hHelperProcess != nullptr && WaitForSingleObject(m_pImpl->m_hHelperProcess, 0) == WAIT_TIMEOUT)
		return true;
	const uint64_t Now = GetTickCount64();
	if(!SourceChanged && Now < m_pImpl->m_NextLaunchTick)
		return true;
	std::string PreparedSource(Source);
	auto Prepared = PrepareHelperLaunch(pHelperPath, PreparedSource);
	// 仅完整准备后才退休旧句柄、切来源并开始一次实际启动的节流。
	if(SourceChanged)
	{
		Stop();
		m_pImpl->m_Source.swap(PreparedSource);
	}
	else if(m_pImpl->m_hHelperProcess != nullptr)
	{
		CloseHandle(m_pImpl->m_hHelperProcess);
		m_pImpl->m_hHelperProcess = nullptr;
	}
	m_pImpl->m_NextLaunchTick = Now + HELPER_RETRY_MS;
	DWORD Error = Prepared.m_PathError;
	m_pImpl->m_hHelperProcess = Prepared.m_Path.empty() ? nullptr : LaunchPreparedHelper(Prepared.m_Path, Prepared.m_CommandLine, Error);
	if(m_pImpl->m_hHelperProcess == nullptr)
		PublishLaunchError(m_pImpl->m_Error, Prepared.m_Error, "歌词采集器启动失败（Windows 错误 %lu）", Error);
	else
		m_pImpl->m_Error.clear();
	return true;
}
catch(const std::bad_alloc &)
{
	// 保留原来源、句柄及重试时刻，下帧重新准备。
	return false;
}
catch(const std::length_error &)
{
	return false;
}

bool CQmSodaHookProvider::RunKugouSetup(bool Restore)
try
{
	const std::wstring Path = DefaultHelperPath(true);
	if(Path.empty())
		return false;
	const std::wstring Command = L"\"" + Path + (Restore ? L"\" --kugou-restore" : L"\" --kugou-setup");
	std::vector<wchar_t> Mutable(Command.begin(), Command.end());
	Mutable.push_back(L'\0');
	std::string PreparedError(160, '\0');
	DWORD Error = ERROR_SUCCESS;
	const HANDLE hProcess = LaunchPreparedHelper(Path, Mutable, Error);
	if(hProcess == nullptr)
	{
		PublishLaunchError(m_pImpl->m_Error, PreparedError, "无法启动酷狗接入程序（Windows 错误 %lu）", Error);
		return false;
	}
	CloseHandle(hProcess);
	return true;
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
}

bool CQmSodaHookProvider::GetStatus(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	QmSodaHook::CopyUtf8Truncated(pBuffer, BufferSize, m_pImpl->m_Error.data(), m_pImpl->m_Error.size());
	return pBuffer[0] != '\0';
}

void CQmSodaHookProvider::Stop()
{
	if(!m_pImpl)
		return;
	if(m_pImpl->m_hHelperProcess != nullptr)
	{
		if(WaitForSingleObject(m_pImpl->m_hHelperProcess, 0) == WAIT_TIMEOUT)
		{
			// Helper 是本客户端创建的子进程;关闭 Hook 数据源时结束它的监听循环。
			TerminateProcess(m_pImpl->m_hHelperProcess, 0);
		}
		CloseHandle(m_pImpl->m_hHelperProcess);
		m_pImpl->m_hHelperProcess = nullptr;
	}
	m_pImpl->m_NextLaunchTick = 0;
	m_pImpl->m_Error.clear();
	m_pImpl->CloseMapping();
}

bool CQmSodaHookProvider::Read(QmSodaHook::SSnapshot *pSnapshot, int TimeoutMs)
{
	if(pSnapshot == nullptr || !m_pImpl)
		return false;
	if(m_pImpl->m_pView == nullptr && !m_pImpl->OpenMapping(FILE_MAP_READ))
		return false;
	QmSodaHook::SSnapshot Candidate{};
	bool Stable = false;
	for(int Attempt = 0; Attempt < 3; ++Attempt)
	{
		const auto *pShared = static_cast<const volatile QmSodaHook::SSharedBlock *>(m_pImpl->m_pView);
		const uint64_t Begin = *reinterpret_cast<const volatile uint64_t *>(&pShared->m_Sequence);
		MemoryBarrier();
		if(Begin == 0 || (Begin & 1) != 0)
			continue;
		std::memcpy(&Candidate, (const void *)&pShared->m_Snapshot, sizeof(Candidate));
		MemoryBarrier();
		const uint64_t End = *reinterpret_cast<const volatile uint64_t *>(&pShared->m_Sequence);
		if(!QmSodaHook::IsStableSequence(Begin, End) || Candidate.m_Sequence != End)
			continue;
		if(!QmSodaHook::ValidateSnapshot(Candidate))
			continue;
		const uint64_t Now = GetTickCount64();
		if(QmSodaHook::IsStale(Candidate, Now, (uint64_t)std::max(1, TimeoutMs)))
			return false;
		if(m_pImpl->m_LastPid != 0 && Candidate.m_SodaMusicPid != m_pImpl->m_LastPid)
			m_pImpl->m_LastSequence = 0;
		if(m_pImpl->m_LastSequence != 0 && Candidate.m_Sequence < m_pImpl->m_LastSequence)
			m_pImpl->m_LastSequence = 0;
		m_pImpl->m_LastPid = Candidate.m_SodaMusicPid;
		m_pImpl->m_LastSequence = Candidate.m_Sequence;
		*pSnapshot = Candidate;
		Stable = true;
		break;
	}
	return Stable;
}

bool CQmSodaHookProvider::IsRunning() const
{
	return m_pImpl != nullptr && m_pImpl->m_pView != nullptr;
}

#else

struct CQmSodaHookProvider::SImpl
{
};

CQmSodaHookProvider::CQmSodaHookProvider() :
	m_pImpl(std::make_unique<SImpl>()) {}
CQmSodaHookProvider::~CQmSodaHookProvider() = default;
bool CQmSodaHookProvider::Start(const char *, const char *) { return true; }
bool CQmSodaHookProvider::RunKugouSetup(bool) { return false; }
bool CQmSodaHookProvider::GetStatus(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer != nullptr && BufferSize > 0)
		*pBuffer = '\0';
	return false;
}
void CQmSodaHookProvider::Stop() {}
bool CQmSodaHookProvider::Read(QmSodaHook::SSnapshot *, int) { return false; }
bool CQmSodaHookProvider::IsRunning() const { return false; }

#endif
