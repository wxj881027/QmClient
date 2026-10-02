#include <game/client/ui.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

TEST(QmUiScale, VirtualHeightUsesClampedPercentage)
{
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(50), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(100), 600.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(200), 300.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(0), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(300), 300.0f);
}

TEST(QmUiScale, CenteredPopupMarginKeepsUsableContentAtTwoHundredPercent)
{
	const CUIRect DefaultScreen = {0.0f, 0.0f, 1066.0f, 600.0f};
	const CUIRect ScaledScreen = {0.0f, 0.0f, 533.0f, 300.0f};
	const CUIRect NarrowScaledScreen = {0.0f, 0.0f, 375.0f, 300.0f};

	EXPECT_FLOAT_EQ(QmUiCenteredMargin(DefaultScreen, 150.0f, 300.0f, 180.0f), 150.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 180.0f), 60.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(NarrowScaledScreen, 150.0f, 300.0f, 180.0f), 37.5f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin({0.0f, 0.0f, 200.0f, 120.0f}, 150.0f, 300.0f, 180.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 300.0f), 0.0f);
	EXPECT_EQ(QmUiVisibleRows(52.0f, 20.0f, 20.0f, 4, 4), 1);
	EXPECT_EQ(QmUiVisibleRows(126.0f, 20.0f, 20.0f, 8, 4), 4);
	EXPECT_EQ(QmUiVisibleRows(19.0f, 20.0f, 20.0f, 4, 4), 0);
}
