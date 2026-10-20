// 卡片换位追随的语义契约：按槽位序号算差值、跳太远直接对齐、切列直接对齐。
// 这些行为不依赖渲染上下文，只用纯函数与状态断言，避免源码字符串合同。
#include <gtest/gtest.h>

#include <game/client/QmUi/QmSlotReflow.h>

#include <cmath>

TEST(QmSlotReflow, SlotDeltaOffsetPointsBackToOldPosition)
{
	// 3 列、格子 100x80：从槽位 1（第 0 行第 1 列）换到槽位 0，
	// 卡片要往右挪一格才能看起来停在原处。
	const vec2 Right = CQmSlotReflow::SlotDeltaOffset(1, 0, 3, 100.0f, 80.0f);
	EXPECT_FLOAT_EQ(Right.x, 100.0f);
	EXPECT_FLOAT_EQ(Right.y, 0.0f);
	// 从槽位 4（第 1 行第 1 列）换到槽位 1：往下挪一行。
	const vec2 Down = CQmSlotReflow::SlotDeltaOffset(4, 1, 3, 100.0f, 80.0f);
	EXPECT_FLOAT_EQ(Down.x, 0.0f);
	EXPECT_FLOAT_EQ(Down.y, 80.0f);
}

TEST(QmSlotReflow, FirstSightHasNoOffset)
{
	CQmSlotReflow Reflow;
	Reflow.BeginFrame(1.0f / 60.0f, 3, false);
	const vec2 Offset = Reflow.Track("A", 5, 100.0f, 80.0f);
	EXPECT_FLOAT_EQ(Offset.x, 0.0f);
	EXPECT_FLOAT_EQ(Offset.y, 0.0f);
}

TEST(QmSlotReflow, DeletedNeighbourPullsCardUpThenSettles)
{
	CQmSlotReflow Reflow;
	const float Dt = 1.0f / 60.0f;
	Reflow.BeginFrame(Dt, 3, false);
	Reflow.Track("A", 3, 100.0f, 80.0f);
	// 前面删掉一个：槽位 3 -> 2，视觉上先停在原处，偏移 = +1 行。
	Reflow.BeginFrame(Dt, 3, false);
	const vec2 First = Reflow.Track("A", 2, 100.0f, 80.0f);
	EXPECT_NEAR(First.y, 80.0f, 0.01f);
	// 一秒之后应当收敛回布局位置。
	vec2 Offset = First;
	for(int Frame = 0; Frame < 60; ++Frame)
	{
		Reflow.BeginFrame(Dt, 3, false);
		Offset = Reflow.Track("A", 2, 100.0f, 80.0f);
	}
	EXPECT_LT(std::abs(Offset.y), 1.0f);
}

TEST(QmSlotReflow, SnapForcesLayoutPosition)
{
	CQmSlotReflow Reflow;
	const float Dt = 1.0f / 60.0f;
	Reflow.BeginFrame(Dt, 3, false);
	Reflow.Track("A", 0, 100.0f, 80.0f);
	Reflow.BeginFrame(Dt, 3, false);
	// 0 -> 1：往左一格，偏移是 +1 格（卡片先停在原处）。
	EXPECT_GT(std::abs(Reflow.Track("A", 1, 100.0f, 80.0f).x), 0.5f);
	// 滚动/切页时直接对齐：任何残留偏移都必须清零。
	Reflow.BeginFrame(Dt, 3, true);
	EXPECT_FLOAT_EQ(Reflow.Track("A", 1, 100.0f, 80.0f).x, 0.0f);
	EXPECT_FLOAT_EQ(Reflow.Track("A", 1, 100.0f, 80.0f).y, 0.0f);
}

TEST(QmSlotReflow, ColumnChangeSnaps)
{
	CQmSlotReflow Reflow;
	const float Dt = 1.0f / 60.0f;
	Reflow.BeginFrame(Dt, 3, false);
	Reflow.Track("A", 6, 100.0f, 80.0f);
	Reflow.BeginFrame(Dt, 4, false);
	// 换列后槽位含义整体变了，不做动画，直接落位。
	EXPECT_FLOAT_EQ(Reflow.Track("A", 6, 100.0f, 80.0f).y, 0.0f);
	EXPECT_FLOAT_EQ(Reflow.Track("A", 6, 100.0f, 80.0f).x, 0.0f);
}

TEST(QmSlotReflow, HugeJumpDoesNotAnimate)
{
	CQmSlotReflow Reflow;
	const float Dt = 1.0f / 60.0f;
	Reflow.BeginFrame(Dt, 3, false);
	Reflow.Track("A", 0, 100.0f, 80.0f);
	// 换筛选后同一个名字落到很远的槽位：应当是直接对齐，而不是飞过去。
	Reflow.BeginFrame(Dt, 3, false);
	const vec2 Offset = Reflow.Track("A", 20, 100.0f, 80.0f);
	EXPECT_FLOAT_EQ(Offset.x, 0.0f);
	EXPECT_FLOAT_EQ(Offset.y, 0.0f);
}
