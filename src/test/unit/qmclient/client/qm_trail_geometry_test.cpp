#include <game/client/components/qmclient/trail_band_geometry.h>
#include <game/client/components/qmclient/trail_band_section.h>
#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

TEST(QmTrailCurve, PreparedCurveKeepsEndpointsAndStraightMidpoint)
{
	const auto Curve = qm_tee_trail::PrepareCurve({-6.0f, 4.0f}, {0.0f, 4.0f}, {6.0f, 4.0f}, {12.0f, 4.0f});
	EXPECT_EQ(Curve.Evaluate(0.0f), vec2(0.0f, 4.0f));
	EXPECT_EQ(Curve.Evaluate(0.5f), vec2(3.0f, 4.0f));
	EXPECT_EQ(Curve.Evaluate(1.0f), vec2(6.0f, 4.0f));
}

TEST(QmTrailCurve, CollapsedCurveStaysAtItsOnlyPoint)
{
	const vec2 Point(3.0f, 4.0f);
	const auto Curve = qm_tee_trail::PrepareCurve(Point, Point, Point, Point);
	for(float Position : {0.0f, 0.25f, 0.5f, 1.0f})
	{
		SCOPED_TRACE(Position);
		EXPECT_EQ(Curve.Evaluate(Position), Point);
	}
}

TEST(QmTrailBand, PreparedSectionKeepsTransparentEdgesAndSolidInnerColors)
{
	const auto Section = QmPrepareTrailBandSection({10.0f, 20.0f}, 4.0f, 8.0f, ColorRGBA(0.2f, 0.4f, 0.6f, 0.5f), {0.0f, 1.0f}, 0.5f, 1.0f);
	EXPECT_EQ(Section.m_aPos[0], vec2(10.0f, 16.0f));
	EXPECT_EQ(Section.m_aPos[1], vec2(10.0f, 18.0f));
	EXPECT_EQ(Section.m_aPos[2], vec2(10.0f, 24.0f));
	EXPECT_EQ(Section.m_aPos[3], vec2(10.0f, 28.0f));
	EXPECT_FLOAT_EQ(Section.m_aColor[0].a, 0.0f);
	EXPECT_FLOAT_EQ(Section.m_aColor[3].a, 0.0f);
	EXPECT_FLOAT_EQ(Section.m_aColor[1].a, 0.5f);
	EXPECT_FLOAT_EQ(Section.m_aColor[2].a, 0.5f);
}

TEST(QmTrailBand, CoincidentPointsAndReversalsProduceFiniteNonnegativeWidths)
{
	struct SPoint
	{
		vec2 m_Pos;
		float m_Left = 4.0f;
		float m_Right = 6.0f;
	};
	for(const std::array<vec2, 3> Positions : {std::array<vec2, 3>{vec2(0, 0), vec2(0, 0), vec2(0, 0)}, std::array<vec2, 3>{vec2(0, 0), vec2(6, 0), vec2(0, 0)}})
	{
		std::array<SPoint, 3> Band = {SPoint{Positions[0]}, SPoint{Positions[1]}, SPoint{Positions[2]}};
		std::array<vec2, 3> Normals;
		QmPrepareTrailBandJoins(Band.data(), Band.size(), Normals.data());
		for(size_t Index = 0; Index < Band.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			EXPECT_TRUE(std::isfinite(Normals[Index].x));
			EXPECT_TRUE(std::isfinite(Normals[Index].y));
			EXPECT_TRUE(std::isfinite(Band[Index].m_Left));
			EXPECT_TRUE(std::isfinite(Band[Index].m_Right));
			EXPECT_GE(Band[Index].m_Left, 0.0f);
			EXPECT_GE(Band[Index].m_Right, 0.0f);
		}
	}
}
