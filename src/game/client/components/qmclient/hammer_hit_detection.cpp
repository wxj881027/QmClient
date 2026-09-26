// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "hammer_hit_detection.h"

#include <base/math.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/teamscore.h>

#include <algorithm>

namespace
{
	// 事件可能比产生它的 tick 晚 1-2 个快照才送达（事件缓冲跨快照、抖动）。
	// 实机日志证实快照 tick 与挥锤 tick 差可达 2；只允许 1 会让攻击者推断
	// 频繁失败，导致已被预测播放的锤击特效被快照再播一次（粒子"延迟两下"）。
	// 边界由 QmHammerHitDetection 测试锁定（stale 样本使用 Age=4）。
	constexpr int MAX_ATTACK_AGE = 3;
	constexpr float ATTACK_POSITION_SLACK = 16.0f;
	constexpr float TARGET_POSITION_SLACK = 16.0f;
	constexpr float CONTINUOUS_MOVE_RADIUS_MULTIPLIER = 4.0f;

	float DistancePointSegment(vec2 Point, vec2 SegmentStart, vec2 SegmentEnd)
	{
		const vec2 Segment = SegmentEnd - SegmentStart;
		const float SegmentLengthSquared = length_squared(Segment);
		if(SegmentLengthSquared <= 0.000001f)
			return distance(Point, SegmentStart);
		const float Amount = std::clamp(dot(Point - SegmentStart, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
		return distance(Point, SegmentStart + Segment * Amount);
	}

	float DistanceToSnapshotMovement(vec2 Point, vec2 PrevPos, vec2 CurPos, float ProximityRadius)
	{
		if(distance(PrevPos, CurPos) <= ProximityRadius * CONTINUOUS_MOVE_RADIUS_MULTIPLIER)
			return DistancePointSegment(Point, PrevPos, CurPos);
		return minimum(distance(Point, PrevPos), distance(Point, CurPos));
	}

	bool IsAttackCandidate(const SQmHammerAttackSample &Sample, vec2 EventPos, int EventTick)
	{
		if(Sample.m_ClientId < 0 || Sample.m_ClientId >= MAX_CLIENTS ||
			!Sample.m_HammerHitEnabled || Sample.m_ProximityRadius <= 0.0f)
		{
			return false;
		}
		// 快照里的武器已经是挥锤之后的状态（锤完立刻切枪很常见），
		// 因此当前或上一 tick 拿着锤都算候选。
		if(Sample.m_Weapon != WEAPON_HAMMER && Sample.m_PrevWeapon != WEAPON_HAMMER)
			return false;
		const int AttackAge = EventTick - Sample.m_AttackTick;
		if(AttackAge < 0 || AttackAge > MAX_ATTACK_AGE)
			return false;

		if(AttackAge >= 1)
		{
			// The current angle belongs to the next tick. Only use the movement envelope.
			const float MaxEventDistance = Sample.m_ProximityRadius * 1.75f + ATTACK_POSITION_SLACK;
			return DistanceToSnapshotMovement(EventPos, Sample.m_PrevPos, Sample.m_CurPos, Sample.m_ProximityRadius) <= MaxEventDistance;
		}

		vec2 Direction = Sample.m_Direction;
		if(length_squared(Direction) <= 0.000001f)
			Direction = vec2(0.0f, -1.0f);
		else
			Direction = normalize(Direction);
		const vec2 HammerCenter = Sample.m_CurPos + Direction * Sample.m_ProximityRadius * 0.75f;
		return distance(EventPos, HammerCenter) <= Sample.m_ProximityRadius + ATTACK_POSITION_SLACK;
	}

	bool IsTargetCandidate(const SQmHammerTargetSample &Sample, vec2 EventPos)
	{
		if(Sample.m_ClientId < 0 || Sample.m_ClientId >= MAX_CLIENTS || Sample.m_ProximityRadius <= 0.0f)
			return false;
		const float MaxEventDistance = Sample.m_ProximityRadius * 0.5f + TARGET_POSITION_SLACK;
		return DistanceToSnapshotMovement(EventPos, Sample.m_PrevPos, Sample.m_CurPos, Sample.m_ProximityRadius) <= MaxEventDistance;
	}

	bool CanHammerTarget(const SQmHammerAttackSample &Attack, const SQmHammerTargetSample &Target)
	{
		if(Attack.m_ClientId == Target.m_ClientId)
			return false;
		if(Attack.m_Super || Target.m_Super)
			return true;
		if(Attack.m_Solo || Target.m_Solo)
			return false;
		return Attack.m_DDTeam == Target.m_DDTeam;
	}

	bool ClientMatchesFilter(int ClientId, int Filter)
	{
		return Filter == CQmHammerHitTracker::ANY_CLIENT || ClientId == Filter;
	}

	bool ConnectionMatchesFilter(int Connection, int Filter)
	{
		return Filter == CQmHammerHitTracker::ANY_CONNECTION || Connection == Filter;
	}
}

bool QmIsHammerSuperTeam(int DDTeam, bool IsDDRace16)
{
	return QmIsHammerSuperTeam(DDTeam, IsDDRace16 ? VANILLA_MAX_CLIENTS + 1 : NUM_DDRACE_TEAMS);
}

bool QmIsHammerSuperTeam(int DDTeam, int NumDDRaceTeams)
{
	return DDTeam == QmNormalizeNumDDRaceTeams(NumDDRaceTeams) - 1;
}

bool QmIsHammerWakeupTransition(int PrevFreezeEnd, int CurFreezeEnd, int EventTick)
{
	if(EventTick < 0 || CurFreezeEnd != 0)
		return false;
	return PrevFreezeEnd == -1 || PrevFreezeEnd > EventTick;
}

SQmHammerHitMatch QmMatchHammerHitEvent(
	vec2 EventPos,
	int EventTick,
	const SQmHammerAttackSample *pAttackSamples,
	int NumAttackSamples,
	const SQmHammerTargetSample *pTargetSamples,
	int NumTargetSamples)
{
	const SQmHammerAttackSample *apAttacks[MAX_CLIENTS] = {};
	const SQmHammerTargetSample *apTargets[MAX_CLIENTS] = {};

	for(int Index = 0; pAttackSamples != nullptr && Index < NumAttackSamples; ++Index)
	{
		const SQmHammerAttackSample &Sample = pAttackSamples[Index];
		if(IsAttackCandidate(Sample, EventPos, EventTick) && apAttacks[Sample.m_ClientId] == nullptr)
			apAttacks[Sample.m_ClientId] = &Sample;
	}

	for(int Index = 0; pTargetSamples != nullptr && Index < NumTargetSamples; ++Index)
	{
		const SQmHammerTargetSample &Sample = pTargetSamples[Index];
		if(IsTargetCandidate(Sample, EventPos) && apTargets[Sample.m_ClientId] == nullptr)
			apTargets[Sample.m_ClientId] = &Sample;
	}

	SQmHammerHitMatch Result;
	bool HaveCandidate = false;
	for(int AttackerId = 0; AttackerId < MAX_CLIENTS; ++AttackerId)
	{
		if(apAttacks[AttackerId] == nullptr)
			continue;
		for(int TargetId = 0; TargetId < MAX_CLIENTS; ++TargetId)
		{
			if(apTargets[TargetId] == nullptr || !CanHammerTarget(*apAttacks[AttackerId], *apTargets[TargetId]))
				continue;
			if(!HaveCandidate)
			{
				Result.m_AttackerId = AttackerId;
				Result.m_TargetId = TargetId;
				HaveCandidate = true;
			}
			else
			{
				if(Result.m_AttackerId != AttackerId)
					Result.m_AttackerId = -1;
				if(Result.m_TargetId != TargetId)
					Result.m_TargetId = -1;
			}
		}
	}
	return Result;
}

void CQmHammerHitTracker::Reset()
{
	for(auto &Entry : m_aEntries)
		Entry = SEntry();
	m_NextEntry = 0;
	m_NextSequence = 1;
}

bool CQmHammerHitTracker::Record(const SQmHammerHitRecord &Record)
{
	if(Record.m_SnapshotTick < 0 || Record.m_EventOrdinal < 0 || Record.m_AttackerId < -1 || Record.m_AttackerId >= MAX_CLIENTS ||
		Record.m_TargetId < -1 || Record.m_TargetId >= MAX_CLIENTS || Record.m_Connection < -1 ||
		(Record.m_AttackerId >= 0 && Record.m_AttackerId == Record.m_TargetId))
	{
		return false;
	}
	for(const SEntry &Entry : m_aEntries)
	{
		if(Entry.m_Sequence != 0 && Entry.m_Record.m_Connection == Record.m_Connection &&
			Entry.m_Record.m_SnapshotTick == Record.m_SnapshotTick && Entry.m_Record.m_EventOrdinal == Record.m_EventOrdinal)
		{
			return false;
		}
	}

	SEntry &Entry = m_aEntries[m_NextEntry];
	Entry = SEntry();
	Entry.m_Record = Record;
	Entry.m_Sequence = m_NextSequence++;
	m_NextEntry = (m_NextEntry + 1) % MAX_RECORDS;
	return true;
}

bool CQmHammerHitTracker::FindLatest(int AttackerId, int TargetId, int MinSnapshotTick, SQmHammerHitRecord *pResult, int Connection) const
{
	const SEntry *pBest = nullptr;
	for(const SEntry &Entry : m_aEntries)
	{
		if(Entry.m_Sequence == 0 || Entry.m_Record.m_SnapshotTick < MinSnapshotTick ||
			!ClientMatchesFilter(Entry.m_Record.m_AttackerId, AttackerId) ||
			!ClientMatchesFilter(Entry.m_Record.m_TargetId, TargetId) ||
			!ConnectionMatchesFilter(Entry.m_Record.m_Connection, Connection))
		{
			continue;
		}
		if(pBest == nullptr || Entry.m_Record.m_SnapshotTick > pBest->m_Record.m_SnapshotTick ||
			(Entry.m_Record.m_SnapshotTick == pBest->m_Record.m_SnapshotTick && Entry.m_Sequence > pBest->m_Sequence))
		{
			pBest = &Entry;
		}
	}
	if(pBest == nullptr)
		return false;
	if(pResult != nullptr)
		*pResult = pBest->m_Record;
	return true;
}

int CQmHammerHitTracker::FindLatestTargets(int AttackerId, int MinSnapshotTick, SQmHammerHitRecord *pResults, int MaxResults, int Connection) const
{
	if(AttackerId < 0 || AttackerId >= MAX_CLIENTS || pResults == nullptr || MaxResults <= 0)
		return 0;
	const SEntry *apBest[MAX_CLIENTS] = {};
	for(const SEntry &Entry : m_aEntries)
	{
		const int TargetId = Entry.m_Record.m_TargetId;
		if(Entry.m_Sequence == 0 || Entry.m_Record.m_AttackerId != AttackerId || TargetId < 0 || TargetId >= MAX_CLIENTS ||
			Entry.m_Record.m_SnapshotTick < MinSnapshotTick || !ConnectionMatchesFilter(Entry.m_Record.m_Connection, Connection))
		{
			continue;
		}
		const SEntry *pBest = apBest[TargetId];
		if(pBest == nullptr || Entry.m_Record.m_SnapshotTick > pBest->m_Record.m_SnapshotTick ||
			(Entry.m_Record.m_SnapshotTick == pBest->m_Record.m_SnapshotTick && Entry.m_Sequence > pBest->m_Sequence))
		{
			apBest[TargetId] = &Entry;
		}
	}

	int NumResults = 0;
	for(int TargetId = 0; TargetId < MAX_CLIENTS && NumResults < MaxResults; ++TargetId)
	{
		if(apBest[TargetId] != nullptr)
			pResults[NumResults++] = apBest[TargetId]->m_Record;
	}
	return NumResults;
}

int CQmHammerHitTracker::FindTargetHitsAtTick(int TargetId, int SnapshotTick, SQmHammerHitRecord *pResults, int MaxResults, int Connection) const
{
	if(TargetId < 0 || TargetId >= MAX_CLIENTS || SnapshotTick < 0 || pResults == nullptr || MaxResults <= 0)
		return 0;
	int NumResults = 0;
	for(const SEntry &Entry : m_aEntries)
	{
		if(Entry.m_Sequence == 0 || Entry.m_Record.m_TargetId != TargetId || Entry.m_Record.m_SnapshotTick != SnapshotTick ||
			!ConnectionMatchesFilter(Entry.m_Record.m_Connection, Connection))
		{
			continue;
		}
		if(NumResults < MaxResults)
			pResults[NumResults++] = Entry.m_Record;
	}
	return NumResults;
}

EQmHammerHitRelation QmClassifyHammerHitRelation(const SQmHammerHitRecord *pRecord, int TargetId, int LocalClientId, int LocalDummyId)
{
	if(pRecord == nullptr || pRecord->m_TargetId != TargetId || pRecord->m_AttackerId < 0)
		return EQmHammerHitRelation::NONE;
	if(pRecord->m_AttackerId == TargetId)
		return EQmHammerHitRelation::SELF;
	const bool TargetIsMain = TargetId >= 0 && TargetId == LocalClientId;
	const bool TargetIsDummy = TargetId >= 0 && TargetId == LocalDummyId;
	if((TargetIsMain && pRecord->m_AttackerId == LocalDummyId) || (TargetIsDummy && pRecord->m_AttackerId == LocalClientId))
		return EQmHammerHitRelation::COUNTERPART;
	return EQmHammerHitRelation::EXTERNAL;
}

SQmHammerWakeupDecision QmDecideHammerWakeupActions(const SQmHammerWakeupDecisionInput &Input)
{
	SQmHammerWakeupDecision Result;
	const int Active = Input.m_ActiveConnection == 1 ? 1 : 0;
	bool aExternalWakeup[2];
	for(int Connection = 0; Connection < 2; ++Connection)
	{
		aExternalWakeup[Connection] = Input.m_aExternalHammerWakeup[Connection] && Input.m_aWasInFreeze[Connection] && !Input.m_aInFreeze[Connection];
		Result.m_aShowPopup[Connection] = Input.m_ShowPopup && aExternalWakeup[Connection];
	}

	const bool ActiveExternalWakeup = aExternalWakeup[Active];
	Result.m_UnspecActiveConnection = Input.m_AutoUnspec && Input.m_ActiveSpectating && ActiveExternalWakeup;
	Result.m_CloseChat = Input.m_AutoCloseChat && Input.m_ChatActive && ActiveExternalWakeup;

	if(Input.m_AutoSwitch && Input.m_aWasInFreeze[0] && Input.m_aWasInFreeze[1])
	{
		if(Active == 1 && aExternalWakeup[0] && Input.m_aInFreeze[1])
			Result.m_SwitchToConnection = 0;
		else if(Active == 0 && aExternalWakeup[1] && Input.m_aInFreeze[0])
			Result.m_SwitchToConnection = 1;
	}
	return Result;
}
