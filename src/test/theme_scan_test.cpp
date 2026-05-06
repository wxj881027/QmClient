#include <gtest/gtest.h>

#include <game/client/components/theme_scan.h>

TEST(ThemeScan, FileCandidateDetection)
{
	EXPECT_TRUE(IsThemeFileCandidate("heavens.map"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.png"));
	EXPECT_TRUE(IsThemeFileCandidate("HEAVENS.PNG"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.webp"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.mp4"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.webm"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.jpg"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.jpeg"));
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
	EXPECT_EQ(ThemeIconPathFromName("autumn.webp"), "themes/autumn.webp");
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.webp"), "themes/autumn_day.webp");
	EXPECT_EQ(ThemeIconPathFromName("autumn.mp4"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.mp4"), "themes/autumn.png");
}
