#include <game/client/components/qmclient/translate/translate_jobs.h>

#include <gtest/gtest.h>

#include <string>

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
