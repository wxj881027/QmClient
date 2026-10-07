// 图标策略直接验证生产接口；图集资源清单由独立合同验证。
#include <game/client/qm_icon_label.h>
#include <game/client/qm_icon_label_runs.h>
#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

TEST(QmIconAtlas, RuntimeIconNamesAreStable)
{
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::STAR), "star");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::BOOKMARK), "bookmark");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SEARCH), "magnifying-glass");
	// 名字必须与 Phosphor 官方一致（datasrc/qm_icons/phosphor.codepoints）。
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CLOSE), "x");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE), "eye");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE_OFF), "eye-slash");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CHEVRON_DOWN), "chevron-down");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::PLUS), "plus");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::TRASH), "trash");
	// 原自制 satellite 图标已替换为官方 Phosphor 图标。
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::ARROWS_IN), "arrows-in");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::ARROWS_OUT), "arrows-out");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SWAP), "swap");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SPEAKER_SLASH), "speaker-slash");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CHECK), "check");
}

TEST(QmIconPolicy, InvalidWeightsUseRegularFallback)
{
	EXPECT_EQ(NormalizeQmIconWeight(-1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(0), 0);
	EXPECT_EQ(NormalizeQmIconWeight(1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(2), 2);
	EXPECT_EQ(NormalizeQmIconWeight(3), 3);
	EXPECT_EQ(NormalizeQmIconWeight(4), 4);
	EXPECT_EQ(NormalizeQmIconWeight(5), 1);
	EXPECT_EQ(NormalizeQmIconWeight(6), 1);
}

TEST(QmIconPolicy, ReloadCooldownExpiresAndConfigurationChangesBypassIt)
{
	// 位图 alpha 图集已移除：图集不可用即字体兜底，仅剩 MSDF 单一路径。
	EXPECT_TRUE(QmIconAtlasNeedsReload(false, 1, 1));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, 0, 1));
	EXPECT_FALSE(QmIconAtlasNeedsReload(true, 1, 1));
	EXPECT_TRUE(QmIconAtlasRetryCooldownActive(99, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(100, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(101, 100));
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(100, 100, true, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, false, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, true, 0, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, false, 1, true));
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 0, true, 0, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, true, 0, true));
}

TEST(QmIconPolicy, MsdfBatchSizesUseBoundedBuckets)
{
	EXPECT_EQ(QmIconMsdfRunBucket(1), 0u);
	EXPECT_EQ(QmIconMsdfRunBucket(2), 1u);
	EXPECT_EQ(QmIconMsdfRunBucket(3), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(4), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(8), 3u);
	EXPECT_EQ(QmIconMsdfRunBucket(16), 4u);
	EXPECT_EQ(QmIconMsdfRunBucket(32), 5u);
	EXPECT_EQ(QmIconMsdfRunBucket(64), 6u);
	EXPECT_EQ(QmIconMsdfRunBucket(65), 7u);
}

TEST(QmIconPolicy, OnlyValidHealthyTexturesCanCommit)
{
	EXPECT_TRUE(QmIconTextureCanCommit(true, false));
	EXPECT_FALSE(QmIconTextureCanCommit(false, false));
	EXPECT_FALSE(QmIconTextureCanCommit(true, true));
	EXPECT_FALSE(QmIconTextureCanCommit(false, true));
}

TEST(QmIconPolicy, PixelScaleRejectsInvalidDimensions)
{
	EXPECT_FLOAT_EQ(QmIconPixelScale(0, 100.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, -1.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(200, 100.0f), 2.0f);
}

TEST(QmIconAtlas, UiTintPreservesSemanticAlpha)
{
	const ColorRGBA SemanticColor(0.20f, 0.60f, 0.80f, 0.35f);
	const ColorRGBA White = QmUiIconColor(SemanticColor, 1);
	const ColorRGBA Black = QmUiIconColor(SemanticColor, 2);
	const unsigned int CustomColor = ColorHSLA(0.28f, 0.70f, 0.45f, 1.0f).Pack(false);
	const ColorRGBA Custom = QmUiIconColor(SemanticColor, 3, CustomColor);
	const ColorRGBA ExpectedCustom = color_cast<ColorRGBA>(ColorHSLA(CustomColor));
	const ColorRGBA Rainbow = QmUiIconColor(SemanticColor, 4, 0, 2.5f);
	const ColorRGBA ExpectedRainbow = color_cast<ColorRGBA>(ColorHSLA(0.5f, 0.75f, 0.6f, SemanticColor.a));
	EXPECT_FLOAT_EQ(White.r, 1.0f);
	EXPECT_FLOAT_EQ(White.g, 1.0f);
	EXPECT_FLOAT_EQ(White.b, 1.0f);
	EXPECT_FLOAT_EQ(White.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Black.r, 0.0f);
	EXPECT_FLOAT_EQ(Black.g, 0.0f);
	EXPECT_FLOAT_EQ(Black.b, 0.0f);
	EXPECT_FLOAT_EQ(Black.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Custom.r, ExpectedCustom.r);
	EXPECT_FLOAT_EQ(Custom.g, ExpectedCustom.g);
	EXPECT_FLOAT_EQ(Custom.b, ExpectedCustom.b);
	EXPECT_FLOAT_EQ(Custom.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Rainbow.r, ExpectedRainbow.r);
	EXPECT_FLOAT_EQ(Rainbow.g, ExpectedRainbow.g);
	EXPECT_FLOAT_EQ(Rainbow.b, ExpectedRainbow.b);
	EXPECT_FLOAT_EQ(Rainbow.a, SemanticColor.a);
}

TEST(QmIconGeometry, IconDrawsPreserveGlyphAspectRatio)
{
	// manifest 存的是每个字形自己的紧贴框（宽高比各异），绘制必须等比适配调用方方框，
	// 否则每个图标都会按自己的宽高比被拉伸——历史症状就是「图标不是 1:1」。
	const CUIRect Square{10.0f, 20.0f, 32.0f, 32.0f};

	// 宽字形：宽度填满、高度按比例收窄并垂直居中
	const CUIRect Wide = QmIconAspectFittedRect(Square, 60, 44);
	EXPECT_FLOAT_EQ(Wide.w, 32.0f);
	EXPECT_NEAR(Wide.h, 32.0f * 44.0f / 60.0f, 0.001f);
	EXPECT_NEAR(Wide.y, Square.y + (Square.h - Wide.h) * 0.5f, 0.001f);
	EXPECT_NEAR(Wide.x, Square.x, 0.001f);

	// 高字形：高度填满、宽度按比例收窄并水平居中
	const CUIRect Tall = QmIconAspectFittedRect(Square, 44, 60);
	EXPECT_FLOAT_EQ(Tall.h, 32.0f);
	EXPECT_NEAR(Tall.w, 32.0f * 44.0f / 60.0f, 0.001f);
	EXPECT_NEAR(Tall.x, Square.x + (Square.w - Tall.w) * 0.5f, 0.001f);

	// 等比方框原样返回；适配结果永不超出原方框
	const CUIRect Same = QmIconAspectFittedRect(Square, 48, 48);
	EXPECT_FLOAT_EQ(Same.w, Square.w);
	EXPECT_FLOAT_EQ(Same.h, Square.h);
	for(const CUIRect &Fitted : {Wide, Tall, Same})
	{
		EXPECT_GE(Fitted.x, Square.x - 0.001f);
		EXPECT_GE(Fitted.y, Square.y - 0.001f);
		EXPECT_LE(Fitted.x + Fitted.w, Square.x + Square.w + 0.001f);
		EXPECT_LE(Fitted.y + Fitted.h, Square.y + Square.h + 0.001f);
	}

	// 核心断言：绘制宽高比 == 字形宽高比
	EXPECT_NEAR(Wide.w / Wide.h, 60.0f / 44.0f, 0.001f);
	EXPECT_NEAR(Tall.w / Tall.h, 44.0f / 60.0f, 0.001f);

	// 非方形调用方方框同样等比适配（例如媒体岛眨眼用的压缩方框）
	const CUIRect Squashed{0.0f, 0.0f, 88.0f, 44.0f};
	const CUIRect FittedInSquashed = QmIconAspectFittedRect(Squashed, 60, 44);
	EXPECT_NEAR(FittedInSquashed.w / FittedInSquashed.h, 60.0f / 44.0f, 0.001f);
}

TEST(QmIconGeometry, MorphFrameBlendSelectsAdjacentFrames)
{
	// 端点必须精确落在首/末帧上：否则动画结束交回静态图标时会有形状跳变。
	constexpr int Frames = 8;
	const SQmIconMorphFrameBlend Start = QmIconMorphFrameBlend(0.0f, Frames);
	EXPECT_EQ(Start.m_Index0, 0);
	EXPECT_FLOAT_EQ(Start.m_Alpha0, 1.0f);
	EXPECT_FLOAT_EQ(Start.m_Alpha1, 0.0f);

	const SQmIconMorphFrameBlend End = QmIconMorphFrameBlend(1.0f, Frames);
	EXPECT_EQ(End.m_Index0, Frames - 1);
	EXPECT_EQ(End.m_Index1, Frames - 1);
	EXPECT_FLOAT_EQ(End.m_Alpha0, 1.0f);
	EXPECT_FLOAT_EQ(End.m_Alpha1, 0.0f);

	// 弹簧会 over/undershoot，越界进度必须被夹紧。
	const SQmIconMorphFrameBlend Under = QmIconMorphFrameBlend(-0.35f, Frames);
	EXPECT_EQ(Under.m_Index0, 0);
	EXPECT_FLOAT_EQ(Under.m_Alpha1, 0.0f);
	const SQmIconMorphFrameBlend Over = QmIconMorphFrameBlend(1.45f, Frames);
	EXPECT_EQ(Over.m_Index0, Frames - 1);
	EXPECT_FLOAT_EQ(Over.m_Alpha0, 1.0f);

	// 单帧退化：不得产生越界索引。
	const SQmIconMorphFrameBlend Single = QmIconMorphFrameBlend(0.5f, 1);
	EXPECT_EQ(Single.m_Index0, 0);
	EXPECT_EQ(Single.m_Index1, 0);
	EXPECT_FLOAT_EQ(Single.m_Alpha1, 0.0f);

	for(int Step = 0; Step <= 40; ++Step)
	{
		const SQmIconMorphFrameBlend Blend = QmIconMorphFrameBlend(Step / 40.0f, Frames);
		EXPECT_GE(Blend.m_Index0, 0);
		EXPECT_LT(Blend.m_Index1, Frames);
		EXPECT_GE(Blend.m_Index1, Blend.m_Index0);
		EXPECT_NEAR(Blend.m_Alpha0 + Blend.m_Alpha1, 1.0f, 1e-4f);
	}
}

TEST(QmIconGeometry, InvalidBitmapOrEmptyBoxPreservesInputRectangle)
{
	const CUIRect Box{2.0f, 3.0f, 20.0f, 10.0f};
	for(const CUIRect &Fitted : {QmIconAspectFittedRect(Box, 0, 48), QmIconAspectFittedRect(Box, 48, -1),
		    QmIconAspectFittedRect(CUIRect{2.0f, 3.0f, 0.0f, 10.0f}, 48, 48)})
	{
		EXPECT_FLOAT_EQ(Fitted.x, 2.0f);
		EXPECT_FLOAT_EQ(Fitted.y, 3.0f);
		EXPECT_FLOAT_EQ(Fitted.h, 10.0f);
	}
	EXPECT_FLOAT_EQ(QmIconAspectFittedRect(Box, 0, 48).w, 20.0f);
	EXPECT_FLOAT_EQ(QmIconAspectFittedRect(Box, 48, -1).w, 20.0f);
	EXPECT_FLOAT_EQ(QmIconAspectFittedRect(CUIRect{2.0f, 3.0f, 0.0f, 10.0f}, 48, 48).w, 0.0f);
}

TEST(QmIconGeometry, MissingMorphFramesUseOpaqueFirstFrameFallback)
{
	for(int FrameCount : {-1, 0, 1})
	{
		SCOPED_TRACE(FrameCount);
		const auto Blend = QmIconMorphFrameBlend(0.75f, FrameCount);
		EXPECT_EQ(Blend.m_Index0, 0);
		EXPECT_EQ(Blend.m_Index1, 0);
		EXPECT_FLOAT_EQ(Blend.m_Alpha0, 1.0f);
		EXPECT_FLOAT_EQ(Blend.m_Alpha1, 0.0f);
	}
}

TEST(QmIconLabel, EveryBundledGlyphResolvesWithoutTreatingBodyTextAsIcons)
{
	for(const char *pGlyph : FontIcons::FONT_ICON_ALL)
	{
		SCOPED_TRACE(pGlyph);
		EXPECT_NE(QmIconForLabel(EFontPreset::ICON_FONT, pGlyph), EQmIcon::COUNT);
		EXPECT_NE(QmIconForLabel(EFontPreset::ICON_FONT_BOLD, pGlyph), EQmIcon::COUNT);
		EXPECT_EQ(QmIconForLabel(EFontPreset::DEFAULT_FONT, pGlyph), EQmIcon::COUNT);
	}
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, FontIcons::FONT_ICON_MICROPHONE), EQmIcon::MICROPHONE);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, "A"), EQmIcon::COUNT);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, ""), EQmIcon::COUNT);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, nullptr), EQmIcon::COUNT);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, FontIcons::FONT_ICON_STAR, 0), EQmIcon::COUNT);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, FontIcons::FONT_ICON_STAR, 2), EQmIcon::COUNT);
	EXPECT_EQ(QmIconForLabel(EFontPreset::ICON_FONT, FontIcons::FONT_ICON_STAR, 3), EQmIcon::STAR);
}

TEST(QmIconLabel, LoadingButtonResolvesBothGlyphsAndRejectsMixedText)
{
	const std::string Loading = std::string(FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT) + FontIcons::FONT_ICON_ELLIPSIS;
	const auto Icons = QmIconLabelGlyphs(EFontPreset::ICON_FONT, Loading.c_str());
	ASSERT_EQ(Icons.m_Count, 2);
	EXPECT_EQ(Icons.m_aIcons[0], EQmIcon::ARROW_ROTATE_RIGHT);
	EXPECT_EQ(Icons.m_aIcons[1], EQmIcon::ELLIPSIS);
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::DEFAULT_FONT, Loading.c_str()).m_Count, 0);
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::ICON_FONT, (Loading + "text").c_str()).m_Count, 0);
	std::string TooMany;
	for(int Index = 0; Index < 9; ++Index)
		TooMany += FontIcons::FONT_ICON_STAR;
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::ICON_FONT, TooMany.c_str()).m_Count, 0);
}

TEST(QmIconLabel, AlignmentFitsNarrowRectAndClampsInvalidSize)
{
	const CUIRect Rect{10.0f, 20.0f, 50.0f, 30.0f};
	const auto Center = QmIconLabelRect(Rect, 12.0f, TEXTALIGN_MC);
	EXPECT_FLOAT_EQ(Center.x, 29.0f);
	EXPECT_FLOAT_EQ(Center.y, 29.0f);
	EXPECT_FLOAT_EQ(Center.w, 12.0f);
	EXPECT_FLOAT_EQ(Center.h, 12.0f);
	const auto Narrow = QmIconLabelRect(CUIRect{10, 20, 5, 30}, 12, TEXTALIGN_BR);
	EXPECT_FLOAT_EQ(Narrow.x, 10.0f);
	EXPECT_FLOAT_EQ(Narrow.y, 45.0f);
	EXPECT_FLOAT_EQ(Narrow.w, 5.0f);
	EXPECT_FLOAT_EQ(QmIconLabelRect(Rect, -1, TEXTALIGN_TL).w, 0.0f);
}

TEST(QmIconLabel, BoundedGlyphDoesNotRequireTerminatorAndRejectsTruncation)
{
	const char aGlyph[] = {'\xEE', '\x91', '\xAA'};
	EXPECT_EQ(CQmIconManager::IconFromGlyph(aGlyph, 3), EQmIcon::STAR);
	const char aOneByte[] = {'\xEE'};
	EXPECT_EQ(CQmIconManager::IconFromGlyph(aOneByte, 1), EQmIcon::COUNT);
	const char aTwoBytes[] = {'\xEE', '\x91'};
	EXPECT_EQ(CQmIconManager::IconFromGlyph(aTwoBytes, 2), EQmIcon::COUNT);
	EXPECT_EQ(CQmIconManager::IconFromGlyph(aOneByte, 0), EQmIcon::COUNT);
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::ICON_FONT, aGlyph, 3).m_Count, 1);
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::ICON_FONT, aOneByte, 1).m_Count, 0);
	EXPECT_EQ(QmIconLabelGlyphs(EFontPreset::ICON_FONT, aTwoBytes, 2).m_Count, 0);
}

TEST(QmIconLabelRuns, IconAndNumberUseSeparateFontRuns)
{
	const std::string Label = std::string(FontIcons::FONT_ICON_LIST_UL) + "12";
	std::vector<SQmIconLabelRun> vRuns;
	ASSERT_TRUE(QmVisitIconLabelRuns(Label.c_str(), [&](const auto &Run) { vRuns.push_back(Run); }));
	ASSERT_EQ(vRuns.size(), 2u);
	EXPECT_TRUE(vRuns[0].m_IsIcon);
	EXPECT_EQ(vRuns[0].m_Icon, EQmIcon::LIST_UL);
	EXPECT_FALSE(vRuns[1].m_IsIcon);
	EXPECT_EQ(std::string(vRuns[1].m_pText, vRuns[1].m_Length), "12");
}

TEST(QmIconLabelRuns, LegacyMappingsPreserveInputAndSelectBundledFallback)
{
	for(const auto &Case : {std::pair<const char *, EQmIcon>{"\xEF\x83\x89", EQmIcon::LIST_UL},
		    {"\xEF\x85\x8E", EQmIcon::GEAR}, {"\xEF\x95\x90", EQmIcon::GEAR}})
	{
		const std::string Label = std::string(Case.first) + "3";
		const std::string Original = Label;
		std::vector<SQmIconLabelRun> vRuns;
		ASSERT_TRUE(QmVisitIconLabelRuns(Label.c_str(), [&](const auto &Run) { vRuns.push_back(Run); }));
		ASSERT_EQ(vRuns.size(), 2u);
		EXPECT_EQ(vRuns[0].m_Icon, Case.second);
		ASSERT_NE(vRuns[0].m_pFallback, nullptr);
		EXPECT_EQ(CQmIconManager::IconFromGlyph(vRuns[0].m_pFallback), Case.second);
		EXPECT_EQ(Label, Original);
		EXPECT_EQ(std::string(vRuns[1].m_pText, vRuns[1].m_Length), "3");
	}
}

TEST(QmIconLabelRuns, ChineseAndDigitsRemainOneContinuousBodyRun)
{
	const std::string Label = std::string("菜单12") + FontIcons::FONT_ICON_GEAR;
	std::vector<SQmIconLabelRun> vRuns;
	ASSERT_TRUE(QmVisitIconLabelRuns(Label.c_str(), [&](const auto &Run) { vRuns.push_back(Run); }));
	ASSERT_EQ(vRuns.size(), 2u);
	EXPECT_FALSE(vRuns[0].m_IsIcon);
	EXPECT_EQ(std::string(vRuns[0].m_pText, vRuns[0].m_Length), "菜单12");
	EXPECT_EQ(vRuns[1].m_Icon, EQmIcon::GEAR);
}

TEST(QmIconLabelRuns, InvalidUtf8StopsBeforeInvalidBytesAndFollowingText)
{
	const char aLabel[] = {'A', '\xEE', '\xFF', 'B', '\0'};
	std::vector<SQmIconLabelRun> vRuns;
	EXPECT_FALSE(QmVisitIconLabelRuns(aLabel, [&](const auto &Run) { vRuns.push_back(Run); }));
	ASSERT_EQ(vRuns.size(), 1u);
	EXPECT_EQ(std::string(vRuns[0].m_pText, vRuns[0].m_Length), "A");
}

TEST(QmIconLabelRuns, AdjacentIconsRemainIndividualRunsAndUnknownPrivateGlyphIsPreserved)
{
	const std::string Label = std::string(FontIcons::FONT_ICON_STAR) + FontIcons::FONT_ICON_GEAR + "\xEE\x80\x80";
	std::vector<SQmIconLabelRun> vRuns;
	ASSERT_TRUE(QmVisitIconLabelRuns(Label.c_str(), [&](const auto &Run) { vRuns.push_back(Run); }));
	ASSERT_EQ(vRuns.size(), 3u);
	EXPECT_EQ(vRuns[0].m_Icon, EQmIcon::STAR);
	EXPECT_EQ(vRuns[1].m_Icon, EQmIcon::GEAR);
	EXPECT_TRUE(vRuns[2].m_IsIcon);
	EXPECT_EQ(vRuns[2].m_Icon, EQmIcon::COUNT);
	EXPECT_EQ(vRuns[2].m_pFallback, nullptr);
	EXPECT_EQ(std::string(vRuns[2].m_pText, vRuns[2].m_Length), "\xEE\x80\x80");
}

TEST(QmIconLabelRuns, EmptyAndNullLabelsDoNotVisitRuns)
{
	int Visits = 0;
	EXPECT_TRUE(QmVisitIconLabelRuns(nullptr, [&](const auto &) { ++Visits; }));
	EXPECT_TRUE(QmVisitIconLabelRuns("", [&](const auto &) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

namespace
{
	class CQmConfiguredIconColorTest : public ::testing::Test
	{
		int m_Preset = g_Config.m_QmUiIconColor;
		int m_CustomEnabled = g_Config.m_QmUiIconCustomColorEnabled;
		unsigned m_CustomColor = g_Config.m_QmUiIconCustomColor;

	protected:
		void TearDown() override
		{
			g_Config.m_QmUiIconColor = m_Preset;
			g_Config.m_QmUiIconCustomColorEnabled = m_CustomEnabled;
			g_Config.m_QmUiIconCustomColor = m_CustomColor;
		}
	};
}

TEST_F(CQmConfiguredIconColorTest, CustomSwitchOverridesEveryPresetAndKeepsStateAlpha)
{
	g_Config.m_QmUiIconCustomColorEnabled = 1;
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.37f, 0.8f, 0.45f).Pack(false);
	const ColorRGBA Expected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiIconCustomColor));
	for(int Preset : {1, 2, 4})
	{
		g_Config.m_QmUiIconColor = Preset;
		for(float Alpha : {0.0f, 0.17f, 0.65f, 1.0f})
		{
			SCOPED_TRACE(::testing::Message() << "preset=" << Preset << " alpha=" << Alpha);
			const ColorRGBA Actual = ConfiguredQmUiIconColor(ColorRGBA(1.0f, 0.85f, 0.3f, Alpha));
			EXPECT_FLOAT_EQ(Actual.r, Expected.r);
			EXPECT_FLOAT_EQ(Actual.g, Expected.g);
			EXPECT_FLOAT_EQ(Actual.b, Expected.b);
			EXPECT_FLOAT_EQ(Actual.a, Alpha);
		}
	}
}

TEST_F(CQmConfiguredIconColorTest, TurningCustomOffImmediatelyRestoresPreset)
{
	g_Config.m_QmUiIconColor = 2;
	g_Config.m_QmUiIconCustomColorEnabled = 1;
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.37f, 0.8f, 0.45f).Pack(false);
	const ColorRGBA StateColor(0.4f, 0.6f, 0.8f, 0.35f);
	EXPECT_NE(ConfiguredQmUiIconColor(StateColor).g, 0.0f);
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	const ColorRGBA Actual = ConfiguredQmUiIconColor(StateColor);
	EXPECT_FLOAT_EQ(Actual.r, 0.0f);
	EXPECT_FLOAT_EQ(Actual.g, 0.0f);
	EXPECT_FLOAT_EQ(Actual.b, 0.0f);
	EXPECT_FLOAT_EQ(Actual.a, StateColor.a);
}

TEST_F(CQmConfiguredIconColorTest, LegacyCustomPresetStillUsesSavedColor)
{
	g_Config.m_QmUiIconColor = 3;
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.37f, 0.8f, 0.45f).Pack(false);
	const ColorRGBA Expected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiIconCustomColor));
	const ColorRGBA Actual = ConfiguredQmUiIconColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
	EXPECT_FLOAT_EQ(Actual.r, Expected.r);
	EXPECT_FLOAT_EQ(Actual.g, Expected.g);
	EXPECT_FLOAT_EQ(Actual.b, Expected.b);
	EXPECT_FLOAT_EQ(Actual.a, 0.6f);
}

TEST_F(CQmConfiguredIconColorTest, SemanticOverlayScopeRestoresNestedDrawingPolicy)
{
	g_Config.m_QmUiIconColor = 1;
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	const ColorRGBA Overlay(1.0f, 0.0f, 0.0f, 0.8f);
	EXPECT_FLOAT_EQ(ConfiguredQmUiIconColor(Overlay).g, 1.0f);
	{
		const CQmIconSemanticColorScope Outer;
		EXPECT_EQ(ConfiguredQmUiIconColor(Overlay), Overlay);
		{
			const CQmIconSemanticColorScope Inner;
			EXPECT_EQ(ConfiguredQmUiIconColor(Overlay), Overlay);
		}
		EXPECT_EQ(ConfiguredQmUiIconColor(Overlay), Overlay);
	}
	EXPECT_FLOAT_EQ(ConfiguredQmUiIconColor(Overlay).g, 1.0f);
}

TEST(QmIconPolicy, ContrastOutlinePreservesPrimaryAndStateAlpha)
{
	struct SCase
	{
		ColorRGBA m_Primary;
		float m_ExpectedChannel;
		float m_ReferenceLuminance;
	};
	// 参考亮度使用标准 sRGB 原色系数，与生产转换实现独立。
	const SCase aCases[] = {
		{ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0.0f},
		{ColorRGBA(1.0f, 1.0f, 1.0f, 0.17f), 0.0f, 1.0f},
		{ColorRGBA(1.0f, 0.0f, 0.0f, 0.65f), 0.0f, 0.2126f},
		{ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f), 0.0f, 0.7152f},
		{ColorRGBA(0.0f, 0.0f, 1.0f, 0.35f), 1.0f, 0.0722f},
	};
	for(const SCase &Case : aCases)
	{
		SCOPED_TRACE(::testing::Message() << "RGB=" << Case.m_Primary.r << "," << Case.m_Primary.g << "," << Case.m_Primary.b);
		const ColorRGBA Original = Case.m_Primary;
		const ColorRGBA Outline = QmUiIconContrastColor(Case.m_Primary);
		EXPECT_EQ(Case.m_Primary, Original);
		EXPECT_FLOAT_EQ(Outline.r, Case.m_ExpectedChannel);
		EXPECT_FLOAT_EQ(Outline.g, Case.m_ExpectedChannel);
		EXPECT_FLOAT_EQ(Outline.b, Case.m_ExpectedChannel);
		EXPECT_FLOAT_EQ(Outline.a, Case.m_Primary.a);
		const float Contrast = Case.m_ExpectedChannel == 1.0f ? 1.05f / (Case.m_ReferenceLuminance + 0.05f) : (Case.m_ReferenceLuminance + 0.05f) / 0.05f;
		EXPECT_GE(Contrast, 4.5f);
	}
}

TEST(QmIconPolicy, CustomDarkAndLightColorsGetOppositeOutline)
{
	const ColorRGBA Dark = QmUiIconColor(ColorRGBA(1, 1, 1, 0.42f), 3, ColorHSLA(0.6f, 0.8f, 0.08f).Pack(false));
	const ColorRGBA Light = QmUiIconColor(ColorRGBA(0, 0, 0, 0.73f), 3, ColorHSLA(0.12f, 0.8f, 0.92f).Pack(false));
	const ColorRGBA DarkOutline = QmUiIconContrastColor(Dark);
	const ColorRGBA LightOutline = QmUiIconContrastColor(Light);
	EXPECT_FLOAT_EQ(DarkOutline.r, 1.0f);
	EXPECT_FLOAT_EQ(DarkOutline.a, Dark.a);
	EXPECT_FLOAT_EQ(LightOutline.r, 0.0f);
	EXPECT_FLOAT_EQ(LightOutline.a, Light.a);
}

TEST(QmIconPolicy, RainbowRemainsSmoothAfterLongUptimeAndAcrossCycleBoundary)
{
	const ColorRGBA Input(1, 1, 1, 0.42f);
	const double LongTime = 1000000000.0 + 1.25;
	const ColorRGBA Start = QmUiIconColor(Input, 4, 0, LongTime);
	const ColorRGBA Next = QmUiIconColor(Input, 4, 0, LongTime + 1.0 / 144.0);
	EXPECT_NE(Start, Next);
	EXPECT_LT(std::abs(Start.r - Next.r) + std::abs(Start.g - Next.g) + std::abs(Start.b - Next.b), 0.02f);
	EXPECT_EQ(Start, QmUiIconColor(Input, 4, 0, 1.25));
	const ColorRGBA Before = QmUiIconColor(Input, 4, 0, 4.999);
	const ColorRGBA After = QmUiIconColor(Input, 4, 0, 5.001);
	EXPECT_LT(std::abs(Before.r - After.r) + std::abs(Before.g - After.g) + std::abs(Before.b - After.b), 0.01f);
	EXPECT_FLOAT_EQ(Next.a, Input.a);
}

TEST_F(CQmConfiguredIconColorTest, RainbowUsesOneFrameSampleAndStableOutline)
{
	const double OriginalTime = CQmIconFrameColorClock::Time();
	g_Config.m_QmUiIconColor = 4;
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	CQmIconFrameColorClock::BeginFrame(1000000001.25);
	const ColorRGBA First = ConfiguredQmUiIconColor(ColorRGBA(1, 1, 1, 0.65f));
	EXPECT_EQ(First, ConfiguredQmUiIconColor(ColorRGBA(0, 0, 0, 0.65f)));
	for(int Frame = 0; Frame < 720; ++Frame)
	{
		CQmIconFrameColorClock::BeginFrame(Frame / 144.0);
		const ColorRGBA Outline = ConfiguredQmUiIconContrastColor(ConfiguredQmUiIconColor(ColorRGBA(1, 1, 1, 0.65f)));
		EXPECT_EQ(Outline, ColorRGBA(0, 0, 0, 0.65f));
	}
	CQmIconFrameColorClock::BeginFrame(OriginalTime);
}
