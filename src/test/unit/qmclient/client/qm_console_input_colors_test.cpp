#include <game/client/components/qmclient/console_syntax.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>
#include <vector>

namespace
{
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});
}

TEST(QmConsoleInputColors, CommandAndNumberKeepNormalByteRanges)
{
	std::vector<STextColorSplit> vColors;
	ASSERT_TRUE(QmConsoleSyntax::PrepareInputColors("echo 2", 6, nullptr, Palette, vColors));
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_EQ(vColors[0].m_CharIndex, 0);
	EXPECT_EQ(vColors[0].m_Length, 4);
	EXPECT_EQ(vColors[0].m_Color, Palette.m_aColors[QmConsoleAppearance::COMMAND]);
	EXPECT_EQ(vColors[1].m_CharIndex, 5);
	EXPECT_EQ(vColors[1].m_Length, 1);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::NUMBER]);
}

TEST(QmConsoleInputColors, CompositionInsideNumberChangesOnlyPreparedDisplayColors)
{
	std::string Text = "echo 12";
	std::string Composition = "中文";
	std::vector<STextColorSplit> vColors;
	ASSERT_TRUE(QmConsoleSyntax::PrepareInputColors(Text.c_str(), 6, Composition.c_str(), Palette, vColors));
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_EQ(vColors[1].m_CharIndex, 5);
	EXPECT_EQ(vColors[1].m_Length, 8);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::PARAMETER]);
	EXPECT_EQ(Text, "echo 12");
	EXPECT_EQ(Composition, "中文");
}

TEST(QmConsoleInputColors, CompositionInsideQuotesKeepsUtf8BytePositions)
{
	std::vector<STextColorSplit> vColors;
	ASSERT_TRUE(QmConsoleSyntax::PrepareInputColors("echo \"\"", 6, "中文", Palette, vColors));
	ASSERT_EQ(vColors.size(), 2u);
	EXPECT_EQ(vColors[1].m_CharIndex, 5);
	EXPECT_EQ(vColors[1].m_Length, 8);
	EXPECT_EQ(vColors[1].m_Color, Palette.m_aColors[QmConsoleAppearance::STRING]);
}

TEST(QmConsoleInputColors, InvalidCompositionOffsetDiscardsOldDecoration)
{
	for(int Offset : {-1, 8})
	{
		SCOPED_TRACE(Offset);
		std::vector<STextColorSplit> vColors{{0, 4, Palette.m_MutedText}};
		EXPECT_FALSE(QmConsoleSyntax::PrepareInputColors("echo 12", Offset, "中文", Palette, vColors));
		EXPECT_TRUE(vColors.empty());
	}
}

TEST(QmConsoleInputColors, EmptyInputClearsPreviouslyPreparedColors)
{
	std::vector<STextColorSplit> vColors{{0, 4, Palette.m_MutedText}};
	EXPECT_TRUE(QmConsoleSyntax::PrepareInputColors("", 0, nullptr, Palette, vColors));
	EXPECT_TRUE(vColors.empty());
}

TEST(QmConsoleInputColors, PreparationAllocationFailuresDiscardPartialColorsAndAllowRetry)
{
	const std::string Text = "echo " + std::string(100, 'x') + " 3 \"quoted\"; quit 4";
	const std::string Composition(64, 'c');
	bool Completed = false;
	size_t Failures = 0;
	for(size_t BeforeFailure = 0; BeforeFailure < 128; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		std::vector<STextColorSplit> vColors{{0, 4, Palette.m_MutedText}};
		bool Ready, Triggered;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			Ready = QmConsoleSyntax::PrepareInputColors(Text.c_str(), 5, Composition.c_str(), Palette, vColors);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Ready);
			EXPECT_FALSE(vColors.empty());
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Ready);
		EXPECT_TRUE(vColors.empty());
		ASSERT_TRUE(QmConsoleSyntax::PrepareInputColors(Text.c_str(), 5, Composition.c_str(), Palette, vColors));
		EXPECT_FALSE(vColors.empty());
	}
	EXPECT_TRUE(Completed);
	EXPECT_GT(Failures, 2u);
}
