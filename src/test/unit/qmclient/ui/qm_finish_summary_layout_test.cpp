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
		EXPECT_GE(Rect.x, 20.0f);
		EXPECT_LE(Rect.x + Rect.w, Width - 20.0f);
		EXPECT_LE(Rect.y + Rect.h, 290.0f);
	}
}

TEST(QmFinishSummaryLayout, ShortViewportLimitsExpandedHeight)
{
	const CUIRect Rect = qm_finish_summary::ExpandedRect(533.0f, 100.0f, 10.0f);
	EXPECT_FLOAT_EQ(Rect.h, 80.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 90.0f);
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
