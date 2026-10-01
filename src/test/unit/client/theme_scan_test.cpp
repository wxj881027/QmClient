#include <game/client/components/theme_scan.h>

#include <gtest/gtest.h>

TEST(ThemeScan, FileCandidateDetection)
{
	EXPECT_TRUE(IsThemeFileCandidate("heavens.map"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.png"));
	EXPECT_TRUE(IsThemeFileCandidate("HEAVENS.PNG"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.webp"));
#if defined(CONF_VIDEORECORDER)
	EXPECT_TRUE(IsThemeFileCandidate("heavens.jpg"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.jpeg"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.jfif"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.bmp"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.tga"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.tif"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.tiff"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.gif"));
#else
	EXPECT_FALSE(IsThemeFileCandidate("heavens.jpg"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.jpeg"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.jfif"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.bmp"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.tga"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.tif"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.tiff"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.gif"));
#endif
	EXPECT_TRUE(IsThemeFileCandidate("heavens.mp4"));
	EXPECT_TRUE(IsThemeFileCandidate("heavens.webm"));
	EXPECT_FALSE(IsThemeFileCandidate("heavens.txt"));
	EXPECT_FALSE(IsThemeFileCandidate("themes"));
}

TEST(ThemeScan, CandidatePathBuilding)
{
	EXPECT_EQ(BuildThemeCandidatePath("themes/autumn_night", ".map"), "themes/autumn_night.map");
	EXPECT_EQ(BuildThemeCandidatePath("themes/jungle_day", ".png"), "themes/jungle_day.png");
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
#if defined(CONF_VIDEORECORDER)
	EXPECT_EQ(ThemeIconPathFromName("autumn.jpg"), "themes/autumn.jpg");
	EXPECT_EQ(ThemeIconPathFromName("autumn.jpeg"), "themes/autumn.jpeg");
	EXPECT_EQ(ThemeIconPathFromName("autumn.jfif"), "themes/autumn.jfif");
	EXPECT_EQ(ThemeIconPathFromName("autumn.bmp"), "themes/autumn.bmp");
	EXPECT_EQ(ThemeIconPathFromName("autumn.tga"), "themes/autumn.tga");
	EXPECT_EQ(ThemeIconPathFromName("autumn.tiff"), "themes/autumn.tiff");
	EXPECT_EQ(ThemeIconPathFromName("autumn.gif"), "themes/autumn.gif");
#else
	EXPECT_EQ(ThemeIconPathFromName("autumn.jpg"), "themes/autumn.jpg.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.jpeg"), "themes/autumn.jpeg.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.jfif"), "themes/autumn.jfif.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.bmp"), "themes/autumn.bmp.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.tga"), "themes/autumn.tga.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.tiff"), "themes/autumn.tiff.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn.gif"), "themes/autumn.gif.png");
#endif
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.webp"), "themes/autumn_day.webp");
#if defined(CONF_VIDEORECORDER)
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.jpg"), "themes/autumn_day.jpg");
#else
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.jpg"), "themes/autumn_day.jpg.png");
#endif
	EXPECT_EQ(ThemeIconPathFromName("autumn.mp4"), "themes/autumn.png");
	EXPECT_EQ(ThemeIconPathFromName("autumn_day.mp4"), "themes/autumn.png");
}
