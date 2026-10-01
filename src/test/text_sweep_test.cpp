#include <engine/client/text_sweep.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
	using TSweepQuad = std::array<STextSweepVertex, 4>;

	TSweepQuad Glyph(float X = 0.0f, float Y = 0.0f, float Alpha = 1.0f)
	{
		return {{{vec2(X, Y + 10.0f), vec2(100.0f, 230.0f), Alpha},
			{vec2(X + 10.0f, Y + 10.0f), vec2(120.0f, 230.0f), Alpha},
			{vec2(X + 10.0f, Y), vec2(120.0f, 200.0f), Alpha},
			{vec2(X, Y), vec2(100.0f, 200.0f), Alpha}}};
	}

	std::vector<TSweepQuad> Clip(const TSweepQuad &Quad, const STextSweepBand &Band)
	{
		std::vector<TSweepQuad> vResult;
		TextSweepClipQuad(Quad, Band, [&](const TSweepQuad &Fragment) { vResult.push_back(Fragment); });
		return vResult;
	}
}

TEST(TextSweep, NarrowBandCutsInsideASingleGlyph)
{
	const auto vFragments = Clip(Glyph(), {5.0f, 2.0f, 0.0f});
	ASSERT_FALSE(vFragments.empty());
	float Area = 0.0f;
	for(const auto &Fragment : vFragments)
	{
		for(const auto &Vertex : Fragment)
		{
			EXPECT_GE(Vertex.m_Position.x, 3.0f - 0.0001f);
			EXPECT_LE(Vertex.m_Position.x, 7.0f + 0.0001f);
			EXPECT_GE(Vertex.m_Position.y, 0.0f);
			EXPECT_LE(Vertex.m_Position.y, 10.0f);
		}
		const vec2 A = Fragment[1].m_Position - Fragment[0].m_Position;
		const vec2 B = Fragment[2].m_Position - Fragment[0].m_Position;
		Area += std::abs(A.x * B.y - A.y * B.x) * 0.5f;
	}
	EXPECT_NEAR(Area, 40.0f, 0.001f);
}

TEST(TextSweep, ClippingPreservesAtlasCoordinatesAndFadesAtBothEdges)
{
	const auto Quad = Glyph(20.0f, 30.0f, 0.4f);
	const auto Original = Quad;
	const auto vFragments = Clip(Quad, {34.0f, 2.0f, 0.25f});
	ASSERT_FALSE(vFragments.empty());
	float MinAlpha = 1.0f;
	float MaxAlpha = 0.0f;
	for(const auto &Fragment : vFragments)
	{
		for(const auto &Vertex : Fragment)
		{
			EXPECT_NEAR(Vertex.m_TexCoord.x, 100.0f + (Vertex.m_Position.x - 20.0f) * 2.0f, 0.0001f);
			EXPECT_NEAR(Vertex.m_TexCoord.y, 200.0f + (Vertex.m_Position.y - 30.0f) * 3.0f, 0.0001f);
			EXPECT_GE(Vertex.m_Alpha, 0.0f);
			EXPECT_LE(Vertex.m_Alpha, 0.4f);
			MinAlpha = std::min(MinAlpha, Vertex.m_Alpha);
			MaxAlpha = std::max(MaxAlpha, Vertex.m_Alpha);
		}
	}
	EXPECT_NEAR(MinAlpha, 0.0f, 0.0001f);
	EXPECT_NEAR(MaxAlpha, 0.4f, 0.0001f);
	for(size_t i = 0; i < Quad.size(); ++i)
	{
		EXPECT_EQ(Quad[i].m_Position, Original[i].m_Position);
		EXPECT_EQ(Quad[i].m_TexCoord, Original[i].m_TexCoord);
		EXPECT_EQ(Quad[i].m_Alpha, Original[i].m_Alpha);
	}
}

TEST(TextSweepLayout, EachSelectedLineContainsOnlyItsOwnGlyphs)
{
	CTextSweepLayout Layout;
	Layout.AddQuad(1, 0);
	Layout.AddQuad(1, 1);
	Layout.AddQuad(2, 2);
	Layout.AddQuad(2, 3);
	Layout.AddQuad(2, 4);
	Layout.AddQuad(3, 5);
	ASSERT_EQ(Layout.LineCount(), 3);
	EXPECT_EQ(Layout.Line(0).m_Begin, 0u);
	EXPECT_EQ(Layout.Line(0).m_End, 2u);
	EXPECT_EQ(Layout.Line(1).m_Begin, 2u);
	EXPECT_EQ(Layout.Line(1).m_End, 5u);
	EXPECT_EQ(Layout.Line(2).m_Begin, 5u);
	EXPECT_EQ(Layout.Line(2).m_End, 6u);
}

TEST(TextSweepLayout, PrefixAndBlankLinesDoNotAddEmptySweepSteps)
{
	CTextSweepLayout Layout;
	Layout.AddQuad(6, 0);
	Layout.AddQuad(6, 1);
	Layout.AddQuad(9, 2);
	ASSERT_EQ(Layout.LineCount(), 2);
	EXPECT_EQ(Layout.Line(0).m_Begin, 0u);
	EXPECT_EQ(Layout.Line(0).m_End, 2u);
	EXPECT_EQ(Layout.Line(1).m_Begin, 2u);
	EXPECT_EQ(Layout.Line(1).m_End, 3u);
}

TEST(TextSweepLayout, ContinuingAnAppendExtendsTheExistingLine)
{
	CTextSweepLayout Layout;
	Layout.AddQuad(1, 0);
	EXPECT_EQ(Layout.Line(0).m_End, 1u);
	Layout.AddQuad(1, 1);
	EXPECT_EQ(Layout.LineCount(), 1);
	EXPECT_EQ(Layout.Line(0).m_Begin, 0u);
	EXPECT_EQ(Layout.Line(0).m_End, 2u);
}

TEST(TextSweepLayout, RebuildingDiscardsPreviousWrapRanges)
{
	CTextSweepLayout Layout;
	Layout.AddQuad(1, 0);
	Layout.AddQuad(2, 1);
	Layout.Clear();
	EXPECT_EQ(Layout.LineCount(), 0);
	Layout.AddQuad(1, 0);
	Layout.AddQuad(1, 1);
	ASSERT_EQ(Layout.LineCount(), 1);
	EXPECT_EQ(Layout.Line(0).m_Begin, 0u);
	EXPECT_EQ(Layout.Line(0).m_End, 2u);
	EXPECT_EQ(Layout.Line(1).m_Begin, Layout.Line(1).m_End);
}

TEST(TextSweepLayout, MissingLinesHaveAnEmptyRange)
{
	CTextSweepLayout Layout;
	EXPECT_EQ(Layout.Line(0).m_Begin, Layout.Line(0).m_End);
	Layout.AddQuad(1, 0);
	for(int Index : {-1, 1, 2})
		EXPECT_EQ(Layout.Line(Index).m_Begin, Layout.Line(Index).m_End);
}

TEST(TextSweep, TravelStartsAndEndsOutsideTheSelectedLine)
{
	const float Start = TextSweepCenter(0.0f, 40.0f, 2.0f, 0.0f);
	const float End = TextSweepCenter(0.0f, 40.0f, 2.0f, 1.0f);
	EXPECT_TRUE(Clip(Glyph(), {Start, 2.0f, 0.0f}).empty());
	EXPECT_TRUE(Clip(Glyph(30.0f), {End, 2.0f, 0.0f}).empty());
	const float Middle = TextSweepCenter(0.0f, 40.0f, 2.0f, 0.5f);
	EXPECT_FALSE(Clip(Glyph(15.0f), {Middle, 2.0f, 0.0f}).empty());
}

TEST(TextSweep, MovingTheBandMovesFragmentsWithinOneCharacter)
{
	for(float Center : {3.0f, 3.25f, 3.5f})
	{
		const auto vFragments = Clip(Glyph(), {Center, 1.0f, 0.0f});
		ASSERT_FALSE(vFragments.empty());
		float MinX = 10.0f;
		float MaxX = 0.0f;
		for(const auto &Fragment : vFragments)
		{
			for(const auto &Vertex : Fragment)
			{
				MinX = std::min(MinX, Vertex.m_Position.x);
				MaxX = std::max(MaxX, Vertex.m_Position.x);
			}
		}
		EXPECT_NEAR(MinX, Center - 1.0f, 0.0001f);
		EXPECT_NEAR(MaxX, Center + 1.0f, 0.0001f);
	}
}

TEST(TextSweep, InvisibleGlyphsDoNotBecomeVisible)
{
	const auto vFragments = Clip(Glyph(0.0f, 0.0f, 0.0f), {5.0f, 2.0f, 0.0f});
	ASSERT_FALSE(vFragments.empty());
	for(const auto &Fragment : vFragments)
		for(const auto &Vertex : Fragment)
			EXPECT_FLOAT_EQ(Vertex.m_Alpha, 0.0f);
}

TEST(TextSweep, InvalidOrDistantBandsProduceNoFragments)
{
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for(const auto &Band : std::array<STextSweepBand, 7>{
		    STextSweepBand{-10.0f, 1.0f, 0.0f}, {20.0f, 1.0f, 0.0f}, {5.0f, 0.0f, 0.0f},
		    {5.0f, -1.0f, 0.0f}, {NaN, 1.0f, 0.0f}, {5.0f, Infinity, 0.0f}, {5.0f, 1.0f, NaN}})
		EXPECT_TRUE(Clip(Glyph(), Band).empty());
}
