#include <game/client/components/qmclient/local_saves.h>

#include <gtest/gtest.h>

#include <limits>
#include <new>
#include <stdexcept>

namespace
{
	class QmLocalSavePostSend : public ::testing::Test
	{
	protected:
		QmLocalSaves::CConfirmation m_Confirmation;
		QmLocalSaves::CRestore m_Restore;

		void SetUp() override
		{
			m_Confirmation.Track({"map", "old", 0, 2, 50, false, 10000});
			m_Restore.Begin({"time", "A, B", "map", "old"}, {"A", "B"}, 100, 1000);
		}
		void ExpectStopped()
		{
			EXPECT_FALSE(m_Confirmation.Active());
			EXPECT_FALSE(m_Restore.Active());
			EXPECT_EQ(m_Confirmation.Message(0, -1, "Loading successfully done", "map", 2, 101), QmLocalSaves::EResult::NONE);
			EXPECT_EQ(m_Restore.Update({}, 101, 1000), QmLocalSaves::EAction::WAIT);
		}
	};
}

TEST_F(QmLocalSavePostSend, AllocationFailureStopsOldConfirmationAndAutomaticRestore)
{
	EXPECT_FALSE(QmLocalSaves::TrackAfterSend(m_Confirmation, m_Restore, [] { throw std::bad_alloc(); }));
	ExpectStopped();
}

TEST_F(QmLocalSavePostSend, LengthFailureStopsOldTrackingWithoutEscaping)
{
	EXPECT_FALSE(QmLocalSaves::TrackAfterSend(m_Confirmation, m_Restore, [] { throw std::length_error("load tracking"); }));
	ExpectStopped();
}

TEST_F(QmLocalSavePostSend, FailureAfterPublishingPartialNewTrackingClearsBothStates)
{
	EXPECT_FALSE(QmLocalSaves::TrackAfterSend(m_Confirmation, m_Restore, [&] {
		m_Confirmation.Reset();
		m_Confirmation.Track({"map", "new", 0, 2, 51, false, 10000});
		m_Restore.Begin({"time", "A, B", "map", "new"}, {"A", "B"}, 101, 1000);
		throw 7;
	}));
	ExpectStopped();
}

TEST_F(QmLocalSavePostSend, SuccessfulTrackingRetainsTheNewRequestAndRestore)
{
	EXPECT_TRUE(QmLocalSaves::TrackAfterSend(m_Confirmation, m_Restore, [&] {
		m_Confirmation.Reset();
		m_Confirmation.Track({"map", "new", 0, 2, 51, false, 10000});
		m_Restore.Begin({"time", "A, B", "map", "new"}, {"A", "B"}, 101, 1000);
	}));
	EXPECT_TRUE(m_Confirmation.Active());
	EXPECT_EQ(m_Confirmation.Request().m_Code, "new");
	EXPECT_TRUE(m_Restore.Active());
	EXPECT_EQ(m_Restore.Entry().m_Code, "new");
}

TEST_F(QmLocalSavePostSend, UnrelatedMessageNoOpKeepsTheExistingTracking)
{
	EXPECT_TRUE(QmLocalSaves::TrackAfterSend(m_Confirmation, m_Restore, [] {}));
	EXPECT_TRUE(m_Confirmation.Active());
	EXPECT_EQ(m_Confirmation.Request().m_Code, "old");
	EXPECT_TRUE(m_Restore.Active());
	EXPECT_EQ(m_Restore.Entry().m_Code, "old");
}

TEST_F(QmLocalSavePostSend, TakingTheRequestClearsTheSourceAndKeepsOwnedDataForCompletion)
{
	m_Confirmation.Reset();
	const std::string Map(96, 'm');
	const std::string Code(96, 'c');
	m_Confirmation.Track({Map, Code, 1, 2, 51, true, 10000});
	const auto Request = m_Confirmation.TakeRequest();
	EXPECT_FALSE(m_Confirmation.Active());
	EXPECT_EQ(Request.m_Map, Map);
	EXPECT_EQ(Request.m_Code, Code);
	EXPECT_EQ(Request.m_Conn, 1);
	EXPECT_EQ(Request.m_Team, 2);
	EXPECT_EQ(Request.m_SentTick, 51);
	EXPECT_TRUE(Request.m_WasRacing);
	EXPECT_EQ(Request.m_Deadline, 10000);
	m_Confirmation.Track({"next", "next", 0, 3, 52, false, 20000});
	EXPECT_EQ(Request.m_Map, Map);
	EXPECT_EQ(Request.m_Code, Code);
}

TEST_F(QmLocalSavePostSend, TakingAnAmbiguousRequestDoesNotContaminateTheNextConfirmation)
{
	m_Confirmation.Track({"map", "different", 0, 2, 51, false, 10000});
	ASSERT_EQ(m_Confirmation.Message(0, -1, "Loading successfully done", "map", 2, 101), QmLocalSaves::EResult::FAILED);
	EXPECT_EQ(m_Confirmation.TakeRequest().m_Code, "old");
	EXPECT_FALSE(m_Confirmation.Active());
	m_Confirmation.Track({"new map", "new", 0, 2, 52, false, 10000});
	EXPECT_EQ(m_Confirmation.Message(0, -1, "Loading successfully done", "new map", 2, 102), QmLocalSaves::EResult::SUCCESS);
}

TEST_F(QmLocalSavePostSend, MinimumWarmupTimerCannotConfirmAnEarlierRaceStart)
{
	EXPECT_FALSE(m_Confirmation.RestoredRaceFromWarmupTimer("map", 2, 60, std::numeric_limits<int>::min(), 10, 101));
	EXPECT_TRUE(m_Confirmation.Active());
}

TEST_F(QmLocalSavePostSend, EncodedRaceStartKeepsTheStrictTwoSecondBoundary)
{
	EXPECT_TRUE(m_Confirmation.RestoredRaceFromWarmupTimer("map", 2, 60, -29, 10, 101));
	EXPECT_FALSE(m_Confirmation.RestoredRaceFromWarmupTimer("map", 2, 60, -30, 10, 101));
	EXPECT_FALSE(m_Confirmation.RestoredRaceFromWarmupTimer("map", 2, 60, -31, 10, 101));
}

TEST_F(QmLocalSavePostSend, WideTickSpeedMultiplicationCannotTurnTheThresholdIntoAnEarlierRace)
{
	EXPECT_FALSE(m_Confirmation.RestoredRaceFromWarmupTimer("map", 2, 60, std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 101));
}
