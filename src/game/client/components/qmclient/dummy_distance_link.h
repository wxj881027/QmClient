#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_H

#include <base/vmath.h>

#include <engine/client/enums.h>

#include <game/gamecore.h>

#include <optional>

class CGameClient;
class IGraphics;

namespace qm_dummy_link
{
	struct SEndpoint
	{
		vec2 m_Pos;
		bool m_Solo;
	};

	constexpr float HAMMER_REACH = CCharacterCore::PhysicalSize() * (0.75f + 0.5f + 1.0f);
	constexpr float TIP_REACH = HAMMER_REACH * 1.4f;

	struct SLink
	{
		vec2 m_Main;
		vec2 m_Dummy;
		vec2 m_Direction;
		float m_Distance;
		float m_Alpha;
		bool m_InHookRange;

		std::optional<vec2> LineStart(bool Droplet) const
		{
			const float Offset = Droplet ? TIP_REACH : 0.0f;
			if(m_Distance <= Offset)
				return std::nullopt;
			return m_Main + m_Direction * Offset;
		}
	};

	inline std::optional<SLink> MakeLink(std::optional<SEndpoint> Main, std::optional<SEndpoint> Dummy, float HookLength)
	{
		if(!Main || !Dummy)
			return std::nullopt;
		const vec2 Delta = Dummy->m_Pos - Main->m_Pos;
		const float Distance = length(Delta);
		return SLink{Main->m_Pos, Dummy->m_Pos, Distance > 0.0f ? Delta / Distance : vec2(0, 0),
			Distance, 0.15f + 0.60f / (1.0f + Distance / 320.0f),
			!Main->m_Solo && !Dummy->m_Solo && HookLength > 0.0f && Distance <= HookLength};
	}

	class CView
	{
		struct SFallback
		{
			int m_Tick = -1;
			int m_ClientId = -1;
			std::optional<SEndpoint> m_Endpoint;
		};
		SFallback m_aFallback[NUM_DUMMIES]{};
		std::optional<SEndpoint> CurrentEndpoint(CGameClient &GameClient, int Connection);

	public:
		std::optional<SLink> CurrentLink(CGameClient &GameClient);
	};
	void Draw(IGraphics &Graphics, const SLink &Link, bool Droplet);
}

#endif
