/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_PREDICTION_GAMEWORLD_H
#define GAME_CLIENT_PREDICTION_GAMEWORLD_H

#include <game/gamecore.h>
#include <game/teamscore.h>

#include <list>
#include <vector>

class CCollision;
class CCharacter;
class CEntity;
class CMapBugs;

class CGameWorld
{
public:
	enum
	{
		ENTTYPE_PROJECTILE = 0,
		ENTTYPE_LASER,
		ENTTYPE_DOOR,
		ENTTYPE_DRAGGER,
		ENTTYPE_LIGHT,
		ENTTYPE_GUN,
		ENTTYPE_PLASMA,
		ENTTYPE_PICKUP,
		ENTTYPE_FLAG,
		ENTTYPE_CHARACTER,
		NUM_ENTTYPES
	};

	CWorldCore m_Core;
	CTeamsCore m_Teams;

	CGameWorld();
	~CGameWorld();
	void Init(CCollision *pCollision, CTuningParams *pTuningList, const CMapBugs *pMapBugs);

	CEntity *FindFirst(int Type);
	CEntity *FindLast(int Type);
	int FindEntities(vec2 Pos, float Radius, CEntity **ppEnts, int Max, int Type);
	CCharacter *IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, const CCharacter *pNotThis = nullptr, int CollideWith = -1, const CCharacter *pThisOnly = nullptr);
	CEntity *IntersectEntity(vec2 Pos0, vec2 Pos1, float Radius, int Type, vec2 &NewPos, const CEntity *pNotThis = nullptr, int CollideWith = -1, const CEntity *pThisOnly = nullptr);
	void InsertEntity(CEntity *pEntity, bool Last = false);
	void RemoveEntity(CEntity *pEntity);
	void RemoveCharacter(CCharacter *pChar);
	void Tick();

	// DDRace
	void ReleaseHooked(int ClientId);
	std::vector<CCharacter *> IntersectedCharacters(vec2 Pos0, vec2 Pos1, float Radius, const CEntity *pNotThis = nullptr);

	int m_GameTick;

	// getter for server variables
	int GameTick() const { return m_GameTick; }
	int GameTickSpeed() const { return SERVER_TICK_SPEED; }
	const CCollision *Collision() const { return m_pCollision; }
	CCollision *Collision() { return m_pCollision; }
	CTeamsCore *Teams() { return &m_Teams; }
	std::vector<SSwitchers> &Switchers() { return m_Core.m_vSwitchers; }
	CEntity *GetEntity(int Id, int EntityType);
	CCharacter *GetCharacterById(int Id) { return (Id >= 0 && Id < MAX_CLIENTS) ? m_apCharacters[Id] : nullptr; }

	// from gamecontext
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, CClientMask Mask, int Id = -1);

	// for client side prediction
	struct
	{
		bool m_IsDDRace;
		bool m_IsVanilla;
		bool m_IsFNG;
		bool m_InfiniteAmmo;
		bool m_PredictTiles;
		int m_PredictFreeze;
		bool m_PredictWeapons;
		bool m_PredictDDRace;
		bool m_IsSolo;
		bool m_UseTuneZones;
		bool m_BugDDRaceInput;
		bool m_NoWeakHookAndBounce;
		bool m_PredictEvents;
		bool m_OldLaser;
		bool m_PredictTeleport = false;
	} m_WorldConfig;

	bool m_IsValidCopy;
	CGameWorld *m_pParent;
	CGameWorld *m_pChild;

	int m_LocalClientId;

	bool IsLocalTeam(int OwnerId) const;
	void OnModified() const;
	void NetObjBegin(CTeamsCore Teams, int LocalClientId);
	void NetCharAdd(int ObjId, CNetObj_Character *pChar, CNetObj_DDNetCharacter *pExtended, int GameTeam, bool IsLocal);
	void NetObjAdd(int ObjId, int ObjType, const void *pObjData, const CNetObj_EntityEx *pDataEx);
	void ResetDoorCollision();
	void NetObjEnd();
	void CopyWorld(CGameWorld *pFrom);
	void CopyWorldClean(CGameWorld *pFrom); // TClient
	CEntity *FindMatch(int ObjId, int ObjType, const void *pObjData);
	void Clear();

	const CTuningParams *TuningList() const { return m_pTuningList; }
	CTuningParams *TuningList() { return m_pTuningList; }
	const CTuningParams *GlobalTuning() const { return &TuningList()[0]; }
	CTuningParams *GlobalTuning() { return &TuningList()[0]; }
	const CTuningParams *GetTuning(int i) const { return &TuningList()[i]; }
	CTuningParams *GetTuning(int i) { return &TuningList()[i]; }

	bool EmulateBug(int Bug) const;

	class CPredictedEvent
	{
	public:
		int m_EventId;
		vec2 m_Pos; // NetEvent's Pos are integers
		int m_Id; // identifier to prevent adding the same event multiple times
		int m_Tick;

		int m_ExtraInfo;
		bool m_Handled = false;
		bool m_ServerConfirmed = false;

		CPredictedEvent(int EventId, vec2 Pos, int Id, int Tick, int ExtraInfo = -1) :
			m_EventId(EventId), m_Pos(vec2((int)Pos.x, (int)Pos.y)), m_Id(Id), m_Tick(Tick), m_ExtraInfo(ExtraInfo)
		{
		}
	};

	std::vector<CPredictedEvent> m_PredictedEvents;

	void CreatePredictedEvent(const CPredictedEvent &NewEvent);
	// pUnplayedMatch（可空）：确认命中"尚未被预测侧播放"的事件时置 true，
	// 调用方应当场代播，避免预测与快照都不播（声音/特效丢失）。
	bool CheckPredictedEventHandled(const CPredictedEvent &CheckEvent, bool *pUnplayedMatch = nullptr);
	bool CheckPredictedHammerHitHandled(const CPredictedEvent &CheckEvent, bool *pUnplayedMatch = nullptr);
	// QmClient: 目标 Id 推断失败时的锤击命中宽松确认，见
	// QmCheckPredictedHammerHitHandledLoose 注释。
	bool CheckPredictedHammerHitHandledLoose(int AttackerId, vec2 Pos, int Tick);
	void PlayPredictedEvents(int Tick);

	void CreatePredictedSound(vec2 Pos, int SoundId, int Id = -1);
	void CreatePredictedExplosionEvent(vec2 Pos, int Id = -1);
	void CreatePredictedHammerHitEvent(vec2 Pos, int Id = -1, int TargetId = -1);
	void CreatePredictedDamageIndEvent(vec2 Pos, float Angle, int Amount, int Id = -1);
	// QmClient: 本地角色核心事件声音（跳跃/钩子）在预测侧立即播放后，
	// 用本方法登记一个已处理的屏障事件，让快照确认路径跳过同一服务端
	// 声音事件，避免预测与快照双重播放。与 CreatePredictedSound 不同，
	// 屏障事件不会被 HandlePredictedEvents 再次播放，也不受
	// cl_predict_events 门禁限制（预测侧直接播放路径同样不受其控制）。
	void CreateHandledPredictedSound(vec2 Pos, int SoundId, int Id = -1);

private:
	void RemoveEntities();

	CEntity *m_pNextTraverseEntity = nullptr;
	CEntity *m_apFirstEntityTypes[NUM_ENTTYPES];

	CCharacter *m_apCharacters[MAX_CLIENTS];

	CCollision *m_pCollision;
	CTuningParams *m_pTuningList;
	const CMapBugs *m_pMapBugs;
};

// pUnplayedMatch（可空）：确认命中"尚未被预测侧播放"的事件时置 true，
// 调用方应当场代播特效（本函数同时把事件标记 handled，预测循环不会再播）。
inline bool QmCheckPredictedHammerHitHandled(std::vector<CGameWorld::CPredictedEvent> &vPredictedEvents, const CGameWorld::CPredictedEvent &CheckEvent, bool *pUnplayedMatch = nullptr)
{
	// 契约值：32（由 HammerHitPredictionMatchingUsesOwnerDistanceAndOneToOneConsumption 锁定）。
	// 预测/快照位置漂移导致的重复播放在 FinalizeHammerHitEvents 的本地攻击者
	// 兜底中单独处理，不改变这里的匹配语义。
	constexpr float MaxPositionCorrection = 32.0f;
	constexpr float MaxPositionCorrectionSquared = MaxPositionCorrection * MaxPositionCorrection;
	constexpr int MaxTickDifference = SERVER_TICK_SPEED;
	if(CheckEvent.m_Id < 0 || CheckEvent.m_ExtraInfo < 0)
		return false;
	auto Closest = vPredictedEvents.end();
	int ClosestTickDifference = MaxTickDifference + 1;
	float ClosestDistanceSquared = 0.0f;
	for(auto It = vPredictedEvents.begin(); It != vPredictedEvents.end(); ++It)
	{
		if(It->m_EventId != NETEVENTTYPE_HAMMERHIT || CheckEvent.m_EventId != NETEVENTTYPE_HAMMERHIT ||
			(CheckEvent.m_Id >= 0 && It->m_Id != CheckEvent.m_Id) || It->m_Tick > CheckEvent.m_Tick || CheckEvent.m_Tick - It->m_Tick > MaxTickDifference || It->m_ExtraInfo != CheckEvent.m_ExtraInfo)
			continue;
		if(It->m_ServerConfirmed && CheckEvent.m_Tick - It->m_Tick > 1)
			continue;
		const float DistanceSquared = length_squared(It->m_Pos - CheckEvent.m_Pos);
		if(DistanceSquared > MaxPositionCorrectionSquared)
			continue;
		const int TickDifference = CheckEvent.m_Tick - It->m_Tick;
		if(Closest == vPredictedEvents.end() || TickDifference < ClosestTickDifference || (TickDifference == ClosestTickDifference && DistanceSquared < ClosestDistanceSquared))
		{
			Closest = It;
			ClosestTickDifference = TickDifference;
			ClosestDistanceSquared = DistanceSquared;
		}
	}
	if(Closest == vPredictedEvents.end())
		return false;
	// QmClient: 确认时事件还没被预测播放 → 标记 handled 并通知调用方代播，
	// 防止预测与快照都不播（特效/声音丢失）。与 QmCheckPredictedEventHandled 相同的防御。
	if(pUnplayedMatch != nullptr)
		*pUnplayedMatch = !Closest->m_Handled;
	if(!Closest->m_Handled)
		Closest->m_Handled = true;
	if(Closest->m_ServerConfirmed)
		return true;
	// 保留短期确认屏障，防止同一 snapshot 的重复锤击事件再次播放粒子。
	Closest->m_ServerConfirmed = true;
	if(Closest->m_Handled)
	{
		Closest->m_Tick = CheckEvent.m_Tick;
		Closest->m_Pos = CheckEvent.m_Pos;
	}
	return true;
}

// QmClient: 锤击命中确认的宽松匹配。快照事件推断不出目标 Id 时（目标刚传送/
// 冻结状态切换/快照样本不足），只要攻击者 Id 匹配、位置在宽松半径内且处于
// 同一确认窗口，就认定预测已播放过特效，避免快照路径再播一次——锤击特效
// 内含 SOUND_HAMMER_HIT，双重播放很明显（"延迟两下"）。匹配成功即置
// confirmed：confirmed 事件只对 1 tick 内的确认继续生效，因此连续挥锤的
// 下一发不会被本函数误吞。
inline bool QmCheckPredictedHammerHitHandledLoose(std::vector<CGameWorld::CPredictedEvent> &vPredictedEvents, int AttackerId, vec2 Pos, int Tick)
{
	constexpr float MaxPositionCorrection = 64.0f;
	constexpr float MaxPositionCorrectionSquared = MaxPositionCorrection * MaxPositionCorrection;
	constexpr int MaxTickDifference = SERVER_TICK_SPEED;
	if(AttackerId < 0)
		return false;
	// 与严格版一致，选 tick 最接近的候选：一次误预测的孤儿事件（本地预测
	// 命中但服务端判定未命中，永远等不到确认）若被直接匹配，会吞掉后续
	// 同位置挥锤的合法确认，导致特效/声音丢失。
	auto Closest = vPredictedEvents.end();
	int ClosestTickDifference = MaxTickDifference + 1;
	for(auto It = vPredictedEvents.begin(); It != vPredictedEvents.end(); ++It)
	{
		if(It->m_EventId != NETEVENTTYPE_HAMMERHIT || It->m_Id != AttackerId ||
			It->m_Tick > Tick || Tick - It->m_Tick > MaxTickDifference)
			continue;
		if(It->m_ServerConfirmed && Tick - It->m_Tick > 1)
			continue;
		if(length_squared(It->m_Pos - Pos) > MaxPositionCorrectionSquared)
			continue;
		const int TickDifference = Tick - It->m_Tick;
		if(Closest == vPredictedEvents.end() || TickDifference < ClosestTickDifference)
		{
			Closest = It;
			ClosestTickDifference = TickDifference;
		}
	}
	if(Closest == vPredictedEvents.end())
		return false;
	Closest->m_ServerConfirmed = true;
	return true;
}

inline bool QmPredictedEventPositionsMatch(int EventId, vec2 ExistingPos, vec2 ConfirmedPos)
{
	constexpr float SoundMaxPositionCorrection = 96.0f;
	constexpr float ExplosionMaxPositionCorrection = 64.0f;
	constexpr float DamageIndicatorMaxPositionCorrection = 32.0f;
	float MaxPositionCorrection = 0.0f;
	if(EventId == NETEVENTTYPE_SOUNDWORLD)
		MaxPositionCorrection = SoundMaxPositionCorrection;
	else if(EventId == NETEVENTTYPE_EXPLOSION)
		MaxPositionCorrection = ExplosionMaxPositionCorrection;
	else if(EventId == NETEVENTTYPE_DAMAGEIND)
		MaxPositionCorrection = DamageIndicatorMaxPositionCorrection;

	const float DistanceSquared = length_squared(ExistingPos - ConfirmedPos);
	return MaxPositionCorrection == 0.0f ? DistanceSquared == 0.0f : DistanceSquared <= MaxPositionCorrection * MaxPositionCorrection;
}

inline bool QmPredictedEventMatchesForCreation(const CGameWorld::CPredictedEvent &Existing, const CGameWorld::CPredictedEvent &NewEvent)
{
	if(Existing.m_EventId != NewEvent.m_EventId || Existing.m_ExtraInfo != NewEvent.m_ExtraInfo)
		return false;

	// 声音事件由实体和 tick 标识。预测重放同一 tick 时位置可能被校正，
	// 但不能因此再次排队同一份声音。其他效果仍需用位置区分同 tick 内
	// 可能同时发生的多个独立事件（例如散弹爆炸）。
	if(Existing.m_Id == NewEvent.m_Id && Existing.m_Tick == NewEvent.m_Tick)
		return NewEvent.m_EventId == NETEVENTTYPE_SOUNDWORLD || Existing.m_Pos == NewEvent.m_Pos;

	// 服务端事件可能先于本地预测抵达。把这种已播放的无 Id 确认事件
	// 作为短期屏障，避免稍后的预测再次播放同一效果。
	if(!Existing.m_ServerConfirmed || NewEvent.m_Id < 0 || (Existing.m_Id >= 0 && Existing.m_Id != NewEvent.m_Id) || NewEvent.m_Tick < Existing.m_Tick || NewEvent.m_Tick - Existing.m_Tick > 1)
		return false;
	return QmPredictedEventPositionsMatch(NewEvent.m_EventId, Existing.m_Pos, NewEvent.m_Pos);
}

// pUnplayedMatch（可空）：匹配成功且该预测事件尚未被预测侧播放时置 true，
// 调用方应当场代播一次（本函数同时把事件标记 handled，预测循环不会再播）。
inline bool QmCheckPredictedEventHandled(std::vector<CGameWorld::CPredictedEvent> &vPredictedEvents, const CGameWorld::CPredictedEvent &CheckEvent, bool *pUnplayedMatch = nullptr)
{
	constexpr int MaxTickDifference = 3 * SERVER_TICK_SPEED;
	auto Closest = vPredictedEvents.end();
	int ClosestTickDifference = MaxTickDifference + 1;
	float ClosestDistanceSquared = 0.0f;
	for(auto It = vPredictedEvents.begin(); It != vPredictedEvents.end(); ++It)
	{
		if(It->m_EventId != CheckEvent.m_EventId || It->m_Tick > CheckEvent.m_Tick || CheckEvent.m_Tick - It->m_Tick > MaxTickDifference || It->m_ExtraInfo != CheckEvent.m_ExtraInfo)
			continue;
		if(!QmPredictedEventPositionsMatch(CheckEvent.m_EventId, It->m_Pos, CheckEvent.m_Pos))
			continue;

		const float DistanceSquared = length_squared(It->m_Pos - CheckEvent.m_Pos);
		const int TickDifference = CheckEvent.m_Tick - It->m_Tick;
		if(It->m_ServerConfirmed && TickDifference > 1)
			continue;
		// 无 Id 的确认事件只作为服务端先到的短期屏障，不能在数秒内
		// 把同位置的合法连续开火/爆炸误判成重复事件。
		if(It->m_Id < 0 && TickDifference > 1)
			continue;
		// Position is the strongest identity available for snapshot events. Prefer
		// the spatially closest candidate before using recency as a tie-breaker;
		// otherwise a newer but distant event can consume the confirmation and
		// leave the actual predicted effect to play again later.
		if(Closest == vPredictedEvents.end() || DistanceSquared < ClosestDistanceSquared ||
			(DistanceSquared == ClosestDistanceSquared && TickDifference < ClosestTickDifference))
		{
			Closest = It;
			ClosestTickDifference = TickDifference;
			ClosestDistanceSquared = DistanceSquared;
		}
	}

	if(Closest == vPredictedEvents.end())
		return false;
	// QmClient: 确认时事件还没被预测播放（快照先于预测循环轮到它到达），
	// 不能只标记 confirmed 就吞掉——否则预测与快照都不播，声音/特效丢失。
	// 标记 handled 让预测循环跳过，并通过出参通知调用方当场代播一次。
	if(pUnplayedMatch != nullptr)
		*pUnplayedMatch = !Closest->m_Handled;
	if(!Closest->m_Handled)
		Closest->m_Handled = true;
	if(Closest->m_ServerConfirmed)
		return true;
	// 保留一个短期确认屏障，防止同一 snapshot 中的重复服务端事件再次播放。
	// 声音等无实体 Id 的事件只在当前/下一 tick 内屏蔽。
	Closest->m_ServerConfirmed = true;
	if(Closest->m_Handled)
	{
		Closest->m_Id = -1;
		Closest->m_Tick = CheckEvent.m_Tick;
		Closest->m_Pos = CheckEvent.m_Pos;
	}
	return true;
}

class CCharOrder
{
public:
	std::list<int> m_Ids; // reverse of the order in the gameworld, since entities will be inserted in reverse
	CCharOrder()
	{
		Reset();
	}
	void Reset()
	{
		m_Ids.clear();
		for(int i = 0; i < MAX_CLIENTS; i++)
			m_Ids.push_back(i);
	}
	void GiveStrong(int c)
	{
		if(0 <= c && c < MAX_CLIENTS)
		{
			m_Ids.remove(c);
			m_Ids.push_front(c);
		}
	}
	void GiveWeak(int c)
	{
		if(0 <= c && c < MAX_CLIENTS)
		{
			m_Ids.remove(c);
			m_Ids.push_back(c);
		}
	}
	bool HasStrongAgainst(int From, int To)
	{
		for(int i : m_Ids)
		{
			if(i == To)
				return false;
			else if(i == From)
				return true;
		}
		return false;
	}
};

#endif
