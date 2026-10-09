#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/QmImeAppearance.h>
#include <game/client/QmUi/cards/QmColorGradientEditor.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
	SQmColorGradient Gradient(int Type = 0, int Angle = 0, int CenterX = 50, int CenterY = 50, int Range = 100, bool Reverse = false)
	{
		return SQmColorGradient::FromConfig("FF0000,00FF00,0000FF", ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f), Type, Angle, CenterX, CenterY, Range, Reverse);
	}
}

TEST(QmColorGradient, EmptyAndMalformedPalettesPreserveTheSingleColor)
{
	for(const char *pColors : {"", "invalid,garbage"})
	{
		const auto Value = SQmColorGradient::FromConfig(pColors, ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f), 4, 90, 10, 20, 50, true);
		EXPECT_EQ(Value.m_NumColors, 1);
		const auto Color = Value.Sample(vec2(0.1f, 0.8f));
		EXPECT_FLOAT_EQ(Color.r, 0.2f);
		EXPECT_FLOAT_EQ(Color.g, 0.3f);
		EXPECT_FLOAT_EQ(Color.b, 0.4f);
		EXPECT_FLOAT_EQ(Color.a, 0.5f);
	}
}

TEST(QmColorGradient, LinearDirectionCanRunLeftToRightOrRightToLeft)
{
	const auto Forward = Gradient();
	const auto Backward = Gradient(0, 180);
	EXPECT_FLOAT_EQ(Forward.Position(vec2(0.0f, 0.5f)), 0.0f);
	EXPECT_FLOAT_EQ(Forward.Position(vec2(1.0f, 0.5f)), 1.0f);
	EXPECT_NEAR(Backward.Position(vec2(0.0f, 0.5f)), 1.0f, 0.00001f);
	EXPECT_NEAR(Backward.Position(vec2(1.0f, 0.5f)), 0.0f, 0.00001f);
}

TEST(QmColorGradient, RotationSupportsVerticalAndDiagonalDirections)
{
	const auto Vertical = Gradient(0, 90);
	EXPECT_NEAR(Vertical.Position(vec2(0.5f, 0.0f)), 0.0f, 0.00001f);
	EXPECT_NEAR(Vertical.Position(vec2(0.5f, 1.0f)), 1.0f, 0.00001f);
	const auto Diagonal = Gradient(0, 45);
	EXPECT_NEAR(Diagonal.Position(vec2(0.0f, 0.0f)), 0.0f, 0.00001f);
	EXPECT_NEAR(Diagonal.Position(vec2(1.0f, 1.0f)), 1.0f, 0.00001f);
}

TEST(QmColorGradient, MovingTheCenterAndChangingRangeMovesTheColorTransition)
{
	EXPECT_NEAR(Gradient(0, 0, 25, 50).Position(vec2(0.25f, 0.5f)), 0.5f, 0.00001f);
	EXPECT_FLOAT_EQ(Gradient(0, 0, 50, 50, 50).Position(vec2(0.75f, 0.5f)), 1.0f);
	EXPECT_FLOAT_EQ(Gradient(0, 0, 50, 50, 200).Position(vec2(0.75f, 0.5f)), 0.625f);
}

TEST(QmColorGradient, RadialGradientSpreadsFromAnAdjustableCenter)
{
	const auto Value = Gradient(1, 0, 25, 75);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.25f, 0.75f)), 0.0f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.75f, 0.75f)), 1.0f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.25f, 0.25f)), 1.0f);
}

TEST(QmColorGradient, AngularGradientRotatesItsStartingRay)
{
	const auto Value = Gradient(2);
	EXPECT_FLOAT_EQ(Value.Position(vec2(1.0f, 0.5f)), 0.0f);
	EXPECT_NEAR(Value.Position(vec2(0.5f, 1.0f)), 0.25f, 0.00001f);
	EXPECT_NEAR(Value.Position(vec2(0.5f, 0.0f)), 0.75f, 0.00001f);
	EXPECT_NEAR(Gradient(2, 90).Position(vec2(0.5f, 1.0f)), 0.0f, 0.00001f);
}

TEST(QmColorGradient, ReflectedGradientMirrorsBothSidesOfTheCenter)
{
	const auto Value = Gradient(3);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.5f, 0.5f)), 0.0f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.25f, 0.5f)), 0.5f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.75f, 0.5f)), 0.5f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.0f, 0.5f)), 1.0f);
	EXPECT_NEAR(Gradient(3, 90).Position(vec2(0.5f, 0.0f)), 1.0f, 0.00001f);
}

TEST(QmColorGradient, DiamondGradientHasDiamondShapedEqualColorContours)
{
	const auto Value = Gradient(4);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.5f, 0.5f)), 0.0f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(0.75f, 0.75f)), 1.0f);
	EXPECT_FLOAT_EQ(Value.Position(vec2(1.0f, 0.5f)), 1.0f);
	EXPECT_NEAR(Gradient(4, 45).Position(vec2(0.75f, 0.75f)), std::sqrt(0.5f), 0.00001f);
}

TEST(QmColorGradient, ReverseSwapsTheBeginningAndEndForAllFiveTypes)
{
	for(int Type = 0; Type <= 4; ++Type)
	{
		SCOPED_TRACE(Type);
		const vec2 Point(0.7f, 0.6f);
		EXPECT_FLOAT_EQ(Gradient(Type).Position(Point) + Gradient(Type, 0, 50, 50, 100, true).Position(Point), 1.0f);
	}
}

TEST(QmColorGradient, SevenStopsKeepTheirOrderAndTheConfiguredOpacity)
{
	const auto Value = SQmColorGradient::FromConfig("FF0000,00FF00,0000FF,FFFF00,00FFFF,FF00FF,FFFFFF,000000",
		ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), 0, 0, 50, 50, 100, false);
	ASSERT_EQ(Value.m_NumColors, 7);
	for(int i = 0; i < 7; ++i)
	{
		SCOPED_TRACE(i);
		const auto Color = Value.Sample(vec2(i / 6.0f, 0.5f));
		EXPECT_NEAR(Color.r, Value.m_aColors[i].r, 0.00001f);
		EXPECT_NEAR(Color.g, Value.m_aColors[i].g, 0.00001f);
		EXPECT_NEAR(Color.b, Value.m_aColors[i].b, 0.00001f);
		EXPECT_FLOAT_EQ(Color.a, 0.4f);
	}
}

TEST(QmColorGradient, InvalidConfigValuesAreClampedToFiniteSamplingParameters)
{
	const auto Value = Gradient(100, -100, -10, 120, 0);
	EXPECT_EQ(Value.m_Type, EQmGradientType::DIAMOND);
	EXPECT_FLOAT_EQ(Value.m_Center.x, 0.0f);
	EXPECT_FLOAT_EQ(Value.m_Center.y, 1.0f);
	EXPECT_FLOAT_EQ(Value.m_Range, 0.1f);
	EXPECT_TRUE(std::isfinite(Value.Position(vec2(0.5f, 0.5f))));
}

TEST(QmColorGradient, TextPaintSamplesPhysicalCoordinatesAndPreservesAlpha)
{
	const auto Value = Gradient();
	const SQmGradientTextPaint Paint{&Value, vec2(10.0f, 20.0f), vec2(80.0f, 16.0f), 0.5f};
	const auto Left = SQmGradientTextPaint::Sample(vec2(10.0f, 28.0f), &Paint);
	const auto Middle = SQmGradientTextPaint::Sample(vec2(50.0f, 28.0f), &Paint);
	const auto Right = SQmGradientTextPaint::Sample(vec2(90.0f, 28.0f), &Paint);
	EXPECT_NEAR(Left.r, 1.0f, 0.02f);
	EXPECT_NEAR(Middle.g, 1.0f, 0.02f);
	EXPECT_NEAR(Right.b, 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(Middle.a, 0.2f);
}

TEST(QmImeAppearance, ColorRolesKeepIndependentPalettesAndGeometry)
{
	CConfig Config{};
	Config.m_QmImeBgColor = DefaultConfig::QmImeBgColor;
	Config.m_QmImeTextColor = DefaultConfig::QmImeTextColor;
	Config.m_QmImeSelectedTextColor = DefaultConfig::QmImeSelectedTextColor;
	Config.m_QmImeSelectedColor = DefaultConfig::QmImeSelectedColor;
	Config.m_QmImeFontSize = DefaultConfig::QmImeFontSize;
	Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
	Config.m_QmImeTextGradientRange = 100;
	Config.m_QmImeTextGradientCenterX = Config.m_QmImeTextGradientCenterY = 50;
	str_copy(Config.m_QmImeBgGradient, "FF0000,0000FF");
	Config.m_QmImeBgGradientType = 1;
	Config.m_QmImeBgGradientCenterX = 25;
	str_copy(Config.m_QmImeTextGradient, "00FF00,FFFFFF");
	Config.m_QmImeTextGradientAngle = 180;
	const auto Appearance = QmImeAppearance(Config);
	EXPECT_EQ(Appearance.m_Background.m_Type, EQmGradientType::RADIAL);
	EXPECT_FLOAT_EQ(Appearance.m_Background.m_Center.x, 0.25f);
	EXPECT_EQ(Appearance.m_Text.m_Type, EQmGradientType::LINEAR);
	EXPECT_NEAR(Appearance.m_Text.Position(vec2(0.0f, 0.5f)), 1.0f, 0.00001f);
	EXPECT_EQ(Appearance.m_SelectedText.m_NumColors, 1);
	EXPECT_EQ(Appearance.m_Selection.m_NumColors, 1);
}

TEST(QmColorGradient, VerticalTextPaintChangesColorFromTopToBottom)
{
	const auto Value = Gradient(0, 90);
	const SQmGradientTextPaint Paint{&Value, vec2(10.0f, 20.0f), vec2(16.0f, 16.0f), 1.0f};
	EXPECT_NEAR(SQmGradientTextPaint::Sample(vec2(18.0f, 20.0f), &Paint).r, 1.0f, 0.02f);
	EXPECT_NEAR(SQmGradientTextPaint::Sample(vec2(18.0f, 36.0f), &Paint).b, 1.0f, 0.02f);
}

TEST(QmColorGradient, LongHorizontalMessagesDoNotNeedAnEightByEightGridForEveryGlyph)
{
	const auto Value = SQmColorGradient::FromConfig("FF0000,00FF00,0000FF,FFFF00,00FFFF,FF00FF,FFFFFF",
		ColorRGBA(1, 1, 1, 1), 0, 0, 50, 50, 100, false);
	const auto LongText = QmGradientTextGrid(Value, vec2(1000, 12), 12);
	EXPECT_EQ(LongText[0], 1);
	EXPECT_EQ(LongText[1], 1);
	const auto SingleGlyph = QmGradientTextGrid(Value, vec2(12, 12), 12);
	EXPECT_EQ(SingleGlyph[0], 8);
	EXPECT_EQ(SingleGlyph[1], 1);
}

TEST(QmColorGradient, TextGridTracksTheDirectionAndKeepsTheMaximumBudgetBounded)
{
	const auto Vertical = QmGradientTextGrid(Gradient(0, 90), vec2(1000, 12), 12);
	EXPECT_EQ(Vertical[0], 1);
	EXPECT_GT(Vertical[1], 1);
	for(int Type = 0; Type < 5; ++Type)
	{
		SCOPED_TRACE(Type);
		const auto Grid = QmGradientTextGrid(Gradient(Type, 45, 50, 50, 10), vec2(0, 0), 200);
		EXPECT_GE(Grid[0], 1);
		EXPECT_GE(Grid[1], 1);
		EXPECT_LE(Grid[0] * Grid[1], 64);
	}
}

TEST(QmGradientPalette, EditingAnImeColorPreservesHueAndOverallOpacity)
{
	unsigned BaseColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 0.0f, 0.0f, 0.56f)).Pack(true);
	const unsigned OriginalAlpha = BaseColor & 0xff000000u;
	char aPalette[128] = "";
	const SQmGradientPaletteBinding Binding{&BaseColor, aPalette, sizeof(aPalette), true};
	unsigned aColors[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(Binding.Load(aColors), 1);
	aColors[0] = color_cast<ColorHSLA>(ColorRGBA(0.0f, 1.0f, 0.66f, 1.0f)).Pack(true);
	Binding.Store(aColors, 1);
	const auto Color = color_cast<ColorRGBA>(ColorHSLA(BaseColor, true));
	EXPECT_NEAR(Color.r, 0.0f, 0.02f);
	EXPECT_NEAR(Color.g, 1.0f, 0.02f);
	EXPECT_NEAR(Color.b, 0.66f, 0.02f);
	EXPECT_EQ(BaseColor & 0xff000000u, OriginalAlpha);
	EXPECT_STREQ(aPalette, "");
	ASSERT_EQ(Binding.Load(aColors), 1);
	EXPECT_EQ(aColors[0], color_cast<ColorHSLA>(Color).WithAlpha(1.0f).Pack(true));
}

TEST(QmGradientPalette, OverallOpacityChangesWithoutChangingColorOrStops)
{
	unsigned BaseColor = color_cast<ColorHSLA>(ColorRGBA(0.0f, 1.0f, 0.66f, 0.56f)).Pack(true);
	const unsigned OriginalColor = BaseColor & 0x00ffffffu;
	char aPalette[128] = "00FFA800,0000FF80";
	const SQmGradientPaletteBinding Binding{&BaseColor, aPalette, sizeof(aPalette), true};
	EXPECT_EQ(Binding.Opacity(), 56);
	for(int Opacity : {0, 25, 100, 56})
	{
		SCOPED_TRACE(Opacity);
		Binding.SetOpacity(Opacity);
		EXPECT_EQ(Binding.Opacity(), Opacity);
		EXPECT_EQ(BaseColor & 0x00ffffffu, OriginalColor);
		EXPECT_STREQ(aPalette, "00FFA800,0000FF80");
	}
}

TEST(QmGradientPalette, RemovingTheLastStopKeepsItsTransparencyUntilMadeOpaque)
{
	unsigned BaseColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)).Pack(true);
	char aPalette[128] = "FF000080,0000FF00";
	const SQmGradientPaletteBinding Binding{&BaseColor, aPalette, sizeof(aPalette), true};
	unsigned aColors[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(Binding.Load(aColors), 2);
	Binding.Store(aColors, 1);
	ASSERT_EQ(Binding.Load(aColors), 1);
	EXPECT_NEAR(ColorHSLA(aColors[0], true).a, 128.0f / 255.0f, 0.00001f);
	EXPECT_EQ(Binding.Opacity(), 50);
	aColors[0] = ColorHSLA(aColors[0], true).WithAlpha(1.0f).Pack(true);
	Binding.Store(aColors, 1);
	EXPECT_STREQ(aPalette, "");
	EXPECT_EQ(Binding.Opacity(), 50);
}

TEST(QmGradientPalette, ChatBaseColorKeepsItsRgbWhenAStopBecomesTransparent)
{
	unsigned BaseColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 0.0f, 0.0f)).Pack(false);
	char aPalette[128] = "";
	const SQmGradientPaletteBinding Binding{&BaseColor, aPalette, sizeof(aPalette), false};
	unsigned aColors[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(Binding.Load(aColors), 1);
	aColors[0] = ColorHSLA(aColors[0], true).WithAlpha(0.0f).Pack(true);
	Binding.Store(aColors, 1);
	const auto Persisted = color_parse<ColorRGBA>(aPalette);
	ASSERT_TRUE(Persisted.has_value());
	EXPECT_NEAR(Persisted->r, 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(Persisted->a, 0.0f);
	const auto Color = color_cast<ColorRGBA>(ColorHSLA(BaseColor));
	EXPECT_NEAR(Color.r, 1.0f, 0.02f);
	EXPECT_NEAR(Color.g, 0.0f, 0.02f);
	ASSERT_EQ(Binding.Load(aColors), 1);
	EXPECT_FLOAT_EQ(ColorHSLA(aColors[0], true).a, 0.0f);
}

TEST(QmColorGradient, TransparentStopsSurvivePackingAndReloading)
{
	unsigned aColors[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(CMessageGradient::Unpack("FF000000,00FF0080,0000FF", aColors, CMessageGradient::MAX_COLORS), 3);
	EXPECT_FLOAT_EQ(ColorHSLA(aColors[0], true).a, 0.0f);
	EXPECT_FLOAT_EQ(ColorHSLA(aColors[1], true).a, 128.0f / 255.0f);
	EXPECT_FLOAT_EQ(ColorHSLA(aColors[2], true).a, 1.0f);
	char aPalette[128];
	CMessageGradient::Pack(aColors, 3, aPalette, sizeof(aPalette));
	unsigned aReloaded[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(CMessageGradient::Unpack(aPalette, aReloaded, CMessageGradient::MAX_COLORS), 3);
	for(int i = 0; i < 3; ++i)
		EXPECT_EQ(aColors[i], aReloaded[i]);
}

TEST(QmColorGradient, MixedLegacyAndAlphaColorsSkipMalformedStops)
{
	const auto Value = SQmColorGradient::FromConfig("invalid;#F008 $00FF00;0000FF40 garbage",
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0, 0, 50, 50, 100, false);
	ASSERT_EQ(Value.m_NumColors, 3);
	EXPECT_NEAR(Value.m_aColors[0].r, 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(Value.m_aColors[0].a, (136.0f / 255.0f) * 0.5f);
	EXPECT_NEAR(Value.m_aColors[1].g, 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(Value.m_aColors[1].a, 0.5f);
	EXPECT_NEAR(Value.m_aColors[2].b, 1.0f, 0.02f);
	EXPECT_FLOAT_EQ(Value.m_aColors[2].a, (64.0f / 255.0f) * 0.5f);
}

TEST(QmColorGradient, StopOpacityInterpolatesAndMultipliesWithOverallOpacityAndFade)
{
	const auto Value = SQmColorGradient::FromConfig("FF000000,0000FFFF", ColorRGBA(1, 1, 1, 0.6f), 0, 0, 50, 50, 100, false);
	EXPECT_FLOAT_EQ(Value.Sample(vec2(0.0f, 0.5f)).a, 0.0f);
	EXPECT_FLOAT_EQ(Value.Sample(vec2(0.5f, 0.5f)).a, 0.3f);
	EXPECT_FLOAT_EQ(Value.Sample(vec2(1.0f, 0.5f)).a, 0.6f);
	const SQmGradientTextPaint Paint{&Value, vec2(10, 20), vec2(100, 20), 0.5f};
	EXPECT_FLOAT_EQ(SQmGradientTextPaint::Sample(vec2(60, 30), &Paint).a, 0.15f);
}

TEST(QmImeAppearance, AllFourRolesMultiplyStopsByTheirOwnOverallOpacity)
{
	CConfig Config{};
	Config.m_QmImeOpacity = 60;
	Config.m_QmImeTextColor = color_cast<ColorHSLA>(ColorRGBA(1, 1, 1, 0.5f)).Pack(true);
	Config.m_QmImeSelectedTextColor = color_cast<ColorHSLA>(ColorRGBA(1, 1, 1, 0.75f)).Pack(true);
	Config.m_QmImeSelectedColor = color_cast<ColorHSLA>(ColorRGBA(1, 1, 1, 0.25f)).Pack(true);
	for(char *pPalette : {Config.m_QmImeBgGradient, Config.m_QmImeTextGradient,
		Config.m_QmImeSelectedTextGradient, Config.m_QmImeSelectedGradient})
		str_copy(pPalette, "FF000000,0000FF80", sizeof(Config.m_QmImeBgGradient));
	const auto Appearance = QmImeAppearance(Config);
	const std::array<const SQmColorGradient *, 4> apGradients = {&Appearance.m_Background, &Appearance.m_Text,
		&Appearance.m_SelectedText, &Appearance.m_Selection};
	const std::array<float, 4> aOpacity = {0.6f, 128.0f / 255.0f, 191.0f / 255.0f, 64.0f / 255.0f};
	for(int i = 0; i < 4; ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(apGradients[i]->m_aColors[0].a, 0.0f);
		EXPECT_FLOAT_EQ(apGradients[i]->m_aColors[1].a, aOpacity[i] * (128.0f / 255.0f));
	}
	EXPECT_FLOAT_EQ(Appearance.m_BackgroundOpacity, 0.6f);
}

TEST(QmColorGradient, TransparencyChecksAllStopsInsteadOfOnlyTheFirst)
{
	const auto Value = SQmColorGradient::FromConfig("FF0000,0000FF00", ColorRGBA(1, 1, 1, 1), 0, 0, 50, 50, 100, false);
	EXPECT_FLOAT_EQ(Value.m_aColors[0].a, 1.0f);
	EXPECT_TRUE(Value.HasTransparency());
	EXPECT_FALSE(SQmColorGradient::FromConfig("FF0000,0000FF", ColorRGBA(1, 1, 1, 1), 0, 0, 50, 50, 100, false).HasTransparency());
}

TEST(QmGradientPalette, AddingStopsCopiesOpacityWithoutChangingTheOverallOpacity)
{
	unsigned BaseColor = color_cast<ColorHSLA>(ColorRGBA(0, 1, 0, 0.5f)).Pack(true);
	char aPalette[128] = "00FF0040";
	const SQmGradientPaletteBinding Binding{&BaseColor, aPalette, sizeof(aPalette), true};
	unsigned aColors[CMessageGradient::MAX_COLORS];
	ASSERT_EQ(Binding.Load(aColors), 1);
	for(int Count = 2; Count <= CMessageGradient::MAX_COLORS; ++Count)
	{
		SCOPED_TRACE(Count);
		aColors[Count - 1] = aColors[Count - 2];
		Binding.Store(aColors, Count);
		ASSERT_EQ(Binding.Load(aColors), Count);
		EXPECT_FLOAT_EQ(ColorHSLA(aColors[Count - 1], true).a, 64.0f / 255.0f);
		EXPECT_EQ(Binding.Opacity(), 50);
	}
}

TEST(QmColorGradient, CharacterSplitsKeepStopOpacityAndTheExistingTextOffset)
{
	CTextCursor Cursor;
	Cursor.m_CharCount = 5;
	CMessageGradient::AddTextSplits(Cursor, "a你b", "FF000000,0000FF80", ColorRGBA(1, 1, 1, 0.5f));
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 3u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, 5);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_CharIndex, 6);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_Length, 3);
	EXPECT_EQ(Cursor.m_vColorSplits[2].m_CharIndex, 9);
	EXPECT_FLOAT_EQ(Cursor.m_vColorSplits[0].m_Color.a, 0.0f);
	EXPECT_FLOAT_EQ(Cursor.m_vColorSplits[1].m_Color.a, (128.0f / 255.0f) * 0.25f);
	EXPECT_FLOAT_EQ(Cursor.m_vColorSplits[2].m_Color.a, (128.0f / 255.0f) * 0.5f);
	EXPECT_EQ(Cursor.m_CharCount, 5);
}
