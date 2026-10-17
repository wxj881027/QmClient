#include <game/client/components/qmclient/streamer_word_filter.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmStreamerWordFilter, ReplacesOnlyMatchedChineseNames)
{
	std::string Text = "玩家习近平进入了服务器";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "玩家***进入了服务器");
}

TEST(QmStreamerWordFilter, FiltersImportedBuiltInWords)
{
	const char *apWords[] = {"福音会", "电话窃听", "西布曲明", "形式主义", "集合", "咖啡因", "boycott", "198964", "姬胜德", "屄"};
	for(const char *pWord : apWords)
	{
		SCOPED_TRACE(pWord);
		std::string Text = std::string("前缀") + pWord + "后缀";
		EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
		EXPECT_EQ(Text, "前缀***后缀");
	}
}

TEST(QmStreamerWordFilter, NormalizesDisguisedImportedWords)
{
	std::string Text = "电 . 话_窃\u200b听 / ＢＯＹＣＯＴＴ / 轮😀子功";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "*** / *** / ***");
}

TEST(QmStreamerWordFilter, RequiresWholeTokensForImportedLatinAndNumericWords)
{
	std::string Text = "boycotting xboycott boycott 11989640 198964";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "boycotting xboycott *** 11989640 ***");
}

TEST(QmStreamerWordFilter, PrefersExistingLongerWordsOverImportedPrefixes)
{
	std::string Text = "法轮、法轮功、法轮大法";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***、***、***");
}

TEST(QmStreamerWordFilter, DisablingPreservesImportedWords)
{
	std::string Text = "电话窃听 / boycott / 集合";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, false));
	EXPECT_EQ(Text, "电话窃听 / boycott / 集合");
}

TEST(QmStreamerWordFilter, NormalizesTraditionalChinese)
{
	std::string Text = "習近平、劉曉波、法輪功";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***、***、***");
}

TEST(QmStreamerWordFilter, MatchesInsertedPunctuationAndSpaces)
{
	std::string Text = "习 . 近_平 / 六，四事件";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "*** / ***");
}

TEST(QmStreamerWordFilter, MatchesInvisibleCharactersAndEmoji)
{
	std::string Text = "习\u200b近\u2060平 习😀近🌸平";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "*** ***");
}

TEST(QmStreamerWordFilter, MatchesCombiningMarksAndVariationSelectors)
{
	std::string Text = "习\u0301近\ufe0f平";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerWordFilter, MatchesInsertedChatColorMarkers)
{
	std::string Text = "习[[red]]近[[blue]]平";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerWordFilter, MatchesPinyinWithCaseWidthAndToneVariants)
{
	std::string Text = "Xi Jinping / ＸＪＰ / Xí Jìn Píng";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "*** / *** / ***");
}

TEST(QmStreamerWordFilter, MatchesExplicitHomophonesAndSplitCharacterAliases)
{
	std::string Text = "习禁评、刁近平、习辶斤平、小熊维尼";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***、***、***、***");
}

TEST(QmStreamerWordFilter, MatchesCommonLatinLookalikes)
{
	std::string Text = "хјр";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerWordFilter, RequiresWholeTokensForLatinAliases)
{
	std::string Text = "xjplayer falungongtest axjp xjp";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "xjplayer falungongtest axjp ***");
}

TEST(QmStreamerWordFilter, MatchesLineBreaksWithinOneMessage)
{
	std::string Text = "习\n近平\r\nXi\nJinping";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***\r\n***");
}

TEST(QmStreamerWordFilter, DoesNotJoinSeparateMessages)
{
	std::string First = "习";
	std::string Second = "近平";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(First, true));
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Second, true));
	EXPECT_EQ(First, "习");
	EXPECT_EQ(Second, "近平");
}

TEST(QmStreamerWordFilter, PreservesOrdinaryGameTextAndNumbers)
{
	std::string Text = "学习进步，近平原地图，64 tick，8964 分，1989 年，台湾玩家，包子好吃";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "学习进步，近平原地图，64 tick，8964 分，1989 年，台湾玩家，包子好吃");
}

TEST(QmStreamerWordFilter, PrefersLongestMatchingPhrase)
{
	std::string Text = "六四事件";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerWordFilter, HandlesRepeatedAndAdjacentMatches)
{
	std::string Text = "习近平习近平 六四 六四";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "****** *** ***");
}

TEST(QmStreamerWordFilter, DoesNotChangeAlreadyFilteredText)
{
	std::string Text = "习近平";
	ASSERT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerWordFilter, DisablingPreservesTheOriginalInput)
{
	std::string Text = "习近平 / xjp";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, false));
	EXPECT_EQ(Text, "习近平 / xjp");
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "*** / ***");
}

TEST(QmStreamerWordFilter, FiltersNameBuffersInPlaceWithoutGrowingThem)
{
	char aName[] = "玩家习.近平😀";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(aName, true));
	EXPECT_STREQ(aName, "玩家***😀");
}

TEST(QmStreamerWordFilter, HandlesEmptyAndInvalidUtf8WithoutJoiningAcrossIt)
{
	EXPECT_FALSE(QmStreamerWordFilter::Filter(nullptr, true));
	std::string Empty;
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Empty, true));
	std::string Text = "习";
	Text.push_back('\xff');
	Text += "近平";
	const std::string Original = Text;
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, Original);
}

namespace
{

	class QmStreamerExternalWords : public testing::Test
	{
		void SetUp() override { QmStreamerWordFilter::SetAdditionalWords({}); }
		void TearDown() override { QmStreamerWordFilter::SetAdditionalWords({}); }
	};

}

TEST_F(QmStreamerExternalWords, AddsWordsWithoutRemovingBuiltInRules)
{
	QmStreamerWordFilter::SetAdditionalWords({"测试词"});
	std::string Text = "测试词、习近平、电话窃听";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***、***、***");
}

TEST_F(QmStreamerExternalWords, NormalizesDisguisedExternalWords)
{
	QmStreamerWordFilter::SetAdditionalWords({"測試詞"});
	std::string Text = "測.試\u200b詞";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST_F(QmStreamerExternalWords, ShortAsciiWordsDoNotGrowNameBuffers)
{
	QmStreamerWordFilter::SetAdditionalWords({"q", "xy"});
	char aText[] = "q xy";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(aText, true));
	EXPECT_STREQ(aText, "* **");
}

TEST_F(QmStreamerExternalWords, ReplacingAndClearingTheFileRulesAdvancesRevision)
{
	QmStreamerWordFilter::SetAdditionalWords({"测试词"});
	const uint64_t FirstRevision = QmStreamerWordFilter::Revision();
	QmStreamerWordFilter::SetAdditionalWords({"测试词"});
	EXPECT_EQ(QmStreamerWordFilter::Revision(), FirstRevision);
	QmStreamerWordFilter::SetAdditionalWords({"其他词"});
	EXPECT_GT(QmStreamerWordFilter::Revision(), FirstRevision);
	std::string Text = "测试词、其他词";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "测试词、***");
	QmStreamerWordFilter::SetAdditionalWords({});
	Text = "其他词";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, true));
}

TEST_F(QmStreamerExternalWords, ClearingExternalWordsKeepsImportedBuiltInWords)
{
	QmStreamerWordFilter::SetAdditionalWords({"测试词"});
	QmStreamerWordFilter::SetAdditionalWords({});
	std::string Text = "测试词、电话窃听、习近平";
	EXPECT_TRUE(QmStreamerWordFilter::Filter(Text, true));
	EXPECT_EQ(Text, "测试词、***、***");
}

TEST_F(QmStreamerExternalWords, DisablingAlsoPreservesExternalWords)
{
	QmStreamerWordFilter::SetAdditionalWords({"测试词"});
	std::string Text = "测试词";
	EXPECT_FALSE(QmStreamerWordFilter::Filter(Text, false));
	EXPECT_EQ(Text, "测试词");
}
