#include "test.h"

#include <base/system.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>

#include <gtest/gtest.h>

namespace
{
	CImageInfo MakeRgbaImage(size_t Width, size_t Height)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(Width * Height * 4));
		mem_zero(Image.m_pData, Width * Height * 4);
		return Image;
	}

	CImageInfo MakeRImage(size_t Width, size_t Height)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_R;
		Image.m_pData = static_cast<uint8_t *>(malloc(Width * Height));
		mem_zero(Image.m_pData, Width * Height);
		return Image;
	}

	void SetPixel(CImageInfo &Image, size_t X, size_t Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A)
	{
		const size_t Offset = (Y * Image.m_Width + X) * 4;
		Image.m_pData[Offset + 0] = R;
		Image.m_pData[Offset + 1] = G;
		Image.m_pData[Offset + 2] = B;
		Image.m_pData[Offset + 3] = A;
	}

	bool PixelEquals(const CImageInfo &Image, size_t X, size_t Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A)
	{
		const size_t Offset = (Y * Image.m_Width + X) * 4;
		return Image.m_pData[Offset + 0] == R && Image.m_pData[Offset + 1] == G &&
		       Image.m_pData[Offset + 2] == B && Image.m_pData[Offset + 3] == A;
	}

	// hud spriteset 的网格：16x16（datasrc/content.py 的 SpriteSet("hud", image_hud, 16, 16)）。
	constexpr int QM_TEST_HUD_GRID = 16;
	constexpr size_t QM_TEST_HUD_SIZE = 512;
} // namespace

TEST(QmBlankAssetFallback, ResolveSpritePixelRectConvertsGridCellsToPixels)
{
	size_t x = 0, y = 0, w = 0, h = 0;
	const bool Resolved = ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, 4, 6, 2, 2, x, y, w, h);

	ASSERT_TRUE(Resolved);
	EXPECT_EQ(x, 128u);
	EXPECT_EQ(y, 192u);
	EXPECT_EQ(w, 64u);
	EXPECT_EQ(h, 64u);
}

TEST(QmBlankAssetFallback, ResolveSpritePixelRectRejectsAtlasNotDivisibleByGrid)
{
	size_t x = 0, y = 0, w = 0, h = 0;

	// 宽度 500 无法被 16 整除，格宽只能取整，换算出错区域。
	EXPECT_FALSE(ResolveSpritePixelRect(500, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, 10, 6, 2, 2, x, y, w, h));
}

TEST(QmBlankAssetFallback, ResolveSpritePixelRectRejectsInvalidGridAndSprite)
{
	size_t x = 0, y = 0, w = 0, h = 0;

	EXPECT_FALSE(ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, 0, QM_TEST_HUD_GRID, 0, 0, 1, 1, x, y, w, h));
	EXPECT_FALSE(ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, 0, 0, 0, 1, x, y, w, h));
	EXPECT_FALSE(ResolveSpritePixelRect(0, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, 0, 0, 1, 1, x, y, w, h));
}

TEST(QmBlankAssetFallback, ResolveSpritePixelRectFlagsSpriteBeyondAtlas)
{
	size_t x = 0, y = 0, w = 0, h = 0;
	bool OutOfBounds = false;

	// (15,6) 起 2 格宽会超出 16 格图集：按「图集比默认布局小」处理，而不是坏包。
	EXPECT_FALSE(ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, 15, 6, 2, 2, x, y, w, h, &OutOfBounds));
	EXPECT_TRUE(OutOfBounds);

	// 参数非法不应被误判成越界。
	OutOfBounds = false;
	EXPECT_FALSE(ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, 0, QM_TEST_HUD_GRID, 0, 0, 1, 1, x, y, w, h, &OutOfBounds));
	EXPECT_FALSE(OutOfBounds);
}

TEST(QmBlankAssetFallback, ResolveSpritePixelRectKeepsHudStatusIconsOnDistinctCells)
{
	// practice / lock / team0 都在 hud 图集第 6 行，回退必须取各自的格，不能共用区域。
	const struct
	{
		int m_SpriteX;
		int m_SpriteY;
		size_t m_ExpectedX;
	} aCases[] = {
		{4, 6, 128},
		{10, 6, 320},
		{12, 6, 384},
	};

	for(const auto &Case : aCases)
	{
		size_t x = 0, y = 0, w = 0, h = 0;
		ASSERT_TRUE(ResolveSpritePixelRect(QM_TEST_HUD_SIZE, QM_TEST_HUD_SIZE, QM_TEST_HUD_GRID, QM_TEST_HUD_GRID, Case.m_SpriteX, Case.m_SpriteY, 2, 2, x, y, w, h));
		EXPECT_EQ(x, Case.m_ExpectedX);
		EXPECT_EQ(y, 192u);
		EXPECT_EQ(w, 64u);
		EXPECT_EQ(h, 64u);
	}
}

TEST(QmBlankAssetFallback, IsImageRectFullyTransparentTreatsZeroAlphaAsBlank)
{
	CImageInfo Image = MakeRgbaImage(4, 4);
	// RGB 有内容但 alpha 为 0：仍然算空白，皮肤作者隐藏图标就是这么留空的。
	SetPixel(Image, 1, 1, 255, 0, 0, 0);

	EXPECT_TRUE(IsImageRectFullyTransparent(Image, 0, 0, 4, 4));
	EXPECT_TRUE(IsImageRectFullyTransparent(Image, 1, 1, 1, 1));
	Image.Free();
}

TEST(QmBlankAssetFallback, IsImageRectFullyTransparentRejectsAnyVisiblePixel)
{
	CImageInfo Image = MakeRgbaImage(4, 4);
	SetPixel(Image, 2, 3, 0, 0, 0, 1);

	EXPECT_FALSE(IsImageRectFullyTransparent(Image, 0, 0, 4, 4));
	EXPECT_TRUE(IsImageRectFullyTransparent(Image, 0, 0, 2, 3));
	Image.Free();
}

TEST(QmBlankAssetFallback, IsImageRectFullyTransparentRejectsRectOutsideImage)
{
	CImageInfo Image = MakeRgbaImage(4, 4);

	EXPECT_FALSE(IsImageRectFullyTransparent(Image, 3, 0, 2, 1));
	EXPECT_FALSE(IsImageRectFullyTransparent(Image, 0, 0, 0, 4));
	Image.Free();
}

TEST(QmBlankAssetFallback, CopyFallbackOverBlankRectFillsBlankSpriteOnly)
{
	CImageInfo Custom = MakeRgbaImage(4, 4);
	CImageInfo Default = MakeRgbaImage(4, 4);
	// 默认图的两个 2x2 格里都有内容；自定义图只画了左上格，右下格是空白。
	SetPixel(Default, 0, 0, 10, 10, 10, 255);
	SetPixel(Default, 2, 2, 20, 20, 20, 255);
	SetPixel(Custom, 0, 0, 10, 10, 10, 255);

	EXPECT_TRUE(CopyFallbackOverBlankRect(Custom, Default, 2, 2, 2, 2, 2, 2, 2, 2));
	EXPECT_TRUE(PixelEquals(Custom, 2, 2, 20, 20, 20, 255));
	EXPECT_TRUE(PixelEquals(Custom, 0, 0, 10, 10, 10, 255));

	// 已经画过的格不再被覆盖。
	SetPixel(Default, 0, 0, 99, 99, 99, 255);
	EXPECT_FALSE(CopyFallbackOverBlankRect(Custom, Default, 0, 0, 2, 2, 0, 0, 2, 2));
	EXPECT_TRUE(PixelEquals(Custom, 0, 0, 10, 10, 10, 255));

	Custom.Free();
	Default.Free();
}

TEST(QmBlankAssetFallback, CopyFallbackOverBlankRectScalesFallbackBetweenCanvasSizes)
{
	// 自定义画布与默认画布分辨率不同时，整图回退仍按比例取最近邻样本，而不是直接放弃。
	CImageInfo Custom = MakeRgbaImage(4, 4);
	CImageInfo Default = MakeRgbaImage(8, 8);
	SetPixel(Default, 0, 0, 20, 20, 20, 255);
	SetPixel(Default, 6, 6, 30, 30, 30, 255);

	ASSERT_TRUE(CopyFallbackOverBlankRect(Custom, Default, 0, 0, 4, 4, 0, 0, 8, 8));
	EXPECT_TRUE(PixelEquals(Custom, 0, 0, 20, 20, 20, 255));
	// 目标 (3,3) 采样默认 (6,6)：(3 * 8) / 4 = 6。
	EXPECT_TRUE(PixelEquals(Custom, 3, 3, 30, 30, 30, 255));

	Custom.Free();
	Default.Free();
}

TEST(QmBlankAssetFallback, CopyFallbackOverBlankRectMapsEachCellAcrossCanvasSizes)
{
	// strong_weak 这类 3x1 图集：两张画布分辨率不同，回退仍按格位一一对应。
	CImageInfo Custom = MakeRgbaImage(6, 3);
	CImageInfo Default = MakeRgbaImage(12, 6);
	SetPixel(Default, 4, 0, 40, 40, 40, 255); // 默认图第 1 格（x 4..7）
	SetPixel(Custom, 0, 0, 10, 10, 10, 255); // 自定义图只有第 0 格有内容

	// 自定义第 1 格 (2,0)-(4,3) 对应默认第 1 格 (4,0)-(8,6)。
	ASSERT_TRUE(CopyFallbackOverBlankRect(Custom, Default, 2, 0, 2, 3, 4, 0, 4, 6));
	EXPECT_TRUE(PixelEquals(Custom, 2, 0, 40, 40, 40, 255));
	EXPECT_TRUE(PixelEquals(Custom, 0, 0, 10, 10, 10, 255));
	EXPECT_TRUE(PixelEquals(Custom, 4, 0, 0, 0, 0, 0));

	Custom.Free();
	Default.Free();
}

TEST(QmBlankAssetFallback, ClearImageToTransparentBlanksEveryChannel)
{
	// 空白材质按内置图的尺寸/格式造全透明图：清空后整张都应判定为空白。
	CImageInfo Image = MakeRgbaImage(3, 2);
	SetPixel(Image, 1, 1, 200, 100, 50, 255);

	ClearImageToTransparent(Image);

	EXPECT_TRUE(IsImageRectFullyTransparent(Image, 0, 0, 3, 2));
	EXPECT_TRUE(PixelEquals(Image, 1, 1, 0, 0, 0, 0));
	Image.Free();
}

TEST(QmBlankAssetFallback, CopyFallbackOverBlankRectRequiresMatchingFormat)
{
	CImageInfo Custom = MakeRgbaImage(4, 4);
	CImageInfo Default = MakeRImage(4, 4);

	// 通道布局不同（RGBA 对 R）时不能按像素直接拷贝。
	EXPECT_FALSE(CopyFallbackOverBlankRect(Custom, Default, 0, 0, 4, 4, 0, 0, 4, 4));
	EXPECT_TRUE(PixelEquals(Custom, 0, 0, 0, 0, 0, 0));

	Custom.Free();
	Default.Free();
}

namespace
{
	// 32x32 图集按 2x2 网格切成 16x16 格，便于用少量像素验证提取区域。
	constexpr size_t QM_TEST_ATLAS_SIZE = 32;
	constexpr int QM_TEST_ATLAS_GRID = 2;

	CDataSpriteset MakeAtlasSet()
	{
		CDataSpriteset Set{};
		Set.m_Gridx = QM_TEST_ATLAS_GRID;
		Set.m_Gridy = QM_TEST_ATLAS_GRID;
		return Set;
	}

	CDataSprite MakeSprite(CDataSpriteset *pSet, int X, int Y, int W, int H)
	{
		CDataSprite Sprite{};
		Sprite.m_pSet = pSet;
		Sprite.m_X = X;
		Sprite.m_Y = Y;
		Sprite.m_W = W;
		Sprite.m_H = H;
		Sprite.m_pName = "test_sprite";
		return Sprite;
	}
} // namespace

TEST(QmBlankAssetFallback, ExtractSpriteImageCopiesTheGridCellRegionFromTheAtlas)
{
	CImageInfo Atlas = MakeRgbaImage(QM_TEST_ATLAS_SIZE, QM_TEST_ATLAS_SIZE);
	// 第二列首行的像素用可区分的颜色标记，提取后应出现在结果的原点。
	SetPixel(Atlas, 16, 0, 11, 22, 33, 44);
	SetPixel(Atlas, 31, 15, 55, 66, 77, 88);

	CDataSpriteset Set = MakeAtlasSet();
	const CDataSprite Sprite = MakeSprite(&Set, 1, 0, 1, 1);

	CImageInfo Result;
	ASSERT_TRUE(ExtractSpriteImage(Atlas, &Sprite, Result));

	EXPECT_EQ(Result.m_Width, 16u);
	EXPECT_EQ(Result.m_Height, 16u);
	EXPECT_EQ(Result.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_TRUE(PixelEquals(Result, 0, 0, 11, 22, 33, 44));
	// 图集内该格的右下角像素也要一起搬过来，而不是只复制原点。
	EXPECT_TRUE(PixelEquals(Result, 15, 15, 55, 66, 77, 88));

	Atlas.Free();
	Result.Free();
}

TEST(QmBlankAssetFallback, ExtractSpriteImageRejectsSpriteBeyondTheAtlasWithoutTouchingResult)
{
	CImageInfo Atlas = MakeRgbaImage(QM_TEST_ATLAS_SIZE, QM_TEST_ATLAS_SIZE);

	// 自定义图集比默认布局小时，越界精灵是预期情形：此处必须失败，
	// 由调用方按 qm_blank_asset_fallback 决定回退默认资源还是保持不可见。
	CDataSpriteset Set = MakeAtlasSet();
	const CDataSprite Sprite = MakeSprite(&Set, 1, 0, 2, 1);

	CImageInfo Result;
	EXPECT_FALSE(ExtractSpriteImage(Atlas, &Sprite, Result));
	EXPECT_EQ(Result.m_pData, nullptr);

	Atlas.Free();
}

TEST(QmBlankAssetFallback, ExtractSpriteImageRejectsMissingSpriteMissingDataAndBadGrid)
{
	CImageInfo Atlas = MakeRgbaImage(QM_TEST_ATLAS_SIZE, QM_TEST_ATLAS_SIZE);
	CDataSpriteset Set = MakeAtlasSet();
	const CDataSprite Sprite = MakeSprite(&Set, 0, 0, 1, 1);

	CImageInfo Result;
	EXPECT_FALSE(ExtractSpriteImage(Atlas, nullptr, Result));

	// 图集没有像素数据时不能进入复制路径。
	CImageInfo NoData;
	NoData.m_Width = QM_TEST_ATLAS_SIZE;
	NoData.m_Height = QM_TEST_ATLAS_SIZE;
	NoData.m_Format = CImageInfo::FORMAT_RGBA;
	EXPECT_FALSE(ExtractSpriteImage(NoData, &Sprite, Result));

	// 网格为 0 的 sprite 无法换算像素矩形。
	CDataSpriteset BadSet{};
	const CDataSprite BadSprite = MakeSprite(&BadSet, 0, 0, 1, 1);
	EXPECT_FALSE(ExtractSpriteImage(Atlas, &BadSprite, Result));

	// 越界之外的无数据情形同样不应写入结果。
	EXPECT_EQ(Result.m_pData, nullptr);

	Atlas.Free();
}
