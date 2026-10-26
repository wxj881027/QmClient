#include <base/system.h>

#include <game/client/components/qmclient/console_appearance.h>

#include <gtest/gtest.h>

TEST(QmConsoleAppearance, DefaultTerminalUsesDarkBackgroundAndLightText)
{
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});
	EXPECT_LT(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].r, 0.1f);
	EXPECT_GT(Palette.m_aColors[QmConsoleAppearance::TEXT].r, 0.75f);
	EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].a, 0.96f);
	EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::TEXT].a, 1.0f);
}

TEST(QmConsoleAppearance, ClassicPresetUsesABlueBackground)
{
	const auto Palette = QmConsoleAppearance::ResolvePalette(1, 100, {});
	const auto Background = Palette.m_aColors[QmConsoleAppearance::BACKGROUND];
	EXPECT_GT(Background.b, Background.g);
	EXPECT_GT(Background.g, Background.r);
}

TEST(QmConsoleAppearance, CustomColorsKeepOpaqueTextAndIndependentBackgroundOpacity)
{
	std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> aCustom{};
	aCustom.fill(ColorHSLA(0.5f, 0.75f, 0.6f, 1.0f).Pack(false));
	const auto Palette = QmConsoleAppearance::ResolvePalette(2, 50, aCustom);
	const auto Expected = color_cast<ColorRGBA>(ColorHSLA(aCustom[0]));
	EXPECT_EQ(Palette.m_aColors[QmConsoleAppearance::TEXT], Expected);
	EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].r, Expected.r);
	EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].a, 0.5f);
}

TEST(QmConsoleAppearance, InvalidExternalValuesHaveBoundedFallbacks)
{
	EXPECT_EQ(QmConsoleAppearance::ResolvePalette(99, 96, {}).m_aColors, QmConsoleAppearance::ResolvePalette(0, 96, {}).m_aColors);
	EXPECT_FLOAT_EQ(QmConsoleAppearance::ResolvePalette(0, -10, {}).m_aColors[0].a, 0.2f);
	EXPECT_FLOAT_EQ(QmConsoleAppearance::ResolvePalette(0, 200, {}).m_aColors[0].a, 1.0f);
	EXPECT_FLOAT_EQ(QmConsoleAppearance::FontSize(-1), 1.0f);
	EXPECT_FLOAT_EQ(QmConsoleAppearance::FontSize(100), 24.0f);
}

TEST(QmConsoleAppearance, ResetRestoresAppearanceAndPreservesLogFilters)
{
	CConfig Config{};
	Config.m_QmConsoleFilterMask = 2;
	Config.m_QmConsoleFontSize = 24;
	Config.m_QmConsoleColorScheme = 2;
	Config.m_QmConsoleOpacity = 20;
	Config.m_QmConsoleHighlightCommands = 0;
	QmConsoleAppearance::Reset(Config);
	EXPECT_EQ(Config.m_QmConsoleFilterMask, 2);
	EXPECT_EQ(Config.m_QmConsoleFontSize, 10);
	EXPECT_EQ(Config.m_QmConsoleColorScheme, 0);
	EXPECT_EQ(Config.m_QmConsoleOpacity, 96);
	EXPECT_EQ(Config.m_QmConsoleHighlightCommands, 1);
	EXPECT_EQ(Config.m_QmConsoleLinkColor, DefaultConfig::QmConsoleLinkColor);
	EXPECT_EQ(Config.m_QmConsoleSearchSelectedColor, DefaultConfig::QmConsoleSearchSelectedColor);
}

TEST(QmConsoleAppearance, FontSizeAllowsOneThroughSevenWithoutClampingToEight)
{
	for(int Size = 1; Size <= 7; ++Size)
		EXPECT_FLOAT_EQ(QmConsoleAppearance::FontSize(Size), static_cast<float>(Size));
}

TEST(QmConsoleAppearance, CustomBackgroundChangesBorderAndShadowColors)
{
	std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> aCustom{};
	aCustom.fill(ColorHSLA(0.0f, 0.0f, 0.9f, 1.0f).Pack(false));
	aCustom[QmConsoleAppearance::BACKGROUND] = ColorHSLA(0.0f, 0.8f, 0.4f, 1.0f).Pack(false);
	const auto Red = QmConsoleAppearance::ResolvePalette(2, 80, aCustom);
	aCustom[QmConsoleAppearance::BACKGROUND] = ColorHSLA(0.6f, 0.8f, 0.4f, 1.0f).Pack(false);
	const auto Blue = QmConsoleAppearance::ResolvePalette(2, 80, aCustom);
	EXPECT_NE(Red.m_Border, Blue.m_Border);
	EXPECT_GT(Red.m_Border.r, Red.m_Border.b);
	EXPECT_GT(Blue.m_Border.b, Blue.m_Border.r);
	EXPECT_GT(Red.m_Shadow.r, Red.m_Shadow.b);
	EXPECT_GT(Blue.m_Shadow.b, Blue.m_Shadow.r);
	for(const auto &Palette : {Red, Blue})
	{
		const auto Background = Palette.m_aColors[QmConsoleAppearance::BACKGROUND];
		EXPECT_FLOAT_EQ(Palette.m_Shadow.r, Background.r);
		EXPECT_FLOAT_EQ(Palette.m_Shadow.g, Background.g);
		EXPECT_FLOAT_EQ(Palette.m_Shadow.b, Background.b);
	}
}

TEST(QmConsoleAppearance, CustomTextChangesBorderWithoutChangingShadow)
{
	std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> aCustom{};
	aCustom.fill(ColorHSLA(0.0f, 0.0f, 0.4f, 1.0f).Pack(false));
	aCustom[QmConsoleAppearance::TEXT] = ColorHSLA(0.0f, 0.0f, 0.1f, 1.0f).Pack(false);
	const auto DarkText = QmConsoleAppearance::ResolvePalette(2, 80, aCustom);
	aCustom[QmConsoleAppearance::TEXT] = ColorHSLA(0.0f, 0.0f, 0.9f, 1.0f).Pack(false);
	const auto LightText = QmConsoleAppearance::ResolvePalette(2, 80, aCustom);
	const auto Background = LightText.m_aColors[QmConsoleAppearance::BACKGROUND];
	EXPECT_LT(DarkText.m_Border.r, Background.r);
	EXPECT_GT(LightText.m_Border.r, Background.r);
	EXPECT_LT(LightText.m_Border.r, LightText.m_aColors[QmConsoleAppearance::TEXT].r);
	EXPECT_LT(LightText.m_Border.r - Background.r, Background.r - DarkText.m_aColors[QmConsoleAppearance::TEXT].r);
	EXPECT_EQ(DarkText.m_Shadow, LightText.m_Shadow);
}

TEST(QmConsoleAppearance, OpacityChangesDecorationAlphaWithoutChangingRgb)
{
	for(int Scheme : {0, 1, 2})
	{
		SCOPED_TRACE(Scheme);
		std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> aCustom{};
		aCustom.fill(ColorHSLA(0.3f, 0.7f, 0.5f, 1.0f).Pack(false));
		const auto Translucent = QmConsoleAppearance::ResolvePalette(Scheme, 20, aCustom);
		const auto Opaque = QmConsoleAppearance::ResolvePalette(Scheme, 100, aCustom);
		EXPECT_LT(Translucent.m_Border.a, Opaque.m_Border.a);
		EXPECT_LT(Translucent.m_Shadow.a, Opaque.m_Shadow.a);
		for(const auto &Palette : {Translucent, Opaque})
		{
			EXPECT_FLOAT_EQ(Palette.m_Border.a, Palette.m_aColors[QmConsoleAppearance::BACKGROUND].a);
			EXPECT_GT(Palette.m_Shadow.a, 0.0f);
			EXPECT_LT(Palette.m_Shadow.a, Palette.m_aColors[QmConsoleAppearance::BACKGROUND].a);
		}
		EXPECT_EQ(Translucent.m_Border.WithAlpha(1.0f), Opaque.m_Border.WithAlpha(1.0f));
		EXPECT_EQ(Translucent.m_Shadow.WithAlpha(1.0f), Opaque.m_Shadow.WithAlpha(1.0f));
		const auto BelowMinimum = QmConsoleAppearance::ResolvePalette(Scheme, -10, aCustom);
		const auto AboveMaximum = QmConsoleAppearance::ResolvePalette(Scheme, 200, aCustom);
		EXPECT_EQ(BelowMinimum.m_Border, Translucent.m_Border);
		EXPECT_EQ(BelowMinimum.m_Shadow, Translucent.m_Shadow);
		EXPECT_EQ(AboveMaximum.m_Border, Opaque.m_Border);
		EXPECT_EQ(AboveMaximum.m_Shadow, Opaque.m_Shadow);
	}
}

TEST(QmConsoleAppearance, SwitchingBackToPresetsRestoresDecorations)
{
	std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> aCustom{};
	aCustom.fill(ColorHSLA(0.3f, 0.8f, 0.6f, 1.0f).Pack(false));
	const auto Custom = QmConsoleAppearance::ResolvePalette(2, 96, aCustom);
	for(int Scheme : {0, 1})
	{
		SCOPED_TRACE(Scheme);
		const auto Restored = QmConsoleAppearance::ResolvePalette(Scheme, 96, aCustom);
		const auto Preset = QmConsoleAppearance::ResolvePalette(Scheme, 96, {});
		EXPECT_EQ(Restored.m_Border, Preset.m_Border);
		EXPECT_EQ(Restored.m_Shadow, Preset.m_Shadow);
		EXPECT_NE(Restored.m_Border, Custom.m_Border);
		EXPECT_NE(Restored.m_Shadow, Custom.m_Shadow);
		EXPECT_GT(Restored.m_Border.r, Restored.m_aColors[QmConsoleAppearance::BACKGROUND].r);
		EXPECT_LT(Restored.m_Border.r, Restored.m_aColors[QmConsoleAppearance::TEXT].r);
	}
	const auto Fallback = QmConsoleAppearance::ResolvePalette(99, 96, aCustom);
	const auto Default = QmConsoleAppearance::ResolvePalette(0, 96, {});
	EXPECT_EQ(Fallback.m_Border, Default.m_Border);
	EXPECT_EQ(Fallback.m_Shadow, Default.m_Shadow);
}

TEST(QmConsoleAppearance, ResetRestoresDefaultDecorations)
{
	CConfig Config{};
	QmConsoleAppearance::Reset(Config);
	const auto Default = QmConsoleAppearance::Palette(Config);
	Config.m_QmConsoleColorScheme = 2;
	Config.m_QmConsoleOpacity = 20;
	Config.m_QmConsoleBackgroundColor = ColorHSLA(0.3f, 0.8f, 0.6f, 1.0f).Pack(false);
	Config.m_QmConsoleTextColor = ColorHSLA(0.0f, 0.0f, 0.1f, 1.0f).Pack(false);
	const auto Custom = QmConsoleAppearance::Palette(Config);
	EXPECT_NE(Custom.m_Border, Default.m_Border);
	EXPECT_NE(Custom.m_Shadow, Default.m_Shadow);
	QmConsoleAppearance::Reset(Config);
	const auto Restored = QmConsoleAppearance::Palette(Config);
	EXPECT_EQ(Restored.m_Border, Default.m_Border);
	EXPECT_EQ(Restored.m_Shadow, Default.m_Shadow);
}

// 内置方案必须真的接管背景与前景：选中 Campbell 后不是原来的现代终端配色。
TEST(QmConsoleAppearance, BuiltinSchemeReplacesBackgroundAndText)
{
	const int CampbellIndex = QmConsoleAppearance::k_BuiltinSchemeBase + [] {
		for(int i = 0; i < QmConsoleSchemes::Count(); ++i)
			if(str_comp(QmConsoleSchemes::At(i).m_pName, "Campbell") == 0)
				return i;
		return -1;
	}();
	ASSERT_GT(CampbellIndex, QmConsoleAppearance::k_BuiltinSchemeBase - 1);
	const auto Palette = QmConsoleAppearance::ResolvePalette(CampbellIndex, 96, {});
	// Campbell 背景 #0C0C0C、前景 #CCCCCC。
	EXPECT_NEAR(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].r, 0x0C / 255.0f, 1e-4f);
	EXPECT_NEAR(Palette.m_aColors[QmConsoleAppearance::TEXT].r, 0xCC / 255.0f, 1e-4f);
	EXPECT_NE(Palette.m_aColors[QmConsoleAppearance::BACKGROUND], QmConsoleAppearance::ResolvePalette(0, 96, {}).m_aColors[QmConsoleAppearance::BACKGROUND]);
}

// 语法层级在任意内置方案下都保持强弱关系：命令/链接/搜索都要与正文区分开。
TEST(QmConsoleAppearance, EveryBuiltinSchemeKeepsSyntaxColorsDistinctFromBodyText)
{
	for(int Index = 0; Index < QmConsoleSchemes::Count(); ++Index)
	{
		SCOPED_TRACE(QmConsoleSchemes::At(Index).m_pName);
		const auto Palette = QmConsoleAppearance::ResolvePalette(QmConsoleAppearance::k_BuiltinSchemeBase + Index, 96, {});
		const auto Text = Palette.m_aColors[QmConsoleAppearance::TEXT];
		for(int Color : {QmConsoleAppearance::COMMAND, QmConsoleAppearance::PARAMETER, QmConsoleAppearance::STRING,
			    QmConsoleAppearance::NUMBER, QmConsoleAppearance::LINK})
		{
			EXPECT_NE(Palette.m_aColors[Color], Text);
		}
		// 搜索高亮必须与正文可区分，否则命中看不出来。
		EXPECT_NE(Palette.m_aColors[QmConsoleAppearance::SEARCH], Text);
		// 部分方案的黄色与正文相同，此时"当前命中"改成能与命中色区分的色。
		EXPECT_NE(Palette.m_aColors[QmConsoleAppearance::SEARCH], Palette.m_aColors[QmConsoleAppearance::SEARCH_SELECTED]);
		// 内置方案全部不透明，背景透明度仍由 Opacity 单独控制。
		EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::BACKGROUND].a, 0.96f);
		EXPECT_FLOAT_EQ(Palette.m_aColors[QmConsoleAppearance::TEXT].a, 1.0f);
	}
}

// 方案编号越界时退回现代终端，不能让界面崩或画面全黑。
TEST(QmConsoleAppearance, UnknownSchemeValueFallsBackToTheDefaultPalette)
{
	const auto Default = QmConsoleAppearance::ResolvePalette(0, 96, {});
	EXPECT_EQ(QmConsoleAppearance::ResolvePalette(QmConsoleAppearance::k_BuiltinSchemeBase + QmConsoleSchemes::Count(), 96, {}).m_aColors, Default.m_aColors);
	EXPECT_EQ(QmConsoleAppearance::ResolvePalette(9999, 96, {}).m_aColors, Default.m_aColors);
	EXPECT_FALSE(QmConsoleAppearance::IsBuiltinScheme(2));
	EXPECT_FALSE(QmConsoleAppearance::IsBuiltinScheme(QmConsoleAppearance::k_BuiltinSchemeBase + QmConsoleSchemes::Count()));
	EXPECT_TRUE(QmConsoleAppearance::IsBuiltinScheme(QmConsoleAppearance::k_BuiltinSchemeBase));
}

// 透明度对外置方案同样只作用于背景，不改变文字与语法色。
TEST(QmConsoleAppearance, BuiltinSchemeOpacityOnlyAffectsBackgroundAlpha)
{
	const int Scheme = QmConsoleAppearance::k_BuiltinSchemeBase;
	const auto Opaque = QmConsoleAppearance::ResolvePalette(Scheme, 100, {});
	const auto Translucent = QmConsoleAppearance::ResolvePalette(Scheme, 40, {});
	for(int Color = 1; Color < QmConsoleAppearance::COLOR_COUNT; ++Color)
		EXPECT_EQ(Opaque.m_aColors[Color], Translucent.m_aColors[Color]);
	EXPECT_FLOAT_EQ(Translucent.m_aColors[QmConsoleAppearance::BACKGROUND].a, 0.4f);
	EXPECT_EQ(Opaque.m_aColors[QmConsoleAppearance::BACKGROUND].WithAlpha(1.0f), Translucent.m_aColors[QmConsoleAppearance::BACKGROUND].WithAlpha(1.0f));
}
