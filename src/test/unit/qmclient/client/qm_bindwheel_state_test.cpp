#include <game/client/components/qmclient/bindwheel_state.h>

#include <gtest/gtest.h>

TEST(QmBindWheelState, ReleaseCommitsSelectionOnce)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(3);
	State.Release(4);
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.TakePendingBind(), 3);
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, EscapeAndGlobalReleaseCancelSelection)
{
	CQmBindWheelState State;
	State.Open();
	State.Cancel();
	State.Release(2);
	EXPECT_FALSE(State.IsActive());
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, CancelDiscardsPendingCommand)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(2);
	State.Cancel();
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, BindMutationDiscardsPendingIndex)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(2);
	State.InvalidateSelection();
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, ReopeningDoesNotExecutePreviousSelection)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(2);
	State.Open();
	EXPECT_TRUE(State.IsActive());
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, ResolvesLatestCursorPositionAndEmptyCenter)
{
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, 0), 4), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(110, 0), 4), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(170, 0), 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(170, 0), 4), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, 170), 4), 1);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(-170, 0), 4), 2);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, -170), 4), 3);
}

TEST(QmBindWheelState, CenterReleaseStillRequestsMouseReset)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(-1);
	const auto Pending = State.TakePendingBind();
	ASSERT_TRUE(Pending.has_value());
	EXPECT_EQ(*Pending, -1);
}

TEST(QmBindWheelState, ListSelectionReturnsTheRowUnderTheCursor)
{
	// 条目区从 (300, 200) 开始，宽 400、行高 28；鼠标落在某行内即选中该行。
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 210.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 240.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), 1);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 396.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), 7);
}

TEST(QmBindWheelState, ListSelectionIgnoresPointsOutsideTheItemArea)
{
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(290.0f, 210.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(710.0f, 210.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 190.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 430.0f), 300.0f, 200.0f, 400.0f, 28.0f, 8, 0), -1);
}

TEST(QmBindWheelState, ListSelectionAppliesTheScrollOffset)
{
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 210.0f), 300.0f, 200.0f, 400.0f, 28.0f, 64, 3), 3);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 322.0f), 300.0f, 200.0f, 400.0f, 28.0f, 64, 3), 7);
}

TEST(QmBindWheelState, ListSelectionIgnoresEmptyPages)
{
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 210.0f), 300.0f, 200.0f, 400.0f, 28.0f, 0, 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedListBind(vec2(320.0f, 210.0f), 300.0f, 200.0f, 400.0f, 0.0f, 8, 0), -1);
}
