// 图标策略直接验证生产接口。
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_font_render.h>
#include <game/client/qm_icon_label.h>
#include <game/client/qm_icon_label_runs.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

TEST(QmIconRegistry, RuntimeIconNamesAreStable)
{
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::STAR), "star");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::BOOKMARK), "bookmark");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::SEARCH), "magnifying-glass");
	// 名字必须与 Phosphor 官方一致（datasrc/qm_icons/phosphor.codepoints）。
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::CLOSE), "x");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::EYE), "eye");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::EYE_OFF), "eye-slash");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::CHEVRON_DOWN), "chevron-down");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::PLUS), "plus");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::TRASH), "trash");
	// 原自制 satellite 图标已替换为官方 Phosphor 图标。
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::ARROWS_IN), "arrows-in");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::ARROWS_OUT), "arrows-out");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::SWAP), "swap");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::SPEAKER_SLASH), "speaker-slash");
	EXPECT_STREQ(CQmIconRegistry::IconName(EQmIcon::CHECK), "check");
}

TEST(QmIconPolicy, InvalidWeightsUseBold)
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

TEST(QmIconRegistry, UiTintPreservesSemanticAlpha)
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
	EXPECT_EQ(CQmIconRegistry::IconFromGlyph(aGlyph, 3), EQmIcon::STAR);
	const char aOneByte[] = {'\xEE'};
	EXPECT_EQ(CQmIconRegistry::IconFromGlyph(aOneByte, 1), EQmIcon::COUNT);
	const char aTwoBytes[] = {'\xEE', '\x91'};
	EXPECT_EQ(CQmIconRegistry::IconFromGlyph(aTwoBytes, 2), EQmIcon::COUNT);
	EXPECT_EQ(CQmIconRegistry::IconFromGlyph(aOneByte, 0), EQmIcon::COUNT);
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
		EXPECT_EQ(CQmIconRegistry::IconFromGlyph(vRuns[0].m_pFallback), Case.second);
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
		unsigned m_FriendColor = g_Config.m_QmUiFriendIconColor;
		unsigned m_FavoriteColor = g_Config.m_QmUiFavoriteIconColor;

	protected:
		void TearDown() override
		{
			g_Config.m_QmUiIconColor = m_Preset;
			g_Config.m_QmUiIconCustomColorEnabled = m_CustomEnabled;
			g_Config.m_QmUiIconCustomColor = m_CustomColor;
			g_Config.m_QmUiFriendIconColor = m_FriendColor;
			g_Config.m_QmUiFavoriteIconColor = m_FavoriteColor;
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

TEST_F(CQmConfiguredIconColorTest, FriendAndFavoriteColorsRemainIndependentAcrossGlobalPresets)
{
	g_Config.m_QmUiFriendIconColor = ColorHSLA(0.0f, 1.0f, 0.5f).Pack(false);
	g_Config.m_QmUiFavoriteIconColor = ColorHSLA(1.0f / 6.0f, 1.0f, 0.5f).Pack(false);
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.6f, 1.0f, 0.5f).Pack(false);
	for(int Preset : {1, 2, 3, 4})
	{
		g_Config.m_QmUiIconColor = Preset;
		for(int CustomEnabled : {0, 1})
		{
			g_Config.m_QmUiIconCustomColorEnabled = CustomEnabled;
			for(float Alpha : {0.0f, 0.17f, 0.65f, 1.0f})
			{
				SCOPED_TRACE(::testing::Message() << "preset=" << Preset << " custom=" << CustomEnabled << " alpha=" << Alpha);
				const ColorRGBA StateColor(0.2f, 0.4f, 0.8f, Alpha);
				EXPECT_EQ(ConfiguredQmUiIconColor(StateColor, EQmIcon::HEART), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiFriendIconColor)).WithAlpha(Alpha));
				EXPECT_EQ(ConfiguredQmUiIconColor(StateColor, EQmIcon::STAR), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiFavoriteIconColor)).WithAlpha(Alpha));
				EXPECT_EQ(ConfiguredQmUiIconColor(StateColor, EQmIcon::GEAR), ConfiguredQmUiIconColor(StateColor));
			}
		}
	}
}

TEST_F(CQmConfiguredIconColorTest, ChangingOneSemanticColorImmediatelyUpdatesOnlyItsIcon)
{
	g_Config.m_QmUiFriendIconColor = 0x00D1AB;
	g_Config.m_QmUiFavoriteIconColor = 0x21FFA6;
	const ColorRGBA Input(1, 1, 1, 0.35f);
	const ColorRGBA OriginalFriend = ConfiguredQmUiIconColor(Input, EQmIcon::HEART);
	const ColorRGBA OriginalFavorite = ConfiguredQmUiIconColor(Input, EQmIcon::STAR);
	g_Config.m_QmUiFriendIconColor = ColorHSLA(0.6f, 0.8f, 0.45f).Pack(false);
	EXPECT_NE(ConfiguredQmUiIconColor(Input, EQmIcon::HEART), OriginalFriend);
	EXPECT_EQ(ConfiguredQmUiIconColor(Input, EQmIcon::STAR), OriginalFavorite);
	g_Config.m_QmUiFriendIconColor = 0x00D1AB;
	EXPECT_EQ(ConfiguredQmUiIconColor(Input, EQmIcon::HEART), OriginalFriend);
}

TEST_F(CQmConfiguredIconColorTest, SemanticDrawingScopePreventsRetintAndRestoresOrdinaryDrawing)
{
	g_Config.m_QmUiIconColor = 1;
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	g_Config.m_QmUiFriendIconColor = 0x00D1AB;
	const ColorRGBA Input(1, 1, 1, 0.35f);
	const ColorRGBA Friend = ConfiguredQmUiIconColor(Input, EQmIcon::HEART);
	{
		const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(EQmIcon::HEART));
		EXPECT_EQ(ConfiguredQmUiIconColor(Friend), Friend);
		const CQmIconSemanticColorScope OrdinaryNestedScope(false);
		EXPECT_EQ(ConfiguredQmUiIconColor(Friend, EQmIcon::HEART), Friend);
	}
	EXPECT_EQ(ConfiguredQmUiIconColor(Friend, EQmIcon::GEAR), Input);
	EXPECT_EQ(ConfiguredQmUiIconColor(Friend, EQmIcon::HEART), Friend);
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
		EXPECT_EQ(Outline, ColorRGBA(0, 0, 0, 0.65f * 0.35f));
	}
	CQmIconFrameColorClock::BeginFrame(OriginalTime);
}

TEST(QmIconSurfaceProtection, ReadableWhiteAndBlackNeedNoExtraContour)
{
	EXPECT_FLOAT_EQ(QmUiIconSurfaceProtection(ColorRGBA(1, 1, 1, 1), ColorRGBA(0.1f, 0.1f, 0.1f, 1)).a, 0.0f);
	EXPECT_FLOAT_EQ(QmUiIconSurfaceProtection(ColorRGBA(0, 0, 0, 1), ColorRGBA(0.8f, 0.8f, 0.8f, 1)).a, 0.0f);
}

TEST(QmIconSurfaceProtection, MatchingSurfaceUsesWeakProtectionAndPreservesStateAlpha)
{
	const ColorRGBA Surface(0.8f, 0.8f, 0.8f, 1);
	const ColorRGBA Protection = QmUiIconSurfaceProtection(Surface, Surface);
	EXPECT_EQ(Protection, ColorRGBA(0, 0, 0, 0.35f));
	const ColorRGBA Disabled = QmUiIconSurfaceProtection(Surface.WithAlpha(0.25f), Surface);
	EXPECT_FLOAT_EQ(Disabled.a, Protection.a * 0.25f);
	EXPECT_FLOAT_EQ(QmUiIconSurfaceProtection(Surface.WithAlpha(0), Surface).a, 0);
}

TEST(QmIconSurfaceProtection, RainbowKeepsOutlineRgbAndChangesProtectionContinuously)
{
	for(const ColorRGBA Surface : {ColorRGBA(0.1f, 0.1f, 0.1f, 1), ColorRGBA(0.8f, 0.8f, 0.8f, 1)})
	{
		ColorRGBA Previous = QmUiIconSurfaceProtection(QmUiIconColor(ColorRGBA(1, 1, 1, 1), 4, 0, 0), Surface);
		for(int Frame = 1; Frame <= 720; ++Frame)
		{
			const ColorRGBA Current = QmUiIconSurfaceProtection(QmUiIconColor(ColorRGBA(1, 1, 1, 1), 4, 0, Frame / 144.0), Surface);
			EXPECT_FLOAT_EQ(Current.r, Previous.r);
			EXPECT_FLOAT_EQ(Current.g, Previous.g);
			EXPECT_FLOAT_EQ(Current.b, Previous.b);
			EXPECT_LT(std::abs(Current.a - Previous.a), 0.025f);
			Previous = Current;
		}
	}
}

TEST(QmIconSurfaceProtection, NestedSurfaceScopeRestoresParentProtection)
{
	const ColorRGBA Original = CUiScopedSurfaceText::CurrentSurface();
	{
		CUiScopedSurfaceText Parent(nullptr, ColorRGBA(0.1f, 0.1f, 0.1f, 1));
		EXPECT_FLOAT_EQ(ConfiguredQmUiIconContrastColor(ColorRGBA(1, 1, 1, 1)).a, 0);
		{
			CUiScopedSurfaceText Child(nullptr, ColorRGBA(1, 1, 1, 1));
			EXPECT_GT(ConfiguredQmUiIconContrastColor(ColorRGBA(1, 1, 1, 1)).a, 0);
		}
		EXPECT_FLOAT_EQ(ConfiguredQmUiIconContrastColor(ColorRGBA(1, 1, 1, 1)).a, 0);
	}
	EXPECT_EQ(CUiScopedSurfaceText::CurrentSurface(), Original);
}

TEST(QmIconSurfaceProtection, UnknownWorldBackgroundKeepsWeakProtectionAcrossNestedScopes)
{
	ASSERT_FALSE(CUiScopedSurfaceText::HasKnownSurface());
	const ColorRGBA Primary(1, 1, 1, 0.5f);
	const ColorRGBA Unknown = ConfiguredQmUiIconContrastColor(Primary);
	EXPECT_EQ(Unknown, ColorRGBA(0, 0, 0, 0.175f));
	{
		CUiScopedSurfaceText Transparent(nullptr, ColorRGBA(0, 0, 0, 0));
		EXPECT_FALSE(CUiScopedSurfaceText::HasKnownSurface());
		EXPECT_EQ(ConfiguredQmUiIconContrastColor(Primary), Unknown);
		{
			CUiScopedSurfaceText Translucent(nullptr, ColorRGBA(0, 0, 0, 0.5f));
			EXPECT_FALSE(CUiScopedSurfaceText::HasKnownSurface());
			EXPECT_EQ(ConfiguredQmUiIconContrastColor(Primary), Unknown);
		}
		{
			CUiScopedSurfaceText Known(nullptr, ColorRGBA(0.1f, 0.1f, 0.1f, 1));
			EXPECT_TRUE(CUiScopedSurfaceText::HasKnownSurface());
			EXPECT_FLOAT_EQ(ConfiguredQmUiIconContrastColor(Primary).a, 0);
			{
				CUiScopedSurfaceText Disabled(nullptr, ColorRGBA(1, 1, 1, 1), false);
				EXPECT_TRUE(CUiScopedSurfaceText::HasKnownSurface());
				EXPECT_FLOAT_EQ(ConfiguredQmUiIconContrastColor(Primary).a, 0);
			}
		}
		EXPECT_FALSE(CUiScopedSurfaceText::HasKnownSurface());
		EXPECT_EQ(ConfiguredQmUiIconContrastColor(Primary), Unknown);
	}
	EXPECT_FALSE(CUiScopedSurfaceText::HasKnownSurface());
	EXPECT_EQ(ConfiguredQmUiIconContrastColor(Primary), Unknown);
}

namespace
{
	struct SFontIconRenderObserver
	{
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		ColorRGBA m_Outline{0.7f, 0.6f, 0.5f, 0.4f};
		unsigned m_Flags = TEXT_RENDER_FLAG_NO_X_BEARING;
		ColorRGBA m_BuildColor;
		ColorRGBA m_DrawnColor;
		ColorRGBA m_DrawnProtection;
		int m_Draws = 0;
		int m_Deletes = 0;
		bool m_CreateValid = true;
		ColorRGBA GetTextColor() const { return m_Color; }
		ColorRGBA GetTextOutlineColor() const { return m_Outline; }
		unsigned GetRenderFlags() const { return m_Flags; }
		void TextColor(ColorRGBA Color) { m_Color = Color; }
		void TextOutlineColor(ColorRGBA Color) { m_Outline = Color; }
		void SetRenderFlags(unsigned Flags) { m_Flags = Flags; }
		void CreateTextContainer(STextContainerIndex &Container, CTextCursor *pCursor, const char *, int)
		{
			m_BuildColor = m_Color;
			EXPECT_TRUE(pCursor->m_vColorSplits.empty());
			EXPECT_NE(m_Flags & TEXT_RENDER_FLAG_ONE_TIME_USE, 0u);
			pCursor->m_X += 7.0f;
			if(m_CreateValid)
				Container.m_Index = 0;
		}
		void RenderTextContainer(STextContainerIndex, ColorRGBA Color, ColorRGBA Protection)
		{
			++m_Draws;
			m_DrawnColor = Color;
			m_DrawnProtection = Protection;
		}
		void DeleteTextContainer(STextContainerIndex &Container)
		{
			++m_Deletes;
			Container.Reset();
		}
	};
}

TEST(QmIconImmediateFont, WhiteVerticesPreserveActualProtectionAlphaAndRestoreCallerState)
{
	SFontIconRenderObserver Render;
	const ColorRGBA PreviousColor = Render.m_Color;
	const ColorRGBA PreviousOutline = Render.m_Outline;
	const unsigned PreviousFlags = Render.m_Flags;
	const ColorRGBA Primary(0.9f, 0.8f, 0.2f, 0.25f);
	const ColorRGBA Protection(0, 0, 0, 0.0875f);
	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_X = 10;
	Cursor.m_vColorSplits.emplace_back(0, 1, ColorRGBA(0.2f, 0.4f, 0.8f, 0.3f), ColorRGBA(0.8f, 0.4f, 0.2f, 0.7f));
	const auto SavedSplit = Cursor.m_vColorSplits.front();
	QmRenderImmediateFontIcon(Render, &Cursor, "icon", -1, Primary, Protection);
	EXPECT_EQ(Render.m_BuildColor, ColorRGBA(1, 1, 1, 1));
	EXPECT_EQ(Render.m_DrawnColor, Primary);
	EXPECT_EQ(Render.m_DrawnProtection, Protection);
	EXPECT_EQ(Render.m_Color, PreviousColor);
	EXPECT_EQ(Render.m_Outline, PreviousOutline);
	EXPECT_EQ(Render.m_Flags, PreviousFlags);
	EXPECT_FLOAT_EQ(Cursor.m_X, 17);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, SavedSplit.m_CharIndex);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Length, SavedSplit.m_Length);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Color, SavedSplit.m_Color);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_ColorEnd, SavedSplit.m_ColorEnd);
	EXPECT_EQ(Render.m_Draws, 1);
	EXPECT_EQ(Render.m_Deletes, 1);
}

TEST(QmIconImmediateFont, LayoutOnlyPreservesStateWithoutDrawingAndReleasesContainer)
{
	SFontIconRenderObserver Render;
	Render.m_CreateValid = true;
	const ColorRGBA Previous = Render.m_Color;
	CTextCursor Cursor;
	Cursor.m_Flags = 0;
	Cursor.m_vColorSplits.emplace_back(0, 1, Previous);
	QmRenderImmediateFontIcon(Render, &Cursor, "icon", -1, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 0));
	EXPECT_EQ(Render.m_Draws, 0);
	EXPECT_EQ(Render.m_Deletes, 1);
	EXPECT_EQ(Render.m_Color, Previous);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Color, Previous);
}

TEST(QmIconImmediateFont, FailedCreationPreservesStateWithoutDrawingAndReleasesContainer)
{
	SFontIconRenderObserver Render;
	Render.m_CreateValid = false;
	const ColorRGBA Previous = Render.m_Color;
	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.m_vColorSplits.emplace_back(0, 1, Previous);
	QmRenderImmediateFontIcon(Render, &Cursor, "icon", -1, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 0));
	EXPECT_EQ(Render.m_Draws, 0);
	EXPECT_EQ(Render.m_Deletes, 1);
	EXPECT_EQ(Render.m_Color, Previous);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Color, Previous);
}
