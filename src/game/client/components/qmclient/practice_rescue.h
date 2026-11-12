#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_RESCUE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_RESCUE_H

#include <base/vmath.h>

#include <game/gamecore.h>

// 快速练习 /r 的判定规则，逐条对齐服务器 CCharacter::TrySetRescue 与 Rescue。
// 救援点只在下面四个条件同时成立时刷新；这里不看 m_FreezeTime——被普通冻结但已经
// 落在平台上的情况，服务器同样会刷新救援点，于是 /r 的结果是就地解冻。
inline bool QmPracticeCanSetRescuePoint(bool InFreeze, bool DeepFrozen, bool Grounded, bool InFreezePickup)
{
	return !InFreeze && !DeepFrozen && Grounded && !InFreezePickup;
}

struct SQmPracticeRescueDecision
{
	bool m_SavePoint = false;
	bool m_Teleport = false;
};

// 服务器 ConRescue：manual 模式下 /r 先尝试就地设点，设点成功就完全不传送；
// 设点失败才退化成 Rescue()。auto 模式直接 Rescue()。
// Rescue() 在 super/invincible 时直接拒绝，所以这两个状态只拦住传送，不影响设点。
inline SQmPracticeRescueDecision QmEvaluatePracticeRescue(bool ManualMode, bool CanSetNow, bool HasSavedPoint, bool TeleportBlocked)
{
	SQmPracticeRescueDecision Decision;
	if(ManualMode && CanSetNow)
	{
		Decision.m_SavePoint = true;
		return Decision;
	}
	Decision.m_Teleport = HasSavedPoint && !TeleportBlocked;
	return Decision;
}

// 救援点保存的 tee 状态，对应服务器 CSaveTee 里客户端侧存在的字段。
// 服务器 Rescue() 在载入后还会强制清空速度与钩子，因此这两项不在这里保存。
// 冻结开始与忍者激活的 tick 按服务器做法存成相对值：载入时换算回绝对 tick，
// 保证剩余时长不随保存与使用之间的时间差缩短。
struct SQmPracticeSavedTee
{
	bool m_Valid = false;

	struct SQmPracticeSavedWeapon
	{
		int m_AmmoRegenStart = 0;
		int m_Ammocost = 0;
		bool m_Got = false;
	};

	vec2 m_Pos = vec2(0.0f, 0.0f);
	int m_ActiveWeapon = WEAPON_GUN;
	SQmPracticeSavedWeapon m_aWeapons[NUM_WEAPONS];

	int m_Jumped = 0;
	int m_JumpedTotal = 0;
	int m_Jumps = 0;
	bool m_EndlessJump = false;
	bool m_EndlessHook = false;
	bool m_Jetpack = false;

	bool m_CollisionDisabled = false;
	bool m_HookHitDisabled = false;
	bool m_HammerHitDisabled = false;
	bool m_ShotgunHitDisabled = false;
	bool m_GrenadeHitDisabled = false;
	bool m_LaserHitDisabled = false;
	bool m_HasTelegunGun = false;
	bool m_HasTelegunGrenade = false;
	bool m_HasTelegunLaser = false;
	bool m_Solo = false;

	int m_FreezeStartRelative = 0;
	bool m_DeepFrozen = false;
	bool m_LiveFrozen = false;

	vec2 m_NinjaActivationDir = vec2(0.0f, 0.0f);
	int m_NinjaActivationTickRelative = 0;
	int m_NinjaCurrentMoveTime = 0;
	int m_NinjaOldVelAmount = 0;

	// CCharacter 侧字段（CCharacterCore 之外）
	int m_FreezeTime = 0;
	int m_ReloadTimer = 0;
	int m_LastWeapon = WEAPON_HAMMER;
	int m_QueuedWeapon = -1;
	bool m_NinjaJetpack = false;
	int m_TuneZone = 0;
};

inline void QmCapturePracticeRescueCore(const CCharacterCore &Core, int NowTick, SQmPracticeSavedTee &Tee)
{
	Tee.m_Valid = true;
	Tee.m_Pos = Core.m_Pos;
	Tee.m_ActiveWeapon = Core.m_ActiveWeapon;
	for(int i = 0; i < NUM_WEAPONS; i++)
	{
		Tee.m_aWeapons[i].m_AmmoRegenStart = Core.m_aWeapons[i].m_AmmoRegenStart;
		Tee.m_aWeapons[i].m_Ammocost = Core.m_aWeapons[i].m_Ammocost;
		Tee.m_aWeapons[i].m_Got = Core.m_aWeapons[i].m_Got;
	}

	Tee.m_Jumped = Core.m_Jumped;
	Tee.m_JumpedTotal = Core.m_JumpedTotal;
	Tee.m_Jumps = Core.m_Jumps;
	Tee.m_EndlessJump = Core.m_EndlessJump;
	Tee.m_EndlessHook = Core.m_EndlessHook;
	Tee.m_Jetpack = Core.m_Jetpack;

	Tee.m_CollisionDisabled = Core.m_CollisionDisabled;
	Tee.m_HookHitDisabled = Core.m_HookHitDisabled;
	Tee.m_HammerHitDisabled = Core.m_HammerHitDisabled;
	Tee.m_ShotgunHitDisabled = Core.m_ShotgunHitDisabled;
	Tee.m_GrenadeHitDisabled = Core.m_GrenadeHitDisabled;
	Tee.m_LaserHitDisabled = Core.m_LaserHitDisabled;
	Tee.m_HasTelegunGun = Core.m_HasTelegunGun;
	Tee.m_HasTelegunGrenade = Core.m_HasTelegunGrenade;
	Tee.m_HasTelegunLaser = Core.m_HasTelegunLaser;
	Tee.m_Solo = Core.m_Solo;

	Tee.m_FreezeStartRelative = Core.m_FreezeStart ? NowTick - Core.m_FreezeStart : 0;
	Tee.m_DeepFrozen = Core.m_DeepFrozen;
	Tee.m_LiveFrozen = Core.m_LiveFrozen;

	Tee.m_NinjaActivationDir = Core.m_Ninja.m_ActivationDir;
	Tee.m_NinjaActivationTickRelative = Core.m_Ninja.m_ActivationTick ? NowTick - Core.m_Ninja.m_ActivationTick : 0;
	Tee.m_NinjaCurrentMoveTime = Core.m_Ninja.m_CurrentMoveTime;
	Tee.m_NinjaOldVelAmount = Core.m_Ninja.m_OldVelAmount;
}

inline void QmApplyPracticeRescueCore(CCharacterCore &Core, int NowTick, const SQmPracticeSavedTee &Tee)
{
	if(!Tee.m_Valid)
		return;

	Core.m_Pos = Tee.m_Pos;
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_ActiveWeapon = Tee.m_ActiveWeapon;
	for(int i = 0; i < NUM_WEAPONS; i++)
	{
		Core.m_aWeapons[i].m_AmmoRegenStart = Tee.m_aWeapons[i].m_AmmoRegenStart;
		Core.m_aWeapons[i].m_Ammocost = Tee.m_aWeapons[i].m_Ammocost;
		Core.m_aWeapons[i].m_Got = Tee.m_aWeapons[i].m_Got;
		// 服务器载入救援点时不恢复弹药数量（ddnet #2086），统一置 -1。
		Core.m_aWeapons[i].m_Ammo = -1;
	}

	Core.m_Jumped = Tee.m_Jumped;
	Core.m_JumpedTotal = Tee.m_JumpedTotal;
	Core.m_Jumps = Tee.m_Jumps;
	Core.m_EndlessJump = Tee.m_EndlessJump;
	Core.m_EndlessHook = Tee.m_EndlessHook;
	Core.m_Jetpack = Tee.m_Jetpack;

	Core.m_CollisionDisabled = Tee.m_CollisionDisabled;
	Core.m_HookHitDisabled = Tee.m_HookHitDisabled;
	Core.m_HammerHitDisabled = Tee.m_HammerHitDisabled;
	Core.m_ShotgunHitDisabled = Tee.m_ShotgunHitDisabled;
	Core.m_GrenadeHitDisabled = Tee.m_GrenadeHitDisabled;
	Core.m_LaserHitDisabled = Tee.m_LaserHitDisabled;
	Core.m_HasTelegunGun = Tee.m_HasTelegunGun;
	Core.m_HasTelegunGrenade = Tee.m_HasTelegunGrenade;
	Core.m_HasTelegunLaser = Tee.m_HasTelegunLaser;
	Core.m_Solo = Tee.m_Solo;

	Core.m_FreezeStart = Tee.m_FreezeStartRelative ? NowTick - Tee.m_FreezeStartRelative : 0;
	Core.m_DeepFrozen = Tee.m_DeepFrozen;
	Core.m_LiveFrozen = Tee.m_LiveFrozen;

	Core.m_Ninja.m_ActivationDir = Tee.m_NinjaActivationDir;
	Core.m_Ninja.m_ActivationTick = Tee.m_NinjaActivationTickRelative ? NowTick - Tee.m_NinjaActivationTickRelative : 0;
	Core.m_Ninja.m_CurrentMoveTime = Tee.m_NinjaCurrentMoveTime;
	Core.m_Ninja.m_OldVelAmount = Tee.m_NinjaOldVelAmount;

	// Rescue() 载入后强制恢复为无钩、静止，并且不恢复被保存时的事件队列，
	// 否则回放旧事件会重复播放音效/特效。
	Core.m_HookState = HOOK_IDLE;
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	Core.m_HookTeleBase = vec2(0.0f, 0.0f);
	Core.m_HookTick = 0;
	Core.m_NewHook = false;
	Core.SetHookedPlayer(-1);
	Core.m_AttachedPlayers.clear();
	Core.m_TriggeredEvents = 0;
}

#endif
