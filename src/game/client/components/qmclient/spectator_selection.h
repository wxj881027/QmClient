#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTION_H

#include <base/vmath.h>

#include <cmath>
#include <cstddef>
#include <span>

namespace QmSpectatorSelection
{
	inline bool NeedsRetarget(std::span<const bool> Selected, int CurrentId)
	{
		return CurrentId < 0 || static_cast<std::size_t>(CurrentId) >= Selected.size() || !Selected[CurrentId];
	}

	class CClosest
	{
		vec2 m_Origin;
		double m_DistanceSquared = 0.0;
		int m_SelectedId = -1;

	public:
		explicit CClosest(vec2 Origin) :
			m_Origin(Origin) {}
		int SelectedId() const { return m_SelectedId; }

		void Consider(int ClientId, vec2 Position)
		{
			// 坐标先拓宽再相减；比较距离平方，避免float距离转int及大坐标平方溢出。
			const double X = static_cast<double>(Position.x) - m_Origin.x;
			const double Y = static_cast<double>(Position.y) - m_Origin.y;
			const double DistanceSquared = X * X + Y * Y;
			if(ClientId < 0 || !std::isfinite(DistanceSquared))
				return;
			if(m_SelectedId < 0 || DistanceSquared < m_DistanceSquared)
			{
				m_SelectedId = ClientId;
				m_DistanceSquared = DistanceSquared;
			}
		}
	};
}

#endif
