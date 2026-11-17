#include <game/client/components/qmclient/tee_trail_style_common.h>
#include <game/client/components/qmclient/trail_band_geometry.h>
#include <game/client/components/qmclient/trail_band_section.h>
#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

TEST(QmTrailBand, HeadStartsCollapsedAndTransparentThenRecoversTheBody)
{
	using qm_tee_trail::style_detail::SBandPoint;
	const ColorRGBA Color(0.2f, 0.4f, 0.6f, 0.8f);
	std::array<SBandPoint, 5> Band;
	const float aDistances[] = {0.0f, 4.0f, 8.0f, 16.0f, 64.0f};
	for(size_t i = 0; i < Band.size(); ++i)
		Band[i] = {vec2(aDistances[i], 0.0f), 8.0f, 6.0f, Color};
	const auto Original = Band;
	QmPrepareTrailBandHead(Band.data(), Band.size());
	EXPECT_FLOAT_EQ(Band[0].m_Left, 0.0f);
	EXPECT_FLOAT_EQ(Band[0].m_Right, 0.0f);
	EXPECT_FLOAT_EQ(Band[0].m_Color.a, 0.0f);
	for(size_t i = 1; i < 3; ++i)
	{
		EXPECT_GT(Band[i].m_Left, Band[i - 1].m_Left);
		EXPECT_LT(Band[i].m_Left, Original[i].m_Left);
		EXPECT_GT(Band[i].m_Color.a, Band[i - 1].m_Color.a);
		EXPECT_LT(Band[i].m_Color.a, Color.a);
	}
	for(size_t i = 0; i < Band.size(); ++i)
	{
		EXPECT_EQ(Band[i].m_Pos, Original[i].m_Pos);
		EXPECT_FLOAT_EQ(Band[i].m_Color.r, Color.r);
		EXPECT_FLOAT_EQ(Band[i].m_Color.g, Color.g);
		EXPECT_FLOAT_EQ(Band[i].m_Color.b, Color.b);
		if(i >= 3)
		{
			EXPECT_FLOAT_EQ(Band[i].m_Left, Original[i].m_Left);
			EXPECT_FLOAT_EQ(Band[i].m_Right, Original[i].m_Right);
			EXPECT_FLOAT_EQ(Band[i].m_Color.a, Color.a);
		}
	}
}

TEST(QmTrailBand, HeadTransitionUsesDistanceRatherThanSampleCount)
{
	using qm_tee_trail::style_detail::SBandPoint;
	std::array<SBandPoint, 3> Sparse;
	std::array<SBandPoint, 5> Dense;
	for(size_t i = 0; i < Sparse.size(); ++i)
		Sparse[i] = {vec2(float(i) * 4.0f, 0.0f), 8.0f, 8.0f, ColorRGBA(1, 1, 1, 1)};
	for(size_t i = 0; i < Dense.size(); ++i)
		Dense[i] = {vec2(float(i) * 2.0f, 0.0f), 8.0f, 8.0f, ColorRGBA(1, 1, 1, 1)};
	QmPrepareTrailBandHead(Sparse.data(), Sparse.size());
	QmPrepareTrailBandHead(Dense.data(), Dense.size());
	for(size_t i = 0; i < Sparse.size(); ++i)
	{
		EXPECT_FLOAT_EQ(Sparse[i].m_Left, Dense[i * 2].m_Left);
		EXPECT_FLOAT_EQ(Sparse[i].m_Color.a, Dense[i * 2].m_Color.a);
	}
}

TEST(QmTrailBand, ShortAndCoincidentHeadsStayFinite)
{
	using qm_tee_trail::style_detail::SBandPoint;
	for(float Span : {0.0f, 0.25f, 2.0f})
	{
		std::array<SBandPoint, 2> Band = {{
			{vec2(0, 0), 8.0f, 8.0f, ColorRGBA(1, 1, 1, 0.5f)},
			{vec2(Span, 0), 8.0f, 8.0f, ColorRGBA(1, 1, 1, 0.5f)},
		}};
		QmPrepareTrailBandHead(Band.data(), Band.size());
		EXPECT_TRUE(std::isfinite(Band[1].m_Left));
		EXPECT_TRUE(std::isfinite(Band[1].m_Color.a));
		EXPECT_GE(Band[1].m_Left, 0.0f);
		EXPECT_LT(Band[1].m_Left, 8.0f);
		if(Span > 0.0f)
		{
			EXPECT_GT(Band[1].m_Left, 0.0f);
			EXPECT_GT(Band[1].m_Color.a, 0.0f);
		}
	}
}

TEST(QmTrailBand, StyledBandsHaveNoVisibleFlatHeadInAnyDirection)
{
	using qm_tee_trail::style_detail::SBandPoint;
	for(vec2 Direction : {vec2(1, 0), vec2(0, 1), vec2(0.6f, 0.8f)})
		for(bool Additive : {false, true})
			for(float Width : {1.0f, 12.0f, 40.0f})
			{
				SCOPED_TRACE(Width);
				std::array<SBandPoint, 65> Band;
				for(size_t i = 0; i < Band.size(); ++i)
					Band[i] = {Direction * (float(i) * 2.0f), Width, Width, ColorRGBA(1, 1, 1, 0.8f)};
				std::vector<qm_tee_trail::SQuad> vQuads;
				qm_tee_trail::style_detail::Band(vQuads, Band.data(), Band.size(), 0.5f, 1.0f, Additive);
				ASSERT_FALSE(vQuads.empty());
				bool VisibleBody = false;
				for(const auto &Quad : vQuads)
					for(size_t i = 0; i < 4; ++i)
					{
						if(std::abs(dot(Quad.m_aPos[i], Direction)) < 0.001f)
							EXPECT_FLOAT_EQ(Quad.m_aColor[i].a, 0.0f);
						VisibleBody |= Quad.m_aColor[i].a > 0.5f;
					}
				EXPECT_TRUE(VisibleBody);
			}
}

TEST(QmTrailBand, OriginalHeadIsSoftWhenTailTaperAndFadeAreDisabled)
{
	std::vector<CTrailPart> vTrail(17);
	for(size_t i = 0; i < vTrail.size(); ++i)
	{
		vTrail[i].m_Pos = vec2(0.0f, float(i) * 4.0f);
		vTrail[i].m_Time = 40.0 - double(i) * 0.1;
		vTrail[i].m_Tick = int(vTrail[i].m_Time);
		vTrail[i].m_Life = 100.0f;
		vTrail[i].m_Speed = 20.0f;
		vTrail[i].m_Distance = double(vTrail.size() - 1 - i) * 4.0;
		vTrail[i].m_Col = ColorRGBA(1, 1, 1, 0.8f);
	}
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_ORIGINAL, false, 40.0, 12.0f, 1, vQuads, 1.0f, false, false);
	ASSERT_FALSE(vQuads.empty());
	bool VisibleBody = false;
	for(const auto &Quad : vQuads)
		for(size_t i = 0; i < 4; ++i)
		{
			if(std::abs(Quad.m_aPos[i].y) < 0.001f)
				EXPECT_FLOAT_EQ(Quad.m_aColor[i].a, 0.0f);
			VisibleBody |= Quad.m_aColor[i].a > 0.5f;
		}
	EXPECT_TRUE(VisibleBody);
}

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
