#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(AssetsEditorLayout, StrongWeakUsesThreeHorizontalSquareSlots)
{
	int GridX = 0;
	int GridY = 0;
	CMenus::GetStrongWeakEditorGridSize(GridX, GridY);

	EXPECT_EQ(GridX, 3);
	EXPECT_EQ(GridY, 1);

	const auto vSlots = CMenus::BuildStrongWeakEditorSlots("default");

	ASSERT_EQ(vSlots.size(), static_cast<size_t>(GridX));

	for(size_t Index = 0; Index < vSlots.size(); ++Index)
	{
		const auto &Slot = vSlots[Index];
		EXPECT_GE(Slot.m_DstX, 0);
		EXPECT_LT(Slot.m_DstX, GridX);
		EXPECT_GE(Slot.m_DstY, 0);
		EXPECT_LT(Slot.m_DstY, GridY);
		EXPECT_EQ(Slot.m_DstX, static_cast<int>(Index));
		EXPECT_EQ(Slot.m_DstY, 0);
		EXPECT_EQ(Slot.m_DstW, 1);
		EXPECT_EQ(Slot.m_DstH, 1);
		EXPECT_LE(Slot.m_DstX + Slot.m_DstW, GridX);
		EXPECT_LE(Slot.m_DstY + Slot.m_DstH, GridY);
		EXPECT_EQ(Slot.m_SrcX, static_cast<int>(Index));
		EXPECT_EQ(Slot.m_SrcY, 0);
		EXPECT_EQ(Slot.m_SrcW, 1);
		EXPECT_EQ(Slot.m_SrcH, 1);
		EXPECT_STREQ(Slot.m_aSourceAsset, "default");
	}
}

TEST(AssetsEditorLayout, SkinUsesExpectedAtlasSlices)
{
	const auto vSlots = CMenus::BuildSkinEditorSlots("default");
	ASSERT_EQ(vSlots.size(), 14u);

	const auto FindSlot = [&](const char *pFamilyKey) -> const CMenus::SAssetsEditorPartSlot * {
		for(const auto &Slot : vSlots)
		{
			if(str_comp(Slot.m_aFamilyKey, pFamilyKey) == 0)
				return &Slot;
		}
		return nullptr;
	};

	const auto *pBody = FindSlot("skin:body");
	const auto *pFeet = FindSlot("skin:feet");
	const auto *pRightStrip0 = FindSlot("skin:right_strip_0");
	const auto *pRightStrip1 = FindSlot("skin:right_strip_1");
	const auto *pRightStrip2 = FindSlot("skin:right_strip_2");
	const auto *pRightStrip3 = FindSlot("skin:right_strip_3");
	const auto *pBottomStrip0 = FindSlot("skin:bottom_strip_0");
	const auto *pBottomStrip7 = FindSlot("skin:bottom_strip_7");

	ASSERT_NE(pBody, nullptr);
	ASSERT_NE(pFeet, nullptr);
	ASSERT_NE(pRightStrip0, nullptr);
	ASSERT_NE(pRightStrip1, nullptr);
	ASSERT_NE(pRightStrip2, nullptr);
	ASSERT_NE(pRightStrip3, nullptr);
	ASSERT_NE(pBottomStrip0, nullptr);
	ASSERT_NE(pBottomStrip7, nullptr);

	EXPECT_EQ(pBody->m_DstW, 96);
	EXPECT_EQ(pBody->m_DstH, 96);
	EXPECT_EQ(pFeet->m_DstW, 96);
	EXPECT_EQ(pFeet->m_DstH, 96);

	EXPECT_EQ(pRightStrip0->m_DstX, 192);
	EXPECT_EQ(pRightStrip0->m_DstY, 0);
	EXPECT_EQ(pRightStrip0->m_DstW, 32);
	EXPECT_EQ(pRightStrip0->m_DstH, 32);

	EXPECT_EQ(pRightStrip1->m_DstX, 224);
	EXPECT_EQ(pRightStrip1->m_DstY, 0);
	EXPECT_EQ(pRightStrip1->m_DstW, 32);
	EXPECT_EQ(pRightStrip1->m_DstH, 32);

	EXPECT_EQ(pRightStrip2->m_DstX, 192);
	EXPECT_EQ(pRightStrip2->m_DstY, 32);
	EXPECT_EQ(pRightStrip2->m_DstW, 64);
	EXPECT_EQ(pRightStrip2->m_DstH, 32);

	EXPECT_EQ(pRightStrip3->m_DstX, 192);
	EXPECT_EQ(pRightStrip3->m_DstY, 64);
	EXPECT_EQ(pRightStrip3->m_DstW, 64);
	EXPECT_EQ(pRightStrip3->m_DstH, 32);

	EXPECT_EQ(pBottomStrip0->m_DstX, 0);
	EXPECT_EQ(pBottomStrip0->m_DstY, 96);
	EXPECT_EQ(pBottomStrip0->m_DstW, 32);
	EXPECT_EQ(pBottomStrip0->m_DstH, 32);

	EXPECT_EQ(pBottomStrip7->m_DstX, 224);
	EXPECT_EQ(pBottomStrip7->m_DstY, 96);
	EXPECT_EQ(pBottomStrip7->m_DstW, 32);
	EXPECT_EQ(pBottomStrip7->m_DstH, 32);

	const unsigned int DefaultColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)).Pack(true);
	for(const auto &Slot : vSlots)
	{
		EXPECT_STREQ(Slot.m_aSourceAsset, "default");
		EXPECT_EQ(Slot.m_SrcX, Slot.m_DstX);
		EXPECT_EQ(Slot.m_SrcY, Slot.m_DstY);
		EXPECT_EQ(Slot.m_SrcW, Slot.m_DstW);
		EXPECT_EQ(Slot.m_SrcH, Slot.m_DstH);
		EXPECT_EQ(Slot.m_Color, DefaultColor);
	}
}

TEST(AssetsEditorBlendMode, MultiplyMatchesLegacyChannelTint)
{
	const ColorRGBA Base(0.25f, 0.70f, 0.50f, 0.80f);
	const ColorRGBA Tint(0.80f, 0.40f, 0.30f, 0.60f);

	const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_MULTIPLY);

	EXPECT_NEAR(Result.r, 0.22f, 0.0001f);
	EXPECT_NEAR(Result.g, 0.448f, 0.0001f);
	EXPECT_NEAR(Result.b, 0.29f, 0.0001f);
	EXPECT_NEAR(Result.a, 0.80f, 0.0001f);
}

TEST(AssetsEditorBlendMode, StandardModesMatchReferenceColors)
{
	const ColorRGBA Base(0.25f, 0.50f, 0.75f, 0.80f);
	const ColorRGBA Tint(0.80f, 0.20f, 0.40f, 1.0f);
	// 固定参考值覆盖逐通道运算和保留明度的颜色分量运算。
	const struct
	{
		int m_Mode;
		ColorRGBA m_Expected;
	} aCases[] = {
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_MULTIPLY, ColorRGBA(0.20f, 0.10f, 0.30f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_NORMAL, ColorRGBA(0.80f, 0.20f, 0.40f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN, ColorRGBA(0.85f, 0.60f, 0.85f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_OVERLAY, ColorRGBA(0.40f, 0.20f, 0.70f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_DARKEN, ColorRGBA(0.25f, 0.20f, 0.40f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_BURN, ColorRGBA(0.0625f, 0.0f, 0.375f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_LIGHTEN, ColorRGBA(0.80f, 0.50f, 0.75f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_DODGE, ColorRGBA(1.0f, 0.625f, 1.0f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_SOFT_LIGHT, ColorRGBA(0.40f, 0.35f, 0.7125f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_HARD_LIGHT, ColorRGBA(0.70f, 0.20f, 0.60f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_DIFFERENCE, ColorRGBA(0.55f, 0.30f, 0.35f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_EXCLUSION, ColorRGBA(0.65f, 0.50f, 0.55f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_HUE, ColorRGBA(0.78416667f, 0.28416667f, 0.45083333f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_SATURATION, ColorRGBA(0.2095f, 0.5095f, 0.8095f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR, ColorRGBA(0.8505f, 0.2505f, 0.4505f)},
		{CMenus::ASSETS_EDITOR_COLOR_BLEND_LUMINOSITY, ColorRGBA(0.1995f, 0.4495f, 0.6995f)},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Mode);
		const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, Tint, Case.m_Mode);
		EXPECT_NEAR(Result.r, Case.m_Expected.r, 0.0001f);
		EXPECT_NEAR(Result.g, Case.m_Expected.g, 0.0001f);
		EXPECT_NEAR(Result.b, Case.m_Expected.b, 0.0001f);
		EXPECT_FLOAT_EQ(Result.a, Base.a);
	}
}

TEST(AssetsEditorBlendMode, InvalidModesFallBackToMultiply)
{
	const ColorRGBA Base(0.15f, 0.25f, 0.35f, 0.90f);
	const ColorRGBA Tint(0.60f, 0.50f, 0.40f, 0.75f);

	const ColorRGBA Multiply = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_MULTIPLY);
	const ColorRGBA Invalid = CMenus::AssetsEditorBlendColor(Base, Tint, -1);
	const ColorRGBA TooLarge = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_COUNT + 3);

	EXPECT_NEAR(Invalid.r, Multiply.r, 0.0001f);
	EXPECT_NEAR(Invalid.g, Multiply.g, 0.0001f);
	EXPECT_NEAR(Invalid.b, Multiply.b, 0.0001f);
	EXPECT_NEAR(Invalid.a, Multiply.a, 0.0001f);

	EXPECT_NEAR(TooLarge.r, Multiply.r, 0.0001f);
	EXPECT_NEAR(TooLarge.g, Multiply.g, 0.0001f);
	EXPECT_NEAR(TooLarge.b, Multiply.b, 0.0001f);
	EXPECT_NEAR(TooLarge.a, Multiply.a, 0.0001f);
}

TEST(AssetsEditorCompose, PureColorOverrideStillNeedsProcessing)
{
	CMenus::SAssetsEditorPartSlot Slot;
	str_copy(Slot.m_aSourceAsset, "default", sizeof(Slot.m_aSourceAsset));
	Slot.m_SrcX = 0;
	Slot.m_SrcY = 0;
	Slot.m_SrcW = 1;
	Slot.m_SrcH = 1;
	Slot.m_DstX = 0;
	Slot.m_DstY = 0;
	Slot.m_DstW = 1;
	Slot.m_DstH = 1;
	Slot.m_Color = color_cast<ColorHSLA>(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f)).Pack(true);

	EXPECT_TRUE(CMenus::AssetsEditorSlotNeedsProcessing(Slot, "default"));
}

TEST(AssetsEditorCompose, ColorOverrideSkipsFullyTransparentPixels)
{
	CImageInfo Image;
	Image.m_Width = 2;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	uint8_t aPixels[] = {
		255,
		255,
		255,
		255,
		12,
		34,
		56,
		0,
	};
	Image.m_pData = aPixels;

	CMenus::AssetsEditorApplyColorOverrideToImageRect(
		Image,
		0,
		0,
		2,
		1,
		ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f),
		CMenus::ASSETS_EDITOR_COLOR_BLEND_NORMAL);

	EXPECT_EQ(aPixels[0], 255);
	EXPECT_EQ(aPixels[1], 0);
	EXPECT_EQ(aPixels[2], 0);
	EXPECT_EQ(aPixels[3], 255);

	EXPECT_EQ(aPixels[4], 12);
	EXPECT_EQ(aPixels[5], 34);
	EXPECT_EQ(aPixels[6], 56);
	EXPECT_EQ(aPixels[7], 0);
}

TEST(AssetsEditorBlendMode, ScreenAndOverlayPreserveWhiteHighlights)
{
	const ColorRGBA Base(1.0f, 1.0f, 1.0f, 1.0f);
	const ColorRGBA Tint(1.0f, 0.0f, 0.0f, 1.0f);
	for(const int Mode : {CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN, CMenus::ASSETS_EDITOR_COLOR_BLEND_OVERLAY})
	{
		const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, Tint, Mode);
		EXPECT_FLOAT_EQ(Result.r, 1.0f);
		EXPECT_FLOAT_EQ(Result.g, 1.0f);
		EXPECT_FLOAT_EQ(Result.b, 1.0f);
	}
}

TEST(AssetsEditorBlendMode, StrengthInterpolatesWithoutChangingTransparency)
{
	const ColorRGBA Base(0.40f, 0.60f, 0.20f, 0.75f);
	const ColorRGBA Tint(1.0f, 0.20f, 0.70f, 1.0f);
	for(int Mode = 0; Mode < CMenus::ASSETS_EDITOR_COLOR_BLEND_COUNT; ++Mode)
	{
		const ColorRGBA Full = CMenus::AssetsEditorBlendColor(Base, Tint, Mode);
		for(const float Strength : {0.0f, 0.25f, 1.0f})
		{
			ColorRGBA PartialTint = Tint;
			PartialTint.a = Strength;
			const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, PartialTint, Mode);
			EXPECT_NEAR(Result.r, Base.r + (Full.r - Base.r) * Strength, 0.0001f);
			EXPECT_NEAR(Result.g, Base.g + (Full.g - Base.g) * Strength, 0.0001f);
			EXPECT_NEAR(Result.b, Base.b + (Full.b - Base.b) * Strength, 0.0001f);
			EXPECT_FLOAT_EQ(Result.a, Base.a);
		}
	}
}

TEST(AssetsEditorBlendMode, DodgeBurnAndSoftLightHandleEndpoints)
{
	const ColorRGBA Black(0.0f, 0.0f, 0.0f, 1.0f);
	const ColorRGBA White(1.0f, 1.0f, 1.0f, 1.0f);
	EXPECT_FLOAT_EQ(CMenus::AssetsEditorBlendColor(Black, White, CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_DODGE).r, 0.0f);
	EXPECT_FLOAT_EQ(CMenus::AssetsEditorBlendColor(White, Black, CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_BURN).r, 1.0f);
	EXPECT_FLOAT_EQ(CMenus::AssetsEditorBlendColor(White, Black, CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_DODGE).r, 1.0f);
	EXPECT_FLOAT_EQ(CMenus::AssetsEditorBlendColor(Black, White, CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR_BURN).r, 0.0f);
	const ColorRGBA Result = CMenus::AssetsEditorBlendColor(ColorRGBA(0.10f, 0.25f, 0.64f), White, CMenus::ASSETS_EDITOR_COLOR_BLEND_SOFT_LIGHT);
	EXPECT_NEAR(Result.r, 0.296f, 0.0001f);
	EXPECT_NEAR(Result.g, 0.50f, 0.0001f);
	EXPECT_NEAR(Result.b, 0.80f, 0.0001f);
}

TEST(AssetsEditorBlendMode, ColorModesPreserveLuminosityAfterGamutClipping)
{
	for(const float Gray : {0.0f, 0.05f, 0.5f, 0.95f, 1.0f})
	{
		const ColorRGBA Base(Gray, Gray, Gray, 0.4f);
		for(const ColorRGBA Tint : {ColorRGBA(1.0f, 0.0f, 0.0f), ColorRGBA(0.0f, 0.0f, 1.0f), ColorRGBA(0.5f, 0.5f, 0.5f)})
		{
			for(const int Mode : {CMenus::ASSETS_EDITOR_COLOR_BLEND_HUE, CMenus::ASSETS_EDITOR_COLOR_BLEND_SATURATION, CMenus::ASSETS_EDITOR_COLOR_BLEND_COLOR})
			{
				const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, Tint, Mode);
				EXPECT_NEAR(Result.r * 0.3f + Result.g * 0.59f + Result.b * 0.11f, Gray, 0.0001f);
				for(const float Channel : {Result.r, Result.g, Result.b})
				{
					EXPECT_GE(Channel, 0.0f);
					EXPECT_LE(Channel, 1.0f);
				}
				EXPECT_FLOAT_EQ(Result.a, Base.a);
			}
		}
	}
}

TEST(AssetsEditorCompose, WhiteScreenTintChangesOnlyTheSelectedImageRect)
{
	CImageInfo Image;
	Image.m_Width = 3;
	Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	uint8_t aPixels[] = {10, 20, 30, 255, 40, 50, 60, 128, 70, 80, 90, 0};
	Image.m_pData = aPixels;
	CMenus::AssetsEditorApplyColorOverrideToImageRect(Image, 1, 0, 2, 1, ColorRGBA(1.0f, 1.0f, 1.0f), CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN);
	const uint8_t aExpected[] = {10, 20, 30, 255, 255, 255, 255, 128, 70, 80, 90, 0};
	for(size_t Index = 0; Index < sizeof(aPixels); ++Index)
		EXPECT_EQ(aPixels[Index], aExpected[Index]);
}

TEST(AssetsEditorCompose, PartsHaveIndependentModesAndStrengths)
{
	auto vSlots = CMenus::BuildStrongWeakEditorSlots("default");
	ASSERT_GE(vSlots.size(), 2u);
	const CMenus::SAssetsEditorPartSlot Original = vSlots[1];
	vSlots[0].m_ColorBlendMode = CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN;
	vSlots[0].m_BlendStrength = 25;
	EXPECT_TRUE(CMenus::AssetsEditorSlotNeedsProcessing(vSlots[0], "default"));
	EXPECT_FALSE(CMenus::AssetsEditorSlotNeedsProcessing(vSlots[1], "default"));
	EXPECT_EQ(vSlots[1].m_ColorBlendMode, Original.m_ColorBlendMode);
	EXPECT_EQ(vSlots[1].m_BlendStrength, Original.m_BlendStrength);
	EXPECT_EQ(vSlots[1].m_Color, Original.m_Color);
	EXPECT_FLOAT_EQ(CMenus::AssetsEditorSlotTint(vSlots[0]).a, 0.25f);
	vSlots[0].m_BlendStrength = 0;
	EXPECT_FALSE(CMenus::AssetsEditorSlotNeedsProcessing(vSlots[0], "default"));
	// 即使关闭调色，替换来源仍需参与合成。
	str_copy(vSlots[0].m_aSourceAsset, "donor");
	EXPECT_TRUE(CMenus::AssetsEditorSlotNeedsProcessing(vSlots[0], "default"));
}
