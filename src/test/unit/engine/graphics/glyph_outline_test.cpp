#include <engine/client/glyph_lookup_cache.h>
#include <engine/client/glyph_outline.h>
#include <engine/client/qm_glyph_geometry.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

TEST(QmGlyphOutline, IntegerRadiiRetainExistingCoverage)
{
	std::array<unsigned char, 121> Input{}, Legacy{}, Continuous{};
	Input[0] = 127;
	Input[60] = 255;
	Input[120] = 200;
	for(int Radius = 0; Radius <= 4; ++Radius)
	{
		QmGrowGlyphOutline(Input.data(), Legacy.data(), 11, 11, Radius);
		QmGrowGlyphOutlineContinuous(Input.data(), Continuous.data(), 11, 11, Radius);
		EXPECT_EQ(Continuous, Legacy) << Radius;
	}
}

TEST(QmGlyphOutline, FractionalRadiusChangesCoverageGraduallyAndPreservesFill)
{
	std::array<unsigned char, 25> Input{}, Smaller{}, Larger{};
	Input[12] = 255;
	Input[0] = 120;
	QmGrowGlyphOutlineContinuous(Input.data(), Smaller.data(), 5, 5, 0.5f);
	QmGrowGlyphOutlineContinuous(Input.data(), Larger.data(), 5, 5, 0.75f);
	EXPECT_GT(Smaller[13], 0);
	EXPECT_LT(Smaller[13], Larger[13]);
	EXPECT_LT(Larger[13], 255);
	for(size_t i = 0; i < Input.size(); ++i)
	{
		EXPECT_GE(Smaller[i], Input[i]);
		EXPECT_GE(Larger[i], Smaller[i]);
	}
}

TEST(QmGlyphOutline, RadiusThresholdDoesNotDoubleWorldThickness)
{
	for(int Size : {17, 18, 48, 49, 128})
		EXPECT_NEAR(QmNameplateGlyphOutlineRadius(Size) / Size, 0.05f, 0.000001f);
	EXPECT_FLOAT_EQ(QmNameplateGlyphOutlineRadius(-100), 0.3f);
	EXPECT_FLOAT_EQ(QmNameplateGlyphOutlineRadius(1000), 6.4f);
}

TEST(QmGlyphOutline, ClippedSinglePixelAndEmptyBuffersAreSafe)
{
	unsigned char Input = 177, Output = 0;
	QmGrowGlyphOutlineContinuous(&Input, &Output, 1, 1, 6.4f);
	EXPECT_EQ(Output, Input);
	QmGrowGlyphOutlineContinuous(nullptr, nullptr, 0, 0, 0.0f);
	QmGrowGlyphOutlineContinuous(&Input, &Output, 1, 1, 0.0f);
	EXPECT_EQ(Output, Input);
}

TEST(QmGlyphGeometry, NestedContourBecomesHoleAndTriangulatesIndependently)
{
	FT_Vector aPoints[] = {
		{0, 0},
		{256, 0},
		{256, 256},
		{0, 256},
		{64, 64},
		{64, 192},
		{192, 192},
		{192, 64},
	};
	unsigned char aTags[] = {
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
		FT_CURVE_TAG_ON,
	};
	unsigned short aContours[] = {3, 7};
	FT_Outline Outline{};
	Outline.n_contours = 2;
	Outline.n_points = 8;
	Outline.points = aPoints;
	Outline.tags = aTags;
	Outline.contours = aContours;
	Outline.flags = FT_OUTLINE_NONE;
	FT_Glyph_Metrics Metrics{};
	Metrics.width = 256;
	Metrics.height = 256;
	Metrics.horiBearingX = 0;
	Metrics.horiBearingY = 256;
	Metrics.horiAdvance = 320;

	SQmGlyphGeometry Geometry;
	ASSERT_TRUE(QmBuildGlyphGeometry(Outline, Metrics, 16, false, false, Geometry));
	ASSERT_EQ(Geometry.m_vContours.size(), 2u);
	EXPECT_EQ(Geometry.m_FillRule, EQmGlyphFillRule::NON_ZERO);
	EXPECT_EQ(Geometry.m_vContours[0].m_Depth, 0);
	EXPECT_EQ(Geometry.m_vContours[1].m_Depth, 1);
	EXPECT_NE(Geometry.m_vContours[0].m_WindingSign, Geometry.m_vContours[1].m_WindingSign);
	EXPECT_GT(Geometry.m_vContours[0].m_vTriangles.size(), 0u);
	EXPECT_GT(Geometry.m_vContours[1].m_vTriangles.size(), 0u);
}

TEST(QmGlyphGeometry, GpuCoverageDownsampleUsesWhiteBackground)
{
	std::array<uint8_t, 16> aRgba{};
	aRgba.fill(255);
	std::array<uint8_t, 4> aFill{};
	ASSERT_TRUE(QmDownsampleGpuGlyphCoverage(aRgba.data(), 2, 2, 0, 0, 1, 1, 2, EQmGlyphFillRule::EVEN_ODD, aFill.data()));
	EXPECT_EQ(aFill[0], 0);

	for(int y = 0; y < 2; ++y)
		for(int x = 0; x < 2; ++x)
			for(int Channel = 0; Channel < 3; ++Channel)
				aRgba[(y * 2 + x) * 4 + Channel] = 0;
	ASSERT_TRUE(QmDownsampleGpuGlyphCoverage(aRgba.data(), 2, 2, 0, 0, 1, 1, 2, EQmGlyphFillRule::EVEN_ODD, aFill.data()));
	EXPECT_EQ(aFill[0], 255);
}

TEST(QmGlyphGeometry, GpuCoverageNonZeroUsesSignedWindingChannels)
{
	std::array<uint8_t, 16> aRgba{};
	std::array<uint8_t, 4> aFill{};
	// 4x 超采样中只有一个样本有正环绕，其余无环绕。
	aRgba[0] = 16;
	ASSERT_TRUE(QmDownsampleGpuGlyphCoverage(aRgba.data(), 2, 2, 0, 0, 1, 1, 2, EQmGlyphFillRule::NON_ZERO, aFill.data()));
	EXPECT_EQ(aFill[0], 64);

	// 正负环绕相等（外轮廓 + 孔洞）应回到透明：同一采样点红绿通道相同。
	aRgba.fill(0);
	aRgba[0] = 16;
	aRgba[1] = 16;
	ASSERT_TRUE(QmDownsampleGpuGlyphCoverage(aRgba.data(), 2, 2, 0, 0, 1, 1, 2, EQmGlyphFillRule::NON_ZERO, aFill.data()));
	EXPECT_EQ(aFill[0], 0);
}

TEST(QmGlyphLookupCache, NameplateAndNormalProfilesNeverCrossHit)
{
	CQmGlyphLookupCache<int> Cache;
	int Face = 0, Normal = 1, Nameplate = 2;
	Cache.Store(&Face, 65, 18, &Normal);
	EXPECT_EQ(Cache.Find(&Face, 65, 18), &Normal);
	EXPECT_EQ(Cache.Find(&Face, 65, 18, 1), nullptr);
	Cache.Store(&Face, 65, 18, &Nameplate, 1);
	EXPECT_EQ(Cache.Find(&Face, 65, 18, 1), &Nameplate);
	EXPECT_EQ(Cache.Find(&Face, 65, 18), &Normal);
	Cache.Reset();
	EXPECT_EQ(Cache.Find(&Face, 65, 18, 1), nullptr);
	EXPECT_EQ(Cache.Find(&Face, 65, 18), nullptr);
}
