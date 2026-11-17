#include <game/client/QmUi/QmFinishSummaryCover.h>

#include <gtest/gtest.h>

using namespace qm_finish_summary;

TEST(QmFinishSummaryCover, ImageSpansBothEdgesAndCropsAroundCenter)
{
	const auto Rows = CoverRows(480, 216, 8, 0, 0, 0);
	EXPECT_FLOAT_EQ(Rows.front().m_Left, 8);
	EXPECT_FLOAT_EQ(Rows.front().m_Right, 472);
	EXPECT_FLOAT_EQ(Rows[12].m_Left, 0);
	EXPECT_FLOAT_EQ(Rows[12].m_Right, 480);
	const vec2 TopLeft = CoverUV({}, 480, 216);
	const vec2 BottomRight = CoverUV({480, 216}, 480, 216);
	EXPECT_FLOAT_EQ(TopLeft.x, 0.0f);
	EXPECT_FLOAT_EQ(BottomRight.x, 1.0f);
	EXPECT_GT(TopLeft.y, 0.0f);
	EXPECT_NEAR(TopLeft.y + BottomRight.y, 1.0f, 0.001f);
	EXPECT_EQ(CoverUV({80, 80}, 160, 160), vec2(0.5f, 0.5f));
}

TEST(QmFinishSummaryCover, AllCornerCurvesRemainInsideCard)
{
	for(int Curve = 0; Curve <= 3; ++Curve)
	{
		SCOPED_TRACE(Curve);
		const auto Rows = CoverRows(480, 216, 8, Curve, 1.0f, 0.5f);
		float PreviousY = 0;
		for(const auto &Row : Rows)
		{
			EXPECT_GE(Row.m_Y, PreviousY);
			EXPECT_LE(Row.m_Y, 216);
			EXPECT_GE(Row.m_Left, 0);
			EXPECT_FLOAT_EQ(Row.m_Right, 480.0f - Row.m_Left);
			EXPECT_LE(Row.m_Right, 480);
			const vec2 UV = CoverUV({Row.m_Right, Row.m_Y}, 480, 216);
			EXPECT_GE(UV.x, 0);
			EXPECT_LE(UV.x, 1);
			EXPECT_GE(UV.y, 0);
			EXPECT_LE(UV.y, 1);
			PreviousY = Row.m_Y;
		}
		EXPECT_FLOAT_EQ(Rows.front().m_Left, Rows.back().m_Left);
		EXPECT_GT(Rows.front().m_Left, 0);
	}
}

TEST(QmFinishSummaryCover, EmptyDimensionsProduceEmptyGeometry)
{
	for(const auto &Row : CoverRows(0, 216, 8, 0, 0, 0))
	{
		EXPECT_FLOAT_EQ(Row.m_Y, 0);
		EXPECT_FLOAT_EQ(Row.m_Right, 0);
	}
	EXPECT_EQ(CoverUV({1, 1}, 0, 216), vec2());
}

TEST(QmFinishSummaryCover, SmoothedCornersFollowConfiguredCurveGrowth)
{
	const auto Classic = CoverRows(480, 216, 8, 0, 1, 0);
	const auto Smooth = CoverRows(480, 216, 8, 2, 1, 0);
	EXPECT_FLOAT_EQ(Classic.front().m_Left, 8);
	EXPECT_GT(Smooth.front().m_Left, Classic.front().m_Left);
	EXPECT_GT(Smooth[8].m_Y, Classic[8].m_Y);
	EXPECT_FLOAT_EQ(Smooth[8].m_Left, 0);
	EXPECT_FLOAT_EQ(Smooth[16].m_Left, 0);
}
