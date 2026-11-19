#include <base/system.h>

#include <game/client/QmUi/AudioPackEditorLayout.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>

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

TEST(AudioPackCatalog, UnloadedVariantsHaveNoEngineSample)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	ASSERT_FALSE(vSlots.empty());
	for(const auto &Slot : vSlots)
	{
		const auto &Sample = g_pData->m_aSounds[Slot.m_SetId].m_aSounds[Slot.m_VariantIndex];
		EXPECT_EQ(Sample.m_Id, -1) << Slot.m_pRelativePath;
	}
}

TEST(AudioPackCatalog, EveryGeneratedSoundGroupHasUsageAndKeepsEveryVariant)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	ASSERT_EQ(vGroups.size(), (size_t)g_pData->m_NumSounds);
	std::set<int> SeenSlots;
	for(const auto &Group : vGroups)
	{
		ASSERT_NE(Group.m_pInfo, nullptr);
		EXPECT_STREQ(Group.m_pInfo->m_pSetName, Group.m_pSetName);
		EXPECT_NE(Group.m_pInfo->m_pUsage[0], '\0');
		EXPECT_EQ(Group.m_vSlotIndices.size(), (size_t)g_pData->m_aSounds[Group.m_SetId].m_NumSounds);
		for(int SlotIndex : Group.m_vSlotIndices)
		{
			ASSERT_GE(SlotIndex, 0);
			ASSERT_LT(SlotIndex, (int)vSlots.size());
			EXPECT_TRUE(SeenSlots.insert(SlotIndex).second);
			EXPECT_EQ(vSlots[SlotIndex].m_SetId, Group.m_SetId);
		}
	}
	EXPECT_EQ(SeenSlots.size(), vSlots.size());
}

TEST(AudioPackCatalog, FilenameSearchFindsTheWholeGroupIncludingOtherVariants)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	const auto vVisible = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "WP_GUN_FIRE-03");
	ASSERT_EQ(vVisible.size(), 1u);
	const auto &Group = vGroups[vVisible[0]];
	EXPECT_STREQ(Group.m_pSetName, "gun_fire");
	ASSERT_EQ(Group.m_vSlotIndices.size(), 3u);
	EXPECT_STREQ(vSlots[Group.m_vSlotIndices.front()].m_pRelativePath, "wp_gun_fire-01.wv");
}

namespace
{
	const char *TranslateAudioCatalogForTest(const char *pText)
	{
		if(str_comp(pText, "Ground jump") == 0)
			return "地面起跳";
		if(str_comp(pText, "Movement") == 0)
			return "移动";
		if(str_comp(pText, qm_audio_pack::SoundInfo("player_jump").m_pUsage) == 0)
			return "左右脚文件在地面跳跃时使用";
		return pText;
	}
}

TEST(AudioPackCatalog, SearchIncludesTranslatedNameUsageAndCategory)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	for(const char *pQuery : {"地面起跳", "左右脚"})
	{
		SCOPED_TRACE(pQuery);
		const auto vVisible = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, pQuery, TranslateAudioCatalogForTest);
		ASSERT_EQ(vVisible.size(), 1u);
		EXPECT_STREQ(vGroups[vVisible[0]].m_pSetName, "player_jump");
	}
	const auto vMovement = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "移动", TranslateAudioCatalogForTest);
	ASSERT_FALSE(vMovement.empty());
	for(int Index : vMovement)
		EXPECT_EQ(vGroups[Index].m_pInfo->m_Category, qm_audio_pack::ECategory::MOVEMENT);
}

TEST(AudioPackCatalog, CategoryAndSearchBothRestrictResults)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	EXPECT_TRUE(qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::HOOK, "gun_fire").empty());
	EXPECT_TRUE(qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "missing_sound_file").empty());
	EXPECT_EQ(qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "").size(), vGroups.size());
	const auto vHook = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::HOOK, "");
	ASSERT_FALSE(vHook.empty());
	for(int Index : vHook)
		EXPECT_EQ(vGroups[Index].m_pInfo->m_Category, qm_audio_pack::ECategory::HOOK);
}

TEST(AudioPackCatalog, SelectingAGroupKeepsItsVariantAndSwitchingGroupsSelectsTheFirstVariant)
{
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	const auto vGun = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "wp_gun_fire-01");
	ASSERT_EQ(vGun.size(), 1u);
	const auto &Gun = vGroups[vGun[0]];
	ASSERT_GE(Gun.m_vSlotIndices.size(), 2u);
	EXPECT_EQ(qm_audio_pack::SelectGroupSlot(Gun, Gun.m_vSlotIndices[1]), Gun.m_vSlotIndices[1]);
	EXPECT_EQ(qm_audio_pack::SelectedGroup(vGroups, Gun.m_vSlotIndices[1]), vGun[0]);
	const auto vMenu = qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::ALL, "music_menu.wv");
	ASSERT_EQ(vMenu.size(), 1u);
	const auto &Menu = vGroups[vMenu[0]];
	EXPECT_EQ(qm_audio_pack::SelectGroupSlot(Menu, Gun.m_vSlotIndices[1]), Menu.m_vSlotIndices.front());
	EXPECT_EQ(qm_audio_pack::SelectedGroup(vGroups, -1), -1);
}

TEST(AudioPackCatalog, UnrecognizedSoundsRemainAccessibleWithoutInventingUsage)
{
	const std::vector<qm_audio_pack::SSlot> vSlots = {{"future.wv", "future_sound", "future.wv", 100, 0, 1}};
	const auto vGroups = qm_audio_pack::BuildGroups(vSlots);
	ASSERT_EQ(vGroups.size(), 1u);
	EXPECT_EQ(vGroups[0].m_pInfo->m_Category, qm_audio_pack::ECategory::OTHER);
	EXPECT_EQ(vGroups[0].m_pInfo->m_Context, qm_audio_pack::EContext::SERVER_DEPENDENT);
	EXPECT_EQ(qm_audio_pack::FilterGroups(vGroups, vSlots, qm_audio_pack::ECategory::OTHER, "future.wv").size(), 1u);
}

TEST(AudioPackEditorLayout, NarrowAndScaledViewsKeepBothPanelsInsideTheViewport)
{
	for(const auto &[Width, Scale] : {std::pair{900.0f, 1.0f}, {420.0f, 1.0f}, {900.0f, 2.0f}, {0.0f, 1.0f}})
	{
		SCOPED_TRACE(Width);
		SCOPED_TRACE(Scale);
		const CUIRect View{20.0f, 30.0f, Width, 400.0f};
		const auto Layout = qm_audio_pack::ResolveEditorLayout(View, Scale);
		for(const CUIRect &Panel : {Layout.m_Catalog, Layout.m_Workspace})
		{
			EXPECT_GE(Panel.w, 0.0f);
			EXPECT_GE(Panel.h, 0.0f);
			EXPECT_GE(Panel.x, View.x);
			EXPECT_GE(Panel.y, View.y);
			EXPECT_LE(Panel.x + Panel.w, View.x + View.w + 0.001f);
			EXPECT_LE(Panel.y + Panel.h, View.y + View.h + 0.001f);
		}
		if(Layout.m_Stacked)
			EXPECT_LE(Layout.m_Catalog.y + Layout.m_Catalog.h, Layout.m_Workspace.y);
		else
			EXPECT_LE(Layout.m_Catalog.x + Layout.m_Catalog.w, Layout.m_Workspace.x);
	}
}
