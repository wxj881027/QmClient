#include <game/client/components/qmclient/chat_emoji.h>

#include <gtest/gtest.h>

TEST(QmChatEmoji, MatchesAllBuiltInCodes)
{
	struct STestCase
	{
		const char *m_pCode;
		EQmChatEmoji m_Emoji;
		const char *m_pTexturePath;
	};
	const STestCase aCases[] = {
		{":ax", EQmChatEmoji::LOVE, "qmclient/chat_emojis/love.png"},
		{":bx", EQmChatEmoji::NO, "qmclient/chat_emojis/no.png"},
		{":fd", EQmChatEmoji::OPPOSE, "qmclient/chat_emojis/oppose.png"},
		{":gg", EQmChatEmoji::AWKWARD, "qmclient/chat_emojis/awkward.png"},
		{":gx", EQmChatEmoji::KNEEL, "qmclient/chat_emojis/kneel.png"},
		{":hh", EQmChatEmoji::HEHE, "qmclient/chat_emojis/hehe.png"},
		{":mr", EQmChatEmoji::INSULT, "qmclient/chat_emojis/insult.png"},
		{":mm", EQmChatEmoji::CUTE, "qmclient/chat_emojis/cute.png"},
		{":sq", EQmChatEmoji::ANGRY, "qmclient/chat_emojis/angry.png"},
		{":sd", EQmChatEmoji::DEAD, "qmclient/chat_emojis/dead.png"},
		{":ty", EQmChatEmoji::AGREE, "qmclient/chat_emojis/agree.png"},
		{":tx", EQmChatEmoji::SURRENDER, "qmclient/chat_emojis/surrender.png"},
		{":wd", EQmChatEmoji::SMELL, "qmclient/chat_emojis/smell.png"},
		{":wh", EQmChatEmoji::QUESTION, "qmclient/chat_emojis/question.png"},
		{":zj", EQmChatEmoji::SHOCKED, "qmclient/chat_emojis/shocked.png"},
		{":zc", EQmChatEmoji::SUPPORT, "qmclient/chat_emojis/support.png"},
	};

	for(const auto &TestCase : aCases)
	{
		EXPECT_EQ(QmChatEmojiFromText(TestCase.m_pCode), TestCase.m_Emoji) << TestCase.m_pCode;
		EXPECT_STREQ(QmChatEmojiTexturePath(TestCase.m_Emoji), TestCase.m_pTexturePath) << TestCase.m_pCode;
	}
}

TEST(QmChatEmoji, MatchesOnlyExactCodes)
{
	EXPECT_EQ(QmChatEmojiFromText(nullptr), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(""), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText("/sq"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(":SQ"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(" :sq"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(":sq "), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(":sq\n"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText("hello :sq"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(":sq:"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText(":unknown"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiTexturePath(EQmChatEmoji::NONE), nullptr);
	EXPECT_EQ(QmChatEmojiTexturePath(static_cast<EQmChatEmoji>(999)), nullptr);
}

TEST(QmChatEmoji, MatchesFullWidthColonCodes)
{
	EXPECT_EQ(QmChatEmojiFromText("：ax"), EQmChatEmoji::LOVE);
	EXPECT_EQ(QmChatEmojiFromText("：sq"), EQmChatEmoji::ANGRY);
	EXPECT_EQ(QmChatEmojiFromText("：zc"), EQmChatEmoji::SUPPORT);
	EXPECT_EQ(QmChatEmojiFromText("：SQ"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText("：unknown"), EQmChatEmoji::NONE);
	EXPECT_EQ(QmChatEmojiFromText("：sq "), EQmChatEmoji::NONE);
}

TEST(QmChatEmoji, DetectsHalfAndFullWidthColonPrefix)
{
	EXPECT_EQ(QmChatEmojiColonUtf8Length(":ax"), 1);
	EXPECT_EQ(QmChatEmojiColonUtf8Length(":"), 1);
	EXPECT_EQ(QmChatEmojiColonUtf8Length("：ax"), 3);
	EXPECT_EQ(QmChatEmojiColonUtf8Length("："), 3);
	EXPECT_EQ(QmChatEmojiColonUtf8Length("ax"), 0);
	EXPECT_EQ(QmChatEmojiColonUtf8Length(""), 0);
	EXPECT_EQ(QmChatEmojiColonUtf8Length(nullptr), 0);
	EXPECT_TRUE(QmChatEmojiIsColonPrefixed(":"));
	EXPECT_TRUE(QmChatEmojiIsColonPrefixed("：z"));
	EXPECT_FALSE(QmChatEmojiIsColonPrefixed("hello"));
	EXPECT_FALSE(QmChatEmojiIsColonPrefixed(nullptr));
}

TEST(QmChatEmoji, CollectsPrefixMatchesInDefinitionOrder)
{
	const SQmChatEmojiDefinition *apMatches[QM_CHAT_EMOJI_COUNT];

	EXPECT_EQ(QmChatEmojiCollectByPrefix("", apMatches, (int)QM_CHAT_EMOJI_COUNT), (int)QM_CHAT_EMOJI_COUNT);
	EXPECT_STREQ(apMatches[0]->m_pText, ":ax");
	EXPECT_STREQ(apMatches[1]->m_pText, ":bx");

	EXPECT_EQ(QmChatEmojiCollectByPrefix("z", apMatches, (int)QM_CHAT_EMOJI_COUNT), 2);
	EXPECT_STREQ(apMatches[0]->m_pText, ":zj");
	EXPECT_STREQ(apMatches[1]->m_pText, ":zc");

	EXPECT_EQ(QmChatEmojiCollectByPrefix("ax", apMatches, (int)QM_CHAT_EMOJI_COUNT), 1);
	EXPECT_STREQ(apMatches[0]->m_pText, ":ax");

	EXPECT_EQ(QmChatEmojiCollectByPrefix("AX", apMatches, (int)QM_CHAT_EMOJI_COUNT), 1);
	EXPECT_STREQ(apMatches[0]->m_pText, ":ax");

	EXPECT_EQ(QmChatEmojiCollectByPrefix("unknown", apMatches, (int)QM_CHAT_EMOJI_COUNT), 0);
}

TEST(QmChatEmoji, FormatsCandidatesWithCommas)
{
	const SQmChatEmojiDefinition *apMatches[QM_CHAT_EMOJI_COUNT];
	char aBuf[128];

	const int NumZ = QmChatEmojiCollectByPrefix("z", apMatches, (int)QM_CHAT_EMOJI_COUNT);
	ASSERT_EQ(NumZ, 2);
	EXPECT_TRUE(QmChatEmojiFormatCandidates(apMatches, NumZ, aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "zj,zc");

	const int NumA = QmChatEmojiCollectByPrefix("a", apMatches, (int)QM_CHAT_EMOJI_COUNT);
	ASSERT_EQ(NumA, 1);
	EXPECT_TRUE(QmChatEmojiFormatCandidates(apMatches, NumA, aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "ax");

	EXPECT_FALSE(QmChatEmojiFormatCandidates(apMatches, 0, aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmChatEmoji, RequiresKnownEmojiAndAvailableTexture)
{
	const auto InvalidEmoji = static_cast<EQmChatEmoji>(999);
	EXPECT_TRUE(QmChatEmojiShouldRenderImage(EQmChatEmoji::ANGRY, true));
	EXPECT_TRUE(QmChatEmojiShouldRenderImage(EQmChatEmoji::LOVE, true));
	EXPECT_FALSE(QmChatEmojiShouldRenderImage(EQmChatEmoji::ANGRY, false));
	EXPECT_FALSE(QmChatEmojiShouldRenderImage(EQmChatEmoji::NONE, true));
	EXPECT_FALSE(QmChatEmojiShouldRenderImage(InvalidEmoji, true));
}

TEST(QmChatEmoji, SkipsTranslationForEmojiMessages)
{
	const auto InvalidEmoji = static_cast<EQmChatEmoji>(999);
	EXPECT_FALSE(QmChatEmojiShouldTranslate(EQmChatEmoji::ANGRY));
	EXPECT_FALSE(QmChatEmojiShouldTranslate(EQmChatEmoji::SUPPORT));
	EXPECT_TRUE(QmChatEmojiShouldTranslate(EQmChatEmoji::NONE));
	EXPECT_TRUE(QmChatEmojiShouldTranslate(InvalidEmoji));
}

TEST(QmChatEmoji, LoadsKnownTexturesOnlyOnce)
{
	const auto InvalidEmoji = static_cast<EQmChatEmoji>(999);
	EXPECT_TRUE(QmChatEmojiShouldLoadTexture(EQmChatEmoji::LOVE, false));
	EXPECT_FALSE(QmChatEmojiShouldLoadTexture(EQmChatEmoji::LOVE, true));
	EXPECT_FALSE(QmChatEmojiShouldLoadTexture(EQmChatEmoji::NONE, false));
	EXPECT_FALSE(QmChatEmojiShouldLoadTexture(InvalidEmoji, false));
}

TEST(QmChatEmoji, ChatDisplaySizeIsBounded)
{
	EXPECT_FLOAT_EQ(QmChatEmojiChatDisplaySize(1.0f), 18.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiChatDisplaySize(6.0f), 18.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiChatDisplaySize(10.0f), 30.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiChatDisplaySize(20.0f), 30.0f);
}

TEST(QmChatEmoji, BubbleDisplaySizeIsBounded)
{
	EXPECT_FLOAT_EQ(QmChatEmojiBubbleDisplaySize(1.0f), 48.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiBubbleDisplaySize(20.0f), 60.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiBubbleDisplaySize(32.0f), 96.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiBubbleDisplaySize(64.0f), 96.0f);
}
