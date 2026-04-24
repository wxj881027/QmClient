#include <gtest/gtest.h>

#include <base/system.h>

#include <game/client/components/menus.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

TEST(AudioPackEditor, ExportPathUsesAudioPackDirectory)
{
	EXPECT_EQ(CMenus::BuildAudioPackExportPath("my_pack", "hit.wav"), "audio/my_pack/hit.wav");
	EXPECT_EQ(CMenus::BuildAudioPackExportPath("my_pack", "audio/hook_attach.opus"), "audio/my_pack/audio/hook_attach.opus");
}

TEST(AudioPackEditor, SlotsFlattenGeneratedSoundEntriesIntoRelativePaths)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	ASSERT_FALSE(vSlots.empty());

	bool FoundGunFire = false;
	bool FoundMenu = false;

	for(const auto &Slot : vSlots)
	{
		ASSERT_NE(Slot.m_pDisplayName, nullptr);
		ASSERT_NE(Slot.m_pSetName, nullptr);
		ASSERT_NE(Slot.m_pRelativePath, nullptr);
		EXPECT_NE(Slot.m_pRelativePath[0], '\0');
		EXPECT_FALSE(str_startswith(Slot.m_pRelativePath, "audio/"));
		EXPECT_GE(Slot.m_VariantIndex, 0);
		EXPECT_GE(Slot.m_VariantCount, 1);
		EXPECT_LT(Slot.m_VariantIndex, Slot.m_VariantCount);

		if(str_comp(Slot.m_pSetName, "gun_fire") == 0 && str_comp(Slot.m_pRelativePath, "wp_gun_fire-01.wv") == 0)
		{
			FoundGunFire = true;
			EXPECT_EQ(Slot.m_VariantIndex, 0);
			EXPECT_EQ(Slot.m_VariantCount, 3);
		}

		if(str_comp(Slot.m_pSetName, "menu") == 0 && str_comp(Slot.m_pRelativePath, "music_menu.wv") == 0)
		{
			FoundMenu = true;
			EXPECT_EQ(Slot.m_VariantIndex, 0);
			EXPECT_EQ(Slot.m_VariantCount, 1);
		}
	}

	EXPECT_TRUE(FoundGunFire);
	EXPECT_TRUE(FoundMenu);
}

TEST(AudioPackEditor, CandidateEntriesPreferCurrentFileAndCurrentPackFiles)
{
	const std::vector<std::string> vPaths = {
		"audio/other/menu_click.wv",
		"audio/my_pack/hook_attach.wv",
		"audio/my_pack/wp_gun_fire-01.wv",
		"audio/default/wp_gun_fire-01.wv",
		"audio/default/readme.txt",
	};

	const auto vEntries = CMenus::BuildAudioPackCandidateEntries(vPaths, "my_pack", "audio/my_pack/wp_gun_fire-01.wv");
	ASSERT_EQ(vEntries.size(), 4u);

	EXPECT_EQ(vEntries[0].m_Path, "audio/my_pack/wp_gun_fire-01.wv");
	EXPECT_EQ(vEntries[0].m_DisplayName, "my_pack/wp_gun_fire-01.wv");
	EXPECT_TRUE(vEntries[0].m_IsCurrentFile);
	EXPECT_TRUE(vEntries[0].m_IsCurrentPackFile);

	EXPECT_EQ(vEntries[1].m_Path, "audio/my_pack/hook_attach.wv");
	EXPECT_TRUE(vEntries[1].m_IsCurrentPackFile);
	EXPECT_FALSE(vEntries[1].m_IsCurrentFile);

	EXPECT_FALSE(vEntries[2].m_IsCurrentPackFile);
	EXPECT_FALSE(vEntries[3].m_IsCurrentPackFile);
}

TEST(AudioPackEditor, CandidateEntriesKeepBuiltinDefaultsVisibleWithoutDuplicates)
{
	const std::vector<std::string> vPaths = {
		"audio/music_menu.wv",
		"audio/music_menu.wv",
		"audio/wp_gun_fire-01.wv",
		"audio/my_pack/music_menu.wv",
	};

	const auto vEntries = CMenus::BuildAudioPackCandidateEntries(vPaths, "my_pack", "audio/my_pack/music_menu.wv");
	ASSERT_EQ(vEntries.size(), 3u);

	EXPECT_EQ(vEntries[0].m_Path, "audio/my_pack/music_menu.wv");
	EXPECT_TRUE(vEntries[0].m_IsCurrentFile);
	EXPECT_TRUE(vEntries[0].m_IsCurrentPackFile);

	EXPECT_EQ(vEntries[1].m_Path, "audio/music_menu.wv");
	EXPECT_EQ(vEntries[1].m_DisplayName, "music_menu.wv");
	EXPECT_FALSE(vEntries[1].m_IsCurrentPackFile);

	EXPECT_EQ(vEntries[2].m_Path, "audio/wp_gun_fire-01.wv");
}

TEST(AudioPackEditor, BuiltinDefaultCandidatesEnterThroughScanChain)
{
	const auto vRoots = CMenus::BuildAudioPackCandidateScanRoots();
	std::set<std::string> vCandidatePaths;
	bool FoundBuiltinRoot = false;

	for(const auto &Root : vRoots)
	{
		if(str_comp(Root.m_pScanRoot, "data/audio") != 0)
			continue;

		FoundBuiltinRoot = true;
		std::string CandidatePath;
		ASSERT_TRUE(CMenus::TryBuildAudioPackCandidatePathFromScan(Root.m_pOutputPrefix, "music_menu.wv", CandidatePath));
		vCandidatePaths.insert(CandidatePath);
	}

	ASSERT_TRUE(FoundBuiltinRoot);

	const std::vector<std::string> vPaths(vCandidatePaths.begin(), vCandidatePaths.end());
	const auto vEntries = CMenus::BuildAudioPackCandidateEntries(vPaths, "my_pack", "");
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(vEntries[0].m_Path, "audio/music_menu.wv");
	EXPECT_EQ(vEntries[0].m_DisplayName, "music_menu.wv");
}

TEST(AudioPackEditor, PreviewPathPrefersSelectedCandidateOverManualImport)
{
	EXPECT_EQ(CMenus::ResolveAudioPackPreviewPath("audio/default/music_menu.wv", "C:/temp/custom_music.wv"), "audio/default/music_menu.wv");
	EXPECT_EQ(CMenus::ResolveAudioPackPreviewPath("", "C:/temp/custom_music.wv"), "C:/temp/custom_music.wv");
	EXPECT_TRUE(CMenus::ResolveAudioPackPreviewPath("", "").empty());
}

TEST(AudioPackEditor, ExportSourcePathPrefersManualImportOverSelectedCandidate)
{
	EXPECT_EQ(CMenus::ResolveAudioPackExportSourcePath("audio/default/music_menu.wv", "C:/temp/custom_music.wv"), "C:/temp/custom_music.wv");
	EXPECT_EQ(CMenus::ResolveAudioPackExportSourcePath("audio/default/music_menu.wv", ""), "audio/default/music_menu.wv");
	EXPECT_TRUE(CMenus::ResolveAudioPackExportSourcePath("", "").empty());
}
