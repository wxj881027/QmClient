#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>

#include <string>

TEST(TranslateSendOptions, OrdinaryEnterKeepsExistingTranslationHandling)
{
	for(bool AutoOutgoing : {false, true})
	{
		SCOPED_TRACE(AutoOutgoing);
		const auto Options = ResolveTranslateSendOptions(AutoOutgoing, false);
		EXPECT_TRUE(Options.m_AllowTranslation);
		EXPECT_FALSE(Options.m_TranslateOnce);
	}
}

TEST(TranslateSendOptions, CtrlEnterWithAutomaticTranslationEnabledBypassesTranslationOnlyOnce)
{
	const auto Inverted = ResolveTranslateSendOptions(true, true);
	EXPECT_FALSE(Inverted.m_AllowTranslation);
	EXPECT_FALSE(Inverted.m_TranslateOnce);

	const auto NextSend = ResolveTranslateSendOptions(true, false);
	EXPECT_TRUE(NextSend.m_AllowTranslation);
	EXPECT_FALSE(NextSend.m_TranslateOnce);

	const auto InvertedAgain = ResolveTranslateSendOptions(true, true);
	EXPECT_FALSE(InvertedAgain.m_AllowTranslation);
	EXPECT_FALSE(InvertedAgain.m_TranslateOnce);
}

TEST(TranslateSendOptions, CtrlEnterWithAutomaticTranslationDisabledForcesTranslationOnlyOnce)
{
	const auto Inverted = ResolveTranslateSendOptions(false, true);
	EXPECT_TRUE(Inverted.m_AllowTranslation);
	EXPECT_TRUE(Inverted.m_TranslateOnce);

	const auto NextSend = ResolveTranslateSendOptions(false, false);
	EXPECT_TRUE(NextSend.m_AllowTranslation);
	EXPECT_FALSE(NextSend.m_TranslateOnce);
}

TEST(TranslateOutgoingRequest, OrdinarySendWithoutSuffixDoesNotRequestTranslation)
{
	std::string Text;
	std::string Target;
	EXPECT_FALSE(PrepareOutgoingTranslateRequest("你好", nullptr, Text, Target));
}

TEST(TranslateOutgoingRequest, OneShotUsesSelectedLanguageWithoutAffectingNextSend)
{
	std::string Text;
	std::string Target;
	ASSERT_TRUE(PrepareOutgoingTranslateRequest("你好", "en", Text, Target));
	EXPECT_EQ(Text, "你好");
	EXPECT_EQ(Target, "en");
	EXPECT_FALSE(PrepareOutgoingTranslateRequest("下一条消息", nullptr, Text, Target));
}

TEST(TranslateOutgoingRequest, OneShotDoesNotSkipShortMessages)
{
	std::string Text;
	std::string Target;
	ASSERT_TRUE(PrepareOutgoingTranslateRequest("hi", "ja", Text, Target));
	EXPECT_EQ(Text, "hi");
	EXPECT_EQ(Target, "ja");
}

TEST(TranslateOutgoingRequest, ExplicitSuffixOverridesSelectedLanguage)
{
	for(const char *pFallback : {static_cast<const char *>(nullptr), "en"})
	{
		SCOPED_TRACE(pFallback ? pFallback : "ordinary send");
		std::string Text;
		std::string Target;
		ASSERT_TRUE(PrepareOutgoingTranslateRequest("你好 [pt-BR] \t", pFallback, Text, Target));
		EXPECT_EQ(Text, "你好");
		EXPECT_EQ(Target, "pt-BR");
	}
}

TEST(TranslateOutgoingRequest, CommandsDoNotBecomeTranslationRequests)
{
	for(const char *pLine : {"/pause", " /login secret", "\t/w Alice hello", "/team 2 [en]", "　/login secret"})
	{
		SCOPED_TRACE(pLine);
		std::string Text;
		std::string Target;
		EXPECT_FALSE(PrepareOutgoingTranslateRequest(pLine, "en", Text, Target));
	}
}

TEST(TranslateOutgoingRequest, EmptyInputDoesNotBecomeTranslationRequest)
{
	for(const char *pLine : {static_cast<const char *>(nullptr), "", " \t", "　"})
	{
		SCOPED_TRACE(pLine ? pLine : "null");
		std::string Text;
		std::string Target;
		EXPECT_FALSE(PrepareOutgoingTranslateRequest(pLine, "en", Text, Target));
	}
}

TEST(TranslateOutgoingRequest, OneShotKeepsLiteralBracketsWhenNoValidSuffixExists)
{
	std::string Text;
	std::string Target;
	EXPECT_FALSE(PrepareOutgoingTranslateRequest("你好 [not a language]", nullptr, Text, Target));
	ASSERT_TRUE(PrepareOutgoingTranslateRequest("你好 [not a language]", "de", Text, Target));
	EXPECT_EQ(Text, "你好 [not a language]");
	EXPECT_EQ(Target, "de");
}

TEST(TranslateOutgoingRequest, EmptyFallbackDoesNotEnableOneShotTranslation)
{
	std::string Text;
	std::string Target;
	EXPECT_FALSE(PrepareOutgoingTranslateRequest("hello", "", Text, Target));
}
