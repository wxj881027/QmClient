#include <engine/shared/json.h>

#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/sponsor_chat_render.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

TEST(QmSponsorChatStyle, StableIdsRoundTripAndUnknownIdsUseNormalText)
{
	for(const auto Style : {EQmSponsorChatStyle::NONE, EQmSponsorChatStyle::SOFT_GLOW, EQmSponsorChatStyle::PLATINUM})
		EXPECT_EQ(QmSponsorChatStyleFromId(QmSponsorChatStyleId(Style)), Style);
	for(const char *pId : std::array<const char *, 4>{nullptr, "unknown", "rainbow", "Soft_Glow"})
		EXPECT_EQ(QmSponsorChatStyleFromId(pId), EQmSponsorChatStyle::NONE);
}

TEST(QmSponsorChatStyle, OrdinaryMessagesKeepTheSelectedStyle)
{
	for(const auto Style : {EQmSponsorChatStyle::SOFT_GLOW, EQmSponsorChatStyle::PLATINUM})
		EXPECT_EQ(QmSponsorChatMessageStyle(Style, false, false, false, false, false), Style);
}

class CQmSponsorChatSemanticTest : public testing::TestWithParam<int>
{
};

TEST_P(CQmSponsorChatSemanticTest, FunctionalMessagePresentationTakesPriority)
{
	const int Flag = GetParam();
	for(const auto Style : {EQmSponsorChatStyle::SOFT_GLOW, EQmSponsorChatStyle::PLATINUM})
		EXPECT_EQ(QmSponsorChatMessageStyle(Style, Flag == 0, Flag == 1, Flag == 2, Flag == 3, Flag == 4), EQmSponsorChatStyle::NONE);
}

INSTANTIATE_TEST_SUITE_P(HighlightTeamWhisperCustomColorEmoji, CQmSponsorChatSemanticTest, testing::Range(0, 5));

TEST(QmSponsorChatStyle, MergingKeepsOnlyAStyleSharedByEveryMessage)
{
	const auto Glow = EQmSponsorChatStyle::SOFT_GLOW;
	const auto Platinum = EQmSponsorChatStyle::PLATINUM;
	EXPECT_EQ(QmSponsorChatMergedStyle(Glow, Glow), Glow);
	EXPECT_EQ(QmSponsorChatMergedStyle(Platinum, Platinum), Platinum);
	const auto Mixed = QmSponsorChatMergedStyle(Glow, Platinum);
	EXPECT_EQ(Mixed, EQmSponsorChatStyle::NONE);
	EXPECT_EQ(QmSponsorChatMergedStyle(Mixed, Glow), EQmSponsorChatStyle::NONE);
	EXPECT_EQ(QmSponsorChatMergedStyle(Platinum, EQmSponsorChatStyle::NONE), EQmSponsorChatStyle::NONE);
}

TEST(QmSponsorChatStyle, GlowStaysNarrowAtLargeFontSizesAndRejectsInvalidSizes)
{
	EXPECT_GT(QmSponsorChatGlowRadius(20.0f), QmSponsorChatGlowRadius(8.0f));
	EXPECT_LE(QmSponsorChatGlowRadius(200.0f), 2.5f);
	EXPECT_GT(QmSponsorChatGlowRadius(1.0f), 0.0f);
	for(float Size : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		EXPECT_FLOAT_EQ(QmSponsorChatGlowRadius(Size), 0.0f);
}

TEST(QmSponsorChatStyle, PlatinumHasABrightLowSaturationBandAndPreservesOpacity)
{
	const ColorRGBA Peak = QmSponsorChatPlatinumColor(0.64f, 0.35f);
	EXPECT_LT(std::max({Peak.r, Peak.g, Peak.b}), 0.9f);
	for(float Position : {0.0f, 0.25f, 0.5f, 0.64f, 0.8f, 1.0f})
	{
		const ColorRGBA Color = QmSponsorChatPlatinumColor(Position, 0.35f);
		EXPECT_LE(std::max({Color.r, Color.g, Color.b}) - std::min({Color.r, Color.g, Color.b}), 0.1f);
		EXPECT_FLOAT_EQ(Color.a, 0.35f);
	}
	for(float Edge : {0.0f, 1.0f})
	{
		const ColorRGBA Color = QmSponsorChatPlatinumColor(Edge, 1.0f);
		EXPECT_GT(Peak.r + Peak.g + Peak.b, Color.r + Color.g + Color.b);
	}
}

TEST(QmSponsorChatStyle, PlatinumClampsPositionsWithoutInvalidColors)
{
	EXPECT_EQ(QmSponsorChatPlatinumColor(-1.0f, 1.0f), QmSponsorChatPlatinumColor(0.0f, 1.0f));
	EXPECT_EQ(QmSponsorChatPlatinumColor(2.0f, 1.0f), QmSponsorChatPlatinumColor(1.0f, 1.0f));
	const ColorRGBA Invalid = QmSponsorChatPlatinumColor(std::numeric_limits<float>::quiet_NaN(), 1.0f);
	EXPECT_TRUE(std::isfinite(Invalid.r) && std::isfinite(Invalid.g) && std::isfinite(Invalid.b));
}

TEST(QmSponsorChatStyle, PlatinumSplitsUseUtf8ByteOffsetsAfterTheHeader)
{
	CTextCursor Cursor;
	Cursor.m_CharCount = 11;
	Cursor.SetPosition(vec2(25.0f, 40.0f));
	QmSponsorChatAddPlatinumSplits(Cursor, "A中\n文", 0.6f);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 3u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, 11);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Length, 1);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_CharIndex, 12);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_Length, 3);
	EXPECT_EQ(Cursor.m_vColorSplits[2].m_CharIndex, 16);
	EXPECT_EQ(Cursor.m_vColorSplits[2].m_Length, 3);
	for(const auto &Split : Cursor.m_vColorSplits)
		EXPECT_FLOAT_EQ(Split.m_Color.a, 0.6f);
	EXPECT_EQ(Cursor.m_CharCount, 11);
	EXPECT_FLOAT_EQ(Cursor.m_X, 25.0f);
	EXPECT_FLOAT_EQ(Cursor.m_Y, 40.0f);
}

TEST(QmSponsorChatStyle, SingleCharacterGetsTheHighlightAndEmptyTextAddsNothing)
{
	CTextCursor Cursor;
	QmSponsorChatAddPlatinumSplits(Cursor, "\n", 1.0f);
	QmSponsorChatAddPlatinumSplits(Cursor, "", 1.0f);
	EXPECT_TRUE(Cursor.m_vColorSplits.empty());
	QmSponsorChatAddPlatinumSplits(Cursor, "中", 1.0f);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Color, QmSponsorChatPlatinumColor(0.64f, 1.0f));
}

TEST(QmSponsorChatSweep, NewMessagesBeginWithAOneSecondFirstLine)
{
	for(bool Latest : {false, true})
	{
		for(double Age : {0.0, 0.5, 0.99})
		{
			const auto State = QmSponsorChatSweepState(Age, 1, Latest);
			EXPECT_EQ(State.m_Line, 0);
			EXPECT_FLOAT_EQ(State.m_Progress, static_cast<float>(Age));
		}
		EXPECT_EQ(QmSponsorChatSweepState(1.0, 1, Latest).m_Line, -1);
	}
}

TEST(QmSponsorChatSweep, SingleLinePausesForOneSecondBeforeRestarting)
{
	for(double Age : {1.0, 1.5, 1.99, 3.0, 3.99})
		EXPECT_EQ(QmSponsorChatSweepState(Age, 1, true).m_Line, -1);
	const auto Restart = QmSponsorChatSweepState(2.0, 1, true);
	EXPECT_EQ(Restart.m_Line, 0);
	EXPECT_FLOAT_EQ(Restart.m_Progress, 0.0f);
	for(double Age : {2.5, 4.5, 6000.5})
	{
		const auto State = QmSponsorChatSweepState(Age, 1, true);
		EXPECT_EQ(State.m_Line, 0);
		EXPECT_FLOAT_EQ(State.m_Progress, 0.5f);
	}
}

TEST(QmSponsorChatSweep, MultilineSweepsTopToBottomWithoutPausingBetweenLines)
{
	const auto First = QmSponsorChatSweepState(0.5, 3, true);
	EXPECT_EQ(First.m_Line, 0);
	EXPECT_FLOAT_EQ(First.m_Progress, 0.5f);
	const auto SecondStart = QmSponsorChatSweepState(1.0, 3, true);
	EXPECT_EQ(SecondStart.m_Line, 1);
	EXPECT_FLOAT_EQ(SecondStart.m_Progress, 0.0f);
	const auto Second = QmSponsorChatSweepState(1.5, 3, true);
	EXPECT_EQ(Second.m_Line, 1);
	EXPECT_FLOAT_EQ(Second.m_Progress, 0.5f);
	const auto Third = QmSponsorChatSweepState(2.5, 3, true);
	EXPECT_EQ(Third.m_Line, 2);
	EXPECT_FLOAT_EQ(Third.m_Progress, 0.5f);
}

TEST(QmSponsorChatSweep, MultilinePausesOnlyAfterTheFinalLine)
{
	for(double Age : {3.0, 3.5, 3.99})
		EXPECT_EQ(QmSponsorChatSweepState(Age, 3, true).m_Line, -1);
	const auto Restart = QmSponsorChatSweepState(4.0, 3, true);
	EXPECT_EQ(Restart.m_Line, 0);
	EXPECT_FLOAT_EQ(Restart.m_Progress, 0.0f);
	EXPECT_EQ(QmSponsorChatSweepState(5.5, 3, true).m_Line, 1);
}

TEST(QmSponsorChatSweep, OlderMessagesFinishTheInitialPassWithoutRepeating)
{
	const auto LastLine = QmSponsorChatSweepState(2.5, 3, false);
	EXPECT_EQ(LastLine.m_Line, 2);
	EXPECT_FLOAT_EQ(LastLine.m_Progress, 0.5f);
	for(double Age : {3.0, 4.5, 5.5})
		EXPECT_EQ(QmSponsorChatSweepState(Age, 3, false).m_Line, -1);
}

TEST(QmSponsorChatSweep, OldMessagesCanReplayWhenTheyBecomeTheLatestVisibleSponsor)
{
	EXPECT_EQ(QmSponsorChatSweepState(6000.5, 3, false).m_Line, -1);
	const auto Visible = QmSponsorChatSweepState(6000.5, 3, true);
	EXPECT_EQ(Visible.m_Line, 0);
	EXPECT_FLOAT_EQ(Visible.m_Progress, 0.5f);
}

TEST(QmSponsorChatSweep, ReflowUsesTheCurrentLineCount)
{
	EXPECT_EQ(QmSponsorChatSweepState(2.5, 3, true).m_Line, 2);
	const auto Reflowed = QmSponsorChatSweepState(2.5, 1, true);
	EXPECT_EQ(Reflowed.m_Line, 0);
	EXPECT_FLOAT_EQ(Reflowed.m_Progress, 0.5f);
}

TEST(QmSponsorChatSweep, InvalidAgesAndEmptyLayoutsDoNotSelectALine)
{
	for(double Age : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
		EXPECT_EQ(QmSponsorChatSweepState(Age, 1, true).m_Line, -1);
	for(int LineCount : {0, -1})
		EXPECT_EQ(QmSponsorChatSweepState(0.5, LineCount, true).m_Line, -1);
}

namespace
{
	std::vector<SQmTitlePresence> ReadPresences(const char *pText)
	{
		const std::unique_ptr<json_value, decltype(&json_value_free)> pJson(json_parse(pText, str_length(pText)), json_value_free);
		EXPECT_NE(pJson, nullptr);
		return ParseQmTitlePresences(pJson.get(), "test:8303", nullptr);
	}
}

TEST(QmSponsorChatPresence, BodyStyleIsIndependentFromTheTitleStyle)
{
	const auto Presences = ReadPresences(R"({"server_time":1000,"presences":[
		{"server_address":"test:8303","player_id":3,"player_name":"Main","title":"Sponsor","style":"eternity","chat_style":"soft_glow","issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":4,"player_name":"Dummy","title":"Sponsor","style":"eternity","chat_style":"platinum","issued_at":1000,"expires_at":1015}
	]})");
	ASSERT_EQ(Presences.size(), 2u);
	EXPECT_EQ(Presences[0].m_ChatStyle, EQmSponsorChatStyle::SOFT_GLOW);
	EXPECT_EQ(Presences[1].m_ChatStyle, EQmSponsorChatStyle::PLATINUM);
	EXPECT_EQ(Presences[0].m_Style, "eternity");
	EXPECT_EQ(Presences[1].m_PlayerId, 4);
	EXPECT_EQ(Presences[1].m_PlayerName, "Dummy");
}

TEST(QmSponsorChatPresence, MissingUnknownAndWrongTypeStylesKeepNormalTitles)
{
	const auto Presences = ReadPresences(R"({"server_time":1000,"presences":[
		{"server_address":"test:8303","player_id":3,"player_name":"A","title":"Sponsor","issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":4,"player_name":"B","title":"Sponsor","chat_style":"future_style","issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":5,"player_name":"C","title":"Sponsor","chat_style":1,"issued_at":1000,"expires_at":1015}
	]})");
	ASSERT_EQ(Presences.size(), 3u);
	for(const auto &Presence : Presences)
	{
		EXPECT_EQ(Presence.m_ChatStyle, EQmSponsorChatStyle::NONE);
		EXPECT_EQ(Presence.m_Title, "Sponsor");
	}
}

TEST(QmSponsorChatPresence, StyleDoesNotBypassLeaseOrServerValidation)
{
	const auto Presences = ReadPresences(R"({"server_time":1000,"presences":[
		{"server_address":"other:8303","player_id":3,"player_name":"A","title":"Sponsor","chat_style":"soft_glow","issued_at":1000,"expires_at":1015},
		{"server_address":"test:8303","player_id":4,"player_name":"B","title":"Sponsor","chat_style":"platinum","issued_at":900,"expires_at":1000},
		{"server_address":"test:8303","player_id":5,"player_name":"C","title":"Sponsor","chat_style":"platinum","issued_at":1001,"expires_at":1015}
	]})");
	EXPECT_TRUE(Presences.empty());
}
