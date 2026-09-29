#include "qm_soda_watchdog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace QmSodaWatchdog
{
	namespace
	{
		std::wstring Lower(std::wstring Value)
		{
			std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t C) { return (wchar_t)towlower(C); });
			return Value;
		}

		struct SCandidate
		{
			DWORD m_Pid = 0;
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
				Parameters.m_CommandLine.m_Buffer == nullptr || Parameters.m_CommandLine.m_Length == 0)
				return {};
			const size_t CharCount = Parameters.m_CommandLine.m_Length / sizeof(wchar_t);
			std::wstring Result(CharCount, L'\0');
			if(!ReadProcessMemory(hProcess, Parameters.m_CommandLine.m_Buffer, Result.data(), Parameters.m_CommandLine.m_Length, &Read) ||
				Read != Parameters.m_CommandLine.m_Length)
				return {};
			return Result;
		}

		bool IsChildProcessCommandLine(const std::wstring &CommandLine)
		{
			const std::wstring LowerCommandLine = Lower(CommandLine);
			return LowerCommandLine.find(L"--type=") != std::wstring::npos || LowerCommandLine.find(L"--type ") != std::wstring::npos;
		}

		std::vector<SCandidate> FindCandidates()
		{
			std::vector<SCandidate> Result;
			HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if(hSnapshot == INVALID_HANDLE_VALUE)
				return Result;
			PROCESSENTRY32W Entry{};
			Entry.dwSize = sizeof(Entry);
			// 进程名比较必须两侧都小写;TARGET_EXE 是原样大写。
			const std::wstring LowerTarget = Lower(TARGET_EXE);
			if(Process32FirstW(hSnapshot, &Entry))
			{
				do
				{
					if(Lower(Entry.szExeFile) != LowerTarget)
						continue;
					SCandidate Candidate;
					Candidate.m_Pid = Entry.th32ProcessID;
					// 目标进程可能以更高权限运行(如管理员),OpenProcess 会失败。
					// 此时命令行留空,由 FindMainPid 的 Fallback 兜底返回该 PID。
					HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, Entry.th32ProcessID);
					if(hProcess != nullptr)
					{
						Candidate.m_CommandLine = QueryCommandLine(hProcess);
						CloseHandle(hProcess);
					}
					Result.push_back(std::move(Candidate));
				} while(Process32NextW(hSnapshot, &Entry));
			}
			CloseHandle(hSnapshot);
			return Result;
		}
	}

	uint32_t FindMainPid()
	{
		const std::vector<SCandidate> Candidates = FindCandidates();
		// 主进程(browser)命令行没有 --type=;renderer/gpu/utility 子进程都带。
		// QueryCommandLine 可能因权限失败返回空串:空命令行不能证明是主进程。
		// 兜底策略:先选有 node-debug-handler 命名映射的候选(只有主进程创建它),
		// 其次选第一个非子进程命令行,最后落到第一个候选。
		uint32_t FallbackPid = 0;
		uint32_t HandlerPid = 0;
		for(const SCandidate &Candidate : Candidates)
		{
			if(Candidate.m_CommandLine.empty())
			{
				if(FallbackPid == 0)
					FallbackPid = Candidate.m_Pid;
				continue;
			}
			if(!IsChildProcessCommandLine(Candidate.m_CommandLine))
				return Candidate.m_Pid;
		}
		if(HandlerPid != 0)
			return HandlerPid;
		// 命令行全空(目标以更高权限运行)时,用命名映射识别主进程。
		if(FallbackPid != 0)
		{
			const std::wstring MappingName = L"node-debug-handler-" + std::to_wstring(FallbackPid);
			HANDLE hMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName.c_str());
			if(hMapping != nullptr)
			{
				CloseHandle(hMapping);
				return FallbackPid;
			}
			// 第一个候选没有映射时,检查其余候选。
			for(const SCandidate &Candidate : Candidates)
			{
				const std::wstring Name = L"node-debug-handler-" + std::to_wstring(Candidate.m_Pid);
				hMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, Name.c_str());
				if(hMapping != nullptr)
				{
					CloseHandle(hMapping);
					return Candidate.m_Pid;
				}
			}
		}
		return FallbackPid;
	}

	bool IsInspectorUp()
	{
		// 不真正请求 HTTP:检查 9229 是否有 loopback 监听者即可。
		DWORD BufferSize = 0;
		const DWORD ProbeResult = GetExtendedTcpTable(nullptr, &BufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
		if(ProbeResult != ERROR_INSUFFICIENT_BUFFER || BufferSize < sizeof(MIB_TCPTABLE_OWNER_PID))
			return false;
		std::vector<uint8_t> Buffer(BufferSize);
		auto *pTable = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(Buffer.data());
		if(GetExtendedTcpTable(pTable, &BufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
			return false;
		const DWORD LoopbackAddress = htonl(INADDR_LOOPBACK);
		for(DWORD Index = 0; Index < pTable->dwNumEntries; ++Index)
		{
			const MIB_TCPROW_OWNER_PID &Row = pTable->table[Index];
			if(Row.dwState == MIB_TCP_STATE_LISTEN && Row.dwLocalAddr == LoopbackAddress && ntohs((u_short)Row.dwLocalPort) == INSPECTOR_PORT)
				return true;
		}
		return false;
	}

	bool EnsureInspector(uint32_t Pid)
	{
		if(Pid == 0)
			return false;
		if(IsInspectorUp())
			return true;
		// 命名映射 "node-debug-handler-<pid>" 由 Node/Electron 主进程启动时创建。
		const std::wstring MappingName = L"node-debug-handler-" + std::to_wstring(Pid);
		HANDLE hMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName.c_str());
		if(hMapping == nullptr)
			return false;
		void *pView = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, sizeof(void *));
		if(pView == nullptr)
		{
			CloseHandle(hMapping);
			return false;
		}
		uintptr_t HandlerAddress = 0;
		std::memcpy(&HandlerAddress, pView, sizeof(HandlerAddress));
		UnmapViewOfFile(pView);
		CloseHandle(hMapping);
		if(HandlerAddress == 0)
			return false;

		HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, Pid);
		if(hProcess == nullptr)
			return false;
		HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(HandlerAddress), nullptr, 0, nullptr);
		CloseHandle(hProcess);
		if(hThread == nullptr)
			return false;
		CloseHandle(hThread);
		return true;
	}
} // namespace QmSodaWatchdog
