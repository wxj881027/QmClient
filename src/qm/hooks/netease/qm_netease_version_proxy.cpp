// 网易云启动期 version.dll 代理。该 DLL 只负责转发系统 VERSION API，
// 并在进程加载期完成最小 Bootstrap。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "qm_netease_bootstrap.h"

#include <windows.h>

#include <winver.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace
{
	HMODULE g_hSystemVersion = nullptr;
	INIT_ONCE g_InitOnce = INIT_ONCE_STATIC_INIT;

	// version.dll 在 cloudmusic.dll 的依赖解析阶段加载。此时各模块的 IAT
	// 已经完成绑定，但 CEF 尚未初始化；改写 IAT 可以让 CEF 读取到真正的
	// 启动参数。这里不改 kernel32 的代码，只改网易云自身模块的导入槽。
	using TGetCommandLineW = LPWSTR(WINAPI *)();
	using TGetCommandLineA = LPSTR(WINAPI *)();
	TGetCommandLineW g_pOriginalGetCommandLineW = nullptr;
	TGetCommandLineA g_pOriginalGetCommandLineA = nullptr;
	LPWSTR g_pPatchedCommandLineW = nullptr;
	LPSTR g_pPatchedCommandLineA = nullptr;
	LONG g_CommandLineHookState = 0;

	LPWSTR WINAPI HookGetCommandLineW()
	{
		if(g_pPatchedCommandLineW != nullptr)
			return g_pPatchedCommandLineW;
		return g_pOriginalGetCommandLineW != nullptr ? g_pOriginalGetCommandLineW() : nullptr;
	}

	LPSTR WINAPI HookGetCommandLineA()
	{
		if(g_pPatchedCommandLineA != nullptr)
			return g_pPatchedCommandLineA;
		return g_pOriginalGetCommandLineA != nullptr ? g_pOriginalGetCommandLineA() : nullptr;
	}

	bool IsReadableImage(HMODULE hModule, const IMAGE_NT_HEADERS **ppNtHeaders)
	{
		if(hModule == nullptr || ppNtHeaders == nullptr)
			return false;
		const auto *pBase = reinterpret_cast<const BYTE *>(hModule);
		const auto *pDos = reinterpret_cast<const IMAGE_DOS_HEADER *>(pBase);
		if(pDos->e_magic != IMAGE_DOS_SIGNATURE || pDos->e_lfanew <= 0 || pDos->e_lfanew > 0x10000000)
			return false;
		const auto *pNt = reinterpret_cast<const IMAGE_NT_HEADERS *>(pBase + pDos->e_lfanew);
		if(pNt->Signature != IMAGE_NT_SIGNATURE)
			return false;
		*ppNtHeaders = pNt;
		return true;
	}

	void PatchImportSlot(void **pSlot, void *pReplacement, void **ppOriginal)
	{
		if(pSlot == nullptr || pReplacement == nullptr || ppOriginal == nullptr)
			return;
		if(*pSlot == pReplacement)
			return;
		if(*ppOriginal == nullptr)
			*ppOriginal = *pSlot;
		DWORD OldProtection = 0;
		if(!VirtualProtect(pSlot, sizeof(void *), PAGE_READWRITE, &OldProtection))
			return;
		*pSlot = pReplacement;
		DWORD Ignored = 0;
		VirtualProtect(pSlot, sizeof(void *), OldProtection, &Ignored);
	}

	void PatchModuleImports(HMODULE hModule)
	{
		const IMAGE_NT_HEADERS *pNtHeaders = nullptr;
		if(!IsReadableImage(hModule, &pNtHeaders))
			return;
		const IMAGE_DATA_DIRECTORY &ImportDirectory = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if(ImportDirectory.VirtualAddress == 0 || ImportDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
			return;
		const auto *pBase = reinterpret_cast<const BYTE *>(hModule);
		const auto *pImports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(pBase + ImportDirectory.VirtualAddress);
		for(const IMAGE_IMPORT_DESCRIPTOR *pImport = pImports; pImport->Name != 0; ++pImport)
		{
			// 绑定导入没有原始名称表，无法安全判断函数名；当前网易云
			// 二进制使用标准 INT，因此遇到这种模块直接跳过。
			if(pImport->OriginalFirstThunk == 0 || pImport->FirstThunk == 0)
				continue;
			const auto *pNames = reinterpret_cast<const IMAGE_THUNK_DATA *>(pBase + pImport->OriginalFirstThunk);
			auto *pSlots = reinterpret_cast<IMAGE_THUNK_DATA *>(const_cast<BYTE *>(pBase) + pImport->FirstThunk);
			for(size_t Index = 0; pNames[Index].u1.AddressOfData != 0; ++Index)
			{
				if(IMAGE_SNAP_BY_ORDINAL(pNames[Index].u1.Ordinal))
					continue;
				const auto *pImportName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(pBase + pNames[Index].u1.AddressOfData);
				if(std::strcmp(reinterpret_cast<const char *>(pImportName->Name), "GetCommandLineW") == 0)
				{
					PatchImportSlot(reinterpret_cast<void **>(&pSlots[Index].u1.Function), reinterpret_cast<void *>(&HookGetCommandLineW), reinterpret_cast<void **>(&g_pOriginalGetCommandLineW));
				}
				else if(std::strcmp(reinterpret_cast<const char *>(pImportName->Name), "GetCommandLineA") == 0)
				{
					PatchImportSlot(reinterpret_cast<void **>(&pSlots[Index].u1.Function), reinterpret_cast<void *>(&HookGetCommandLineA), reinterpret_cast<void **>(&g_pOriginalGetCommandLineA));
				}
			}
		}
	}

	bool PreparePatchedCommandLine()
	{
		if(g_pPatchedCommandLineW != nullptr)
			return true;
		const auto pOriginalW = reinterpret_cast<TGetCommandLineW>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCommandLineW"));
		const auto pOriginalA = reinterpret_cast<TGetCommandLineA>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCommandLineA"));
		if(pOriginalW == nullptr || pOriginalA == nullptr)
			return false;
		g_pOriginalGetCommandLineW = pOriginalW;
		g_pOriginalGetCommandLineA = pOriginalA;

		wchar_t aExecutable[MAX_PATH] = {};
		const DWORD ExecutableLength = GetModuleFileNameW(nullptr, aExecutable, (DWORD)std::size(aExecutable));
		const wchar_t *pCurrent = pOriginalW();
		if(ExecutableLength == 0 || ExecutableLength >= std::size(aExecutable) || pCurrent == nullptr)
			return false;
		std::wstring Patched;
		const uint16_t Port = QmNeteaseBootstrap::EarlyLoopbackPort(GetCurrentProcessId());
		if(!QmNeteaseBootstrap::BuildLoopbackDebuggingCommandLine(std::wstring_view(aExecutable, ExecutableLength), pCurrent, Port, &Patched))
			return false;
		if(Patched.empty())
			return false;
		const size_t WideBytes = (Patched.size() + 1) * sizeof(wchar_t);
		if(WideBytes > std::numeric_limits<SIZE_T>::max())
			return false;
		g_pPatchedCommandLineW = static_cast<LPWSTR>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WideBytes));
		if(g_pPatchedCommandLineW == nullptr)
			return false;
		std::memcpy(g_pPatchedCommandLineW, Patched.c_str(), WideBytes);

		const int AnsiBytes = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Patched.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if(AnsiBytes <= 0)
			return false;
		g_pPatchedCommandLineA = static_cast<LPSTR>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)AnsiBytes));
		if(g_pPatchedCommandLineA == nullptr || WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Patched.c_str(), -1, g_pPatchedCommandLineA, AnsiBytes, nullptr, nullptr) <= 0)
			return false;
		return true;
	}

	void InstallCommandLineHooks()
	{
		if(InterlockedCompareExchange(&g_CommandLineHookState, 1, 0) != 0)
			return;
		if(!PreparePatchedCommandLine())
		{
			InterlockedExchange(&g_CommandLineHookState, 2);
			return;
		}
		// 主程序和 CEF 相关模块都可能直接通过各自的 IAT 读取命令行。
		PatchModuleImports(GetModuleHandleW(nullptr));
		PatchModuleImports(GetModuleHandleW(L"cloudmusic.dll"));
		PatchModuleImports(GetModuleHandleW(L"libcef.dll"));
		PatchModuleImports(GetModuleHandleW(L"chrome_elf.dll"));
		InterlockedExchange(&g_CommandLineHookState, 2);
	}

	HMODULE LoadSystemVersion()
	{
		const std::wstring Path = QmNeteaseBootstrap::SystemVersionDllPath();
		if(Path.empty())
			return nullptr;
		return LoadLibraryExW(Path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	}

	BOOL CALLBACK InitSystemVersion(PINIT_ONCE, PVOID, PVOID *)
	{
		g_hSystemVersion = LoadSystemVersion();
		return TRUE;
	}

	bool EnsureSystemVersion()
	{
		// 如果某个模块在 DLL_PROCESS_ATTACH 之后才进入 loader list，
		// 在首次 VERSION API 调用时再补一次 IAT 拦截。
		InstallCommandLineHooks();
		InitOnceExecuteOnce(&g_InitOnce, InitSystemVersion, nullptr, nullptr);
		return g_hSystemVersion != nullptr;
	}

	template<typename T>
	T Resolve(const char *pName)
	{
		if(!EnsureSystemVersion() || pName == nullptr)
			return nullptr;
		return reinterpret_cast<T>(GetProcAddress(g_hSystemVersion, pName));
	}
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR FileName, LPDWORD Handle)
{
	using T = DWORD(WINAPI *)(LPCSTR, LPDWORD);
	const T Function = Resolve<T>("GetFileVersionInfoSizeA");
	return Function != nullptr ? Function(FileName, Handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR FileName, LPDWORD Handle)
{
	using T = DWORD(WINAPI *)(LPCWSTR, LPDWORD);
	const T Function = Resolve<T>("GetFileVersionInfoSizeW");
	return Function != nullptr ? Function(FileName, Handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD Flags, LPCSTR FileName, LPDWORD Handle)
{
	using T = DWORD(WINAPI *)(DWORD, LPCSTR, LPDWORD);
	const T Function = Resolve<T>("GetFileVersionInfoSizeExA");
	return Function != nullptr ? Function(Flags, FileName, Handle) : 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD Flags, LPCWSTR FileName, LPDWORD Handle)
{
	using T = DWORD(WINAPI *)(DWORD, LPCWSTR, LPDWORD);
	const T Function = Resolve<T>("GetFileVersionInfoSizeExW");
	return Function != nullptr ? Function(Flags, FileName, Handle) : 0;
}

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR FileName, DWORD Handle, DWORD Length, LPVOID Data)
{
	using T = BOOL(WINAPI *)(LPCSTR, DWORD, DWORD, LPVOID);
	const T Function = Resolve<T>("GetFileVersionInfoA");
	return Function != nullptr ? Function(FileName, Handle, Length, Data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR FileName, DWORD Handle, DWORD Length, LPVOID Data)
{
	using T = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
	const T Function = Resolve<T>("GetFileVersionInfoW");
	return Function != nullptr ? Function(FileName, Handle, Length, Data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD Flags, LPCSTR FileName, DWORD Handle, DWORD Length, LPVOID Data)
{
	using T = BOOL(WINAPI *)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
	const T Function = Resolve<T>("GetFileVersionInfoExA");
	return Function != nullptr ? Function(Flags, FileName, Handle, Length, Data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD Flags, LPCWSTR FileName, DWORD Handle, DWORD Length, LPVOID Data)
{
	using T = BOOL(WINAPI *)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
	const T Function = Resolve<T>("GetFileVersionInfoExW");
	return Function != nullptr ? Function(Flags, FileName, Handle, Length, Data) : FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle(HANDLE File, DWORD Length, LPVOID Data)
{
	using T = BOOL(WINAPI *)(HANDLE, DWORD, LPVOID);
	const T Function = Resolve<T>("GetFileVersionInfoByHandle");
	return Function != nullptr ? Function(File, Length, Data) : FALSE;
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD Language, LPSTR Buffer, DWORD Size)
{
	using T = DWORD(WINAPI *)(DWORD, LPSTR, DWORD);
	const T Function = Resolve<T>("VerLanguageNameA");
	return Function != nullptr ? Function(Language, Buffer, Size) : 0;
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD Language, LPWSTR Buffer, DWORD Size)
{
	using T = DWORD(WINAPI *)(DWORD, LPWSTR, DWORD);
	const T Function = Resolve<T>("VerLanguageNameW");
	return Function != nullptr ? Function(Language, Buffer, Size) : 0;
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID Block, LPCSTR SubBlock, LPVOID *Buffer, PUINT Length)
{
	using T = BOOL(WINAPI *)(LPCVOID, LPCSTR, LPVOID *, PUINT);
	const T Function = Resolve<T>("VerQueryValueA");
	return Function != nullptr ? Function(Block, SubBlock, Buffer, Length) : FALSE;
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID Block, LPCWSTR SubBlock, LPVOID *Buffer, PUINT Length)
{
	using T = BOOL(WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
	const T Function = Resolve<T>("VerQueryValueW");
	return Function != nullptr ? Function(Block, SubBlock, Buffer, Length) : FALSE;
}

extern "C" DWORD WINAPI VerFindFileA(DWORD Flags, LPCSTR FileName, LPCSTR WinDir, LPCSTR AppDir, LPSTR CurDir, PUINT CurDirLen, LPSTR DestDir, PUINT DestDirLen)
{
	using T = DWORD(WINAPI *)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
	const T Function = Resolve<T>("VerFindFileA");
	return Function != nullptr ? Function(Flags, FileName, WinDir, AppDir, CurDir, CurDirLen, DestDir, DestDirLen) : 0;
}

extern "C" DWORD WINAPI VerFindFileW(DWORD Flags, LPCWSTR FileName, LPCWSTR WinDir, LPCWSTR AppDir, LPWSTR CurDir, PUINT CurDirLen, LPWSTR DestDir, PUINT DestDirLen)
{
	using T = DWORD(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
	const T Function = Resolve<T>("VerFindFileW");
	return Function != nullptr ? Function(Flags, FileName, WinDir, AppDir, CurDir, CurDirLen, DestDir, DestDirLen) : 0;
}

extern "C" DWORD WINAPI VerInstallFileA(DWORD Flags, LPCSTR SrcFileName, LPCSTR DestFileName, LPCSTR WinDir, LPCSTR WinAppDir, LPCSTR CurDir, LPSTR TmpFile, PUINT TmpFileLen)
{
	using T = DWORD(WINAPI *)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
	const T Function = Resolve<T>("VerInstallFileA");
	return Function != nullptr ? Function(Flags, SrcFileName, DestFileName, WinDir, WinAppDir, CurDir, TmpFile, TmpFileLen) : 0;
}

extern "C" DWORD WINAPI VerInstallFileW(DWORD Flags, LPCWSTR SrcFileName, LPCWSTR DestFileName, LPCWSTR WinDir, LPCWSTR WinAppDir, LPCWSTR CurDir, LPWSTR TmpFile, PUINT TmpFileLen)
{
	using T = DWORD(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
	const T Function = Resolve<T>("VerInstallFileW");
	return Function != nullptr ? Function(Flags, SrcFileName, DestFileName, WinDir, WinAppDir, CurDir, TmpFile, TmpFileLen) : 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD Reason, LPVOID)
{
	if(Reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		InstallCommandLineHooks();
		// 保留 PEB 镜像仅供诊断/Helper 发现端口；真正供 CEF 使用的是上面的
		// GetCommandLineW/A IAT 返回值。
		QmNeteaseBootstrap::PatchCurrentProcessCommandLineEarly(QmNeteaseBootstrap::EarlyLoopbackPort(GetCurrentProcessId()));
	}
	return TRUE;
}
