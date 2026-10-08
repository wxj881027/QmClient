#ifndef ENGINE_CLIENT_STEAM_CLIENT_PATH_H
#define ENGINE_CLIENT_STEAM_CLIENT_PATH_H

#include <base/system.h>

#include <algorithm>
#include <string>

enum class ESteamClientPlatform
{
	WINDOWS,
	MACOS,
	UNIX,
};

// 手动路径只接受客户端文件本身，不把参数或相对路径交给进程启动器。
inline std::string SteamManualPathCandidate(const char *pInput, ESteamClientPlatform Platform)
{
	std::string Path = pInput != nullptr ? pInput : "";
	const size_t Start = Path.find_first_not_of(" \t");
	if(Start == std::string::npos)
		return {};
	Path = Path.substr(Start, Path.find_last_not_of(" \t") - Start + 1);
	if(Path.size() >= 2 && Path.front() == '"' && Path.back() == '"')
		Path = Path.substr(1, Path.size() - 2);
	if(Path.empty() || Path.size() >= 1024 || Path.find_first_of("\"\r\n") != std::string::npos)
		return {};
	if(Platform == ESteamClientPlatform::WINDOWS)
	{
		std::replace(Path.begin(), Path.end(), '/', '\\');
		const bool DriveLetter = (Path[0] >= 'A' && Path[0] <= 'Z') || (Path[0] >= 'a' && Path[0] <= 'z');
		if(Path.size() < 3 || !DriveLetter || Path[1] != ':' || Path[2] != '\\')
			return {};
		const size_t Separator = Path.find_last_of('\\');
		if(str_comp_nocase(Path.c_str() + Separator + 1, "steam.exe") != 0)
			return {};
	}
	else
	{
		if(Path[0] != '/')
			return {};
		const std::string Filename = Path.substr(Path.find_last_of('/') + 1);
		if(Platform == ESteamClientPlatform::MACOS ? Filename != "Steam.app" : Filename != "steam" && Filename != "steam.sh")
			return {};
	}
	return Path;
}

#endif
