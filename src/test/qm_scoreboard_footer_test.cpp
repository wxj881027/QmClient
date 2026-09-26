#include <game/client/components/qmclient/scoreboard_footer.h>

#include <gtest/gtest.h>

TEST(QmScoreboardFooter, MediaBarOnlyTakesSpaceWhenThereIsSongInfo)
{
	CUIRect Area = {10.0f, 20.0f, 300.0f, 100.0f};

	// 无歌曲信息：媒体条高度为 0，旁观者区域拿到整块空间。
	const SQmScoreboardFooterLayout NoMedia = QmScoreboardFooterLayout(Area, false, true);
	EXPECT_FLOAT_EQ(NoMedia.m_Media.h, 0.0f);
	EXPECT_FLOAT_EQ(NoMedia.m_Spectators.h, 100.0f);
	EXPECT_FLOAT_EQ(NoMedia.m_Spectators.y, 20.0f);
	EXPECT_FLOAT_EQ(NoMedia.m_Spectators.w, 300.0f);

	// 有歌曲信息：媒体条占 25，下方减去 5 的间隙。
	const SQmScoreboardFooterLayout WithMedia = QmScoreboardFooterLayout(Area, true, true);
	EXPECT_FLOAT_EQ(WithMedia.m_Media.h, 25.0f);
	EXPECT_FLOAT_EQ(WithMedia.m_Media.y, 20.0f);
	EXPECT_FLOAT_EQ(WithMedia.m_Spectators.y, 50.0f);
	EXPECT_FLOAT_EQ(WithMedia.m_Spectators.h, 70.0f);

	// 有歌曲信息但没有旁观者：不留下旁观者区域。
	const SQmScoreboardFooterLayout MediaOnly = QmScoreboardFooterLayout(Area, true, false);
	EXPECT_FLOAT_EQ(MediaOnly.m_Media.h, 25.0f);
	EXPECT_FLOAT_EQ(MediaOnly.m_Spectators.h, 0.0f);

	// 两者都没有：整块底栏不占任何绘制区域。
	const SQmScoreboardFooterLayout Empty = QmScoreboardFooterLayout(Area, false, false);
	EXPECT_FLOAT_EQ(Empty.m_Media.h, 0.0f);
	EXPECT_FLOAT_EQ(Empty.m_Spectators.h, 0.0f);
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
