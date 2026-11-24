#include <game/map/qm_entity_overlay_tint.h>

#include <gtest/gtest.h>

TEST(QmEntityOverlayTint, BlackFreezeTileAppliesTintAtFullIntensity)
{
	const ColorRGBA Source(0.0f, 0.0f, 0.0f, 0.5f);
	const ColorRGBA Tint(1.0f, 0.0f, 0.0f, 1.0f);
	const ColorRGBA Result = QmTintEntityOverlayPixel(Source, Tint);
	EXPECT_FLOAT_EQ(Result.r, 1.0f);
	EXPECT_FLOAT_EQ(Result.g, 0.0f);
	EXPECT_FLOAT_EQ(Result.b, 0.0f);
	EXPECT_FLOAT_EQ(Result.a, 0.5f);
}

TEST(QmEntityOverlayTint, LumaCarriesExistingTileShading)
{
	const ColorRGBA Source(0.5f, 0.5f, 0.5f, 0.8f);
	const ColorRGBA Tint(0.0f, 0.0f, 1.0f, 1.0f);
	const ColorRGBA Result = QmTintEntityOverlayPixel(Source, Tint);
	EXPECT_NEAR(Result.r, 0.0f, 1e-6f);
	EXPECT_NEAR(Result.g, 0.0f, 1e-6f);
	EXPECT_NEAR(Result.b, 0.5f, 1e-6f);
	EXPECT_FLOAT_EQ(Result.a, 0.8f);
}

TEST(QmEntityOverlayTint, WhiteTintKeepsSourcePixels)
{
	const ColorRGBA Source(0.2f, 0.4f, 0.6f, 0.5f);
	const ColorRGBA Result = QmTintEntityOverlayPixel(Source, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	EXPECT_FLOAT_EQ(Result.r, Source.r);
	EXPECT_FLOAT_EQ(Result.g, Source.g);
	EXPECT_FLOAT_EQ(Result.b, Source.b);
	EXPECT_FLOAT_EQ(Result.a, Source.a);
}

TEST(QmEntityOverlayTint, TintAlphaScalesTileAlpha)
{
	const ColorRGBA Source(0.0f, 0.0f, 0.0f, 0.8f);
	const ColorRGBA Result = QmTintEntityOverlayPixel(Source, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
	EXPECT_FLOAT_EQ(Result.a, 0.4f);
}

TEST(QmEntityOverlayTint, TransparentPixelsStayTransparent)
{
	const ColorRGBA Result = QmTintEntityOverlayPixel(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(Result.a, 0.0f);
}
