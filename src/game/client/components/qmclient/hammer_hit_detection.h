// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HAMMER_HIT_DETECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HAMMER_HIT_DETECTION_H

#include <base/vmath.h>

#include <cstdint>

enum class EQmHammerHitRelation
{
	NONE,
	SELF,
	COUNTERPART,
	EXTERNAL,
};

struct SQmHammerAttackSample
{
	int m_ClientId = -1;
	int m_AttackTick = -1;
	int m_Weapon = -1;
	// QmClient: 挥锤后攻击者可能已经切换武器，快照到达时当前武器不再是锤，
	// 仅凭 m_Weapon 会漏判这一次的挥锤。
	int m_PrevWeapon = -1;
	bool m_HammerHitEnabled = true;
	vec2 m_PrevPos = vec2(0.0f, 0.0f);
	vec2 m_CurPos = vec2(0.0f, 0.0f);
	vec2 m_Direction = vec2(0.0f, -1.0f);
	float m_ProximityRadius = 28.0f;
	int m_DDTeam = 0;
	bool m_Solo = false;
	bool m_Super = false;
};

struct SQmHammerTargetSample
{
	int m_ClientId = -1;
	vec2 m_PrevPos = vec2(0.0f, 0.0f);
	vec2 m_CurPos = vec2(0.0f, 0.0f);
	float m_ProximityRadius = 28.0f;
	int m_DDTeam = 0;
	bool m_Solo = false;
	bool m_Super = false;
};

struct SQmHammerHitMatch
{
	int m_AttackerId = -1;
	int m_TargetId = -1;
};

struct SQmHammerHitRecord
{
	int m_AttackerId = -1;
	int m_TargetId = -1;
	int m_SnapshotTick = -1;
	int m_EventOrdinal = -1;
	vec2 m_Pos = vec2(0.0f, 0.0f);
	// The snapshot connection that observed this event, not the attacker's connection.
	int m_Connection = -1;
	bool m_TargetWoke = false;
};

bool QmIsHammerSuperTeam(int DDTeam, bool IsDDRace16);
bool QmIsHammerSuperTeam(int DDTeam, int NumDDRaceTeams);
bool QmIsHammerWakeupTransition(int PrevFreezeEnd, int CurFreezeEnd, int EventTick);

SQmHammerHitMatch QmMatchHammerHitEvent(
	vec2 EventPos,
	int EventTick,
	const SQmHammerAttackSample *pAttackSamples,
	int NumAttackSamples,
	const SQmHammerTargetSample *pTargetSamples,
	int NumTargetSamples);

class CQmHammerHitTracker
{
public:
	static constexpr int MAX_RECORDS = 512;
	static constexpr int ANY_CLIENT = -2;
	static constexpr int ANY_CONNECTION = -1;

	void Reset();
	bool Record(const SQmHammerHitRecord &Record);
	bool FindLatest(int AttackerId, int TargetId, int MinSnapshotTick, SQmHammerHitRecord *pResult = nullptr, int Connection = ANY_CONNECTION) const;
	int FindLatestTargets(int AttackerId, int MinSnapshotTick, SQmHammerHitRecord *pResults, int MaxResults, int Connection = ANY_CONNECTION) const;
	int FindTargetHitsAtTick(int TargetId, int SnapshotTick, SQmHammerHitRecord *pResults, int MaxResults, int Connection = ANY_CONNECTION) const;

private:
	struct SEntry
	{
		SQmHammerHitRecord m_Record;
		uint64_t m_Sequence = 0;
	};

	SEntry m_aEntries[MAX_RECORDS];
	int m_NextEntry = 0;
	uint64_t m_NextSequence = 1;
};

EQmHammerHitRelation QmClassifyHammerHitRelation(const SQmHammerHitRecord *pRecord, int TargetId, int LocalClientId, int LocalDummyId);

struct SQmHammerWakeupDecisionInput
{
	bool m_aWasInFreeze[2] = {false, false};
	bool m_aInFreeze[2] = {false, false};
	bool m_aExternalHammerWakeup[2] = {false, false};
	int m_ActiveConnection = 0;
	bool m_ActiveSpectating = false;
	bool m_ChatActive = false;
	bool m_ShowPopup = false;
	bool m_AutoUnspec = false;
	bool m_AutoSwitch = false;
	bool m_AutoCloseChat = false;
};

struct SQmHammerWakeupDecision
{
	bool m_aShowPopup[2] = {false, false};
	bool m_UnspecActiveConnection = false;
	bool m_CloseChat = false;
	int m_SwitchToConnection = -1;
};

SQmHammerWakeupDecision QmDecideHammerWakeupActions(const SQmHammerWakeupDecisionInput &Input);

#endif
