#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_VIEW_CAMERA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_VIEW_CAMERA_H

#include <base/vmath.h>

#include <algorithm>
#include <cmath>

namespace qmclient::rank_ghost
{
	// 任一轴平移不可表示时保留整个旧中心；正常范围保持原 float 运算。
	inline bool PanFreeCamera(vec2 &Center, vec2 MouseDelta, float WorldWidth, int WindowWidth)
	{
		if(!std::isfinite(Center.x) || !std::isfinite(Center.y) ||
			!std::isfinite(MouseDelta.x) || !std::isfinite(MouseDelta.y) ||
			!std::isfinite(WorldWidth) || !(WorldWidth > 0.0f))
			return false;
		const float Pixels = std::max(1.0f, static_cast<float>(WindowWidth));
		const vec2 Candidate = Center + MouseDelta * (WorldWidth / Pixels);
		if(!std::isfinite(Candidate.x) || !std::isfinite(Candidate.y))
			return false;
		Center = Candidate;
		return true;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_VIEW_CAMERA_H
