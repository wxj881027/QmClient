#include <engine/client/plausible_sizes.h>

#include <gtest/gtest.h>

TEST(PlausibleSizes, RefreshRateAndWindowGuardsMatchContract)
{
	// Refresh rate: 0..1000 inclusive is the persisted-range contract.
	EXPECT_TRUE(IsPlausibleRefreshRate(0));
	EXPECT_TRUE(IsPlausibleRefreshRate(60));
	EXPECT_TRUE(IsPlausibleRefreshRate(1000));
	EXPECT_FALSE(IsPlausibleRefreshRate(-1));
	EXPECT_FALSE(IsPlausibleRefreshRate(1001));
	// Window size: 320..16384 on both axes.
	EXPECT_TRUE(IsPlausibleWindowSize(320, 240));
	EXPECT_TRUE(IsPlausibleWindowSize(16384, 16384));
	EXPECT_FALSE(IsPlausibleWindowSize(319, 240)); // under min width
	EXPECT_FALSE(IsPlausibleWindowSize(320, 239)); // under min height
	EXPECT_FALSE(IsPlausibleWindowSize(16385, 1080)); // over max width
	EXPECT_FALSE(IsPlausibleWindowSize(1920, 16385)); // over max height
}
