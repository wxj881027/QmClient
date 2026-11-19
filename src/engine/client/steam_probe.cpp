#include "steam_probe.h"

#include "steam_client_path.h"

#include <base/fs.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/steam.h>

#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>

namespace
{
	// 校验候选路径（Steam 安装目录或完整 exe 路径）：去掉注册表值可能带的首尾
	// 引号与空白，统一斜杠后拼接/确认 steam.exe 存在，有效时写出完整路径。
	bool SteamAcceptCandidatePath(const char *pCandidate, char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile = fs_is_file)
	{
		const auto Path = steam_path::WindowsCandidate(pCandidate);
		if(!Path)
			return false;
		return steam_path::Accept(Path->data(), pBuffer, BufferSize, [&IsFile](const char *pPath) {
			// UNC 路径与断连的网络映射盘上的存在性探测会走 SMB I/O，断连时
			// GetFileAttributesW 可能阻塞数秒以上；Steam 不支持网络安装，此类
			// 候选直接拒绝，避免启动期一次性扫描被网络超时拖慢。
			if(pPath[0] == '\\' && pPath[1] == '\\')
				return false;
			if(pPath[1] == ':' && pPath[2] == '\\')
			{
				// 先查询盘符类型，远程或不可用的盘符跳过文件探测。
				const wchar_t aRoot[4] = {(wchar_t)pPath[0], L':', L'\\', L'\0'};
				const UINT DriveType = GetDriveTypeW(aRoot);
				if(DriveType == DRIVE_REMOTE || DriveType == DRIVE_NO_ROOT_DIR || DriveType == DRIVE_UNKNOWN)
					return false;
			}
			return IsFile(pPath) != 0;
		});
	}

	// 读取 REG_SZ 注册表值并转为 UTF-8；先按 64 位视图打开，失败再按当前视图兜底，
	// pValue 为 nullptr 时读默认值。
	bool SteamRegQueryString(HKEY Root, const wchar_t *pSubKey, const wchar_t *pValue, char *pBuffer, int BufferSize)
	{
		HKEY Key;
		LONG Result = RegOpenKeyExW(Root, pSubKey, 0, KEY_READ | KEY_WOW64_64KEY, &Key);
		if(Result != ERROR_SUCCESS)
			Result = RegOpenKeyExW(Root, pSubKey, 0, KEY_READ, &Key);
		if(Result != ERROR_SUCCESS)
			return false;
		wchar_t aValue[1024] = {};
		DWORD Type = 0;
		DWORD Size = sizeof(aValue);
		Result = RegQueryValueExW(Key, pValue, nullptr, &Type, reinterpret_cast<BYTE *>(aValue), &Size);
		RegCloseKey(Key);
		if(Result != ERROR_SUCCESS || (Type != REG_SZ && Type != REG_EXPAND_SZ))
			return false;
		const std::optional<std::string> Utf8 = SteamProbeDecodeRegistryString(aValue, std::size(aValue), Size);
		return Utf8.has_value() && steam_path::Copy(*Utf8, pBuffer, BufferSize);
	}

	// Steam 正在运行时直接取其映像路径，覆盖自定义目录安装。
	bool SteamPathFromRunningProcess(char *pBuffer, int BufferSize)
	{
		if(!pBuffer || BufferSize <= 0)
			return false;
		HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(Snapshot == INVALID_HANDLE_VALUE)
			return false;
		std::unique_ptr<void, decltype(&CloseHandle)> SnapshotOwner(Snapshot, &CloseHandle);
		bool Found = false;
		PROCESSENTRY32W Entry;
		Entry.dwSize = sizeof(Entry);
		if(Process32FirstW(Snapshot, &Entry))
		{
			do
			{
				if(lstrcmpiW(Entry.szExeFile, L"steam.exe") != 0)
					continue;
				HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
				if(Process == nullptr)
					continue;
				std::unique_ptr<void, decltype(&CloseHandle)> ProcessOwner(Process, &CloseHandle);
				wchar_t aImage[1024] = {};
				DWORD ImageLength = sizeof(aImage) / sizeof(aImage[0]);
				if(QueryFullProcessImageNameW(Process, 0, aImage, &ImageLength))
				{
					const std::optional<std::string> Utf8 = windows_wide_to_utf8(aImage);
					Found = Utf8.has_value() && SteamAcceptCandidatePath(Utf8->c_str(), pBuffer, BufferSize);
				}
			} while(!Found && Process32NextW(Snapshot, &Entry));
		}
		return Found;
	}

	// 常见默认安装目录，兜底覆盖注册表被清理的极端情况。
	bool SteamPathFromCommonDirectories(char *pBuffer, int BufferSize)
	{
		const wchar_t *apEnvNames[] = {L"ProgramFiles(x86)", L"ProgramFiles"};
		for(const wchar_t *pEnvName : apEnvNames)
		{
			wchar_t aDirectory[512] = {};
			const DWORD Length = GetEnvironmentVariableW(pEnvName, aDirectory, sizeof(aDirectory) / sizeof(aDirectory[0]));
			if(Length == 0 || Length >= sizeof(aDirectory) / sizeof(aDirectory[0]))
				continue;
			const std::wstring WidePath = std::wstring(aDirectory) + L"\\Steam\\steam.exe";
			const std::optional<std::string> Utf8 = windows_wide_to_utf8(WidePath.c_str());
			if(Utf8.has_value() && SteamAcceptCandidatePath(Utf8->c_str(), pBuffer, BufferSize))
				return true;
		}
		const char *apFixedPaths[] = {
			"C:\\Program Files (x86)\\Steam\\steam.exe",
			"C:\\Program Files\\Steam\\steam.exe",
		};
		for(const char *pFixedPath : apFixedPaths)
		{
			if(SteamAcceptCandidatePath(pFixedPath, pBuffer, BufferSize))
				return true;
		}
		return false;
	}

	bool SteamTryPathDirectory(std::wstring_view Directory, char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile)
	{
		const size_t First = Directory.find_first_not_of(L" \t");
		if(First == std::wstring_view::npos)
			return false;
		std::wstring Candidate(Directory.substr(First, Directory.find_last_not_of(L" \t") - First + 1));
		const bool DriveRelativeRoot = Candidate.size() == 2 && Candidate[1] == L':';
		if(!DriveRelativeRoot && Candidate.back() != L'\\' && Candidate.back() != L'/')
			Candidate += L'\\';
		Candidate += L"steam.exe";

		// 相对目录先展开，供文件探测前的 UNC 和盘符检查使用。
		wchar_t aResult[1024] = {};
		const DWORD Length = GetFullPathNameW(Candidate.c_str(), std::size(aResult), aResult, nullptr);
		if(Length == 0 || Length >= std::size(aResult))
			return false;
		const std::optional<std::string> Utf8 = windows_wide_to_utf8(aResult);
		return Utf8.has_value() && SteamAcceptCandidatePath(Utf8->c_str(), pBuffer, BufferSize, IsFile);
	}
} // namespace

namespace steam_probe
{
	bool FindClientInPath(std::wstring_view Path, char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile)
	{
		std::wstring Directory;
		bool Quoted = false;
		for(wchar_t Character : Path)
		{
			if(Character == L'"')
				Quoted = !Quoted;
			else if(Character == L';' && !Quoted)
			{
				if(SteamTryPathDirectory(Directory, pBuffer, BufferSize, IsFile))
					return true;
				Directory.clear();
			}
			else
				Directory += Character;
		}
		return !Quoted && SteamTryPathDirectory(Directory, pBuffer, BufferSize, IsFile);
	}
} // namespace steam_probe

namespace
{
	bool SteamPathFromEnvironment(char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile)
	{
		const DWORD Size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
		if(Size == 0)
			return false;
		std::wstring Path(Size, L'\0');
		const DWORD Length = GetEnvironmentVariableW(L"PATH", Path.data(), Size);
		if(Length == 0 || Length >= Size)
			return false;
		Path.resize(Length);
		return steam_probe::FindClientInPath(Path, pBuffer, BufferSize, IsFile);
	}

	bool SteamPathFromLegacySearchPath(char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile)
	{
		// 旧系统没有目录列表接口，使用 Win32 目录查询和系统的安全搜索设置。
		const auto Probe = [&](std::wstring_view Directory) {
			return SteamTryPathDirectory(Directory, pBuffer, BufferSize, IsFile);
		};
		wchar_t aDirectory[1024];
		const DWORD ImageLength = GetModuleFileNameW(nullptr, aDirectory, std::size(aDirectory));
		if(ImageLength != 0 && ImageLength < std::size(aDirectory))
		{
			const std::wstring_view Image(aDirectory, ImageLength);
			const size_t Separator = Image.find_last_of(L"\\/");
			if(Separator != std::wstring_view::npos && Probe(Image.substr(0, Separator + 1)))
				return true;
		}

		DWORD SearchMode = 0;
		DWORD Size = sizeof(SearchMode);
		const LONG Result = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
			L"SafeProcessSearchMode", RRF_RT_REG_DWORD, nullptr, &SearchMode, &Size);
		const bool SafeSearch = Result == ERROR_SUCCESS && SearchMode == 1;
		if(!SafeSearch && Probe(L"."))
			return true;

		const UINT SystemLength = GetSystemDirectoryW(aDirectory, std::size(aDirectory));
		if(SystemLength != 0 && SystemLength < std::size(aDirectory) && Probe(std::wstring_view(aDirectory, SystemLength)))
			return true;
		const UINT WindowsLength = GetWindowsDirectoryW(aDirectory, std::size(aDirectory));
		if(WindowsLength != 0 && WindowsLength < std::size(aDirectory))
		{
			const std::wstring_view WindowsDirectory(aDirectory, WindowsLength);
			if(Probe(std::wstring(WindowsDirectory) + L"\\System") || Probe(WindowsDirectory))
				return true;
		}
		return (SafeSearch && Probe(L".")) || SteamPathFromEnvironment(pBuffer, BufferSize, IsFile);
	}
} // namespace

namespace steam_probe
{
	bool FindClientInDefaultPath(char *pBuffer, int BufferSize, const std::function<int(const char *)> &IsFile)
	{
		// 只生成 SearchPathW 使用的目录序列，保留当前进程的搜索模式。
		// 文件探测前先过滤网络候选，避免 SearchPathW 直接访问共享目录。
		using TGetSearchPath = LONG(WINAPI *)(wchar_t **);
		using TReleaseSearchPath = void(WINAPI *)(wchar_t *);
		if(const HMODULE Module = GetModuleHandleW(L"ntdll.dll"))
		{
			const auto pGetSearchPath = reinterpret_cast<TGetSearchPath>(GetProcAddress(Module, "RtlGetSearchPath"));
			const auto pReleaseSearchPath = reinterpret_cast<TReleaseSearchPath>(GetProcAddress(Module, "RtlReleasePath"));
			wchar_t *pPath = nullptr;
			if(pGetSearchPath && pReleaseSearchPath && pGetSearchPath(&pPath) >= 0 && pPath)
			{
				const std::unique_ptr<wchar_t, TReleaseSearchPath> Path(pPath, pReleaseSearchPath);
				return FindClientInPath(Path.get(), pBuffer, BufferSize, IsFile);
			}
		}
		return SteamPathFromLegacySearchPath(pBuffer, BufferSize, IsFile);
	}
} // namespace steam_probe

bool SteamProbeFindClientWindows(char *pBuffer, int BufferSize, ESteamClientSource *pSource)
{
	if(pSource != nullptr)
		*pSource = ESteamClientSource::NOT_FOUND;
	if(!pBuffer || BufferSize <= 0)
		return false;
	// 注册表：Steam 安装器写入的标准位置。用户级记录是自定义目录安装（含免
	// 管理员安装）唯一可靠记录，优先于机器级视图与 App Paths。
	const struct SSteamRegistryProbe
	{
		HKEY Root;
		const wchar_t *pSubKey;
		const wchar_t *pValue;
	} apRegistryProbes[] = {
		{HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe"},
		{HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"},
		{HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"},
		{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"},
		{HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\steam.exe", nullptr},
		{HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\steam.exe", nullptr},
	};
	for(const SSteamRegistryProbe &Probe : apRegistryProbes)
	{
		char aValue[1024];
		if(SteamRegQueryString(Probe.Root, Probe.pSubKey, Probe.pValue, aValue, (int)sizeof(aValue)) &&
			SteamAcceptCandidatePath(aValue, pBuffer, BufferSize))
		{
			if(pSource != nullptr)
				*pSource = ESteamClientSource::REGISTRY;
			return true;
		}
	}
	const struct
	{
		bool (*m_pProbe)(char *, int);
		ESteamClientSource m_Source;
	} aFallbacks[] = {
		{SteamPathFromRunningProcess, ESteamClientSource::RUNNING_PROCESS},
		{SteamPathFromCommonDirectories, ESteamClientSource::COMMON_DIRECTORY},
		{[](char *pOutput, int OutputSize) { return steam_probe::FindClientInDefaultPath(pOutput, OutputSize, fs_is_file); }, ESteamClientSource::ENVIRONMENT_PATH},
	};
	for(const auto &Fallback : aFallbacks)
	{
		if(Fallback.m_pProbe(pBuffer, BufferSize))
		{
			if(pSource != nullptr)
				*pSource = Fallback.m_Source;
			return true;
		}
	}
	return false;
}

bool SteamProbeValidateManualPath(const char *pCandidate, char *pBuffer, int BufferSize)
{
	const std::string Path = SteamManualPathCandidate(pCandidate, ESteamClientPlatform::WINDOWS);
	return !Path.empty() && SteamAcceptCandidatePath(Path.c_str(), pBuffer, BufferSize);
}

bool SteamProbePathFromRunningProcess(char *pBuffer, int BufferSize)
{
	return SteamPathFromRunningProcess(pBuffer, BufferSize);
}
#else
// 非 Windows 平台此编译单元为空，占位避免空翻译单元。
typedef int QmSteamProbeUnusedTranslationUnit;
#endif
