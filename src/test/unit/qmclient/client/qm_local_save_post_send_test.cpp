#include <game/client/components/qmclient/local_saves.h>

#include <gtest/gtest.h>

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
