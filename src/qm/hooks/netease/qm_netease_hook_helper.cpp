// 网易云音乐 Hook Helper：只发现并注入已经运行的主进程。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "qm_netease_bootstrap.h"
#include "qm_netease_frontend_bridge.h"
#include "qm_netease_helper_input.h"
#include "qm_netease_remote_injection.h"

#include <windows.h>

#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
	constexpr wchar_t TARGET_EXE[] = L"cloudmusic.exe";
	constexpr wchar_t TARGET_DLL[] = L"qm-nmt-hook64.dll";
	constexpr wchar_t HELPER_MUTEX_NAME[] = L"Local\\QmClient.NeteaseHook.Helper.v5";
	constexpr wchar_t APP_PATHS_KEY[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\cloudmusic.exe";
	constexpr wchar_t NETEASE_KEY[] = L"SOFTWARE\\NetEase\\CloudMusic";

	struct CHandleCloser
	{
		void operator()(void *pHandle) const noexcept
		{
			if(pHandle && pHandle != INVALID_HANDLE_VALUE)
				CloseHandle(pHandle);
		}
	};
	using CWindowsHandle = std::unique_ptr<void, CHandleCloser>;

	std::wstring Lower(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t C) { return (wchar_t)towlower(C); });
		return Value;
	}

	bool IsTargetArchitecture(HANDLE hProcess)
	{
		USHORT ProcessMachine = 0;
		USHORT NativeMachine = 0;
		using TIsWow64Process2 = BOOL(WINAPI *)(HANDLE, USHORT *, USHORT *);
		const auto pfn = reinterpret_cast<TIsWow64Process2>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
		if(pfn == nullptr || !pfn(hProcess, &ProcessMachine, &NativeMachine))
			return false;
		return ProcessMachine == IMAGE_FILE_MACHINE_UNKNOWN && NativeMachine == IMAGE_FILE_MACHINE_AMD64;
	}

	struct SCandidate
	{
		DWORD m_Pid = 0;
		DWORD m_ParentPid = 0;
		std::wstring m_Path;
		std::wstring m_CommandLine;
	};

	struct SUnicodeString
	{
		USHORT m_Length = 0;
		USHORT m_MaximumLength = 0;
		PWSTR m_Buffer = nullptr;
	};

	struct SProcessBasicInformation
	{
		PVOID m_Reserved1 = nullptr;
		PVOID m_PebBaseAddress = nullptr;
		PVOID m_Reserved2[2]{};
		ULONG_PTR m_UniqueProcessId = 0;
		PVOID m_Reserved3 = nullptr;
	};

	struct SPeb
	{
		BYTE m_Reserved1[2]{};
		BYTE m_BeingDebugged = 0;
		BYTE m_Reserved2[1]{};
		PVOID m_Reserved3[2]{};
		PVOID m_Ldr = nullptr;
		PVOID m_ProcessParameters = nullptr;
	};

	struct SProcessParametersPrefix
	{
		BYTE m_Reserved1[16]{};
		PVOID m_Reserved2[10]{};
		SUnicodeString m_ImagePathName;
		SUnicodeString m_CommandLine;
	};

	std::wstring QueryCommandLine(HANDLE hProcess)
	{
		using TNtQueryInformationProcess = LONG(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);
		const auto pfn = reinterpret_cast<TNtQueryInformationProcess>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
		if(pfn == nullptr)
			return {};
		SProcessBasicInformation BasicInfo;
		ULONG ReturnLength = 0;
		if(pfn(hProcess, 0, &BasicInfo, sizeof(BasicInfo), &ReturnLength) != 0 || BasicInfo.m_PebBaseAddress == nullptr)
			return {};
		SPeb Peb;
		SIZE_T Read = 0;
		if(!ReadProcessMemory(hProcess, BasicInfo.m_PebBaseAddress, &Peb, sizeof(Peb), &Read) || Read != sizeof(Peb) || Peb.m_ProcessParameters == nullptr)
			return {};
		SProcessParametersPrefix Parameters;
		if(!ReadProcessMemory(hProcess, Peb.m_ProcessParameters, &Parameters, sizeof(Parameters), &Read) || Read != sizeof(Parameters) ||
			Parameters.m_CommandLine.m_Buffer == nullptr || Parameters.m_CommandLine.m_Length == 0 ||
			!QmNeteaseHelper::ValidWideByteCount(Parameters.m_CommandLine.m_Length, Parameters.m_CommandLine.m_MaximumLength))
			return {};
		const size_t CharCount = Parameters.m_CommandLine.m_Length / sizeof(wchar_t);
		std::wstring Result(CharCount, L'\0');
		if(!ReadProcessMemory(hProcess, Parameters.m_CommandLine.m_Buffer, Result.data(), Parameters.m_CommandLine.m_Length, &Read) ||
			Read != Parameters.m_CommandLine.m_Length)
			return {};
		return Result;
	}

	bool IsRendererCommandLine(const std::wstring &CommandLine)
	{
		const std::wstring LowerCommandLine = Lower(CommandLine);
		return LowerCommandLine.find(L"--type=") != std::wstring::npos || LowerCommandLine.find(L"--type ") != std::wstring::npos;
	}

	std::vector<SCandidate> FindCandidates()
	{
		std::vector<SCandidate> Result;
		const CWindowsHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		const HANDLE hSnapshot = Snapshot.get();
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return Result;
		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		if(Process32FirstW(hSnapshot, &Entry))
		{
			do
			{
				if(Lower(Entry.szExeFile) != TARGET_EXE)
					continue;
				const CWindowsHandle Process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, Entry.th32ProcessID));
				const HANDLE hProcess = Process.get();
				if(hProcess == nullptr || !IsTargetArchitecture(hProcess))
				{
					continue;
				}
				std::wstring Path;
				for(DWORD Capacity = MAX_PATH; Capacity <= 32768; Capacity *= 2)
				{
					std::wstring Buffer(Capacity, L'\0');
					DWORD PathLength = Capacity;
					if(QueryFullProcessImageNameW(hProcess, 0, Buffer.data(), &PathLength))
					{
						Buffer.resize(PathLength);
						Path = std::move(Buffer);
						break;
					}
					if(GetLastError() != ERROR_INSUFFICIENT_BUFFER)
						break;
				}
				if(!Path.empty())
					Result.push_back({Entry.th32ProcessID, Entry.th32ParentProcessID, std::move(Path), QueryCommandLine(hProcess)});
			} while(Process32NextW(hSnapshot, &Entry));
		}
		return Result;
	}

	bool MatchesTargetBuild(const SCandidate &Candidate);

	std::optional<SCandidate> FindMainProcess()
	{
		const std::vector<SCandidate> Candidates = FindCandidates();
		for(const SCandidate &Candidate : Candidates)
		{
			if(IsRendererCommandLine(Candidate.m_CommandLine))
				continue;
			const bool HasCloudMusicParent = std::any_of(Candidates.begin(), Candidates.end(), [&Candidate](const SCandidate &Other) {
				return Candidate.m_ParentPid == Other.m_Pid && Other.m_Pid != Candidate.m_Pid;
			});
			if(!HasCloudMusicParent && MatchesTargetBuild(Candidate))
				return Candidate;
		}
		return std::nullopt;
	}

	std::wstring ModuleDirectory()
	{
		wchar_t aPath[MAX_PATH];
		const DWORD Length = GetModuleFileNameW(nullptr, aPath, (DWORD)std::size(aPath));
		if(Length == 0 || Length >= std::size(aPath))
			return {};
		std::wstring Path(aPath, Length);
		const size_t Slash = Path.find_last_of(L"\\/");
		return Slash == std::wstring::npos ? L"." : Path.substr(0, Slash);
	}

	std::wstring ModulePath()
	{
		std::vector<wchar_t> Buffer(MAX_PATH);
		for(;;)
		{
			const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), (DWORD)Buffer.size());
			if(Length == 0)
				return {};
			if(Length < Buffer.size())
				return std::wstring(Buffer.data(), Length);
			if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || Buffer.size() >= 32768)
				return {};
			Buffer.resize(std::min<size_t>(Buffer.size() * 2, 32768));
		}
	}

	std::wstring QueryRegistryString(HKEY hRoot, const wchar_t *pSubKey, const wchar_t *pValueName, REGSAM View)
	{
		HKEY hKey = nullptr;
		if(RegOpenKeyExW(hRoot, pSubKey, 0, KEY_QUERY_VALUE | View, &hKey) != ERROR_SUCCESS)
			return {};
		std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> Key(hKey, RegCloseKey);
		DWORD Type = 0;
		DWORD ByteSize = 0;
		LONG Result = RegQueryValueExW(hKey, pValueName, nullptr, &Type, nullptr, &ByteSize);
		if(Result != ERROR_SUCCESS || (Type != REG_SZ && Type != REG_EXPAND_SZ) || ByteSize < sizeof(wchar_t) || ByteSize > 32768 * sizeof(wchar_t) || !QmNeteaseHelper::ValidWideByteCount(ByteSize, 32768 * sizeof(wchar_t)))
		{
			return {};
		}
		std::vector<wchar_t> Buffer(ByteSize / sizeof(wchar_t) + 1, L'\0');
		Result = RegQueryValueExW(hKey, pValueName, nullptr, &Type, reinterpret_cast<BYTE *>(Buffer.data()), &ByteSize);
		Key.reset();
		if(Result != ERROR_SUCCESS || (Type != REG_SZ && Type != REG_EXPAND_SZ) ||
			!QmNeteaseHelper::ValidWideByteCount(ByteSize, (Buffer.size() - 1) * sizeof(wchar_t)))
			return {};
		std::wstring Value = QmNeteaseHelper::CopyWideBytes(Buffer.data(), ByteSize, Buffer.size());
		if(Type == REG_EXPAND_SZ)
		{
			const DWORD Required = ExpandEnvironmentStringsW(Value.c_str(), nullptr, 0);
			if(Required == 0 || Required > 32768)
				return {};
			std::vector<wchar_t> Expanded(Required);
			const DWORD Written = ExpandEnvironmentStringsW(Value.c_str(), Expanded.data(), Required);
			if(Written == 0 || Written > Expanded.size())
				return {};
			Value = QmNeteaseHelper::CopyWideBytes(Expanded.data(), Written * sizeof(wchar_t), Expanded.size());
		}
		while(!Value.empty() && iswspace(Value.front()))
			Value.erase(Value.begin());
		while(!Value.empty() && iswspace(Value.back()))
			Value.pop_back();
		if(Value.size() >= 2 && Value.front() == L'"' && Value.back() == L'"')
			Value = Value.substr(1, Value.size() - 2);
		return Value;
	}

	bool IsCloudMusicDirectory(const std::wstring &Directory)
	{
		if(Directory.empty())
			return false;
		const std::filesystem::path Base(Directory);
		const std::filesystem::path Executable = Base / TARGET_EXE;
		const std::filesystem::path Renderer = Base / L"cloudmusic.dll";
		DWORD BinaryType = 0;
		return GetFileAttributesW(Executable.c_str()) != INVALID_FILE_ATTRIBUTES &&
		       GetFileAttributesW(Renderer.c_str()) != INVALID_FILE_ATTRIBUTES &&
		       GetBinaryTypeW(Executable.c_str(), &BinaryType) && BinaryType == SCS_64BIT_BINARY;
	}

	std::wstring FindInstalledCloudMusicDirectory()
	{
		const HKEY aRoots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
		constexpr REGSAM aViews[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
		for(const HKEY hRoot : aRoots)
		{
			for(const REGSAM View : aViews)
			{
				const std::wstring Executable = QueryRegistryString(hRoot, APP_PATHS_KEY, nullptr, View);
				if(!Executable.empty())
				{
					const std::wstring Directory = std::filesystem::path(Executable).parent_path().wstring();
					if(IsCloudMusicDirectory(Directory))
						return Directory;
				}
				const std::wstring Directory = QueryRegistryString(hRoot, NETEASE_KEY, L"install_dir", View);
				if(IsCloudMusicDirectory(Directory))
					return Directory;
			}
		}
		return {};
	}

	bool RunElevatedBootstrapInstall(const std::wstring &CloudMusicDirectory)
	{
		if(!IsCloudMusicDirectory(CloudMusicDirectory) || CloudMusicDirectory.find(L'"') != std::wstring::npos)
			return false;
		const std::wstring Executable = ModulePath();
		if(Executable.empty())
			return false;
		const std::wstring WorkingDirectory = ModuleDirectory();
		const std::wstring Parameters = L"--install-bootstrap " + QmNeteaseHelper::QuoteDirectoryArgument(CloudMusicDirectory);
		SHELLEXECUTEINFOW Execute{};
		Execute.cbSize = sizeof(Execute);
		Execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		Execute.lpVerb = L"runas";
		Execute.lpFile = Executable.c_str();
		Execute.lpParameters = Parameters.c_str();
		Execute.lpDirectory = WorkingDirectory.c_str();
		Execute.nShow = SW_HIDE;
		if(!ShellExecuteExW(&Execute) || Execute.hProcess == nullptr)
			return false;
		const DWORD WaitResult = WaitForSingleObject(Execute.hProcess, 30000);
		DWORD ExitCode = 1;
		const bool Success = WaitResult == WAIT_OBJECT_0 && GetExitCodeProcess(Execute.hProcess, &ExitCode) && ExitCode == 0;
		CloseHandle(Execute.hProcess);
		return Success;
	}

	bool IsSuccessfulInstallResult(QmNeteaseBootstrap::EInstallResult Result)
	{
		return Result == QmNeteaseBootstrap::EInstallResult::AlreadyInstalled ||
		       Result == QmNeteaseBootstrap::EInstallResult::Installed ||
		       Result == QmNeteaseBootstrap::EInstallResult::Updated;
	}

	int InstallBootstrap(const std::wstring &CloudMusicDirectory)
	{
		if(!IsCloudMusicDirectory(CloudMusicDirectory))
			return 10;
		const std::wstring SourcePath = ModuleDirectory() + L"\\" + QmNeteaseBootstrap::BOOTSTRAP_SOURCE_NAME;
		const QmNeteaseBootstrap::EInstallResult Result = QmNeteaseBootstrap::EnsureInstalled(CloudMusicDirectory, SourcePath);
		return IsSuccessfulInstallResult(Result) ? 0 : 11;
	}

	bool MatchesTargetBuild(const SCandidate &Candidate)
	{
		const std::filesystem::path ExePath(Candidate.m_Path);
		const std::filesystem::path Directory = ExePath.parent_path();
		// 不绑定某一个网易云补丁的 hash。Updater 会替换 exe/CEF，
		// 只要目标是 x64 cloudmusic.exe 且同目录存在 renderer DLL 即允许
		// CDP/兼容注入，具体 target 仍由 Orpheus URL 校验。
		const std::filesystem::path CloudMusicDll = Directory / L"cloudmusic.dll";
		return !ExePath.empty() && GetFileAttributesW(ExePath.c_str()) != INVALID_FILE_ATTRIBUTES &&
		       GetFileAttributesW(CloudMusicDll.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	bool IsAlreadyInjected(DWORD Pid, const std::wstring &DllPath)
	{
		const std::wstring FileName = Lower(std::filesystem::path(DllPath).filename().wstring());
		const CWindowsHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, Pid));
		const HANDLE hSnapshot = Snapshot.get();
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return false;
		MODULEENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		bool Found = false;
		if(Module32FirstW(hSnapshot, &Entry))
		{
			do
			{
				if(Lower(Entry.szModule) == FileName)
				{
					Found = true;
					break;
				}
			} while(Module32NextW(hSnapshot, &Entry));
		}
		return Found;
	}

	class CInjectionOperations : public QmNeteaseHelper::CRemoteInjection::COperations
	{
	public:
		void *OpenProcess(uint32_t Pid) override
		{
			CWindowsHandle Process(::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, Pid));
			return Process && IsTargetArchitecture(Process.get()) ? Process.release() : nullptr;
		}
		void *AllocatePath(void *pProcess, size_t Bytes) override { return VirtualAllocEx(pProcess, nullptr, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
		bool WritePath(void *pProcess, void *pRemote, const wchar_t *pPath, size_t Bytes) override
		{
			SIZE_T Written = 0;
			return WriteProcessMemory(pProcess, pRemote, pPath, Bytes, &Written) && Written == Bytes;
		}
		void *CreateThread(void *pProcess, void *pRemote) override
		{
			const auto pLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
			return pLoadLibrary ? CreateRemoteThread(pProcess, nullptr, 0, pLoadLibrary, pRemote, 0, nullptr) : nullptr;
		}
		QmNeteaseHelper::CRemoteInjection::EWait Wait(void *pThread, uint32_t TimeoutMs) noexcept override
		{
			using EWait = QmNeteaseHelper::CRemoteInjection::EWait;
			const DWORD Result = WaitForSingleObject(pThread, TimeoutMs);
			return Result == WAIT_OBJECT_0 ? EWait::COMPLETE : (Result == WAIT_TIMEOUT ? EWait::PENDING : EWait::ERROR);
		}
		void FreePath(void *pProcess, void *pRemote) noexcept override { VirtualFreeEx(pProcess, pRemote, 0, MEM_RELEASE); }
		void Close(void *pHandle) noexcept override { CloseHandle(pHandle); }
	};

	int Watch(const std::wstring &HookOverride, DWORD ParentPid)
	{
		const std::wstring Dir = ModuleDirectory();
		const std::wstring HookPath = HookOverride.empty() ? Dir + L"\\" + TARGET_DLL : HookOverride;
		const std::wstring BootstrapSourcePath = Dir + L"\\" + QmNeteaseBootstrap::BOOTSTRAP_SOURCE_NAME;
		if(GetFileAttributesW(HookPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			return 3;
		HANDLE hParent = nullptr;
		if(ParentPid != 0)
		{
			hParent = OpenProcess(SYNCHRONIZE, FALSE, ParentPid);
			if(hParent == nullptr)
				return 4;
		}
		const CWindowsHandle Parent(hParent);
		int Result = 0;
		CInjectionOperations InjectionOperations;
		QmNeteaseHelper::CRemoteInjection Injection(InjectionOperations);
		QmNeteaseBridge::CFrontendLyricBridge FrontendBridge;
		DWORD ActiveCloudMusicPid = 0;
		std::wstring BootstrapConflictDirectory;
		std::wstring BootstrapElevationAttemptDirectory;
		uint64_t NextBootstrapCheck = 0;
		for(;;)
		{
			if(hParent != nullptr && WaitForSingleObject(hParent, 0) == WAIT_OBJECT_0)
				break;
			Injection.Poll();
			try
			{
				const std::optional<SCandidate> Candidate = FindMainProcess();
				const uint64_t Now = GetTickCount64();
				if(Now >= NextBootstrapCheck)
				{
					NextBootstrapCheck = Now + 10000;
					const std::wstring InstallDirectory = Candidate.has_value() ? std::filesystem::path(Candidate->m_Path).parent_path().wstring() : FindInstalledCloudMusicDirectory();
					if(!InstallDirectory.empty() && InstallDirectory != BootstrapConflictDirectory)
					{
						QmNeteaseBootstrap::EInstallResult InstallResult = QmNeteaseBootstrap::EnsureInstalled(InstallDirectory, BootstrapSourcePath);
						if(InstallResult == QmNeteaseBootstrap::EInstallResult::AccessDenied && InstallDirectory != BootstrapElevationAttemptDirectory)
						{
							std::wstring AttemptDirectory = InstallDirectory;
							const bool Elevated = RunElevatedBootstrapInstall(InstallDirectory);
							BootstrapElevationAttemptDirectory.swap(AttemptDirectory);
							if(Elevated)
								InstallResult = QmNeteaseBootstrap::EnsureInstalled(InstallDirectory, BootstrapSourcePath);
						}
						if(InstallResult == QmNeteaseBootstrap::EInstallResult::Conflict)
						{
							BootstrapConflictDirectory = InstallDirectory;
							OutputDebugStringW(L"NCM/Bootstrap: BootstrapConflict，拒绝覆盖未知 version.dll\n");
						}
					}
				}
				if(Candidate.has_value())
				{
					if(!IsAlreadyInjected(Candidate->m_Pid, HookPath))
						Injection.Start(Candidate->m_Pid, HookPath);
					if(ActiveCloudMusicPid != Candidate->m_Pid || !FrontendBridge.IsRunning())
					{
						FrontendBridge.Stop();
						if(FrontendBridge.Start(Candidate->m_Pid, Candidate->m_CommandLine))
							ActiveCloudMusicPid = Candidate->m_Pid;
					}
				}
				else if(ActiveCloudMusicPid != 0)
				{
					FrontendBridge.Stop();
					ActiveCloudMusicPid = 0;
				}
			}
			catch(...)
			{
				// 本轮准备失败不消费缺席状态；保留既有bridge，下一轮继续。
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
		FrontendBridge.Stop();
		return Result;
	}
}

static int RunHelper(int argc, wchar_t **argv)
{
	if(argc == 3 && std::wstring(argv[1]) == L"--install-bootstrap")
		return InstallBootstrap(argv[2]);
	if(argc < 2 || std::wstring(argv[1]) != L"--watch")
		return 1;
	HANDLE hMutex = CreateMutexW(nullptr, TRUE, HELPER_MUTEX_NAME);
	const DWORD MutexError = GetLastError();
	const CWindowsHandle Mutex(hMutex);
	if(hMutex == nullptr)
		return 2;
	if(MutexError == ERROR_ALREADY_EXISTS)
	{
		return 0;
	}
	std::wstring HookOverride;
	DWORD ParentPid = 0;
	for(int i = 2; i + 1 < argc; ++i)
	{
		if(std::wstring(argv[i]) == L"--hook")
			HookOverride = argv[++i];
		else if(std::wstring(argv[i]) == L"--parent-pid")
			ParentPid = (DWORD)_wtoi(argv[++i]);
	}
	const int Result = Watch(HookOverride, ParentPid);
	return Result;
}

int wmain(int argc, wchar_t **argv)
{
	try
	{
		return RunHelper(argc, argv);
	}
	catch(...)
	{
		// 原生句柄由各自owner回收，入口准备失败以普通错误码结束。
		return 12;
	}
}
