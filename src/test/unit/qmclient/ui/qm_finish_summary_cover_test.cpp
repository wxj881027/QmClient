#include <game/client/QmUi/QmFinishSummaryCover.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace qm_finish_summary;

TEST(QmFinishSummaryCover, StaticEdgeReachesReferenceProportions)
{
	const auto Rows = CoverRows(480, 216, 8, 0, 0, 0, 0, 0);
	EXPECT_FLOAT_EQ(Rows.front().m_FadeEnd, 480 * 0.65f);
	EXPECT_NEAR(Rows.back().m_FadeEnd, 480 * 0.30f, 0.001f);
	EXPECT_FLOAT_EQ(Rows.front().m_Y, 0);
	EXPECT_FLOAT_EQ(Rows.back().m_Y, 216);
	EXPECT_FLOAT_EQ(Rows.front().m_Left, 8);
	EXPECT_FLOAT_EQ(Rows.back().m_Left, 8);
	EXPECT_FLOAT_EQ(Rows[12].m_Left, 0);
}

TEST(QmFinishSummaryCover, MotionDisabledKeepsGeometryStable)
{
	const auto First = CoverRows(480, 216, 8, 0, 0, 2, 0.8f, 0.1f);
	const auto Later = CoverRows(480, 216, 8, 100, 0, 2, 0.8f, 0.1f);
	for(size_t i = 0; i < First.size(); ++i)
	{
		EXPECT_FLOAT_EQ(First[i].m_FadeStart, Later[i].m_FadeStart);
		EXPECT_FLOAT_EQ(First[i].m_FadeEnd, Later[i].m_FadeEnd);
		EXPECT_FLOAT_EQ(First[i].m_Left, Later[i].m_Left);
		EXPECT_FLOAT_EQ(First[i].m_Y, Later[i].m_Y);
	}
}

TEST(QmFinishSummaryCover, MovingBoundaryIsBoundedAndContinuous)
{
	const auto Static = CoverRows(480, 216, 8, 0, 0, 0, 0, 0);
	bool Changed = false;
	for(const double Time : {0.0, 1.0, 7.0, 1000.0})
	{
		const auto Current = CoverRows(480, 216, 8, Time, 2, 0, 0, 0);
		const auto Next = CoverRows(480, 216, 8, Time + 0.001, 2, 0, 0, 0);
		for(size_t i = 0; i < Current.size(); ++i)
		{
			EXPECT_LE(std::abs(Current[i].m_FadeEnd - Static[i].m_FadeEnd), 480 * 0.02f + 0.001f);
			EXPECT_NEAR(Current[i].m_FadeEnd, Next[i].m_FadeEnd, 0.01f);
			EXPECT_FLOAT_EQ(Current[i].m_Left, Static[i].m_Left);
			EXPECT_FLOAT_EQ(Current[i].m_Y, Static[i].m_Y);
			Changed |= std::abs(Current[i].m_FadeEnd - Static[i].m_FadeEnd) > 0.1f;
		}
	}
	EXPECT_TRUE(Changed);
}

TEST(QmFinishSummaryCover, AllCornerCurvesRemainInsideCard)
{
	for(int Curve = 0; Curve <= 3; ++Curve)
	{
		SCOPED_TRACE(Curve);
		const auto Rows = CoverRows(480, 216, 8, 7, 2, Curve, 1.0f, 0.5f);
		float PreviousY = 0;
		for(const auto &Row : Rows)
		{
			EXPECT_GE(Row.m_Y, PreviousY);
			EXPECT_LE(Row.m_Y, 216);
			EXPECT_GE(Row.m_Left, 0);
			EXPECT_GE(Row.m_FadeStart, Row.m_Left);
			EXPECT_GT(Row.m_FadeEnd, Row.m_FadeStart);
			EXPECT_LE(Row.m_FadeEnd, 480);
			const vec2 UV = CoverUV({Row.m_FadeEnd, Row.m_Y}, 480, 216);
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
	for(const auto &Row : CoverRows(0, 216, 8, 1, 2, 0, 0, 0))
	{
		EXPECT_FLOAT_EQ(Row.m_Y, 0);
		EXPECT_FLOAT_EQ(Row.m_FadeEnd, 0);
	}
	EXPECT_EQ(CoverUV({1, 1}, 0, 216), vec2());
}

TEST(QmFinishSummaryCover, SmoothedCornersFollowConfiguredCurveGrowth)
{
	const auto Classic = CoverRows(480, 216, 8, 0, 0, 0, 1, 0);
	const auto Smooth = CoverRows(480, 216, 8, 0, 0, 2, 1, 0);
	EXPECT_FLOAT_EQ(Classic.front().m_Left, 8);
	EXPECT_GT(Smooth.front().m_Left, Classic.front().m_Left);
	EXPECT_GT(Smooth[8].m_Y, Classic[8].m_Y);
	EXPECT_FLOAT_EQ(Smooth[8].m_Left, 0);
	EXPECT_FLOAT_EQ(Smooth[16].m_Left, 0);
}
