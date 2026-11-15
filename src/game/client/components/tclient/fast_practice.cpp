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
#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/practice_cursor.h>
#include <game/client/components/qmclient/practice_predict_publish.h>
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
	// sv_plasma_per_sec 与 sv_plasma_range 是服务器端配置，不随协议下发，客户端无从
	// 得知当前服务器改成了多少。练习世界只能按 DDNet 默认值模拟机关炮；服务器改过
	// 这两项时，练习里的射速与射程会与真实世界不同，这是协议层面的已知边界。
	constexpr int PRACTICE_PLASMA_PER_SEC = 3;
	constexpr int PRACTICE_PLASMA_RANGE = 700;

	// 预测写回的 CGameClient 适配器：模板只认字段槽位，不依赖客户端类型。
	class CPracticePredictStateSink
	{
	public:
		explicit CPracticePredictStateSink(CGameClient *pGameClient) :
			m_pGameClient(pGameClient) {}

		CGameWorld &PredictedWorld() { return m_pGameClient->m_PredictedWorld; }
		CGameWorld &PrevPredictedWorld() { return m_pGameClient->m_PrevPredictedWorld; }
		CCharacterCore &PredictedChar() { return m_pGameClient->m_PredictedChar; }
		CCharacterCore &PredictedPrevChar() { return m_pGameClient->m_PredictedPrevChar; }
		CCharacterCore &ClientPredicted(int ClientId) { return m_pGameClient->m_aClients[ClientId].m_Predicted; }
		CCharacterCore &ClientPrevPredicted(int ClientId) { return m_pGameClient->m_aClients[ClientId].m_PrevPredicted; }
		vec2 &ClientPredPos(int ClientId, int Slot) { return m_pGameClient->m_aClients[ClientId].m_aPredPos[Slot]; }
		int &ClientPredTick(int ClientId, int Slot) { return m_pGameClient->m_aClients[ClientId].m_aPredTick[Slot]; }

	private:
		CGameClient *m_pGameClient;
	};

	// 两个预测循环共用的分身锤瞄准：让分身朝向本体所在方向挥锤。
	void ApplyDummyHammerAim(CCharacter *pLocalChar, CCharacter *pDummyChar, CNetObj_PlayerInput &DummyInput)
	{
		const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
		DummyInput.m_TargetX = (int)Dir.x;
		DummyInput.m_TargetY = (int)Dir.y;
		if(DummyInput.m_TargetX == 0 && DummyInput.m_TargetY == 0)
			DummyInput.m_TargetY = -1;
	}

	// 两个预测循环共用的输入落地顺序：冻结期可移动判定、cid 顺序、OnDirectInput。
	void ApplyPracticeActorInputs(CCharacter *pLocalChar, CCharacter *pDummyChar, CNetObj_PlayerInput *pLocalInput, CNetObj_PlayerInput *pDummyInput, int Tick, int FreezeReferenceTick)
	{
		pLocalChar->m_CanMoveInFreeze = g_Config.m_ClPredictFreeze == 2 && FreezeReferenceTick - 1 - FreezeReferenceTick % 2 <= Tick;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;

		const bool DummyFirst = pLocalInput && pDummyInput && pDummyChar && pDummyChar->GetCid() < pLocalChar->GetCid();
		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInput);
		if(pLocalInput)
			pLocalChar->OnDirectInput(pLocalInput);
		if(pDummyInput && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInput);
	}

	// 两个预测循环共用的单 tick 推进：PreInputs → 输入 → PreInputs。
	// 调用方负责额外步骤（机关炮、事件消费、采样）与最后的 World.Tick()。
	void StepPracticeWorld(CGameClient *pGameClient, CGameWorld &World, int Tick, CCharacter *pLocalChar, CCharacter *pDummyChar, CNetObj_PlayerInput *pLocalInput, CNetObj_PlayerInput *pDummyInput)
	{
		pGameClient->ApplyPreInputs(Tick, true, World);
		World.m_GameTick = Tick;
		if(pLocalInput)
			pLocalChar->OnPredictedInput(pLocalInput);
		if(pDummyInput)
			pDummyChar->OnPredictedInput(pDummyInput);
		pGameClient->ApplyPreInputs(Tick, false, World);
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
	m_PracticeRunTicks = 0;
	// 下一次进入练习是新的分支时间线，事件屏障从零开始。
	m_EventBarrierTick = -1;
	m_EnableLocalConn = -1;
	m_EnableLocalClientId = -1;
	m_EnableDummyClientId = -1;
	m_PracticeWorldInitialized = false;
	m_HasDummyAnchor = false;
	m_HasPendingLocalTee = false;
	m_PendingLocalTee = {};
	m_Recorder.Reset();
	m_SuppressFireOnNextPredictTick = false;
	m_InputSuppressTicks = 0;
	m_TransientFailureTicks = 0;
	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_LastResolvedLocalInputConn = -1;
	m_LastResolvedDummyInputConn = -1;
	m_InputLock.Reset();
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

	// 当前控制的连接优先，切换 cl_dummy 后命令与检查点跟着换人。
	return QmResolveControlledPracticeClientId(GameClient()->m_aLocalIds, g_Config.m_ClDummy != 0,
		m_EnableLocalClientId, m_EnableDummyClientId);
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

bool CFastPractice::TryRebindPracticeParticipants()
{
	// 服务器上死亡后重进会分配新的 client id。分支世界按 client id 找角色，
	// 旧 id 在重新 fork 的世界里已经不存在；不重绑的话本轮练习会被判失效，
	// 输入锁随之解除——练习中的输入瞬间打到真身上，表现为"莫名其妙切出去然后死了"。
	if(!m_Enabled)
		return false;

	// 角色身份跟着连接走：启用时本体来自哪条连接，重绑后仍是同一条连接的新 id，
	// 这样中途切换 cl_dummy 也不会把两个参与者互换。
	int LocalConn = m_EnableLocalConn;
	if(LocalConn < 0)
		LocalConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(
		GameClient()->m_aLocalIds, LocalConn, m_EnableLocalClientId, m_EnableDummyClientId, m_RequireDummy);
	if(!Decision.m_Changed)
		return false;

	if(g_Config.m_Debug)
		dbg_msg("fast_practice", "rebind participants: local %d->%d dummy %d->%d",
			m_EnableLocalClientId, Decision.m_LocalClientId, m_EnableDummyClientId, Decision.m_DummyClientId);

	// 检查点、救援点等按 client id 索引；id 换了以后跟着迁移，重进后还能继续用。
	if(Decision.m_MigrateLocalState)
		m_aPracticeCommandState[Decision.m_LocalClientId] = m_aPracticeCommandState[m_EnableLocalClientId];
	if(Decision.m_MigrateDummyState)
		m_aPracticeCommandState[Decision.m_DummyClientId] = m_aPracticeCommandState[m_EnableDummyClientId];

	m_EnableLocalClientId = Decision.m_LocalClientId;
	m_EnableDummyClientId = Decision.m_DummyClientId;
	return true;
}

bool CFastPractice::IsPracticeParticipant(int ClientId) const
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return ClientId == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && ClientId == m_EnableDummyClientId);
}

float CFastPractice::ServerTeeAlpha(int ClientId, float DefaultAlpha) const
{
	// 参与者渲染的是分支世界里的自己，必须保持原样；其余客户端（包括其他玩家）
	// 的位置全部来自服务器快照，属于同一条"主世界"时间线。
	if(!m_Enabled || ClientId < 0 || GameClient()->m_Snap.m_SpecInfo.m_Active || IsPracticeParticipant(ClientId))
		return DefaultAlpha;
	// 关掉开关就是整组隐藏，配合不透明度调整"看得见但让位"的程度。
	if(!g_Config.m_QmFastPracticeShowMainWorld)
		return 0.0f;
	const float GhostAlpha = std::clamp(g_Config.m_QmFastPracticeMainWorldAlpha, 0, 100) / 100.0f;
	return std::min(DefaultAlpha, GhostAlpha);
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

int CFastPractice::PracticeRunCentiseconds() const
{
	const int TickSpeed = std::max(Client()->GameTickSpeed(), 1);
	return (int)((int64_t)m_PracticeRunTicks * 100 / TickSpeed);
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
	// 预测事件队列会随 CopyWorldClean 一起进入预测世界，tick 必须跟着平移，
	// 否则平移后的第一帧会把积压事件一次性播出来（特效/音效集中爆发）。
	QmShiftPracticePredictedEventTicks(m_PracticeBaseWorld.m_PredictedEvents, TickDelta);
	if(m_EventBarrierTick >= 0)
		m_EventBarrierTick += TickDelta;
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

void CFastPractice::HoldPracticeWorldForSuspension()
{
	// 解析失败时仍由练习接管渲染：把最后一次有效的分支世界铺到预测世界，
	// 镜头停在练习位置等服务器状态恢复，而不是瞬间弹回真身。
	if(!m_PracticeWorldInitialized)
		return;

	const int TargetTick = Client()->PredGameTick(g_Config.m_ClDummy);
	ShiftPracticeWorldTicks(TargetTick - m_PracticeBaseWorld.GameTick());
	PublishPracticeWorldSnapshot(m_PracticeBaseWorld, m_EnableLocalClientId, m_RequireDummy ? m_EnableDummyClientId : -1);
}

bool CFastPractice::SuspendPracticeForTransientFailure(const char *pReason)
{
	// 服务器重进、分身重连等过渡期里角色与 id 会短暂对不上。直接 Disable 会连同
	// 输入锁一起丢掉，练习输入立刻打到真身上；先冻结练习世界等恢复，宽限超时才退出。
	const int GraceTicks = QmPracticeTransientGraceTicks(Client()->GameTickSpeed());
	m_TransientFailureTicks++;
	if(QmResolvePracticeTransientFailure(m_TransientFailureTicks, GraceTicks) == EQmPracticeTransientFailureAction::HOLD)
	{
		if(g_Config.m_Debug)
			dbg_msg("fast_practice", "transient failure (%s): %d/%d ticks", pReason, m_TransientFailureTicks, GraceTicks);
		HoldPracticeWorldForSuspension();
		// 分支世界还没 fork（例如刚重进）时没有可冻结的内容，这一帧交回普通预测，
		// 否则本地角色会停在上一次画面、输入也打不进去。
		return m_PracticeWorldInitialized;
	}

	if(g_Config.m_Debug)
		dbg_msg("fast_practice", "practice disabled after persistent failure (%s)", pReason);
	EchoPractice("fast practice disabled: practice state unavailable (%s)", pReason);
	Disable();
	return false;
}

void CFastPractice::SyncPracticeWorldConfig()
{
	m_PracticeBaseWorld.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	// 分支世界不消费服务器事件（ProcessEvents 在练习里直接返回），开火声、
	// 子弹命中、爆炸和锤击特效只能来自预测事件。cl_predict_events 默认关闭，
	// 沿用主世界开关会让练习里所有武器都没有开火反馈。
	m_PracticeBaseWorld.m_WorldConfig.m_PredictEvents = true;
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
		if(PRACTICE_PLASMA_PER_SEC > 0)
			FirePracticeGun(World, Gun, Tick);
	}
}

void CFastPractice::FirePracticeGun(CGameWorld &World, SPracticeGun &Gun, int Tick)
{
	const int FireDelay = Client()->GameTickSpeed() / PRACTICE_PLASMA_PER_SEC;

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

		if(distance(pTarget->m_Pos, Gun.m_Pos) > PRACTICE_PLASMA_RANGE)
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
	m_Recorder.MarkBoundary(GameClient()->m_LocalWorld);

	m_PracticeBaseWorld.CopyWorldClean(&GameClient()->m_GameWorld);
	// 服务器世界那份队列属于上一段时间线（也可能停在进入练习前的版本），
	// 带进新的分支世界会让重算 tick 去重失效，整份作废。
	QmResetPracticePredictedEvents(m_PracticeBaseWorld.m_PredictedEvents);
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

	if(m_HasPendingLocalTee)
	{
		// 分身接入/断开时的重新 fork：fork 出来的角色在服务器世界的位置上，
		// 这里把本地 tee 放回练习分支原来的位置与状态，跑图进度不丢。
		if(CCharacter *pLocal = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
			ApplyPracticeRescueTee(pLocal, m_PendingLocalTee);
		m_HasPendingLocalTee = false;
		m_PendingLocalTee = {};
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

std::vector<SSwitchers> *CFastPractice::PracticeSwitchers()
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return nullptr;
	return &m_PracticeBaseWorld.Switchers();
}

int CFastPractice::PracticeSwitchTeam() const
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return -1;

	// 开关状态按当前操控的 tee 所在队伍取；分身接管时同样成立。
	int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId != m_EnableLocalClientId && ClientId != m_EnableDummyClientId)
		ClientId = m_EnableLocalClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return -1;

	return std::clamp(m_PracticeBaseWorld.m_Teams.Team(ClientId), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
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

void CFastPractice::Enable(bool PreserveHeldInput)
{
	if(m_Enabled || !CanEnable())
		return;

	GameClient()->m_Binds.RefreshActiveBinds();

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int InactiveConn = ActiveConn ^ 1;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	m_EnableLocalConn = ActiveConn;
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
	m_PracticeRunTicks = 0;
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	// 进入练习前的普通预测已经播到当前预测 tick，分支事件从它之后算起。
	m_EventBarrierTick = m_PracticeBaseWorld.GameTick();
	// 录制是全手动的：进入练习不再自动开录，由“开始录制”按键启动轨迹。
	m_Recorder.Reset();
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
	PublishPracticeWorldSnapshot(m_PracticeBaseWorld, m_EnableLocalClientId, m_RequireDummy ? m_EnableDummyClientId : -1);
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

	// 接入分身要重新 fork 世界。fork 出来的本地角色在服务器世界的位置上，
	// 先把练习分支里的本地 tee 存起来，fork 成功后由 InitPracticeWorld 放回去。
	// 分支世界没建好时（旁观返回、重进）不保存，让重新 fork 的结果作准。
	if(m_PracticeWorldInitialized)
	{
		if(CCharacter *pLocalChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		{
			CapturePracticeRescueTee(pLocalChar, m_PracticeBaseWorld.GameTick(), m_PendingLocalTee);
			m_HasPendingLocalTee = true;
		}
	}

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

	PublishPracticeWorldSnapshot(m_PracticeBaseWorld, m_EnableLocalClientId, m_EnableDummyClientId);
	return true;
}

void CFastPractice::Disable()
{
	const bool WasEnabled = m_Enabled;
	if(m_Enabled)
		GameClient()->m_PredictedDummyId = -1;
	m_InputLock.ClearLocks();
	if(WasEnabled)
		ReleaseBufferedActionInputState();
	// 机关炮缓存绑定当前地图，下次启用时重新解析。
	m_PracticeGunsLoaded = false;
	m_vPracticeGuns.clear();
	// 世界树：主服务器世界 m_GameWorld 一直独立推进，练习是其 fork 出的分支。
	// 退出即销毁分支——EndTrack 丢弃轨迹会话，ResetPracticeState 里的
	// m_PracticeBaseWorld.Clear() 销毁所有实体；下次进入重新 fork。
	// 已 /pg 落盘的 .gho 组是独立文件，分支销毁后依然存在。
	m_Recorder.End(GameClient()->m_LocalWorld);
	ResetPracticeState();
	if(WasEnabled)
		m_InputLock.BeginPostPracticeSuppression(2);
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
	QmNeutralizePracticeInput(NeutralInput);
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

	PublishPracticeWorldSnapshot(m_PracticeBaseWorld, LocalClientId, m_RequireDummy ? DummyClientId : -1);
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetAttackTickHistory();
}

void CFastPractice::ResetPracticeToAnchor()
{
	if(!m_Enabled)
		return;

	// 复位目标由锚点决定，丢弃可能残留的分身重 fork 待还原 tee。
	m_HasPendingLocalTee = false;
	m_PendingLocalTee = {};

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
	// 时间线整条换掉：救援点、/lasttp、计时全部从零开始。
	m_PracticeRunTicks = 0;
	for(auto &State : m_aPracticeCommandState)
	{
		// 救援模式是用户偏好，重开后保留；其余记录都指向旧时间线，必须清空。
		State.m_HasRescueAuto = false;
		State.m_HasRescueManual = false;
		State.m_AutoTee = {};
		State.m_ManualTee = {};
		State.m_Records = {};
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
	// 快照初刻留下的事件 tick 早已被屏障盖过，留着会在下一帧集中播放。
	QmDropPracticePredictedEventsUntil(m_PracticeBaseWorld.m_PredictedEvents, m_EventBarrierTick);
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
	m_Recorder.MarkBoundary(GameClient()->m_LocalWorld);
	return true;
}

void CFastPractice::SetPracticeCheckpoint()
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return;
	// 与 /r、/tp 等聊天命令一致：操作当前控制的 tee。分身接管时按的是分身，
	// 只有解析不到当前角色（例如旁观）才回落到启用练习时的本体。
	int ClientId = CurrentLocalPracticeId();
	if(ClientId < 0)
		ClientId = m_EnableLocalClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(ClientId);
	if(!pChar)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasCheckpoint = true;
	CapturePracticeRescueTee(pChar, m_PracticeBaseWorld.GameTick(), State.m_CheckpointTee);
	EchoPractice("practice checkpoint set");
}

void CFastPractice::RestorePracticeCheckpoint()
{
	if(!m_Enabled || !m_PracticeWorldInitialized)
		return;
	int ClientId = CurrentLocalPracticeId();
	if(ClientId < 0)
		ClientId = m_EnableLocalClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	if(!State.m_HasCheckpoint || !State.m_CheckpointTee.m_Valid)
	{
		EchoPractice("no practice checkpoint set");
		return;
	}
	CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(ClientId);
	if(!pChar)
		return;
	// 与 /r 相同，重生点只回滚 tee，不动世界状态。
	OnPracticePositionJump();
	ApplyPracticeRescueTee(pChar, State.m_CheckpointTee);
	pChar->Unfreeze();
	PublishPracticeWorldState();
}

void CFastPractice::PublishPracticeWorldState()
{
	// 调用方要么整条时间线重开（/kill、/restart），要么把人挪回检查点：
	// 两种都是新的一段尝试，计时从零开始。
	m_PracticeRunTicks = 0;
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
	PublishPracticeWorldSnapshot(m_PracticeBaseWorld, m_EnableLocalClientId, m_RequireDummy ? m_EnableDummyClientId : -1);
}

void CFastPractice::PublishPracticeWorldSnapshot(CGameWorld &World, int LocalClientId, int DummyClientId)
{
	CPracticePredictStateSink Sink(GameClient());
	QmPublishPracticeWorldSnapshot(Sink, World, LocalClientId, DummyClientId, GameClient()->m_PredictedTick);
}

void CFastPractice::PublishPracticePredictionBeforeStep(CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, bool CopyWorldToPredicted, int LocalClientId)
{
	CPracticePredictStateSink Sink(GameClient());
	QmPublishPracticePredictionBeforeStep(Sink, World, Plan, Tick, CopyWorldToPredicted, LocalClientId);
}

void CFastPractice::PublishPracticePredictionAfterStep(CGameWorld &World, const SPracticePredictTickPlan &Plan, int Tick, int LocalClientId)
{
	CPracticePredictStateSink Sink(GameClient());
	QmPublishPracticePredictionAfterStep(Sink, World, Plan, Tick, LocalClientId);
}

void CFastPractice::PublishPracticePredictedPositions(CGameWorld &World, int Tick)
{
	CPracticePredictStateSink Sink(GameClient());
	QmPublishPracticePredictedPositions(Sink, World, Tick);
}

bool CFastPractice::PublishPracticeNewPredictedFeedback(int Slot, CCharacter *pChar, int Tick)
{
	if(!pChar)
		return false;
	if(!QmPracticeConsumeNewPredictedTick(Tick, GameClient()->m_aLastNewPredictedTick[Slot]))
		return false;

	const vec2 Pos = pChar->Core()->m_Pos;
	const int Events = pChar->Core()->m_TriggeredEvents;
	if(!GameClient()->m_SuppressEvents)
	{
		if(Events & COREEVENT_AIR_JUMP)
		{
			GameClient()->m_aLastPredictedAirJumpTick[Slot] = Tick;
			GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);
		}
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
	return true;
}

void CFastPractice::TrackPracticeTileFeedback(int ClientId, CCharacter *pChar, const vec2 &BeforePos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;

	const vec2 AfterPos = pChar->Core()->m_Pos;
	const bool WasInDeath = IsCharacterTouchingDeathTile(Collision(), BeforePos, pChar->GetProximityRadius());
	const bool IsInDeath = IsCharacterTouchingDeathTile(Collision(), AfterPos, pChar->GetProximityRadius());
	const SPracticeTileFeedbackDecision FeedbackDecision = EvaluatePracticeTileFeedback(WasInDeath, IsInDeath);
	if(!FeedbackDecision.m_RecordDeath)
		return;

	// 死亡只写 /back 记录，来源是死亡前最后一次自动救援快照，与服务器
	// CCharacter::Die 写 m_LastDeath 的时机和内容一致。
	// 地图传送 tile 不写任何命令记录：服务器同样不更新 m_LastTeleTee 与 m_LastDeath。
	auto &State = m_aPracticeCommandState[ClientId];
	QmRecordPracticeDeath(State.m_Records, State.m_AutoTee);

	// 死亡 tile 的落点：manual 模式下用显式救援点，否则用最近的安全点；
	// 连救援点都还没采到时回到启用练习那一刻的起点。
	const SQmPracticeSavedTee *pTee = nullptr;
	if(State.m_RescueManual && State.m_ManualTee.m_Valid)
		pTee = &State.m_ManualTee;
	else if(State.m_AutoTee.m_Valid)
		pTee = &State.m_AutoTee;
	CCharacter *pStartChar = m_PracticeStartWorld.GetCharacterById(ClientId);

	const CCharacterCore *pCore = pChar->Core();
	const SQmPracticeDeathTileDecision DeathDecision = QmEvaluatePracticeDeathTile(
		FeedbackDecision.m_RecordDeath, pCore->m_Super || pCore->m_Invincible, pTee != nullptr || pStartChar != nullptr);
	if(!DeathDecision.m_PlayFeedback)
		return;

	// 服务器在 practice 队伍里对死亡 tile 只播一次/秒的死亡效果并冻结；
	// 分支世界不再冻结，改为立刻回退，所以每个新进入的 tick 播一次反馈。
	if(!GameClient()->m_SuppressEvents)
		GameClient()->m_Effects.PlayerDeath(AfterPos, ClientId, 1.0f);
	if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
		if(GetQmFocusModeDecisions().m_PlayDeathOrSpawnSound)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_DIE, 1.0f, AfterPos);

	if(!DeathDecision.m_ReturnToSavedPoint)
		return;

	// 位置突变必须切段，否则存成幽灵后回放会连出一条穿过死亡 tile 的直线。
	OnPracticePositionJump();
	if(pTee)
	{
		ApplyPracticeRescueTee(pChar, *pTee);
	}
	else
	{
		TeleportCharacter(pChar, pStartChar->Core()->m_Pos);
	}
	pChar->Unfreeze();
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
	// 分身连接断开：降级为本体单人练习，并重新 fork 去掉分支世界里的残留分身。
	// 重新 fork 会把本地角色换到服务器世界的位置，先存住分支里的本地 tee。
	if(m_RequireDummy && !Client()->DummyConnected())
	{
		if(m_PracticeWorldInitialized)
		{
			if(CCharacter *pLocalChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
			{
				CapturePracticeRescueTee(pLocalChar, m_PracticeBaseWorld.GameTick(), m_PendingLocalTee);
				m_HasPendingLocalTee = true;
			}
		}
		m_RequireDummy = false;
		m_HasDummyAnchor = false;
		m_EnableDummyClientId = -1;
		m_PracticeWorldInitialized = false;
	}

	// 刚从旁观回到游戏时分支世界还没重新 fork，按本次连接的实际 id 重绑参与者。
	if(!m_PracticeWorldInitialized)
		TryRebindPracticeParticipants();
	if(!m_RequireDummy)
		TryAttachDummyFromSnapshot();

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		// 重进过快、没观察到旁观帧时 client id 也可能已经换了：旧分支世界按旧 id
		// 建立，只有重绑并重新 fork 才能继续，否则会一直解析失败直到宽限退出。
		if(TryRebindPracticeParticipants())
			m_PracticeWorldInitialized = false;
		if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
			return SuspendPracticeForTransientFailure("resolve roles");
	}

	int LocalInputConn = -1;
	int DummyInputConn = -1;
	if(!ResolveParticipantInputs(LocalClientId, DummyClientId, LocalInputConn, DummyInputConn))
		return SuspendPracticeForTransientFailure("resolve inputs");

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
		return SuspendPracticeForTransientFailure("init practice world");

	SyncPracticeWorldConfig();

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);

	CCharacter *pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId);
	CCharacter *pDummyChar = m_RequireDummy ? GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId) : nullptr;
	if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		return SuspendPracticeForTransientFailure("missing practice character");

	// 角色与输入映射都恢复了，之前的瞬态失败不再累计。
	m_TransientFailureTicks = 0;

	const int BaseGameTick = m_PracticeBaseWorld.GameTick();
	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
	const int FinalTickRegular = PredTick;
	// 主循环追赶真实预测 tick：从分支世界当前 tick 补齐到 PredTick，所有参与者都发布。
	const SPracticePredictTickPlan TickPlan = QmBuildPracticePredictTickPlan(BaseGameTick, FinalTickRegular, 0, true);

	const int LocalTickSlot = LocalInputConn;
	const int DummyTickSlot = m_RequireDummy ? DummyInputConn : (LocalTickSlot ^ 1);

	if(TickPlan.Empty())
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
	for(int Tick = TickPlan.m_FirstTick; Tick <= TickPlan.m_FinalTickSelf; Tick++)
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

		// 事件屏障：重算已经播过的 tick 时不允许再创建预测事件。去重表跟着世界
		// 重建，光靠 (id, tick) 匹配挡不住重复开火声，必须在源头关掉。
		GameClient()->m_PredictedWorld.m_WorldConfig.m_PredictEvents = QmPracticeTickAllowsPredictedEvents(Tick, m_EventBarrierTick);

		PublishPracticePredictionBeforeStep(GameClient()->m_PredictedWorld, TickPlan, Tick, false, LocalClientId);

		// 和原生预测循环一样取“不晚于该 tick 的最新记录”。输入环按发送序号写入，
		// 发送频率低于 tick 频率，按 tick 取模定位（旧的 GetInputExact）几乎总是落空，
		// 跳跃/钩子只能靠偶发命中的旧记录生效，表现为按键延迟数秒。
		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalPracticeInput = {};
		CNetObj_PlayerInput DummyPracticeInput = {};
		// 连接初期还没有任何记录时沿用角色上一 tick 的输入。
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
				QmPracticeSuppressActionInput(*pInputData, pLocalChar->LatestInput()->m_Fire);
			if(pDummyInputData)
				QmPracticeSuppressActionInput(*pDummyInputData, pDummyChar->LatestInput()->m_Fire);
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
			ApplyDummyHammerAim(pLocalChar, pDummyChar, *pDummyInputData);
		}

		const vec2 LocalPosBeforeTick = pLocalChar->Core()->m_Pos;
		const vec2 DummyPosBeforeTick = pDummyChar ? pDummyChar->Core()->m_Pos : vec2(0.0f, 0.0f);
		ApplyPracticeActorInputs(pLocalChar, pDummyChar, pInputData, pDummyInputData, Tick, PredTick);
		StepPracticeWorld(GameClient(), GameClient()->m_PredictedWorld, Tick, pLocalChar, pDummyChar, pInputData, pDummyInputData);

		CollectTrackedProjectiles(GameClient()->m_PredictedWorld, LocalClientId, DummyClientId, vTrackedProjectilesBefore);
		// 机关炮在实体 tick 之前发射，新生成的 plasma 同一 tick 就开始移动。
		TickPracticeGuns(GameClient()->m_PredictedWorld, Tick);
		GameClient()->m_PredictedWorld.Tick();
		// 死亡 tile 反馈与回退必须在角色前进之后判断：Before/After 两点才能区分
		// “刚踩进去”和“一直停在上面”。回退后紧跟的 TrackSafeRescuePosition 会
		// 在下一 tick 重新采样安全点。
		// 分支世界每推进一 tick 记一次练习用时；服务器暂停时这个循环不跑，计时随之停住。
		// 先累计再处理死亡回退：回退会清零计时，新一段从 0 开始而不是从 1 tick 开始。
		m_PracticeRunTicks++;
		TrackPracticeTileFeedback(LocalClientId, pLocalChar, LocalPosBeforeTick);
		if(pDummyChar)
			TrackPracticeTileFeedback(DummyClientId, pDummyChar, DummyPosBeforeTick);
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
		// 本 tick 的事件已经创建并消费；此后任何路径重算这段 tick 都不再发声。
		m_EventBarrierTick = std::max(m_EventBarrierTick, Tick);

		PublishPracticePredictionAfterStep(GameClient()->m_PredictedWorld, TickPlan, Tick, LocalClientId);
		PublishPracticePredictedPositions(GameClient()->m_PredictedWorld, Tick);

		if(PublishPracticeNewPredictedFeedback(LocalTickSlot, pLocalChar, Tick))
			GameClient()->m_NewPredictedTick = true;
		if(pDummyChar)
			PublishPracticeNewPredictedFeedback(DummyTickSlot, pDummyChar, Tick);
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

	// 视觉外推从真实末帧继续往前跑；他人是否一起外推由快速输入设置决定，
	// 关闭时停在真实末帧，避免把其他玩家画到未来位置。
	const SPracticePredictTickPlan TickPlan = QmBuildPracticePredictTickPlan(FinalTickRegular, FinalTickRegular, FastInputTicks, EffectiveFastInputOthers(GameClient()));

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
	for(int Tick = TickPlan.m_FirstTick; Tick <= TickPlan.m_FinalTickSelf; Tick++)
	{
		Collision()->SetQmAxiomQuadTick(Tick, Client()->GameTickSpeed());
		pLocalChar = VisualWorld.GetCharacterById(LocalClientId);
		pDummyChar = m_RequireDummy ? VisualWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
			return FinalTickRegular;

		PublishPracticePredictionBeforeStep(VisualWorld, TickPlan, Tick, true, LocalClientId);

		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalFastInput = GameClient()->m_Controls.m_aFastInput[LocalTee];
		CNetObj_PlayerInput DummyFastInput = {};
		CNetObj_PlayerInput DummyVisualInput = {};
		CNetObj_PlayerInput *pInputData = &LocalFastInput;
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
			ApplyDummyHammerAim(pLocalChar, pDummyChar, *pDummyInputData);
		}

		const bool TempPredEventState = VisualWorld.m_WorldConfig.m_PredictEvents;
		VisualWorld.m_WorldConfig.m_PredictEvents = false;

		ApplyPracticeActorInputs(pLocalChar, pDummyChar, pInputData, pDummyInputData, Tick, FinalTickRegular);
		StepPracticeWorld(GameClient(), VisualWorld, Tick, pLocalChar, pDummyChar, pInputData, pDummyInputData);

		VisualWorld.Tick();
		VisualWorld.m_WorldConfig.m_PredictEvents = TempPredEventState;

		PublishPracticePredictionAfterStep(VisualWorld, TickPlan, Tick, LocalClientId);
		PublishPracticePredictedPositions(VisualWorld, Tick);
	}

	return TickPlan.m_FinalTickSelf;
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

	// 主世界影子：练习推进时，服务器那条时间线里的自己停在开练习那一刻。
	// 它和练习中的自己长同一个皮肤，重叠时很容易看错人，所以开关与不透明度
	// 都交给配置；需要干净画面的人可以整条关掉。
	if(!g_Config.m_QmFastPracticeShowMainWorld)
		return;
	const float MainAlpha = std::clamp(g_Config.m_QmFastPracticeMainWorldAlpha, 0, 100) / 100.0f;
	if(MainAlpha <= 0.0f)
		return;
	RenderRealMarker(m_MainGhost, MainAlpha);
	// 分身影子略淡：两条影子同时在场时，浅一点的那条更容易和本体分辨。
	RenderRealMarker(m_DummyGhost, MainAlpha * 0.8f);
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

void CFastPractice::RecordPracticeCommandTeleport(int ClientId, CCharacter *pChar)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;

	// 服务器在 Teleport 之后先 ResetJumps/Unfreeze/ResetVelocity 再存 m_LastTeleTee，
	// 调用方已经做过等价的角色整理，这里直接采整份 tee。
	SQmPracticeSavedTee Tee;
	CapturePracticeRescueTee(pChar, m_PracticeBaseWorld.GameTick(), Tee);
	QmRecordPracticeCommandTeleport(m_aPracticeCommandState[ClientId].m_Records, Tee);
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

	auto &State = m_aPracticeCommandState[ClientId];
	// 服务器每个 tick 都覆盖救援点，/r 的落点就是最近一帧的合法位置。
	State.m_HasRescueAuto = true;
	CapturePracticeRescueTee(pChar, pChar->GameWorld()->GameTick(), State.m_AutoTee);
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
