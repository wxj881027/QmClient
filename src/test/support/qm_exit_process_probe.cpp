#include "qm_exit_process_probe.h"

#include <base/system.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

#if defined(CONF_FAMILY_WINDOWS)
namespace
{
	using FCreateProcess = decltype(&CreateProcessW);
	FCreateProcess gs_pCreateProcess = nullptr;

	BOOL WINAPI CreateProcessGate(LPCWSTR pApplication, LPWSTR pCommand, LPSECURITY_ATTRIBUTES pProcessAttributes, LPSECURITY_ATTRIBUTES pThreadAttributes, BOOL InheritHandles, DWORD Flags, LPVOID pEnvironment, LPCWSTR pDirectory, LPSTARTUPINFOW pStartup, LPPROCESS_INFORMATION pProcess)
	{
		const BOOL Result = gs_pCreateProcess(pApplication, pCommand, pProcessAttributes, pThreadAttributes, InheritHandles, Flags, pEnvironment, pDirectory, pStartup, pProcess);
		if(Result)
		{
			// 停在真实系统调用已返回、生产启动函数尚未继续的边界。
			std::printf("owned-server-pid=%lu\n", pProcess->dwProcessId);
			std::fflush(stdout);
			std::getchar();
		}
		return Result;
	}

	bool InstallCreationGate()
	{
		// 仅测试 runner 的导入表：替换外部 OS 边界，仍调用真实生产启动函数和系统 API。
		auto *pBase = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
		auto *pDos = reinterpret_cast<IMAGE_DOS_HEADER *>(pBase);
		auto *pNt = reinterpret_cast<IMAGE_NT_HEADERS *>(pBase + pDos->e_lfanew);
		const DWORD Imports = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
		if(Imports == 0)
			return false;
		auto *pImport = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(pBase + Imports);
		for(; pImport->Name != 0; ++pImport)
		{
			if(pImport->OriginalFirstThunk == 0)
				continue;
			auto *pName = reinterpret_cast<IMAGE_THUNK_DATA *>(pBase + pImport->OriginalFirstThunk);
			auto *pAddress = reinterpret_cast<IMAGE_THUNK_DATA *>(pBase + pImport->FirstThunk);
			for(; pName->u1.AddressOfData != 0; ++pName, ++pAddress)
			{
				if(IMAGE_SNAP_BY_ORDINAL(pName->u1.Ordinal))
					continue;
				auto *pFunction = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(pBase + pName->u1.AddressOfData);
				if(std::strcmp(reinterpret_cast<const char *>(pFunction->Name), "CreateProcessW") != 0)
					continue;
				DWORD OldProtection;
				if(!VirtualProtect(&pAddress->u1.Function, sizeof(pAddress->u1.Function), PAGE_READWRITE, &OldProtection))
					return false;
				gs_pCreateProcess = reinterpret_cast<FCreateProcess>(pAddress->u1.Function);
				pAddress->u1.Function = reinterpret_cast<ULONG_PTR>(&CreateProcessGate);
				DWORD Ignored;
				return VirtualProtect(&pAddress->u1.Function, sizeof(pAddress->u1.Function), OldProtection, &Ignored) != FALSE;
			}
		}
		return false;
	}
}
#endif

std::optional<int> QmExitProcessProbe(int argc, const char **ppArgv)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(argc < 3 || str_comp(ppArgv[1], "--qm-test-owned-server") != 0)
		return std::nullopt;
	if(argc >= 5 && str_comp(ppArgv[4], "creation-gate") == 0 && !InstallCreationGate())
		return 4;
	const char *apArguments[] = {"sv_register 0", argc >= 4 ? ppArgv[3] : "sv_port 0", "sv_bindaddr 127.0.0.1", "sv_map ctf1", "logfile qm-owned-server.log"};
	PROCESS Process = shell_execute_owned(ppArgv[2], apArguments, std::size(apArguments));
	if(Process == INVALID_PROCESS)
		return 2;
	std::printf("owned-server-pid=%lu\n", GetProcessId(Process));
	std::fflush(stdout);
	// 测试驱动控制退出方式：普通关闭、跳过析构、或外部终止父进程。
	const int Command = std::getchar();
	if(Command == 'x')
		std::_Exit(0);
	return kill_process(Process) ? 0 : 3;
#else
	(void)argc;
	(void)ppArgv;
	return std::nullopt;
#endif
}
