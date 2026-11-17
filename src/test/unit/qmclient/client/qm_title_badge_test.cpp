#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_title_badge.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	using CJson = std::unique_ptr<json_value, decltype(&json_value_free)>;
	CJson ReadJson(const char *pText)
	{
		return CJson(json_parse(pText, str_length(pText)), json_value_free);
	}
}

TEST(QmTitleBadge, CustomTextKeepsContentAndRemovesLegacyBrackets)
{
	EXPECT_EQ(QmTitleBadgeText("[我的头衔]"), "我的头衔");
	EXPECT_EQ(QmTitleBadgeText("Own title"), "Own title");
	EXPECT_EQ(QmTitleBadgeText(nullptr), "");
}

TEST(QmTitleBadge, LargerLabelKeepsTheCapsuleHeight)
{
	EXPECT_FLOAT_EQ(QmTitleBadgeLabelFontSize(24.0f), 16.0f);
	const vec2 Size = QmTitleBadgeSize(96.0f, 16.0f);
	EXPECT_FLOAT_EQ(Size.x, 135.0f);
	EXPECT_FLOAT_EQ(Size.y, 24.0f);
	const vec2 Large = QmTitleBadgeSize(288.0f, 48.0f);
	EXPECT_FLOAT_EQ(Large.x, 405.0f);
	EXPECT_FLOAT_EQ(Large.y, 72.0f);
}

TEST(QmTitleBadge, PlainTextWorksWithBothRawProfilesAndBracketedChatTitles)
{
	SQmTitleAppearance Text;
	Text.m_TextStyle = 3;
	EXPECT_EQ(QmTitleDisplayText("猫", Text), "[猫]");
	EXPECT_EQ(QmTitleDisplayText("[猫]", Text), "[猫]");
	EXPECT_EQ(QmTitleDisplayText("[猫]", {}), "猫");
	EXPECT_TRUE(QmTitleDisplayText("", Text).empty());
}

TEST(QmTitleBadge, TextStylesKeepTheirPaletteWhenLocalWaveChanges)
{
	const unsigned aFirstColors[] = {0xff3636, 0x811d95, 0xffdc16, 0x00ffc8, 0x83efff};
	for(int Style = 1; Style <= 5; ++Style)
	{
		SCOPED_TRACE(Style);
		CTextCursor Still, Wave;
		Still.m_FontSize = Wave.m_FontSize = 12.0f;
		QmPrepareTitleTextCursor(Still, "[猫A]", Style, false, 0.0f);
		QmPrepareTitleTextCursor(Wave, "[猫A]", Style, true, 0.5f);
		ASSERT_FALSE(Still.m_vColorSplits.empty());
		EXPECT_EQ(Still.m_vColorSplits.front().m_Color.Pack(false), aFirstColors[Style - 1]);
		if(Style == 5)
			EXPECT_EQ(Still.m_vColorSplits.back().m_ColorEnd.Pack(false), 0x2437e6u);
		ASSERT_EQ(Still.m_vColorSplits.size(), Wave.m_vColorSplits.size());
		for(size_t i = 0; i < Still.m_vColorSplits.size(); ++i)
		{
			EXPECT_EQ(Still.m_vColorSplits[i].m_Color, Wave.m_vColorSplits[i].m_Color);
			EXPECT_EQ(Still.m_vColorSplits[i].m_ColorEnd, Wave.m_vColorSplits[i].m_ColorEnd);
		}
		EXPECT_TRUE(Still.m_vCharOffsets.empty());
		ASSERT_EQ(Wave.m_vCharOffsets.size(), 4u);
		EXPECT_EQ(Wave.m_vCharOffsets[2].m_CharIndex, 4);
		EXPECT_NE(Wave.m_vCharOffsets[0].m_YOffset, Wave.m_vCharOffsets[1].m_YOffset);
	}
}

TEST(QmTitleBadge, NameplateScaleChangesTheWholeCapsuleAndCanReturnToDefault)
{
	const float NameFontSize = 30.0f;
	for(const int Scale : {100, 50, 200, 125, 100})
	{
		SCOPED_TRACE(Scale);
		const float Height = QmNameplateTitleHeight(NameFontSize, Scale);
		const float LabelFontSize = QmTitleBadgeLabelFontSize(Height);
		const vec2 Size = QmTitleBadgeSize(LabelFontSize * 4.0f, LabelFontSize);
		EXPECT_FLOAT_EQ(Size.y, 24.0f * Scale / 100.0f);
		EXPECT_FLOAT_EQ(Size.x, 103.0f * Scale / 100.0f);
	}
}

TEST(QmTitleBadge, NameplateScaleClampsToSupportedRange)
{
	EXPECT_FLOAT_EQ(QmNameplateTitleHeight(30.0f, 0), 12.0f);
	EXPECT_FLOAT_EQ(QmNameplateTitleHeight(30.0f, 300), 48.0f);
	EXPECT_FLOAT_EQ(QmNameplateTitleHeight(-1.0f, 100), 0.0f);
}

TEST(QmTitleBadge, ShortTextKeepsTheIconAndPadding)
{
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(8.0f, 16.0f).x, 47.0f);
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(0.0f, 16.0f).x, 39.0f);
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(-1.0f, -1.0f).y, 0.0f);
}

TEST(QmTitleBadge, VisibleGlyphsAreCenteredDespiteDifferentFontBearings)
{
	const CUIRect Rect{10.0f, 20.0f, 135.0f, 24.0f};
	for(const CUIRect Bounds : {CUIRect{0.0f, 2.0f, 96.0f, 12.0f}, CUIRect{1.0f, 5.0f, 38.0f, 8.0f}, CUIRect{-1.0f, 3.0f, 48.0f, 16.0f}})
	{
		SCOPED_TRACE(::testing::Message() << "Bearing " << Bounds.x << ", " << Bounds.y);
		const vec2 Position = QmTitleBadgeTextPosition(Rect, Bounds);
		EXPECT_FLOAT_EQ(Position.x + Bounds.x + Bounds.w * 0.5f, 88.0f);
		EXPECT_FLOAT_EQ(Position.y + Bounds.y + Bounds.h * 0.5f, 32.0f);
	}
}

TEST(QmTitleBadge, ScaledCapsuleKeepsGlyphsCenteredInItsTextArea)
{
	const CUIRect Bounds{0.5f, 1.0f, 47.0f, 6.0f};
	const vec2 Position = QmTitleBadgeTextPosition({5.0f, 7.0f, 67.5f, 12.0f}, Bounds);
	EXPECT_FLOAT_EQ(Position.x + Bounds.x + Bounds.w * 0.5f, 44.0f);
	EXPECT_FLOAT_EQ(Position.y + Bounds.y + Bounds.h * 0.5f, 13.0f);
}

TEST(QmTitleBadge, VisualBoundsAreRelativeToTheBakedTextOrigin)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_HasVisualBoundingBox = true;
	Cursor.m_VisualLeft = 12.0f;
	Cursor.m_VisualRight = 62.0f;
	Cursor.m_VisualTop = 23.0f;
	Cursor.m_VisualBottom = 37.0f;
	const CUIRect Bounds = QmTitleBadgeVisualBounds(Cursor);
	EXPECT_FLOAT_EQ(Bounds.x, 2.0f);
	EXPECT_FLOAT_EQ(Bounds.y, 3.0f);
	EXPECT_FLOAT_EQ(Bounds.w, 50.0f);
	EXPECT_FLOAT_EQ(Bounds.h, 14.0f);
}

TEST(QmTitleBadge, DefaultMoonPaletteMatchesDarkFigmaVariant)
{
	const auto Palette = QmTitleBadgePalette({});
	EXPECT_NEAR(Palette.m_Background.r, 0x35 / 255.0f, 0.00001f);
	EXPECT_NEAR(Palette.m_Text.g, 0xd8 / 255.0f, 0.00001f);
	EXPECT_NEAR(Palette.m_Icon.b, 0xe2 / 255.0f, 0.00001f);
}

TEST(QmTitleBadge, DefaultSunPaletteMatchesDarkFigmaVariant)
{
	const auto Palette = QmTitleBadgePalette({EQmTitleBadgeStyle::SUN, 0xbd862d, 100});
	EXPECT_NEAR(Palette.m_Background.r, 0x44 / 255.0f, 0.00001f);
	EXPECT_NEAR(Palette.m_Text.g, 0xe6 / 255.0f, 0.00001f);
	EXPECT_NEAR(Palette.m_Icon.b, 0x6d / 255.0f, 0.00001f);
}

TEST(QmTitleBadge, OpacityAndSpectatorFadeApplyToEveryLayer)
{
	SQmTitleAppearance Appearance;
	Appearance.m_Opacity = 40;
	const auto Palette = QmTitleBadgePalette(Appearance, 0.5f);
	for(const auto &Color : {Palette.m_Background, Palette.m_Border, Palette.m_Text, Palette.m_Icon})
		EXPECT_FLOAT_EQ(Color.a, 0.2f);
	Appearance.m_Opacity = 0;
	EXPECT_FLOAT_EQ(QmTitleBadgePalette(Appearance).m_Text.a, 0.0f);
}

TEST(QmTitleBadge, NeutralColorChangesAllLayersWithoutIntroducingHue)
{
	const auto Palette = QmTitleBadgePalette({EQmTitleBadgeStyle::MOON, 0x808080, 100});
	for(const auto &Color : {Palette.m_Background, Palette.m_Border, Palette.m_Text, Palette.m_Icon})
	{
		EXPECT_FLOAT_EQ(Color.r, Color.g);
		EXPECT_FLOAT_EQ(Color.g, Color.b);
	}
}

TEST(QmTitleBadge, TextStyleFitsBesideTheNameWithoutCapsulePadding)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 12.0f;
	Cursor.m_LineWidth = 70.0f;
	Cursor.m_X = 28.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 36.0f, true);
	EXPECT_EQ(Cursor.m_LineCount, 1);
	EXPECT_FLOAT_EQ(Rect.x, 28.0f);
	EXPECT_FLOAT_EQ(Rect.w, 36.0f);
	EXPECT_FLOAT_EQ(Rect.h, 12.0f);
	EXPECT_FLOAT_EQ(Cursor.m_X, 65.8f);
}

TEST(QmTitleBadge, InlinePlacementKeepsTheWholeCapsuleBeforeTheNickname)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_FontSize = 24.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 96.0f);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Rect.y, 20.0f);
	EXPECT_FLOAT_EQ(Rect.w, 135.0f);
	EXPECT_FLOAT_EQ(Rect.h, 24.0f);
	EXPECT_GT(Cursor.m_X, Rect.x + Rect.w);
}

TEST(QmTitleBadge, InsufficientWidthWrapsTheWholeCapsule)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_X = 80.0f;
	Cursor.m_LineWidth = 150.0f;
	Cursor.m_FontSize = 24.0f;
	Cursor.m_AlignedFontSize = 26.0f;
	Cursor.m_AlignedLineSpacing = 2.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 96.0f);
	EXPECT_EQ(Cursor.m_LineCount, 2);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Cursor.m_Y, 48.0f);
}

TEST(QmTitleBadge, NarrowLineScalesTheWholeCapsuleProportionally)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 24.0f;
	Cursor.m_LineWidth = 30.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 96.0f);
	EXPECT_LE(Rect.w, 30.0f);
	EXPECT_NEAR(Rect.w / Rect.h, 135.0f / 24.0f, 0.00001f);
	EXPECT_FLOAT_EQ(Cursor.m_X, 30.0f);
}

TEST(QmTitleBadge, ServerProfilePreservesBlackAndFullyTransparentAppearance)
{
	const auto Json = ReadJson(R"({"style":"sun","color":0,"opacity":0})");
	SQmTitleAppearance Appearance;
	ASSERT_TRUE(ParseQmTitleAppearance(Json.get(), Appearance));
	EXPECT_EQ(Appearance.m_Style, EQmTitleBadgeStyle::SUN);
	EXPECT_EQ(Appearance.m_Color, 0u);
	EXPECT_EQ(Appearance.m_Opacity, 0);
}

TEST(QmTitleBadge, ProfilesKeepLegacyCapsulesAndDetectTextStyleSupport)
{
	SQmTitleAppearance Appearance;
	bool TextStyleSupported = true;
	const auto Legacy = ReadJson(R"({"style":"sun","color":0,"opacity":0})");
	ASSERT_TRUE(ParseQmTitleAppearance(Legacy.get(), Appearance, &TextStyleSupported));
	EXPECT_FALSE(TextStyleSupported);
	EXPECT_EQ(Appearance.m_TextStyle, 0);
	for(const int Style : {0, 1, 2, 3, 4, 5})
	{
		SCOPED_TRACE(Style);
		char aJson[128];
		str_format(aJson, sizeof(aJson), R"({"style":"sun","color":0,"opacity":0,"text_style":%d})", Style);
		const auto Profile = ReadJson(aJson);
		ASSERT_TRUE(ParseQmTitleAppearance(Profile.get(), Appearance, &TextStyleSupported));
		EXPECT_TRUE(TextStyleSupported);
		EXPECT_EQ(Appearance.m_TextStyle, Style);
		EXPECT_EQ(Appearance.m_Style, EQmTitleBadgeStyle::SUN);
	}
}

TEST(QmTitleBadge, InvalidOrMissingAppearanceFallsBackAsOneUnit)
{
	for(const char *pJson : {"null", "[]", R"({"style":"sun","color":-1,"opacity":80})",
		    R"({"style":"sun","color":16777216,"opacity":80})", R"({"style":"moon","color":1,"opacity":101})",
		    R"({"style":"unknown","color":1,"opacity":80})", R"({"style":"sun","color":1.5,"opacity":80})",
		    R"({"style":"sun","color":1})", R"({"style":"moon","color":1,"opacity":80,"text_style":-1})",
		    R"({"style":"moon","color":1,"opacity":80,"text_style":6})", R"({"style":"sun","color":1,"opacity":80,"text_style":"1"})",
		    R"({"style":"sun","color":1,"opacity":80,"text_style":null})"})
	{
		SCOPED_TRACE(pJson);
		const auto Json = ReadJson(pJson);
		SQmTitleAppearance Appearance{EQmTitleBadgeStyle::SUN, 0, 0};
		EXPECT_FALSE(ParseQmTitleAppearance(Json.get(), Appearance));
		EXPECT_EQ(Appearance, SQmTitleAppearance{});
	}
}

TEST(QmTitleBadge, PresenceCarriesAppearanceForMainAndDummy)
{
	const auto Json = ReadJson(R"({"server_time":1000,"presences":[
		{"server_address":"test:8303","player_id":3,"player_name":"Main","title":"自选文字","appearance":{"style":"sun","color":1193046,"opacity":35,"text_style":5},"issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":4,"player_name":"Dummy","title":"Legacy","issued_at":1000,"expires_at":1015}
	]})");
	const auto Presences = ParseQmTitlePresences(Json.get(), "test:8303");
	ASSERT_EQ(Presences.size(), 2u);
	EXPECT_EQ(Presences[0].m_Appearance, (SQmTitleAppearance{EQmTitleBadgeStyle::SUN, 0x123456, 35, 5}));
	EXPECT_EQ(Presences[0].m_Title, "自选文字");
	EXPECT_EQ(Presences[1].m_Appearance, SQmTitleAppearance{});
	EXPECT_TRUE(ParseQmTitlePresences(Json.get(), "other:8303").empty());
}
