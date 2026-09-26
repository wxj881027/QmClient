// Qm UI 静态源码合同：稳定的实现接线约束。运行时行为保留在 QmAnimTest.cpp。
#include "test.h"

#include <gtest/gtest.h>

#include <string>

TEST(SettingsCardContract, HeaderTextUsesCanonicalBoundedEllipsis)
{
	const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCard.cpp");
	EXPECT_NE(Source.find("TitleProps.m_MaxWidth = DrawFrame.m_TitleRect.w;"), std::string::npos);
	EXPECT_NE(Source.find("TitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_NE(Source.find("SubtitleProps.m_MaxWidth = DrawFrame.m_SubtitleRect.w;"), std::string::npos);
	EXPECT_NE(Source.find("SubtitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
}

TEST(SettingsCardContract, DeckMeasuresContentFromCanonicalPaddingToken)
{
	const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_NE(Source.find("2.0f * ui_token::settings::CARD_PADDING"), std::string::npos);
	EXPECT_EQ(Source.find("Slot.w - 28.0f"), std::string::npos);
}

TEST(SettingsCardContract, EntryHoverSuppressionWaitsForStableLayout)
{
	const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_NE(Source.find("const bool LayoutStable = !EntryPending && !EntryPositionActive"), std::string::npos);
	EXPECT_NE(Source.find("LayoutStable && m_HasPointerPosition"), std::string::npos);
	EXPECT_NE(Source.find("!ContentHeightAnimationActive"), std::string::npos);
	EXPECT_NE(Source.find("!ReflowTargetChanged"), std::string::npos);
	EXPECT_NE(Source.find("!ReflowPositionActive"), std::string::npos);
}

TEST(SettingsCardContract, ScrollingKeepsHoverFeedbackStable)
{
	const std::string Source = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_EQ(Source.find("const bool ScrollOffsetChanged"), std::string::npos);
	EXPECT_NE(Source.find("State.m_HoverFeedbackEnabled = !m_SuppressHoverFeedbackOnce"), std::string::npos);
}
