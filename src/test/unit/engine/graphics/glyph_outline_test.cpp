#include <engine/client/glyph_lookup_cache.h>
#include <engine/client/glyph_outline.h>

#include <gtest/gtest.h>

#include <array>

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
