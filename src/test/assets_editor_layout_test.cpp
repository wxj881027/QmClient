#include <gtest/gtest.h>

#include <game/client/components/menus.h>

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

TEST(AssetsEditorBlendMode, SupportsScreenOverlayAndNormalModes)
{
	const ColorRGBA Base(0.25f, 0.70f, 0.50f, 0.80f);
	const ColorRGBA Tint(0.80f, 0.40f, 0.30f, 0.60f);

	const ColorRGBA Normal = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_NORMAL);
	const ColorRGBA Screen = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN);
	const ColorRGBA Overlay = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_OVERLAY);

	EXPECT_GT(Normal.r, Base.r);
	EXPECT_LT(Normal.g, Base.g);
	EXPECT_LT(Normal.b, Base.b);
	EXPECT_NEAR(Normal.a, Base.a, 0.0001f);

	EXPECT_GT(Screen.r, Normal.r);
	EXPECT_GT(Screen.g, Normal.g);
	EXPECT_GT(Screen.b, Normal.b);
	EXPECT_NEAR(Screen.a, Base.a, 0.0001f);

	EXPECT_NE(Overlay.r, Normal.r);
	EXPECT_NE(Overlay.g, Screen.g);
	EXPECT_NE(Overlay.b, Base.b);
	EXPECT_NEAR(Overlay.a, Base.a, 0.0001f);
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
		255, 255, 255, 255,
		12, 34, 56, 0,
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

TEST(AssetsEditorBlendMode, WhiteHighlightsBecomeClearlyTinted)
{
	const ColorRGBA Base(0.96f, 0.94f, 0.90f, 1.0f);
	const ColorRGBA Tint(1.0f, 0.0f, 0.0f, 1.0f);

	for(int Mode = CMenus::ASSETS_EDITOR_COLOR_BLEND_MULTIPLY; Mode < CMenus::ASSETS_EDITOR_COLOR_BLEND_COUNT; ++Mode)
	{
		const ColorRGBA Result = CMenus::AssetsEditorBlendColor(Base, Tint, Mode);
		EXPECT_GT(Result.r, 0.75f) << Mode;
		EXPECT_LT(Result.g, 0.30f) << Mode;
		EXPECT_LT(Result.b, 0.30f) << Mode;
		EXPECT_NEAR(Result.a, 1.0f, 0.0001f) << Mode;
	}
}

TEST(AssetsEditorBlendMode, DistinctModesProduceDistinctVisibleOutput)
{
	const ColorRGBA Base(0.40f, 0.60f, 0.20f, 0.75f);
	const ColorRGBA Tint(1.0f, 0.20f, 0.70f, 1.0f);

	const ColorRGBA Multiply = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_MULTIPLY);
	const ColorRGBA Normal = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_NORMAL);
	const ColorRGBA Screen = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_SCREEN);
	const ColorRGBA Overlay = CMenus::AssetsEditorBlendColor(Base, Tint, CMenus::ASSETS_EDITOR_COLOR_BLEND_OVERLAY);

	EXPECT_NE(Multiply.r, Normal.r);
	EXPECT_NE(Multiply.g, Screen.g);
	EXPECT_NE(Screen.b, Overlay.b);
	EXPECT_NE(Normal.r, Overlay.r);
	EXPECT_GT(Screen.r, Normal.r);
	EXPECT_NEAR(Multiply.a, Base.a, 0.0001f);
	EXPECT_NEAR(Normal.a, Base.a, 0.0001f);
	EXPECT_NEAR(Screen.a, Base.a, 0.0001f);
	EXPECT_NEAR(Overlay.a, Base.a, 0.0001f);
}
