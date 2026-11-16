#include <game/client/QmUi/QmFinishSummary.h>
#include <game/client/QmUi/QmFinishSummaryMotion.h>

#include <gtest/gtest.h>

TEST(QmFinishSummaryLayout, ExpandedCardStaysCenteredAndInsideViewport)
{
	for(const float Width : {320.0f, 533.3333f, 711.1111f, 1024.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(Width, 300.0f, 5.0f);
		EXPECT_FLOAT_EQ(Rect.y, 5.0f);
		EXPECT_NEAR(Rect.x + Rect.w * 0.5f, Width * 0.5f, 0.001f);
		EXPECT_LE(Rect.w, Width * 0.375f);
		EXPECT_LE(Rect.h, 90.0f);
		EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
	}
}

TEST(QmFinishSummaryLayout, ShortViewportLimitsExpandedHeight)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(533.0f, 100.0f, 10.0f);
	EXPECT_FLOAT_EQ(Rect.h, 30.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 40.0f);
}

TEST(QmFinishSummaryLayout, HudEditorScalingKeepsPhysicalCardWithinScreenFractions)
{
	for(const float Scale : {0.5f, 1.0f, 1.5f, 2.0f, 4.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(1920.0f, 1080.0f, 10.0f, Scale);
		EXPECT_LE(Rect.w * Scale, 720.0f);
		EXPECT_LE(Rect.h * Scale, 324.0f);
		EXPECT_GT(Rect.w, 0.0f);
		EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
	}
}

TEST(QmFinishSummaryLayout, UltrawideViewportKeepsHeightLimitAndContentAspectRatio)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(3440.0f, 1440.0f, 10.0f);
	EXPECT_FLOAT_EQ(Rect.h, 432.0f);
	EXPECT_NEAR(Rect.w, 960.0f, 0.001f);
}

TEST(QmFinishSummaryLayout, OuterShadowFitsInsidePhysicalSizeLimitAfterHudScaling)
{
	for(const float Scale : {0.5f, 1.0f, 2.0f, 4.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(1920.0f, 1080.0f, 10.0f, Scale, 5.0f);
		EXPECT_LE(Rect.w * Scale + 10.0f, 720.0f);
		EXPECT_LE(Rect.h * Scale + 10.0f, 324.0f);
	}
}

TEST(QmFinishSummaryLayout, CardNearBottomUsesRemainingHeightWithoutStretching)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(533.3333f, 300.0f, 285.0f);
	EXPECT_FLOAT_EQ(Rect.h, 15.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 300.0f);
	EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
}

TEST(QmFinishSummaryLayout, CardGrowsFromInsideHostAndDetachesBelowIt)
{
	const CUIRect Host{70, 4, 60, 16};
	const CUIRect Target{20, 28, 160, 72};
	const auto Closed = qm_finish_summary::Layout(Host, Target, 0.0f);
	const auto Bud = qm_finish_summary::Layout(Host, Target, 0.25f);
	const auto Open = qm_finish_summary::Layout(Host, Target, 1.0f);
	EXPECT_FLOAT_EQ(Closed.m_Rect.w, 0.0f);
	EXPECT_FLOAT_EQ(Closed.m_ContentAlpha, 0.0f);
	EXPECT_LT(Bud.m_Rect.y, Host.y + Host.h);
	EXPECT_GT(Bud.m_Rect.y + Bud.m_Rect.h, Host.y + Host.h);
	EXPECT_GT(Bud.m_Connection, 0.0f);
	EXPECT_LT(Bud.m_Rect.w, Target.w);
	EXPECT_FLOAT_EQ(Open.m_Rect.x, Target.x);
	EXPECT_FLOAT_EQ(Open.m_Rect.y, Target.y);
	EXPECT_FLOAT_EQ(Open.m_Rect.h, Target.h);
	EXPECT_FLOAT_EQ(Open.m_Connection, 0.0f);
	EXPECT_FLOAT_EQ(Open.m_ContentAlpha, 1.0f);
}

TEST(QmFinishSummaryLayout, ClosingPullsNearEdgeIntoIslandBeforeFarEdge)
{
	const CUIRect Host{70, 4, 60, 16};
	const CUIRect Target{20, 28, 160, 72};
	for(const float Start : {0.5f, 0.82f, 1.0f})
	{
		const auto Current = qm_finish_summary::Layout(Host, Target, Start);
		const auto Reversed = qm_finish_summary::ClosingLayout(Host, Target, Start, Start);
		EXPECT_FLOAT_EQ(Reversed.m_Rect.x, Current.m_Rect.x);
		EXPECT_FLOAT_EQ(Reversed.m_Rect.y, Current.m_Rect.y);
		EXPECT_FLOAT_EQ(Reversed.m_Rect.w, Current.m_Rect.w);
	}
	const auto Half = qm_finish_summary::ClosingLayout(Host, Target, 1.0f, 0.5f);
	const auto Tail = qm_finish_summary::ClosingLayout(Host, Target, 1.0f, 0.2f);
	EXPECT_FLOAT_EQ(Half.m_Rect.y, Host.y + Host.h);
	EXPECT_GT(Half.m_Pinch, 0.0f);
	EXPECT_LT(Tail.m_Rect.w, Half.m_Rect.w);
	EXPECT_LT(Tail.m_Rect.h, Half.m_Rect.h);
	const auto End = qm_finish_summary::ClosingLayout(Host, Target, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(End.m_Rect.h, 0.0f);
	EXPECT_FLOAT_EQ(End.m_Rect.w, 0.0f);
}

TEST(QmFinishSummaryMotion, MovingTeeTiltsAndStoppingLevelsTheCard)
{
	qm_finish_summary::CCardMotion Motion;
	Motion.Update(1, 0.0, true, 1.0f, {}, 2);
	Motion.Update(1, 6.0, true, 1.0f, {10, -5}, 2);
	EXPECT_GT(Motion.TiltX(), 0.0f);
	EXPECT_GT(Motion.TiltY(), 0.0f);
	Motion.Update(1, 7.0, true, 1.0f, {}, 2);
	EXPECT_NEAR(Motion.TiltX(), 0.0f, 0.001f);
	EXPECT_NEAR(Motion.TiltY(), 0.0f, 0.001f);
}

TEST(QmFinishSummaryMotion, HostReversesFromCurrentShapeWhenDismissedDuringGrowth)
{
	qm_finish_summary::CCardMotion Motion;
	Motion.Update(1, 0.0, true, 0.0f, {}, 2);
	Motion.Update(1, 0.1, true, 0.5f, {}, 2);
	const float Before = Motion.HostScale();
	EXPECT_LT(Before, 1.0f);
	Motion.Update(1, 0.1, false, 0.5f, {}, 2);
	EXPECT_FLOAT_EQ(Motion.HostScale(), Before);
	Motion.Update(1, 0.3, false, 0.3f, {}, 2);
	EXPECT_GT(Motion.HostScale(), 1.0f);
}

TEST(QmFinishSummaryMotion, DisablingMotionLevelsTheCardImmediately)
{
	qm_finish_summary::CCardMotion Motion;
	Motion.Update(1, 0.0, true, 0.0f, {}, 2);
	Motion.Update(1, 0.1, true, 0.5f, {-8, 6}, 2);
	ASSERT_LT(Motion.TiltX(), 0.0f);
	ASSERT_LT(Motion.TiltY(), 0.0f);
	Motion.Update(1, 6.0, true, 1.0f, {-8, 6}, 0);
	EXPECT_FLOAT_EQ(Motion.TiltX(), 0.0f);
	EXPECT_FLOAT_EQ(Motion.TiltY(), 0.0f);
	EXPECT_FLOAT_EQ(Motion.HostScale(), 1.0f);
}
