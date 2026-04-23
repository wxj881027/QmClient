#include <gtest/gtest.h>

#include <game/client/components/theme_scan.h>

TEST(ThemeScan, FileCandidateDetection)
{
	EXPECT_TRUE(IsThemeFileCandidate("heavens.map"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.png"));
	EXPECT_TRUE(IsThemeFileCandidate("HEAVENS.PNG"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.txt"));
	EXPECT_FALSE(IsThemeFileCandidate("themes"));
}

TEST(ThemeScan, IconPathMapping)
{
	EXPECT_EQ(ThemeIconPathFromName(""), "themes/none.png");
	EXPECT_EQ(ThemeIconPathFromName("auto"), "themes/auto.png");
	EXPECT_EQ(ThemeIconPathFromName("rand"), "themes/rand.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.map"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.map"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn_night.map"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.png"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("AUTUMN.PNG"), "themes/AUTUMN.PNG");
}
