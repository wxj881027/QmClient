#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/QmImeAppearance.h>

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
