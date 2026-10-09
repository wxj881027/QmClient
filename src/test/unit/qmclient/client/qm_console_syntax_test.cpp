#include <game/client/components/qmclient/console_syntax.h>

#include <gtest/gtest.h>

namespace
{
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});

	std::vector<STextColorSplit> Colors(const char *pText)
	{
		std::vector<STextColorSplit> vResult;
		QmConsoleSyntax::AppendColors(pText, Palette, vResult);
		return vResult;
	}
}

TEST(QmConsoleSyntax, CommandParametersNumbersAndStringsHaveDistinctColors)
{
	const auto vColors = Colors("echo player -42 \"hello world\"");
	ASSERT_EQ(vColors.size(), 4u);
	EXPECT_EQ(vColors[0].m_Color, Palette.m_aColors[QmConsoleAppearance::COMMAND]);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::PARAMETER]);
	EXPECT_EQ(vColors[2].m_Color, Palette.m_aColors[QmConsoleAppearance::NUMBER]);
	EXPECT_EQ(vColors[3].m_Color, Palette.m_aColors[QmConsoleAppearance::STRING]);
	EXPECT_EQ(vColors[3].m_CharIndex, 16);
	EXPECT_EQ(vColors[3].m_Length, 13);
}

TEST(QmConsoleSyntax, SemicolonStartsANewCommandOutsideQuotedStrings)
{
	const auto vColors = Colors("echo \"a;b#c\"; quit");
	ASSERT_EQ(vColors.size(), 4u);
	EXPECT_EQ(vColors[1].m_Length, 7);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::STRING]);
	EXPECT_EQ(vColors[3].m_Color, Palette.m_aColors[QmConsoleAppearance::COMMAND]);
}

TEST(QmConsoleSyntax, EscapedQuotesDoNotEndAString)
{
	const auto vColors = Colors("echo \"a\\\";b\" 7");
	ASSERT_EQ(vColors.size(), 3u);
	EXPECT_EQ(vColors[1].m_Length, 7);
	EXPECT_EQ(vColors[2].m_Color, Palette.m_aColors[QmConsoleAppearance::NUMBER]);
}

TEST(QmConsoleSyntax, UnfinishedStringStaysColoredWhileTyping)
{
	const auto vColors = Colors("echo \"unterminated;");
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_EQ(vColors[1].m_Length, 14);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::STRING]);
}

TEST(QmConsoleSyntax, CommentStopsCommandHighlighting)
{
	const auto vColors = Colors("echo 1 # quit; echo 2");
	ASSERT_EQ(vColors.size(), 3u);
	EXPECT_EQ(vColors[2].m_CharIndex, 7);
	EXPECT_EQ(vColors[2].m_Color, Palette.m_MutedText);
}

TEST(QmConsoleSyntax, Utf8AndEchoPrefixUseByteOffsets)
{
	std::vector<STextColorSplit> vColors{{0, 2, Palette.m_MutedText}};
	QmConsoleSyntax::AppendColors("echo 中文 2", Palette, vColors, 2);
	ASSERT_EQ(vColors.size(), 4u);
	EXPECT_EQ(vColors[0].m_Length, 2);
	EXPECT_EQ(vColors[2].m_CharIndex, 7);
	EXPECT_EQ(vColors[2].m_Length, 6);
	EXPECT_EQ(vColors[3].m_CharIndex, 14);
}

TEST(QmConsoleSyntax, NumericTokensRequireACompleteNumber)
{
	for(const char *pToken : {"0", "-12", "+.5", "12.", "-1.25e+3"})
	{
		SCOPED_TRACE(pToken);
		EXPECT_TRUE(QmConsoleSyntax::IsNumber(pToken));
	}
	for(const char *pToken : {"", "+", ".", "12ms", "1e", "1e+", "127.0.0.1"})
	{
		SCOPED_TRACE(pToken);
		EXPECT_FALSE(QmConsoleSyntax::IsNumber(pToken));
	}
}

TEST(QmConsoleSyntax, EmptyOrWhitespaceInputProducesNoColorRanges)
{
	EXPECT_TRUE(Colors("").empty());
	EXPECT_TRUE(Colors(" \t ").empty());
}
