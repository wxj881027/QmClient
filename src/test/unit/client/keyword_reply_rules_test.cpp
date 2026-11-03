#include <game/client/components/qmclient/keyword_reply_rules.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

TEST(KeywordReplyRules, EachReplyUsesOneSlotInTheSameRandomRange)
{
	const char *apExpected[] = {"A", "B", "C", "D"};
	for(int Index = 0; Index < 4; ++Index)
	{
		SCOPED_TRACE(Index);
		char aReply[32];
		int RandomCalls = 0;
		EXPECT_TRUE(QmKeywordReplyRules::PickReply("A|B|C|D", aReply, sizeof(aReply), [&](int Count) {
			EXPECT_EQ(Count, 4);
			++RandomCalls;
			return Index;
		}));
		EXPECT_STREQ(aReply, apExpected[Index]);
		EXPECT_EQ(RandomCalls, 1);
	}
}

TEST(KeywordReplyRules, SingleReplyKeepsPunctuationWithoutDrawingRandom)
{
	char aReply[64];
	EXPECT_TRUE(QmKeywordReplyRules::PickReply("still here, yes; C:\\new", aReply, sizeof(aReply), [](int) {
		ADD_FAILURE() << "A single reply does not need a random choice";
		return 0;
	}));
	EXPECT_STREQ(aReply, "still here, yes; C:\\new");
}

TEST(KeywordReplyRules, EmptyAndWhitespaceAlternativesDoNotGetProbabilitySlots)
{
	const char *apExpected[] = {"A", "B"};
	for(int Index = 0; Index < 2; ++Index)
	{
		SCOPED_TRACE(Index);
		char aReply[32];
		EXPECT_TRUE(QmKeywordReplyRules::PickReply("| \t| A || B |　|", aReply, sizeof(aReply), [&](int Count) {
			EXPECT_EQ(Count, 2);
			return Index;
		}));
		EXPECT_STREQ(aReply, apExpected[Index]);
	}
}

TEST(KeywordReplyRules, EmptyAlternativesDoNotReplyOrDrawRandom)
{
	const char *apReplies[] = {nullptr, "", "|", "||", " | \t |　"};
	for(const char *pReplies : apReplies)
	{
		SCOPED_TRACE(pReplies != nullptr ? pReplies : "nullptr");
		char aReply[32] = "previous reply";
		EXPECT_FALSE(QmKeywordReplyRules::PickReply(pReplies, aReply, sizeof(aReply), [](int) {
			ADD_FAILURE() << "Empty alternatives cannot be selected";
			return 0;
		}));
		EXPECT_STREQ(aReply, "");
	}
}

TEST(KeywordReplyRules, KeepsUtf8ReplyContent)
{
	char aReply[64];
	EXPECT_TRUE(QmKeywordReplyRules::PickReply("你好|在的|马上回来", aReply, sizeof(aReply), [](int Count) {
		EXPECT_EQ(Count, 3);
		return 2;
	}));
	EXPECT_STREQ(aReply, "马上回来");
}

TEST(KeywordReplyRules, RepeatedAlternativesKeepSeparateProbabilitySlots)
{
	const char *apExpected[] = {"A", "A", "B"};
	for(int Index = 0; Index < 3; ++Index)
	{
		SCOPED_TRACE(Index);
		char aReply[32];
		EXPECT_TRUE(QmKeywordReplyRules::PickReply("A|A|B", aReply, sizeof(aReply), [&](int Count) {
			EXPECT_EQ(Count, 3);
			return Index;
		}));
		EXPECT_STREQ(aReply, apExpected[Index]);
	}
}

TEST(KeywordReplyRules, SelectsFromAllAlternativesBeforeTruncatingOutput)
{
	const std::string Replies = std::string(300, 'X') + "|last";
	char aReply[8];
	EXPECT_TRUE(QmKeywordReplyRules::PickReply(Replies.c_str(), aReply, sizeof(aReply), [](int Count) {
		EXPECT_EQ(Count, 2);
		return 1;
	}));
	EXPECT_STREQ(aReply, "last");

	EXPECT_TRUE(QmKeywordReplyRules::PickReply(Replies.c_str(), aReply, sizeof(aReply), [](int Count) {
		EXPECT_EQ(Count, 2);
		return 0;
	}));
	EXPECT_STREQ(aReply, "XXXXXXX");
}

TEST(KeywordReplyRules, EncodesMultilineRulesForSingleLineConfig)
{
	const char *pRules = "你好=>在\n虾米=>在的";
	char aEncoded[128];
	QmKeywordReplyRules::EncodeForConfig(pRules, aEncoded, sizeof(aEncoded));

	EXPECT_STREQ(aEncoded, "你好=>在\\n虾米=>在的");
	EXPECT_EQ(strchr(aEncoded, '\n'), nullptr);
}

TEST(KeywordReplyRules, DecodesSecondLineChineseKeyword)
{
	const char *pEncoded = "你好=>在\\n虾米=>在的";
	char aDecoded[128];
	QmKeywordReplyRules::DecodeFromConfig(pEncoded, aDecoded, sizeof(aDecoded));

	EXPECT_STREQ(aDecoded, "你好=>在\n虾米=>在的");
}

TEST(KeywordReplyRules, KeepsLiteralBackslashN)
{
	const char *pRules = "路径=>C:\\new";
	char aEncoded[128];
	QmKeywordReplyRules::EncodeForConfig(pRules, aEncoded, sizeof(aEncoded));

	char aDecoded[128];
	QmKeywordReplyRules::DecodeFromConfig(aEncoded, aDecoded, sizeof(aDecoded));

	EXPECT_STREQ(aEncoded, "路径=>C:\\\\new");
	EXPECT_STREQ(aDecoded, pRules);
}

TEST(KeywordReplyRules, EveryEditorMutationCommitsExceptDuringRenderOnly)
{
	QmKeywordReplyRules::SEditorChanges Changes;
	EXPECT_FALSE(Changes.Any());
	EXPECT_FALSE(Changes.ShouldCommit(false));

	Changes.m_Added = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
	EXPECT_FALSE(Changes.ShouldCommit(true));
	Changes = {};
	Changes.m_Removed = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
	Changes = {};
	Changes.m_TriggerText = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
	Changes = {};
	Changes.m_ReplyText = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
	Changes = {};
	Changes.m_Rename = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
	Changes = {};
	Changes.m_Regex = true;
	EXPECT_TRUE(Changes.ShouldCommit(false));
}

TEST(KeywordReplyRules, ExternalConfigInvalidatesInitializedEditorRows)
{
	EXPECT_TRUE(QmKeywordReplyRules::EditorConfigChanged(false, "same", "same"));
	EXPECT_FALSE(QmKeywordReplyRules::EditorConfigChanged(true, "same", "same"));
	EXPECT_TRUE(QmKeywordReplyRules::EditorConfigChanged(true, "old", "external"));
	EXPECT_FALSE(QmKeywordReplyRules::EditorConfigChanged(true, nullptr, nullptr));
}

TEST(KeywordReplyRules, OverflowPreservesPreviousConfigIncludingUtf8)
{
	char aConfig[32] = "existing=>reply";
	const std::string Rules = "你好=>" + std::string(40, 'X');
	EXPECT_FALSE(QmKeywordReplyRules::EncodeForConfig(Rules.c_str(), aConfig, sizeof(aConfig)));
	EXPECT_STREQ(aConfig, "existing=>reply");
}

TEST(KeywordReplyRules, EncodingExpansionIsIncludedInCapacityCheck)
{
	char aConfig[8] = "old";
	EXPECT_FALSE(QmKeywordReplyRules::EncodeForConfig("a=>\\\\\\", aConfig, sizeof(aConfig)));
	EXPECT_STREQ(aConfig, "old");
	EXPECT_TRUE(QmKeywordReplyRules::EncodeForConfig("a=>1234", aConfig, sizeof(aConfig)));
	EXPECT_STREQ(aConfig, "a=>1234");
}

TEST(KeywordReplyRules, FailedRuleSaveCanBeShortenedAndSavedWithoutLosingOtherRules)
{
	char aConfig[32] = "old=>reply";
	std::vector<QmKeywordReplyRules::SRule> vRules = {{"first", "reply"}, {"second", std::string(50, 'X')}};
	EXPECT_FALSE(QmKeywordReplyRules::EncodeRules(vRules, aConfig, sizeof(aConfig)));
	EXPECT_STREQ(aConfig, "old=>reply");
	vRules[1].m_Reply = "ok";
	EXPECT_TRUE(QmKeywordReplyRules::EncodeRules(vRules, aConfig, sizeof(aConfig)));
	char aDecoded[32];
	QmKeywordReplyRules::DecodeFromConfig(aConfig, aDecoded, sizeof(aDecoded));
	const auto vParsed = QmKeywordReplyRules::ParseDecoded(aDecoded);
	ASSERT_EQ(vParsed.size(), 2u);
	EXPECT_EQ(vParsed[0].m_Reply, "reply");
	EXPECT_EQ(vParsed[1].m_Reply, "ok");
}

TEST(KeywordReplyRules, EditorAndMatcherShareFlagsAndEscaping)
{
	char aConfig[128];
	ASSERT_TRUE(QmKeywordReplyRules::EncodeRules({{"^hello$", "C:\\new|你好", true, true}}, aConfig, sizeof(aConfig)));
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig(aConfig);
	char aReply[64];
	bool Rename = false;
	ASSERT_TRUE(Rules.Match("HELLO", aReply, sizeof(aReply), Rename, [](int Count) { return Count - 1; }));
	EXPECT_STREQ(aReply, "你好");
	EXPECT_TRUE(Rename);
}

TEST(KeywordReplyRules, DisabledRegexTreatsMetacharactersLiterally)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("[abc]=>literal");
	char aReply[32];
	bool Rename = false;
	EXPECT_FALSE(Rules.Match("a", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	EXPECT_TRUE(Rules.Match("look [ABC]", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	EXPECT_STREQ(aReply, "literal");
}

TEST(KeywordReplyRules, LiteralKeywordsDoNotGuessChineseNegation)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("帮忙=>在的");
	char aReply[32];
	bool Rename = false;
	EXPECT_TRUE(Rules.Match("有没有人帮忙", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	EXPECT_STREQ(aReply, "在的");
	EXPECT_TRUE(Rules.Match("不用帮忙", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
}

TEST(KeywordReplyRules, LiteralModeKeepsSupportedKeywordSeparators)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("one，two；three｜four,five;six|七=>ok");
	for(const char *pMessage : {"one", "two", "three", "four", "five", "six", "七"})
	{
		SCOPED_TRACE(pMessage);
		char aReply[32];
		bool Rename = false;
		EXPECT_TRUE(Rules.Match(pMessage, aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	}
}

TEST(KeywordReplyRules, RegexModeKeepsAlternationTogetherAndInvalidRegexDoesNotReply)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("[regex] /^(one|two)$/i=>ok");
	char aReply[32];
	bool Rename = false;
	EXPECT_TRUE(Rules.Match("TWO", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	EXPECT_FALSE(Rules.Match("two words", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	Rules.SetConfig("[regex] [=>invalid");
	EXPECT_FALSE(Rules.Match("[", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
}

TEST(KeywordReplyRules, ConfigChangeReplacesCompiledRulesAndClearingDisablesThem)
{
	QmKeywordReplyRules::CRuleSet Rules;
	char aReply[32];
	bool Rename = false;
	Rules.SetConfig("old=>first");
	EXPECT_TRUE(Rules.Match("old", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	Rules.SetConfig("new=>second");
	EXPECT_FALSE(Rules.Match("old", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	EXPECT_TRUE(Rules.Match("new", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
	Rules.SetConfig("");
	EXPECT_FALSE(Rules.Match("new", aReply, sizeof(aReply), Rename, [](int) { return 0; }));
}

TEST(KeywordReplyRules, EmptyReplyAlternativesDoNotHideOtherMatchingRules)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("hello=>| |　|\\nhello=>ok");
	char aReply[32];
	bool Rename = false;
	EXPECT_TRUE(Rules.Match("hello", aReply, sizeof(aReply), Rename, [](int Count) {
		EXPECT_EQ(Count, 1);
		return 0;
	}));
	EXPECT_STREQ(aReply, "ok");
}
