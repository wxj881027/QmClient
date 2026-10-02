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
