#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_H

#include <base/vmath.h>

#include <engine/client/enums.h>

#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/gamecore.h>

#include <cstdint>
#include <optional>

class CGameClient;
class IGraphics;

namespace qm_dummy_link
{
	inline bool CanUseFallbackSnapshot(int CharacterTick, int ConnectionTick, int TickSpeed)
	{
		const int64_t Age = static_cast<int64_t>(ConnectionTick) - CharacterTick;
		return CharacterTick == 0 || (Age >= 0 && Age <= 3LL * TickSpeed);
	}

	struct SEndpoint
	{
		vec2 m_Pos;
		bool m_Solo;
		vec2 m_HookTargetPos = m_Pos;
		bool m_Frozen = false;
		bool m_HookHitDisabled = false;
		bool m_Super = false;
		bool m_HookInputDisabled = false;
	};

	constexpr float TILE_SIZE = 32.0f;
	constexpr float LINK_FADE_END = 30.0f * TILE_SIZE;
	constexpr float HAMMER_REACH = CCharacterCore::PhysicalSize() * (0.75f + 0.5f + 1.0f);

	struct SShape
	{
		float m_Radius = 0.0f;
		float m_Sharpness = 0.0f;

		vec2 Offset(vec2 Unit) const
		{
			const float Front = std::max(Unit.x, 0.0f);
			const float Taper = m_Sharpness * Front * Front;
			return vec2(Unit.x + 1.5f * Taper * Front, Unit.y * (1.0f - 0.76f * Taper)) * m_Radius;
		}
	};

	class CDropletMotion
	{
		float m_Size = 0.0f;
		float m_SizeVelocity = 0.0f;
		float m_Sharpness = 0.0f;
		float m_ShapeVelocity = 0.0f;

	public:
		SShape Advance(float Distance, float Dt, int MotionLevel)
		{
			const float Outside = std::max(0.0f, Distance - HAMMER_REACH);
			const float SizeTarget = std::min(Outside / CCharacterCore::PhysicalSize(), 1.0f);
			const float ShapeTarget = Outside / (Outside + 160.0f);
			const auto Step = [&](float &Value, float &Velocity, float Target) {
				if(MotionLevel == 0 || (std::abs(Value - Target) < 0.0005f && std::abs(Velocity) < 0.005f))
				{
					Value = Target;
					Velocity = 0.0f;
				}
				else
				{
					constexpr auto Full = SUiSpringConfig::FromResponse(0.50f, 0.86f);
					constexpr auto Reduced = SUiSpringConfig::FromResponse(0.24f, 1.0f);
					QmSpringAdvance(Value, Velocity, Target, MotionLevel == 1 ? Reduced : Full, Dt);
				}
			};
			Step(m_Size, m_SizeVelocity, SizeTarget);
			Step(m_Sharpness, m_ShapeVelocity, ShapeTarget);
			const float Sharpness = std::clamp(m_Sharpness, 0.0f, 1.0f);
			const float Radius = CCharacterCore::PhysicalSize() * std::clamp(m_Size, 0.0f, 1.0f);
			return {std::min(Radius, 0.44f * Distance / (1.0f + 1.5f * Sharpness)), Sharpness};
		}
	};

	struct SLink
	{
		vec2 m_Main;
		vec2 m_Dummy;
		vec2 m_Direction;
		float m_Distance;
		float m_Alpha;
		bool m_InHookRange;

		SShape m_Shape{};

		vec2 LineStart() const
		{
			return m_Main + m_Direction * m_Shape.Offset(vec2(1, 0)).x;
		}
		vec2 LineEnd() const
		{
			return m_Dummy - m_Direction * m_Shape.Offset(vec2(1, 0)).x;
		}
	};

	inline std::optional<SLink> MakeLink(std::optional<SEndpoint> Main, std::optional<SEndpoint> Dummy)
	{
		if(!Main || !Dummy || (Main->m_Frozen && Dummy->m_Frozen))
			return std::nullopt;
		constexpr float FadeStart = 15.0f * TILE_SIZE;
		const vec2 Delta = Dummy->m_Pos - Main->m_Pos;
		const float Distance = length(Delta);
		if(Distance >= LINK_FADE_END)
			return std::nullopt;
		const float Alpha = 0.9f * std::min(1.0f, (LINK_FADE_END - Distance) / (LINK_FADE_END - FadeStart));
		return SLink{Main->m_Pos, Dummy->m_Pos, Distance > 0.0f ? Delta / Distance : vec2(0, 0),
			Distance, Alpha, false};
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
		CDropletMotion m_Motion;
		uint64_t m_LastDropletFrame = 0;
		std::optional<SEndpoint> CurrentEndpoint(CGameClient &GameClient, int Connection);

	public:
		std::optional<SLink> CurrentLink(CGameClient &GameClient);
	};
	void Draw(IGraphics &Graphics, const SLink &Link);
}

#endif
