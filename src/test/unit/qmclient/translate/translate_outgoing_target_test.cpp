#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>

#include <initializer_list>

TEST(TranslateOutgoingTarget, FinalSuffixPreservesLeadingTextAndEarlierBrackets)
{
	const auto Parsed = ParseOutgoingTranslateTarget("  hi [earlier] there \t[zh-CN]\r\n");
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_Text, "  hi [earlier] there");
	EXPECT_EQ(Parsed->m_Target, "zh-CN");
}

TEST(TranslateOutgoingTarget, AdjacentSuffixPreservesUtf8Message)
{
	const auto Parsed = ParseOutgoingTranslateTarget("你好[en][ja]");
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_Text, "你好[en]");
	EXPECT_EQ(Parsed->m_Target, "ja");
}

TEST(TranslateOutgoingTarget, TargetLengthUsesExistingTwoToFifteenByteLimits)
{
	for(const char *pInput : {"hi [a]", "hi []", "hi [abcdefghijklmnop]"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_FALSE(ParseOutgoingTranslateTarget(pInput));
	}
	const auto Parsed = ParseOutgoingTranslateTarget("hi [abcdefghijklmno]");
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_Target, "abcdefghijklmno");
}

TEST(TranslateOutgoingTarget, TargetAcceptsAlphanumericDashAndUnderscoreOnly)
{
	const auto Parsed = ParseOutgoingTranslateTarget("hi [es_419-US]");
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_Target, "es_419-US");
	for(const char *pInput : {"hi [e n]", "hi [en!]", "hi [en.]", "hi [en/US]"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_FALSE(ParseOutgoingTranslateTarget(pInput));
	}
}

TEST(TranslateOutgoingTarget, MissingTextDoesNotBecomeTranslationRequest)
{
	const char *apInputs[] = {nullptr, "", " \t\r\n", "[en]", " \t[en]"};
	for(const char *pInput : apInputs)
	{
		SCOPED_TRACE(pInput ? pInput : "null");
		EXPECT_FALSE(ParseOutgoingTranslateTarget(pInput));
	}
}

TEST(TranslateOutgoingTarget, CommandsRemainCommandsAfterAsciiOrUnicodeWhitespace)
{
	for(const char *pInput : {"/help [en]", " \t/help [en]", "　/help [en]"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_FALSE(ParseOutgoingTranslateTarget(pInput));
	}
}

TEST(TranslateOutgoingTarget, MissingOrNonfinalSuffixIsRejected)
{
	for(const char *pInput : {"hi", "hi en]", "hi [en", "hi [en] trailing", "hi [en]　"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_FALSE(ParseOutgoingTranslateTarget(pInput));
	}
}

TEST(TranslateOutgoingTarget, ParsedSlicesBorrowInputWithoutOwnedStringCopies)
{
	const char aInput[] = "hello [en]";
	const auto Parsed = ParseOutgoingTranslateTarget(aInput);
	ASSERT_TRUE(Parsed);
	EXPECT_EQ(Parsed->m_Text.data(), aInput);
	EXPECT_EQ(Parsed->m_Text.size(), 5u);
	EXPECT_EQ(Parsed->m_Target.data(), aInput + 7);
	EXPECT_EQ(Parsed->m_Target.size(), 2u);
}
