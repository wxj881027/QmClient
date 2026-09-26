// Skins 静态源码合同：skins_resource_contract_test.cpp.
// 源码合同测试：皮肤资源、菜单集成和队列策略。运行时行为保留在 skins_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

extern CDataContainer *g_pData;

static std::string FunctionBody(const std::string &Source, const std::string &Signature)
{
	const size_t FunctionStart = Source.find(Signature);
	EXPECT_NE(FunctionStart, std::string::npos) << Signature;
	const size_t BodyStart = Source.find("{", FunctionStart);
	EXPECT_NE(BodyStart, std::string::npos) << Signature;
	int Depth = 0;
	for(size_t Index = BodyStart; Index < Source.size(); ++Index)
	{
		if(Source[Index] == '{')
			++Depth;
		else if(Source[Index] == '}')
		{
			--Depth;
			if(Depth == 0)
				return Source.substr(BodyStart, Index - BodyStart);
		}
	}
	ADD_FAILURE() << Signature;
	return {};
}

TEST(SkinsContract, TeeBackgroundDrainSeparatesRequestedBacklogFromAdmittedQueue)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("BACKGROUND_REQUESTED"), std::string::npos);
	EXPECT_NE(Source.find("size_t m_NumBackgroundRequested = 0;"), std::string::npos);
	EXPECT_NE(Source.find("return !ExistsInSkinMap || (!TracksUsage(State, AlwaysLoaded) && State != EState::BACKGROUND_REQUESTED);"), std::string::npos);
}

TEST(SkinsContract, TeeBackgroundRequestsWaitForAdmissionBeforePending)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("if(m_State == EState::UNLOADED)\n\t\t\tSetState(EState::BACKGROUND_REQUESTED, ESettingsResourcePriority::BACKGROUND);"), std::string::npos);
	EXPECT_NE(Source.find("if(m_State == EState::UNLOADED || m_State == EState::BACKGROUND_REQUESTED)"), std::string::npos);
	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("if(Stats.m_NumPending == 0 && pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)"), std::string::npos);
	EXPECT_NE(StartLoading.find("if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)"), std::string::npos);
	EXPECT_NE(StartLoading.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING, Admission.m_PromotePriority);"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumBackgroundRequested--;"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumPending++;"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListVirtualizationKeepsTotalListLength)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettings", RenderTeePos + 1);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("s_ListBox.DoStart(TeeSkinListRowHeight, vSkinList.size(), TeeSkinListItemsPerRow, 2, OldSelected, &MainView);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("s_ListBox.SkipItems("), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("int RowsRendered = 0;"), std::string::npos);
	const size_t RowsRendered = RenderTeeBody.find("++RowsRendered;");
	EXPECT_NE(RowsRendered, std::string::npos);
	EXPECT_LT(RenderTeeBody.find("if(RowStart)"), RowsRendered);
	EXPECT_EQ(RenderTeeBody.find("const int RowsRendered = RowsIterated;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=list_frame page=settings:tee"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("rows_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListVirtualizationUsesFourColumnContract)
{
	const std::string JobsSource = ReadTestSourceFile("src/game/client/components/settings_resource_jobs.cpp");
	const std::string Source = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("constexpr int TeeSkinListItemsPerRow = 4;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("s_ListBox.DoStart(TeeSkinListRowHeight, vSkinList.size(), TeeSkinListItemsPerRow"), std::string::npos);
	const size_t VisibleRange = RenderTeeBody.find("SettingsSkinListVisibleRangeForScroll(");
	ASSERT_NE(VisibleRange, std::string::npos);
	const size_t VisibleRangeEnd = RenderTeeBody.find(");", VisibleRange);
	ASSERT_NE(VisibleRangeEnd, std::string::npos);
	const std::string VisibleRangeCall = RenderTeeBody.substr(VisibleRange, VisibleRangeEnd - VisibleRange);
	const size_t ScrollOffset = VisibleRangeCall.find("s_ListBox.ScrollOffsetY()");
	const size_t ViewHeight = VisibleRangeCall.find("s_ListBox.ViewHeight()");
	const size_t RowHeight = VisibleRangeCall.find("TeeSkinListRowHeight");
	const size_t ItemsPerRow = VisibleRangeCall.find("TeeSkinListItemsPerRow");
	const size_t ListSize = VisibleRangeCall.find("(int)vSkinList.size()");
	const size_t OverscanRows = VisibleRangeCall.find("1");
	ASSERT_NE(ScrollOffset, std::string::npos);
	ASSERT_NE(ViewHeight, std::string::npos);
	ASSERT_NE(RowHeight, std::string::npos);
	ASSERT_NE(ItemsPerRow, std::string::npos);
	ASSERT_NE(ListSize, std::string::npos);
	ASSERT_NE(OverscanRows, std::string::npos);
	EXPECT_LT(ScrollOffset, ViewHeight);
	EXPECT_LT(ViewHeight, RowHeight);
	EXPECT_LT(RowHeight, ItemsPerRow);
	EXPECT_LT(ItemsPerRow, ListSize);
	EXPECT_LT(ListSize, OverscanRows);
	EXPECT_NE(JobsSource.find("constexpr int TeeSkinListItemsPerRow = 4;"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListSortModeKeepsFavoritesPinnedThenUsesOfficialDate)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/skins.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string ConfigSource = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string MenusI18nSource = ReadTestSourceFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");
	const size_t ComparePos = Source.find("bool CSkins::CSkinListEntry::operator<");
	const size_t ScanJobPos = Source.find("int CSkins::CSkinDirectoryScanJob::ScanCallback");
	const size_t RenderTeePos = MenusSource.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	const size_t RenderTeeEnd = MenusSource.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(ComparePos, std::string::npos);
	ASSERT_NE(ScanJobPos, std::string::npos);
	ASSERT_NE(RenderTeePos, std::string::npos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string CompareBody = Source.substr(ComparePos, 1200);
	const std::string ScanJobBody = Source.substr(ScanJobPos, 900);
	const std::string RenderTeeBody = MenusSource.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinSortMode, qm_skin_sort_mode, 0, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinShowMetadata, qm_skin_show_metadata, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Header.find("time_t m_LastModified"), std::string::npos);
	EXPECT_NE(Header.find("time_t LastModified() const"), std::string::npos);
	EXPECT_NE(Header.find("int m_OfficialReleaseDate"), std::string::npos);
	EXPECT_NE(Header.find("int OfficialReleaseDate() const"), std::string::npos);
	EXPECT_NE(Header.find("char m_aOfficialCreator"), std::string::npos);
	EXPECT_NE(Header.find("const char *OfficialCreator() const"), std::string::npos);
	EXPECT_NE(Source.find("ListDirectoryInfo(IStorage::TYPE_ALL, pDirectory"), std::string::npos);
	EXPECT_NE(Source.find("ScanDirectory(\"skins\", CSkinContainer::EType::LOCAL);"), std::string::npos);
	EXPECT_NE(Source.find("ScanDirectory(\"downloadedskins\", CSkinContainer::EType::DOWNLOAD);"), std::string::npos);
	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_URL = \"https://ddnet.org/skins/skin/skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_CACHE_PATH = \"downloadedskins/official_skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("QueueOfficialSkinIndexRequest();"), std::string::npos);
	EXPECT_NE(Source.find("LoadOfficialSkinIndexCache();"), std::string::npos);
	EXPECT_NE(Source.find("json_string_get(json_object_get(pEntry, \"date\"))"), std::string::npos);
	EXPECT_NE(Source.find("json_string_get(json_object_get(pEntry, \"creator\"))"), std::string::npos);
	EXPECT_NE(ScanJobBody.find("pInfo->m_TimeModified"), std::string::npos);
	EXPECT_NE(CompareBody.find("g_Config.m_QmSkinSortMode"), std::string::npos);
	EXPECT_NE(CompareBody.find("OfficialReleaseDate()"), std::string::npos);
	EXPECT_NE(CompareBody.find("LastModified()"), std::string::npos);
	EXPECT_NE(CompareBody.find("OfficialReleaseDate > OtherOfficialReleaseDate"), std::string::npos);
	EXPECT_NE(CompareBody.find("LastModified() > Other.m_pSkinContainer->LastModified()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinSortMode"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Skin sort\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Name\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Time\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("SortModeControl = NextPrefixRow();"), std::string::npos);
	const size_t SortChangePos = RenderTeeBody.find("if(g_Config.m_QmSkinSortMode != SkinSortModeNew)");
	ASSERT_NE(SortChangePos, std::string::npos);
	const size_t SortChangeEnd = RenderTeeBody.find("Button = NextPrefixRow();", SortChangePos);
	ASSERT_NE(SortChangeEnd, std::string::npos);
	const std::string SortChangeBody = RenderTeeBody.substr(SortChangePos, SortChangeEnd - SortChangePos);
	EXPECT_NE(SortChangeBody.find("GameClient()->m_Skins.RebuildSkinListPlan();"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("GameClient()->m_Skins.Refresh"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("ClearSettingsTeeListPreviewCache"), std::string::npos);
	EXPECT_EQ(SortChangeBody.find("SkinList(m_Dummy).ForceRefresh"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("Localize(\"Show skin date and author\")"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("g_Config.m_QmSkinSortMode == 1 && g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("g_Config.m_QmSkinShowMetadata != 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("pSkinContainer->OfficialCreator()"), std::string::npos);
	const size_t TimeTranslationPos = MenusI18nSource.find("key = \"Time\"");
	ASSERT_NE(TimeTranslationPos, std::string::npos);
	const size_t TimeTranslationEnd = MenusI18nSource.find("[[message]]", TimeTranslationPos + 1);
	const std::string TimeTranslationBody = MenusI18nSource.substr(TimeTranslationPos, TimeTranslationEnd - TimeTranslationPos);
	EXPECT_NE(TimeTranslationBody.find("simplified_chinese = \"时间\""), std::string::npos);
	EXPECT_EQ(TimeTranslationBody.find("simplified_chinese = \"用时\""), std::string::npos);
	const size_t ToolbarCommentPos = RenderTeeBody.find("// Layout bottom controls and use remainder for skin selector");
	ASSERT_NE(ToolbarCommentPos, std::string::npos);
	const std::string BottomToolbarBody = RenderTeeBody.substr(ToolbarCommentPos);
	EXPECT_EQ(BottomToolbarBody.find("SkinSortModeControlWidth"), std::string::npos);
	EXPECT_NE(BottomToolbarBody.find("const float SkinControlLabelPadding = TeeMetrics.m_LineSpacing * 3.0f;"), std::string::npos);
	const size_t RefreshRightPos = BottomToolbarBody.find("ControlsArea.VSplitRight(SkinRefreshButtonWidth, &ControlsArea, &RefreshButton);");
	const size_t EditButtonPos = BottomToolbarBody.find("SplitSkinToolbarLeft(ControlsArea, EditTextureButtonWidth, &EditTextureButton);");
	ASSERT_NE(RefreshRightPos, std::string::npos);
	ASSERT_NE(EditButtonPos, std::string::npos);
	EXPECT_LT(RefreshRightPos, EditButtonPos);
	const size_t FavoritePos = CompareBody.find("m_Favorite");
	const size_t SortModePos = CompareBody.find("g_Config.m_QmSkinSortMode");
	const size_t ModifiedPos = CompareBody.find("LastModified()");
	ASSERT_NE(FavoritePos, std::string::npos);
	ASSERT_NE(SortModePos, std::string::npos);
	ASSERT_NE(ModifiedPos, std::string::npos);
	EXPECT_LT(FavoritePos, SortModePos);
	EXPECT_LT(SortModePos, ModifiedPos);
	EXPECT_NE(CompareBody.find("if(g_Config.m_QmSkinSortMode == 1)"), std::string::npos);
	EXPECT_NE(CompareBody.find("if(m_Favorite && !Other.m_Favorite)"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListStableIdleAvoidsFullBackgroundScan)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(Source.find("bool m_BackgroundRequestScanComplete = false;"), std::string::npos);
	EXPECT_NE(Source.find("uint64_t m_BackgroundRequestScanRevision = std::numeric_limits<uint64_t>::max();"), std::string::npos);
	EXPECT_NE(Source.find("uint64_t m_FullListSettledRevision = std::numeric_limits<uint64_t>::max();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("VisibleSourceSettled && BackgroundRequestBudget > 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("!gs_TeeSettingsPageState.m_BackgroundRequestScanComplete"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("SkinStatsBeforeBackgroundRequest.m_NumUnloaded > 0"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = true;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=tee_skin_background_scan"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("items_total=%d items_scanned=%d items_skipped_visible=%d requests_issued=%d complete=%d budget=%d dur_ms=%.3f block_reason=%s"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("event=tee_skin_list_prescan"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool NeedFullListSourceState = g_Config.m_QmSettingsPrewarm != 0;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_SelectedIndexRevision != SkinList.Revision()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool NeedFullListSettledScan = NeedFullListSourceState && gs_TeeSettingsPageState.m_FullListSettledRevision != SkinList.Revision();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("if(NeedFullListSettledScan)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("m_BackgroundRequestScanRevision != SkinList.Revision()"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("gs_TeeSettingsPageState.m_SelectedIndex = NewSelected;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("visual_ready_count=%d"), std::string::npos);
}

TEST(SkinsContract, TeeSkinListSeparatesVisualReadyFromSourceSettled)
{
	std::ifstream JobsFile(TestSourcePath("src/game/client/components/settings_resource_jobs.cpp"));
	ASSERT_TRUE(JobsFile.good());
	std::stringstream JobsBuffer;
	JobsBuffer << JobsFile.rdbuf();
	const std::string JobsSource = JobsBuffer.str();

	EXPECT_NE(JobsSource.find("bool SettingsSkinListEntryVisualReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady)"), std::string::npos);
	EXPECT_NE(JobsSource.find("bool SettingsSkinListEntrySourceSettled(bool SourceReady, bool TerminalFailure)"), std::string::npos);
	EXPECT_NE(JobsSource.find("return SourceReady || TerminalFailure || PreviewCacheReady;"), std::string::npos);
	EXPECT_NE(JobsSource.find("return SourceReady || TerminalFailure;"), std::string::npos);

	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();
	const size_t RenderTeePos = MenusSource.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = MenusSource.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = MenusSource.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("VisibleVisualReadyCount"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("VisibleSourceSettledCount"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool VisibleSourceSettled = VisibleSourceSettledCount == (int)vVisibleSkinIndices.size();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("m_SettingsHighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("FrameContext.m_HighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("m_SettingsHighPrioritySettled = VisibleSettled;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("FrameContext.m_HighPrioritySettled = VisibleSettled;"), std::string::npos);
}

TEST(SkinsContract, TeeStartLoadingFallbackSweepIsBoundedAndLogged)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/components/skins.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string HeaderSource = HeaderBuffer.str();
	EXPECT_NE(HeaderSource.find("m_FallbackSweepScanned"), std::string::npos);
	EXPECT_NE(HeaderSource.find("m_FallbackSweepStarted"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsSourceFallbackSweepCursor"), std::string::npos);

	std::ifstream SourceFile(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();
	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(Source.find("event=skin_start_loading_fallback_sweep"), std::string::npos);
	EXPECT_NE(Source.find("items_total=%d items_scanned=%d items_started=%d items_skipped=%d invoked=%d dur_ms=%.3f reason=%s"), std::string::npos);
	EXPECT_NE(StartLoading.find("LogSettingsSkinStartLoadingFallbackSweepEvent("), std::string::npos);
	EXPECT_NE(StartLoading.find("disabled_explicit_queues"), std::string::npos);
	EXPECT_EQ(StartLoading.find("std::advance("), std::string::npos);
	EXPECT_EQ(StartLoading.find("for(auto &[_, pSkinContainer] : m_Skins)\n\t{"), std::string::npos);
}

TEST(SkinsContract, SettingsAssetsListVirtualizationKeepsTotalListLength)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const size_t LocalListPos = Source.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalListPos, std::string::npos);
	const size_t WorkshopListPos = Source.find("if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory) && WorkshopHudView.h > 0.0f)", LocalListPos);
	ASSERT_NE(WorkshopListPos, std::string::npos);
	const std::string LocalListBody = Source.substr(LocalListPos, WorkshopListPos - LocalListPos);
	const size_t WorkshopListEnd = Source.find("if(ui_widget::InputField(AssetsSearchCtx", WorkshopListPos);
	ASSERT_NE(WorkshopListEnd, std::string::npos);
	const std::string WorkshopListBody = Source.substr(WorkshopListPos, WorkshopListEnd - WorkshopListPos);

	EXPECT_NE(LocalListBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(LocalListBody.find("OldSelected = SelectedCustomAssetIndex(s_CurCustomTab, SearchListSize);"), std::string::npos);
	EXPECT_NE(LocalListBody.find("s_ListBox.SkipItems("), std::string::npos);
	EXPECT_NE(LocalListBody.find("assets_local_list_frame"), std::string::npos);
	EXPECT_EQ(LocalListBody.find("for(size_t i = 0; i < SearchListSize; ++i)"), std::string::npos);

	EXPECT_NE(WorkshopListBody.find("SettingsSkinListVisibleRangeForScroll("), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("OldCombinedSelected = SelectedCombinedAssetIndex(s_CurCustomTab, vVisibleLocalAssetIndices);"), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("s_WorkshopAssetsListBox.SkipItems("), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("abs(WorkshopVisibleRange.m_FirstItem - PreviousFirstVisibleCombinedIndex)"), std::string::npos);
	EXPECT_NE(WorkshopListBody.find("assets_workshop_list_frame"), std::string::npos);
	EXPECT_EQ(WorkshopListBody.find("for(size_t ListIndex = 0; ListIndex < CombinedCount; ++ListIndex)"), std::string::npos);
}

TEST(SkinsContract, TeePriorityRequestsReclaimBackgroundRequestedBeforeAdmittedBackgroundWork)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ReclaimBackground = Source.find("bool CSkins::ReclaimBackgroundSkinForPriorityRequest");
	ASSERT_NE(ReclaimBackground, std::string::npos);
	const size_t ReclaimBackgroundEnd = Source.find("\n}", ReclaimBackground);
	ASSERT_NE(ReclaimBackgroundEnd, std::string::npos);
	const std::string ReclaimBody = Source.substr(ReclaimBackground, ReclaimBackgroundEnd - ReclaimBackground);

	const size_t BackgroundRequestedPos = ReclaimBody.find("if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)");
	const size_t PendingBranchPos = ReclaimBody.find("if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&");
	ASSERT_NE(BackgroundRequestedPos, std::string::npos);
	ASSERT_NE(PendingBranchPos, std::string::npos);
	EXPECT_LT(BackgroundRequestedPos, PendingBranchPos);
}

TEST(SkinsContract, PriorityRequestsCanReclaimBackgroundLoadingSlots)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t ReclaimBackground = Source.find("bool CSkins::ReclaimBackgroundSkinForPriorityRequest");
	ASSERT_NE(ReclaimBackground, std::string::npos);
	const size_t ReclaimBackgroundEnd = Source.find("\n}", ReclaimBackground);
	ASSERT_NE(ReclaimBackgroundEnd, std::string::npos);
	const std::string ReclaimBody = Source.substr(ReclaimBackground, ReclaimBackgroundEnd - ReclaimBackground);
	EXPECT_NE(ReclaimBody.find("CSkinContainer::EState::LOADING"), std::string::npos);
	EXPECT_NE(ReclaimBody.find("m_pLoadJob->Abort()"), std::string::npos);

	const size_t StartLoadJob = Source.find("auto StartLoadJob = [&]");
	ASSERT_NE(StartLoadJob, std::string::npos);
	const size_t StartLoadJobEnd = Source.find("\n\t};", StartLoadJob);
	ASSERT_NE(StartLoadJobEnd, std::string::npos);
	const std::string StartLoadBody = Source.substr(StartLoadJob, StartLoadJobEnd - StartLoadJob);
	EXPECT_NE(StartLoadBody.find("ReclaimBackgroundSkinForPriorityRequest"), std::string::npos);
	EXPECT_NE(StartLoadBody.find("Stats = LoadingStats();"), std::string::npos);
	EXPECT_EQ(StartLoadBody.find("Priority != ESettingsResourcePriority::BACKGROUND"), std::string::npos);
}

TEST(SkinsContract, PrioritizedLoadQueueKeepsOriginalRequestPriority)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("StartLoadJob(It->second.get(), It->second->m_LoadPriority)"), std::string::npos);
	EXPECT_EQ(StartLoading.find("StartLoadJob(It->second.get(), ESettingsResourcePriority::VISIBLE)"), std::string::npos);
	EXPECT_NE(StartLoading.find("Stats.m_NumPending + Stats.m_NumLoading"), std::string::npos);
	EXPECT_EQ(StartLoading.find("Stats.m_NumPending + Stats.m_NumLoaded + Stats.m_NumLoading"), std::string::npos);
}

TEST(SkinsContract, TeeSettingsScrollBudgetFeedsFinalizeAndUploadLimits)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t FinalizeBudget = Source.find("return pGameClient->m_Skins.SettingsFinalizeBudgetForFrame();");
	EXPECT_NE(FinalizeBudget, std::string::npos);

	const size_t UploadBudget = Source.find("return pGameClient->m_Skins.SettingsGpuUploadFrameBudgetForFrame();");
	EXPECT_NE(UploadBudget, std::string::npos);

	const size_t ImmediateScrollContext = Source.find("return SettingsBuildFrameContext(PersistentContext.m_ScrollActive, ImmediateScrollInput, PersistentContext.m_PostScrollRecoveryFrames);");
	EXPECT_NE(ImmediateScrollContext, std::string::npos);

	const size_t ScrollRegionMouseDown = Source.find("(pGameClient->Input()->KeyPress(KEY_MOUSE_1) && pUi->HotScrollRegion() != nullptr)");
	EXPECT_NE(ScrollRegionMouseDown, std::string::npos);
}

TEST(SkinsContract, GpuUploadLimiterResetsBeforeSkinUpdateConsumesBudget)
{
	std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t OnUpdatePos = Source.find("void CGameClient::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnRenderPos = Source.find("void CGameClient::OnRender()");
	ASSERT_NE(OnRenderPos, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, OnRenderPos - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("m_Skins.PrepareSettingsThroughputForFrame();"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_GpuUploadLimiter.OnFrameStart(FrameGpuUploadLimit);"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("const int FrameGpuUploadLimit = m_Menus.SettingsGpuUploadLimitForFrame(TeeSettingsActive, AssetsSettingsActive, m_Skins.SettingsGpuUploadLimiterUnitsForFrame());"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_Menus.ResetSettingsFrameBudgetForFrame(TeeSettingsActive, AssetsSettingsActive, FrameSkinUploadBudget);"), std::string::npos);

	const size_t OnRenderEnd = Source.find("const ColorRGBA ClearColor", OnRenderPos);
	ASSERT_NE(OnRenderEnd, std::string::npos);
	const std::string OnRenderPreamble = Source.substr(OnRenderPos, OnRenderEnd - OnRenderPos);
	EXPECT_EQ(OnRenderPreamble.find("m_GpuUploadLimiter.OnFrameStart();"), std::string::npos);
}

TEST(SkinsContract, SettingsWarmupBypassesPeriodicSkinUpdateThrottle)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t WarmupPos = Source.find("void CSkins::UpdateForSettingsWarmup()");
	ASSERT_NE(WarmupPos, std::string::npos);
	const size_t PreparePos = Source.find("void CSkins::PrepareSettingsThroughputForFrame()", WarmupPos);
	ASSERT_NE(PreparePos, std::string::npos);
	const std::string WarmupBody = Source.substr(WarmupPos, PreparePos - WarmupPos);

	EXPECT_NE(WarmupBody.find("m_ContainerUpdateTime.reset();"), std::string::npos);
	EXPECT_NE(WarmupBody.find("OnUpdate();"), std::string::npos);
}
