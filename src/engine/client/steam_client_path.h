#ifndef ENGINE_CLIENT_STEAM_CLIENT_PATH_H
#define ENGINE_CLIENT_STEAM_CLIENT_PATH_H

#include <base/system.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

enum class ESteamClientPlatform
{
	WINDOWS,
	MACOS,
	UNIX,
};

namespace steam_path
{
	using CPath = std::array<char, 1024>;

	inline bool Fits(std::string_view Path, const char *pBuffer, int BufferSize) noexcept
	{
		return pBuffer && BufferSize > 0 && !Path.empty() && Path.size() < static_cast<size_t>(BufferSize) && Path.find('\0') == std::string_view::npos;
	}

	// 完整路径与终止符均可容纳才发布，失败不改已有输出。
	inline bool Copy(std::string_view Path, char *pBuffer, int BufferSize) noexcept
	{
		if(!Fits(Path, pBuffer, BufferSize))
			return false;
		std::memmove(pBuffer, Path.data(), Path.size());
		pBuffer[Path.size()] = '\0';
		return true;
	}

	// 先验证容量，避免对已截断的候选执行存在性查询。
	template<typename TValidate>
	bool Accept(const char *pPath, char *pBuffer, int BufferSize, TValidate &&Validate)
	{
		if(!pPath || !Fits(pPath, pBuffer, BufferSize) || !Validate(pPath))
			return false;
		return Copy(pPath, pBuffer, BufferSize);
	}

	inline std::optional<CPath> Join(std::string_view Base, std::string_view Suffix) noexcept
	{
		CPath Path{};
		if(Base.empty() || Base.size() >= Path.size() || Suffix.size() >= Path.size() - Base.size() || Base.find('\0') != std::string_view::npos || Suffix.find('\0') != std::string_view::npos)
			return std::nullopt;
		std::memcpy(Path.data(), Base.data(), Base.size());
		if(!Suffix.empty())
			std::memcpy(Path.data() + Base.size(), Suffix.data(), Suffix.size());
		return Path;
	}

	// 自动发现保留既有引号/空白与exe判定；完整追加后再检查文件。
	inline std::optional<CPath> WindowsCandidate(const char *pCandidate) noexcept
	{
		if(!pCandidate)
			return std::nullopt;
		std::string_view Candidate(pCandidate);
		while(!Candidate.empty() && (Candidate.front() == '"' || Candidate.front() == ' '))
			Candidate.remove_prefix(1);
		while(!Candidate.empty() && (Candidate.back() == '"' || Candidate.back() == ' '))
			Candidate.remove_suffix(1);
		// 尾部可能有被裁去的引号，后缀比较不能读取view之外的字符。
		const bool ExeSuffix = Candidate.size() >= 4 && str_comp_nocase_num(Candidate.data() + Candidate.size() - 4, ".exe", 4) == 0;
		auto Path = Join(Candidate, ExeSuffix ? std::string_view{} : std::string_view{"\\steam.exe"});
		if(Path)
			std::replace(Path->begin(), Path->end(), '/', '\\');
		return Path;
	}
}

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
