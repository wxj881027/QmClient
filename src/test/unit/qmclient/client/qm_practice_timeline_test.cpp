#include <game/client/components/qmclient/practice_timeline.h>

#include <gtest/gtest.h>

#include <vector>

TEST(QmPracticeTimeline, ConvertsEventsAndSwitchStatesToTheViewFormat)
{
	std::vector<SLocalActorEvent> vEvents(1);
	vEvents[0].m_Tick = 42;
	vEvents[0].m_Type = NETEVENTTYPE_EXPLOSION;
	vEvents[0].m_aData[0] = 10;
	vEvents[0].m_aData[1] = 20;
	vEvents[0].m_aData[2] = -1;

	std::vector<SLocalSwitchState> vSwitchStates(1);
	vSwitchStates[0].m_Tick = 50;
	vSwitchStates[0].m_HighestSwitchNumber = 3;
	vSwitchStates[0].m_Team = 4;
	vSwitchStates[0].m_aStatus[0] = 0x80000001u;
	vSwitchStates[0].m_aStatus[7] = 0x2u;

	const qmclient::rank_ghost::SViewTimeline Timeline = QmBuildPracticeViewTimeline(vEvents, vSwitchStates);
	ASSERT_EQ(Timeline.m_vEvents.size(), 1u);
	EXPECT_EQ(Timeline.m_vEvents[0].m_RelTick, 42);
	EXPECT_EQ(Timeline.m_vEvents[0].m_Type, NETEVENTTYPE_EXPLOSION);
	EXPECT_EQ(Timeline.m_vEvents[0].m_aData[2], -1);
	ASSERT_EQ(Timeline.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_RelTick, 50);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_HighestSwitchNumber, 3);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_Team, 4);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_aStatus[0], 0x80000001u);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_aStatus[7], 0x2u);
	EXPECT_TRUE(Timeline.m_vMessages.empty());
}

TEST(QmPracticeTimeline, EmptyRecordingProducesAnEmptyTimeline)
{
	const qmclient::rank_ghost::SViewTimeline Timeline = QmBuildPracticeViewTimeline({}, {});
	EXPECT_TRUE(Timeline.m_vEvents.empty());
	EXPECT_TRUE(Timeline.m_vSwitchStates.empty());
	EXPECT_TRUE(Timeline.m_vMessages.empty());
}
