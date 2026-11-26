#include <base/system.h>

#include <game/client/QmUi/AudioPackEditorLayout.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
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

TEST(AudioPackEditor, PreviewUsesTheRequestedSourceAndItsStorageType)
{
	const auto Candidate = qm_audio_pack::ResolvePreviewSource("audio/default/music_menu.wv", false);
	EXPECT_STREQ(Candidate.m_pPath, "audio/default/music_menu.wv");
	EXPECT_EQ(Candidate.m_StorageType, IStorage::TYPE_ALL);
	const auto Manual = qm_audio_pack::ResolvePreviewSource("C:/temp/custom_music.wv", true);
	EXPECT_STREQ(Manual.m_pPath, "C:/temp/custom_music.wv");
	EXPECT_EQ(Manual.m_StorageType, IStorage::TYPE_ABSOLUTE);
	EXPECT_STREQ(qm_audio_pack::ResolvePreviewSource(nullptr, false).m_pPath, "");
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

TEST(AudioPackEditorLayout, ExtraHeightGoesEntirelyToCandidateFiles)
{
	const auto Short = qm_audio_pack::ResolveWorkspaceLayout({12, 20, 350, 290}, 100, 24, 36, 80);
	const auto Tall = qm_audio_pack::ResolveWorkspaceLayout({12, 20, 350, 490}, 100, 24, 36, 80);
	EXPECT_FLOAT_EQ(Short.m_Details.h, 100);
	EXPECT_FLOAT_EQ(Tall.m_Details.h, Short.m_Details.h);
	EXPECT_FLOAT_EQ(Tall.m_Search.h, 24);
	EXPECT_FLOAT_EQ(Tall.m_Footer.h, 80);
	EXPECT_FLOAT_EQ(Tall.m_Footer.y + Tall.m_Footer.h, 510);
	EXPECT_FLOAT_EQ(Tall.m_Candidates.y, Short.m_Candidates.y);
	EXPECT_FLOAT_EQ(Tall.m_Candidates.h - Short.m_Candidates.h, 200);
	EXPECT_GT(Tall.m_Candidates.h, 3 * 36);
}

TEST(AudioPackEditorLayout, WrappedDetailsConsumeOnlyAvailableCandidateSpace)
{
	const CUIRect View{20, 30, 180, 330};
	const auto Plain = qm_audio_pack::ResolveWorkspaceLayout(View, 90, 24, 40, 70);
	const auto Wrapped = qm_audio_pack::ResolveWorkspaceLayout(View, 150, 24, 40, 70);
	EXPECT_FLOAT_EQ(Plain.m_Footer.y, Wrapped.m_Footer.y);
	EXPECT_FLOAT_EQ(Wrapped.m_Candidates.y - Plain.m_Candidates.y, 60);
	EXPECT_FLOAT_EQ(Plain.m_Candidates.h - Wrapped.m_Candidates.h, 60);
	const auto Overflow = qm_audio_pack::ResolveWorkspaceLayout(View, 540, 24, 40, 70);
	EXPECT_NEAR(Overflow.m_Candidates.h, 40, 0.001f);
	EXPECT_GT(Overflow.m_Details.h, 0);
	EXPECT_LT(Overflow.m_Details.h, 540);
	EXPECT_GT(Overflow.m_Footer.h, 0);
}

TEST(AudioPackEditorLayout, ShortOrEmptyWorkspaceClipsWithoutOverlappingHitAreas)
{
	for(float Height : {0.0f, 7.0f, 39.0f, 40.0f, 60.0f, 114.0f})
	{
		SCOPED_TRACE(Height);
		const CUIRect View{7, 13, 81, Height};
		const auto Work = qm_audio_pack::ResolveWorkspaceLayout(View, 280, 30, 40, 160);
		float Bottom = View.y;
		for(const CUIRect &Part : {Work.m_Details, Work.m_Search, Work.m_Candidates, Work.m_Footer})
		{
			EXPECT_FLOAT_EQ(Part.x, View.x);
			EXPECT_FLOAT_EQ(Part.w, View.w);
			EXPECT_NEAR(Part.y, Bottom, 0.001f);
			EXPECT_GE(Part.h, 0);
			Bottom = Part.y + Part.h;
		}
		EXPECT_NEAR(Bottom, View.y + View.h, 0.001f);
		if(Height > 0)
		{
			EXPECT_GT(Work.m_Candidates.h, 0);
			EXPECT_GT(Work.m_Details.h, 0);
			EXPECT_GT(Work.m_Footer.h, 0);
		}
	}
}

TEST(AudioPackEditorLayout, ScrollingShortEditorCanExposeManualInputAndReplaceButton)
{
	constexpr float Line = 20, Gap = 4;
	for(float Width : {180.0f, 720.0f})
	{
		const float ViewHeight = 32;
		const float ContentHeight = qm_audio_pack::MinimumEditorHeight(Width, 1, Line, Gap, true);
		ASSERT_GT(ContentHeight, ViewHeight);
		CUIRect Content{0, 0, Width, ContentHeight};
		Content.Margin(ui_token::panel::PADDING, &Content);
		Content.HSplitTop(ui_token::page_title::HEIGHT + Line + ui_token::panel::GAP, nullptr, &Content);
		auto Workspace = qm_audio_pack::ResolveEditorLayout(Content, 1).m_Workspace;
		Workspace.Margin(ui_token::settings::CARD_PADDING, &Workspace);
		Workspace.HSplitBottom(Line, &Workspace, nullptr);
		constexpr float FooterHeight = 120;
		const auto Work = qm_audio_pack::ResolveWorkspaceLayout(Workspace, 500, Line + Gap, 2 * Line, FooterHeight);
		EXPECT_GE(Work.m_Candidates.h + 0.001f, 2 * Line);
		EXPECT_GE(Work.m_Details.h + 0.001f, Line);
		ASSERT_GE(Work.m_Footer.h + 0.001f, Line);
		// 外层滚到操作区，内层分别滚到手动输入和末行按钮。两种偏移都必须落在实际滚动范围内。
		for(float ControlTop : {Gap, FooterHeight - Line})
		{
			const float InnerScroll = std::clamp(ControlTop, 0.0f, FooterHeight - Work.m_Footer.h);
			const float Top = Work.m_Footer.y + ControlTop - InnerScroll;
			const float OuterScroll = std::clamp(Top, 0.0f, ContentHeight - ViewHeight);
			EXPECT_GE(Top, Work.m_Footer.y);
			EXPECT_LE(Top + Line, Work.m_Footer.y + Work.m_Footer.h + 0.001f);
			EXPECT_GE(Top - OuterScroll, 0);
			EXPECT_LE(Top + Line - OuterScroll, ViewHeight + 0.001f);
		}
	}
}

TEST(AudioPackEditor, OnlyConsecutiveBodyClicksReplaceAndInlinePreviewPreservesSelection)
{
	const qm_audio_pack::SFilteredCandidates Filtered{{4, 2}, 0};
	for(int PreviewRow : {-1, 0, 1})
	{
		SCOPED_TRACE(PreviewRow);
		CUiDoubleClickState Clicks;
		int Selection = -1;
		char Body;
		const auto ClickBody = [&](float Time) {
			return qm_audio_pack::FinishCandidateList(Clicks, Selection, Filtered, 0, -1, Clicks.Click(&Body, Time, vec2(10, 10), 32)).m_Replace;
		};
		EXPECT_FALSE(ClickBody(0));
		ASSERT_EQ(Selection, 4);
		if(PreviewRow >= 0)
		{
			const auto Preview = qm_audio_pack::FinishCandidateList(Clicks, Selection, Filtered, 0, PreviewRow, false);
			EXPECT_EQ(Preview.m_PreviewIndex, Filtered.m_vIndices[PreviewRow]);
			EXPECT_FALSE(Preview.m_Replace);
			EXPECT_EQ(Selection, 4);
		}
		EXPECT_EQ(ClickBody(0.2f), PreviewRow < 0);
		EXPECT_EQ(ClickBody(0.3f), PreviewRow >= 0);
	}
}

TEST(AudioPackEditor, PopupSuspendsListNavigationAndRetainsTheActivePane)
{
	using namespace qm_audio_pack;
	CEditorListFocus Focus;
	EXPECT_TRUE(Focus.Active(EEditorList::CATALOG, false));
	EXPECT_FALSE(Focus.Active(EEditorList::CANDIDATES, false));
	Focus.Click(EEditorList::CANDIDATES, false);
	EXPECT_FALSE(Focus.Active(EEditorList::CATALOG, false));
	EXPECT_TRUE(Focus.Active(EEditorList::CANDIDATES, false));
	Focus.Click(EEditorList::CATALOG, true);
	EXPECT_FALSE(Focus.Active(EEditorList::CATALOG, true));
	EXPECT_FALSE(Focus.Active(EEditorList::CANDIDATES, true));
	EXPECT_TRUE(Focus.Active(EEditorList::CANDIDATES, false));
	Focus.Click(EEditorList::CATALOG, false);
	EXPECT_TRUE(Focus.Active(EEditorList::CATALOG, false));
	EXPECT_FALSE(Focus.Active(EEditorList::CANDIDATES, false));
}

TEST(AudioPackEditor, CurrentPathAndCandidatesPublishTogetherAfterRetry)
{
	qm_audio_pack::SEditorCandidates State;
	const auto Scan = [](auto &Out) {
		Out = {"audio/a.wv", "audio/b.wv"};
		return true;
	};
	ASSERT_TRUE(State.Update(true, "pack", "audio/a.wv", Scan));
	const auto Fail = [](auto &Out) {
		Out = {"audio/partial.wv"};
		return false;
	};
	ASSERT_FALSE(State.Update(true, "pack", "audio/b.wv", Fail));
	EXPECT_STREQ(State.m_aCurrentPath, "audio/a.wv");
	ASSERT_GE(State.m_SelectedCandidateIndex, 0);
	EXPECT_EQ(State.m_vCandidateEntries.at(State.m_SelectedCandidateIndex).m_Path, State.m_aCurrentPath);
	ASSERT_TRUE(State.Update(false, "pack", "audio/b.wv", Fail));
	EXPECT_STREQ(State.m_aCurrentPath, "audio/b.wv");
	EXPECT_EQ(State.m_vCandidateEntries.at(State.m_SelectedCandidateIndex).m_Path, State.m_aCurrentPath);
	ASSERT_TRUE(State.Update(true, "pack", "", Scan));
	EXPECT_STREQ(State.m_aCurrentPath, "");
}

TEST(AudioPackEditor, CandidateFilterPreservesVisiblePathAndFallsBackWithinMatches)
{
	const auto Entries = CMenus::BuildAudioPackCandidateEntries({"audio/base/a.wv", "audio/custom/a.wv", "audio/custom/b.wv"}, "base", "");
	const int B = CMenus::FindAudioPackCandidateEntryIndex(Entries, "audio/custom/b.wv");
	ASSERT_GE(B, 0);
	const auto Visible = qm_audio_pack::FilterCandidates(Entries, "CUSTOM/", B);
	ASSERT_EQ(Visible.m_vIndices.size(), 2u);
	ASSERT_GE(Visible.m_SelectedRow, 0);
	EXPECT_EQ(Visible.m_vIndices.at(Visible.m_SelectedRow), B);
	const auto Hidden = qm_audio_pack::FilterCandidates(Entries, "/base/", B);
	ASSERT_EQ(Hidden.m_vIndices.size(), 1u);
	ASSERT_EQ(Hidden.m_SelectedRow, 0);
	EXPECT_EQ(Entries.at(Hidden.m_vIndices.front()).m_Path, "audio/base/a.wv");
	const auto Empty = qm_audio_pack::FilterCandidates(Entries, "absent", B);
	EXPECT_TRUE(Empty.m_vIndices.empty());
	EXPECT_EQ(Empty.m_SelectedRow, -1);
}

TEST(AudioPackEditor, InlinePreviewAndRowActivationHaveDisjointHitAreas)
{
	CListboxItem Item{};
	Item.m_Rect = {10, 20, 140, 44};
	Item.m_ContentRect = {17, 20, 126, 44};
	const auto Play = Item.TrailingAction(22);
	const auto Select = Item.SelectionHitRect(22);
	EXPECT_FLOAT_EQ(Play.w, Play.h);
	EXPECT_FLOAT_EQ(Play.y + Play.h * 0.5f, 42);
	const vec2 Boundary(Play.x, Play.y + Play.h * 0.5f);
	EXPECT_TRUE(Play.Inside(Boundary));
	EXPECT_FALSE(Select.Inside(Boundary));
	EXPECT_TRUE(Select.Inside(vec2(Item.m_Rect.x + 1, Boundary.y)));
	EXPECT_FLOAT_EQ(Item.SelectionHitRect(0).w, Item.m_Rect.w);
	Item.m_ContentRect.w = 4;
	EXPECT_FLOAT_EQ(Item.TrailingAction(22).w, 4);
	EXPECT_LE(Item.SelectionHitRect(22).x + Item.SelectionHitRect(22).w, Item.TrailingAction(22).x);
}

TEST(AudioPackCatalog, EditorCategoriesKeepSpecificSoundsSearchable)
{
	using namespace qm_audio_pack;
	const auto vSlots = CMenus::BuildAudioPackSlots();
	const auto vGroups = BuildGroups(vSlots);
	for(const auto &[Category, Query, SetName] : {
		    std::tuple{ECategory::INTERFACE, "Music", "menu"},
		    {ECategory::INTERFACE, "chat_server", "chat_server"},
		    {ECategory::MAP_ENVIRONMENT, "ctf_drop", "ctf_drop"},
		    {ECategory::MAP_ENVIRONMENT, "pickup_health", "pickup_health"},
		    {ECategory::OTHER, "player_die", "player_die"}})
	{
		SCOPED_TRACE(Query);
		const auto vMatches = FilterGroups(vGroups, vSlots, Category, Query);
		ASSERT_EQ(vMatches.size(), 1u);
		EXPECT_STREQ(vGroups[vMatches.front()].m_pSetName, SetName);
	}
	EXPECT_TRUE(FilterGroups(vGroups, vSlots, ECategory::INTERFACE, "ctf_drop").empty());
}
