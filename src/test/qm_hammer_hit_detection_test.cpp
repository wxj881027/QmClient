// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/hammer_hit_detection.h>
#include <game/teamscore.h>

#include <gtest/gtest.h>

namespace
{
	SQmHammerAttackSample Attack(int ClientId, int AttackTick, vec2 Pos, vec2 Direction, int Team = 1, int Weapon = WEAPON_HAMMER)
	{
		SQmHammerAttackSample Sample;
		Sample.m_ClientId = ClientId;
		Sample.m_AttackTick = AttackTick;
		Sample.m_Weapon = Weapon;
		Sample.m_PrevPos = Pos;
		Sample.m_CurPos = Pos;
		Sample.m_Direction = Direction;
		Sample.m_DDTeam = Team;
		return Sample;
	}

	SQmHammerTargetSample Target(int ClientId, vec2 PrevPos, vec2 CurPos, int Team = 1)
	{
		SQmHammerTargetSample Sample;
		Sample.m_ClientId = ClientId;
		Sample.m_PrevPos = PrevPos;
		Sample.m_CurPos = CurPos;
		Sample.m_DDTeam = Team;
		return Sample;
	}

	SQmHammerHitRecord Hit(int AttackerId, int TargetId, int Tick, int Ordinal, int Connection = 0, bool TargetWoke = false)
	{
		return {AttackerId, TargetId, Tick, Ordinal, vec2((float)Ordinal, 0.0f), Connection, TargetWoke};
	}
}

TEST(QmHammerHitDetection, KeepsAttackerUnknownWhenMultipleHammerCentersArePhysicallyPossible)
{
	const SQmHammerAttackSample aAttacks[] = {
		Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)),
		Attack(2, 100, vec2(30.0f, 0.0f), vec2(-1.0f, 0.0f)),
	};
	const SQmHammerTargetSample aTargets[] = {Target(3, vec2(35.0f, 0.0f), vec2(45.0f, 0.0f))};

	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 2, aTargets, 1);
	EXPECT_EQ(Match.m_AttackerId, -1);
	EXPECT_EQ(Match.m_TargetId, 3);
}

TEST(QmHammerHitDetection, UsesCollisionCompatibilityToResolveTheOnlyPossiblePair)
{
	const SQmHammerAttackSample aAttacks[] = {
		Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), 1),
		Attack(2, 100, vec2(30.0f, 0.0f), vec2(-1.0f, 0.0f), 2),
	};
	const SQmHammerTargetSample TargetSample = Target(3, vec2(35.0f, 0.0f), vec2(45.0f, 0.0f), 1);
	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 2, &TargetSample, 1);
	EXPECT_EQ(Match.m_AttackerId, 1);
	EXPECT_EQ(Match.m_TargetId, 3);
}

TEST(QmHammerHitDetection, KeepsOnlyCommonIdsWhenPairsAreAmbiguous)
{
	const SQmHammerAttackSample aAttacks[] = {
		Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)),
		Attack(2, 100, vec2(42.0f, 0.0f), vec2(-1.0f, 0.0f)),
	};
	const SQmHammerTargetSample TargetSample = Target(3, vec2(21.0f, 0.0f), vec2(21.0f, 0.0f));

	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 2, &TargetSample, 1);
	EXPECT_EQ(Match.m_AttackerId, -1);
	EXPECT_EQ(Match.m_TargetId, 3);
}

TEST(QmHammerHitDetection, RejectsStaleNonHammerAndHitDisabledAttacks)
{
	SQmHammerAttackSample Disabled = Attack(3, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f));
	Disabled.m_HammerHitEnabled = false;
	const SQmHammerAttackSample aAttacks[] = {
		// 事件缓冲跨快照后最多晚 3 tick 送达（实机日志证实 Age=2 常见），更旧的拒绝。
		Attack(1, 96, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)),
		Attack(2, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), 1, WEAPON_GUN),
		Disabled,
	};
	const SQmHammerTargetSample TargetSample = Target(4, vec2(21.0f, 0.0f), vec2(21.0f, 0.0f));

	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 3, &TargetSample, 1);
	EXPECT_EQ(Match.m_AttackerId, -1);
}

TEST(QmHammerHitDetection, AcceptsAttackDeliveredAcrossSnapshotBoundaries)
{
	// 快照 tick 落后挥锤 tick 2-3 tick 时攻击者仍可被推断（实机数据：Age=2 常见）。
	const SQmHammerAttackSample aAttacks[] = {
		Attack(1, 97, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)),
		Attack(2, 98, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f)),
	};
	const SQmHammerTargetSample TargetSample = Target(4, vec2(21.0f, 0.0f), vec2(21.0f, 0.0f));

	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 2, &TargetSample, 1);
	EXPECT_EQ(Match.m_AttackerId, -1); // 两个候选都合法，归属歧义仍保持保守
	EXPECT_EQ(QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, aAttacks, 1, &TargetSample, 1).m_AttackerId, 1);
}

TEST(QmHammerHitDetection, MatchesOneTickOldAttackWithoutTrustingCurrentAim)
{
	SQmHammerAttackSample AttackSample = Attack(1, 99, vec2(0.0f, 0.0f), vec2(-1.0f, 0.0f));
	AttackSample.m_CurPos = vec2(8.0f, 0.0f);
	const SQmHammerTargetSample TargetSample = Target(3, vec2(35.0f, 0.0f), vec2(45.0f, 0.0f));
	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &AttackSample, 1, &TargetSample, 1);
	EXPECT_EQ(Match.m_AttackerId, 1);
	EXPECT_EQ(Match.m_TargetId, 3);
}

TEST(QmHammerHitDetection, MatchesTargetAcrossNormalSnapshotMovement)
{
	const SQmHammerAttackSample AttackSample = Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f));
	const SQmHammerTargetSample aTargets[] = {
		Target(3, vec2(35.0f, 0.0f), vec2(55.0f, 0.0f)),
		Target(4, vec2(21.0f, 80.0f), vec2(21.0f, 90.0f)),
	};

	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &AttackSample, 1, aTargets, 2);
	EXPECT_EQ(Match.m_TargetId, 3);
}

TEST(QmHammerHitDetection, DoesNotTreatTeleportAsContinuousMovement)
{
	const SQmHammerAttackSample AttackSample = Attack(1, 100, vec2(79.0f, 0.0f), vec2(1.0f, 0.0f));
	const SQmHammerTargetSample TeleportingTarget = Target(3, vec2(0.0f, 0.0f), vec2(200.0f, 0.0f));
	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(100.0f, 0.0f), 100, &AttackSample, 1, &TeleportingTarget, 1);
	EXPECT_EQ(Match.m_TargetId, -1);
}

TEST(QmHammerHitDetection, EnforcesTeamAndSoloCollisionCompatibility)
{
	const SQmHammerAttackSample AttackSample = Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), 1);
	SQmHammerTargetSample OtherTeam = Target(2, vec2(21.0f, 0.0f), vec2(21.0f, 0.0f), 2);
	SQmHammerTargetSample SameTeam = Target(3, vec2(30.0f, 0.0f), vec2(30.0f, 0.0f), 1);
	const SQmHammerTargetSample aTargets[] = {OtherTeam, SameTeam};

	EXPECT_EQ(QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &AttackSample, 1, aTargets, 2).m_TargetId, 3);
	SameTeam.m_Solo = true;
	EXPECT_EQ(QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &AttackSample, 1, &SameTeam, 1).m_TargetId, -1);
}

TEST(QmHammerHitDetection, SupportsCurrentAndLegacySuperTeams)
{
	EXPECT_TRUE(QmIsHammerSuperTeam(TEAM_SUPER, false));
	EXPECT_FALSE(QmIsHammerSuperTeam(VANILLA_TEAM_SUPER, false));
	EXPECT_TRUE(QmIsHammerSuperTeam(VANILLA_TEAM_SUPER, true));
	EXPECT_FALSE(QmIsHammerSuperTeam(TEAM_SUPER, true));
	EXPECT_TRUE(QmIsHammerSuperTeam(VANILLA_TEAM_SUPER, VANILLA_MAX_CLIENTS + 1));
	EXPECT_TRUE(QmIsHammerSuperTeam(LEGACY_TEAM_SUPER, LEGACY_MAX_CLIENTS + 1));
	EXPECT_TRUE(QmIsHammerSuperTeam(TEAM_SUPER, NUM_DDRACE_TEAMS));
	EXPECT_FALSE(QmIsHammerSuperTeam(TEAM_SUPER, 0));

	SQmHammerAttackSample SuperAttack = Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), TEAM_SUPER);
	SuperAttack.m_Super = true;
	const SQmHammerTargetSample OtherTeam = Target(2, vec2(21.0f, 0.0f), vec2(21.0f, 0.0f), 2);
	EXPECT_EQ(QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &SuperAttack, 1, &OtherTeam, 1).m_TargetId, 2);
}

TEST(QmHammerHitDetection, HammerWakeupRequiresFreezeToOutliveEventTick)
{
	EXPECT_TRUE(QmIsHammerWakeupTransition(-1, 0, 100));
	EXPECT_TRUE(QmIsHammerWakeupTransition(101, 0, 100));
	EXPECT_FALSE(QmIsHammerWakeupTransition(100, 0, 100));
	EXPECT_FALSE(QmIsHammerWakeupTransition(99, 0, 100));
	EXPECT_FALSE(QmIsHammerWakeupTransition(101, 102, 100));
	EXPECT_FALSE(QmIsHammerWakeupTransition(101, 0, -1));
}

TEST(QmHammerHitDetection, KeepsAttackerKnownForOneSwingWithAmbiguousTargets)
{
	const SQmHammerAttackSample AttackSample = Attack(1, 100, vec2(0.0f, 0.0f), vec2(1.0f, 0.0f));
	const SQmHammerTargetSample aTargets[] = {
		Target(2, vec2(20.0f, 0.0f), vec2(20.0f, 0.0f)),
		Target(3, vec2(22.0f, 0.0f), vec2(22.0f, 0.0f)),
	};
	const SQmHammerHitMatch Match = QmMatchHammerHitEvent(vec2(21.0f, 0.0f), 100, &AttackSample, 1, aTargets, 2);
	EXPECT_EQ(Match.m_AttackerId, 1);
	EXPECT_EQ(Match.m_TargetId, -1);
}

TEST(QmHammerHitTracker, DeduplicatesRawSnapshotEventIdentity)
{
	CQmHammerHitTracker Tracker;
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Record(Hit(1, 3, 120, 4)));
	EXPECT_FALSE(Tracker.Record(Hit(1, 3, 120, 4)));
	EXPECT_TRUE(Tracker.Record(Hit(1, 4, 120, 5)));
	EXPECT_TRUE(Tracker.FindLatest(1, 3, 120));
	EXPECT_FALSE(Tracker.FindLatest(2, 3, 120));
}

TEST(QmHammerHitTracker, SeparatesObservationConnectionsButAllowsExplicitSharedView)
{
	CQmHammerHitTracker Tracker;
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Record(Hit(1, 3, 200, 1, 0)));
	EXPECT_TRUE(Tracker.Record(Hit(2, 4, 200, 1, 1)));
	EXPECT_TRUE(Tracker.FindLatest(1, 3, 200, nullptr, 0));
	EXPECT_FALSE(Tracker.FindLatest(1, 3, 200, nullptr, 1));
	EXPECT_TRUE(Tracker.FindLatest(1, 3, 200, nullptr, CQmHammerHitTracker::ANY_CONNECTION));
}

TEST(QmHammerHitTracker, ReturnsLatestHitForEveryTargetInOneScan)
{
	CQmHammerHitTracker Tracker;
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Record(Hit(1, 2, 300, 1)));
	EXPECT_TRUE(Tracker.Record(Hit(1, 2, 301, 2)));
	EXPECT_TRUE(Tracker.Record(Hit(1, 3, 301, 3)));
	SQmHammerHitRecord aHits[MAX_CLIENTS];
	const int NumHits = Tracker.FindLatestTargets(1, 300, aHits, MAX_CLIENTS);
	ASSERT_EQ(NumHits, 2);
	EXPECT_EQ(aHits[0].m_TargetId, 2);
	EXPECT_EQ(aHits[0].m_SnapshotTick, 301);
	EXPECT_EQ(aHits[1].m_TargetId, 3);
}

TEST(QmHammerHitTracker, WakeupLookupRequiresExactSnapshotTickAndKeepsAllAttackers)
{
	CQmHammerHitTracker Tracker;
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Record(Hit(1, 3, 400, 1, 0, true)));
	EXPECT_TRUE(Tracker.Record(Hit(2, 3, 400, 2, 0, true)));
	SQmHammerHitRecord aHits[MAX_CLIENTS];
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(3, 399, aHits, MAX_CLIENTS), 0);
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(3, 400, aHits, MAX_CLIENTS), 2);
}

TEST(QmHammerHitTracker, ClassifiesCounterpartAndExternalHits)
{
	SQmHammerHitRecord Record = Hit(11, 10, 500, 1);
	EXPECT_EQ(QmClassifyHammerHitRelation(&Record, 10, 10, 11), EQmHammerHitRelation::COUNTERPART);
	Record.m_AttackerId = 42;
	EXPECT_EQ(QmClassifyHammerHitRelation(&Record, 10, 10, 11), EQmHammerHitRelation::EXTERNAL);
	EXPECT_EQ(QmClassifyHammerHitRelation(&Record, 9, 10, 11), EQmHammerHitRelation::NONE);
}

TEST(QmHammerWakeupDecision, ActiveExternalWakeupTriggersOnlyActiveUiActions)
{
	SQmHammerWakeupDecisionInput Input;
	Input.m_aWasInFreeze[0] = true;
	Input.m_aInFreeze[0] = false;
	Input.m_aExternalHammerWakeup[0] = true;
	Input.m_ActiveConnection = 0;
	Input.m_ActiveSpectating = true;
	Input.m_ChatActive = true;
	Input.m_ShowPopup = true;
	Input.m_AutoUnspec = true;
	Input.m_AutoCloseChat = true;
	const SQmHammerWakeupDecision Decision = QmDecideHammerWakeupActions(Input);
	EXPECT_TRUE(Decision.m_aShowPopup[0]);
	EXPECT_TRUE(Decision.m_UnspecActiveConnection);
	EXPECT_TRUE(Decision.m_CloseChat);
	EXPECT_EQ(Decision.m_SwitchToConnection, -1);
}

TEST(QmHammerWakeupDecision, InactiveExternalWakeupSwitchesOnlyAfterBothWereFrozen)
{
	SQmHammerWakeupDecisionInput Input;
	Input.m_aWasInFreeze[0] = true;
	Input.m_aWasInFreeze[1] = true;
	Input.m_aInFreeze[0] = false;
	Input.m_aInFreeze[1] = true;
	Input.m_aExternalHammerWakeup[0] = true;
	Input.m_ActiveConnection = 1;
	Input.m_ChatActive = true;
	Input.m_ShowPopup = true;
	Input.m_AutoSwitch = true;
	Input.m_AutoCloseChat = true;
	const SQmHammerWakeupDecision Decision = QmDecideHammerWakeupActions(Input);
	EXPECT_TRUE(Decision.m_aShowPopup[0]);
	EXPECT_FALSE(Decision.m_CloseChat);
	EXPECT_EQ(Decision.m_SwitchToConnection, 0);

	Input.m_aWasInFreeze[1] = false;
	EXPECT_EQ(QmDecideHammerWakeupActions(Input).m_SwitchToConnection, -1);
}

TEST(QmHammerWakeupDecision, NaturalCounterpartAndSimultaneousUnfreezeDoNotTrigger)
{
	SQmHammerWakeupDecisionInput Input;
	Input.m_aWasInFreeze[0] = true;
	Input.m_aWasInFreeze[1] = true;
	Input.m_aInFreeze[0] = false;
	Input.m_aInFreeze[1] = false;
	Input.m_ActiveConnection = 0;
	Input.m_ActiveSpectating = true;
	Input.m_ChatActive = true;
	Input.m_ShowPopup = true;
	Input.m_AutoUnspec = true;
	Input.m_AutoSwitch = true;
	Input.m_AutoCloseChat = true;
	const SQmHammerWakeupDecision Decision = QmDecideHammerWakeupActions(Input);
	EXPECT_FALSE(Decision.m_aShowPopup[0]);
	EXPECT_FALSE(Decision.m_UnspecActiveConnection);
	EXPECT_FALSE(Decision.m_CloseChat);
	EXPECT_EQ(Decision.m_SwitchToConnection, -1);
}

TEST(QmHammerWakeupDecision, ExternalFlagWithoutFreezeTransitionDoesNotTrigger)
{
	SQmHammerWakeupDecisionInput Input;
	Input.m_aExternalHammerWakeup[0] = true;
	Input.m_ActiveConnection = 0;
	Input.m_ActiveSpectating = true;
	Input.m_ChatActive = true;
	Input.m_ShowPopup = true;
	Input.m_AutoUnspec = true;
	Input.m_AutoCloseChat = true;
	const SQmHammerWakeupDecision Decision = QmDecideHammerWakeupActions(Input);
	EXPECT_FALSE(Decision.m_aShowPopup[0]);
	EXPECT_FALSE(Decision.m_UnspecActiveConnection);
	EXPECT_FALSE(Decision.m_CloseChat);
}
