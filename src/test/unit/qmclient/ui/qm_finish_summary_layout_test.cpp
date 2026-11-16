#include <game/client/QmUi/QmFinishSummary.h>

#include <gtest/gtest.h>

TEST(QmFinishSummaryLayout, ExpandedCardStaysCenteredAndInsideViewport)
{
	for(const float Width : {320.0f, 533.3333f, 711.1111f, 1024.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(Width, 300.0f, 5.0f);
		EXPECT_FLOAT_EQ(Rect.y, 5.0f);
		EXPECT_NEAR(Rect.x + Rect.w * 0.5f, Width * 0.5f, 0.001f);
		EXPECT_LE(Rect.w, Width * 0.25f);
		EXPECT_LE(Rect.h, 60.0f);
		EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
	}
}

TEST(QmFinishSummaryLayout, ShortViewportLimitsExpandedHeight)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(533.0f, 100.0f, 10.0f);
	EXPECT_FLOAT_EQ(Rect.h, 20.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 30.0f);
}

TEST(QmFinishSummaryLayout, HudEditorScalingKeepsPhysicalCardWithinScreenFractions)
{
	for(const float Scale : {0.5f, 1.0f, 1.5f, 2.0f, 4.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(1920.0f, 1080.0f, 10.0f, Scale);
		EXPECT_LE(Rect.w * Scale, 480.0f);
		EXPECT_LE(Rect.h * Scale, 216.0f);
		EXPECT_GT(Rect.w, 0.0f);
		EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
	}
}

TEST(QmFinishSummaryLayout, UltrawideViewportKeepsHeightLimitAndContentAspectRatio)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(3440.0f, 1440.0f, 10.0f);
	EXPECT_FLOAT_EQ(Rect.h, 288.0f);
	EXPECT_NEAR(Rect.w, 640.0f, 0.001f);
}

TEST(QmFinishSummaryLayout, OuterShadowFitsInsidePhysicalSizeLimitAfterHudScaling)
{
	for(const float Scale : {0.5f, 1.0f, 2.0f, 4.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Rect = qm_finish_summary::ExpandedRect(1920.0f, 1080.0f, 10.0f, Scale, 5.0f);
		EXPECT_LE(Rect.w * Scale + 10.0f, 480.0f);
		EXPECT_LE(Rect.h * Scale + 10.0f, 216.0f);
	}
}

TEST(QmFinishSummaryLayout, CardNearBottomUsesRemainingHeightWithoutStretching)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(533.3333f, 300.0f, 285.0f);
	EXPECT_FLOAT_EQ(Rect.h, 15.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 300.0f);
	EXPECT_NEAR(Rect.w / Rect.h, 480.0f / 216.0f, 0.001f);
}

TEST(QmFinishSummaryLayout, ExpansionKeepsTopAnchorAndInterpolatesBothDimensions)
{
	const CUIRect Normal{250.0f, 7.0f, 33.0f, 11.2f};
	const CUIRect Expanded{100.0f, 7.0f, 333.0f, 139.86f};
	const CUIRect Half = qm_finish_summary::ResolveRect(Normal, Expanded, 0.5f);
	EXPECT_FLOAT_EQ(Half.y, 7.0f);
	EXPECT_FLOAT_EQ(Half.x, 175.0f);
	EXPECT_FLOAT_EQ(Half.w, 183.0f);
	EXPECT_NEAR(Half.h, 75.53f, 0.001f);
}

TEST(QmFinishSummaryLayout, SpringOvershootCannotExceedFinalGeometry)
{
	const CUIRect Normal{250.0f, 0.0f, 33.0f, 11.2f};
	const CUIRect Expanded{100.0f, 0.0f, 333.0f, 139.86f};
	const CUIRect Opening = qm_finish_summary::ResolveRect(Normal, Expanded, 1.1f);
	const CUIRect Closing = qm_finish_summary::ResolveRect(Normal, Expanded, -0.1f);
	EXPECT_FLOAT_EQ(Opening.x, Expanded.x);
	EXPECT_FLOAT_EQ(Opening.w, Expanded.w);
	EXPECT_FLOAT_EQ(Opening.h, Expanded.h);
	EXPECT_FLOAT_EQ(Closing.x, Normal.x);
	EXPECT_FLOAT_EQ(Closing.w, Normal.w);
	EXPECT_FLOAT_EQ(Closing.h, Normal.h);
}
