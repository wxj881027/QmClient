#include "steam_probe.h"
#include "steam_client_path.h"

#include <engine/steam.h>

#include <base/fs.h>
#include <base/system.h>
#include <base/windows.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>

namespace
{
	// 校验候选路径（Steam 安装目录或完整 exe 路径）：去掉注册表值可能带的首尾
	// 引号与空白，统一斜杠后拼接/确认 steam.exe 存在，有效时写出完整路径。
	bool SteamAcceptCandidatePath(const char *pCandidate, char *pBuffer, int BufferSize)
	{
		if(pCandidate == nullptr || pCandidate[0] == '\0')
			return false;
		const char *pStart = pCandidate;
		while(*pStart == '"' || *pStart == ' ')
			pStart++;
		const int Length = str_length(pStart);
		int Trimmed = Length;
		while(Trimmed > 0 && (pStart[Trimmed - 1] == '"' || pStart[Trimmed - 1] == ' '))
			Trimmed--;
		char aPath[1024];
		if(Trimmed == 0 || Trimmed >= (int)sizeof(aPath))
			return false;
		str_truncate(aPath, (int)sizeof(aPath), pStart, Trimmed);
		if(str_endswith_nocase(aPath, ".exe") == nullptr)
			str_append(aPath, "\\steam.exe", (int)sizeof(aPath));
		// 注册表 SteamPath 使用正斜杠，统一替换后再验证。
		for(char *pChar = aPath; *pChar != '\0'; pChar++)
		{
			if(*pChar == '/')
				*pChar = '\\';
		}
		// UNC 路径与断连的网络映射盘上的存在性探测会走 SMB I/O，断连时
		// GetFileAttributesW 可能阻塞数秒以上；Steam 不支持网络安装，此类
		// 候选直接拒绝，避免启动期一次性扫描被网络超时拖慢。
		if(aPath[0] == '\\' && aPath[1] == '\\')
			return false;
		if(aPath[1] == ':' && aPath[2] == '\\')
		{
			// GetDriveTypeW 只查盘符映射，不触网络，断连映射盘也立即返回。
			const wchar_t aRoot[4] = {(wchar_t)aPath[0], L':', L'\\', L'\0'};
			if(GetDriveTypeW(aRoot) == DRIVE_REMOTE)
				return false;
		}
		if(!fs_is_file(aPath))
			return false;
		str_copy(pBuffer, aPath, BufferSize);
		return true;
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
		const std::optional<std::string> Utf8 = windows_wide_to_utf8(aValue);
		if(!Utf8.has_value() || Utf8->empty() || (int)Utf8->size() >= BufferSize)
			return false;
		str_copy(pBuffer, Utf8->c_str(), BufferSize);
		return true;
	}

	// Steam 正在运行时直接取其映像路径，覆盖自定义目录安装。
	bool SteamPathFromRunningProcess(char *pBuffer, int BufferSize)
	{
		HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(Snapshot == INVALID_HANDLE_VALUE)
			return false;
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
				wchar_t aImage[1024] = {};
				DWORD ImageLength = sizeof(aImage) / sizeof(aImage[0]);
				if(QueryFullProcessImageNameW(Process, 0, aImage, &ImageLength))
				{
					const std::optional<std::string> Utf8 = windows_wide_to_utf8(aImage);
					Found = Utf8.has_value() && SteamAcceptCandidatePath(Utf8->c_str(), pBuffer, BufferSize);
				}
				CloseHandle(Process);
			} while(!Found && Process32NextW(Snapshot, &Entry));
		}
		CloseHandle(Snapshot);
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

	// PATH 搜索兜底，覆盖把 Steam 目录加入 PATH 的自定义安装。
	bool SteamPathFromSearchPath(char *pBuffer, int BufferSize)
	{
		wchar_t aResult[1024] = {};
		wchar_t *pFilePart = nullptr;
		const DWORD Length = SearchPathW(nullptr, L"steam.exe", nullptr, sizeof(aResult) / sizeof(aResult[0]), aResult, &pFilePart);
		if(Length == 0 || Length >= sizeof(aResult) / sizeof(aResult[0]))
			return false;
		const std::optional<std::string> Utf8 = windows_wide_to_utf8(aResult);
		return Utf8.has_value() && SteamAcceptCandidatePath(Utf8->c_str(), pBuffer, BufferSize);
	}
} // namespace

bool SteamProbeFindClientWindows(char *pBuffer, int BufferSize, ESteamClientSource *pSource)
{
	if(pSource != nullptr)
		*pSource = ESteamClientSource::NOT_FOUND;
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
		{SteamPathFromSearchPath, ESteamClientSource::ENVIRONMENT_PATH},
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
