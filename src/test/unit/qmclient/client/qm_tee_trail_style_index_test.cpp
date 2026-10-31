#include <game/client/components/qmclient/tee_trail_styles.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
	void ExpectFinite(const std::vector<qm_tee_trail::SQuad> &vQuads)
	{
		EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS);
		for(const auto &Quad : vQuads)
			for(int i = 0; i < 4; ++i)
			{
				EXPECT_TRUE(std::isfinite(Quad.m_aPos[i].x));
				EXPECT_TRUE(std::isfinite(Quad.m_aPos[i].y));
				EXPECT_TRUE(std::isfinite(Quad.m_aColor[i].r));
				EXPECT_TRUE(std::isfinite(Quad.m_aColor[i].g));
				EXPECT_TRUE(std::isfinite(Quad.m_aColor[i].b));
				EXPECT_TRUE(std::isfinite(Quad.m_aColor[i].a));
			}
	}
}

TEST(QmTeeTrailStyleIndex, OrdinaryDistancesKeepTheFloorRule)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(0), 0u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(1.99), 1u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(12345.75), 12345u);
}

TEST(QmTeeTrailStyleIndex, NegativeFractionsKeepTheirOriginalWrappedIndex)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(-0.25), 0xffffffffu);
	EXPECT_EQ(qm_tee_trail::StyleCellId(-1.25), 0xfffffffeu);
	EXPECT_EQ(qm_tee_trail::StyleCellId(-4294967296.0), 0u);
}

TEST(QmTeeTrailStyleIndex, LastRepresentableSignedDomainValuesKeepTheirLowBits)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(9223372036854774784.0), 0xfffffc00u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(-9223372036854775808.0), 0u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(-9223372036854774784.0), 1024u);
}

TEST(QmTeeTrailStyleIndex, SignedUpperLimitAndLargerDistanceAreReducedBeforeConversion)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(9223372036854775808.0), 0u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(9223372036854777856.0), 2048u);
}

TEST(QmTeeTrailStyleIndex, DistanceBelowTheSignedLowerLimitKeepsItsWrappedLowBits)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(-9223372036854777856.0), 0xfffff800u);
}

TEST(QmTeeTrailStyleIndex, DistanceBeyondTheUnsignedWideDomainRetainsItsLowBits)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(18446744073709555712.0), 4096u);
	EXPECT_EQ(qm_tee_trail::StyleCellId(1180591620717411565568.0), 262144u);
}

TEST(QmTeeTrailStyleIndex, NegativeDistanceBeyondTheUnsignedWideDomainRetainsItsLowBits)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(-18446744073709555712.0), 0xfffff000u);
}

TEST(QmTeeTrailStyleIndex, LargestFinitePositiveDistanceStillHasAnIndex)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(std::numeric_limits<double>::max()), 0u);
}

TEST(QmTeeTrailStyleIndex, LargestFiniteNegativeDistanceStillHasAnIndex)
{
	EXPECT_EQ(qm_tee_trail::StyleCellId(-std::numeric_limits<double>::max()), 0u);
}

TEST(QmTeeTrailStyleIndex, NonfiniteDistanceHasNoIndex)
{
	EXPECT_FALSE(qm_tee_trail::StyleCellId(std::numeric_limits<double>::quiet_NaN()));
	EXPECT_FALSE(qm_tee_trail::StyleCellId(std::numeric_limits<double>::infinity()));
	EXPECT_FALSE(qm_tee_trail::StyleCellId(-std::numeric_limits<double>::infinity()));
}

TEST(QmTeeTrailStyleIndex, ModernGeometryCanPassLargeFinitePathMetadataToEveryStyle)
{
	std::vector<CTrailPart> vTrail(4);
	for(size_t i = 0; i < vTrail.size(); ++i)
	{
		vTrail[i].m_Pos = vec2(90 - float(i) * 30, 40);
		vTrail[i].m_Time = 40 - double(i);
		vTrail[i].m_Distance = 1.0e30;
		vTrail[i].m_Speed = 20;
		vTrail[i].m_Life = 100;
	}
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC, qm_tee_trail::STYLE_PIXEL})
	{
		SCOPED_TRACE(Style);
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 40, 12, 9, vQuads);
		if(Style == qm_tee_trail::STYLE_MAGIC)
			EXPECT_FALSE(vQuads.empty());
		ExpectFinite(vQuads);
	}
}

TEST(QmTeeTrailStyleIndex, ExtremeFinitePathMetadataDoesNotRequireAnIntegerAnchorDomain)
{
	for(double Distance : {std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()})
	{
		SCOPED_TRACE(Distance);
		qm_tee_trail::SStyleSample aSamples[3]{};
		for(int i = 0; i < 3; ++i)
		{
			auto &S = aSamples[i];
			S.m_Pos = vec2(i * 30, 40);
			S.m_Normal = vec2(0, 1);
			S.m_Tint = ColorRGBA(1, 1, 1, 1);
			S.m_Distance = Distance;
			S.m_Age = 1;
			S.m_Width = 10;
			S.m_Alpha = 1;
			S.m_Energy = 1;
			S.m_Head = i * 30;
			S.m_Remaining = 1;
		}
		for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC})
		{
			SCOPED_TRACE(Style);
			std::vector<qm_tee_trail::SQuad> vQuads;
			qm_tee_trail::BuildStyledEffect(aSamples, 3, Style, true, 10, 1, 9, vQuads);
			if(Style == qm_tee_trail::STYLE_MAGIC)
				EXPECT_FALSE(vQuads.empty());
			ExpectFinite(vQuads);
		}
	}
}
