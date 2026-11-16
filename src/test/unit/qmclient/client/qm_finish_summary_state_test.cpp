#include <game/client/components/qmclient/finish_summary_state.h>

#include <gtest/gtest.h>

namespace
{
	QmFinishSummary::SSnapshot Finish(int64_t TimeMs = 100000)
	{
		QmFinishSummary::SSnapshot Snapshot;
		Snapshot.m_Community = QmFinishSummary::ECommunity::DDNET;
		Snapshot.m_Map = "Test map";
		Snapshot.m_Player = "Test player";
		Snapshot.m_ClientId = 7;
		Snapshot.m_TimeMs = TimeMs;
		return Snapshot;
	}
}

TEST(QmFinishSummaryState, StartsWithoutVisibleOrRetainedSummary)
{
	QmFinishSummary::CState State;
	EXPECT_FALSE(State.Visible());
	EXPECT_EQ(State.Snapshot(), nullptr);
	EXPECT_EQ(State.MutableSnapshot(), nullptr);
}

TEST(QmFinishSummaryState, NewFinishAssignsIdentityAndOwnsItsSnapshot)
{
	QmFinishSummary::CState State;
	auto Input = Finish();
	Input.m_Id = 900;
	ASSERT_TRUE(State.Begin(Input, 10.0, 100));
	Input.m_Player = "Changed player";

	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_TRUE(State.Visible());
	EXPECT_EQ(State.Snapshot()->m_Id, 1u);
	EXPECT_EQ(State.Snapshot()->m_Player, "Test player");
}

TEST(QmFinishSummaryState, CentisecondChatAndMillisecondMessageShareOneEvent)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(100000), 10.0, -1));
	EXPECT_FALSE(State.Begin(Finish(100009), 10.1, -1));
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 1u);
	EXPECT_EQ(State.Snapshot()->m_TimeMs, 100000);
}

TEST(QmFinishSummaryState, KnownRunSuppressesLateMessageWithDifferentPrecision)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(100000), 10.0, 100));
	EXPECT_FALSE(State.Begin(Finish(100001), 30.0, 100));
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 1u);
}

TEST(QmFinishSummaryState, RoundedChatAndTruncatedMessageShareOneEventAcrossCentisecondBoundary)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(100020), 10.0, -1));
	EXPECT_FALSE(State.Begin(Finish(100019), 10.1, -1));
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 1u);
}

TEST(QmFinishSummaryState, DuplicateCanSupplyRunIdentityForLaterMessages)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(100000), 10.0, -1));
	EXPECT_FALSE(State.Begin(Finish(100001), 10.1, 100));
	EXPECT_FALSE(State.Begin(Finish(100001), 30.0, 100));
}

TEST(QmFinishSummaryState, DistinctKnownRunAcceptsSameTimeInsideDuplicateWindow)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	EXPECT_TRUE(State.Begin(Finish(), 10.1, 200));
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 2u);
}

TEST(QmFinishSummaryState, UnknownRunAcceptsSameTimeAfterDuplicateWindow)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, -1));
	EXPECT_FALSE(State.Begin(Finish(), 10.9, -1));
	EXPECT_TRUE(State.Begin(Finish(), 11.0, -1));
}

TEST(QmFinishSummaryState, DifferentUnknownRunTimeIsASeparateFinish)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(100000), 10.0, -1));
	EXPECT_TRUE(State.Begin(Finish(100010), 10.1, -1));
}

TEST(QmFinishSummaryState, DifferentFinishIdentityIsNotSuppressed)
{
	// 同一因果条件的身份字段矩阵，断言能定位发生变化的字段。
	for(int Field = 0; Field < 4; ++Field)
	{
		SCOPED_TRACE(Field);
		QmFinishSummary::CState State;
		ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
		auto Other = Finish();
		switch(Field)
		{
		case 0:
			Other.m_Community = QmFinishSummary::ECommunity::AXIOM_GORES;
			break;
		case 1:
			Other.m_Map = "Other map";
			break;
		case 2:
			Other.m_Player = "Other player";
			break;
		case 3:
			Other.m_ClientId = 8;
			break;
		}
		EXPECT_TRUE(State.Begin(Other, 10.1, 100));
	}
}

TEST(QmFinishSummaryState, ExpansionTimeDoesNotConsumeVisibleHoldTime)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(false, 11.0);
	State.Advance(40.0);
	EXPECT_TRUE(State.Visible());
	State.SetExpanded(true, 40.0);
	State.Advance(51.9);
	EXPECT_TRUE(State.Visible());
	State.Advance(52.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, ZeroClockIsAValidExpansionStart)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 0.0, 100));
	State.SetExpanded(true, 0.0);
	State.Advance(12.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, RepeatedSettledFramesDoNotExtendTheHold)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	State.SetExpanded(true, 20.0);
	State.Advance(23.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, TransientShapeChangeDoesNotRestartTheHold)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	State.SetExpanded(false, 15.0);
	State.SetExpanded(true, 20.0);
	State.Advance(23.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, DuplicateMessageDoesNotRestartTheHold)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	EXPECT_FALSE(State.Begin(Finish(), 20.0, 100));
	State.SetExpanded(true, 20.0);
	State.Advance(23.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, NewFinishStartsItsOwnHoldAfterSettling)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	ASSERT_TRUE(State.Begin(Finish(120000), 20.0, 200));
	State.Advance(23.0);
	EXPECT_TRUE(State.Visible());
	State.SetExpanded(true, 24.0);
	State.Advance(35.9);
	EXPECT_TRUE(State.Visible());
	State.Advance(36.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, DismissRetainsSummaryForClosingAndDuplicateSuppression)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.Dismiss();
	EXPECT_FALSE(State.Visible());
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_TimeMs, 100000);
	EXPECT_FALSE(State.Begin(Finish(), 30.0, 100));
	State.SetExpanded(true, 30.0);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, NewFinishDuringClosingReplacesContentAndReopens)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.Dismiss();
	ASSERT_TRUE(State.Begin(Finish(120000), 10.1, 200));
	EXPECT_TRUE(State.Visible());
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 2u);
	EXPECT_EQ(State.Snapshot()->m_TimeMs, 120000);
}

TEST(QmFinishSummaryState, StartRunHidesSummaryAndRetainsSnapshotForClosing)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 10.1);
	State.StartRun();
	EXPECT_FALSE(State.Visible());
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 1u);
	EXPECT_EQ(State.Snapshot()->m_TimeMs, 100000);
	State.SetExpanded(true, 10.2);
	EXPECT_FALSE(State.Visible());
}

TEST(QmFinishSummaryState, StartRunAcceptsSameUnknownRunTimeInsideDuplicateWindow)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, -1));
	State.StartRun();
	ASSERT_TRUE(State.Begin(Finish(), 10.1, -1));
	EXPECT_TRUE(State.Visible());
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 2u);
	EXPECT_FALSE(State.Begin(Finish(), 10.2, -1));
}

TEST(QmFinishSummaryState, ResetDiscardsSummaryButKeepsEventIdsIncreasing)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	State.Reset();
	EXPECT_FALSE(State.Visible());
	EXPECT_EQ(State.Snapshot(), nullptr);
	EXPECT_EQ(State.MutableSnapshot(), nullptr);
	ASSERT_TRUE(State.Begin(Finish(), 11.1, 100));
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Id, 2u);
	State.Advance(23.0);
	EXPECT_TRUE(State.Visible());
}

TEST(QmFinishSummaryState, ResponseEnrichmentUpdatesRetainedDataWithoutRestartingHold)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	ASSERT_NE(State.MutableSnapshot(), nullptr);
	State.MutableSnapshot()->m_Rank = {5, QmFinishSummary::EStatus::READY};
	ASSERT_NE(State.Snapshot(), nullptr);
	EXPECT_EQ(State.Snapshot()->m_Rank.m_Value, 5);
	State.Advance(23.0);
	EXPECT_FALSE(State.Visible());
	EXPECT_EQ(State.Snapshot()->m_Rank.m_Value, 5);
}

TEST(QmFinishSummaryState, BackwardClockDoesNotPrematurelyExpireTheHold)
{
	QmFinishSummary::CState State;
	ASSERT_TRUE(State.Begin(Finish(), 10.0, 100));
	State.SetExpanded(true, 11.0);
	State.Advance(5.0);
	EXPECT_TRUE(State.Visible());
	State.Advance(22.9);
	EXPECT_TRUE(State.Visible());
	State.Advance(23.0);
	EXPECT_FALSE(State.Visible());
}
