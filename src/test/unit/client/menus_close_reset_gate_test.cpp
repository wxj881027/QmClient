#include <game/client/components/menus_close_reset_gate.h>

#include <gtest/gtest.h>

TEST(MenuCloseResetGate, AnimatedCloseResetsWhenInputIsReturned)
{
	CMenuCloseResetGate Gate;
	int Resets = 0;

	Gate.Begin(true, true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);

	Gate.Finish(true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);
}

TEST(MenuCloseResetGate, PanelOpenedWhileClosingSurvivesCloseFinish)
{
	CMenuCloseResetGate Gate;
	bool ScoreboardActive = false;

	Gate.Begin(true, true, [&] { ScoreboardActive = false; });
	// 滑出期间 Tab 已交回游戏，玩家重新打开计分板并保持按住。
	ScoreboardActive = true;

	Gate.Finish(true, [&] { ScoreboardActive = false; });
	EXPECT_TRUE(ScoreboardActive);
}

TEST(MenuCloseResetGate, ImmediateCloseResetsWhenMenuActuallyCloses)
{
	CMenuCloseResetGate Gate;
	int Resets = 0;

	Gate.Begin(true, false, [&] { ++Resets; });
	EXPECT_EQ(Resets, 0);

	Gate.Finish(true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);
}

TEST(MenuCloseResetGate, OfflineCloseDoesNotTouchGameInput)
{
	CMenuCloseResetGate Gate;
	int Resets = 0;

	Gate.Begin(false, true, [&] { ++Resets; });
	Gate.Finish(false, [&] { ++Resets; });
	EXPECT_EQ(Resets, 0);
}

TEST(MenuCloseResetGate, CanceledAnimatedCloseCanResetOnNextImmediateClose)
{
	CMenuCloseResetGate Gate;
	int Resets = 0;

	Gate.Begin(true, true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);

	Gate.Cancel();
	Gate.Begin(true, false, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);

	Gate.Finish(true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 2);
}

TEST(MenuCloseResetGate, DirectCloseWithoutBeginStillResets)
{
	CMenuCloseResetGate Gate;
	int Resets = 0;

	Gate.Finish(true, [&] { ++Resets; });
	EXPECT_EQ(Resets, 1);
}
