#include <game/client/components/qmclient/chat_gradient.h>

#include <gtest/gtest.h>

#include <array>

TEST(QmChatGradient, EveryMessageCategoryReadsItsOwnPaletteAndGeometry)
{
	CConfig Config{};
	const std::array<char *, 6> apPalettes = {Config.m_ClMessageSystemGradient, Config.m_ClMessageClientGradient,
		Config.m_ClMessageHighlightGradient, Config.m_ClMessageTeamGradient, Config.m_ClMessageFriendGradient, Config.m_ClMessageGradient};
	const std::array<SQmGradientGeometryBinding, 6> aSettings = {{
		{&Config.m_QmChatSystemGradientType, &Config.m_QmChatSystemGradientAngle, &Config.m_QmChatSystemGradientCenterX, &Config.m_QmChatSystemGradientCenterY, &Config.m_QmChatSystemGradientRange, &Config.m_QmChatSystemGradientReverse},
		{&Config.m_QmChatClientGradientType, &Config.m_QmChatClientGradientAngle, &Config.m_QmChatClientGradientCenterX, &Config.m_QmChatClientGradientCenterY, &Config.m_QmChatClientGradientRange, &Config.m_QmChatClientGradientReverse},
		{&Config.m_QmChatHighlightGradientType, &Config.m_QmChatHighlightGradientAngle, &Config.m_QmChatHighlightGradientCenterX, &Config.m_QmChatHighlightGradientCenterY, &Config.m_QmChatHighlightGradientRange, &Config.m_QmChatHighlightGradientReverse},
		{&Config.m_QmChatTeamGradientType, &Config.m_QmChatTeamGradientAngle, &Config.m_QmChatTeamGradientCenterX, &Config.m_QmChatTeamGradientCenterY, &Config.m_QmChatTeamGradientRange, &Config.m_QmChatTeamGradientReverse},
		{&Config.m_QmChatFriendGradientType, &Config.m_QmChatFriendGradientAngle, &Config.m_QmChatFriendGradientCenterX, &Config.m_QmChatFriendGradientCenterY, &Config.m_QmChatFriendGradientRange, &Config.m_QmChatFriendGradientReverse},
		{&Config.m_QmChatNormalGradientType, &Config.m_QmChatNormalGradientAngle, &Config.m_QmChatNormalGradientCenterX, &Config.m_QmChatNormalGradientCenterY, &Config.m_QmChatNormalGradientRange, &Config.m_QmChatNormalGradientReverse},
	}};
	const std::array<const char *, 6> apColors = {"FF0000,FFFFFF", "00FF00,FFFFFF", "0000FF,FFFFFF",
		"FFFF00,FFFFFF", "00FFFF,FFFFFF", "FF00FF,FFFFFF"};
	const std::array<ColorRGBA, 6> aFirstColors = {ColorRGBA(1, 0, 0), ColorRGBA(0, 1, 0), ColorRGBA(0, 0, 1),
		ColorRGBA(1, 1, 0), ColorRGBA(0, 1, 1), ColorRGBA(1, 0, 1)};
	for(int i = 0; i < 6; ++i)
	{
		QmChatGradientBinding(Config, static_cast<EQmChatGradientRole>(i)).Reset();
		str_copy(apPalettes[i], apColors[i], sizeof(Config.m_ClMessageGradient));
		*aSettings[i].m_pType = i % 5;
		*aSettings[i].m_pAngle = 90;
		*aSettings[i].m_pCenterX = 30 + i * 10;
		*aSettings[i].m_pCenterY = 75 - i * 5;
		*aSettings[i].m_pRange = 125 + i * 5;
		*aSettings[i].m_pReverse = i % 2;
	}
	for(int i = 0; i < 6; ++i)
	{
		SCOPED_TRACE(i);
		const auto Value = QmChatGradientStyle(Config, static_cast<EQmChatGradientRole>(i), ColorRGBA(1, 1, 1, 0.5f));
		EXPECT_EQ(Value.m_Type, static_cast<EQmGradientType>(i % 5));
		EXPECT_FLOAT_EQ(Value.m_Direction.x, 0.0f);
		EXPECT_FLOAT_EQ(Value.m_Direction.y, 1.0f);
		EXPECT_FLOAT_EQ(Value.m_Center.x, (30 + i * 10) / 100.0f);
		EXPECT_FLOAT_EQ(Value.m_Center.y, (75 - i * 5) / 100.0f);
		EXPECT_FLOAT_EQ(Value.m_Range, (125 + i * 5) / 100.0f);
		EXPECT_EQ(Value.m_Reverse, i % 2 != 0);
		EXPECT_EQ(Value.m_NumColors, 2);
		EXPECT_NEAR(Value.m_aColors[0].r, aFirstColors[i].r, 0.02f);
		EXPECT_NEAR(Value.m_aColors[0].g, aFirstColors[i].g, 0.02f);
		EXPECT_NEAR(Value.m_aColors[0].b, aFirstColors[i].b, 0.02f);
		EXPECT_FLOAT_EQ(Value.m_aColors[0].a, 0.5f);
	}
}

TEST(QmChatGradient, ResettingOneCategoryLeavesTheOtherCategoriesAlone)
{
	CConfig Config{};
	Config.m_QmChatSystemGradientType = 2;
	Config.m_QmChatSystemGradientRange = 175;
	const auto Normal = QmChatGradientBinding(Config, EQmChatGradientRole::NORMAL);
	*Normal.m_pType = 4;
	*Normal.m_pAngle = 90;
	*Normal.m_pCenterX = 25;
	*Normal.m_pCenterY = 75;
	*Normal.m_pRange = 10;
	*Normal.m_pReverse = 1;
	Normal.Reset();
	EXPECT_EQ(Config.m_QmChatNormalGradientType, 0);
	EXPECT_EQ(Config.m_QmChatNormalGradientAngle, 0);
	EXPECT_EQ(Config.m_QmChatNormalGradientCenterX, 50);
	EXPECT_EQ(Config.m_QmChatNormalGradientCenterY, 50);
	EXPECT_EQ(Config.m_QmChatNormalGradientRange, 100);
	EXPECT_EQ(Config.m_QmChatNormalGradientReverse, 0);
	EXPECT_EQ(Config.m_QmChatSystemGradientType, 2);
	EXPECT_EQ(Config.m_QmChatSystemGradientRange, 175);
}

TEST(QmChatGradient, AnEmptyPalettePreservesTheConfiguredSingleColor)
{
	CConfig Config{};
	QmChatGradientBinding(Config, EQmChatGradientRole::NORMAL).Reset();
	const auto Value = QmChatGradientStyle(Config, EQmChatGradientRole::NORMAL, ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
	EXPECT_EQ(Value.m_NumColors, 1);
	EXPECT_FLOAT_EQ(Value.Sample(vec2(0.5f, 0.5f)).r, 0.2f);
	EXPECT_FLOAT_EQ(Value.Sample(vec2(0.5f, 0.5f)).a, 0.5f);
}

TEST(QmChatGradient, AShortMessageStartsItsGradientAfterTheNamePrefix)
{
	CTextCursor Start;
	Start.SetPosition(vec2(10, 100));
	Start.m_X = 40;
	CTextCursor Measured = Start;
	Measured.m_X = 110;
	Measured.m_HasVisualBoundingBox = true;
	Measured.m_VisualTop = 102;
	Measured.m_VisualBottom = 112;
	const CUIRect Bounds = QmChatGradientBounds(Start, Measured);
	EXPECT_FLOAT_EQ(Bounds.x, 40);
	EXPECT_FLOAT_EQ(Bounds.y, 102);
	EXPECT_FLOAT_EQ(Bounds.w, 70);
	EXPECT_FLOAT_EQ(Bounds.h, 10);
}

TEST(QmChatGradient, AWrappedMessageUsesOneGradientAcrossTheWholeTextBlock)
{
	CTextCursor Start;
	Start.SetPosition(vec2(10, 100));
	Start.m_X = 40;
	CTextCursor Measured = Start;
	Measured.m_LineCount = 3;
	Measured.m_LongestLineWidth = 100;
	Measured.m_HasVisualBoundingBox = true;
	Measured.m_VisualTop = 102;
	Measured.m_VisualBottom = 134;
	const CUIRect Bounds = QmChatGradientBounds(Start, Measured);
	EXPECT_FLOAT_EQ(Bounds.x, 10);
	EXPECT_FLOAT_EQ(Bounds.y, 102);
	EXPECT_FLOAT_EQ(Bounds.w, 100);
	EXPECT_FLOAT_EQ(Bounds.h, 32);
}

TEST(QmChatGradient, DegenerateTextBoundsRemainUsableForSampling)
{
	CTextCursor Start;
	Start.SetPosition(vec2(10, 20));
	const CUIRect Bounds = QmChatGradientBounds(Start, Start);
	EXPECT_FLOAT_EQ(Bounds.x, 10);
	EXPECT_FLOAT_EQ(Bounds.y, 20);
	EXPECT_GT(Bounds.w, 0);
	EXPECT_GT(Bounds.h, 0);
}

TEST(QmChatGradient, EveryMessageCategoryPreservesItsStopOpacity)
{
	CConfig Config{};
	const std::array<char *, 6> apPalettes = {Config.m_ClMessageSystemGradient, Config.m_ClMessageClientGradient,
		Config.m_ClMessageHighlightGradient, Config.m_ClMessageTeamGradient, Config.m_ClMessageFriendGradient, Config.m_ClMessageGradient};
	for(int i = 0; i < 6; ++i)
	{
		SCOPED_TRACE(i);
		const auto Role = static_cast<EQmChatGradientRole>(i);
		QmChatGradientBinding(Config, Role).Reset();
		str_copy(apPalettes[i], "FF000040,0000FFC0", sizeof(Config.m_ClMessageGradient));
		const auto Value = QmChatGradientStyle(Config, Role, ColorRGBA(1, 1, 1, 0.5f));
		EXPECT_FLOAT_EQ(Value.Sample(vec2(0, 0.5f)).a, (64.0f / 255.0f) * 0.5f);
		EXPECT_FLOAT_EQ(Value.Sample(vec2(1, 0.5f)).a, (192.0f / 255.0f) * 0.5f);
	}
}

TEST(QmChatGradient, ARemainingTransparentStopSamplesWithoutMeasuringText)
{
	CConfig Config{};
	QmChatGradientBinding(Config, EQmChatGradientRole::NORMAL).Reset();
	str_copy(Config.m_ClMessageGradient, "FF000080");
	const auto Value = QmChatGradientStyle(Config, EQmChatGradientRole::NORMAL, ColorRGBA(1, 1, 1, 0.5f));
	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	const auto PreviousSampler = +[](vec2, const void *) { return ColorRGBA(0, 1, 0, 1); };
	int PreviousContext = 0;
	Cursor.m_pfnColorSampler = PreviousSampler;
	Cursor.m_pColorSamplerContext = &PreviousContext;
	Cursor.m_ColorSamplerColumns = 3;
	Cursor.m_ColorSamplerRows = 4;
	{
		const CQmChatGradientPaint Paint(nullptr, Cursor, "text", &Value);
		ASSERT_NE(Cursor.m_pfnColorSampler, nullptr);
		ASSERT_NE(Cursor.m_pfnColorSampler, PreviousSampler);
		EXPECT_EQ(Cursor.m_ColorSamplerColumns, 1);
		EXPECT_EQ(Cursor.m_ColorSamplerRows, 1);
		for(vec2 Point : {vec2(0, 0), vec2(500, 100)})
		{
			const auto Color = Cursor.m_pfnColorSampler(Point, Cursor.m_pColorSamplerContext);
			EXPECT_NEAR(Color.r, 1.0f, 0.02f);
			EXPECT_FLOAT_EQ(Color.a, (128.0f / 255.0f) * 0.5f);
		}
	}
	EXPECT_EQ(Cursor.m_pfnColorSampler, PreviousSampler);
	EXPECT_EQ(Cursor.m_pColorSamplerContext, &PreviousContext);
	EXPECT_EQ(Cursor.m_ColorSamplerColumns, 3);
	EXPECT_EQ(Cursor.m_ColorSamplerRows, 4);
}

TEST(QmChatGradient, EmptyAndInvalidPalettesKeepTheExistingTextSampler)
{
	for(const char *pPalette : {"", "invalid"})
	{
		SCOPED_TRACE(pPalette);
		CConfig Config{};
		str_copy(Config.m_ClMessageGradient, pPalette);
		const auto Value = QmChatGradientStyle(Config, EQmChatGradientRole::NORMAL, ColorRGBA(1, 1, 1, 0.5f));
		CTextCursor Cursor;
		Cursor.m_Flags = TEXTFLAG_RENDER;
		const CQmChatGradientPaint Paint(nullptr, Cursor, "text", &Value);
		EXPECT_EQ(Cursor.m_pfnColorSampler, nullptr);
		EXPECT_EQ(Cursor.m_pColorSamplerContext, nullptr);
	}
}

TEST(QmChatGradient, NestedSingleStopPaintRestoresTheOuterOpacity)
{
	const auto Outer = SQmColorGradient::FromConfig("FF000080", ColorRGBA(1, 1, 1, 1), 0, 0, 50, 50, 100, false);
	const auto Inner = SQmColorGradient::FromConfig("0000FF00", ColorRGBA(1, 1, 1, 1), 0, 0, 50, 50, 100, false);
	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	{
		const CQmChatGradientPaint OuterPaint(nullptr, Cursor, "outer", &Outer);
		ASSERT_NE(Cursor.m_pfnColorSampler, nullptr);
		const void *pOuterContext = Cursor.m_pColorSamplerContext;
		{
			const CQmChatGradientPaint InnerPaint(nullptr, Cursor, "inner", &Inner);
			ASSERT_NE(Cursor.m_pfnColorSampler, nullptr);
			EXPECT_FLOAT_EQ(Cursor.m_pfnColorSampler(vec2(0, 0), Cursor.m_pColorSamplerContext).a, 0.0f);
		}
		EXPECT_EQ(Cursor.m_pColorSamplerContext, pOuterContext);
		EXPECT_FLOAT_EQ(Cursor.m_pfnColorSampler(vec2(0, 0), Cursor.m_pColorSamplerContext).a, 128.0f / 255.0f);
	}
	EXPECT_EQ(Cursor.m_pfnColorSampler, nullptr);
	EXPECT_EQ(Cursor.m_pColorSamplerContext, nullptr);
}
