#include <game/client/components/qmclient/practice_commands.h>

#include <gtest/gtest.h>

namespace
{
	SQmPracticeSavedTee MakeTee(vec2 Pos, int ActiveWeapon = WEAPON_HAMMER)
	{
		SQmPracticeSavedTee Tee;
		Tee.m_Valid = true;
		Tee.m_Pos = Pos;
		Tee.m_ActiveWeapon = ActiveWeapon;
		Tee.m_aWeapons[ActiveWeapon].m_Got = true;
		return Tee;
	}
}

TEST(QmPracticeCommands, DeathWithoutAnAutoRescueSnapshotRecordsNothing)
{
	SQmPracticeCommandRecords Records;
	EXPECT_FALSE(QmRecordPracticeDeath(Records, SQmPracticeSavedTee{}));
	EXPECT_FALSE(Records.m_HasLastDeath);
}

TEST(QmPracticeCommands, DeathRecordsThePreDeathAutoRescueTee)
{
	SQmPracticeCommandRecords Records;
	const SQmPracticeSavedTee AutoTee = MakeTee(vec2(128.0f, 64.0f), WEAPON_LASER);

	EXPECT_TRUE(QmRecordPracticeDeath(Records, AutoTee));
	EXPECT_TRUE(Records.m_HasLastDeath);
	EXPECT_EQ(Records.m_LastDeathTee.m_Pos, AutoTee.m_Pos);
	EXPECT_EQ(Records.m_LastDeathTee.m_ActiveWeapon, WEAPON_LASER);
}

TEST(QmPracticeCommands, CommandTeleportKeepsTheBackRecordUntouched)
{
	SQmPracticeCommandRecords Records;
	const SQmPracticeSavedTee DeathTee = MakeTee(vec2(32.0f, 32.0f));
	QmRecordPracticeDeath(Records, DeathTee);

	QmRecordPracticeCommandTeleport(Records, MakeTee(vec2(320.0f, 160.0f), WEAPON_SHOTGUN));

	EXPECT_TRUE(Records.m_HasLastDeath);
	EXPECT_EQ(Records.m_LastDeathTee.m_Pos, DeathTee.m_Pos);
	EXPECT_TRUE(Records.m_HasLastTeleport);
	EXPECT_EQ(Records.m_LastTeleportTee.m_Pos, vec2(320.0f, 160.0f));
}

TEST(QmPracticeCommands, CommandTeleportRecordRequiresAValidTee)
{
	SQmPracticeCommandRecords Records;
	QmRecordPracticeCommandTeleport(Records, SQmPracticeSavedTee{});
	EXPECT_FALSE(Records.m_HasLastTeleport);
}

TEST(QmPracticeCommands, BackDoesNothingWithoutADeathRecord)
{
	const SQmPracticeBackDecision Decision = QmEvaluatePracticeBack(false, true, false);
	EXPECT_FALSE(Decision.m_AssignRescueTee);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeCommands, BackAssignsButDoesNotTeleportWithoutAModeSavePoint)
{
	// 服务器 ConBack 只赋值救援槽，改动不置位 m_aSetSavePos，手动模式没设过点时 Rescue() 失败。
	const SQmPracticeBackDecision Decision = QmEvaluatePracticeBack(true, false, false);
	EXPECT_TRUE(Decision.m_AssignRescueTee);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeCommands, BackAssignsButDoesNotTeleportWhileSuperOrInvincible)
{
	const SQmPracticeBackDecision Decision = QmEvaluatePracticeBack(true, true, true);
	EXPECT_TRUE(Decision.m_AssignRescueTee);
	EXPECT_FALSE(Decision.m_Teleport);
}

TEST(QmPracticeCommands, BackTeleportsWhenTheModeHasASavedPoint)
{
	const SQmPracticeBackDecision Decision = QmEvaluatePracticeBack(true, true, false);
	EXPECT_TRUE(Decision.m_AssignRescueTee);
	EXPECT_TRUE(Decision.m_Teleport);
}

TEST(QmPracticeCommands, TeleOutIndexPicksAnyExitWithinRange)
{
	EXPECT_EQ(QmPracticeTeleOutIndex(0, 0), -1);
	EXPECT_EQ(QmPracticeTeleOutIndex(1, 7), 0);
	EXPECT_EQ(QmPracticeTeleOutIndex(3, 0), 0);
	EXPECT_EQ(QmPracticeTeleOutIndex(3, 2), 2);
	EXPECT_EQ(QmPracticeTeleOutIndex(3, 4), 1);
	// 负值只可能来自外部输入，退化成第 0 个出口而不是越界。
	EXPECT_EQ(QmPracticeTeleOutIndex(3, -5), 0);
}

TEST(QmPracticeCommands, LastTeleportRecordRestoresTheSavedTee)
{
	CCharacterCore Source{};
	Source.m_Pos = vec2(512.0f, 256.0f);
	Source.m_ActiveWeapon = WEAPON_GRENADE;
	Source.m_aWeapons[WEAPON_GRENADE].m_Got = true;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	SQmPracticeCommandRecords Records;
	QmRecordPracticeCommandTeleport(Records, Tee);
	ASSERT_TRUE(Records.m_HasLastTeleport);

	CCharacterCore Target{};
	QmApplyPracticeRescueCore(Target, 1000, Records.m_LastTeleportTee);

	EXPECT_EQ(Target.m_Pos, Source.m_Pos);
	EXPECT_EQ(Target.m_ActiveWeapon, WEAPON_GRENADE);
	EXPECT_TRUE(Target.m_aWeapons[WEAPON_GRENADE].m_Got);
}

TEST(QmPracticeCommands, LastTeleportRecordDropsWeaponsPickedUpAfterwards)
{
	CCharacterCore Source{};
	Source.m_ActiveWeapon = WEAPON_HAMMER;
	Source.m_aWeapons[WEAPON_HAMMER].m_Got = true;

	SQmPracticeSavedTee Tee;
	QmCapturePracticeRescueCore(Source, 1000, Tee);

	SQmPracticeCommandRecords Records;
	QmRecordPracticeCommandTeleport(Records, Tee);

	CCharacterCore Target{};
	Target.m_ActiveWeapon = WEAPON_LASER;
	Target.m_aWeapons[WEAPON_LASER].m_Got = true;
	QmApplyPracticeRescueCore(Target, 1000, Records.m_LastTeleportTee);

	EXPECT_TRUE(Target.m_aWeapons[WEAPON_HAMMER].m_Got);
	EXPECT_FALSE(Target.m_aWeapons[WEAPON_LASER].m_Got);
	EXPECT_EQ(Target.m_ActiveWeapon, WEAPON_HAMMER);
}

TEST(QmPracticeCommands, SwitchIndexAcceptsTheHighestNumberLikeTheServer)
{
	// 服务器 in_range(Switch, size-1) 是闭区间：size 为 2 时编号 0 与 1 都能设。
	EXPECT_TRUE(QmPracticeSwitchIndexValid(0, 2));
	EXPECT_TRUE(QmPracticeSwitchIndexValid(1, 2));
	EXPECT_FALSE(QmPracticeSwitchIndexValid(2, 2));
}

TEST(QmPracticeCommands, SwitchIndexRejectsNegativeAndEmptyTables)
{
	EXPECT_FALSE(QmPracticeSwitchIndexValid(-1, 2));
	EXPECT_FALSE(QmPracticeSwitchIndexValid(0, 0));
}
