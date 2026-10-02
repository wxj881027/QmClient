#include <game/client/components/qmclient/translate/translate_detect.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	bool ShouldTranslate(const char *pText, const qm_translate::SPlayerReference *pPlayers = nullptr, int NumPlayers = 0, const char *pTarget = "zh", bool Always = false)
	{
		return qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage(pText, pPlayers, NumPlayers), pTarget, 3, 50, Always);
	}
}

TEST(TranslateDetect, ShortChineseSkipsDespiteMinimum)
{
	EXPECT_FALSE(ShouldTranslate("泥蛙！！！"));
	EXPECT_FALSE(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage("好"), "zh", 12, 100, false));
}

TEST(TranslateDetect, KnownNameAddressSkipsChineseBody)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_FALSE(ShouldTranslate("DYL：泥蛙！！！", aPlayers, 1));
	EXPECT_FALSE(ShouldTranslate("  DYL : 好", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("DYL：help me", aPlayers, 1));
}

TEST(TranslateDetect, UnknownNameAddressRemainsInLanguageDecision)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DY", 7}};
	EXPECT_TRUE(ShouldTranslate("DYL：泥蛙！！！", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("dyl：泥蛙！！！", aPlayers, 1));
}

TEST(TranslateDetect, MentionMayAppearInMiddleOfMessage)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_FALSE(ShouldTranslate("好 @DYL！", aPlayers, 1));
	EXPECT_FALSE(ShouldTranslate("(@DYL) 泥蛙", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("@DYL help", aPlayers, 1));
}

TEST(TranslateDetect, MentionRequiresCompleteNameAndBoundary)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_TRUE(ShouldTranslate("@DYLabc 好", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("mail@DYL 好", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("@DYL你好", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("DYL 好", aPlayers, 1));
}

TEST(TranslateDetect, LongestExplicitMentionWins)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}, {"DYL pro", 9}};
	EXPECT_FALSE(ShouldTranslate("@DYL pro 好", aPlayers, 2));
	EXPECT_FALSE(ShouldTranslate("DYL pro：好", aPlayers, 2));
}

TEST(TranslateDetect, PlayerNameMayContainSpacesUnicodeAndPunctuation)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"[小刻]二师兄", 7}, {"A B", 9}, {"A:B", 10}};
	EXPECT_FALSE(ShouldTranslate("[小刻]二师兄：好", aPlayers, 3));
	EXPECT_FALSE(ShouldTranslate("@A B 好", aPlayers, 3));
	EXPECT_FALSE(ShouldTranslate("A:B：好", aPlayers, 3));
}

TEST(TranslateDetect, OrdinaryWordsAreNotRemovedEvenWhenMatchingPlayerName)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"help", 7}};
	EXPECT_TRUE(ShouldTranslate("help", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("please help", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("好 help", aPlayers, 1));
}

TEST(TranslateDetect, ExplicitIdRequiresConnectedPlayerAndTokenBoundary)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_EQ(qm_translate::AnalyzeLanguage("@7 好", aPlayers, 1).m_Digits, 0);
	EXPECT_EQ(qm_translate::AnalyzeLanguage("@8 好", aPlayers, 1).m_Digits, 1);
	EXPECT_EQ(qm_translate::AnalyzeLanguage("@70 好", aPlayers, 1).m_Digits, 2);
	EXPECT_TRUE(ShouldTranslate("@7abc 好", aPlayers, 1));
	// 超长数字不作为玩家 ID 消耗，仍保留数字与后续文字统计。
	const auto LongId = qm_translate::AnalyzeLanguage("@999999999999999999999abc 好", aPlayers, 1);
	EXPECT_EQ(LongId.m_Digits, 21);
	EXPECT_EQ(LongId.m_Latin, 3);
	EXPECT_EQ(LongId.m_Han, 1);
}

TEST(TranslateDetect, EmptyNullAndDisconnectedRosterDoNotExcludeNames)
{
	const qm_translate::SPlayerReference aPlayers[] = {{nullptr, 7}, {"", 8}};
	EXPECT_TRUE(ShouldTranslate("DYL：好", aPlayers, 2));
	EXPECT_TRUE(ShouldTranslate("DYL：好", nullptr, 2));
	EXPECT_TRUE(ShouldTranslate("DYL：好", aPlayers, -1));
}

TEST(TranslateDetect, RepeatedCallsUseCurrentRosterWithoutStaleNameCache)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_FALSE(ShouldTranslate("DYL：好", aPlayers, 1));
	EXPECT_TRUE(ShouldTranslate("DYL：好"));
	EXPECT_FALSE(ShouldTranslate("DYL：好", aPlayers, 1));
}

TEST(TranslateDetect, EmptyNumbersAndSymbolsNeverTriggerIncomingRequest)
{
	for(const char *pText : {static_cast<const char *>(nullptr), "", "1", "12", "12345", "！！！", "😀", "123 😀", " \t"})
	{
		SCOPED_TRACE(pText ? pText : "null");
		EXPECT_FALSE(ShouldTranslate(pText));
		EXPECT_FALSE(ShouldTranslate(pText, nullptr, 0, "zh", true));
	}
}

TEST(TranslateDetect, NameOnlyMentionDoesNotTriggerRequest)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_FALSE(ShouldTranslate("@DYL", aPlayers, 1));
	EXPECT_FALSE(ShouldTranslate("DYL：！！！", aPlayers, 1));
	EXPECT_FALSE(ShouldTranslate("@7", aPlayers, 1));
}

TEST(TranslateDetect, AlwaysModeStillTranslatesChineseBody)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	EXPECT_TRUE(ShouldTranslate("DYL：好", aPlayers, 1, "zh", true));
}

TEST(TranslateDetect, ExplicitChineseVariantStillAllowsConversion)
{
	EXPECT_TRUE(ShouldTranslate("你好", nullptr, 0, "zh-TW"));
	EXPECT_TRUE(ShouldTranslate("你好", nullptr, 0, "zh-CN"));
	EXPECT_FALSE(ShouldTranslate("你好", nullptr, 0, "ZH"));
}

TEST(TranslateDetect, ForeignScriptsAreNotDiscardedAsSymbols)
{
	for(const char *pText : {"hello", "привет", "こんにちは", "안녕", "مرحبا", "नमस्ते", "好 مرحبا", "好 नमस्ते"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(ShouldTranslate(pText));
	}
}

TEST(TranslateDetect, EmojiAndPunctuationDoNotChangeChineseDecision)
{
	EXPECT_FALSE(ShouldTranslate("好😀！？？ …"));
	EXPECT_FALSE(ShouldTranslate("好123"));
}

TEST(TranslateDetect, MixedTextKeepsMinimumAndRatioSettings)
{
	const auto Stats = qm_translate::AnalyzeLanguage("你好hello");
	EXPECT_TRUE(qm_translate::ShouldTranslateIncoming(Stats, "zh", 3, 50, false));
	const auto Majority = qm_translate::AnalyzeLanguage("你好啊hey");
	EXPECT_FALSE(qm_translate::ShouldTranslateIncoming(Majority, "zh", 3, 50, false));
	EXPECT_TRUE(qm_translate::ShouldTranslateIncoming(Majority, "zh", 3, 75, false));
	EXPECT_TRUE(qm_translate::ShouldTranslateIncoming(Majority, "zh", 4, 50, false));
}

TEST(TranslateDetect, OtherTargetScriptHeuristicsRemainAvailable)
{
	EXPECT_FALSE(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage("こんにちは"), "ja", 3, 75, false));
	EXPECT_FALSE(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage("안녕하세요"), "ko", 3, 75, false));
	EXPECT_FALSE(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage("привет"), "ru", 3, 75, false));
	EXPECT_TRUE(ShouldTranslate("help", nullptr, 0, "en"));
}

TEST(TranslateDetect, InvalidUtf8DoesNotHideRemainingTextOrLoop)
{
	const std::string Text = std::string("\xff") + "help";
	EXPECT_TRUE(ShouldTranslate(Text.c_str()));
}
