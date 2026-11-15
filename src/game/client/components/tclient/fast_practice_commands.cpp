/* Copyright © 2026 BestProject Team */
// 快速练习的命令层：聊天命令解析、/practice 子命令执行与控制台命令注册。
// 与预测循环分离，避免单个文件同时承载热路径与命令解析。

#include "fast_practice.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>

namespace
{
	int ClampWeaponId(int WeaponId)
	{
		return std::clamp(WeaponId, -1, NUM_WEAPONS - 1);
	}
}

bool CFastPractice::ExecutePracticeCommand(int Team, int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	(void)Team;
	WeaponsMutated = false;
	if(vArgs.empty() || vArgs[0].size() < 2 || vArgs[0][0] != '/')
		return false;

	std::string Cmd = vArgs[0].substr(1);
	std::transform(Cmd.begin(), Cmd.end(), Cmd.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	auto &State = m_aPracticeCommandState[LocalClientId];
	const auto ApplyTeleport = [&](const vec2 &Pos) {
		// 传送会在轨迹上造成瞬移，先封存当前段再跳。角色整理（解冻、清速度、
		// 重置跳跃）在 TeleportCharacter 里完成，整理之后才记录 /lasttp，
		// 与服务器先 ResetJumps/Unfreeze/ResetVelocity、再存 m_LastTeleTee 的时序一致。
		OnPracticePositionJump();
		TeleportCharacter(pChar, Pos);
		RecordPracticeCommandTeleport(LocalClientId, pChar);
	};
	const auto GiveWeapon = [&](int Weapon, bool Remove) {
		if(Weapon == -1)
		{
			pChar->GiveWeapon(WEAPON_SHOTGUN, Remove);
			pChar->GiveWeapon(WEAPON_GRENADE, Remove);
			pChar->GiveWeapon(WEAPON_LASER, Remove);
		}
		else
		{
			pChar->GiveWeapon(Weapon, Remove);
		}
	};
	const auto EnsureActiveWeaponIsValid = [&]() {
		const CCharacterCore Core = pChar->GetCore();
		if(Core.m_ActiveWeapon >= WEAPON_HAMMER && Core.m_ActiveWeapon < NUM_WEAPONS && Core.m_aWeapons[Core.m_ActiveWeapon].m_Got)
			return;

		if(Core.m_aWeapons[WEAPON_HAMMER].m_Got)
		{
			pChar->SetActiveWeapon(WEAPON_HAMMER);
			return;
		}
		if(Core.m_aWeapons[WEAPON_GUN].m_Got)
		{
			pChar->SetActiveWeapon(WEAPON_GUN);
			return;
		}
		for(int Weapon = WEAPON_SHOTGUN; Weapon < NUM_WEAPONS; Weapon++)
		{
			if(Core.m_aWeapons[Weapon].m_Got)
			{
				pChar->SetActiveWeapon(Weapon);
				return;
			}
		}
	};
	const auto ToggleHit = [&](int Weapon) {
		CCharacterCore Core = pChar->GetCore();
		switch(Weapon)
		{
		case WEAPON_HAMMER: Core.m_HammerHitDisabled = !Core.m_HammerHitDisabled; break;
		case WEAPON_SHOTGUN: Core.m_ShotgunHitDisabled = !Core.m_ShotgunHitDisabled; break;
		case WEAPON_GRENADE: Core.m_GrenadeHitDisabled = !Core.m_GrenadeHitDisabled; break;
		case WEAPON_LASER: Core.m_LaserHitDisabled = !Core.m_LaserHitDisabled; break;
		default: return;
		}
		pChar->SetCore(Core);
	};
	const auto ClampToPlayableBounds = [&](const vec2 &Pos) {
		float MinX = -std::numeric_limits<float>::max();
		float MinY = -std::numeric_limits<float>::max();
		float MaxX = std::numeric_limits<float>::max();
		float MaxY = std::numeric_limits<float>::max();
		if(CMapItemLayerTilemap *pGameLayer = Layers()->GameLayer())
		{
			constexpr float OuterKillTileBoundaryDistance = 201.0f * 32.0f;
			MinX = (-OuterKillTileBoundaryDistance) + 1.0f;
			MinY = (-OuterKillTileBoundaryDistance) + 1.0f;
			MaxX = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Width * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
			MaxY = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Height * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
		}
		return vec2(std::clamp(Pos.x, MinX, MaxX), std::clamp(Pos.y, MinY, MaxY));
	};

	if(Cmd == "unpractice")
	{
		Disable();
		EchoPractice("practice mode disabled");
		return true;
	}
	if(Cmd == "practice")
	{
		EchoPractice("practice mode already enabled");
		return true;
	}
	if(Cmd == "practicecmdlist")
	{
		EchoPractice("available commands: /r /back /rescuemode /restart /tp /teleport /tpxy /lasttp /tc /telecursor /totele /totelecp /tofinish /pg /solo /unsolo /deep /undeep /livefreeze /unlivefreeze /shotgun /grenade /laser /rifle /unshotgun /ungrenade /unlaser /unrifle /weapons /unweapons /addweapon /removeweapon /jetpack /unjetpack /infjump /uninfjump /setjumps /ninja /unninja /endless /unendless /invincible /collision /hookcollision /hitothers /setswitch /practice /unpractice");
		return true;
	}
	if(Cmd == "pg" || Cmd == "practiceghost")
	{
		// /pg [名字]：把刚跑通的那一段存成幽灵。名字只是给文件加个可辨认的后缀，
		// 留空也完全可用（时间戳已经能区分）。
		const char *pName = vArgs.size() > 1 ? vArgs[1].c_str() : "";
		SavePracticeGhost(pName);
		return true;
	}
	if(Cmd == "kill")
	{
		ResetPracticeToAnchor();
		return true;
	}

	if(Cmd == "rescuemode")
	{
		if(vArgs.size() <= 1)
		{
			EchoPractice("rescue mode: %s", State.m_RescueManual ? "manual" : "auto");
			return true;
		}
		std::string Arg = vArgs[1];
		std::transform(Arg.begin(), Arg.end(), Arg.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if(Arg == "auto")
		{
			State.m_RescueManual = false;
			EchoPractice("rescue mode changed to auto");
		}
		else if(Arg == "manual")
		{
			State.m_RescueManual = true;
			EchoPractice("rescue mode changed to manual");
		}
		else if(Arg == "list")
		{
			EchoPractice("available rescue modes: auto, manual");
		}
		else
		{
			EchoPractice("unknown argument. check '/rescuemode list'");
		}
		return true;
	}

	if(Cmd == "r" || Cmd == "rescue")
	{
		// 逐条对齐服务器 ConRescue / TrySetRescue / Rescue：
		// 救援点条件不看 m_FreezeTime；manual 模式下 /r 优先就地设点，设点成功不传送；
		// super/invincible 只拒绝传送，不影响设点。
		const CCharacterCore *pCore = pChar->Core();
		const bool CanSetNow = QmPracticeCanSetRescuePoint(pCore->m_IsInFreeze, pCore->m_DeepFrozen,
			pChar->IsGrounded(), IsInFreezePickupRange(pChar));
		const bool HasSavedPoint = State.m_RescueManual ? State.m_HasRescueManual : State.m_HasRescueAuto;
		const bool TeleportBlocked = pCore->m_Super || pCore->m_Invincible;
		const SQmPracticeRescueDecision Decision = QmEvaluatePracticeRescue(
			State.m_RescueManual, CanSetNow, HasSavedPoint, TeleportBlocked);

		if(Decision.m_SavePoint)
		{
			State.m_HasRescueManual = true;
			CapturePracticeRescueTee(pChar, pChar->GameWorld()->GameTick(), State.m_ManualTee);
			EchoPractice("manual rescue point saved");
		}

		const SQmPracticeSavedTee &Tee = State.m_RescueManual ? State.m_ManualTee : State.m_AutoTee;
		if(Decision.m_Teleport && Tee.m_Valid)
		{
			// 服务器 Rescue() 不更新 /lasttp 与 /back 的记录，这里同样不碰这两个状态。
			OnPracticePositionJump();
			ApplyPracticeRescueTee(pChar, Tee);
			// 服务器在 Rescue() 成功后立刻 Unfreeze()。
			pChar->Unfreeze();
		}
		return true;
	}

	if(Cmd == "back")
	{
		if(!State.m_Records.m_HasLastDeath)
		{
			EchoPractice("there is nowhere to go back to");
			return true;
		}
		// 对齐服务器 ConBack：把 /back 记录塞进当前救援模式的救援槽，再走 Rescue()。
		// 赋值本身不置位救援点标志，super/invincible 也会被 Rescue() 拒绝；
		// 这两种情况只更新记录，不移动角色。
		const bool TeleportBlocked = pChar->Core()->m_Super || pChar->Core()->m_Invincible;
		const bool ModeSavePointExists = State.m_RescueManual ? State.m_HasRescueManual : State.m_HasRescueAuto;
		const SQmPracticeBackDecision Decision = QmEvaluatePracticeBack(
			State.m_Records.m_HasLastDeath, ModeSavePointExists, TeleportBlocked);
		if(Decision.m_AssignRescueTee)
		{
			if(State.m_RescueManual)
				State.m_ManualTee = State.m_Records.m_LastDeathTee;
			else
				State.m_AutoTee = State.m_Records.m_LastDeathTee;
		}
		if(Decision.m_Teleport)
		{
			OnPracticePositionJump();
			ApplyPracticeRescueTee(pChar, State.m_RescueManual ? State.m_ManualTee : State.m_AutoTee);
			pChar->Unfreeze();
		}
		return true;
	}

	if(Cmd == "lasttp")
	{
		const SQmPracticeSavedTee &Tee = State.m_Records.m_LastTeleportTee;
		if(!State.m_Records.m_HasLastTeleport || !Tee.m_Valid)
		{
			EchoPractice("you haven't previously teleported");
			return true;
		}
		// 对齐服务器 ConLastTele：载入整份 tee，不对世界做别的处理。
		OnPracticePositionJump();
		ApplyPracticeRescueTee(pChar, Tee);
		return true;
	}

	if(Cmd == "tp" || Cmd == "teleport" || Cmd == "tc" || Cmd == "telecursor")
	{
		vec2 Target;
		if(vArgs.size() > 1)
		{
			const int TargetId = FindClientByName(vArgs[1].c_str());
			if(TargetId < 0 || !GameClient()->m_Snap.m_aCharacters[TargetId].m_Active)
			{
				EchoPractice("no player with this name found");
				return true;
			}
			// 分支世界只保留本体与分身，其他玩家在练习世界里不存在。
			// 按服务器快照传送只会落到另一条世界线的空地上，因此直接拒绝。
			CCharacter *pPracticeTarget = nullptr;
			if(TargetId == m_EnableLocalClientId || TargetId == m_EnableDummyClientId)
				pPracticeTarget = m_PracticeBaseWorld.GetCharacterById(TargetId);
			if(!pPracticeTarget)
			{
				EchoPractice("player is not in the practice world");
				return true;
			}
			Target = pPracticeTarget->Core()->m_Pos;
		}
		else
		{
			Target = ResolvePracticeCursorTarget();
		}
		ApplyTeleport(ClampToPlayableBounds(Target));
		return true;
	}

	if(Cmd == "tpxy")
	{
		if(vArgs.size() < 3)
		{
			EchoPractice("usage: /tpxy x y");
			return true;
		}

		float X = 0.0f;
		float Y = 0.0f;
		if(!ParseCoordinateToken(vArgs[1].c_str(), pChar->Core()->m_Pos.x, X))
		{
			EchoPractice("invalid X coordinate");
			return true;
		}
		if(!ParseCoordinateToken(vArgs[2].c_str(), pChar->Core()->m_Pos.y, Y))
		{
			EchoPractice("invalid Y coordinate");
			return true;
		}

		ApplyTeleport(ClampToPlayableBounds(vec2(X, Y)));
		return true;
	}

	if(Cmd == "tofinish")
	{
		vec2 FinishPos(0.0f, 0.0f);
		if(!FindFinishPosition(FinishPos))
		{
			EchoPractice("there is no finish tile on the map");
			return true;
		}
		ApplyTeleport(FinishPos);
		return true;
	}

	if(Cmd == "totele" || Cmd == "totelecp")
	{
		if(vArgs.size() < 2)
		{
			EchoPractice("usage: /%s <index>", Cmd.c_str());
			return true;
		}
		int TeleIndex = 0;
		if(!str_toint(vArgs[1].c_str(), &TeleIndex) || TeleIndex <= 0)
		{
			EchoPractice("invalid teleporter index");
			return true;
		}

		const auto &vTeleOuts = Cmd == "totele" ? Collision()->TeleOuts(TeleIndex - 1) : Collision()->TeleCheckOuts(TeleIndex - 1);
		if(vTeleOuts.empty())
		{
			EchoPractice("there is no teleporter with that index on the map");
			return true;
		}

		// 服务器 ConToTeleporter 在多出口之间随机取一个，练习保持同一语义。
		const int TeleOut = QmPracticeTeleOutIndex((int)vTeleOuts.size(), secure_rand_below((int)vTeleOuts.size()));
		ApplyTeleport(vTeleOuts[TeleOut]);
		return true;
	}

	if(Cmd == "solo" || Cmd == "unsolo")
	{
		pChar->SetSolo(Cmd == "solo");
		return true;
	}

	if(Cmd == "deep" || Cmd == "undeep")
	{
		CCharacterCore Core = pChar->GetCore();
		if(Cmd == "deep")
		{
			Core.m_DeepFrozen = true;
			pChar->SetCore(Core);
			pChar->Freeze();
		}
		else
		{
			Core.m_DeepFrozen = false;
			pChar->SetCore(Core);
			pChar->Unfreeze();
		}
		NormalizeCharacterAfterReset(pChar, Cmd == "deep");
		return true;
	}

	if(Cmd == "livefreeze" || Cmd == "unlivefreeze")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_LiveFrozen = Cmd == "livefreeze";
		pChar->SetCore(Core);
		if(Cmd == "unlivefreeze")
			pChar->Unfreeze();
		NormalizeCharacterAfterReset(pChar, Cmd == "livefreeze");
		return true;
	}

	if(Cmd == "shotgun" || Cmd == "grenade" || Cmd == "laser" || Cmd == "rifle" || Cmd == "unshotgun" || Cmd == "ungrenade" || Cmd == "unlaser" || Cmd == "unrifle" || Cmd == "weapons" || Cmd == "unweapons")
	{
		if(Cmd == "shotgun")
			GiveWeapon(WEAPON_SHOTGUN, false);
		else if(Cmd == "grenade")
			GiveWeapon(WEAPON_GRENADE, false);
		else if(Cmd == "laser" || Cmd == "rifle")
			GiveWeapon(WEAPON_LASER, false);
		else if(Cmd == "unshotgun")
			GiveWeapon(WEAPON_SHOTGUN, true);
		else if(Cmd == "ungrenade")
			GiveWeapon(WEAPON_GRENADE, true);
		else if(Cmd == "unlaser" || Cmd == "unrifle")
			GiveWeapon(WEAPON_LASER, true);
		else if(Cmd == "weapons")
			GiveWeapon(-1, false);
		else if(Cmd == "unweapons")
			GiveWeapon(-1, true);
		EnsureActiveWeaponIsValid();
		WeaponsMutated = true;
		return true;
	}

	if(Cmd == "addweapon" || Cmd == "removeweapon")
	{
		if(vArgs.size() < 2)
		{
			EchoPractice("usage: /%s <weapon-id>", Cmd.c_str());
			return true;
		}
		int WeaponId = 0;
		if(!str_toint(vArgs[1].c_str(), &WeaponId) || WeaponId != ClampWeaponId(WeaponId))
		{
			EchoPractice("invalid weapon id");
			return true;
		}
		GiveWeapon(WeaponId, Cmd == "removeweapon");
		EnsureActiveWeaponIsValid();
		WeaponsMutated = true;
		return true;
	}

	if(Cmd == "jetpack" || Cmd == "unjetpack" || Cmd == "infjump" || Cmd == "uninfjump" || Cmd == "endless" || Cmd == "unendless" || Cmd == "setjumps")
	{
		CCharacterCore Core = pChar->GetCore();
		if(Cmd == "jetpack")
		{
			Core.m_Jetpack = true;
		}
		else if(Cmd == "unjetpack")
		{
			Core.m_Jetpack = false;
		}
		else if(Cmd == "infjump")
		{
			Core.m_EndlessJump = true;
			State.m_InvincibleAddedEndlessJump = false;
		}
		else if(Cmd == "uninfjump")
		{
			Core.m_EndlessJump = false;
			State.m_InvincibleAddedEndlessJump = false;
		}
		else if(Cmd == "endless")
		{
			Core.m_EndlessHook = true;
		}
		else if(Cmd == "unendless")
		{
			Core.m_EndlessHook = false;
		}
		else if(Cmd == "setjumps")
		{
			if(vArgs.size() < 2)
			{
				EchoPractice("usage: /setjumps <count>");
				return true;
			}
			int Jumps = 0;
			if(!str_toint(vArgs[1].c_str(), &Jumps))
			{
				EchoPractice("invalid jumps value");
				return true;
			}
			Core.m_Jumps = Jumps;
			Core.m_Jumped = 0;
			Core.m_JumpedTotal = 0;
		}
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "ninja" || Cmd == "unninja")
	{
		if(Cmd == "ninja")
			pChar->GiveNinja();
		else
			pChar->RemoveNinja();
		return true;
	}

	if(Cmd == "setswitch")
	{
		// 对齐服务器 ConSetSwitch：/setswitch <编号> [0|1] [秒]。省略状态时翻转，
		// 省略秒数时保持到世界结束。开关状态只写练习分支世界，服务器快照不会覆盖。
		int Switch = 0;
		if(vArgs.size() < 2 || vArgs.size() > 4 || !str_toint(vArgs[1].c_str(), &Switch))
		{
			EchoPractice("usage: /setswitch <switch> [0|1] [seconds]");
			return true;
		}
		auto &vSwitchers = m_PracticeBaseWorld.Switchers();
		// 服务器用 in_range(Switch, Switchers().size() - 1)，最高编号同样设不了，
		// 这里保持同款边界，避免练习里能设、连回服务器却设不了。
		if(Switch < 0 || Switch >= (int)vSwitchers.size() - 1)
		{
			EchoPractice("invalid switch id");
			return true;
		}

		const int Team = std::clamp(pChar->Team(), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
		bool NewState = !vSwitchers[Switch].m_aStatus[Team];
		if(vArgs.size() > 2)
		{
			int StateValue = 0;
			str_toint(vArgs[2].c_str(), &StateValue);
			NewState = StateValue != 0;
		}
		int Seconds = 0;
		if(vArgs.size() > 3)
			str_toint(vArgs[3].c_str(), &Seconds);
		const int EndTick = vArgs.size() > 3 ? m_PracticeBaseWorld.GameTick() + 1 + Seconds * Client()->GameTickSpeed() : 0;

		vSwitchers[Switch].m_aStatus[Team] = NewState;
		vSwitchers[Switch].m_aEndTick[Team] = EndTick;
		vSwitchers[Switch].m_aType[Team] = NewState ? (EndTick ? TILE_SWITCHTIMEDOPEN : TILE_SWITCHOPEN) : (EndTick ? TILE_SWITCHTIMEDCLOSE : TILE_SWITCHCLOSE);
		return true;
	}

	if(Cmd == "invincible" || Cmd == "invisbl" || Cmd == "invinsbl")
	{
		bool Invincible = false;
		CCharacterCore Core = pChar->GetCore();
		if(vArgs.size() > 1)
		{
			int Value = 0;
			if(!str_toint(vArgs[1].c_str(), &Value))
			{
				EchoPractice("invalid value, use 0 or 1");
				return true;
			}
			Invincible = Value != 0;
		}
		else
		{
			Invincible = !Core.m_Invincible;
		}

		if(Invincible)
		{
			pChar->SetSuper(false);
			Core = pChar->GetCore();
			Core.m_DeepFrozen = false;
			Core.m_LiveFrozen = false;
			Core.m_IsInFreeze = false;
			Core.m_FreezeEnd = 0;
			if(!Core.m_EndlessJump)
			{
				Core.m_EndlessJump = true;
				State.m_InvincibleAddedEndlessJump = true;
			}
			else
			{
				State.m_InvincibleAddedEndlessJump = false;
			}
			pChar->SetCore(Core);
			pChar->Unfreeze();
			pChar->m_FreezeTime = 0;
		}

		Core = pChar->GetCore();
		if(!Invincible && State.m_InvincibleAddedEndlessJump)
		{
			Core.m_EndlessJump = false;
			State.m_InvincibleAddedEndlessJump = false;
		}
		Core.m_Invincible = Invincible;
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "collision")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_CollisionDisabled = !Core.m_CollisionDisabled;
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "hookcollision")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_HookHitDisabled = !Core.m_HookHitDisabled;
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "hitothers")
	{
		if(vArgs.size() <= 1 || str_comp_nocase(vArgs[1].c_str(), "all") == 0)
		{
			CCharacterCore Core = pChar->GetCore();
			const bool IsDisabled = Core.m_HammerHitDisabled && Core.m_ShotgunHitDisabled && Core.m_GrenadeHitDisabled && Core.m_LaserHitDisabled;
			Core.m_HammerHitDisabled = !IsDisabled;
			Core.m_ShotgunHitDisabled = !IsDisabled;
			Core.m_GrenadeHitDisabled = !IsDisabled;
			Core.m_LaserHitDisabled = !IsDisabled;
			pChar->SetCore(Core);
			return true;
		}

		std::string Arg = vArgs[1];
		std::transform(Arg.begin(), Arg.end(), Arg.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if(Arg == "hammer")
			ToggleHit(WEAPON_HAMMER);
		else if(Arg == "shotgun")
			ToggleHit(WEAPON_SHOTGUN);
		else if(Arg == "grenade")
			ToggleHit(WEAPON_GRENADE);
		else if(Arg == "laser")
			ToggleHit(WEAPON_LASER);
		else
			EchoPractice("unknown argument for /hitothers");
		return true;
	}

	return false;
}

bool CFastPractice::ConsumePracticeChatCommand(int Team, const char *pLine)
{
	if(!m_Enabled || !pLine || pLine[0] != '/')
		return false;

	std::vector<std::string> vArgs;
	if(!ParseCommandArgs(pLine, vArgs))
		return false;

	if(!vArgs.empty() && str_comp_nocase(vArgs[0].c_str(), "/restart") == 0)
	{
		// 重开会替换整个世界（连同角色），不能继续使用下面缓存的 pBaseChar，
		// 所以放在解析角色之前处理。
		RestartPracticeFromStart();
		return true;
	}

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return true;
	}

	CCharacter *pBaseChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
	if(!pBaseChar)
	{
		if(!InitPracticeWorld())
		{
			Disable();
			return true;
		}
		pBaseChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
		if(!pBaseChar)
		{
			Disable();
			return true;
		}
	}

	bool WeaponsMutated = false;
	const bool Consumed = ExecutePracticeCommand(Team, LocalClientId, pBaseChar, vArgs, WeaponsMutated);
	if(!Consumed)
		return false;
	if(!m_Enabled)
		return true;
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);

	if(WeaponsMutated)
	{
		NormalizeWeaponSelectionInput(pBaseChar);
		if(m_RequireDummy && DummyClientId >= 0)
			NormalizeWeaponSelectionInput(m_PracticeBaseWorld.GetCharacterById(DummyClientId));
	}

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && DummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId))
		{
			GameClient()->m_aClients[DummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[DummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();

	if(m_RequireDummy && DummyClientId >= 0 && !m_PracticeBaseWorld.GetCharacterById(DummyClientId))
	{
		Disable();
		return true;
	}

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetAttackTickHistory();
	return true;
}

void CFastPractice::ConPracticeEnter(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->Enable();
}

void CFastPractice::ConPracticeExit(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->Disable();
}

void CFastPractice::ConPracticeRestart(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->RestartPracticeFromStart();
}

void CFastPractice::ConPracticeSetCheckpoint(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->SetPracticeCheckpoint();
}

void CFastPractice::ConPracticeRestoreCheckpoint(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->RestorePracticeCheckpoint();
}

void CFastPractice::ConPracticeRecordStart(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->StartPracticeRecording();
}

void CFastPractice::ConPracticeRecordPause(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->PausePracticeRecording();
}

void CFastPractice::ConPracticeRecordStop(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->StopPracticeRecording();
}

void CFastPractice::ConPracticeViewGhost(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	if(!pSelf->GameClient()->m_RankGhost.ViewPracticeGroup(pResult->GetString(0)))
		pSelf->EchoPractice("could not play practice ghost group '%s'", pResult->GetString(0));
}

void CFastPractice::ConLocalWorldDump(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	const auto Print = [pSelf](const char *pText) {
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "local_world", pText);
	};
	char aBuf[512];
	const CLocalWorld &Local = pSelf->GameClient()->m_LocalWorld;

	str_format(aBuf, sizeof(aBuf), "practice=%d branch_tick=%d local_id=%d dummy_id=%d",
		pSelf->m_Enabled ? 1 : 0, pSelf->m_PracticeBaseWorld.GameTick(), pSelf->m_EnableLocalClientId, pSelf->m_EnableDummyClientId);
	Print(aBuf);
	str_format(aBuf, sizeof(aBuf), "track: active=%d start=%d frames=%d events=%d switches=%d completed=%d",
		Local.TrackActive() ? 1 : 0, Local.TrackStartTick(), Local.CurrentFrameCount(),
		Local.CurrentEventCount(), Local.CurrentSwitchStateCount(), Local.CompletedFrameCount());
	Print(aBuf);
	str_format(aBuf, sizeof(aBuf), "view: session=%d slots=%d manual=%d playback_tick=%d",
		Local.ViewSessionActive() ? 1 : 0, Local.ViewSessionSlotCount(),
		pSelf->GameClient()->m_Ghost.ManualModeActive() ? 1 : 0,
		pSelf->GameClient()->m_Ghost.ManualPlaybackTick());
	Print(aBuf);

	if(!pSelf->m_PracticeBaseWorld.Switchers().empty())
	{
		const int Team = std::clamp(pSelf->GameClient()->SwitchStateTeam(), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
		const SLocalSwitchState State = QmEncodeLocalSwitchState(0, pSelf->m_PracticeBaseWorld.Switchers(), Team);
		str_format(aBuf, sizeof(aBuf), "switchers: count=%d team=%d bits0_31=0x%08x bits32_63=0x%08x",
			(int)pSelf->m_PracticeBaseWorld.Switchers().size(), Team, State.m_aStatus[0], State.m_aStatus[1]);
		Print(aBuf);
	}
	// 本地 Tee 的冻结状态：/r 之后人能动但渲染还画冻结，就是这里的
	// FreezeEnd 没从救援快照重算，直接看这一行比截图可靠。
	// 位置、速度与跳跃位用来判断“按键没进分支世界”还是“进了但物理被状态挡住”：
	// grounded 为 0 且 jumped 已置满时，落地判定的分支世界有问题。
	CCharacter *pLocal = pSelf->m_Enabled ? pSelf->m_PracticeBaseWorld.GetCharacterById(pSelf->m_EnableLocalClientId) : nullptr;
	if(pLocal)
	{
		const CCharacterCore *pCore = pLocal->Core();
		str_format(aBuf, sizeof(aBuf), "local_tee: present=1 deep=%d freeze_end=%d freeze_time=%d grounded=%d pos=%d,%d vel=%d,%d jumped=%d jumps=%d hook_state=%d",
			pCore->m_DeepFrozen ? 1 : 0, pCore->m_FreezeEnd, pLocal->m_FreezeTime, pLocal->IsGrounded() ? 1 : 0,
			round_to_int(pCore->m_Pos.x), round_to_int(pCore->m_Pos.y),
			round_to_int(pCore->m_Vel.x), round_to_int(pCore->m_Vel.y),
			pCore->m_Jumped, pCore->m_Jumps, pCore->m_HookState);
	}
	else
	{
		str_copy(aBuf, "local_tee: present=0 deep=0 freeze_end=0 freeze_time=0 grounded=0 pos=0,0 vel=0,0 jumped=0 jumps=0 hook_state=0");
	}
	Print(aBuf);
	// 时钟对照：分支世界 tick、服务器游戏 tick 与预测 tick。分支世界如果落后预测 tick，
	// 它消费的输入就是旧的，表现为按键延迟好几帧才生效。
	{
		const int GameTick = pSelf->Client()->GameTick(g_Config.m_ClDummy);
		const int PredTick = pSelf->Client()->PredGameTick(g_Config.m_ClDummy);
		str_format(aBuf, sizeof(aBuf), "ticks: game=%d pred=%d branch=%d",
			GameTick, PredTick, pSelf->m_PracticeBaseWorld.GameTick());
		Print(aBuf);
		const CNetObj_PlayerInput *pPredInput = (const CNetObj_PlayerInput *)pSelf->Client()->GetInput(PredTick, 0);
		str_format(aBuf, sizeof(aBuf), "pred_input: present=%d direction=%d jump=%d hook=%d",
			pPredInput ? 1 : 0,
			pPredInput ? pPredInput->m_Direction : 0,
			pPredInput ? pPredInput->m_Jump : 0,
			pPredInput ? pPredInput->m_Hook : 0);
		Print(aBuf);
	}
	// 实时控制层输入：与分支角色最后消费的输入对照，可以看出输入过滤是否吞掉跳跃/钩子，
	// 或者按键根本没进控制层。
	{
		const CNetObj_PlayerInput &Live = pSelf->GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		str_format(aBuf, sizeof(aBuf), "live_input: direction=%d jump=%d hook=%d fire=%d target=%d,%d flags=0x%x",
			Live.m_Direction, Live.m_Jump, Live.m_Hook, Live.m_Fire, Live.m_TargetX, Live.m_TargetY, Live.m_PlayerFlags);
		Print(aBuf);
	}
	// 分支世界角色最后消费的输入：与实时按键对照，可以看出输入过滤是否吞掉跳跃/钩子。
	if(pLocal)
	{
		const CNetObj_PlayerInput *pInput = pLocal->LatestInput();
		str_format(aBuf, sizeof(aBuf), "local_input: direction=%d jump=%d hook=%d fire=%d target=%d,%d flags=0x%x",
			pInput->m_Direction, pInput->m_Jump, pInput->m_Hook, pInput->m_Fire, pInput->m_TargetX, pInput->m_TargetY, pInput->m_PlayerFlags);
	}
	else
	{
		str_copy(aBuf, "local_input: direction=0 jump=0 hook=0 fire=0 target=0,0 flags=0x0");
	}
	Print(aBuf);
	int Characters = 0;
	for(int Cid = 0; Cid < MAX_CLIENTS; Cid++)
		if(pSelf->GameClient()->m_PredictedWorld.GetCharacterById(Cid))
			Characters++;
	str_format(aBuf, sizeof(aBuf), "predicted_world: characters=%d pending_events=%d",
		Characters, (int)pSelf->GameClient()->m_PredictedWorld.m_PredictedEvents.size());
	Print(aBuf);
}

void CFastPractice::OnConsoleInit()
{
	Console()->Register("fast_practice_toggle", "", CFGFLAG_CLIENT, ConFastPracticeToggle, this, "Toggle fast practice mode");
	// 控制页可绑定的练习按键。全部刻意不设默认键，只提供命令。
	Console()->Register("fast_practice_enter", "", CFGFLAG_CLIENT, ConPracticeEnter, this, "Enable fast practice mode");
	Console()->Register("fast_practice_exit", "", CFGFLAG_CLIENT, ConPracticeExit, this, "Disable fast practice mode");
	Console()->Register("practice_restart", "", CFGFLAG_CLIENT, ConPracticeRestart, this,
		"Restore the practice world to the state it had when fast practice was enabled");
	Console()->Register("practice_set_checkpoint", "", CFGFLAG_CLIENT, ConPracticeSetCheckpoint, this,
		"Store the current practice tee state as the respawn point");
	Console()->Register("practice_restore_checkpoint", "", CFGFLAG_CLIENT, ConPracticeRestoreCheckpoint, this,
		"Teleport back to the practice respawn point");
	Console()->Register("practice_record_start", "", CFGFLAG_CLIENT, ConPracticeRecordStart, this,
		"Start or resume recording the practice line");
	Console()->Register("practice_record_pause", "", CFGFLAG_CLIENT, ConPracticeRecordPause, this,
		"Pause recording the practice line (the recorded part is kept)");
	Console()->Register("practice_record_stop", "", CFGFLAG_CLIENT, ConPracticeRecordStop, this,
		"Stop recording and save the practice line as a ghost group");
	Console()->Register("practice_view_ghost", "r[group]", CFGFLAG_CLIENT, ConPracticeViewGhost, this,
		"Replay a saved practice ghost group in demo-style view mode");
	Console()->Register("qm_local_world_dump", "", CFGFLAG_CLIENT, ConLocalWorldDump, this,
		"Dump the client-side world state (practice branch, tracks, switchers, view session)");
}
