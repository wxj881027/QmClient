#include <game/client/QmUi/QmListEntryAnimation.h>

#include <gtest/gtest.h>

namespace
{
	constexpr auto INTERACTIVE = EQmListEntryRenderPass::INTERACTIVE;
	constexpr auto BACKGROUND = EQmListEntryRenderPass::BACKGROUND;
	constexpr auto MEASUREMENT = EQmListEntryRenderPass::MEASUREMENT;
}

// Resolve 返回入场生命周期已流逝秒数（-1 = 未在动画中）；浮现与裁剪由
// QmListEntryVisual 计算。断言分两层：生命周期与位移。
TEST(QmListEntryAnimation, LongConsoleOverlayDoesNotReplayOnReturn)
{
	CQmListEntryAnimation Animation;
	EXPECT_FLOAT_EQ(Animation.Resolve(1000, 1000, true, 2, INTERACTIVE), 0.0f);
	EXPECT_FLOAT_EQ(Animation.Resolve(1200, 1000, true, 2, INTERACTIVE), 0.2f);
	for(int64_t Now = 1216; Now <= 2400; Now += 16)
	{
		const float Elapsed = Animation.Resolve(Now, 1000, true, 2, BACKGROUND);
		// 遮盖期喂帧不重置生命周期：要么续播（elapsed 递增），要么已自然结束。
		EXPECT_TRUE(Elapsed < 0.0f || Elapsed >= 0.2f);
	}
	// 全程无失联 gap，返回交互渲染时不再重放。
	EXPECT_FLOAT_EQ(Animation.Resolve(2416, 1000, true, 2, INTERACTIVE), -1.0f);
}

TEST(QmListEntryAnimation, ConsoleDuringEntryKeepsTheVisibleTrajectory)
{
	for(const int Level : {1, 2})
	{
		SCOPED_TRACE(Level);
		CQmListEntryAnimation Animation;
		CQmListEntryAnimation Reference;
		Animation.Resolve(1000, 1000, true, Level, INTERACTIVE);
		Reference.Resolve(1000, 1000, true, Level, INTERACTIVE);
		for(int64_t Now = 1016; Now < 1160; Now += 16)
		{
			const auto Pass = (Now - 1000) % 32 == 0 ? INTERACTIVE : BACKGROUND;
			EXPECT_FLOAT_EQ(Animation.Resolve(Now, 1000, true, Level, Pass), Reference.Resolve(Now, 1000, true, Level, INTERACTIVE));
		}
		// Full 档整场 0.54s 未完，仍在续播；Reduced 档 0.10s 已自然结束。
		const float Expected = Level == 1 ? -1.0f : 0.2f;
		EXPECT_FLOAT_EQ(Animation.Resolve(1200, 1000, true, Level, INTERACTIVE), Expected);
	}
}

TEST(QmListEntryAnimation, FirstVisibleFrameBehindConsoleStartsEntry)
{
	CQmListEntryAnimation Animation;
	EXPECT_FLOAT_EQ(Animation.Resolve(1000, 1000, true, 2, BACKGROUND), 0.0f);
	// 启动帧行 0 位于浮现起点（+8，被自身裁剪框遮蔽），随后临界阻尼归位。
	EXPECT_NEAR(QmListEntryVisual(0.0f, 0, 2).m_OffsetY, 8.0f, 0.001f);
	EXPECT_GT(Animation.Resolve(1080, 1000, true, 2, BACKGROUND), 0.0f);
	EXPECT_GT(QmListEntryVisual(0.08f, 0, 2).m_OffsetY, 0.0f);
	EXPECT_LT(QmListEntryVisual(0.08f, 0, 2).m_OffsetY, 8.0f);
	EXPECT_FLOAT_EQ(Animation.Resolve(1200, 1000, true, 2, INTERACTIVE), 0.2f);
}

TEST(QmListEntryAnimation, ActualPageAbsenceStillReplaysEntry)
{
	CQmListEntryAnimation Animation;
	Animation.Resolve(1000, 1000, true, 2, INTERACTIVE);
	EXPECT_FLOAT_EQ(Animation.Resolve(1200, 1000, true, 2, INTERACTIVE), 0.2f);
	// 失联超过 gap 后实际重放：重启动帧 elapsed 归零，行 0 回到浮现起点。
	EXPECT_FLOAT_EQ(Animation.Resolve(1700, 1000, true, 2, INTERACTIVE), 0.0f);
	EXPECT_NEAR(QmListEntryVisual(0.0f, 0, 2).m_OffsetY, 8.0f, 0.001f);
}

TEST(QmListEntryAnimation, MeasurementDoesNotKeepAnAbsentPageAlive)
{
	CQmListEntryAnimation Animation;
	Animation.Resolve(1000, 1000, true, 2, INTERACTIVE);
	for(int64_t Now = 1016; Now <= 2400; Now += 16)
		EXPECT_FLOAT_EQ(Animation.Resolve(Now, 1000, true, 2, MEASUREMENT), -1.0f);
	// 测量趟既不推进也不保活：失联 gap 仍按最后一次可见渲染计算，实际重放。
	EXPECT_FLOAT_EQ(Animation.Resolve(2416, 1000, true, 2, INTERACTIVE), 0.0f);
}

TEST(QmListEntryAnimation, MeasurementDoesNotInitializeOrCancelVisibleEntry)
{
	CQmListEntryAnimation Animation;
	EXPECT_FLOAT_EQ(Animation.Resolve(1000, 1000, true, 2, MEASUREMENT), -1.0f);
	EXPECT_FLOAT_EQ(Animation.Resolve(1100, 1000, true, 2, INTERACTIVE), 0.0f);
	const float Before = Animation.Resolve(1140, 1000, true, 2, INTERACTIVE);
	EXPECT_FLOAT_EQ(Animation.Resolve(1140, 1000, false, 0, MEASUREMENT), -1.0f);
	EXPECT_FLOAT_EQ(Animation.Resolve(1140, 1000, true, 2, INTERACTIVE), Before);
}

TEST(QmListEntryAnimation, DisablingMotionBehindConsoleCancelsTheOldEntry)
{
	CQmListEntryAnimation Animation;
	Animation.Resolve(1000, 1000, true, 2, INTERACTIVE);
	EXPECT_FLOAT_EQ(Animation.Resolve(1040, 1000, false, 0, BACKGROUND), -1.0f);
	EXPECT_FLOAT_EQ(Animation.Resolve(1080, 1000, true, 2, INTERACTIVE), -1.0f);
}

TEST(QmListEntryAnimation, ResetStartsANewDisplayCycle)
{
	CQmListEntryAnimation Animation;
	Animation.Resolve(1000, 1000, true, 2, INTERACTIVE);
	Animation.Resolve(1200, 1000, true, 2, INTERACTIVE);
	Animation.Reset();
	EXPECT_FLOAT_EQ(Animation.Resolve(1216, 1000, true, 1, INTERACTIVE), 0.0f);
	// Reduced 档整体浮现起点仍是 +4。
	EXPECT_NEAR(QmListEntryVisual(0.0f, 0, 1).m_OffsetY, 4.0f, 0.001f);
}
