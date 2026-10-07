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
		{":ax", EQmChatEmoji::LOVE, "qmclient/chat_emojis/love.webp"},
		{":bx", EQmChatEmoji::NO, "qmclient/chat_emojis/no.webp"},
		{":fd", EQmChatEmoji::OPPOSE, "qmclient/chat_emojis/oppose.webp"},
		{":gg", EQmChatEmoji::AWKWARD, "qmclient/chat_emojis/awkward.webp"},
		{":gx", EQmChatEmoji::KNEEL, "qmclient/chat_emojis/kneel.webp"},
		{":hh", EQmChatEmoji::HEHE, "qmclient/chat_emojis/hehe.webp"},
		{":mr", EQmChatEmoji::INSULT, "qmclient/chat_emojis/insult.webp"},
		{":mm", EQmChatEmoji::CUTE, "qmclient/chat_emojis/cute.webp"},
		{":sq", EQmChatEmoji::ANGRY, "qmclient/chat_emojis/angry.webp"},
		{":sd", EQmChatEmoji::DEAD, "qmclient/chat_emojis/dead.webp"},
		{":ty", EQmChatEmoji::AGREE, "qmclient/chat_emojis/agree.webp"},
		{":tx", EQmChatEmoji::SURRENDER, "qmclient/chat_emojis/surrender.webp"},
		{":wd", EQmChatEmoji::SMELL, "qmclient/chat_emojis/smell.webp"},
		{":wh", EQmChatEmoji::QUESTION, "qmclient/chat_emojis/question.webp"},
		{":zj", EQmChatEmoji::SHOCKED, "qmclient/chat_emojis/shocked.webp"},
		{":zc", EQmChatEmoji::SUPPORT, "qmclient/chat_emojis/support.webp"},
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

// 表情框底边必须落在文字基线上：默认字体下行框下沉多，挂在基线下会压住下一行文字。
TEST(QmChatEmoji, ChatBaselineOffsetKeepsEmojiOnTheTextBaseline)
{
	for(const float FontSize : {1.0f, 3.0f, 6.0f, 10.0f, 20.0f})
	{
		const float EmojiSize = QmChatEmojiChatDisplaySize(FontSize);
		const float Offset = QmChatEmojiBaselineOffset(FontSize, EmojiSize);
		// 基线位于光标顶部下方 FontSize 处，偏移后底边正好落在基线上。
		EXPECT_FLOAT_EQ(Offset + EmojiSize, FontSize) << FontSize;
		// 表情只向上收，不会掉到基线之下。
		EXPECT_LE(Offset, 0.0f) << FontSize;
	}

	// 正常字号区间（cl_chat_size <= 100）里表情恒大于 em 框，字号越大需要上移越多。
	EXPECT_LT(QmChatEmojiBaselineOffset(10.0f, QmChatEmojiChatDisplaySize(10.0f)), QmChatEmojiBaselineOffset(6.0f, QmChatEmojiChatDisplaySize(6.0f)));

	// 无有效表情尺寸时不产生偏移。
	EXPECT_FLOAT_EQ(QmChatEmojiBaselineOffset(6.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiBaselineOffset(6.0f, -1.0f), 0.0f);
}

// 表情不能越过行尾被聊天区右边缘裁掉：放不下时先缩小，缩到最小可读尺寸仍放不下才换行。
TEST(QmChatEmoji, FitSizeShrinksInsteadOfOverflowingTheLine)
{
	const float EmojiSize = QmChatEmojiChatDisplaySize(10.0f);
	ASSERT_FLOAT_EQ(EmojiSize, 30.0f);

	// 放得下时保持原尺寸。
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, 40.0f), EmojiSize);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, EmojiSize), EmojiSize);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(18.0f, 100.0f), 18.0f);

	// 放不下时按剩余宽度缩小，宽度下限取「绝对最小尺寸」与「原尺寸一半」的较大者。
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, 20.0f), 20.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, 15.0f), 15.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(18.0f, 12.0f), 12.0f);

	// 低于最小可读尺寸时返回 0，交由调用方换行。
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, 14.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(EmojiSize, -5.0f), 0.0f);

	// 无有效表情尺寸时不做任何事。
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(0.0f, 40.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatEmojiFitSize(-1.0f, 40.0f), 0.0f);
}

TEST(QmChatEmoji, LoadedImageInvalidatesCachedTextHeightsForBothChatWidths)
{
	bool ImageLayout = false;
	float aCachedHeights[2] = {8.0f, 14.0f};

	const bool WaitingForImage = QmChatEmojiShouldRenderImage(EQmChatEmoji::DEAD, false);
	EXPECT_FALSE(QmChatEmojiInvalidateChangedLayout(WaitingForImage, ImageLayout, aCachedHeights));
	EXPECT_FLOAT_EQ(aCachedHeights[0], 8.0f);
	EXPECT_FLOAT_EQ(aCachedHeights[1], 14.0f);

	const bool ImageReady = QmChatEmojiShouldRenderImage(EQmChatEmoji::DEAD, true);
	EXPECT_TRUE(QmChatEmojiInvalidateChangedLayout(ImageReady, ImageLayout, aCachedHeights));
	EXPECT_TRUE(ImageLayout);
	EXPECT_LT(aCachedHeights[0], 0.0f);
	EXPECT_LT(aCachedHeights[1], 0.0f);
}

TEST(QmChatEmoji, RepeatedImageFramesKeepMeasuredLayout)
{
	bool ImageLayout = false;
	float aCachedHeights[2] = {20.0f, 26.0f};

	ASSERT_TRUE(QmChatEmojiInvalidateChangedLayout(true, ImageLayout, aCachedHeights));
	aCachedHeights[0] = 20.0f;
	aCachedHeights[1] = 26.0f;
	EXPECT_FALSE(QmChatEmojiInvalidateChangedLayout(true, ImageLayout, aCachedHeights));
	EXPECT_TRUE(ImageLayout);
	EXPECT_FLOAT_EQ(aCachedHeights[0], 20.0f);
	EXPECT_FLOAT_EQ(aCachedHeights[1], 26.0f);
}

TEST(QmChatEmoji, UnavailableImageInvalidatesCachedImageLayout)
{
	bool ImageLayout = true;
	float aCachedHeights[2] = {20.0f, 26.0f};

	const bool ImageAvailable = QmChatEmojiShouldRenderImage(EQmChatEmoji::DEAD, false);
	EXPECT_TRUE(QmChatEmojiInvalidateChangedLayout(ImageAvailable, ImageLayout, aCachedHeights));
	EXPECT_FALSE(ImageLayout);
	EXPECT_LT(aCachedHeights[0], 0.0f);
	EXPECT_LT(aCachedHeights[1], 0.0f);

	aCachedHeights[0] = 8.0f;
	EXPECT_FALSE(QmChatEmojiInvalidateChangedLayout(ImageAvailable, ImageLayout, aCachedHeights));
	EXPECT_FLOAT_EQ(aCachedHeights[0], 8.0f);
}

TEST(QmChatEmoji, BackgroundImageIsVisibleOnlyAfterDecodeCompletes)
{
	CSemaphore Started;
	CSemaphore Finish;
	CJobPool Pool;
	Pool.Init(1);
	auto pJob = std::make_shared<CQmChatEmojiLoadJob>([&](CImageInfo &Image) {
		Image.m_Width = 1260;
		Started.Signal();
		Finish.Wait();
		Image.m_Height = 1244;
	});
	EXPECT_EQ(pJob->Image(), nullptr);
	Pool.Add(pJob);
	Started.Wait();
	EXPECT_EQ(pJob->Image(), nullptr);
	Finish.Signal();
	Pool.Shutdown();
	ASSERT_NE(pJob->Image(), nullptr);
	EXPECT_EQ(pJob->Image()->m_Width, 1260U);
	EXPECT_EQ(pJob->Image()->m_Height, 1244U);
}

TEST(QmChatEmoji, ReleasingComponentReferenceDoesNotInvalidateTheLoad)
{
	CSemaphore Started;
	CSemaphore Finish;
	CJobPool Pool;
	Pool.Init(1);
	bool Completed = false;
	auto pJob = std::make_shared<CQmChatEmojiLoadJob>([&](CImageInfo &) {
		Started.Signal();
		Finish.Wait();
		Completed = true;
	});
	Pool.Add(pJob);
	Started.Wait();
	pJob.reset();
	Finish.Signal();
	Pool.Shutdown();
	EXPECT_TRUE(Completed);
}
