// QmAnim 行为测试：列表行选中/悬停反馈。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/ui_listbox.h>

#include <gtest/gtest.h>
#include <test/test.h>

TEST(QmListRowFeedback, FirstObservedRowStartsFullyOpaque)
{
	const int PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
	g_Config.m_QmUiMotionLevel = 2;
	CQmListRowFeedback Feedback;
	Feedback.BeginFrame(nullptr, 11, false, true);
	Feedback.Observe(3);
	EXPECT_FLOAT_EQ(Feedback.Alpha(3), 1.0f);
	EXPECT_FLOAT_EQ(Feedback.Alpha(2), 0.0f);
	Feedback.EndFrame();
	EXPECT_FLOAT_EQ(Feedback.Alpha(3), 1.0f);
	g_Config.m_QmUiMotionLevel = PreviousMotionLevel;
}

TEST(QmListRowFeedback, MovingBetweenRowsCrossfadesBothSides)
{
	const int PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	CQmListRowFeedback Feedback;
	constexpr uint64_t Key = 12;

	Feedback.BeginFrame(&Runtime, Key, false, true);
	Feedback.Observe(0);
	Feedback.EndFrame();

	Feedback.BeginFrame(&Runtime, Key, false, true);
	Feedback.Observe(1);
	Feedback.EndFrame();

	for(int Frame = 0; Frame < 4; ++Frame)
		Runtime.Advance(1.0f / 60.0f);
	Feedback.BeginFrame(&Runtime, Key, false, true);
	Feedback.Observe(1);
	Feedback.EndFrame();

	const float OldAlpha = Feedback.Alpha(0);
	const float NewAlpha = Feedback.Alpha(1);
	EXPECT_GT(OldAlpha, 0.0f);
	EXPECT_LT(OldAlpha, 1.0f);
	EXPECT_GT(NewAlpha, 0.0f);
	EXPECT_LT(NewAlpha, 1.0f);
	EXPECT_FLOAT_EQ(OldAlpha + NewAlpha, 1.0f);

	for(int Frame = 0; Frame < 30; ++Frame)
		Runtime.Advance(1.0f / 60.0f);
	Feedback.BeginFrame(&Runtime, Key, false, true);
	Feedback.Observe(1);
	Feedback.EndFrame();
	EXPECT_FLOAT_EQ(Feedback.Alpha(0), 0.0f);
	EXPECT_FLOAT_EQ(Feedback.Alpha(1), 1.0f);
	g_Config.m_QmUiMotionLevel = PreviousMotionLevel;
}

TEST(QmListRowFeedback, SnapWhileScrollingSkipsStaleCrossfade)
{
	const int PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	CQmListRowFeedback Feedback;
	constexpr uint64_t Key = 13;

	Feedback.BeginFrame(&Runtime, Key, false, true);
	Feedback.Observe(0);
	Feedback.EndFrame();

	Feedback.BeginFrame(&Runtime, Key, true, true);
	Feedback.Observe(4);
	// snap 帧在绘制阶段就只认本帧候选行，不能等 EndFrame 才切换。
	EXPECT_FLOAT_EQ(Feedback.Alpha(0), 0.0f);
	EXPECT_FLOAT_EQ(Feedback.Alpha(4), 1.0f);
	Feedback.EndFrame();

	EXPECT_FLOAT_EQ(Feedback.Alpha(0), 0.0f);
	EXPECT_FLOAT_EQ(Feedback.Alpha(4), 1.0f);
	g_Config.m_QmUiMotionLevel = PreviousMotionLevel;
}
