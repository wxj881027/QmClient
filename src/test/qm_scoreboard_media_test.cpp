#include <game/client/components/qmclient/scoreboard_media_controls.h>
#include <gtest/gtest.h>

TEST(QmScoreboardMedia, RightRailFitsNarrowAndWideScreens)
{
	for(const float Width : {400.0f, 710.0f, 1066.0f})
	{
		const float BoardWidth = QmScoreboardMediaMaxWidth(Width, true);
		const CUIRect Board{(Width - BoardWidth) * 0.5f, 75.0f, BoardWidth, 385.0f};
		const CUIRect Rail = QmScoreboardMediaRail(Board, {0.0f, 0.0f, Width, 600.0f});
		EXPECT_GT(Rail.x, Board.x + Board.w);
		EXPECT_LE(Rail.x + Rail.w, Width - 5.0f);
		EXPECT_GE(Rail.y, Board.y);
		EXPECT_LE(Rail.y + Rail.h, Board.y + Board.h);
	}
}

TEST(QmScoreboardMedia, VolumeTrackMapsTopToMaximumAndClampsDrag)
{
	const CUIRect Track{10.0f, 20.0f, 4.0f, 100.0f};
	EXPECT_FLOAT_EQ(QmScoreboardMediaVolumeAt(20.0f, Track), 1.0f);
	EXPECT_FLOAT_EQ(QmScoreboardMediaVolumeAt(70.0f, Track), 0.5f);
	EXPECT_FLOAT_EQ(QmScoreboardMediaVolumeAt(120.0f, Track), 0.0f);
	EXPECT_FLOAT_EQ(QmScoreboardMediaVolumeAt(-20.0f, Track), 1.0f);
	EXPECT_FLOAT_EQ(QmScoreboardMediaVolumeAt(200.0f, Track), 0.0f);
}

TEST(QmScoreboardMedia, LargeUiScaleKeepsWholeVolumeControlOnScreen)
{
	const CUIRect Screen{0.0f, 0.0f, 400.0f, 300.0f};
	const CUIRect Board{38.0f, 75.0f, 324.0f, 385.0f};
	const CUIRect Rail = QmScoreboardMediaRail(Board, Screen);
	EXPECT_GE(Rail.y, Screen.y);
	EXPECT_LE(Rail.y + Rail.h, Screen.y + Screen.h);
}
