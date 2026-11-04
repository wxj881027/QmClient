#include <base/str.h>

#include <engine/gfx/image_loader.h>

#include <game/client/components/qmclient/map_cover_art.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
	QmMapCover::SArt DefaultArt(uint32_t Seed = 1234)
	{
		QmMapCover::SArt Art;
		Art.m_Seed = Seed;
		return Art;
	}

	CImageInfo SolidImage(int Width, int Height, uint8_t Red, uint8_t Green, uint8_t Blue)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_RGB;
		const size_t Size = static_cast<size_t>(Width) * Height * 3;
		Image.m_pData = static_cast<uint8_t *>(malloc(Size));
		for(size_t i = 0; i < Size; i += 3)
		{
			Image.m_pData[i] = Red;
			Image.m_pData[i + 1] = Green;
			Image.m_pData[i + 2] = Blue;
		}
		return Image;
	}

	const uint8_t *PixelAt(const CImageInfo &Image, int X, int Y)
	{
		return Image.m_pData + (static_cast<size_t>(Y) * Image.m_Width + X) * 4;
	}
} // namespace

TEST(QmMapCoverArt, ThumbnailUrlOnlyAcceptsOfficialDdnetPng)
{
	EXPECT_TRUE(QmMapCover::ValidThumbnailUrl("https://ddnet.org/ranks/maps/_MegaRosenkohl.png"));
	EXPECT_TRUE(QmMapCover::ValidThumbnailUrl("https://ddnet.org/ranks/maps/17_Jungle.png"));
	EXPECT_TRUE(QmMapCover::ValidThumbnailUrl("https://ddnet.org/ranks/maps/007___2.png"));

	const char *apRejected[] = {
		nullptr,
		"",
		"https://ddnet.org/ranks/maps/",
		"https://ddnet.org/ranks/maps/Map.jpg",
		"https://ddnet.org/ranks/maps/Map",
		"http://ddnet.org/ranks/maps/Map.png",
		"https://example.com/ranks/maps/Map.png",
		"https://ddnet.org/ranks/maps/../Map.png",
		"https://ddnet.org/ranks/maps/Map.png?size=1",
		"https://ddnet.org/ranks/maps/Map Name.png",
		"https://ddnet.org/ranks/maps/Map%20Name.png",
	};
	for(const char *pUrl : apRejected)
	{
		SCOPED_TRACE(pUrl != nullptr ? pUrl : "<null>");
		EXPECT_FALSE(QmMapCover::ValidThumbnailUrl(pUrl));
	}
}

TEST(QmMapCoverArt, CacheFileNameMirrorsOfficialLeaf)
{
	char aPath[64];
	ASSERT_TRUE(QmMapCover::ThumbnailCacheFileName("https://ddnet.org/ranks/maps/_MegaRosenkohl.png", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "qmclient/mapcovers/cover__MegaRosenkohl.png");

	char aSmall[8];
	EXPECT_FALSE(QmMapCover::ThumbnailCacheFileName("https://ddnet.org/ranks/maps/_MegaRosenkohl.png", aSmall, sizeof(aSmall)));
	EXPECT_STREQ(aSmall, "");
	EXPECT_FALSE(QmMapCover::ThumbnailCacheFileName("https://example.com/maps/Map.png", aPath, sizeof(aPath)));
}

TEST(QmMapCoverArt, TailStartsOpaqueAndFadesToNothing)
{
	const QmMapCover::SArt Art = DefaultArt();
	constexpr int Width = 167;
	constexpr int Height = 72;
	constexpr int ImageWidth = 115;
	// 缩略图最右一列仍然完全不透明；拖尾第一列亮度接近，尾巴末端落到 0。
	EXPECT_FLOAT_EQ(QmMapCover::TailAlpha(Art, ImageWidth, Height / 2, Width, Height, ImageWidth), 1.0f);
	EXPECT_GT(QmMapCover::TailAlpha(Art, ImageWidth + 1, Height / 2, Width, Height, ImageWidth), 0.5f);
	EXPECT_LT(QmMapCover::TailAlpha(Art, Width - 1, Height / 2, Width, Height, ImageWidth), 0.05f);
	// 拖尾之外的列不参与遮罩。
	EXPECT_FLOAT_EQ(QmMapCover::TailAlpha(Art, ImageWidth - 1, Height / 2, Width, Height, ImageWidth), 0.0f);
}

TEST(QmMapCoverArt, TailIsRaggedAcrossRows)
{
	const QmMapCover::SArt Art = DefaultArt();
	constexpr int Width = 167;
	constexpr int Height = 72;
	constexpr int ImageWidth = 115;
	constexpr int ProbeX = ImageWidth + 26; // 拖尾中段
	float Min = 1.0f;
	float Max = 0.0f;
	for(int Y = 1; Y + 1 < Height; ++Y)
	{
		const float Alpha = QmMapCover::TailAlpha(Art, ProbeX, Y, Width, Height, ImageWidth);
		Min = std::min(Min, Alpha);
		Max = std::max(Max, Alpha);
	}
	// 参差边界必须让同一列在不同行上取到明显不同的覆盖度；平直渐隐会得到接近 0 的跨度。
	EXPECT_GT(Max - Min, 0.15f);
}

TEST(QmMapCoverArt, TailIsDeterministicAndDependsOnSeed)
{
	constexpr int Width = 167;
	constexpr int Height = 72;
	constexpr int ImageWidth = 115;
	const QmMapCover::SArt Art = DefaultArt();
	const QmMapCover::SArt Other = DefaultArt(987654);
	const int ProbeX = ImageWidth + 20;
	bool Different = false;
	for(int Y = 0; Y < Height; ++Y)
	{
		const float Alpha = QmMapCover::TailAlpha(Art, ProbeX, Y, Width, Height, ImageWidth);
		EXPECT_FLOAT_EQ(Alpha, QmMapCover::TailAlpha(Art, ProbeX, Y, Width, Height, ImageWidth));
		Different |= std::abs(Alpha - QmMapCover::TailAlpha(Other, ProbeX, Y, Width, Height, ImageWidth)) > 0.02f;
	}
	EXPECT_TRUE(Different);
}

TEST(QmMapCoverArt, ValueNoiseIsDeterministicAndBounded)
{
	for(float X = 0.0f; X < 4.0f; X += 0.37f)
	{
		for(float Y = 0.0f; Y < 3.0f; Y += 0.41f)
		{
			const float First = QmMapCover::ValueNoise(X, Y, 42);
			EXPECT_FLOAT_EQ(First, QmMapCover::ValueNoise(X, Y, 42));
			EXPECT_GE(First, 0.0f);
			EXPECT_LE(First, 1.0f);
		}
	}
	EXPECT_NE(QmMapCover::ValueNoise(1.3f, 0.7f, 1), QmMapCover::ValueNoise(1.3f, 0.7f, 2));
}

TEST(QmMapCoverArt, SeedForUrlIsStableAndDistinct)
{
	EXPECT_EQ(QmMapCover::SeedForUrl("https://ddnet.org/ranks/maps/A.png"), QmMapCover::SeedForUrl("https://ddnet.org/ranks/maps/A.png"));
	EXPECT_NE(QmMapCover::SeedForUrl("https://ddnet.org/ranks/maps/A.png"), QmMapCover::SeedForUrl("https://ddnet.org/ranks/maps/B.png"));
}

TEST(QmMapCoverArt, ComposeShowsShadedThumbnailAndTransparentTail)
{
	CImageInfo Source = SolidImage(4, 2, 200, 100, 50);
	CImageInfo Output;
	const QmMapCover::SArt Art = DefaultArt();
	ASSERT_TRUE(QmMapCover::Compose(Source, 167, 72, Art, Output));
	Source.Free();
	ASSERT_EQ(Output.m_Width, 167u);
	ASSERT_EQ(Output.m_Height, 72u);
	ASSERT_EQ(Output.m_Format, CImageInfo::FORMAT_RGBA);

	// 缩略图区域不透明且按 m_Shade 压暗（±2 吸收浮点舍入）。
	const uint8_t *pThumb = PixelAt(Output, 5, 36);
	EXPECT_EQ(pThumb[3], 255);
	EXPECT_NEAR(pThumb[0], 200 * Art.m_Shade, 2.0f);
	EXPECT_NEAR(pThumb[1], 100 * Art.m_Shade, 2.0f);
	EXPECT_NEAR(pThumb[2], 50 * Art.m_Shade, 2.0f);
	// 缩略图与拖尾交界保持不透明，拖尾末端透明；左上角圆角被裁掉。
	EXPECT_EQ(PixelAt(Output, 114, 36)[3], 255);
	EXPECT_GE(PixelAt(Output, 115, 36)[3], 200);
	EXPECT_LT(PixelAt(Output, 166, 36)[3], 20);
	EXPECT_EQ(PixelAt(Output, 0, 0)[3], 0);
	Output.Free();
}

TEST(QmMapCoverArt, ComposeCropsToFillThumbnailAspect)
{
	// 高窄源图进缩略图区域：按宽度放大后上下居中裁切，顶行应来自源图中间部分而不是第 0 行。
	CImageInfo Source;
	Source.m_Width = 2;
	Source.m_Height = 8;
	Source.m_Format = CImageInfo::FORMAT_RGB;
	Source.m_pData = static_cast<uint8_t *>(malloc(2 * 8 * 3));
	for(int Y = 0; Y < 8; ++Y)
	{
		for(int X = 0; X < 2; ++X)
		{
			uint8_t *pPixel = Source.m_pData + (Y * 2 + X) * 3;
			pPixel[0] = static_cast<uint8_t>(Y * 30);
			pPixel[1] = 0;
			pPixel[2] = 0;
		}
	}
	CImageInfo Output;
	const QmMapCover::SArt Art = DefaultArt();
	ASSERT_TRUE(QmMapCover::Compose(Source, 64, 32, Art, Output));
	Source.Free();
	// 源行 0 是 0、源行 7 是 210。按宽度放大到 44 宽后纵向只剩约 1.45 个源行，居中裁切
	// 落在源行 2.8~4.2 之间：顶行既不能是第 0 行，也不能滑到底部几行。
	const uint8_t *pTopLeft = PixelAt(Output, 2, 0);
	EXPECT_GT(pTopLeft[0], 2 * 30 * Art.m_Shade);
	EXPECT_LT(pTopLeft[0], 5 * 30 * Art.m_Shade);
	Output.Free();
}

TEST(QmMapCoverArt, ComposeRejectsInvalidInput)
{
	CImageInfo Empty;
	EXPECT_FALSE(QmMapCover::Compose(Empty, 16, 8, DefaultArt(), Empty));
	CImageInfo Source = SolidImage(2, 2, 10, 20, 30);
	CImageInfo Output;
	EXPECT_FALSE(QmMapCover::Compose(Source, 0, 8, DefaultArt(), Output));
	EXPECT_FALSE(QmMapCover::Compose(Source, 1, 8, DefaultArt(), Output));
	EXPECT_FALSE(QmMapCover::Compose(Source, 16, 0, DefaultArt(), Output));
	EXPECT_FALSE(QmMapCover::Compose(Source, QmMapCover::MAX_WIDTH + 1, 8, DefaultArt(), Output));
	Source.m_Format = CImageInfo::FORMAT_UNDEFINED;
	EXPECT_FALSE(QmMapCover::Compose(Source, 16, 8, DefaultArt(), Output));
	Source.m_Format = CImageInfo::FORMAT_RGB;
	Source.Free();
	CImageInfo Solid = SolidImage(2, 2, 10, 20, 30);
	ASSERT_TRUE(QmMapCover::Compose(Solid, 16, 8, DefaultArt(), Output));
	Solid.Free();
	Output.Free();
}

TEST(QmMapCoverArt, DecodeAndComposeDecodesPngBytes)
{
	CImageInfo Source = SolidImage(4, 2, 90, 180, 30);
	CByteBufferWriter Writer;
	ASSERT_TRUE(CImageLoader::SavePng(Writer, Source));
	Source.Free();

	CImageInfo Output;
	ASSERT_TRUE(QmMapCover::DecodeAndCompose(Writer.Data(), Writer.Size(), "test_cover", 64, 32, DefaultArt(), Output));
	ASSERT_EQ(Output.m_Width, 64u);
	ASSERT_EQ(Output.m_Height, 32u);
	EXPECT_EQ(PixelAt(Output, 4, 16)[3], 255);
	EXPECT_LT(PixelAt(Output, 63, 16)[3], 20);
	Output.Free();

	const uint8_t aNotPng[16] = {};
	EXPECT_FALSE(QmMapCover::DecodeAndCompose(aNotPng, sizeof(aNotPng), "test_cover", 64, 32, DefaultArt(), Output));
	EXPECT_FALSE(QmMapCover::DecodeAndCompose(nullptr, 0, "test_cover", 64, 32, DefaultArt(), Output));
}
