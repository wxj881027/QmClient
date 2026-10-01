#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_VOLUME_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_VOLUME_LOGIC_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace QmMediaVolume
{

inline bool SameIdentity(std::string_view Left, std::string_view Right)
{
	if(Left.empty() || Left.size() != Right.size())
		return false;
	const auto Lower = [](unsigned char Character) { return Character >= 'A' && Character <= 'Z' ? Character + ('a' - 'A') : Character; };
	for(size_t Index = 0; Index < Left.size(); ++Index)
		if(Lower(Left[Index]) != Lower(Right[Index]))
			return false;
	return true;
}

inline bool MatchesPlayer(std::string_view Source, std::string_view AppId, std::string_view Executable)
{
	if(SameIdentity(Source, AppId))
		return true;
	// 传统播放器常用完整 exe 名作为 SMTC 身份；不做子串或模糊匹配。
	const size_t Separator = Executable.find_last_of("/\\");
	const std::string_view File = Separator == std::string_view::npos ? Executable : Executable.substr(Separator + 1);
	return File.size() > 4 && SameIdentity(File.substr(File.size() - 4), ".exe") && SameIdentity(Source, File);
}

struct SRequest
{
	uint64_t m_Generation = 0;
	float m_Level = 0.0f;
};

class CPendingVolume
{
	std::optional<SRequest> m_Request;

public:
	void Set(uint64_t Generation, float Level)
	{
		if(Generation != 0 && std::isfinite(Level))
			m_Request = SRequest{Generation, std::clamp(Level, 0.0f, 1.0f)};
	}
	std::optional<SRequest> Take(uint64_t Generation)
	{
		const auto Request = m_Request;
		m_Request.reset();
		return Request && Request->m_Generation == Generation ? Request : std::nullopt;
	}
	void Reset() { m_Request.reset(); }
};

}
#endif
