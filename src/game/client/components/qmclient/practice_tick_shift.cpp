#include "practice_tick_shift.h"

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/plasma.h>
#include <game/client/prediction/entities/practice_light.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/prediction/tick_shift.h>

bool QmPractice::ShiftWorldTicks(CGameWorld &World, int64_t Delta, std::span<int> LastAttackTicks, QmTickShift::EMode Mode)
{
	if(Delta == 0)
		return true;

	if(!QmTickShift::TryShiftFields(Delta, Mode, {&World.m_GameTick}))
		return false;
	for(auto &Switcher : World.Switchers())
	{
		for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
			if(!QmTickShift::TryShiftFields(Delta, Mode, {Switcher.m_aEndTick[Team] > 0 ? &Switcher.m_aEndTick[Team] : nullptr, Switcher.m_aLastUpdateTick[Team] > 0 ? &Switcher.m_aLastUpdateTick[Team] : nullptr}))
				return false;
	}
	for(int &Tick : LastAttackTicks)
		if(!QmTickShift::TryShiftFields(Delta, Mode, {Tick >= 0 ? &Tick : nullptr}))
			return false;
	for(auto *pChar = static_cast<CCharacter *>(World.FindFirst(CGameWorld::ENTTYPE_CHARACTER)); pChar; pChar = static_cast<CCharacter *>(pChar->TypeNext()))
		if(!pChar->ShiftTickBase(Delta, Mode))
			return false;
	for(auto *pProj = static_cast<CProjectile *>(World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE)); pProj; pProj = static_cast<CProjectile *>(pProj->TypeNext()))
		if(!pProj->ShiftStartTick(Delta, Mode))
			return false;
	for(auto *pLaser = static_cast<CLaser *>(World.FindFirst(CGameWorld::ENTTYPE_LASER)); pLaser; pLaser = static_cast<CLaser *>(pLaser->TypeNext()))
		if(pLaser->GetEvalTick() != -1 && !pLaser->ShiftEvalTick(Delta, Mode))
			return false;
	for(auto *pPlasma = static_cast<CPlasma *>(World.FindFirst(CGameWorld::ENTTYPE_PLASMA)); pPlasma; pPlasma = static_cast<CPlasma *>(pPlasma->TypeNext()))
		if(!pPlasma->ShiftEvalTick(Delta, Mode))
			return false;
	for(auto *pEnt = World.FindFirst(CGameWorld::ENTTYPE_LIGHT); pEnt; pEnt = pEnt->TypeNext())
		if(auto *pLight = dynamic_cast<CPracticeLight *>(pEnt); pLight && !pLight->ShiftEvalTick(Delta, Mode))
			return false;
	return true;
}

bool QmPractice::TryShiftTicks(CGameWorld &World, int64_t Delta, std::span<int> LastAttackTicks)
{
	if(Delta <= 0)
		return true;
	// 同一主线程内无回调/分配，检查与提交之间各时间字段不被其它 owner 改写。
	return ShiftWorldTicks(World, Delta, LastAttackTicks, QmTickShift::EMode::CHECK) && ShiftWorldTicks(World, Delta, LastAttackTicks, QmTickShift::EMode::COMMIT);
}
