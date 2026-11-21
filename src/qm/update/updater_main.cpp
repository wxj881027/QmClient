#if !defined(_WIN32)
#error QmClient-Updater is Windows-only
#endif

#include <engine/shared/qm_update.h>

#include <windows.h>

#include <commctrl.h>
#include <qm/update/updater_arguments.h>
#include <qm/update/updater_session.h>
#include <shellapi.h>
#include <winver.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
	constexpr wchar_t WINDOW_CLASS[] = L"QmClientUpdateWindow";
	constexpr wchar_t WINDOW_TITLE[] = L"QmClient 更新";
	constexpr ULONGLONG PARENT_EXIT_TIMEOUT_MS = 60'000;

	using THandle = std::unique_ptr<void, decltype(&CloseHandle)>;

	HWND g_hStatus = nullptr;
	HWND g_hProgress = nullptr;
	bool g_WaitingForParent = false;
	bool g_ParentWaitCancelled = false;

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

	bool PumpMessages()
	{
		MSG Message;
		while(PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			if(Message.message == WM_QUIT)
				return false;
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
		return true;
	}

	enum class EParentWaitResult
	{
		EXITED,
		CANCELLED,
		TIMED_OUT,
		FAILED,
	};

	EParentWaitResult WaitForParent(DWORD ParentPid)
	{
		if(ParentPid == 0)
			return EParentWaitResult::EXITED;
		const THandle Parent(OpenProcess(SYNCHRONIZE, FALSE, ParentPid), CloseHandle);
		if(!Parent)
			return GetLastError() == ERROR_INVALID_PARAMETER ? EParentWaitResult::EXITED : EParentWaitResult::FAILED;
		const ULONGLONG Start = GetTickCount64();
		while(true)
		{
			if(!PumpMessages() || g_ParentWaitCancelled)
				return EParentWaitResult::CANCELLED;
			const DWORD Result = WaitForSingleObject(Parent.get(), 50);
			if(Result == WAIT_OBJECT_0)
				return EParentWaitResult::EXITED;
			if(Result != WAIT_TIMEOUT)
				return EParentWaitResult::FAILED;
			if(GetTickCount64() - Start >= PARENT_EXIT_TIMEOUT_MS)
				return EParentWaitResult::TIMED_OUT;
		}
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
		const std::unique_ptr<wchar_t *, decltype(&LocalFree)> pArgv(CommandLineToArgvW(GetCommandLineW(), &Argc), LocalFree);
		if(!pArgv)
			return false;
		const std::vector<std::wstring> ArgumentsList(pArgv.get(), pArgv.get() + Argc);
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

	bool RelaunchElevated(const QmUpdate::SArguments &Arguments, const std::wstring &ExecutablePath)
	{
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
		if(Arguments.m_Setup)
			Parameters += L" --setup";
		Parameters += L" --qm-elevated";
		SHELLEXECUTEINFOW Info{};
		Info.cbSize = sizeof(Info);
		Info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		Info.lpVerb = L"runas";
		Info.lpFile = ExecutablePath.c_str();
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
			if(g_WaitingForParent)
				g_ParentWaitCancelled = true;
			return 0;
		}
		if(Message == WM_DESTROY)
		{
			g_hStatus = nullptr;
			g_hProgress = nullptr;
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

	void ShowUpdateError(HWND Window, const char *pError, const wchar_t *pFallback)
	{
		wchar_t aMessage[1024];
		const bool Converted = *pError && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pError, -1, aMessage, static_cast<int>(std::size(aMessage))) > 0;
		MessageBoxW(Window, Converted ? aMessage : pFallback, WINDOW_TITLE, MB_OK | MB_ICONERROR);
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

	class CTemporaryFiles
	{
		const QmUpdate::SArguments &m_Arguments;
		const std::wstring &m_ExecutablePath;
		bool m_OwnsFiles = true;

	public:
		CTemporaryFiles(const QmUpdate::SArguments &Arguments, const std::wstring &ExecutablePath) :
			m_Arguments(Arguments),
			m_ExecutablePath(ExecutablePath) {}
		CTemporaryFiles(const CTemporaryFiles &) = delete;
		CTemporaryFiles &operator=(const CTemporaryFiles &) = delete;
		~CTemporaryFiles() { Cleanup(); }

		void Release() noexcept { m_OwnsFiles = false; }

		void Cleanup() noexcept
		{
			if(!m_OwnsFiles)
				return;
			Release();
			try
			{
				QmUpdate::CleanupDownloadedFiles(m_Arguments);
			}
			catch(const std::exception &)
			{
				// 路径构造失败时仍可用已有宽字符串回收会话附件。
				for(const auto *pPath : {&m_Arguments.m_Package, &m_Arguments.m_PackageSignature, &m_Arguments.m_Manifest, &m_Arguments.m_ManifestSignature})
					DeleteFileW(pPath->c_str());
			}
			if(!DeleteFileW(m_ExecutablePath.c_str()))
			{
				const DWORD Error = GetLastError();
				if(Error != ERROR_FILE_NOT_FOUND && Error != ERROR_PATH_NOT_FOUND)
					MoveFileExW(m_ExecutablePath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
			}
		}
	};

	THandle OpenSetupPackage(const std::wstring &Path, char *pError, size_t ErrorSize)
	{
		// 复验和运行 Setup 期间，拒绝对同一个安装包文件的写入和删除。
		const HANDLE File = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if(File != INVALID_HANDLE_VALUE)
			return THandle(File, CloseHandle);
		const DWORD Error = GetLastError();
		const char *pPermissionMarker = Error == ERROR_ACCESS_DENIED ? QmUpdate::PERMISSION_DENIED_MARKER : "";
		std::snprintf(pError, ErrorSize, "%s%sFailed to open Setup for verification (Windows error %lu)", pPermissionMarker, *pPermissionMarker ? " " : "", Error);
		return THandle(nullptr, CloseHandle);
	}

	QmUpdate::ESetupResult RunSetup(const QmUpdate::SArguments &Arguments, char *pError, size_t ErrorSize)
	{
		const std::wstring Parameters = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /NORESTARTAPPLICATIONS " + QuoteArgument(L"/DIR=" + Arguments.m_Install);
		SHELLEXECUTEINFOW Info{};
		Info.cbSize = sizeof(Info);
		Info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		Info.lpFile = Arguments.m_Package.c_str();
		Info.lpParameters = Parameters.c_str();
		Info.lpDirectory = Arguments.m_Install.c_str();
		Info.nShow = SW_SHOWNORMAL;
		if(!ShellExecuteExW(&Info))
		{
			std::snprintf(pError, ErrorSize, "Failed to start Setup (Windows error %lu)", GetLastError());
			return QmUpdate::ESetupResult::FAILED;
		}
		const THandle Process(Info.hProcess, CloseHandle);
		if(!Process)
		{
			std::snprintf(pError, ErrorSize, "Setup launch returned no process handle. Update files were retained.");
			return QmUpdate::ESetupResult::STILL_RUNNING;
		}
		while(true)
		{
			PumpMessages();
			const DWORD WaitResult = WaitForSingleObject(Process.get(), 50);
			if(WaitResult == WAIT_OBJECT_0)
				break;
			if(WaitResult == WAIT_FAILED)
			{
				std::snprintf(pError, ErrorSize, "Failed to wait for Setup (Windows error %lu). Update files were retained.", GetLastError());
				return QmUpdate::ESetupResult::STILL_RUNNING;
			}
		}
		DWORD ExitCode = 0;
		if(!GetExitCodeProcess(Process.get(), &ExitCode))
		{
			std::snprintf(pError, ErrorSize, "Failed to read Setup exit code (Windows error %lu)", GetLastError());
			return QmUpdate::ESetupResult::FAILED;
		}
		if(ExitCode != 0)
		{
			std::snprintf(pError, ErrorSize, "Setup failed or was cancelled (exit code %lu)", ExitCode);
			return QmUpdate::ESetupResult::FAILED;
		}
		return QmUpdate::ESetupResult::SUCCEEDED;
	}
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int)
try
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
	CTemporaryFiles TemporaryFiles(Arguments, ExecutablePath);
	g_WaitingForParent = Arguments.m_ParentPid != 0;
	const std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> WindowOwner(CreateStatusWindow(Instance), DestroyWindow);
	const HWND Window = WindowOwner.get();
	if(!Window)
		return 3;
	const EParentWaitResult ParentResult = WaitForParent(Arguments.m_ParentPid);
	g_WaitingForParent = false;
	if(ParentResult != EParentWaitResult::EXITED)
	{
		TemporaryFiles.Cleanup();
		if(ParentResult != EParentWaitResult::CANCELLED)
		{
			SetStatus(L"无法等待客户端退出。", false);
			const wchar_t *pMessage = ParentResult == EParentWaitResult::TIMED_OUT ?
							  L"等待客户端退出超时，更新已取消。" :
							  L"无法确认客户端已退出，更新已取消。";
			MessageBoxW(Window, pMessage, WINDOW_TITLE, MB_OK | MB_ICONERROR);
		}
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
		return 1;
	}
	QmUpdate::CInstallLock InstallLock(Arguments.m_Install);
	using ELockResult = QmUpdate::CInstallLock::EResult;
	if(InstallLock.Result() != ELockResult::ACQUIRED)
	{
		SetStatus(L"暂时无法开始更新。", false);
		MessageBoxW(Window, InstallLock.Result() == ELockResult::BUSY ? L"已有更新任务正在处理此安装目录。" : L"无法锁定安装目录，更新已取消。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		return 1;
	}
	if(!ReadInstalledVersion(Arguments.m_Install, CurrentVersion))
	{
		SetStatus(L"无法验证已安装的客户端。", false);
		MessageBoxW(Window, L"安装目录中没有可验证的 QmClient 客户端，更新已取消。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
		TemporaryFiles.Cleanup();
		return 1;
	}
	THandle SetupPackage = Arguments.m_Setup ? OpenSetupPackage(Arguments.m_Package, aError, sizeof(aError)) : THandle(nullptr, CloseHandle);
	bool Success = false;
	if(!Arguments.m_Setup || SetupPackage)
	{
		std::atomic<bool> Finished = false;
		std::thread Worker([&] {
			if(Arguments.m_Setup)
				Success = qm_update_verify_setup_files(Package.c_str(), PackageSignature.c_str(), Manifest.c_str(), ManifestSignature.c_str(), CurrentVersion.c_str(), aError, sizeof(aError));
			else
				Success = qm_update_apply(Package.c_str(), PackageSignature.c_str(), Manifest.c_str(), ManifestSignature.c_str(), Install.c_str(), CurrentVersion.c_str(), aError, sizeof(aError));
			Finished.store(true, std::memory_order_release);
		});
		while(!Finished.load(std::memory_order_acquire))
		{
			PumpMessages();
			Sleep(20);
		}
		Worker.join();
	}
	if(!Success && !Arguments.m_Elevated && QmUpdate::IsPermissionError(aError))
	{
		// 提权后的助手会重新获取锁并读取磁盘上的版本。
		SetupPackage.reset();
		InstallLock.Release();
		if(RelaunchElevated(Arguments, ExecutablePath))
		{
			TemporaryFiles.Release();
			return 0;
		}
	}
	if(Arguments.m_Setup)
	{
		const auto Result = QmUpdate::RunSetupSession(
			[&] { return Success; },
			[&] { return RunSetup(Arguments, aError, sizeof(aError)); },
			[&] {
				SetupPackage.reset();
				TemporaryFiles.Cleanup();
			});
		if(Result == QmUpdate::ESetupResult::STILL_RUNNING)
			TemporaryFiles.Release();
		if(Result != QmUpdate::ESetupResult::SUCCEEDED)
			ShowUpdateError(Window, aError, L"安装程序未能完成更新。");
		return Result == QmUpdate::ESetupResult::SUCCEEDED ? 0 : 1;
	}
	if(Success)
	{
		SetStatus(L"更新完成，客户端即将退出。", false);
		Sleep(450);
	}
	else
	{
		SetStatus(L"更新失败。", false);
		ShowUpdateError(Window, aError, L"更新失败。");
	}
	TemporaryFiles.Cleanup();
	return Success ? 0 : 1;
}
catch(const std::exception &)
{
	MessageBoxW(nullptr, L"无法准备更新。请重新启动客户端后重试。", WINDOW_TITLE, MB_OK | MB_ICONERROR);
	return 1;
}
