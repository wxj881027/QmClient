#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_HOOK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_DISTANCE_LINK_HOOK_H

#include "collision_position.h"
#include "dummy_distance_link.h"
#include "qm_hook_coll_intersection.h"

#include <engine/shared/config.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <bitset>

namespace qm_dummy_link
{
	enum class EHookReach
	{
		BLOCKED,
		POSSIBLE,
		IN_RANGE,
	};

	namespace hook_link
	{
		constexpr float LAUNCH_OFFSET = CCharacterCore::PhysicalSize() * 1.5f;

		template<typename F>
		bool AnyAim(vec2 From, vec2 Target, F &&Try)
		{
			const vec2 Delta = Target - From;
			const float Distance = length(Delta);
			if(Distance == 0.0f)
				return Try(vec2(1, 0));
			const vec2 Center = Delta / Distance;
			const vec2 Side(-Center.y, Center.x);
			// 目标边缘也能被钩住。中心被挡时，继续检查两侧的瞄准方向。
			const float Cone = std::min(1.0f, QmHookCollTargetRadius / Distance);
			for(const float Offset : {0.0f, -0.24f, 0.24f, -0.48f, 0.48f, -0.72f, 0.72f, -0.96f, 0.96f})
			{
				const float Sin = Offset * Cone;
				if(Try(Center * std::sqrt(1.0f - Sin * Sin) + Side * Sin))
					return true;
			}
			return false;
		}

		inline bool TouchesTarget(vec2 From, vec2 To, vec2 Target)
		{
			vec2 Closest;
			return closest_point_on_line(From, To, Target, Closest) && distance(Closest, Target) < QmHookCollTargetRadius;
		}

		inline bool EntersHookTeleport(CCollision &Collision, vec2 From, vec2 Dir, float HookLength, int Tele)
		{
			const vec2 Start = From + Dir * LAUNCH_OFFSET;
			const vec2 End = From + Dir * HookLength;
			int HitTele = 0;
			return QmCollision::CanQueryHookLine(Start, End) &&
			       Collision.IntersectLineTeleHook(Start, End, nullptr, nullptr, &HitTele) == TILE_TELEINHOOK && HitTele == Tele;
		}

		inline EHookReach TeleportReach(CCollision &Collision, vec2 From, vec2 Target, float HookLength, float HookFireSpeed, bool InDirectRange)
		{
			if(!Collision.HasHookTeleIns())
				return EHookReach::BLOCKED;
			// 长钩或复杂传送图达到查询上限时保留普通色，避免把未检查的路径当成堵死。
			if(HookLength > LINK_FADE_END || !QmCollision::CanQueryHookLine(From - vec2(HookLength, HookLength), From + vec2(HookLength, HookLength)))
				return EHookReach::POSSIBLE;
			const int Width = Collision.GetWidth();
			if(Width <= 0 || Collision.GetHeight() <= 0)
				return EHookReach::POSSIBLE;
			const int First = Collision.GetPureMapIndex(From - vec2(HookLength, HookLength));
			const int Last = Collision.GetPureMapIndex(From + vec2(HookLength, HookLength));
			if(First < 0 || Last < 0)
				return EHookReach::POSSIBLE;

			std::bitset<256> Checked;
			const float ExitTravel = std::floor(HookLength / HookFireSpeed) * HookFireSpeed;
			bool FoundPortal = false;
			int RaysLeft = 48;
			for(int Y = First / Width; Y <= Last / Width; ++Y)
			{
				for(int X = First % Width; X <= Last % Width; ++X)
				{
					const int Index = Y * Width + X;
					const int Tele = g_Config.m_SvOldTeleportHook ? Collision.IsTeleport(Index) : Collision.IsTeleportHook(Index);
					if(Tele == 0 || distance(Collision.GetPos(Index), From) > HookLength + 23.0f)
						continue;
					const auto &vExits = Collision.TeleOuts(Tele - 1);
					if(!Checked.test(Tele))
					{
						Checked.set(Tele);
						for(const vec2 Exit : vExits)
						{
							if(RaysLeft-- <= 0)
								return EHookReach::POSSIBLE;
							if(distance(Exit, Target) >= LAUNCH_OFFSET + ExitTravel + QmHookCollTargetRadius)
								continue;
							const bool Reached = AnyAim(Exit, Target, [&](vec2 Dir) {
								if(--RaysLeft < 0)
									return true;

								if(!EntersHookTeleport(Collision, From, Dir, HookLength, Tele))
									return false;

								const vec2 ExitStart = Exit + Dir * LAUNCH_OFFSET;
								const float Travel = std::min(ExitTravel, std::max(1.0f, dot(Target - ExitStart, Dir)));
								const vec2 ExitEnd = ExitStart + Dir * Travel;
								if(!QmCollision::CanQueryHookLine(ExitStart, ExitEnd))
									return false;
								vec2 Hit;
								int ExitTele = 0;
								Collision.IntersectLineTeleHook(ExitStart, ExitEnd, &Hit, nullptr, &ExitTele);
								return TouchesTarget(ExitStart, Hit, Target);
							});
							if(Reached)
								return RaysLeft >= 0 && InDirectRange ? EHookReach::IN_RANGE : EHookReach::POSSIBLE;
						}
					}

					if(!vExits.empty() && !FoundPortal)
					{
						FoundPortal = AnyAim(From, Collision.GetPos(Index), [&](vec2 Dir) {
							if(RaysLeft-- <= 0)
								return true;
							return EntersHookTeleport(Collision, From, Dir, HookLength, Tele);
						});
					}
					if(RaysLeft < 0)
						return EHookReach::POSSIBLE;
				}
			}
			// 有可用出口时仍可能通过传送链或入口边缘接到目标。
			return FoundPortal ? EHookReach::POSSIBLE : EHookReach::BLOCKED;
		}
	}

	inline EHookReach HookReach(CCollision &Collision, const SEndpoint &Source, const SEndpoint &Target, const CTuningParams &Tuning, bool SameTeam)
	{
		const float HookLength = Tuning.m_HookLength;
		const float HookFireSpeed = Tuning.m_HookFireSpeed;
		if(Source.m_HookInputDisabled || Source.m_HookHitDisabled || !Tuning.m_PlayerHooking || HookLength <= 0.0f || HookFireSpeed <= 0.0f)
			return EHookReach::BLOCKED;
		if(!Source.m_Super && !Target.m_Super && (!SameTeam || Source.m_Solo || Target.m_Solo))
			return EHookReach::BLOCKED;
		if(!QmCollision::CanQueryHookLine(Source.m_Pos, Target.m_HookTargetPos) || !Collision.GameLayer())
			return EHookReach::POSSIBLE;

		const bool InDirectRange = distance(Source.m_Pos, Target.m_HookTargetPos) < HookLength + QmHookCollTargetRadius;
		bool UncertainOrBeyondRange = false;
		const bool FoundDirectPath = hook_link::AnyAim(Source.m_Pos, Target.m_HookTargetPos, [&](vec2 Dir) {
			const vec2 Start = Source.m_Pos + Dir * hook_link::LAUNCH_OFFSET;
			const float AimTravel = std::max(1.0f, dot(Target.m_HookTargetPos - Start, Dir));
			const vec2 End = Start + Dir * AimTravel;
			int Tele = 0;
			vec2 Hit;
			if(!QmCollision::CanQueryHookLine(Start, End))
			{
				UncertainOrBeyondRange = true;
				return false;
			}
			Collision.IntersectLineTeleHook(Start, End, &Hit, nullptr, &Tele);
			if(!hook_link::TouchesTarget(Start, Hit, Target.m_HookTargetPos))
				return false;

			UncertainOrBeyondRange = true;
			if(!InDirectRange)
				return true;

			const float Reach = HookLength - hook_link::LAUNCH_OFFSET;
			return Reach > 0.0f &&
			       hook_link::TouchesTarget(Start, Start + Dir * std::min(Reach, distance(Start, Hit)), Target.m_HookTargetPos);
		});
		if(FoundDirectPath)
			return InDirectRange ? EHookReach::IN_RANGE : EHookReach::POSSIBLE;
		if(UncertainOrBeyondRange)
			return EHookReach::POSSIBLE;
		return hook_link::TeleportReach(Collision, Source.m_Pos, Target.m_HookTargetPos, HookLength, HookFireSpeed, InDirectRange);
	}

	inline bool ApplyHookReach(SLink &Link, EHookReach Active, EHookReach Other)
	{
		Link.m_InHookRange = Active == EHookReach::IN_RANGE;
		return Active != EHookReach::BLOCKED || Other != EHookReach::BLOCKED;
	}
}

#endif
