#include <engine/shared/config.h>

#include <game/client/components/chat.h>

#include <gtest/gtest.h>

TEST(QmChatSpacing, DefaultConfigKeepsZeroSpacing)
{
	EXPECT_EQ(DefaultConfig::QmChatLineSpacing, 0);
	EXPECT_EQ(CConfig::ms_QmChatLineSpacing, 0);
}

TEST(QmChatSpacing, ZeroSpacingPreservesDefaultStacking)
{
	// 默认 0 时与原始公式 y -= LineHeight 完全一致。
	const float Y = CChat::AdvanceChatLineStack(100.0f, 12.0f, 0.0f, 1.0f);
	EXPECT_FLOAT_EQ(Y, 88.0f);
}

TEST(QmChatSpacing, ExtraSpacingAccumulatesBetweenLines)
{
	float Y = 200.0f;
	for(int i = 0; i < 3; i++)
		Y = CChat::AdvanceChatLineStack(Y, 10.0f, 4.0f, 1.0f);
	// 每行消耗 14.0f（行高 10 + 间隔 4）。
	EXPECT_FLOAT_EQ(Y, 158.0f);
}

TEST(QmChatSpacing, VisibilityScalesLineAndSpacingTogether)
{
	// 入场动画半可见时，行高与间隔同比例缩放；完全隐藏时不消耗预算。
	const float Y = CChat::AdvanceChatLineStack(50.0f, 10.0f, 6.0f, 0.5f);
	EXPECT_FLOAT_EQ(Y, 42.0f);
	EXPECT_FLOAT_EQ(CChat::AdvanceChatLineStack(50.0f, 10.0f, 6.0f, 0.0f), 50.0f);
}
