#include <game/client/components/qmclient/scoreboard_footer.h>

#include <gtest/gtest.h>

TEST(QmScoreboardFooter, BlocksShrinkToContentAndCenter)
{
	CUIRect Area = {10.0f, 20.0f, 300.0f, 100.0f};
	float CursorY = Area.y;

	// 内容 120 宽：背景收到 120 并在 300 宽的底栏里水平居中。
	const CUIRect Block = QmScoreboardFooterBlock(Area, CursorY, 120.0f, 25.0f, 5.0f);
	EXPECT_FLOAT_EQ(Block.w, 120.0f);
	EXPECT_FLOAT_EQ(Block.h, 25.0f);
	EXPECT_FLOAT_EQ(Block.x, 10.0f + (300.0f - 120.0f) * 0.5f);
	EXPECT_FLOAT_EQ(Block.y, 20.0f);
	EXPECT_FLOAT_EQ(CursorY, 20.0f + 25.0f + 5.0f);
}

TEST(QmScoreboardFooter, ContentWiderThanAreaClampsToFullWidth)
{
	CUIRect Area = {0.0f, 0.0f, 200.0f, 100.0f};
	float CursorY = 0.0f;
	const CUIRect Block = QmScoreboardFooterBlock(Area, CursorY, 500.0f, 20.0f, 0.0f);
	EXPECT_FLOAT_EQ(Block.w, 200.0f);
	EXPECT_FLOAT_EQ(Block.x, 0.0f);
	EXPECT_FLOAT_EQ(Block.h, 20.0f);
}

TEST(QmScoreboardFooter, EmptyBlockTakesNoSpace)
{
	CUIRect Area = {10.0f, 20.0f, 300.0f, 100.0f};
	float CursorY = Area.y;
	// 没有内容的区块不画、也不推进游标：后面的区块直接贴上来，中间不留空白。
	const CUIRect Empty = QmScoreboardFooterBlock(Area, CursorY, 0.0f, 25.0f, 5.0f);
	EXPECT_FLOAT_EQ(Empty.w, 0.0f);
	EXPECT_FLOAT_EQ(Empty.h, 0.0f);
	EXPECT_FLOAT_EQ(CursorY, Area.y);

	const CUIRect NoHeight = QmScoreboardFooterBlock(Area, CursorY, 120.0f, 0.0f, 5.0f);
	EXPECT_FLOAT_EQ(NoHeight.w, 0.0f);
	EXPECT_FLOAT_EQ(CursorY, Area.y);
}

TEST(QmScoreboardFooter, BlocksStackUpwardsWithoutGaps)
{
	CUIRect Area = {0.0f, 100.0f, 400.0f, 90.0f};
	float CursorY = Area.y;

	const CUIRect Media = QmScoreboardFooterBlock(Area, CursorY, 260.0f, 25.0f, 5.0f);
	const CUIRect Spectators = QmScoreboardFooterBlock(Area, CursorY, 180.0f, 21.0f, 5.0f);
	const CUIRect Hint = QmScoreboardFooterBlock(Area, CursorY, 140.0f, 20.0f, 0.0f);
	ASSERT_GT(Media.w, 0.0f);
	ASSERT_GT(Spectators.w, 0.0f);
	ASSERT_GT(Hint.w, 0.0f);

	// 依次紧贴：相邻块之间只有一个 gap，不存在被撑开的中间空白。
	EXPECT_FLOAT_EQ(Spectators.y, Media.y + Media.h + 5.0f);
	EXPECT_FLOAT_EQ(Hint.y, Spectators.y + Spectators.h + 5.0f);
	// 三块各自水平居中。
	EXPECT_FLOAT_EQ(Media.x, (400.0f - Media.w) * 0.5f);
	EXPECT_FLOAT_EQ(Spectators.x, (400.0f - Spectators.w) * 0.5f);
	EXPECT_FLOAT_EQ(Hint.x, (400.0f - Hint.w) * 0.5f);
	// 内容总高小于底栏高度时整排向上贴齐，剩余空白留在底部。
	EXPECT_LE(Hint.y + Hint.h, Area.y + Area.h);
}

TEST(QmScoreboardFooter, SkippedBlockMovesLaterBlocksUp)
{
	CUIRect Area = {0.0f, 100.0f, 400.0f, 90.0f};
	float CursorY = Area.y;

	// 媒体条存在、旁观者为空：提示条直接接在媒体条下面，不会停在被跳过的位置。
	const CUIRect Media = QmScoreboardFooterBlock(Area, CursorY, 260.0f, 25.0f, 5.0f);
	const CUIRect Skipped = QmScoreboardFooterBlock(Area, CursorY, 0.0f, 21.0f, 5.0f);
	const CUIRect Hint = QmScoreboardFooterBlock(Area, CursorY, 140.0f, 20.0f, 0.0f);
	EXPECT_FLOAT_EQ(Skipped.w, 0.0f);
	EXPECT_FLOAT_EQ(Hint.y, Media.y + Media.h + 5.0f);
}

TEST(QmScoreboardFooter, RemainingHeightNeverNegative)
{
	CUIRect Area = {0.0f, 10.0f, 100.0f, 50.0f};
	EXPECT_FLOAT_EQ(QmScoreboardFooterRemainingHeight(Area, 20.0f), 40.0f);
	EXPECT_FLOAT_EQ(QmScoreboardFooterRemainingHeight(Area, 60.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmScoreboardFooterRemainingHeight(Area, 90.0f), 0.0f);
}

TEST(QmScoreboardFooter, SpectatorPanelShrinksToMeasuredLines)
{
	// 10 行的可用高度，实际只排了 1 行：背景收缩到 1 行 + 上下留白。
	EXPECT_FLOAT_EQ(QmScoreboardSpectatorPanelHeight(100.0f, 1, 9, 11.0f, 10.0f), 21.0f);
	// 排满可用行数时取较小者，不越出可用高度。
	EXPECT_FLOAT_EQ(QmScoreboardSpectatorPanelHeight(100.0f, 20, 9, 11.0f, 10.0f), 100.0f);
	// 测量行数为 0 时只保留留白，不退化成负高度。
	EXPECT_FLOAT_EQ(QmScoreboardSpectatorPanelHeight(100.0f, 0, 9, 11.0f, 10.0f), 10.0f);
	// 内容超出上限时按上限截断。
	EXPECT_FLOAT_EQ(QmScoreboardSpectatorPanelHeight(500.0f, 40, 3, 11.0f, 10.0f), 43.0f);
}

TEST(QmScoreboardFooter, TallerPanelLeavesFooterInsideDefaultScreen)
{
	const float Height = QmScoreboardPanelHeight(600.0f, 75.0f);
	EXPECT_FLOAT_EQ(Height, 415.0f);
	CUIRect Footer{0.0f, 75.0f + Height + 5.0f, 450.0f, 100.0f};
	float CursorY = Footer.y;
	const CUIRect Media = QmScoreboardFooterBlock(Footer, CursorY, 450.0f, 25.0f, 5.0f);
	EXPECT_FLOAT_EQ(Media.h, 25.0f);
	EXPECT_LE(CursorY, 595.0f);
}

TEST(QmScoreboardFooter, EnlargedUiClampsPanelToLeaveFooterSpace)
{
	EXPECT_FLOAT_EQ(QmScoreboardPanelHeight(400.0f, 75.0f), 215.0f);
	EXPECT_FLOAT_EQ(QmScoreboardPanelHeight(1200.0f, 75.0f), 415.0f);
	EXPECT_FLOAT_EQ(QmScoreboardPanelHeight(100.0f, 75.0f), 0.0f);
}
