#if !defined(_WIN32)
#error QmClient-Updater is Windows-only
#endif

#include <engine/shared/qm_update.h>

#include <windows.h>

#include <commctrl.h>
#include <qm/update/updater_arguments.h>
#include <shellapi.h>
#include <winver.h>

#include <atomic>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
	constexpr wchar_t WINDOW_CLASS[] = L"QmClientUpdateWindow";
	constexpr wchar_t WINDOW_TITLE[] = L"QmClient 更新";

	HWND g_hStatus = nullptr;
	HWND g_hProgress = nullptr;
	bool g_AllowClose = false;

	void SetStatus(const wchar_t *pText, bool Indeterminate)
	{
		if(g_hStatus)
			SetWindowTextW(g_hStatus, pText);
		if(g_hProgress)
		{
			LONG_PTR Style = GetWindowLongPtrW(g_hProgress, GWL_STYLE);
			if(Indeterminate)
				Style |= PBS_MARQUEE;
			else
				Style &= ~PBS_MARQUEE;
			SetWindowLongPtrW(g_hProgress, GWL_STYLE, Style);
			SendMessageW(g_hProgress, PBM_SETMARQUEE, Indeterminate ? TRUE : FALSE, 35);
			if(!Indeterminate)
				SendMessageW(g_hProgress, PBM_SETPOS, 100, 0);
		}
	}

	void PumpMessages()
	{
		MSG Message;
		while(PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if(Message.message == WM_QUIT)
				return;
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	bool WaitForParent(DWORD ParentPid)
	{
		if(ParentPid == 0)
			return true;
		HANDLE Parent = OpenProcess(SYNCHRONIZE, FALSE, ParentPid);
		if(!Parent)
			return GetLastError() == ERROR_INVALID_PARAMETER;
		while(true)
		{
			PumpMessages();
			const DWORD Result = WaitForSingleObject(Parent, 50);
			if(Result == WAIT_OBJECT_0)
				break;
			if(Result == WAIT_FAILED)
			{
				CloseHandle(Parent);
				return false;
			}
		}
		CloseHandle(Parent);
		return true;
	}

	std::wstring CurrentExecutablePath()
	{
		wchar_t aPath[MAX_PATH * 4];
		const DWORD Length = GetModuleFileNameW(nullptr, aPath, static_cast<DWORD>(std::size(aPath)));
		return Length > 0 && Length < std::size(aPath) ? std::wstring(aPath, Length) : std::wstring();
	}

	bool ParseArguments(QmUpdate::SArguments &Arguments)
	{
		int Argc = 0;
		wchar_t **ppArgv = CommandLineToArgvW(GetCommandLineW(), &Argc);
		if(!ppArgv)
			return false;
		std::vector<std::wstring> ArgumentsList(ppArgv, ppArgv + Argc);
		LocalFree(ppArgv);
		return QmUpdate::ParseArguments(ArgumentsList, Arguments);
	}

	std::wstring QuoteArgument(const std::wstring &Value)
	{
		std::wstring Result = L"\"";
		unsigned Backslashes = 0;
		for(const wchar_t Character : Value)
		{
			if(Character == L'\\')
			{
				++Backslashes;
				continue;
			}
			if(Character == L'\"')
				Result.append(Backslashes * 2 + 1, L'\\');
			else
				Result.append(Backslashes, L'\\');
			Backslashes = 0;
			Result.push_back(Character);
		}
		Result.append(Backslashes * 2, L'\\');
		Result.push_back(L'\"');
		return Result;
	}

	bool RelaunchElevated(const QmUpdate::SArguments &Arguments)
	{
		const std::wstring Executable = CurrentExecutablePath();
		if(Executable.empty())
			return false;
		std::wstring Parameters = L"--parent-pid 0";
		const auto AppendPath = [&](const wchar_t *pName, const std::wstring &Value) {
			Parameters.push_back(L' ');
			Parameters += pName;
			Parameters.push_back(L' ');
			Parameters += QuoteArgument(Value);
		};
		AppendPath(L"--package", Arguments.m_Package);
		AppendPath(L"--package-signature", Arguments.m_PackageSignature);
		AppendPath(L"--manifest", Arguments.m_Manifest);
		AppendPath(L"--manifest-signature", Arguments.m_ManifestSignature);
		AppendPath(L"--install", Arguments.m_Install);
		Parameters += L" --qm-elevated";
		SHELLEXECUTEINFOW Info{};
		Info.cbSize = sizeof(Info);
		Info.fMask = SEE_MASK_NOCLOSEPROCESS;
		Info.lpVerb = L"runas";
		Info.lpFile = Executable.c_str();
		Info.lpParameters = Parameters.c_str();
		Info.nShow = SW_SHOWNORMAL;
		if(!ShellExecuteExW(&Info))
			return false;
		if(Info.hProcess)
			CloseHandle(Info.hProcess);
		return true;
	}

	LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
	{
		if(Message == WM_CLOSE)
		{
			if(!g_AllowClose)
				return 0;
			DestroyWindow(hWnd);
			return 0;
		}
		if(Message == WM_DESTROY)
		{
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hWnd, Message, wParam, lParam);
	}

	HWND CreateStatusWindow(HINSTANCE Instance)
	{
		WNDCLASSW Class{};
		Class.hInstance = Instance;
		Class.lpfnWndProc = WindowProc;
		Class.lpszClassName = WINDOW_CLASS;
		Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		if(!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return nullptr;
		HWND Window = CreateWindowExW(WS_EX_DLGMODALFRAME, WINDOW_CLASS, WINDOW_TITLE, WS_CAPTION | WS_SYSMENU,
			CW_USEDEFAULT, CW_USEDEFAULT, 460, 150, nullptr, nullptr, Instance, nullptr);
		if(!Window)
			return nullptr;
		g_hStatus = CreateWindowW(L"STATIC", L"正在等待客户端退出…", WS_CHILD | WS_VISIBLE,
			20, 20, 410, 24, Window, nullptr, Instance, nullptr);
		g_hProgress = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
			20, 60, 410, 22, Window, nullptr, Instance, nullptr);
		SendMessageW(g_hProgress, PBM_SETMARQUEE, TRUE, 35);
		ShowWindow(Window, SW_SHOWNORMAL);
		UpdateWindow(Window);
		return Window;
	}

	bool ToUtf8(const std::wstring &Value, std::string &Result)
	{
		const int Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if(Length <= 0)
			return false;
		Result.resize(static_cast<size_t>(Length));
		if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Value.c_str(), -1, Result.data(), Length, nullptr, nullptr) <= 0)
			return false;
		Result.resize(static_cast<size_t>(Length - 1));
		return true;
	}

	bool ReadInstalledVersion(const std::wstring &InstallPath, std::string &Version)
	{
		const std::wstring ClientPath = std::filesystem::path(InstallPath) / L"DDNet.exe";
		DWORD Handle = 0;
		const DWORD InfoSize = GetFileVersionInfoSizeW(ClientPath.c_str(), &Handle);
		if(InfoSize == 0)
			return false;
		std::vector<unsigned char> vInfo(InfoSize);
		if(!GetFileVersionInfoW(ClientPath.c_str(), 0, InfoSize, vInfo.data()))
			return false;
		struct SLanguageAndCodePage
		{
			WORD m_Language;
			WORD m_CodePage;
		};
		SLanguageAndCodePage *pTranslation = nullptr;
		UINT TranslationSize = 0;
		if(!VerQueryValueW(vInfo.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&pTranslation), &TranslationSize) ||
			TranslationSize < sizeof(*pTranslation))
			return false;
		wchar_t aQuery[64];
		if(swprintf_s(aQuery, L"\\StringFileInfo\\%04x%04x\\ProductVersion", pTranslation->m_Language, pTranslation->m_CodePage) < 0)
			return false;
		wchar_t *pValue = nullptr;
		UINT ValueSize = 0;
		if(!VerQueryValueW(vInfo.data(), aQuery, reinterpret_cast<void **>(&pValue), &ValueSize) || pValue == nullptr || ValueSize <= 1)
			return false;
		return ToUtf8(std::wstring(pValue, ValueSize - 1), Version);
	}

	void CleanupPath(const std::wstring &Path, bool DelayIfLocked)
	{
		if(Path.empty())
			return;
		if(DeleteFileW(Path.c_str()) == 0 && DelayIfLocked)
			MoveFileExW(Path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
	}

	void CleanupTemporaryFiles(const QmUpdate::SArguments &Arguments)
	{
		CleanupPath(Arguments.m_Package, false);
		CleanupPath(Arguments.m_PackageSignature, false);
		CleanupPath(Arguments.m_Manifest, false);
		CleanupPath(Arguments.m_ManifestSignature, false);
		CleanupPath(CurrentExecutablePath(), true);
	}
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int)
{
	INITCOMMONCONTROLSEX CommonControls{sizeof(CommonControls), ICC_PROGRESS_CLASS};
	InitCommonControlsEx(&CommonControls);
	QmUpdate::SArguments Arguments;
	const std::wstring ExecutablePath = CurrentExecutablePath();
	if(!ParseArguments(Arguments) || ExecutablePath.empty() || !QmUpdate::ValidateSessionPaths(Arguments, ExecutablePath))
	{
		MessageBoxW(nullptr, L"更新参数无效。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		return 2;
	}
	HWND Window = CreateStatusWindow(Instance);
	if(!Window)
	{
		CleanupTemporaryFiles(Arguments);
		return 3;
	}
	if(!WaitForParent(Arguments.m_ParentPid))
	{
		SetStatus(L"无法等待客户端退出。", false);
		MessageBoxW(Window, L"无法确认客户端已退出，更新已取消。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		CleanupTemporaryFiles(Arguments);
		g_AllowClose = true;
		DestroyWindow(Window);
		return 1;
	}
	SetStatus(L"正在验证并安装更新…", true);
	char aError[1024] = "";
	std::string Package;
	std::string PackageSignature;
	std::string Manifest;
	std::string ManifestSignature;
	std::string Install;
	std::string CurrentVersion;
	if(!ToUtf8(Arguments.m_Package, Package) || !ToUtf8(Arguments.m_PackageSignature, PackageSignature) ||
		!ToUtf8(Arguments.m_Manifest, Manifest) || !ToUtf8(Arguments.m_ManifestSignature, ManifestSignature) ||
		!ToUtf8(Arguments.m_Install, Install))
	{
		SetStatus(L"更新路径无效。", false);
		MessageBoxW(Window, L"更新路径不是有效的 UTF-8。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		CleanupTemporaryFiles(Arguments);
		g_AllowClose = true;
		DestroyWindow(Window);
		return 1;
	}
	if(!ReadInstalledVersion(Arguments.m_Install, CurrentVersion))
	{
		SetStatus(L"无法验证已安装的客户端。", false);
		MessageBoxW(Window, L"安装目录中没有可验证的 QmClient 客户端，更新已取消。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		CleanupTemporaryFiles(Arguments);
		g_AllowClose = true;
		DestroyWindow(Window);
		return 1;
	}
	std::atomic<bool> Finished = false;
	bool Success = false;
	std::thread Worker([&] {
		Success = qm_update_apply(Package.c_str(), PackageSignature.c_str(), Manifest.c_str(), ManifestSignature.c_str(), Install.c_str(), CurrentVersion.c_str(), aError, sizeof(aError));
		Finished.store(true, std::memory_order_release);
	});
	while(!Finished.load(std::memory_order_acquire))
	{
		PumpMessages();
		Sleep(20);
	}
	Worker.join();
	if(!Success && !Arguments.m_Elevated && QmUpdate::IsPermissionError(aError) && RelaunchElevated(Arguments))
	{
		DestroyWindow(Window);
		return 0;
	}
	if(Success)
	{
		SetStatus(L"更新完成，客户端即将退出。", false);
		Sleep(450);
	}
	else
	{
		SetStatus(L"更新失败。", false);
		MessageBoxA(Window, aError[0] ? aError : "Update failed", "QmClient update", MB_OK | MB_ICONERROR);
	}
	CleanupTemporaryFiles(Arguments);
	g_AllowClose = true;
	DestroyWindow(Window);
	return Success ? 0 : 1;
}
