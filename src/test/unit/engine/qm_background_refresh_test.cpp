#include <engine/shared/qm_background_refresh.h>

#include <gtest/gtest.h>

TEST(QmBackgroundRefresh, FollowDisplayUsesDetectedDisplayRate)
{
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, 160, 0), 160);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, 1000, 0), 1000);
}

TEST(QmBackgroundRefresh, FollowDisplayFallsBackWhenDisplayRateIsUnusable)
{
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, 0, 0), QM_BACKGROUND_REFRESH_FALLBACK_RATE);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, 5, 0), QM_BACKGROUND_REFRESH_FALLBACK_RATE);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, 1001, 0), QM_BACKGROUND_REFRESH_FALLBACK_RATE);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 144, -1, 0), QM_BACKGROUND_REFRESH_FALLBACK_RATE);
}

TEST(QmBackgroundRefresh, CustomUsesStoredInactiveRate)
{
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::CUSTOM, 144, 160, 0), 144);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::CUSTOM, 0, 160, 0), 0);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::CUSTOM, -5, 160, 0), 0);
}

TEST(QmBackgroundRefresh, UnlimitedIgnoresAllRates)
{
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::UNLIMITED, 144, 160, 120), 0);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::UNLIMITED, 0, 0, 0), 0);
}

TEST(QmBackgroundRefresh, BackgroundNeverExceedsActiveUpdateRate)
{
	// 前台把更新频率压到 120 时，失焦后不能反而按 160 跑。
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 0, 160, 120), 120);
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::CUSTOM, 200, 160, 144), 144);
	// 前台不限（0）时后台按自己的上限走。
	EXPECT_EQ(QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode::FOLLOW_DISPLAY, 0, 160, 0), 160);
}

TEST(QmBackgroundRefresh, UnknownModeFallsBackToFollowDisplay)
{
	EXPECT_EQ(QmBackgroundRefreshModeFromConfig(-1), EQmBackgroundRefreshMode::FOLLOW_DISPLAY);
	EXPECT_EQ(QmBackgroundRefreshModeFromConfig(3), EQmBackgroundRefreshMode::FOLLOW_DISPLAY);
	EXPECT_EQ(QmBackgroundRefreshModeFromConfig(1), EQmBackgroundRefreshMode::CUSTOM);
	EXPECT_EQ(QmBackgroundRefreshModeFromConfig(2), EQmBackgroundRefreshMode::UNLIMITED);
}
