#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <vector>

// 练习分支重新 fork（分身接入/断开、重进、旁观返回）会把世界换回服务器那份，
// /setswitch 在练习里改过的开关状态必须跟着搬进新分支，否则跑图进度会被重置。
// 这里直接驱动生产开销函数，断言新世界里的状态与剩余时长。

namespace
{
	SSwitchers MakeSwitcher(bool Status, int EndTick, int LastUpdateTick)
	{
		SSwitchers Switcher{};
		Switcher.m_aStatus[0] = Status;
		Switcher.m_aEndTick[0] = EndTick;
		Switcher.m_aLastUpdateTick[0] = LastUpdateTick;
		return Switcher;
	}
}

TEST(QmPracticeSwitchCarry, TimedSwitchMovesToNewWorldWithRemainingDuration)
{
	const std::vector<SSwitchers> vSource = {MakeSwitcher(true, 1000, 900)};
	std::vector<SSwitchers> vTarget = {MakeSwitcher(false, 0, 0)};

	// 新分支比旧分支晚 250 tick：结束与更新时间一起后移，剩余时长不变。
	QmCarryPracticeSwitchers(vTarget, vSource, 250);

	ASSERT_EQ(vTarget.size(), 1u);
	EXPECT_TRUE(vTarget[0].m_aStatus[0]);
	EXPECT_EQ(vTarget[0].m_aEndTick[0], 1250);
	EXPECT_EQ(vTarget[0].m_aLastUpdateTick[0], 1150);
}

TEST(QmPracticeSwitchCarry, IndefiniteSwitchKeepsZeroTicks)
{
	const std::vector<SSwitchers> vSource = {MakeSwitcher(true, 0, 0)};
	std::vector<SSwitchers> vTarget = {MakeSwitcher(false, 0, 0)};

	QmCarryPracticeSwitchers(vTarget, vSource, -400);

	ASSERT_EQ(vTarget.size(), 1u);
	EXPECT_TRUE(vTarget[0].m_aStatus[0]);
	EXPECT_EQ(vTarget[0].m_aEndTick[0], 0);
	EXPECT_EQ(vTarget[0].m_aLastUpdateTick[0], 0);
}

TEST(QmPracticeSwitchCarry, EarlierReforkShiftsTimedSwitchBackwards)
{
	const std::vector<SSwitchers> vSource = {MakeSwitcher(true, 1000, 900)};
	std::vector<SSwitchers> vTarget = {MakeSwitcher(false, 0, 0)};

	QmCarryPracticeSwitchers(vTarget, vSource, -300);

	ASSERT_EQ(vTarget.size(), 1u);
	EXPECT_EQ(vTarget[0].m_aEndTick[0], 700);
	EXPECT_EQ(vTarget[0].m_aLastUpdateTick[0], 600);
}

TEST(QmPracticeSwitchCarry, SwitchCountMismatchLeavesTargetAsServerState)
{
	// 换图后开关数量对不上：整份放弃，新分支保持服务器世界的开关状态。
	const std::vector<SSwitchers> vSource = {MakeSwitcher(true, 1000, 900), MakeSwitcher(true, 2000, 1900)};
	std::vector<SSwitchers> vTarget = {MakeSwitcher(false, 0, 0)};

	QmCarryPracticeSwitchers(vTarget, vSource, 250);

	ASSERT_EQ(vTarget.size(), 1u);
	EXPECT_FALSE(vTarget[0].m_aStatus[0]);
	EXPECT_EQ(vTarget[0].m_aEndTick[0], 0);
}

TEST(QmPracticeSwitchCarry, CarriesEveryTeamSlot)
{
	std::vector<SSwitchers> vSource = {MakeSwitcher(false, 0, 0)};
	std::vector<SSwitchers> vTarget = {MakeSwitcher(false, 0, 0)};
	for(int Team = 0; Team < NUM_DDRACE_TEAMS; Team++)
	{
		vSource[0].m_aStatus[Team] = (Team % 2) == 0;
		vSource[0].m_aEndTick[Team] = 500 + Team;
		vSource[0].m_aType[Team] = TILE_SWITCHTIMEDOPEN;
		vSource[0].m_aLastUpdateTick[Team] = 400 + Team;
	}

	QmCarryPracticeSwitchers(vTarget, vSource, 100);

	ASSERT_EQ(vTarget.size(), 1u);
	for(int Team = 0; Team < NUM_DDRACE_TEAMS; Team++)
	{
		EXPECT_EQ(vTarget[0].m_aStatus[Team], vSource[0].m_aStatus[Team]) << "team " << Team;
		EXPECT_EQ(vTarget[0].m_aEndTick[Team], 600 + Team) << "team " << Team;
		EXPECT_EQ(vTarget[0].m_aType[Team], TILE_SWITCHTIMEDOPEN) << "team " << Team;
		EXPECT_EQ(vTarget[0].m_aLastUpdateTick[Team], 500 + Team) << "team " << Team;
	}
}
