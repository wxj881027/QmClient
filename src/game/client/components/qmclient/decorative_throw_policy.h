#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DECORATIVE_THROW_POLICY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DECORATIVE_THROW_POLICY_H

#include <base/str.h>
#include <base/vmath.h>

#include <cmath>

namespace QmDecorativeThrow
{
	inline constexpr const char *CAPABILITY = "decorative_throw_v1";
	inline constexpr const char *NAMES[] = {"grass", "tomato", "egg"};
	inline constexpr int COUNT = 3;

	inline int TypeFromName(const char *pName)
	{
		for(int Type = 0; pName && Type < COUNT; ++Type)
			if(str_comp(pName, NAMES[Type]) == 0)
				return Type;
		return -1;
	}

	inline bool ValidGeometry(vec2 Origin, vec2 Direction)
	{
		if(!std::isfinite(Origin.x) || !std::isfinite(Origin.y) ||
			!std::isfinite(Direction.x) || !std::isfinite(Direction.y) ||
			std::abs(Origin.x) > 1048576.0f || std::abs(Origin.y) > 1048576.0f)
			return false;
		const float Length = length(Direction);
		return Length >= 0.5f && Length <= 2.0f;
	}

	inline bool ShouldShowRemote(bool Enabled, bool ShowEmotes, bool ShowLaunch, bool Ignored,
		const char *pEventServer, const char *pCurrentServer, const char *pEventName, const char *pCurrentName,
		vec2 Origin, vec2 PlayerPosition)
	{
		return Enabled && ShowEmotes && ShowLaunch && !Ignored &&
		       pEventServer && pCurrentServer && pEventServer[0] && str_comp(pEventServer, pCurrentServer) == 0 &&
		       pEventName && pCurrentName && str_comp(pEventName, pCurrentName) == 0 &&
		       distance(Origin, PlayerPosition) <= 256.0f;
	}
}

#endif
