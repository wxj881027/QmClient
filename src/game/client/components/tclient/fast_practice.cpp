/* Copyright © 2026 BestProject Team */
#include "fast_practice.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/ghost.h>
#include <game/client/components/particles.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/practice_cursor.h>
#include <game/client/components/qmclient/practice_rescue.h>
#include <game/client/components/qmclient/world_event_gate.h>
#include <game/client/gameclient.h>
#include <game/client/laser_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/client/prediction/entities/plasma.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <limits>

namespace
{
	void NeutralizeInput(CNetObj_PlayerInput &Input)
	{
		Input.m_Direction = 0;
		Input.m_Jump = 0;
		Input.m_Hook = 0;

		if((Input.m_Fire & 1) != 0)
			Input.m_Fire++;
		Input.m_Fire &= INPUT_STATE_MASK;

		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
		Input.m_WantedWeapon = 0;

		if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
		{
			Input.m_TargetX = 1;
			Input.m_TargetY = 0;
		}
	}

	int ReleasedFireState(int FireState)
	{
		FireState &= INPUT_STATE_MASK;
		if((FireState & 1) != 0)
			FireState = (FireState + 1) & INPUT_STATE_MASK;
		return FireState;
	}

	void SuppressActionInput(CNetObj_PlayerInput &Input, int FireState)
	{
		Input.m_Fire = FireState & INPUT_STATE_MASK;
		Input.m_WantedWeapon = 0;
		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
	}

	float EffectiveFastInputOffsetTicks(const CGameClient *pGameClient)
	{
		SQmFastInputSettings Settings;
		Settings.m_Enabled = pGameClient->TClientComponent().IsFastInputActive();
		Settings.m_FastAmountMs = g_Config.m_QmFastInputAmount;
		return QmEffectiveFastInputOffsetTicks(Settings);
	}

	int FastInputPredictionTicks(float OffsetTicks)
	{
		return QmFastInputPredictionTicks(OffsetTicks);
	}

	bool EffectiveFastInputOthers(const CGameClient *pGameClient)
	{
		return QmEffectiveFastInputOthers(pGameClient->TClientComponent().IsFastInputActive(), g_Config.m_QmFastInputOthers != 0);
	}

	bool IsFrozenState(const CCharacter *pChar)
	{
		if(!pChar)
			return false;
		const CCharacterCore &Core = *pChar->Core();
		return pChar->m_FreezeTime > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_FreezeEnd != 0;
	}

	int ClampWeaponId(int WeaponId)
	{
		return std::clamp(WeaponId, -1, NUM_WEAPONS - 1);
	}

	struct STrackedProjectile
	{
		int m_Owner = -1;
		int m_StartTick = 0;
		int m_Type = WEAPON_GUN;
		int m_TuneZone = 0;
		bool m_Explosive = false;
		vec2 m_StartPos = vec2(0.0f, 0.0f);
		vec2 m_StartVel = vec2(0.0f, 0.0f);
	};

	bool SameProjectile(const STrackedProjectile &A, const STrackedProjectile &B)
	{
		return A.m_Owner == B.m_Owner &&
		       A.m_StartTick == B.m_StartTick &&
		       A.m_Type == B.m_Type &&
		       A.m_TuneZone == B.m_TuneZone &&
		       A.m_Explosive == B.m_Explosive &&
		       distance(A.m_StartPos, B.m_StartPos) < 0.01f &&
		       distance(A.m_StartVel, B.m_StartVel) < 0.01f;
	}

	bool IsTrackedProjectile(const CProjectileData &Data, int LocalClientId, int DummyClientId)
	{
		const bool PracticeOwned = Data.m_Owner == LocalClientId || (DummyClientId >= 0 && Data.m_Owner == DummyClientId);
		return PracticeOwned && (Data.m_Explosive || Data.m_Type == WEAPON_GRENADE || Data.m_Type == WEAPON_GUN || Data.m_Type == WEAPON_SHOTGUN);
	}

	bool IsTrackedProjectileExplosive(const STrackedProjectile &Proj)
	{
		return Proj.m_Explosive || Proj.m_Type == WEAPON_GRENADE;
	}

	void AddProjectileHitEffect(CGameClient *pGameClient, vec2 Pos, vec2 Direction)
	{
		if(!pGameClient || pGameClient->m_SuppressEvents)
			return;

		CParticle Particle;
		Particle.SetDefault();
		Particle.m_Spr = SPRITE_PART_HIT01;
		Particle.m_Pos = Pos;
		Particle.m_LifeSpan = 0.18f;
		Particle.m_StartSize = 56.0f;
		Particle.m_EndSize = 0.0f;
		Particle.m_Rot = length(Direction) > 0.001f ? angle(Direction) + pi : random_angle();
		Particle.m_Color.a = 0.85f;
		Particle.m_StartAlpha = 0.85f;
		Particle.m_FlowAffected = 0.0f;
		Particle.m_Collides = false;
		pGameClient->m_Particles.Add(CParticles::GROUP_EXPLOSIONS, &Particle);
	}

	bool IsDeathTileAt(CCollision *pCollision, const vec2 &Pos)
	{
		if(!pCollision)
			return false;

		const int X = round_to_int(Pos.x);
		const int Y = round_to_int(Pos.y);
		return pCollision->GetCollisionAt(X, Y) == TILE_DEATH ||
		       pCollision->GetFrontCollisionAt(X, Y) == TILE_DEATH;
	}

	bool IsCharacterTouchingDeathTile(CCollision *pCollision, const vec2 &Pos, float ProximityRadius)
	{
		const float Offset = ProximityRadius / 3.0f;
		return IsDeathTileAt(pCollision, Pos) ||
		       IsDeathTileAt(pCollision, Pos + vec2(Offset, -Offset)) ||
		       IsDeathTileAt(pCollision, Pos + vec2(Offset, Offset)) ||
		       IsDeathTileAt(pCollision, Pos + vec2(-Offset, -Offset)) ||
		       IsDeathTileAt(pCollision, Pos + vec2(-Offset, Offset));
	}

	bool PracticePathContainsTeleport(CCollision *pCollision, const vec2 &From, const vec2 &To)
	{
		if(!pCollision)
			return false;

		const std::vector<int> vIndices = pCollision->GetMapIndices(From, To);
		for(int Index : vIndices)
		{
			if(pCollision->IsTeleport(Index) > 0 ||
				pCollision->IsEvilTeleport(Index) > 0 ||
				pCollision->IsCheckTeleport(Index) ||
				pCollision->IsCheckEvilTeleport(Index))
			{
				return true;
			}
		}
		return false;
	}

	void CollectTrackedProjectiles(CGameWorld &World, int LocalClientId, int DummyClientId, std::vector<STrackedProjectile> &vOut)
	{
		vOut.clear();
		for(auto *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
		{
			const CProjectileData Data = pProj->GetData();
			if(!IsTrackedProjectile(Data, LocalClientId, DummyClientId))
				continue;

			STrackedProjectile Proj;
			Proj.m_Owner = Data.m_Owner;
			Proj.m_StartTick = Data.m_StartTick;
			Proj.m_Type = Data.m_Type;
			Proj.m_TuneZone = Data.m_TuneZone;
			Proj.m_Explosive = Data.m_Explosive;
			Proj.m_StartPos = Data.m_StartPos;
			Proj.m_StartVel = Data.m_StartVel;
			vOut.push_back(Proj);
		}
	}

	vec2 CalcTrackedProjectilePos(const STrackedProjectile &Proj, int Tick, int TickSpeed, const CTuningParams *pTuning)
	{
		float Curvature = 0.0f;
		float Speed = 0.0f;
		if(Proj.m_Type == WEAPON_GRENADE)
		{
			Curvature = pTuning->m_GrenadeCurvature;
			Speed = pTuning->m_GrenadeSpeed;
		}
		else if(Proj.m_Type == WEAPON_SHOTGUN)
		{
			Curvature = pTuning->m_ShotgunCurvature;
			Speed = pTuning->m_ShotgunSpeed;
		}
		else
		{
			Curvature = pTuning->m_GunCurvature;
			Speed = pTuning->m_GunSpeed;
		}

		const float Ct = std::max(0.0f, (Tick - Proj.m_StartTick) / (float)TickSpeed);
		return CalcPos(Proj.m_StartPos, Proj.m_StartVel, Curvature, Speed, Ct);
	}
} // namespace

void CFastPractice::ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->Toggle();
}

void CFastPractice::ResetPracticeState()
{
	m_Enabled = false;
	m_RequireDummy = false;
	m_EnableLocalClientId = -1;
	m_EnableDummyClientId = -1;
	m_PracticeWorldInitialized = false;
	m_HasDummyAnchor = false;
	m_RecordingPaused = false;
	m_SuppressFireOnNextPredictTick = false;
	m_InputSuppressTicks = 0;
	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_LastResolvedLocalInputConn = -1;
	m_LastResolvedDummyInputConn = -1;
	m_aHasServerLockedInputs.fill(false);
	m_aServerLockedInputs = {};
	m_aPostPracticeInputSuppressTicks.fill(0);
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_aPracticeInputFilterStates = {};
	m_PracticeBaseWorld.Clear();
	// 退出练习即销毁分支，重开快照随分支一起丢弃；下次进入重新采样。
	m_PracticeStartWorld.Clear();
	m_HasPracticeStartSnapshot = false;
	ResetAttackTickHistory();
	ResetCommandState();
}

void CFastPractice::ResetCommandState()
{
	for(auto &State : m_aPracticeCommandState)
		State = {};
}

void CFastPractice::OnReset()
{
	ResetPracticeState();
}

void CFastPractice::OnMapLoad()
{
	ResetPracticeState();
}

void CFastPractice::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING || NewState == IClient::STATE_LOADING)
	{
		InvalidateBufferedInputState();
		ResetPracticeState();
	}
}

bool CFastPractice::CanEnable() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	if(!GameClient()->m_Snap.m_pLocalInfo || !GameClient()->m_Snap.m_pLocalCharacter)
		return false;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS)
		return false;

	if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	if(!GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active || GameClient()->m_aClients[LocalClientId].m_Paused)
		return false;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS)
	{
		if(!GameClient()->m_Snap.m_aCharacters[ActiveClientId].m_Active || GameClient()->m_aClients[ActiveClientId].m_Paused)
			return false;
	}

	return true;
}

int CFastPractice::DummyClientId() const
{
	if(!Client()->DummyConnected())
		return -1;

	const int DummySlot = !g_Config.m_ClDummy;
	const int DummyId = GameClient()->m_aLocalIds[DummySlot];
	if(DummyId < 0 || DummyId >= MAX_CLIENTS)
		return -1;

	if(!GameClient()->m_Snap.m_aCharacters[DummyId].m_Active || GameClient()->m_aClients[DummyId].m_Paused)
		return -1;

	return DummyId;
}

bool CFastPractice::ResolveParticipantInputs(int LocalClientId, int DummyClientId, int &LocalInputConn, int &DummyInputConn) const
{
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	// Client()->GetInput(Tick, IsDummy) expects a local slot, not a raw connection id:
	// 0 = currently controlled player, 1 = the other one.
	LocalInputConn = 0;
	DummyInputConn = -1;

	if(m_RequireDummy)
	{
		DummyInputConn = 1;
		if(DummyClientId < 0 || DummyClientId >= MAX_CLIENTS || DummyClientId == LocalClientId)
		{
			if(g_Config.m_Debug)
				dbg_msg("fast_practice", "input mapping failed: local_id=%d dummy_id=%d", LocalClientId, DummyClientId);
			return false;
		}
	}

	return true;
}

int CFastPractice::CurrentLocalPracticeId() const
{
	if(!m_Enabled)
		return -1;

	// Use the currently controlled connection so switching cl_dummy swaps control.
	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS &&
		(ActiveClientId == m_EnableLocalClientId || ActiveClientId == m_EnableDummyClientId))
	{
		return ActiveClientId;
	}

	const int InactiveClientId = GameClient()->m_aLocalIds[ActiveConn ^ 1];
	if(InactiveClientId >= 0 && InactiveClientId < MAX_CLIENTS &&
		(InactiveClientId == m_EnableLocalClientId || InactiveClientId == m_EnableDummyClientId))
	{
		return InactiveClientId;
	}

	return -1;
}

bool CFastPractice::ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const
{
	LocalClientId = CurrentLocalPracticeId();
	DummyClientId = -1;

	if(LocalClientId < 0)
	{
		const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
					(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
		if(Spectating)
			LocalClientId = m_EnableLocalClientId;
	}
	if(LocalClientId < 0)
		return false;
	if(LocalClientId != m_EnableLocalClientId && LocalClientId != m_EnableDummyClientId)
		return false;

	if(m_RequireDummy)
		DummyClientId = LocalClientId == m_EnableLocalClientId ? m_EnableDummyClientId : m_EnableLocalClientId;

	return true;
}

bool CFastPractice::IsPracticeParticipant(int ClientId) const
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return ClientId == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && ClientId == m_EnableDummyClientId);
}

int CFastPractice::CurrentPracticeDummyId() const
{
	if(!m_Enabled || !m_RequireDummy)
		return -1;
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	if(Spectating)
		return -1;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	return DummyClientId;
}

int CFastPractice::PracticeDummyId() const
{
	return CurrentPracticeDummyId();
}

bool CFastPractice::IsPracticeDummy(int ClientId) const
{
	return ClientId >= 0 && ClientId == CurrentPracticeDummyId();
}

bool CFastPractice::IsPracticeServerPauseActive() const
{
	if(!m_Enabled)
		return false;
	if(!GameClient()->m_Snap.m_pLocalInfo || GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS)
		return false;

	const auto IsParticipantPaused = [&](int ClientId) {
		return ClientId >= 0 && ClientId < MAX_CLIENTS &&
		       (GameClient()->m_aClients[ClientId].m_Paused || GameClient()->m_aClients[ClientId].m_Spec);
	};
	return IsParticipantPaused(m_EnableLocalClientId) || IsParticipantPaused(m_EnableDummyClientId);
}

bool CFastPractice::ForcePredictWeapons() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return m_Enabled && !Spectating;
}

bool CFastPractice::ForcePredictGrenade() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return m_Enabled && !Spectating;
}

bool CFastPractice::ForcePredictGunfire() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return m_Enabled && !Spectating;
}

bool CFastPractice::ForcePredictPlayers() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return m_Enabled && !Spectating;
}

void CFastPractice::PrunePracticeWorld(CGameWorld &World) const
{
	for(CCharacter *pChar = (CCharacter *)World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), *pCharNext = nullptr; pChar; pChar = pCharNext)
	{
		pCharNext = (CCharacter *)pChar->TypeNext();
		const int ClientId = pChar->GetCid();
		if(ClientId != m_EnableLocalClientId && ClientId != m_EnableDummyClientId)
			pChar->Destroy();
	}

	for(CProjectile *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), *pProjNext = nullptr; pProj; pProj = pProjNext)
	{
		pProjNext = (CProjectile *)pProj->TypeNext();
		const int Owner = pProj->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pProj->Destroy();
	}

	for(CLaser *pLaser = (CLaser *)World.FindFirst(CGameWorld::ENTTYPE_LASER), *pLaserNext = nullptr; pLaser; pLaser = pLaserNext)
	{
		pLaserNext = (CLaser *)pLaser->TypeNext();
		const int Owner = pLaser->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pLaser->Destroy();
	}
}

void CFastPractice::BeginPracticeTrack(int StartTick)
{
	GameClient()->m_LocalWorld.BeginTrack(StartTick);
	// 多轨身份：本体轨恒为 0，分身存在时占轨 1。名字用于回放成员列表，
	// 与服务器快照里的名字同源（练习里没有别的玩家）。
	const int LocalId = m_EnableLocalClientId;
	const int DummyId = m_EnableDummyClientId;
	if(LocalId >= 0 && LocalId < MAX_CLIENTS)
		GameClient()->m_LocalWorld.SetTrackIdentity(0, LocalId, GameClient()->m_aClients[LocalId].m_aName);
	if(DummyId >= 0 && DummyId < MAX_CLIENTS)
		GameClient()->m_LocalWorld.SetTrackIdentity(1, DummyId, GameClient()->m_aClients[DummyId].m_aName);
}

void CFastPractice::RecordPracticeFrames(CGameWorld &World, int Tick, int LocalClientId, int DummyClientId)
{
	if(!GameClient()->m_LocalWorld.TrackActive() || m_RecordingPaused)
		return;

	const auto Record = [&](int TrackIndex, int ClientId) {
		if(ClientId < 0)
			return;
		CCharacter *pChar = World.GetCharacterById(ClientId);
		if(!pChar)
			return;

		// 采样源是分支世界的角色，不是服务器快照：练习中服务器侧的自己被锁了输入，
		// 一直停在开练习那一刻，用快照录出来的只会是一条静止的线。
		const CCharacterCore *pCore = pChar->Core();
		SLocalActorFrame Frame = {};
		Frame.m_Tick = Tick;
		Frame.m_X = (int)pCore->m_Pos.x;
		Frame.m_Y = (int)pCore->m_Pos.y;
		// 协议里速度是 1/256 单位，写回去再由 GetGhostCharacter 转成幽灵格式。
		Frame.m_VelX = (int)(pCore->m_Vel.x * 256.0f);
		Frame.m_VelY = (int)(pCore->m_Vel.y * 256.0f);
		Frame.m_Angle = pCore->m_Angle;
		Frame.m_Direction = pCore->m_Direction;
		Frame.m_Weapon = pCore->m_ActiveWeapon;
		Frame.m_HookState = pCore->m_HookState;
		Frame.m_HookX = (int)pCore->m_HookPos.x;
		Frame.m_HookY = (int)pCore->m_HookPos.y;
		Frame.m_AttackTick = pChar->GetAttackTick();
		GameClient()->m_LocalWorld.AddTrackFrame(TrackIndex, Frame);
	};
	Record(0, LocalClientId);
	Record(1, DummyClientId);
}

void CFastPractice::RecordPracticeEvents(CGameWorld &World, int Tick)
{
	// 与 HandlePredictedEvents 同一门控：被压制的 tick 当时就没播特效/音效，
	// 回放里也不该凭空多出来。
	if(!GameClient()->m_LocalWorld.TrackActive() || m_RecordingPaused || GameClient()->m_SuppressEvents)
		return;

	// 分支世界里真实发生的交互（枪声/锤击音、锤击环、爆炸、伤害指示）要跟轨迹
	// 一起存，回放时查看模式才会像 demo 一样播特效/音效。只收 .qmevt 读取器认识
	// 且 HandlePredictedEvents 当时确实会播的类型；音效走与播放侧完全相同的门控，
	// 否则回看会冒出练习时根本没听见的声音。
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	for(const CGameWorld::CPredictedEvent &Event : World.m_PredictedEvents)
	{
		// m_Handled / m_ServerConfirmed 是主世界快照确认事件：属于另一条时间线，
		// 练习画面里不播，也绝不能录进这条分支的回放。
		if(Event.m_Tick != Tick || Event.m_Handled || Event.m_ServerConfirmed)
			continue;
		switch(Event.m_EventId)
		{
		case NETEVENTTYPE_HAMMERHIT:
		case NETEVENTTYPE_EXPLOSION:
		case NETEVENTTYPE_DAMAGEIND:
			break;
		case NETEVENTTYPE_SOUNDWORLD:
			if(!QmShouldPlayWorldSound(Event.m_ExtraInfo, g_Config.m_SndGame, Focus.m_AirJump.m_PlaySound,
				   Focus.m_PlayDeathOrSpawnSound, GameClient()->m_GameInfo.m_RaceSounds,
				   g_Config.m_SndGun, g_Config.m_SndLongPain))
				continue;
			break;
		default:
			continue;
		}
		SLocalActorEvent Local = {};
		Local.m_Tick = Tick;
		Local.m_Type = Event.m_EventId;
		Local.m_aData[0] = (int)Event.m_Pos.x;
		Local.m_aData[1] = (int)Event.m_Pos.y;
		Local.m_aData[2] = Event.m_ExtraInfo;
		GameClient()->m_LocalWorld.AddTrackEvent(Local);
	}
}

void CFastPractice::RecordPracticeSwitchState(CGameWorld &World, int Tick)
{
	if(!GameClient()->m_LocalWorld.TrackActive() || m_RecordingPaused || World.Switchers().empty())
		return;

	// 队伍与查看模式 ApplyViewSwitchState 用同一个来源，保证录进去的状态
	// 回放时能落回同一批 switcher 槽位。分支世界的开关由预测角色踩 tile
	// 更新（character.cpp HandleTiles）+ CGameWorld::Tick 处理定时到期，
	// 这里只做全量采样，去重由 CLocalWorld 负责。
	const int Team = std::clamp(GameClient()->SwitchStateTeam(), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
	GameClient()->m_LocalWorld.AddTrackSwitchState(QmEncodeLocalSwitchState(Tick, World.Switchers(), Team));
}

void CFastPractice::OnPracticePositionJump()
{
	// 低频诊断点：位置突变必须切段，否则 /pg 出来的线路会带一道穿墙直线。
	dbg_msg("fast_practice", "segment boundary: frames=%d events=%d switches=%d",
		GameClient()->m_LocalWorld.CurrentFrameCount(), GameClient()->m_LocalWorld.CurrentEventCount(),
		GameClient()->m_LocalWorld.CurrentSwitchStateCount());
	GameClient()->m_LocalWorld.MarkSegmentBoundary();
}

bool CFastPractice::SavePracticeGhost(const char *pName)
{
	SLocalSegment Segment;
	if(!GameClient()->m_LocalWorld.BestSegment(&Segment))
	{
		EchoPractice("no practice line to save yet (run for at least a second)");
		return false;
	}
	if(!GameClient()->m_Ghost.SavePracticeTrack(pName, Segment))
	{
		EchoPractice("could not save practice line (ghost recorder busy)");
		return false;
	}
	// 保存不等于播放：用户到影子页点 View 才进入 demo 式回放，不退出服务器。
	dbg_msg("fast_practice", "ghost saved: tracks=%d frames=%d events=%d switches=%d start_tick=%d",
		(int)Segment.m_vTracks.size(), (int)Segment.m_vTracks.front().m_vFrames.size(),
		(int)Segment.m_vEvents.size(), (int)Segment.m_vSwitchStates.size(), Segment.m_StartTick);
	GameClient()->m_Menus.GhostlistPopulate();
	EchoPractice("practice line saved (%d tracks, %d ticks); open the Ghost page and press View to replay it",
		(int)Segment.m_vTracks.size(), (int)Segment.m_vTracks.front().m_vFrames.size());
	return true;
}

void CFastPractice::ShiftPracticeWorldTicks(int TickDelta)
{
	if(TickDelta <= 0)
		return;

	// 低频诊断点：暂停恢复后分支时钟被平移，轨迹/事件/开关的 tick 必须同步。
	dbg_msg("fast_practice", "tick shift: +%d branch_tick=%d track_start=%d frames=%d",
		TickDelta, m_PracticeBaseWorld.GameTick(), GameClient()->m_LocalWorld.TrackStartTick(),
		GameClient()->m_LocalWorld.CurrentFrameCount());
	// 轨迹里的绝对 tick 跟着世界时钟一起走，否则暂停恢复后存成幽灵会错开一截。
	GameClient()->m_LocalWorld.ShiftTrackTicks(TickDelta);

	m_PracticeBaseWorld.m_GameTick += TickDelta;
	// 预测事件队列会随 CopyWorldClean 一起进入预测世界（gameworld.cpp:672/725）。
	// tick 留在旧基准上，QmProcessPredictedEvents 只判 m_Tick <= Tick，
	// 平移后的第一帧就会把积压的事件一次性播出来（特效/音效集中爆发）。
	for(auto &Event : m_PracticeBaseWorld.m_PredictedEvents)
		Event.m_Tick += TickDelta;
	for(auto &Switcher : m_PracticeBaseWorld.Switchers())
	{
		for(int Team = 0; Team < NUM_DDRACE_TEAMS; Team++)
		{
			if(Switcher.m_aEndTick[Team] > 0)
				Switcher.m_aEndTick[Team] += TickDelta;
			if(Switcher.m_aLastUpdateTick[Team] > 0)
				Switcher.m_aLastUpdateTick[Team] += TickDelta;
		}
	}
	for(int &LastAttackTick : m_aLastAttackTick)
		if(LastAttackTick >= 0)
			LastAttackTick += TickDelta;
	for(CCharacter *pChar = (CCharacter *)m_PracticeBaseWorld.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
		pChar->ShiftTickBase(TickDelta);
	for(CProjectile *pProj = (CProjectile *)m_PracticeBaseWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
		pProj->ShiftStartTick(TickDelta);
	for(CLaser *pLaser = (CLaser *)m_PracticeBaseWorld.FindFirst(CGameWorld::ENTTYPE_LASER); pLaser; pLaser = (CLaser *)pLaser->TypeNext())
		pLaser->ShiftEvalTick(TickDelta);
	for(CPlasma *pPlasma = (CPlasma *)m_PracticeBaseWorld.FindFirst(CGameWorld::ENTTYPE_PLASMA); pPlasma; pPlasma = (CPlasma *)pPlasma->TypeNext())
		pPlasma->ShiftEvalTick(TickDelta);
}

void CFastPractice::FreezePracticeWorldForServerPause()
{
	if(!m_PracticeWorldInitialized)
		return;

	const int TargetTick = Client()->PredGameTick(g_Config.m_ClDummy);
	ShiftPracticeWorldTicks(TargetTick - m_PracticeBaseWorld.GameTick());

	CaptureServerReleasedFireStates();
	// 临时自由旁观保留练习世界中的钩子，不重新要求松开钩子键。
	CapturePracticeInputFilterStates(true);
	ReleaseBufferedActionInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	GameClient()->m_PredictedDummyId = -1;
}

void CFastPractice::SyncPracticeWorldConfig()
{
	m_PracticeBaseWorld.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictWeapons = true;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictFreeze = true;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictTiles = true;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictDDRace = true;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictTeleport = true;
	// 队伍号跟随服务器，但 solo 是分支世界的本地状态（/solo /unsolo）。
	// 整份覆盖会把练习里的 solo 每帧冲掉，表现为 solo 不生效、分身仍是实体。
	CTeamsCore Teams = GameClient()->m_Teams;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		Teams.SetSolo(ClientId, m_PracticeBaseWorld.m_Teams.GetSolo(ClientId));
	m_PracticeBaseWorld.m_Teams = Teams;
}

void CFastPractice::LoadPracticeGuns()
{
	m_vPracticeGuns.clear();
	m_PracticeGunsLoaded = true;

	const CTile *pGame = Collision()->GameLayer();
	const CTile *pFront = Collision()->FrontLayer();
	const CSwitchTile *pSwitch = Collision()->SwitchLayer();
	if(!pGame)
		return;

	// 与 CGameContext::CreateAllEntities 一致：实体层的 tile index 减 ENTITY_OFFSET
	// 就是实体编号，冻结/爆炸属性由 ENTITY_PLASMA* 决定。
	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	for(int y = 0; y < Height; y++)
	{
		for(int x = 0; x < Width; x++)
		{
			const int Index = y * Width + x;
			const vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);

			const auto AddGun = [&](int EntityIndex, int Layer, int Number) {
				SPracticeGun Gun;
				switch(EntityIndex)
				{
				case ENTITY_PLASMAE:
					Gun.m_Freeze = false;
					Gun.m_Explosive = true;
					break;
				case ENTITY_PLASMAF:
					Gun.m_Freeze = true;
					Gun.m_Explosive = false;
					break;
				case ENTITY_PLASMA:
					Gun.m_Freeze = true;
					Gun.m_Explosive = true;
					break;
				case ENTITY_PLASMAU:
					Gun.m_Freeze = false;
					Gun.m_Explosive = false;
					break;
				default: return;
				}
				Gun.m_Pos = Pos;
				Gun.m_Layer = Layer;
				Gun.m_Number = Number;
				Gun.m_EvalTick = m_PracticeBaseWorld.GameTick();
				m_vPracticeGuns.push_back(Gun);
			};

			if(pGame[Index].m_Index >= ENTITY_OFFSET)
				AddGun(pGame[Index].m_Index - ENTITY_OFFSET, LAYER_GAME, 0);
			if(pFront && pFront[Index].m_Index >= ENTITY_OFFSET)
				AddGun(pFront[Index].m_Index - ENTITY_OFFSET, LAYER_FRONT, 0);
			if(pSwitch && pSwitch[Index].m_Type >= ENTITY_OFFSET)
				AddGun(pSwitch[Index].m_Type - ENTITY_OFFSET, LAYER_SWITCH, pSwitch[Index].m_Number);
		}
	}
}

void CFastPractice::TickPracticeGuns(CGameWorld &World, int Tick)
{
	if(!m_PracticeGunsLoaded)
		LoadPracticeGuns();
	if(m_vPracticeGuns.empty())
		return;

	const int TickSpeed = Client()->GameTickSpeed();
	const int MoveInterval = (int)(TickSpeed * 0.15f);
	for(SPracticeGun &Gun : m_vPracticeGuns)
	{
		// 与 CGun::Tick 一致：机关坐标每 0.15 秒跟随一次移动平台。
		if(MoveInterval > 0 && Tick % MoveInterval == 0)
		{
			Gun.m_EvalTick = Tick;
			Collision()->MoverSpeed(Gun.m_Pos.x, Gun.m_Pos.y, &Gun.m_Core);
			Gun.m_Pos += Gun.m_Core;
		}
		if(g_Config.m_SvPlasmaPerSec > 0)
			FirePracticeGun(World, Gun, Tick);
	}
}

void CFastPractice::FirePracticeGun(CGameWorld &World, SPracticeGun &Gun, int Tick)
{
	const int FireDelay = Client()->GameTickSpeed() / g_Config.m_SvPlasmaPerSec;

	// 复刻 CGun::Fire：solo 玩家单独计时，其余按队伍取最近目标。
	int aMinDistInTeam[MAX_CLIENTS] = {};
	int aTargetIdInTeam[MAX_CLIENTS];
	bool aIsTarget[MAX_CLIENTS] = {};
	std::fill(std::begin(aTargetIdInTeam), std::end(aTargetIdInTeam), -1);

	for(CCharacter *pTarget = (CCharacter *)World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pTarget; pTarget = (CCharacter *)pTarget->TypeNext())
	{
		const int TargetClientId = pTarget->GetCid();
		if(TargetClientId < 0 || TargetClientId >= MAX_CLIENTS)
			continue;
		const int TargetTeam = pTarget->Team();
		if(TargetTeam == TEAM_SUPER)
			continue;
		if(Gun.m_Layer == LAYER_SWITCH && Gun.m_Number > 0 &&
			!World.Switchers()[Gun.m_Number].m_aStatus[TargetTeam])
			continue;

		const bool TargetIsSolo = World.m_Teams.GetSolo(TargetClientId);
		if((TargetIsSolo && Gun.m_aLastFireSolo[TargetClientId] + FireDelay > Tick) ||
			(!TargetIsSolo && Gun.m_aLastFireTeam[TargetTeam] + FireDelay > Tick))
			continue;

		if(distance(pTarget->m_Pos, Gun.m_Pos) > g_Config.m_SvPlasmaRange)
			continue;
		if(Collision()->IntersectLine(Gun.m_Pos, pTarget->m_Pos, nullptr, nullptr))
			continue;

		if(TargetIsSolo)
		{
			aIsTarget[TargetClientId] = true;
			Gun.m_aLastFireSolo[TargetClientId] = Tick;
		}
		else
		{
			const int Distance = distance(pTarget->m_Pos, Gun.m_Pos);
			if(aMinDistInTeam[TargetTeam] == 0 || aMinDistInTeam[TargetTeam] > Distance)
			{
				aMinDistInTeam[TargetTeam] = Distance;
				aTargetIdInTeam[TargetTeam] = TargetClientId;
			}
		}
	}

	for(int Team = 0; Team < MAX_CLIENTS; Team++)
	{
		if(aTargetIdInTeam[Team] != -1)
		{
			aIsTarget[aTargetIdInTeam[Team]] = true;
			Gun.m_aLastFireTeam[Team] = Tick;
		}
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!aIsTarget[ClientId])
			continue;
		CCharacter *pTarget = World.GetCharacterById(ClientId);
		if(!pTarget)
			continue;
		CLaserData Data = {};
		Data.m_From = Gun.m_Pos;
		Data.m_StartTick = Tick;
		Data.m_Owner = ClientId;
		Data.m_SwitchNumber = Gun.m_Number;
		Data.m_Subtype = (Gun.m_Explosive ? 1 : 0) | (Gun.m_Freeze ? 2 : 0);
		new CPlasma(&World, m_NextPracticePlasmaId++, &Data);
	}
}

bool CFastPractice::InitPracticeWorld()
{
	// 重新 fork（/r、重进、追赶失败）会让角色回到锚点，轨迹上是一次瞬移，
	// 必须先切段，否则存出来的幽灵会带一道穿墙的直线。
	if(GameClient()->m_LocalWorld.TrackActive())
		GameClient()->m_LocalWorld.MarkSegmentBoundary();

	m_PracticeBaseWorld.CopyWorldClean(&GameClient()->m_GameWorld);
	PrunePracticeWorld(m_PracticeBaseWorld);
	SanitizePracticeWorldHooks(m_PracticeBaseWorld);
	SyncPracticeWorldConfig();
	m_PracticeWorldInitialized = true;

	if(!m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
	{
		m_PracticeWorldInitialized = false;
		return false;
	}

	if(m_RequireDummy && !m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
	{
		m_PracticeWorldInitialized = false;
		return false;
	}

	if(!m_HasPracticeStartSnapshot)
	{
		// 重开快照只取第一次 fork 的结果。主世界一直在推进，之后的 InitPracticeWorld
		// 重新 fork 出来的是“现在”，不再是进入练习那一刻的机关与开关状态。
		m_PracticeStartWorld.CopyWorldClean(&m_PracticeBaseWorld);
		m_HasPracticeStartSnapshot = true;
	}

	return true;
}

bool CFastPractice::SanitizePracticeWorldHooks(CGameWorld &World) const
{
	bool Changed = false;
	const int aParticipants[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(const int ClientId : aParticipants)
	{
		CCharacter *pChar = ClientId >= 0 ? World.GetCharacterById(ClientId) : nullptr;
		if(!pChar)
			continue;

		CCharacterCore Core = pChar->GetCore();
		bool CharChanged = false;

		const int HookedPlayer = Core.HookedPlayer();
		if(HookedPlayer >= 0 && HookedPlayer < MAX_CLIENTS &&
			(QmPracticeHookShouldRelease(HookedPlayer, aParticipants[0], aParticipants[1]) || World.GetCharacterById(HookedPlayer) == nullptr))
		{
			// 目标可能已被 PrunePracticeWorld 销毁，SetHookedPlayer 会安全跳过空槽。
			Core.SetHookedPlayer(-1);
			Core.m_HookState = HOOK_RETRACTED;
			Core.m_HookPos = Core.m_Pos;
			Core.m_HookDir = vec2(0.0f, 0.0f);
			Core.m_HookTick = 0;
			Core.m_NewHook = false;
			CharChanged = true;
		}

		// 被裁掉的玩家可能仍挂在 m_AttachedPlayers 里，留下无效 Cid。
		for(auto It = Core.m_AttachedPlayers.begin(); It != Core.m_AttachedPlayers.end();)
		{
			if(World.GetCharacterById(*It) == nullptr)
			{
				It = Core.m_AttachedPlayers.erase(It);
				CharChanged = true;
			}
			else
			{
				++It;
			}
		}

		if(CharChanged)
		{
			pChar->SetCore(Core);
			pChar->ResetHook();
			Changed = true;
		}
	}
	return Changed;
}

bool CFastPractice::ResolvePracticeSpectatorPosition(vec2 &Position)
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return false;

	const auto &SpecInfo = GameClient()->m_Snap.m_SpecInfo;
	if(!SpecInfo.m_Active || !SpecInfo.m_UsePosition)
		return false;
	if(SpecInfo.m_SpectatorId != m_EnableLocalClientId && SpecInfo.m_SpectatorId != m_EnableDummyClientId)
		return false;

	// 被跟随的只能在分支世界内找：裁剪后其他玩家不存在，快照位置更是另一个世界线。
	CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(SpecInfo.m_SpectatorId);
	if(!pChar)
		return false;

	Position = pChar->Core()->m_Pos;
	return true;
}

bool CFastPractice::ResolvePracticeSoloState(int ClientId, bool &Solo) const
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return false;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(ClientId != m_EnableLocalClientId && ClientId != m_EnableDummyClientId)
		return false;

	Solo = m_PracticeBaseWorld.m_Teams.GetSolo(ClientId);
	return true;
}

bool CFastPractice::ResolvePracticePlaybackTick(int &PlaybackTick) const
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return false;
	// 分支世界的 GameTick 才是这条世界线的当前时刻。正常帧它等于 PredGameTick，
	// 但服务器暂停/冻结时练习世界会停住而 PredGameTick 继续推进——此时幽灵必须
	// 也停住，否则暂停回来会看到虚影凭空跑到前面去。
	PlaybackTick = m_PracticeBaseWorld.GameTick() - GameClient()->m_Ghost.RunningPlaybackStartTick();
	return true;
}

vec2 CFastPractice::ResolvePracticeCursorTarget()
{
	const auto &Camera = GameClient()->m_Camera;
	const auto &SpecInfo = GameClient()->m_Snap.m_SpecInfo;
	vec2 SpecPosition = SpecInfo.m_Position;
	ResolvePracticeSpectatorPosition(SpecPosition);
	return QmPracticeCursorWorldPosition(
		GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy],
		Camera.m_Center, SpecPosition, Camera.m_Zoom,
		QmPracticeCursorSpaceFor(SpecInfo.m_Active, SpecInfo.m_UsePosition));
}

void CFastPractice::CaptureAnchorsFromSnapshot()
{
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_HasDummyAnchor = false;

	CaptureAnchorFromSnapshot(m_EnableLocalClientId, m_MainAnchor);
	CaptureAnchorFromSnapshot(m_EnableDummyClientId, m_DummyAnchor);
	m_HasDummyAnchor = m_DummyAnchor.m_Valid;
}

void CFastPractice::CaptureAnchorFromSnapshot(int ClientId, SAnchorData &Anchor)
{
	Anchor = {};
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	Anchor.m_Valid = true;
	Anchor.m_ClientId = ClientId;
	Anchor.m_Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	Anchor.m_HasDDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData;
	if(Anchor.m_HasDDNet)
		Anchor.m_DDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData;
}

bool CFastPractice::ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const
{
	if(!Anchor.m_Valid)
		return false;

	CCharacter *pChar = World.GetCharacterById(Anchor.m_ClientId);
	if(!pChar)
		return false;

	CNetObj_Character CharObj = Anchor.m_Char;
	if(Anchor.m_HasDDNet)
	{
		CNetObj_DDNetCharacter DDNetObj = Anchor.m_DDNet;
		pChar->Read(&CharObj, &DDNetObj, true);
	}
	else
	{
		pChar->Read(&CharObj, nullptr, true);
	}

	CNetObj_PlayerInput NeutralInput = {};
	NeutralInput.m_TargetY = -1;
	pChar->SetInput(&NeutralInput);
	pChar->m_CanMoveInFreeze = false;
	NormalizeDefaultPracticeWeapons(pChar, Anchor.m_HasDDNet);
	return true;
}

void CFastPractice::ResetAttackTickHistory()
{
	m_aLastAttackTick.fill(-1);

	if(CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		m_aLastAttackTick[m_EnableLocalClientId] = pChar->GetAttackTick();
	if(m_EnableDummyClientId >= 0)
		if(CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			m_aLastAttackTick[m_EnableDummyClientId] = pChar->GetAttackTick();
}

void CFastPractice::ReleaseBufferedInputState()
{
	// Hard invalidation path for reconnects/disconnects where stale movement
	// buffers should not survive into a different server-side input stream.
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		NeutralizeInput(GameClient()->m_Controls.m_aInputData[Conn]);
		NeutralizeInput(GameClient()->m_Controls.m_aLastData[Conn]);
		GameClient()->m_Controls.m_aInputData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aInputData[Conn].m_Fire);
		GameClient()->m_Controls.m_aLastData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aLastData[Conn].m_Fire);
	}
	NeutralizeInput(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy]);
	GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire);

	NeutralizeInput(GameClient()->m_DummyInput);
	GameClient()->m_DummyInput.m_Fire = ReleasedFireState(GameClient()->m_DummyInput.m_Fire);
	NeutralizeInput(GameClient()->m_HammerInput);
	GameClient()->m_HammerInput.m_Fire = ReleasedFireState(GameClient()->m_HammerInput.m_Fire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::CaptureServerReleasedFireStates()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		int FireState = GameClient()->m_DummyInput.m_Fire;
		if(Slot == g_Config.m_ClDummy)
			FireState = GameClient()->m_Controls.m_aLastData[Slot].m_Fire;
		else if(g_Config.m_ClDummyHammer)
			FireState = GameClient()->m_HammerInput.m_Fire;
		m_aServerReleasedFireStates[Slot] = ReleasedFireState(FireState);
	}
}

void CFastPractice::ReleaseBufferedActionInputState()
{
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		const int ReleasedFire = m_aServerReleasedFireStates[Conn];
		SuppressActionInput(GameClient()->m_Controls.m_aInputData[Conn], ReleasedFire);
		SuppressActionInput(GameClient()->m_Controls.m_aLastData[Conn], ReleasedFire);
		SuppressActionInput(GameClient()->m_Controls.m_aFastInput[Conn], ReleasedFire);
	}

	const int DummySlot = !g_Config.m_ClDummy;
	const int DummyReleasedFire = m_aServerReleasedFireStates[DummySlot];
	SuppressActionInput(GameClient()->m_DummyInput, DummyReleasedFire);
	SuppressActionInput(GameClient()->m_HammerInput, DummyReleasedFire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::CapturePracticeInputFilterStates(bool PreserveHeldHook)
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		CNetObj_PlayerInput Input = GameClient()->m_Controls.m_aInputData[Slot];
		int Left = 0;
		int Right = 0;
		if(Slot == g_Config.m_ClDummy)
		{
			Left = GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
			Right = GameClient()->m_Controls.m_aInputDirectionRight[Slot];
		}
		else if(Input.m_Direction < 0)
		{
			Left = 1;
		}
		else if(Input.m_Direction > 0)
		{
			Right = 1;
		}

		SPracticeInputFilterState State;
		State.m_LeftHeld = Left != 0;
		State.m_LeftReleased = !State.m_LeftHeld;
		State.m_RightHeld = Right != 0;
		State.m_RightReleased = !State.m_RightHeld;
		State.m_JumpReleased = Input.m_Jump == 0;
		const int ClientId = GameClient()->m_aLocalIds[Slot];
		CCharacter *pCharacter = ClientId >= 0 ? m_PracticeBaseWorld.GetCharacterById(ClientId) : nullptr;
		const bool KeepAttachedHook = PreserveHeldHook && pCharacter && pCharacter->Core()->m_HookState == HOOK_GRABBED;
		State.m_Hook.Capture(Input.m_Hook, KeepAttachedHook);
		m_aPracticeInputFilterStates[Slot] = State;
	}
}

void CFastPractice::PreserveCurrentPracticeInputFilterStates()
{
	for(auto &State : m_aPracticeInputFilterStates)
	{
		State.m_LeftReleased = true;
		State.m_RightReleased = true;
		State.m_JumpReleased = true;
		State.m_Hook.Reset();
	}
}

void CFastPractice::FilterPracticeInput(CNetObj_PlayerInput &Input, int InputConn, bool Commit)
{
	const int Slot = InputConn ^ g_Config.m_ClDummy;
	if(Slot < 0 || Slot >= NUM_DUMMIES)
		return;

	SPracticeInputFilterState State = m_aPracticeInputFilterStates[Slot];
	int LiveLeft = 0;
	int LiveRight = 0;
	if(Slot == g_Config.m_ClDummy)
	{
		LiveLeft = GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
		LiveRight = GameClient()->m_Controls.m_aInputDirectionRight[Slot];
	}
	else if(GameClient()->m_Controls.m_aInputData[Slot].m_Direction < 0)
	{
		LiveLeft = 1;
	}
	else if(GameClient()->m_Controls.m_aInputData[Slot].m_Direction > 0)
	{
		LiveRight = 1;
	}

	if(State.m_LeftHeld && !State.m_LeftReleased)
	{
		if(LiveLeft == 0)
			State.m_LeftReleased = true;
		else
			LiveLeft = 0;
	}
	if(State.m_RightHeld && !State.m_RightReleased)
	{
		if(LiveRight == 0)
			State.m_RightReleased = true;
		else
			LiveRight = 0;
	}
	if(Slot == g_Config.m_ClDummy || (State.m_LeftHeld && !State.m_LeftReleased) || (State.m_RightHeld && !State.m_RightReleased))
		Input.m_Direction = LiveRight - LiveLeft;

	if(!State.m_JumpReleased)
	{
		if(GameClient()->m_Controls.m_aInputData[Slot].m_Jump == 0)
			State.m_JumpReleased = true;
	}
	if(!State.m_JumpReleased)
	{
		if(Input.m_Jump != 0)
			Input.m_Jump = 0;
	}
	Input.m_Hook = State.m_Hook.Filter(Input.m_Hook, GameClient()->m_Controls.m_aInputData[Slot].m_Hook);

	if(Commit)
		m_aPracticeInputFilterStates[Slot] = State;
}

void CFastPractice::InvalidateBufferedInputState()
{
	ReleaseBufferedInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
}

void CFastPractice::CaptureServerLockedInputs()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		// 练习中接入分身时，主角色继续沿用进入练习前锁定的输入。
		if(m_Enabled && m_aHasServerLockedInputs[Slot] && GameClient()->m_aLocalIds[Slot] == m_EnableLocalClientId)
			continue;

		CNetObj_PlayerInput Input = {};
		if(Slot == g_Config.m_ClDummy)
		{
			Input = GameClient()->m_Controls.m_aInputData[Slot];
			Input.m_Direction = GameClient()->m_Controls.m_aInputDirectionRight[Slot] - GameClient()->m_Controls.m_aInputDirectionLeft[Slot];
		}
		else
		{
			Input = g_Config.m_ClDummyHammer ? GameClient()->m_HammerInput : GameClient()->m_DummyInput;
		}

		if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
			Input.m_TargetX = 1;

		// 保留瞄准和钩子，避免挂墙本体松钩掉落；释放移动、跳跃和开火。
		Input.m_Direction = 0;
		Input.m_Jump = 0;
		SuppressActionInput(Input, ReleasedFireState(Input.m_Fire));
		m_aServerLockedInputs[Slot] = Input;
		m_aHasServerLockedInputs[Slot] = true;
	}
}

void CFastPractice::Enable(bool PreserveHeldInput)
{
	if(m_Enabled || !CanEnable())
		return;

	GameClient()->m_Binds.RefreshActiveBinds();

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int InactiveConn = ActiveConn ^ 1;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	m_EnableLocalClientId = (ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS) ? ActiveClientId : GameClient()->m_Snap.m_LocalClientId;

	const int CandidateDummyId = Client()->DummyConnected() ? GameClient()->m_aLocalIds[InactiveConn] : -1;
	m_EnableDummyClientId = CandidateDummyId;
	if(m_EnableDummyClientId < 0 || m_EnableDummyClientId >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_aCharacters[m_EnableDummyClientId].m_Active || GameClient()->m_aClients[m_EnableDummyClientId].m_Paused)
	{
		m_EnableDummyClientId = -1;
	}
	m_RequireDummy = m_EnableDummyClientId >= 0;

	if(!InitPracticeWorld())
	{
		ResetPracticeState();
		return;
	}

	CaptureAnchorsFromSnapshot();
	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		ResetPracticeState();
		return;
	}

	CaptureServerLockedInputs();
	CaptureServerReleasedFireStates();
	CapturePracticeInputFilterStates();
	if(PreserveHeldInput)
		PreserveCurrentPracticeInputFilterStates();
	ReleaseBufferedActionInputState();
	m_Enabled = true;
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	// 录制是全手动的：进入练习不再自动开录，由“开始录制”按键启动轨迹。
	m_RecordingPaused = false;
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetCommandState();
	ResetAttackTickHistory();
	if(CCharacter *pLocal = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pLocal);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);

	// Snap renderer/camera prediction state to the local practice world immediately.
	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableLocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableDummyClientId))
		{
			GameClient()->m_aClients[m_EnableDummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[m_EnableDummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();
}

bool CFastPractice::TryAttachDummyFromSnapshot()
{
	if(!m_Enabled || m_RequireDummy || !Client()->DummyConnected())
		return false;

	int CandidateDummyId = -1;
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		const int ClientId = GameClient()->m_aLocalIds[Conn];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || ClientId == m_EnableLocalClientId)
			continue;
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || GameClient()->m_aClients[ClientId].m_Paused)
			continue;
		CandidateDummyId = ClientId;
		break;
	}
	if(CandidateDummyId < 0)
		return false;

	m_EnableDummyClientId = CandidateDummyId;
	m_RequireDummy = true;
	if(!InitPracticeWorld())
	{
		m_EnableDummyClientId = -1;
		m_RequireDummy = false;
		m_PracticeWorldInitialized = false;
		InitPracticeWorld();
		return false;
	}

	// 主角色锚点属于整个练习会话；延迟接入 dummy 不能把复位点改成当前快照。
	CaptureAnchorFromSnapshot(m_EnableDummyClientId, m_DummyAnchor);
	m_HasDummyAnchor = m_DummyAnchor.m_Valid;
	if(!m_MainAnchor.m_Valid || !m_HasDummyAnchor)
	{
		m_EnableDummyClientId = -1;
		m_RequireDummy = false;
		m_PracticeWorldInitialized = false;
		InitPracticeWorld();
		return false;
	}

	CaptureServerLockedInputs();
	CaptureServerReleasedFireStates();
	CapturePracticeInputFilterStates();
	ReleaseBufferedActionInputState();
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	ResetAttackTickHistory();
	if(CCharacter *pLocal = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pLocal);
	if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
		TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);

	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_LastResolvedLocalInputConn = -1;
	m_LastResolvedDummyInputConn = -1;
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableLocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableDummyClientId))
	{
		GameClient()->m_aClients[m_EnableDummyClientId].m_Predicted = pPredDummy->GetCore();
		GameClient()->m_aClients[m_EnableDummyClientId].m_PrevPredicted = pPredDummy->GetCore();
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();
	return true;
}

void CFastPractice::Disable()
{
	const bool WasEnabled = m_Enabled;
	if(m_Enabled)
		GameClient()->m_PredictedDummyId = -1;
	m_aHasServerLockedInputs.fill(false);
	if(WasEnabled)
		ReleaseBufferedActionInputState();
	// 机关炮缓存绑定当前地图，下次启用时重新解析。
	m_PracticeGunsLoaded = false;
	m_vPracticeGuns.clear();
	// 世界树：主服务器世界 m_GameWorld 一直独立推进，练习是其 fork 出的分支。
	// 退出即销毁分支——EndTrack 丢弃轨迹会话，ResetPracticeState 里的
	// m_PracticeBaseWorld.Clear() 销毁所有实体；下次进入重新 fork。
	// 已 /pg 落盘的 .gho 组是独立文件，分支销毁后依然存在。
	GameClient()->m_LocalWorld.EndTrack();
	ResetPracticeState();
	if(WasEnabled)
		m_aPostPracticeInputSuppressTicks.fill(2);
}

void CFastPractice::Toggle(bool PreserveHeldInput)
{
	if(m_Enabled)
		Disable();
	else
		Enable(PreserveHeldInput);
}

bool CFastPractice::ConsumeKillCommand()
{
	if(!m_Enabled)
		return false;
	ResetPracticeToAnchor();
	return true;
}

void CFastPractice::StandbyCharacter(CCharacter *pChar) const
{
	if(!pChar)
		return;

	CNetObj_PlayerInput NeutralInput = *pChar->LatestInput();
	NeutralizeInput(NeutralInput);
	pChar->SetInput(&NeutralInput);

	CCharacterCore Core = pChar->GetCore();
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_Direction = 0;
	Core.m_Input = NeutralInput;
	Core.m_HookState = HOOK_RETRACTED;
	Core.SetHookedPlayer(-1);
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	Core.m_HookTick = 0;
	Core.m_NewHook = false;
	Core.m_TriggeredEvents = 0;
	pChar->SetCore(Core);
}

void CFastPractice::PrepareForSpectating()
{
	if(!m_Enabled)
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
		return;

	// 只释放练习输入并保存待机状态，调用方仍须把进入旁观的请求发给服务器。
	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		m_PracticeWorldInitialized = false;
		GameClient()->m_PredictedDummyId = -1;
		return;
	}
	if(!m_PracticeWorldInitialized && !InitPracticeWorld())
	{
		m_PracticeWorldInitialized = false;
		GameClient()->m_PredictedDummyId = -1;
		return;
	}

	SyncPracticeWorldConfig();
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);

	StandbyCharacter(m_PracticeBaseWorld.GetCharacterById(LocalClientId));
	if(m_RequireDummy && DummyClientId >= 0)
		StandbyCharacter(m_PracticeBaseWorld.GetCharacterById(DummyClientId));

	CaptureServerReleasedFireStates();
	CapturePracticeInputFilterStates();
	ReleaseBufferedActionInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);

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
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetAttackTickHistory();
}

void CFastPractice::ResetPracticeToAnchor()
{
	if(!m_Enabled)
		return;

	if(!InitPracticeWorld())
	{
		Disable();
		return;
	}

	// 与远程一致：每次 /r 都按当前快照重新捕获锚点，而不是沿用开启练习时的旧锚点。
	CaptureAnchorsFromSnapshot();

	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		Disable();
		return;
	}

	if(!ApplyAnchorToCharacter(m_PracticeBaseWorld, m_MainAnchor))
	{
		Disable();
		return;
	}

	if(m_RequireDummy && !ApplyAnchorToCharacter(m_PracticeBaseWorld, m_DummyAnchor))
	{
		Disable();
		return;
	}

	if(CCharacter *pMain = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		NormalizeCharacterAfterReset(pMain, false);
	if(m_RequireDummy)
	{
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			NormalizeCharacterAfterReset(pDummy, false);
	}

	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	PublishPracticeWorldState();
}

void CFastPractice::ResetPracticeTimelineState()
{
	for(auto &State : m_aPracticeCommandState)
	{
		// 救援模式是用户偏好，重开后保留；其余记录都指向旧时间线，必须清空。
		State.m_HasRescueAuto = false;
		State.m_HasRescueManual = false;
		State.m_RescueAutoPos = vec2(0.0f, 0.0f);
		State.m_RescueManualPos = vec2(0.0f, 0.0f);
		State.m_AutoTee = {};
		State.m_ManualTee = {};
		State.m_HasLastTeleport = false;
		State.m_HasLastDeath = false;
		State.m_LastTeleportPos = vec2(0.0f, 0.0f);
		State.m_LastDeathPos = vec2(0.0f, 0.0f);
		State.m_vSafePositions.clear();
	}
}

bool CFastPractice::RestartPracticeFromStart()
{
	if(!m_Enabled || !m_PracticeWorldInitialized || !m_HasPracticeStartSnapshot)
		return false;

	// 快照采样时分身可能还没接入。这条时间线里没有分身，重开无法同时还原两个角色，
	// 与其恢复出半个世界，不如明确拒绝；/unpractice 后重进会重新采样。
	if(!m_PracticeStartWorld.GetCharacterById(m_EnableLocalClientId) ||
		(m_RequireDummy && !m_PracticeStartWorld.GetCharacterById(m_EnableDummyClientId)))
	{
		EchoPractice("restart unavailable: the participant set changed since practice started");
		return false;
	}

	// 记录当前开火状态：CopyWorldClean 会销毁旧角色，重开后用它避免把"仍按住"误判成
	// 一次新的按下，落地就挥一锤。
	int LocalFire = 0;
	int DummyFire = 0;
	if(CCharacter *pMain = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		LocalFire = pMain->LatestInput()->m_Fire;
	if(m_RequireDummy)
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			DummyFire = pDummy->LatestInput()->m_Fire;

	// 世界树：主世界一直在推进，重新 fork 只会得到“现在”。这里改用启用练习那一刻的
	// 快照，机关、移动方块、投射物、开关与角色一起回到入口状态。
	m_PracticeBaseWorld.CopyWorldClean(&m_PracticeStartWorld);
	SanitizePracticeWorldHooks(m_PracticeBaseWorld);
	SyncPracticeWorldConfig();

	const auto NeutralizeRestartedInput = [](CCharacter *pChar, int Fire) {
		if(!pChar)
			return;
		CNetObj_PlayerInput NeutralInput = {};
		NeutralInput.m_TargetY = -1;
		NeutralInput.m_Fire = Fire;
		pChar->SetInput(&NeutralInput);
		pChar->ResetInput();
	};
	NeutralizeRestartedInput(m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId), LocalFire);
	if(m_RequireDummy)
		NeutralizeRestartedInput(m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId), DummyFire);

	ResetPracticeTimelineState();
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	PublishPracticeWorldState();

	// 重开是一次瞬移。轨迹历史保留，只切段，避免回放里连出一条穿墙直线。
	GameClient()->m_LocalWorld.MarkSegmentBoundary();
	return true;
}

void CFastPractice::SetPracticeCheckpoint()
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return;
	CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId);
	if(!pChar)
		return;
	auto &State = m_aPracticeCommandState[m_EnableLocalClientId];
	State.m_HasCheckpoint = true;
	CapturePracticeRescueTee(pChar, m_PracticeBaseWorld.GameTick(), State.m_CheckpointTee);
	EchoPractice("practice checkpoint set");
}

void CFastPractice::RestorePracticeCheckpoint()
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return;
	auto &State = m_aPracticeCommandState[m_EnableLocalClientId];
	if(!State.m_HasCheckpoint || !State.m_CheckpointTee.m_Valid)
	{
		EchoPractice("no practice checkpoint set");
		return;
	}
	CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId);
	if(!pChar)
		return;
	// 与 /r 相同，重生点只回滚 tee，不动世界状态。
	OnPracticePositionJump();
	ApplyPracticeRescueTee(pChar, State.m_CheckpointTee);
	pChar->Unfreeze();
	PublishPracticeWorldState();
}

void CFastPractice::StartPracticeRecording()
{
	if(!m_Enabled)
	{
		EchoPractice("practice recording: fast practice is not active");
		return;
	}
	if(GameClient()->m_LocalWorld.TrackActive())
	{
		if(!m_RecordingPaused)
		{
			EchoPractice("practice recording is already running");
			return;
		}
		// 暂停后按开始 = 继续。先切段：暂停期间的空洞不能连成一条直线。
		m_RecordingPaused = false;
		GameClient()->m_LocalWorld.MarkSegmentBoundary();
		EchoPractice("practice recording resumed");
		return;
	}
	BeginPracticeTrack(m_PracticeBaseWorld.GameTick());
	m_RecordingPaused = false;
	EchoPractice("practice recording started");
}

void CFastPractice::PausePracticeRecording()
{
	if(!m_Enabled || !GameClient()->m_LocalWorld.TrackActive() || m_RecordingPaused)
	{
		EchoPractice("practice recording is not running");
		return;
	}
	m_RecordingPaused = true;
	// 暂停期间 tee 照常跑，采样必须停：继续录会把空洞连成一条直线，回放里是假轨迹。
	EchoPractice("practice recording paused (resuming starts a new take)");
}

void CFastPractice::StopPracticeRecording()
{
	if(!m_Enabled || !GameClient()->m_LocalWorld.TrackActive())
	{
		EchoPractice("practice recording is not running");
		return;
	}
	// 先落盘再结束：EndTrack 会清空轨迹缓冲，顺序反了就什么都存不下来。
	// 保存失败（线路太短或录音器忙）时不清空轨迹，暂停住让用户接着跑再结束。
	if(!SavePracticeGhost(""))
	{
		m_RecordingPaused = true;
		return;
	}
	m_RecordingPaused = false;
	GameClient()->m_LocalWorld.EndTrack();
	EchoPractice("practice recording stopped and saved");
}

void CFastPractice::PublishPracticeWorldState()
{
	ResetAttackTickHistory();
	if(CCharacter *pMain = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pMain);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	ReleaseBufferedActionInputState();

	// Keep camera interpolation coherent after hard reset.
	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableLocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableDummyClientId))
		{
			GameClient()->m_aClients[m_EnableDummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[m_EnableDummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();
}

void CFastPractice::PrepareInputForSend(int *pData, int Size, bool Dummy)
{
	if(!pData || Size < (int)sizeof(CNetObj_PlayerInput))
		return;
	auto *pInput = reinterpret_cast<CNetObj_PlayerInput *>(pData);
	const int Slot = g_Config.m_ClDummy ^ (int)Dummy;
	if(!m_Enabled)
	{
		if(Slot >= 0 && Slot < NUM_DUMMIES && m_aPostPracticeInputSuppressTicks[Slot] > 0)
		{
			SuppressActionInput(*pInput, m_aServerReleasedFireStates[Slot]);
			m_aPostPracticeInputSuppressTicks[Slot]--;
		}
		return;
	}

	// Keep the real server stream locked to the exact input state that was
	// active when fast practice was opened. Practice inputs must not move it.
	if(Slot >= 0 && Slot < NUM_DUMMIES && m_aHasServerLockedInputs[Slot])
		*pInput = m_aServerLockedInputs[Slot];
	else
		NeutralizeInput(*pInput);
}

void CFastPractice::TrackPracticeTileFeedback(int ClientId, CCharacter *pChar, const vec2 &BeforePos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;

	const vec2 BeforeTickPos = BeforePos;
	const vec2 AfterPos = pChar->Core()->m_Pos;
	const bool PathContainsTeleport = PracticePathContainsTeleport(Collision(), BeforePos, AfterPos);
	const bool WasInDeath = IsCharacterTouchingDeathTile(Collision(), BeforeTickPos, pChar->GetProximityRadius());
	const bool IsInDeath = IsCharacterTouchingDeathTile(Collision(), AfterPos, pChar->GetProximityRadius());
	const SPracticeTileFeedbackDecision FeedbackDecision = EvaluatePracticeTileFeedback(PathContainsTeleport, WasInDeath, IsInDeath, distance(BeforePos, AfterPos));
	if(FeedbackDecision.m_RecordTeleport)
	{
		StoreLastDeathPosition(ClientId, BeforePos);
		StoreLastTeleport(ClientId, AfterPos);
	}

	if(!FeedbackDecision.m_RecordDeath)
		return;

	StoreLastDeathPosition(ClientId, AfterPos);
	if(FeedbackDecision.m_PlayDeathFeedback && !GameClient()->m_SuppressEvents)
		GameClient()->m_Effects.PlayerDeath(AfterPos, ClientId, 1.0f);
	if(FeedbackDecision.m_PlayDeathFeedback && g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
		if(GetQmFocusModeDecisions().m_PlayDeathOrSpawnSound)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_DIE, 1.0f, AfterPos);
}

bool CFastPractice::AdvanceBaseWorldToTick(int TargetTick, int LocalClientId, int DummyClientId)
{
	if(!m_PracticeWorldInitialized)
		return false;

	if(m_PracticeBaseWorld.GameTick() > TargetTick)
		return InitPracticeWorld();

	int LocalInputConn = -1;
	int DummyInputConn = -1;
	if(!ResolveParticipantInputs(LocalClientId, DummyClientId, LocalInputConn, DummyInputConn))
		return false;

	const int FirstBaseTick = m_PracticeBaseWorld.GameTick() + 1;
	CQmAxiomQuadTimeScope BaseQuadTime(Collision(), m_PracticeBaseWorld.GameTick(), Client()->GameTickSpeed());
	for(int Tick = m_PracticeBaseWorld.GameTick() + 1; Tick <= TargetTick; Tick++)
	{
		Collision()->SetQmAxiomQuadTick(Tick, Client()->GameTickSpeed());
		CCharacter *pLocalChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
		CCharacter *pDummyChar = m_RequireDummy ? m_PracticeBaseWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
			return false;

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInputExact(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInputExact(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalPracticeInput = {};
		CNetObj_PlayerInput DummyPracticeInput = {};
		// 该 tick 没有输入记录时沿用角色上一 tick 的输入。GetInput 会回退到更早
		// 的一条记录，多 tick 追赶时同一输入被重复消费，钩子边沿会重复触发或倒退。
		LocalPracticeInput = pInputData ? *pInputData : *pLocalChar->LatestInput();
		pInputData = &LocalPracticeInput;
		if(pDummyChar)
		{
			DummyPracticeInput = pDummyInputData ? *pDummyInputData : *pDummyChar->LatestInput();
			pDummyInputData = &DummyPracticeInput;
		}

		const bool SuppressTransitionTick = Tick == FirstBaseTick && m_SuppressFireOnNextPredictTick;
		const bool SuppressCooldownTick = Tick >= FirstBaseTick && m_InputSuppressTicks > 0;
		if(SuppressTransitionTick || SuppressCooldownTick)
		{
			// 保持上一 tick 的按下状态，不能用"强制松开"压制：松开再恢复会被
			// CountInput 记成一次新的按下，传送落地后立刻挥一下锤子。
			if(pInputData)
				SuppressActionInput(*pInputData, pLocalChar->LatestInput()->m_Fire);
			if(pDummyInputData)
				SuppressActionInput(*pDummyInputData, pDummyChar->LatestInput()->m_Fire);
			if(m_InputSuppressTicks > 0)
				m_InputSuppressTicks--;
			m_SuppressFireOnNextPredictTick = false;
		}

		if(pInputData)
			FilterPracticeInput(*pInputData, LocalInputConn, true);
		if(pDummyInputData)
			FilterPracticeInput(*pDummyInputData, DummyInputConn, true);

		if(pDummyChar && g_Config.m_ClDummyHammer)
		{
			if(!pDummyInputData)
			{
				DummyPracticeInput = {};
				pDummyInputData = &DummyPracticeInput;
			}
			const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
			pDummyInputData->m_TargetX = (int)Dir.x;
			pDummyInputData->m_TargetY = (int)Dir.y;
			if(pDummyInputData->m_TargetX == 0 && pDummyInputData->m_TargetY == 0)
				pDummyInputData->m_TargetY = -1;
		}

		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		const vec2 LocalPosBeforeTick = pLocalChar->Core()->m_Pos;
		const vec2 DummyPosBeforeTick = pDummyChar ? pDummyChar->Core()->m_Pos : vec2(0.0f, 0.0f);

		GameClient()->ApplyPreInputs(Tick, true, m_PracticeBaseWorld);

		m_PracticeBaseWorld.m_GameTick = Tick;
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		GameClient()->ApplyPreInputs(Tick, false, m_PracticeBaseWorld);

		m_PracticeBaseWorld.Tick();

		TrackPracticeTileFeedback(LocalClientId, pLocalChar, LocalPosBeforeTick);
		if(pDummyChar)
			TrackPracticeTileFeedback(DummyClientId, pDummyChar, DummyPosBeforeTick);

		TrackSafeRescuePosition(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackSafeRescuePosition(DummyClientId, pDummyChar);
	}

	return true;
}

bool CFastPractice::OverridePredict()
{
	if(!m_Enabled)
		return false;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return false;
	}
	if(IsPracticeServerPauseActive())
	{
		FreezePracticeWorldForServerPause();
		return false;
	}
	if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
	{
		// Keep practice mode state, but don't replace predicted world while spectating.
		m_PracticeWorldInitialized = false;
		GameClient()->m_PredictedDummyId = -1;
		return false;
	}
	if(!m_RequireDummy)
		TryAttachDummyFromSnapshot();

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return false;
	}

	int LocalInputConn = -1;
	int DummyInputConn = -1;
	if(!ResolveParticipantInputs(LocalClientId, DummyClientId, LocalInputConn, DummyInputConn))
	{
		Disable();
		return false;
	}

	const bool InputMappingChanged = LocalClientId != m_LastResolvedLocalClientId ||
					 DummyClientId != m_LastResolvedDummyClientId ||
					 LocalInputConn != m_LastResolvedLocalInputConn ||
					 DummyInputConn != m_LastResolvedDummyInputConn;
	const bool DummyRoleSwap = m_LastResolvedLocalClientId >= 0 &&
				   m_LastResolvedDummyClientId >= 0 &&
				   LocalClientId == m_LastResolvedDummyClientId &&
				   DummyClientId == m_LastResolvedLocalClientId &&
				   LocalInputConn == m_LastResolvedLocalInputConn &&
				   DummyInputConn == m_LastResolvedDummyInputConn;
	if(InputMappingChanged)
	{
		if(g_Config.m_Debug)
			dbg_msg("fast_practice", "role/input remap: local_id=%d dummy_id=%d local_conn=%d dummy_conn=%d",
				LocalClientId, DummyClientId, LocalInputConn, DummyInputConn);
		if(!DummyRoleSwap && !GameClient()->m_IsDummySwapping)
		{
			m_SuppressFireOnNextPredictTick = true;
			m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
			ReleaseBufferedActionInputState();
		}
	}

	GameClient()->m_PredictedDummyId = DummyClientId;

	if(!m_PracticeWorldInitialized && !InitPracticeWorld())
	{
		Disable();
		return false;
	}

	SyncPracticeWorldConfig();

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);

	CCharacter *pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId);
	CCharacter *pDummyChar = m_RequireDummy ? GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId) : nullptr;
	if(!pLocalChar || (m_RequireDummy && !pDummyChar))
	{
		Disable();
		return false;
	}

	const int BaseGameTick = m_PracticeBaseWorld.GameTick();
	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
	const int FinalTickRegular = PredTick;
	const int FinalTickSelf = FinalTickRegular;
	const int FinalTickOthers = FinalTickSelf;

	const int LocalTickSlot = LocalInputConn;
	const int DummyTickSlot = m_RequireDummy ? DummyInputConn : (LocalTickSlot ^ 1);

	if(FinalTickSelf <= BaseGameTick)
	{
		GameClient()->m_PredictedChar = pLocalChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		if(pDummyChar)
		{
			GameClient()->m_aClients[DummyClientId].m_Predicted = pDummyChar->GetCore();
		}
		GameClient()->m_PredictedTick = BaseGameTick;
		return true;
	}

	CQmAxiomQuadTimeScope PredictedQuadTime(Collision(), BaseGameTick, Client()->GameTickSpeed());
	for(int Tick = BaseGameTick + 1; Tick <= FinalTickSelf; Tick++)
	{
		Collision()->SetQmAxiomQuadTick(Tick, Client()->GameTickSpeed());
		std::vector<STrackedProjectile> vTrackedProjectilesBefore;
		std::vector<STrackedProjectile> vTrackedProjectilesAfter;

		pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId);
		pDummyChar = m_RequireDummy ? GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		{
			Disable();
			return false;
		}

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PrevPredictedWorld.CopyWorldClean(&GameClient()->m_PredictedWorld);
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_PrevPredicted = pChar->GetCore();
		}
		if(Tick == PredTick)
		{
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
			if(pDummyChar)
				GameClient()->m_aClients[DummyClientId].m_PrevPredicted = pDummyChar->GetCore();
		}

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInputExact(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInputExact(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalPracticeInput = {};
		CNetObj_PlayerInput DummyPracticeInput = {};
		// 该 tick 没有输入记录时沿用角色上一 tick 的输入。GetInput 会回退到更早
		// 的一条记录，多 tick 追赶时同一输入被重复消费，钩子边沿会重复触发或倒退。
		LocalPracticeInput = pInputData ? *pInputData : *pLocalChar->LatestInput();
		pInputData = &LocalPracticeInput;
		if(pDummyChar)
		{
			DummyPracticeInput = pDummyInputData ? *pDummyInputData : *pDummyChar->LatestInput();
			pDummyInputData = &DummyPracticeInput;
		}

		const bool SuppressTransitionTick = Tick == BaseGameTick + 1 && (m_SuppressFireOnNextPredictTick || GameClient()->m_IsDummySwapping);
		const bool SuppressCooldownTick = Tick >= BaseGameTick + 1 && m_InputSuppressTicks > 0;
		if(SuppressTransitionTick || SuppressCooldownTick)
		{
			// 同上：压制只清武器切换请求，开火状态必须沿用上一 tick，
			// 否则松手边沿会被当成新的一次开火。
			if(pInputData)
				SuppressActionInput(*pInputData, pLocalChar->LatestInput()->m_Fire);
			if(pDummyInputData)
				SuppressActionInput(*pDummyInputData, pDummyChar->LatestInput()->m_Fire);
			if(m_InputSuppressTicks > 0)
				m_InputSuppressTicks--;
			m_SuppressFireOnNextPredictTick = false;
		}

		if(pInputData)
			FilterPracticeInput(*pInputData, LocalInputConn, true);
		if(pDummyInputData)
			FilterPracticeInput(*pDummyInputData, DummyInputConn, true);

		if(pDummyChar && g_Config.m_ClDummyHammer)
		{
			if(!pDummyInputData)
			{
				DummyPracticeInput = {};
				pDummyInputData = &DummyPracticeInput;
			}
			const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
			pDummyInputData->m_TargetX = (int)Dir.x;
			pDummyInputData->m_TargetY = (int)Dir.y;
			if(pDummyInputData->m_TargetX == 0 && pDummyInputData->m_TargetY == 0)
				pDummyInputData->m_TargetY = -1;
		}

		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		pLocalChar->m_CanMoveInFreeze = g_Config.m_ClPredictFreeze == 2 && PredTick - 1 - PredTick % 2 <= Tick;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		GameClient()->ApplyPreInputs(Tick, true, GameClient()->m_PredictedWorld);

		GameClient()->m_PredictedWorld.m_GameTick = Tick;
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		GameClient()->ApplyPreInputs(Tick, false, GameClient()->m_PredictedWorld);

		CollectTrackedProjectiles(GameClient()->m_PredictedWorld, LocalClientId, DummyClientId, vTrackedProjectilesBefore);
		// 机关炮在实体 tick 之前发射，新生成的 plasma 同一 tick 就开始移动。
		TickPracticeGuns(GameClient()->m_PredictedWorld, Tick);
		GameClient()->m_PredictedWorld.Tick();
		// 采样分叉时空里本体与分身的这一 tick（多轨），连同本 tick 的交互事件，
		// 供 /pg 存成 demo 式可回看的幽灵。Rank 1 幽灵是另一个平行时空，只参与渲染。
		RecordPracticeFrames(GameClient()->m_PredictedWorld, Tick, LocalClientId, DummyClientId);
		RecordPracticeEvents(GameClient()->m_PredictedWorld, Tick);
		RecordPracticeSwitchState(GameClient()->m_PredictedWorld, Tick);

		TrackSafeRescuePosition(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackSafeRescuePosition(DummyClientId, pDummyChar);

		CollectTrackedProjectiles(GameClient()->m_PredictedWorld, LocalClientId, DummyClientId, vTrackedProjectilesAfter);
		for(const auto &TrackedProj : vTrackedProjectilesBefore)
		{
			const bool StillExists = std::any_of(vTrackedProjectilesAfter.begin(), vTrackedProjectilesAfter.end(), [&](const STrackedProjectile &Candidate) {
				return SameProjectile(TrackedProj, Candidate);
			});
			if(StillExists)
				continue;

			const int TickSpeed = Client()->GameTickSpeed();
			const int TuneZone = std::clamp(TrackedProj.m_TuneZone, 0, NUM_TUNEZONES - 1);
			const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
			vec2 PrevPos = CalcTrackedProjectilePos(TrackedProj, Tick - 1, TickSpeed, pTuning);
			vec2 CurPos = CalcTrackedProjectilePos(TrackedProj, Tick, TickSpeed, pTuning);
			vec2 ImpactPos = CurPos;
			Collision()->IntersectLine(PrevPos, CurPos, &ImpactPos, nullptr);

			if(IsTrackedProjectileExplosive(TrackedProj))
			{
				// DDNet 预测世界会为爆炸创建标准预测事件，由统一消费路径播放。
				// 非 DDNet 世界没有该事件，保留练习模式原有的即时反馈。
				if(!GameClient()->m_PredictedWorld.m_WorldConfig.m_IsDDRace)
				{
					if(!GameClient()->m_SuppressEvents)
						GameClient()->m_Effects.Explosion(ImpactPos, 1.0f);
					if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
						GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_GRENADE_EXPLODE, 1.0f, ImpactPos);
				}
			}
			else
			{
				AddProjectileHitEffect(GameClient(), ImpactPos, CurPos - PrevPos);
			}
		}

		// 快速练习接管预测循环，当前 tick 产生的事件必须在这里统一消费，
		// 这样开火声音和锤击反馈才能与普通预测共用同一份确认去重表。
		if(!GameClient()->m_SuppressEvents)
			GameClient()->HandlePredictedEvents(Tick);

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_Predicted = pChar->GetCore();
		}
		if(Tick == PredTick)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
			if(pDummyChar)
				GameClient()->m_aClients[DummyClientId].m_Predicted = pDummyChar->GetCore();
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
			{
				GameClient()->m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
				GameClient()->m_aClients[i].m_aPredTick[Tick % 200] = Tick;
			}

		if(Tick > GameClient()->m_aLastNewPredictedTick[LocalTickSlot] && Tick <= FinalTickRegular)
		{
			GameClient()->m_aLastNewPredictedTick[LocalTickSlot] = Tick;
			GameClient()->m_NewPredictedTick = true;
			const vec2 Pos = pLocalChar->Core()->m_Pos;
			const int Events = pLocalChar->Core()->m_TriggeredEvents;
			if(!GameClient()->m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
				{
					GameClient()->m_aLastPredictedAirJumpTick[LocalTickSlot] = Tick;
					GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);
				}
			if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
			{
				if(Events & COREEVENT_GROUND_JUMP)
					if(GetQmFocusModeDecisions().m_AirJump.m_PlaySound)
						GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_GROUND)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_HIT_NOHOOK)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
			}
		}

		if(pDummyChar && Tick > GameClient()->m_aLastNewPredictedTick[DummyTickSlot])
		{
			GameClient()->m_aLastNewPredictedTick[DummyTickSlot] = Tick;
			const vec2 Pos = pDummyChar->Core()->m_Pos;
			const int Events = pDummyChar->Core()->m_TriggeredEvents;
			if(!GameClient()->m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
				{
					GameClient()->m_aLastPredictedAirJumpTick[DummyTickSlot] = Tick;
					GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);
				}
			if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
			{
				if(Events & COREEVENT_GROUND_JUMP)
					if(GetQmFocusModeDecisions().m_AirJump.m_PlaySound)
						GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_GROUND)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_HIT_NOHOOK)
					GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
			}
		}
	}

	m_PracticeBaseWorld.CopyWorldClean(&GameClient()->m_PredictedWorld);
	GameClient()->m_PredictedTick = ApplyVisualFastInputPrediction(FinalTickRegular, LocalClientId, DummyClientId, LocalInputConn, DummyInputConn);
	if(GameClient()->m_NewPredictedTick)
		GameClient()->m_Ghost.OnNewPredictedSnapshot();
	m_LastResolvedLocalClientId = LocalClientId;
	m_LastResolvedDummyClientId = DummyClientId;
	m_LastResolvedLocalInputConn = LocalInputConn;
	m_LastResolvedDummyInputConn = DummyInputConn;

	return true;
}

int CFastPractice::ApplyVisualFastInputPrediction(int FinalTickRegular, int LocalClientId, int DummyClientId, int LocalInputConn, int DummyInputConn)
{
	const float FastInputOffsetTicks = EffectiveFastInputOffsetTicks(GameClient());
	const int FastInputTicks = FastInputPredictionTicks(FastInputOffsetTicks);
	if(FastInputTicks <= 0)
		return FinalTickRegular;

	CGameWorld VisualWorld;
	VisualWorld.CopyWorldClean(&m_PracticeBaseWorld);

	CCharacter *pLocalChar = VisualWorld.GetCharacterById(LocalClientId);
	CCharacter *pDummyChar = m_RequireDummy ? VisualWorld.GetCharacterById(DummyClientId) : nullptr;
	if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		return FinalTickRegular;

	const int FinalTickSelf = FinalTickRegular + FastInputTicks;
	int FinalTickOthers = FinalTickSelf;
	if(!EffectiveFastInputOthers(GameClient()))
		FinalTickOthers = FinalTickRegular;

	const auto ResolveInputSlotByClientId = [&](int ClientId, int FallbackSlot) {
		for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
		{
			if(GameClient()->m_aLocalIds[Slot] == ClientId)
				return Slot;
		}
		return FallbackSlot;
	};
	const int LocalTee = ResolveInputSlotByClientId(LocalClientId, g_Config.m_ClDummy);
	const int DummyTee = ResolveInputSlotByClientId(DummyClientId, LocalTee ^ 1);

	CQmAxiomQuadTimeScope VisualQuadTime(Collision(), FinalTickRegular, Client()->GameTickSpeed());
	for(int Tick = FinalTickRegular + 1; Tick <= FinalTickSelf; Tick++)
	{
		Collision()->SetQmAxiomQuadTick(Tick, Client()->GameTickSpeed());
		pLocalChar = VisualWorld.GetCharacterById(LocalClientId);
		pDummyChar = m_RequireDummy ? VisualWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
			return FinalTickRegular;

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedWorld.CopyWorldClean(&VisualWorld);
			GameClient()->m_PrevPredictedWorld.CopyWorldClean(&VisualWorld);
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_PrevPredicted = pChar->GetCore();
		}

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalFastInput = GameClient()->m_Controls.m_aFastInput[LocalTee];
		CNetObj_PlayerInput DummyFastInput = {};
		CNetObj_PlayerInput DummyVisualInput = {};
		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		pInputData = &LocalFastInput;
		FilterPracticeInput(*pInputData, LocalInputConn, false);
		if(pDummyInputData)
		{
			DummyVisualInput = *pDummyInputData;
			pDummyInputData = &DummyVisualInput;
		}
		if(pDummyChar && GameClient()->GetDummyFastInput(DummyFastInput, pDummyInputData, pDummyChar, LocalTee, DummyTee))
			pDummyInputData = &DummyFastInput;
		if(pDummyInputData)
			FilterPracticeInput(*pDummyInputData, DummyInputConn, false);
		if(pDummyChar && g_Config.m_ClDummyHammer)
		{
			if(!pDummyInputData || pDummyInputData != &DummyFastInput)
			{
				DummyFastInput = pDummyInputData ? *pDummyInputData : CNetObj_PlayerInput{};
				pDummyInputData = &DummyFastInput;
			}
			const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
			pDummyInputData->m_TargetX = (int)Dir.x;
			pDummyInputData->m_TargetY = (int)Dir.y;
			if(pDummyInputData->m_TargetX == 0 && pDummyInputData->m_TargetY == 0)
				pDummyInputData->m_TargetY = -1;
		}
		pLocalChar->m_CanMoveInFreeze = g_Config.m_ClPredictFreeze == 2 && FinalTickRegular - 1 - FinalTickRegular % 2 <= Tick;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;

		const bool TempPredEventState = VisualWorld.m_WorldConfig.m_PredictEvents;
		VisualWorld.m_WorldConfig.m_PredictEvents = false;

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		GameClient()->ApplyPreInputs(Tick, true, VisualWorld);

		VisualWorld.m_GameTick = Tick;
		pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		GameClient()->ApplyPreInputs(Tick, false, VisualWorld);

		VisualWorld.Tick();
		VisualWorld.m_WorldConfig.m_PredictEvents = TempPredEventState;

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_Predicted = pChar->GetCore();
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
			{
				GameClient()->m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
				GameClient()->m_aClients[i].m_aPredTick[Tick % 200] = Tick;
			}
	}

	return FinalTickSelf;
}

void CFastPractice::UpdateGhostForClientId(int ClientId, SGhostData &Ghost)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		Ghost = SGhostData{};
		return;
	}

	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		const CNetObj_Character &Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		Ghost.m_Valid = true;
		Ghost.m_ClientId = ClientId;
		Ghost.m_Pos = vec2(Char.m_X, Char.m_Y);
		Ghost.m_Direction = vec2(Char.m_Direction == 0 ? 1.0f : (float)Char.m_Direction, 0.0f);
		Ghost.m_Angle = Char.m_Angle;
		Ghost.m_Weapon = Char.m_Weapon;
		Ghost.m_HookState = Char.m_HookState;
		Ghost.m_HookPos = vec2(Char.m_HookX, Char.m_HookY);
		return;
	}

	if(m_Enabled)
	{
		if(CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(ClientId))
		{
			const CCharacterCore *pCore = pChar->Core();
			Ghost.m_Valid = true;
			Ghost.m_ClientId = ClientId;
			Ghost.m_Pos = pCore->m_Pos;
			Ghost.m_Direction = vec2(pCore->m_Direction == 0 ? 1.0f : (float)pCore->m_Direction, 0.0f);
			Ghost.m_Angle = pCore->m_Angle;
			Ghost.m_Weapon = pCore->m_ActiveWeapon;
			Ghost.m_HookState = pCore->m_HookState;
			Ghost.m_HookPos = pCore->m_HookPos;
			return;
		}
	}

	Ghost = SGhostData{};
}

void CFastPractice::UpdateGhostData()
{
	if(m_Enabled)
	{
		UpdateGhostForClientId(m_EnableLocalClientId, m_MainGhost);
		UpdateGhostForClientId(m_EnableDummyClientId, m_DummyGhost);
	}
	else
	{
		UpdateGhostForClientId(GameClient()->m_aLocalIds[0], m_MainGhost);
		UpdateGhostForClientId(GameClient()->m_aLocalIds[1], m_DummyGhost);
	}
}

void CFastPractice::OnNewSnapshot()
{
	UpdateGhostData();

	if(!m_Enabled)
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return;
	}

	if(IsPracticeServerPauseActive())
	{
		FreezePracticeWorldForServerPause();
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
		GameClient()->m_PredictedDummyId = -1;
	else
	{
		if(!m_RequireDummy)
			TryAttachDummyFromSnapshot();
		GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	}
}

void CFastPractice::RenderGhost(const SGhostData &Ghost, float Alpha) const
{
	if(!Ghost.m_Valid || Ghost.m_ClientId < 0 || Ghost.m_ClientId >= MAX_CLIENTS)
		return;
	if(!GameClient()->m_aClients[Ghost.m_ClientId].m_Active)
		return;

	CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Ghost.m_ClientId].m_RenderInfo;
	vec2 Dir = Ghost.m_Direction;
	if(length(Dir) < 0.001f)
		Dir = vec2(1.0f, 0.0f);
	else
		Dir = normalize(Dir);

	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, Dir, Ghost.m_Pos, Alpha);
}

void CFastPractice::OnRender()
{
	if(!m_Enabled)
		return;

	const auto RenderRealMarker = [&](const SGhostData &Ghost, float Alpha) {
		if(!Ghost.m_Valid)
			return;
		if(CCharacter *pPracticeChar = m_PracticeBaseWorld.GetCharacterById(Ghost.m_ClientId))
		{
			// Skip overlay when the real and local worlds are visually on top of each other.
			if(distance(pPracticeChar->Core()->m_Pos, Ghost.m_Pos) < 10.0f)
				return;
		}
		RenderGhost(Ghost, Alpha);
	};

	RenderRealMarker(m_MainGhost, 0.28f);
	RenderRealMarker(m_DummyGhost, 0.22f);
}

void CFastPractice::EchoPractice(const char *pFormat, ...) const
{
	char aBody[256];
	va_list Args;
	va_start(Args, pFormat);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	str_format_v(aBody, sizeof(aBody), pFormat, Args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	va_end(Args);

	char aMsg[320];
	str_format(aMsg, sizeof(aMsg), "[practice local] %s", aBody);
	GameClient()->Echo(aMsg);
}

bool CFastPractice::ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs)
{
	vArgs.clear();
	if(!pLine)
		return false;

	const char *p = pLine;
	while(*p)
	{
		while(*p == ' ' || *p == '\t')
			++p;
		if(*p == '\0')
			break;

		std::string Token;
		bool InQuotes = false;
		if(*p == '"')
		{
			InQuotes = true;
			++p;
		}

		while(*p)
		{
			if(InQuotes)
			{
				if(*p == '"')
				{
					++p;
					break;
				}
				if(*p == '\\' && p[1] == '"')
				{
					Token.push_back('"');
					p += 2;
					continue;
				}
				Token.push_back(*p++);
				continue;
			}

			if(*p == ' ' || *p == '\t')
				break;
			Token.push_back(*p++);
		}

		if(!Token.empty())
			vArgs.push_back(Token);

		while(*p == ' ' || *p == '\t')
			++p;
	}

	return !vArgs.empty();
}

bool CFastPractice::ParseCoordinateToken(const char *pToken, float Base, float &Out)
{
	if(!pToken || pToken[0] == '\0')
		return false;

	const bool Relative = pToken[0] == '~';
	const char *pValue = Relative ? pToken + 1 : pToken;
	float Parsed = 0.0f;
	if(pValue[0] != '\0' && !str_tofloat(pValue, &Parsed))
		return false;

	if(std::isnan(Parsed) || std::isinf(Parsed))
		return false;

	Out = (Relative ? Base : 0.0f) + Parsed * 32.0f;
	return true;
}

int CFastPractice::FindClientByName(const char *pName) const
{
	if(!pName || pName[0] == '\0')
		return -1;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			continue;
		if(str_comp(GameClient()->m_aClients[ClientId].m_aName, pName) == 0)
			return ClientId;
	}

	return -1;
}

void CFastPractice::NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_Jumped = 0;
	Core.m_JumpedTotal = 0;
	Core.m_HookState = HOOK_RETRACTED;
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	Core.m_HookTick = 0;
	Core.m_NewHook = false;
	Core.SetHookedPlayer(-1);
	Core.m_AttachedPlayers.clear();
	if(!KeepFreezeFlags)
	{
		Core.m_DeepFrozen = false;
		Core.m_LiveFrozen = false;
		Core.m_IsInFreeze = false;
		Core.m_FreezeEnd = 0;
	}
	pChar->SetCore(Core);
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
	pChar->ResetHook();
	pChar->ResetVelocity();
	if(!KeepFreezeFlags)
	{
		pChar->Unfreeze();
		pChar->m_FreezeTime = 0;
	}
	pChar->m_CanMoveInFreeze = false;
	pChar->m_FrozenLastTick = false;

	CNetObj_PlayerInput NeutralInput = {};
	NeutralInput.m_TargetY = -1;
	// SetInput 会连"最新输入"一起覆盖。开火状态一旦被清零，下一 tick 恢复按住
	// 就会被 CountInput 记成一次新的按下——传送落地后莫名挥一锤。
	NeutralInput.m_Fire = pChar->LatestInput()->m_Fire;
	pChar->SetInput(&NeutralInput);
	pChar->ResetInput();
}

void CFastPractice::NormalizeDefaultPracticeWeapons(CCharacter *pChar, bool HasExplicitWeaponFlags) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	if(!HasExplicitWeaponFlags)
	{
		Core.m_aWeapons[WEAPON_HAMMER].m_Got = true;
		Core.m_aWeapons[WEAPON_GUN].m_Got = true;
	}
	if(Core.m_aWeapons[WEAPON_HAMMER].m_Got)
		Core.m_aWeapons[WEAPON_HAMMER].m_Ammo = -1;
	if(Core.m_aWeapons[WEAPON_GUN].m_Got)
		Core.m_aWeapons[WEAPON_GUN].m_Ammo = -1;
	if(Core.m_ActiveWeapon < WEAPON_HAMMER || Core.m_ActiveWeapon >= NUM_WEAPONS || !Core.m_aWeapons[Core.m_ActiveWeapon].m_Got)
	{
		if(Core.m_aWeapons[WEAPON_GUN].m_Got)
			Core.m_ActiveWeapon = WEAPON_GUN;
		else if(Core.m_aWeapons[WEAPON_HAMMER].m_Got)
			Core.m_ActiveWeapon = WEAPON_HAMMER;
		else
		{
			for(int Weapon = WEAPON_SHOTGUN; Weapon < NUM_WEAPONS; Weapon++)
			{
				if(Core.m_aWeapons[Weapon].m_Got)
				{
					Core.m_ActiveWeapon = Weapon;
					break;
				}
			}
		}
	}
	pChar->SetCore(Core);
}

void CFastPractice::NormalizeWeaponSelectionInput(CCharacter *pChar) const
{
	if(!pChar)
		return;

	CNetObj_PlayerInput Input = *pChar->LatestInput();
	Input.m_WantedWeapon = 0;
	Input.m_NextWeapon = 0;
	Input.m_PrevWeapon = 0;
	pChar->SetInput(&Input);
}

void CFastPractice::TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	Core.m_Pos = Pos;
	pChar->SetCore(Core);
	pChar->m_Pos = Pos;
	pChar->m_PrevPos = Pos;
	pChar->m_PrevPrevPos = Pos;
	NormalizeCharacterAfterReset(pChar, false);
}

void CFastPractice::StoreLastTeleport(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastTeleport = true;
	State.m_LastTeleportPos = Pos;
}

void CFastPractice::StoreLastDeathPosition(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastDeath = true;
	State.m_LastDeathPos = Pos;
}

bool CFastPractice::IsSafeRescueTile(int Tile) const
{
	return Tile != TILE_DEATH &&
	       Tile != TILE_FREEZE &&
	       Tile != TILE_DFREEZE &&
	       Tile != TILE_LFREEZE;
}

bool CFastPractice::IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const
{
	const float HalfSize = std::max(6.0f, ProximityRadius * 0.5f);
	if(Collision()->TestBox(Pos, vec2(HalfSize * 2.0f, HalfSize * 2.0f)))
		return false;

	const vec2 aSamplePoints[] = {
		Pos,
		Pos + vec2(-HalfSize, -HalfSize),
		Pos + vec2(HalfSize, -HalfSize),
		Pos + vec2(-HalfSize, HalfSize),
		Pos + vec2(HalfSize, HalfSize),
	};
	for(const vec2 &SamplePos : aSamplePoints)
	{
		const int X = round_to_int(SamplePos.x);
		const int Y = round_to_int(SamplePos.y);
		if(!IsSafeRescueTile(Collision()->GetTile(X, Y)) ||
			!IsSafeRescueTile(Collision()->GetFrontTile(X, Y)))
		{
			return false;
		}
	}

	const bool Grounded =
		Collision()->CheckPoint(Pos.x + HalfSize, Pos.y + HalfSize + 5.0f) ||
		Collision()->CheckPoint(Pos.x - HalfSize, Pos.y + HalfSize + 5.0f);
	return Grounded;
}

bool CFastPractice::IsInFreezePickupRange(CCharacter *pChar) const
{
	// 对齐服务器 CCharacter::TrySetRescue：站在冻结拾取物的作用范围内时不能刷新救援点。
	// 服务器只在不处于冻结区域时才扫描，调用方已经先判断了 m_IsInFreeze。
	if(!pChar || pChar->Core()->m_IsInFreeze)
		return false;

	CEntity *apEnts[9];
	const int Num = pChar->GameWorld()->FindEntities(pChar->m_Pos,
		pChar->GetProximityRadius() + CPickup::ms_CollisionExtraSize, apEnts, 9, CGameWorld::ENTTYPE_PICKUP);
	for(int i = 0; i < Num; i++)
	{
		if(static_cast<CPickup *>(apEnts[i])->Type() == POWERUP_FREEZE)
			return true;
	}
	return false;
}

void CFastPractice::CapturePracticeRescueTee(const CCharacter *pChar, int NowTick, SQmPracticeSavedTee &Tee) const
{
	if(!pChar)
		return;
	// 核心字段与服务器 CSaveTee 对应；角色侧字段单独补齐，Rescue() 不恢复的那部分
	//（速度、钩子、触发事件）由 QmApplyPracticeRescueCore 统一清空。
	QmCapturePracticeRescueCore(*pChar->Core(), NowTick, Tee);
	Tee.m_FreezeTime = pChar->m_FreezeTime;
	Tee.m_ReloadTimer = pChar->GetReloadTimer();
	Tee.m_LastWeapon = pChar->GetLastWeapon();
	Tee.m_QueuedWeapon = pChar->GetQueuedWeapon();
	Tee.m_NinjaJetpack = pChar->m_NinjaJetpack;
	Tee.m_TuneZone = pChar->GetPureTuneZone();
}

void CFastPractice::ApplyPracticeRescueTee(CCharacter *pChar, const SQmPracticeSavedTee &Tee) const
{
	if(!pChar || !Tee.m_Valid)
		return;

	CCharacterCore Core = pChar->GetCore();
	QmApplyPracticeRescueCore(Core, pChar->GameWorld()->GameTick(), Tee);
	pChar->SetCore(Core);

	pChar->m_Pos = Tee.m_Pos;
	pChar->m_PrevPos = Tee.m_Pos;
	pChar->m_PrevPrevPos = Tee.m_Pos;
	pChar->m_FreezeTime = Tee.m_FreezeTime;
	pChar->m_NinjaJetpack = Tee.m_NinjaJetpack;
	pChar->SetReloadTimer(Tee.m_ReloadTimer);
	pChar->SetLastWeapon(Tee.m_LastWeapon);
	pChar->SetQueuedWeapon(Tee.m_QueuedWeapon);
	pChar->SetTuneZone(Tee.m_TuneZone);
	// 服务器 CSaveTee::Load 末尾用 SetSolo 同时更新角色核心与队伍核心。
	pChar->SetSolo(Tee.m_Solo);

	// 与 NormalizeCharacterAfterReset 相同的输入处理：保留开火按下状态，
	// 否则松手后的第一 tick 会被 CountInput 记成新的按下，落地就挥一锤。
	CNetObj_PlayerInput NeutralInput = {};
	NeutralInput.m_TargetY = -1;
	NeutralInput.m_Fire = pChar->LatestInput()->m_Fire;
	pChar->SetInput(&NeutralInput);
	pChar->ResetInput();
}

void CFastPractice::TrackSafeRescuePosition(int ClientId, CCharacter *pChar)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;
	// 对齐服务器 CCharacter::TrySetRescue(RESCUEMODE_AUTO)：救援点在
	// “不在冻结区域、非深度冻结、站在地面、不在冻结拾取物范围内”时每个 tick 覆盖。
	// 不看 m_FreezeTime——被普通冻结但落在普通平台上时服务器同样会刷新救援点，
	// 这里若跳过，/r 就会带人回溯到更早的位置，而不是就地解冻。
	const CCharacterCore *pCore = pChar->Core();
	if(!QmPracticeCanSetRescuePoint(pCore->m_IsInFreeze, pCore->m_DeepFrozen, pChar->IsGrounded(), IsInFreezePickupRange(pChar)))
		return;

	const vec2 Pos = pCore->m_Pos;
	auto &State = m_aPracticeCommandState[ClientId];
	// 服务器每个 tick 都覆盖救援点，/r 的落点就是最近一帧的合法位置。48px 去重只作用于
	// 给其它命令使用的历史列表，不能让它拖慢救援点本身，否则 /r 会偏离服务器语义。
	State.m_HasRescueAuto = true;
	State.m_RescueAutoPos = Pos;
	CapturePracticeRescueTee(pChar, pChar->GameWorld()->GameTick(), State.m_AutoTee);
	if(!State.m_vSafePositions.empty() && distance(State.m_vSafePositions.front(), Pos) < 48.0f)
		return;

	State.m_vSafePositions.push_front(Pos);
	if((int)State.m_vSafePositions.size() > SPracticeCommandState::MAX_SAFE_POSITIONS)
		State.m_vSafePositions.pop_back();
}

bool CFastPractice::FindNearestSafeRescuePosition(int ClientId, const vec2 &From, vec2 &OutPos) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const auto &State = m_aPracticeCommandState[ClientId];
	float BestDistance = std::numeric_limits<float>::max();
	bool Found = false;

	for(const vec2 &Pos : State.m_vSafePositions)
	{
		if(!IsSafeRescuePosition(Pos, CCharacterCore::PhysicalSize()))
			continue;

		const float Dist = distance(From, Pos);
		if(Dist < BestDistance)
		{
			BestDistance = Dist;
			OutPos = Pos;
			Found = true;
		}
	}

	return Found;
}

bool CFastPractice::FindFinishPosition(vec2 &OutPos) const
{
	if(Collision() == nullptr || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;

	const int NumTiles = Collision()->GetWidth() * Collision()->GetHeight();
	for(int Index = 0; Index < NumTiles; Index++)
	{
		if(Collision()->GetTileIndex(Index) == TILE_FINISH)
		{
			OutPos = Collision()->GetPos(Index);
			return true;
		}
	}
	for(int Index = 0; Index < NumTiles; Index++)
	{
		if(Collision()->GetFrontTileIndex(Index) == TILE_FINISH)
		{
			OutPos = Collision()->GetPos(Index);
			return true;
		}
	}
	return false;
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
	const vec2 CurrentPos = pChar->Core()->m_Pos;
	const auto SaveTeleportPos = [&](const vec2 &Pos) {
		StoreLastTeleport(LocalClientId, Pos);
		StoreLastDeathPosition(LocalClientId, CurrentPos);
	};
	const auto ApplyTeleport = [&](const vec2 &Pos) {
		SaveTeleportPos(Pos);
		// 传送会在轨迹上造成瞬移，先封存当前段再跳。
		OnPracticePositionJump();
		TeleportCharacter(pChar, Pos);
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
		EchoPractice("available commands: /r /back /rescuemode /restart /tp /teleport /tpxy /lasttp /tc /telecursor /totele /totelecp /tofinish /pg /solo /unsolo /deep /undeep /livefreeze /unlivefreeze /shotgun /grenade /laser /rifle /unshotgun /ungrenade /unlaser /unrifle /weapons /unweapons /addweapon /removeweapon /jetpack /unjetpack /infjump /uninfjump /setjumps /ninja /unninja /endless /unendless /invincible /collision /hookcollision /hitothers /practice /unpractice");
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
			State.m_RescueManualPos = pCore->m_Pos;
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
		if(!State.m_HasLastDeath)
		{
			EchoPractice("there is nowhere to go back to");
			return true;
		}
		ApplyTeleport(State.m_LastDeathPos);
		return true;
	}

	if(Cmd == "lasttp")
	{
		if(!State.m_HasLastTeleport)
		{
			EchoPractice("you haven't previously teleported");
			return true;
		}
		ApplyTeleport(State.m_LastTeleportPos);
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

		ApplyTeleport(vTeleOuts[0]);
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

void CFastPractice::ConSavePracticeGhost(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	if(!pSelf->m_Enabled)
	{
		pSelf->EchoPractice("save practice ghost: fast practice is not active");
		return;
	}
	pSelf->SavePracticeGhost(pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
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
	Console()->Register("practice_save_ghost", "?r[name]", CFGFLAG_CLIENT, ConSavePracticeGhost, this,
		"Save the current fast-practice line (all tees) as a replayable ghost group");
	Console()->Register("qm_local_world_dump", "", CFGFLAG_CLIENT, ConLocalWorldDump, this,
		"Dump the client-side world state (practice branch, tracks, switchers, view session)");
}
