#ifndef ENGINE_CLIENT_QM_SYSTEM_FONTS_H
#define ENGINE_CLIENT_QM_SYSTEM_FONTS_H

#include <base/system.h>
#include <base/windows.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

inline bool QmIsFontFile(const char *pPath)
{
	return str_endswith_nocase(pPath, ".ttf") || str_endswith_nocase(pPath, ".otf") || str_endswith_nocase(pPath, ".ttc") || str_endswith_nocase(pPath, ".otc");
}

// 安装记录可存文件名或完整路径；不把未知相对路径误当成客户端资源。
inline std::string QmResolveSystemFontPath(std::string Path, const std::string &Directory)
{
	std::replace(Path.begin(), Path.end(), '\\', '/');
	if(Path.empty() || !QmIsFontFile(Path.c_str()))
		return {};
	if(Path.find_first_of("/:") == std::string::npos)
	{
		if(Directory.empty())
			return {};
		Path = Directory + "/" + Path;
		std::replace(Path.begin(), Path.end(), '\\', '/');
	}
	if(Path.size() < 3 || !((Path[0] >= 'A' && Path[0] <= 'Z') || (Path[0] >= 'a' && Path[0] <= 'z')) || Path[1] != ':' || Path[2] != '/')
		return {};
	return Path;
}

// 注册表提供实际文件位置，兼容仅当前用户安装和安装在其他目录的字体。
inline std::vector<std::string> QmSystemFontPaths(const std::function<bool()> &Cancelled = {})
{
	std::vector<std::string> vPaths;
	if(Cancelled && Cancelled())
		return vPaths;
#if defined(CONF_FAMILY_WINDOWS)
	wchar_t aWindows[32768] = {};
	const UINT WindowsLength = GetWindowsDirectoryW(aWindows, std::size(aWindows));
	if(WindowsLength == 0 || WindowsLength >= std::size(aWindows))
		return vPaths;
	for(HKEY Root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
	{
		HKEY Key;
		if(RegOpenKeyExW(Root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ | KEY_WOW64_64KEY, &Key) != ERROR_SUCCESS)
			continue;
		const std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)> KeyOwner(Key, &RegCloseKey);
		for(DWORD Index = 0;; ++Index)
		{
			if(Cancelled && Cancelled())
				return {};
			wchar_t aName[16384] = {}, aValue[32768] = {};
			DWORD NameLength = std::size(aName), Size = sizeof(aValue) - sizeof(wchar_t), Type = 0;
			const LONG Result = RegEnumValueW(Key, Index, aName, &NameLength, nullptr, &Type, reinterpret_cast<BYTE *>(aValue), &Size);
			if(Result == ERROR_NO_MORE_ITEMS)
				break;
			if(Result != ERROR_SUCCESS || (Type != REG_SZ && Type != REG_EXPAND_SZ) || Size % sizeof(wchar_t) != 0)
				continue;
			std::wstring Path = aValue;
			if(Type == REG_EXPAND_SZ)
			{
				wchar_t aExpanded[32768];
				const DWORD Length = ExpandEnvironmentStringsW(aValue, aExpanded, std::size(aExpanded));
				if(Length == 0 || Length > std::size(aExpanded))
					continue;
				Path = aExpanded;
			}
			if(Path.empty())
				continue;
			std::wstring Directory = std::wstring(aWindows) + L"\\Fonts";
			if(Path.find_first_of(L"\\/:") == std::wstring::npos)
			{
				if(Root == HKEY_CURRENT_USER)
				{
					wchar_t aLocal[32768];
					const DWORD Length = GetEnvironmentVariableW(L"LOCALAPPDATA", aLocal, std::size(aLocal));
					if(Length == 0 || Length >= std::size(aLocal))
						continue;
					Directory = std::wstring(aLocal) + L"\\Microsoft\\Windows\\Fonts";
				}
			}
			const auto Utf8 = windows_wide_to_utf8(Path.c_str());
			const auto Utf8Directory = windows_wide_to_utf8(Directory.c_str());
			if(!Utf8 || !Utf8Directory)
				continue;
			const auto Resolved = QmResolveSystemFontPath(*Utf8, *Utf8Directory);
			if(Resolved.empty())
				continue;
			// 网络字体不进入启动期同步读取，避免断连网络阻塞客户端。
			const wchar_t aRoot[] = {static_cast<wchar_t>(Resolved[0]), L':', L'\\', L'\0'};
			if(GetDriveTypeW(aRoot) == DRIVE_REMOTE)
				continue;
			vPaths.push_back(Resolved);
		}
	}
	std::sort(vPaths.begin(), vPaths.end(), [](const auto &Left, const auto &Right) { return str_comp_nocase(Left.c_str(), Right.c_str()) < 0; });
	vPaths.erase(std::unique(vPaths.begin(), vPaths.end(), [](const auto &Left, const auto &Right) { return str_comp_nocase(Left.c_str(), Right.c_str()) == 0; }), vPaths.end());
#endif
	return vPaths;
}

#endif
