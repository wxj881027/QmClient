#include "shadow_gun_replay.h"

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int PLASMA_PER_SECOND = 3;
	constexpr float PLASMA_RANGE = 700.0f;
	constexpr float PLASMA_ACCEL = 1.1f;
	constexpr int MAX_PLASMA_PER_VIEW = 256;

	int FireDelay()
	{
		return maximum(1, SERVER_TICK_SPEED / PLASMA_PER_SECOND);
	}

	bool SwitchActive(const std::vector<SSwitchers> &vSwitchers, int Number, int Team)
	{
		if(Number <= 0)
			return true;
		if(Number >= (int)vSwitchers.size() || Team < 0 || Team >= NUM_DDRACE_TEAMS)
			return false;
		return vSwitchers[Number].m_aStatus[Team];
	}
}

namespace qm_shadow_gun
{
	void Load(CCollision *pCollision, std::vector<SGun> &vGuns)
	{
		vGuns.clear();
		if(pCollision == nullptr)
			return;
		const CTile *pGame = pCollision->GameLayer();
		const CTile *pFront = pCollision->FrontLayer();
		const CSwitchTile *pSwitch = pCollision->SwitchLayer();
		if(pGame == nullptr)
			return;

		const int Width = pCollision->GetWidth();
		const int Height = pCollision->GetHeight();
		for(int y = 0; y < Height; ++y)
		{
			for(int x = 0; x < Width; ++x)
			{
				const int Index = y * Width + x;
				const vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				const auto AddGun = [&](int EntityIndex, int Layer, int Number) {
					SGun Gun;
					switch(EntityIndex)
					{
					case ENTITY_PLASMAE:
						Gun.m_Explosive = true;
						break;
					case ENTITY_PLASMAF:
						Gun.m_Freeze = true;
						break;
					case ENTITY_PLASMA:
						Gun.m_Explosive = true;
						Gun.m_Freeze = true;
						break;
					case ENTITY_PLASMAU:
						break;
					default:
						return;
					}
					Gun.m_StartPos = Pos;
					Gun.m_Pos = Pos;
					Gun.m_Layer = Layer;
					Gun.m_Number = Number;
					std::fill(std::begin(Gun.m_aLastFireTeam), std::end(Gun.m_aLastFireTeam), 0);
					vGuns.push_back(Gun);
				};

				if(pGame[Index].m_Index >= ENTITY_OFFSET)
					AddGun(pGame[Index].m_Index - ENTITY_OFFSET, LAYER_GAME, 0);
				if(pFront != nullptr && pFront[Index].m_Index >= ENTITY_OFFSET)
					AddGun(pFront[Index].m_Index - ENTITY_OFFSET, LAYER_FRONT, 0);
				if(pSwitch != nullptr && pSwitch[Index].m_Type >= ENTITY_OFFSET)
					AddGun(pSwitch[Index].m_Type - ENTITY_OFFSET, LAYER_SWITCH, pSwitch[Index].m_Number);
			}
		}
	}

	void Reset(std::vector<SGun> &vGuns, std::vector<SPlasma> &vPlasmas)
	{
		vPlasmas.clear();
		for(SGun &Gun : vGuns)
		{
			Gun.m_Pos = Gun.m_StartPos;
			Gun.m_Core = vec2(0.0f, 0.0f);
			std::fill(std::begin(Gun.m_aLastFireTeam), std::end(Gun.m_aLastFireTeam), 0);
		}
	}

	void Tick(std::vector<SGun> &vGuns, std::vector<SPlasma> &vPlasmas, int Tick,
		const std::vector<SSwitchers> &vSwitchers, int ViewTeam, CCollision *pCollision,
		const FTargetProvider &TargetProvider)
	{
		if(vGuns.empty() || pCollision == nullptr)
			return;

		// 先推进已有等离子球。影子只做视觉重放，命中墙体即消失，不命中任何角色。
		for(size_t i = 0; i < vPlasmas.size();)
		{
			SPlasma &Plasma = vPlasmas[i];
			if(Plasma.m_LifeTime <= 0)
			{
				vPlasmas.erase(vPlasmas.begin() + i);
				continue;
			}
			--Plasma.m_LifeTime;
			const vec2 Next = Plasma.m_Pos + Plasma.m_Core;
			if(pCollision->IntersectNoLaser(Plasma.m_Pos, Next, nullptr, nullptr))
			{
				vPlasmas.erase(vPlasmas.begin() + i);
				continue;
			}
			Plasma.m_Pos = Next;
			Plasma.m_Core *= PLASMA_ACCEL;
			++i;
		}

		const int MoveInterval = maximum(1, (int)((float)SERVER_TICK_SPEED * 0.15f));
		if(Tick % MoveInterval == 0)
		{
			for(SGun &Gun : vGuns)
			{
				pCollision->MoverSpeed(Gun.m_Pos.x, Gun.m_Pos.y, &Gun.m_Core);
				Gun.m_Pos += Gun.m_Core;
			}
		}

		std::vector<STarget> vTargets;
		TargetProvider(Tick, vTargets);
		if(vTargets.empty())
			return;

		const int Delay = FireDelay();
		const int LifeTime = (int)((float)SERVER_TICK_SPEED * 1.5f);
		const auto TargetTeam = [ViewTeam](const STarget &Target) {
			if(Target.m_Team >= 0 && Target.m_Team < NUM_DDRACE_TEAMS)
				return Target.m_Team;
			if(ViewTeam >= 0 && ViewTeam < NUM_DDRACE_TEAMS)
				return ViewTeam;
			return (int)TEAM_FLOCK;
		};
		for(SGun &Gun : vGuns)
		{
			const bool IsSwitchLayer = Gun.m_Layer == LAYER_SWITCH;

			int aMinDist[NUM_DDRACE_TEAMS] = {};
			int aTarget[NUM_DDRACE_TEAMS];
			std::fill(std::begin(aTarget), std::end(aTarget), -1);
			std::vector<int> vFireAt;
			for(size_t i = 0; i < vTargets.size(); ++i)
			{
				const STarget &Target = vTargets[i];
				const int Team = TargetTeam(Target);
				if(IsSwitchLayer && Gun.m_Number > 0 && !SwitchActive(vSwitchers, Gun.m_Number, Team))
					continue;
				if(Gun.m_aLastFireTeam[Team] + Delay > Tick)
					continue;
				const float Distance = distance(Target.m_Pos, Gun.m_Pos);
				if(Distance > PLASMA_RANGE)
					continue;
				if(pCollision->IntersectLine(Gun.m_Pos, Target.m_Pos, nullptr, nullptr))
					continue;
				if(aMinDist[Team] == 0 || aMinDist[Team] > (int)Distance)
				{
					aMinDist[Team] = (int)Distance;
					aTarget[Team] = (int)i;
				}
			}
			for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
			{
				if(aTarget[Team] != -1)
					vFireAt.push_back(aTarget[Team]);
			}

			for(const int TargetIndex : vFireAt)
			{
				const STarget &Target = vTargets[TargetIndex];
				const int Team = TargetTeam(Target);
				Gun.m_aLastFireTeam[Team] = Tick;
				if((int)vPlasmas.size() >= MAX_PLASMA_PER_VIEW)
					vPlasmas.erase(vPlasmas.begin());
				SPlasma Plasma;
				Plasma.m_Pos = Gun.m_Pos;
				Plasma.m_Core = normalize(Target.m_Pos - Gun.m_Pos);
				Plasma.m_LifeTime = LifeTime;
				Plasma.m_Subtype = (Gun.m_Explosive ? 1 : 0) | (Gun.m_Freeze ? 2 : 0);
				Plasma.m_SwitchNumber = Gun.m_Number;
				vPlasmas.push_back(Plasma);
			}
		}
	}
}
