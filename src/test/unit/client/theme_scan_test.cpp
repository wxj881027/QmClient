#include <game/client/components/theme_scan.h>

#include <gtest/gtest.h>

TEST(ThemeScan, FileCandidateDetection)
{
	for(const char *pName : {"heavens.map", "HEAVENS.MAP", "heavens.png", "HEAVENS.PNG", "heavens.webp", "heavens.mp4", "HEAVENS.MP4", "heavens.webm", "heavens.mov", "heavens.mkv"})
		EXPECT_TRUE(IsThemeFileCandidate(pName)) << pName;
#if defined(CONF_VIDEORECORDER)
	for(const char *pName : {"heavens.jpg", "HEAVENS.JPEG", "heavens.jfif", "heavens.bmp", "heavens.tga", "heavens.tif", "heavens.tiff", "heavens.gif"})
		EXPECT_TRUE(IsThemeFileCandidate(pName)) << pName;
#else
	for(const char *pName : {"heavens.jpg", "heavens.jpeg", "heavens.jfif", "heavens.bmp", "heavens.tga", "heavens.tif", "heavens.tiff", "heavens.gif"})
		EXPECT_FALSE(IsThemeFileCandidate(pName)) << pName;
#endif
}

TEST(ThemeScan, UnsupportedFilesAndEmptyNamesAreRejected)
{
	for(const char *pName : {"heavens.txt", "themes", ".map", ".png", ".jpeg", ".mp4", ""})
		EXPECT_FALSE(IsThemeFileCandidate(pName)) << pName;
	EXPECT_FALSE(IsThemeFileCandidate(nullptr));
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

TEST(ThemeScan, PreviewImagesRemainIndependentlySelectable)
{
	std::vector<SThemeFileEntry> Entries;
	AppendThemeFileEntry(Entries, "none.png", false);
	AppendThemeFileEntry(Entries, "auto.png", false);
	AppendThemeFileEntry(Entries, "rand.png", false);
	AppendThemeFileEntry(Entries, "autumn.png", false);
	ASSERT_EQ(Entries.size(), 4u);
	EXPECT_EQ(Entries[0].m_Name, "none.png");
	EXPECT_EQ(Entries[1].m_Name, "auto.png");
	EXPECT_EQ(Entries[2].m_Name, "rand.png");
	EXPECT_EQ(Entries[3].m_Name, "autumn.png");
	EXPECT_EQ(Entries[0].m_IconPath, "themes/none.png");
	EXPECT_EQ(Entries[3].m_IconPath, "themes/autumn.png");
}

TEST(ThemeScan, MapVariantsImagesAndVideosWithTheSameBaseRemainSeparate)
{
	std::vector<SThemeFileEntry> Entries;
	AppendThemeFileEntry(Entries, "autumn_night.map", false);
	AppendThemeFileEntry(Entries, "autumn_day.map", false);
	AppendThemeFileEntry(Entries, "autumn.png", false);
	AppendThemeFileEntry(Entries, "autumn.webp", false);
	AppendThemeFileEntry(Entries, "autumn.mp4", false);
	ASSERT_EQ(Entries.size(), 5u);
	EXPECT_EQ(Entries[0].m_Name, "autumn_night.map");
	EXPECT_EQ(Entries[1].m_Name, "autumn_day.map");
	EXPECT_EQ(Entries[2].m_Name, "autumn.png");
	EXPECT_EQ(Entries[3].m_Name, "autumn.webp");
	EXPECT_EQ(Entries[4].m_Name, "autumn.mp4");
	EXPECT_EQ(Entries[0].m_IconPath, "themes/autumn.png");
	EXPECT_EQ(Entries[1].m_IconPath, "themes/autumn.png");
	EXPECT_EQ(Entries[2].m_IconPath, "themes/autumn.png");
	EXPECT_EQ(Entries[3].m_IconPath, "themes/autumn.webp");
	EXPECT_EQ(Entries[4].m_IconPath, "themes/autumn.png");
}

TEST(ThemeScan, DuplicateFilesAcrossStoragePathsProduceOneEntry)
{
	std::vector<SThemeFileEntry> Entries;
	AppendThemeFileEntry(Entries, "autumn_day.map", false);
	AppendThemeFileEntry(Entries, "autumn_day.map", false);
	AppendThemeFileEntry(Entries, "autumn.png", false);
	AppendThemeFileEntry(Entries, "autumn.png", false);
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_EQ(Entries[0].m_Name, "autumn_day.map");
	EXPECT_EQ(Entries[1].m_Name, "autumn.png");
}

TEST(ThemeScan, DiscoveryPreservesCaseAndImagePreviewPath)
{
	std::vector<SThemeFileEntry> Entries;
	AppendThemeFileEntry(Entries, "Desert_DAY.MAP", false);
	AppendThemeFileEntry(Entries, "Photo.WEBP", false);
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_EQ(Entries[0].m_Name, "Desert_DAY.MAP");
	EXPECT_EQ(Entries[1].m_Name, "Photo.WEBP");
	EXPECT_EQ(Entries[1].m_IconPath, "themes/Photo.WEBP");
#if defined(CONF_VIDEORECORDER)
	AppendThemeFileEntry(Entries, "Photo.JPEG", false);
	ASSERT_EQ(Entries.size(), 3u);
	EXPECT_EQ(Entries[2].m_Name, "Photo.JPEG");
	EXPECT_EQ(Entries[2].m_IconPath, "themes/Photo.JPEG");
#endif
}

TEST(ThemeScan, DirectoriesAndUnsupportedFilesDoNotBecomeThemes)
{
	std::vector<SThemeFileEntry> Entries;
	AppendThemeFileEntry(Entries, "folder.map", true);
	AppendThemeFileEntry(Entries, "folder.png", true);
	AppendThemeFileEntry(Entries, "notes.txt", false);
	AppendThemeFileEntry(Entries, nullptr, false);
	EXPECT_TRUE(Entries.empty());
}

TEST(ThemeScan, FreshScanOnlyContainsRemainingFiles)
{
	std::vector<SThemeFileEntry> Before;
	AppendThemeFileEntry(Before, "autumn_day.map", false);
	AppendThemeFileEntry(Before, "autumn.png", false);
	std::vector<SThemeFileEntry> Refreshed;
	AppendThemeFileEntry(Refreshed, "autumn.png", false);
	ASSERT_EQ(Refreshed.size(), 1u);
	EXPECT_EQ(Refreshed[0].m_Name, "autumn.png");
	EXPECT_EQ(Before.size(), 2u);
}

TEST(ThemeScan, DisplayNamesPreserveBuiltinsAndFullFileNames)
{
	EXPECT_EQ(ThemeDisplayName(""), "(none)");
	EXPECT_EQ(ThemeDisplayName("auto"), "(seasons)");
	EXPECT_EQ(ThemeDisplayName("rand"), "(random)");
	for(const char *pName : {"autumn_day.map", "Desert_NIGHT.MAP", "foo_day_day.map", "custom_night", "My (theme).jpeg", "My (theme).mp4", "auto.png"})
		EXPECT_EQ(ThemeDisplayName(pName), pName);
}

TEST(ThemeScan, ExplicitSelectionKeepsTheChosenFileInEitherTimeOfDay)
{
	for(bool IsDaytime : {false, true})
	{
		SCOPED_TRACE(IsDaytime);
		for(const char *pName : {"", "auto", "rand", "autumn_day.map", "autumn_night.map", "autumn.png", "autumn.mp4"})
			EXPECT_EQ(ResolveThemeSelectionMatch(pName, pName, IsDaytime), EThemeSelectionMatch::EXACT) << pName;
		EXPECT_EQ(ResolveThemeSelectionMatch("autumn_day.map", "autumn_night.map", IsDaytime), EThemeSelectionMatch::NONE);
		EXPECT_EQ(ResolveThemeSelectionMatch("autumn.map", "autumn.png", IsDaytime), EThemeSelectionMatch::NONE);
		EXPECT_EQ(ResolveThemeSelectionMatch("autumn.png", "autumn.mp4", IsDaytime), EThemeSelectionMatch::NONE);
	}
}

TEST(ThemeScan, LegacyBaseNamePrefersCurrentTimeThenBaseThenOtherTime)
{
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_day.map", "autumn", true), EThemeSelectionMatch::PREFERRED_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_night.map", "autumn", false), EThemeSelectionMatch::PREFERRED_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn.map", "autumn", true), EThemeSelectionMatch::ANY_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_night.map", "autumn", true), EThemeSelectionMatch::OTHER_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_day.map", "autumn", false), EThemeSelectionMatch::OTHER_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("Desert_DAY.MAP", "Desert", true), EThemeSelectionMatch::PREFERRED_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("foo_day_day.map", "foo_day", true), EThemeSelectionMatch::PREFERRED_TIME);
	EXPECT_EQ(ResolveThemeSelectionMatch("foo_day.map", "foo_day", true), EThemeSelectionMatch::ANY_TIME);
	EXPECT_GT(EThemeSelectionMatch::EXACT, EThemeSelectionMatch::PREFERRED_TIME);
	EXPECT_GT(EThemeSelectionMatch::PREFERRED_TIME, EThemeSelectionMatch::ANY_TIME);
	EXPECT_GT(EThemeSelectionMatch::ANY_TIME, EThemeSelectionMatch::OTHER_TIME);
	EXPECT_GT(EThemeSelectionMatch::OTHER_TIME, EThemeSelectionMatch::NONE);
}

TEST(ThemeScan, LegacyBaseNameDoesNotSelectUnrelatedOrPreviewFiles)
{
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn.png", "autumn", true), EThemeSelectionMatch::NONE);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn.mp4", "autumn", true), EThemeSelectionMatch::NONE);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_festival.map", "autumn", true), EThemeSelectionMatch::NONE);
	EXPECT_EQ(ResolveThemeSelectionMatch("winter_day.map", "autumn", true), EThemeSelectionMatch::NONE);
	EXPECT_EQ(ResolveThemeSelectionMatch("autumn_day.map", "", true), EThemeSelectionMatch::NONE);
}
