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

TEST(QmTitleBadge, SixChineseGlyphsMatchTheDesignGeometry)
{
	const vec2 Size = QmTitleBadgeSize(78.0f, 13.0f);
	EXPECT_FLOAT_EQ(Size.x, 117.0f);
	EXPECT_FLOAT_EQ(Size.y, 24.0f);
	const vec2 Large = QmTitleBadgeSize(234.0f, 39.0f);
	EXPECT_FLOAT_EQ(Large.x, 351.0f);
	EXPECT_FLOAT_EQ(Large.y, 72.0f);
}

TEST(QmTitleBadge, ShortTextKeepsTheIconAndPadding)
{
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(6.5f, 13.0f).x, 45.5f);
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(0.0f, 13.0f).x, 39.0f);
	EXPECT_FLOAT_EQ(QmTitleBadgeSize(-1.0f, -1.0f).y, 0.0f);
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

TEST(QmTitleBadge, InlinePlacementKeepsTheWholeCapsuleBeforeTheNickname)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_FontSize = 24.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 78.0f);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Rect.y, 20.0f);
	EXPECT_FLOAT_EQ(Rect.w, 117.0f);
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
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 78.0f);
	EXPECT_EQ(Cursor.m_LineCount, 2);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Cursor.m_Y, 48.0f);
}

TEST(QmTitleBadge, NarrowLineScalesTheWholeCapsuleProportionally)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 24.0f;
	Cursor.m_LineWidth = 30.0f;
	const CUIRect Rect = QmLayoutTitleBadge(Cursor, 78.0f);
	EXPECT_LE(Rect.w, 30.0f);
	EXPECT_NEAR(Rect.w / Rect.h, 117.0f / 24.0f, 0.00001f);
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

TEST(QmTitleBadge, InvalidOrMissingAppearanceFallsBackAsOneUnit)
{
	for(const char *pJson : {"null", "[]", R"({"style":"sun","color":-1,"opacity":80})",
		    R"({"style":"sun","color":16777216,"opacity":80})", R"({"style":"moon","color":1,"opacity":101})",
		    R"({"style":"unknown","color":1,"opacity":80})", R"({"style":"sun","color":1.5,"opacity":80})",
		    R"({"style":"sun","color":1})"})
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
		{"server_address":"test:8303","player_id":3,"player_name":"Main","title":"自选文字","appearance":{"style":"sun","color":1193046,"opacity":35},"issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":4,"player_name":"Dummy","title":"Legacy","issued_at":1000,"expires_at":1015}
	]})");
	const auto Presences = ParseQmTitlePresences(Json.get(), "test:8303");
	ASSERT_EQ(Presences.size(), 2u);
	EXPECT_EQ(Presences[0].m_Appearance, (SQmTitleAppearance{EQmTitleBadgeStyle::SUN, 0x123456, 35}));
	EXPECT_EQ(Presences[0].m_Title, "自选文字");
	EXPECT_EQ(Presences[1].m_Appearance, SQmTitleAppearance{});
	EXPECT_TRUE(ParseQmTitlePresences(Json.get(), "other:8303").empty());
}
