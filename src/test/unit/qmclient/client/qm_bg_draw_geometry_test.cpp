#include <game/client/components/qmclient/bg_draw_geometry.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	using TQuads = std::array<IGraphics::CFreeformItem, QM_BG_DRAW_ARC_QUADS>;
}

TEST(QmBgDrawGeometry, WorldProjectionKeepsTheExistingCenterRelativeScale)
{
	EXPECT_EQ(QmBgDrawWorldPosition({12, 23}, {10, 20}, 2, false), vec2(14, 26));
}

TEST(QmBgDrawGeometry, SpectatorProjectionUsesTheCenterWithoutScalingTheTarget)
{
	EXPECT_EQ(QmBgDrawWorldPosition({12, 23}, {10, 20}, 2, true), vec2(10, 20));
}

TEST(QmBgDrawGeometry, PointPreparationScalesWidthAndKeepsPositionAndColor)
{
	const auto Point = QmBgDrawMakePoint({10, 20}, 2, 5, {0.2f, 0.4f, 0.6f, 0.8f});
	EXPECT_TRUE(QmBgDrawPointIsFinite(Point));
	EXPECT_EQ(Point.Pos(), vec2(10, 20));
	EXPECT_FLOAT_EQ(Point.w, 10);
	EXPECT_EQ(Point.Color(), ColorRGBA(0.2f, 0.4f, 0.6f, 0.8f));
}

TEST(QmBgDrawGeometry, QuarterArcKeepsItsVertexOrderAndCenter)
{
	TQuads aQuads{};
	ASSERT_EQ(QmBgDrawBuildArc({10, 20}, 0, pi / 2, 4, aQuads), 4);
	EXPECT_FLOAT_EQ(aQuads[0].m_X0, 12);
	EXPECT_FLOAT_EQ(aQuads[0].m_Y0, 20);
	EXPECT_NEAR(aQuads[0].m_X1, 11.847759f, 0.00001f);
	EXPECT_NEAR(aQuads[0].m_Y1, 20.765367f, 0.00001f);
	EXPECT_NEAR(aQuads[3].m_X1, 10, 0.00001f);
	EXPECT_NEAR(aQuads[3].m_Y1, 22, 0.00001f);
	for(int i = 0; i < 4; ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(aQuads[i].m_X2, 10);
		EXPECT_FLOAT_EQ(aQuads[i].m_Y2, 20);
		EXPECT_FLOAT_EQ(aQuads[i].m_X3, 10);
		EXPECT_FLOAT_EQ(aQuads[i].m_Y3, 20);
	}
}

TEST(QmBgDrawGeometry, ReverseArcKeepsItsOriginalDirection)
{
	TQuads aQuads{};
	ASSERT_EQ(QmBgDrawBuildArc({10, 20}, pi / 2, 0, 4, aQuads), 4);
	EXPECT_NEAR(aQuads[0].m_X0, 10, 0.00001f);
	EXPECT_NEAR(aQuads[0].m_Y0, 22, 0.00001f);
	EXPECT_NEAR(aQuads[3].m_X1, 12, 0.00001f);
	EXPECT_NEAR(aQuads[3].m_Y1, 20, 0.00001f);
}

TEST(QmBgDrawGeometry, ZeroSweepRetainsOneDegenerateQuad)
{
	TQuads aQuads{};
	ASSERT_EQ(QmBgDrawBuildArc({10, 20}, 0, 0, 4, aQuads), 1);
	EXPECT_FLOAT_EQ(aQuads[0].m_X0, 12);
	EXPECT_FLOAT_EQ(aQuads[0].m_Y0, 20);
	EXPECT_FLOAT_EQ(aQuads[0].m_X1, 12);
	EXPECT_FLOAT_EQ(aQuads[0].m_Y1, 20);
}

TEST(QmBgDrawGeometry, ZeroWidthRetainsTheExistingCircleSubdivision)
{
	TQuads aQuads{};
	ASSERT_EQ(QmBgDrawBuildArc({10, 20}, 0, 2 * pi, 0, aQuads), 16);
	for(int i = 0; i < 16; ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(aQuads[i].m_X0, 10);
		EXPECT_FLOAT_EQ(aQuads[i].m_Y0, 20);
		EXPECT_FLOAT_EQ(aQuads[i].m_X1, 10);
		EXPECT_FLOAT_EQ(aQuads[i].m_Y1, 20);
	}
}

TEST(QmBgDrawGeometry, LargeFiniteSweepIsClampedBeforeItsIntegerConversion)
{
	TQuads aQuads{};
	EXPECT_EQ(QmBgDrawBuildArc({0, 0}, 0, 1.0e30f, 2, aQuads), 20);
}

TEST(QmBgDrawGeometry, NonfiniteArcInputsRejectTheWholeBatch)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Bad);
		for(int Field = 0; Field < 5; ++Field)
		{
			SCOPED_TRACE(Field);
			std::array<float, 5> aInput{10, 20, 0, pi / 2, 4};
			aInput[Field] = Bad;
			TQuads aQuads{};
			EXPECT_EQ(QmBgDrawBuildArc({aInput[0], aInput[1]}, aInput[2], aInput[3], aInput[4], aQuads), 0);
		}
	}
}

TEST(QmBgDrawGeometry, FiniteAnglesWithOverflowedDifferenceRejectTheWholeBatch)
{
	TQuads aQuads{};
	EXPECT_EQ(QmBgDrawBuildArc({0, 0}, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 2, aQuads), 0);
}

TEST(QmBgDrawGeometry, FiniteInputsWithOverflowedVerticesRejectTheWholeBatch)
{
	TQuads aQuads{};
	EXPECT_EQ(QmBgDrawBuildArc({std::numeric_limits<float>::max(), 0}, 0, pi / 2, std::numeric_limits<float>::max(), aQuads), 0);
}

TEST(QmBgDrawGeometry, EveryQuadCoordinateMustBeFinite)
{
	for(int Field = 0; Field < 8; ++Field)
	{
		SCOPED_TRACE(Field);
		IGraphics::CFreeformItem Quad(1, 2, 3, 4, 5, 6, 7, 8);
		ASSERT_TRUE(QmBgDrawQuadIsFinite(Quad));
		float *apFields[]{&Quad.m_X0, &Quad.m_Y0, &Quad.m_X1, &Quad.m_Y1, &Quad.m_X2, &Quad.m_Y2, &Quad.m_X3, &Quad.m_Y3};
		*apFields[Field] = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(QmBgDrawQuadIsFinite(Quad));
	}
}

TEST(QmBgDrawGeometry, ColorAdmissionRejectsNonfiniteChannelsWithoutClampingFiniteValues)
{
	EXPECT_TRUE(QmBgDrawColorIsFinite({-1, 2, std::numeric_limits<float>::max(), 0}));
	for(int Field = 0; Field < 4; ++Field)
	{
		SCOPED_TRACE(Field);
		ColorRGBA Color(0.2f, 0.4f, 0.6f, 0.8f);
		float *apFields[]{&Color.r, &Color.g, &Color.b, &Color.a};
		*apFields[Field] = std::numeric_limits<float>::infinity();
		EXPECT_FALSE(QmBgDrawColorIsFinite(Color));
	}
}
