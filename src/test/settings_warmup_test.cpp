// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>

TEST(SettingsWarmup, StageReadinessUsesInclusiveThreshold)
{
	EXPECT_FALSE(IsSettingsWarmupStageReady(0, 1));
	EXPECT_TRUE(IsSettingsWarmupStageReady(1, 1));
	EXPECT_TRUE(IsSettingsWarmupStageReady(-1, 5));
}

TEST(SettingsWarmup, StageAdvanceStopsAfterLastStage)
{
	EXPECT_EQ(AdvanceSettingsWarmupStage(0, 3), 1);
	EXPECT_EQ(AdvanceSettingsWarmupStage(2, 3), 3);
	EXPECT_EQ(AdvanceSettingsWarmupStage(3, 3), -1);
	EXPECT_EQ(AdvanceSettingsWarmupStage(-1, 3), -1);
	EXPECT_EQ(AdvanceSettingsWarmupStage(0, -1), -1);
}

TEST(SettingsWarmup, PrioritizesLastSessionPage)
{
	CSettingsWarmupScheduler Scheduler;
	int WarmedControls = 0;
	int WarmedTClient = 0;

	Scheduler.RegisterSection({EClassicSettingsPage::CONTROLS, "Controls:Mouse", 0, [&]() {
					   ++WarmedControls;
					   return 1.0;
				   }});
	Scheduler.RegisterSection({EClassicSettingsPage::TCLIENT, "TClient:Pet", 0, [&]() {
					   ++WarmedTClient;
					   return 1.0;
				   }});
	Scheduler.SetLastSessionPage(EClassicSettingsPage::TCLIENT);

	EXPECT_FALSE(Scheduler.WarmupFrame(1.5));
	EXPECT_EQ(WarmedControls, 0);
	EXPECT_EQ(WarmedTClient, 1);
}

TEST(SettingsWarmup, StopsAtFrameBudget)
{
	CSettingsWarmupScheduler Scheduler;
	int WarmedCount = 0;

	Scheduler.RegisterSection({EClassicSettingsPage::CONTROLS, "Controls:Mouse", 0, [&]() {
					   ++WarmedCount;
					   return 2.0;
				   }});
	Scheduler.RegisterSection({EClassicSettingsPage::CONTROLS, "Controls:Movement", 1, [&]() {
					   ++WarmedCount;
					   return 2.0;
				   }});

	EXPECT_FALSE(Scheduler.WarmupFrame(2.5));
	EXPECT_EQ(WarmedCount, 1);
	EXPECT_TRUE(Scheduler.WarmupFrame(2.5));
	EXPECT_EQ(WarmedCount, 2);
}

TEST(SettingsWarmup, DisabledSchedulerDoesNotRunSections)
{
	CSettingsWarmupScheduler Scheduler;
	int WarmedCount = 0;

	Scheduler.RegisterSection({EClassicSettingsPage::CONTROLS, "Controls:Mouse", 0, [&]() {
					   ++WarmedCount;
					   return 1.0;
				   }});
	Scheduler.SetEnabled(false);

	EXPECT_TRUE(Scheduler.WarmupFrame(10.0));
	EXPECT_EQ(WarmedCount, 0);
}

TEST(SettingsWarmup, ManySettingsSectionsRespectBudget)
{
	CSettingsWarmupScheduler Scheduler;
	int WarmedCount = 0;

	for(int i = 0; i < 20; ++i)
	{
		Scheduler.RegisterSection({EClassicSettingsPage::CONTROLS, "Controls:Bulk", i, [&]() {
						   ++WarmedCount;
						   return 1.0;
					   }});
	}
	Scheduler.SetLastSessionPage(EClassicSettingsPage::CONTROLS);

	EXPECT_FALSE(Scheduler.WarmupFrame(3.0));
	EXPECT_EQ(WarmedCount, 3);
}

TEST(SettingsWarmup, SectionCacheMetadataRequiresMatchingRuntimeKey)
{
	SSettingsSectionCacheRuntimeKey RuntimeKey;
	RuntimeKey.m_ViewportWidth = 900;
	RuntimeKey.m_ViewportHeight = 620;
	RuntimeKey.m_UiScale = 100;
	RuntimeKey.m_ConfigHash = 1234;
	RuntimeKey.m_LanguageHash = 5678;
	RuntimeKey.m_FontHash = 9012;
	RuntimeKey.m_BackendHash = 3456;
	RuntimeKey.m_WindowHash = 7890;

	SSettingsSectionCacheMetadata Metadata;
	Metadata.m_LastPage = EClassicSettingsPage::TCLIENT;
	Metadata.m_LastTab = 0;
	Metadata.m_LastScrollY = 140.0f;
	Metadata.m_SectionNameHash = 42;
	Metadata.m_SectionHeight = 180.0f;
	Metadata.m_RuntimeKey = RuntimeKey;

	EXPECT_TRUE(Metadata.Matches(RuntimeKey));

	RuntimeKey.m_ViewportHeight += 1;
	EXPECT_FALSE(Metadata.Matches(RuntimeKey));
}

TEST(SettingsWarmup, RuntimeCacheNumericKeysRejectNonFiniteValues)
{
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(-std::numeric_limits<float>::infinity()), 1);
	EXPECT_EQ(SettingsRuntimeCachePositiveRoundedKey(-std::numeric_limits<float>::infinity()), 1);
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(std::numeric_limits<float>::infinity(), 7), 7);
}

TEST(SettingsWarmup, RuntimeCacheNumericKeysClampBeforeIntConversion)
{
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(0.0f), 1);
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(900.9f), 900);
	EXPECT_EQ(SettingsRuntimeCachePositiveRoundedKey(125.4f), 125);
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(12.6f), 13);
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(std::numeric_limits<float>::max()), std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(-std::numeric_limits<float>::max()), std::numeric_limits<int>::min());
}

TEST(SettingsResourceJobs, SkinListVisibleRangeKeepsTotalLengthStable)
{
	const SSettingsSkinListVisibleRange Range = SettingsSkinListVisibleRangeForScroll(125.0f, 300.0f, 50.0f, 4, 101, 1);

	EXPECT_EQ(Range.m_TotalItems, 101);
	EXPECT_EQ(Range.m_TotalRows, 26);
	EXPECT_EQ(Range.m_FirstVisibleRow, 1);
	EXPECT_EQ(Range.m_LastVisibleRow, 9);
	EXPECT_EQ(Range.m_FirstItem, 4);
	EXPECT_EQ(Range.m_EndItem, 40);
	EXPECT_EQ(Range.m_VisibleRows, 9);
	EXPECT_EQ(Range.m_RenderedItems, 36);
	EXPECT_EQ(Range.m_SkippedItems, 65);
}

TEST(SettingsResourceJobs, SkinListVisibleRangeHandlesShortAndEmptyLists)
{
	const SSettingsSkinListVisibleRange Empty = SettingsSkinListVisibleRangeForScroll(0.0f, 300.0f, 50.0f, 4, 0, 1);
	EXPECT_EQ(Empty.m_TotalItems, 0);
	EXPECT_EQ(Empty.m_TotalRows, 0);
	EXPECT_EQ(Empty.m_FirstItem, 0);
	EXPECT_EQ(Empty.m_EndItem, 0);
	EXPECT_EQ(Empty.m_SkippedItems, 0);

	const SSettingsSkinListVisibleRange Short = SettingsSkinListVisibleRangeForScroll(500.0f, 300.0f, 50.0f, 4, 7, 1);
	EXPECT_EQ(Short.m_TotalItems, 7);
	EXPECT_EQ(Short.m_TotalRows, 2);
	EXPECT_EQ(Short.m_FirstItem, 0);
	EXPECT_EQ(Short.m_EndItem, 7);
	EXPECT_EQ(Short.m_SkippedItems, 0);
}

TEST(SettingsWarmup, TeePageWarmupStartsSkinSourcePrewarm)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PrewarmPos = Source.find("bool CMenus::PrewarmSettingsPageResources(int Page, int Tab, const CUIRect &ContentView)");
	ASSERT_NE(PrewarmPos, std::string::npos);
	const size_t PrewarmEnd = Source.find("bool CMenus::OnCursorMove", PrewarmPos);
	ASSERT_NE(PrewarmEnd, std::string::npos);
	const std::string PrewarmBody = Source.substr(PrewarmPos, PrewarmEnd - PrewarmPos);

	const size_t TeeBranchPos = PrewarmBody.find("else if(Page == SETTINGS_TEE)");
	ASSERT_NE(TeeBranchPos, std::string::npos);
	const size_t AssetsBranchPos = PrewarmBody.find("else if(Page == SETTINGS_ASSETS)", TeeBranchPos);
	ASSERT_NE(AssetsBranchPos, std::string::npos);
	const std::string TeeBranch = PrewarmBody.substr(TeeBranchPos, AssetsBranchPos - TeeBranchPos);

	EXPECT_NE(TeeBranch.find("SettingsTeeSkinListFirstPageWarmupEntries(ContentView.h)"), std::string::npos);
	EXPECT_EQ(TeeBranch.find("SettingsSkinListFirstPageWarmupEntries("), std::string::npos);
	EXPECT_NE(TeeBranch.find("PrewarmPlayerPreviewReady"), std::string::npos);
}

TEST(SettingsWarmup, SettingsFrameBudgetResetsBeforeUpdatePhaseConsumers)
{
	std::ifstream GameClientFile(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(GameClientFile.good());
	std::stringstream GameClientBuffer;
	GameClientBuffer << GameClientFile.rdbuf();
	const std::string GameClientSource = GameClientBuffer.str();

	const size_t OnUpdatePos = GameClientSource.find("void CGameClient::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnRenderPos = GameClientSource.find("void CGameClient::OnRender()");
	ASSERT_NE(OnRenderPos, std::string::npos);
	const std::string OnUpdateBody = GameClientSource.substr(OnUpdatePos, OnRenderPos - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("const bool TeeSettingsActive = m_Menus.IsSettingsPageActive() && g_Config.m_UiSettingsPage == CMenus::SETTINGS_TEE;"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("const bool AssetsSettingsActive = m_Menus.IsSettingsPageActive() && g_Config.m_UiSettingsPage == CMenus::SETTINGS_ASSETS;"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_Skins.PrepareSettingsThroughputForFrame();"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("const int FrameGpuUploadLimit = m_Menus.SettingsGpuUploadLimitForFrame(TeeSettingsActive, AssetsSettingsActive, m_Skins.SettingsGpuUploadLimiterUnitsForFrame());"), std::string::npos);
	EXPECT_NE(OnUpdateBody.find("m_Menus.ResetSettingsFrameBudgetForFrame(TeeSettingsActive, AssetsSettingsActive, FrameSkinUploadBudget);"), std::string::npos);

	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();
	const size_t MenusOnRenderPos = MenusSource.find("void CMenus::OnRender()");
	ASSERT_NE(MenusOnRenderPos, std::string::npos);
	const size_t MenusOnRenderEnd = MenusSource.find("if(Client()->State() != IClient::STATE_ONLINE", MenusOnRenderPos);
	ASSERT_NE(MenusOnRenderEnd, std::string::npos);
	const std::string MenusOnRenderPreamble = MenusSource.substr(MenusOnRenderPos, MenusOnRenderEnd - MenusOnRenderPos);
	EXPECT_EQ(MenusOnRenderPreamble.find("m_SettingsFrameBudget = {};"), std::string::npos);

	std::ifstream MenusHeaderFile(TestSourcePath("src/game/client/components/menus.h"));
	ASSERT_TRUE(MenusHeaderFile.good());
	std::stringstream MenusHeaderBuffer;
	MenusHeaderBuffer << MenusHeaderFile.rdbuf();
	const std::string MenusHeaderSource = MenusHeaderBuffer.str();
	EXPECT_NE(MenusHeaderSource.find("int SettingsGpuUploadLimitForFrame(bool TeeSettingsActive, bool AssetsSettingsActive, int TeeSkinGpuUploadLimiterUnits) const"), std::string::npos);
	EXPECT_NE(MenusHeaderSource.find("if(AssetsSettingsActive)"), std::string::npos);
	EXPECT_NE(MenusHeaderSource.find("return 8;"), std::string::npos);
	EXPECT_NE(MenusHeaderSource.find("m_SettingsFrameBudget = SSettingsWarmupFrameBudget{};"), std::string::npos);
	EXPECT_NE(MenusHeaderSource.find("SettingsApplyActiveTeeSkinFrameBudget(m_SettingsFrameBudget, TeeSettingsActive);"), std::string::npos);
	EXPECT_NE(MenusHeaderSource.find("m_SettingsFrameBudget.m_MaxGpuUploads = 8;"), std::string::npos);
}

TEST(SettingsWarmup, LoadingPrewarmDoesNotPumpResourceWork)
{
	std::ifstream GameClientFile(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(GameClientFile.good());
	std::stringstream GameClientBuffer;
	GameClientBuffer << GameClientFile.rdbuf();
	const std::string GameClientSource = GameClientBuffer.str();

	const size_t PrewarmPos = GameClientSource.find("void CGameClient::PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage)");
	ASSERT_NE(PrewarmPos, std::string::npos);
	const size_t OnUpdatePos = GameClientSource.find("void CGameClient::OnUpdate()", PrewarmPos);
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const std::string PrewarmBody = GameClientSource.substr(PrewarmPos, OnUpdatePos - PrewarmPos);

	EXPECT_NE(PrewarmBody.find("m_Menus.PrewarmSettingsPages();"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("m_Menus.RenderLoading(pLoadingCaption, pLoadingMessage, 0);"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("m_Menus.PrewarmSettingsTextPoolForLoading(TEXT_PREWARM_BUDGET_PER_STEP);"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("State.m_LastBuiltTextContainers = m_Menus.SettingsTextContainerCount();"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("State.m_LastMissingTextPlanItems = m_Menus.SettingsTextPrebuildRemaining();"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("State.m_LastMissingTextPlanCollectionUnits = m_Menus.SettingsTextPlanCollectionRemaining();"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("SettingsLoadingPrewarmAdvance(State, m_Menus.SettingsTextContainerCount(), m_Menus.SettingsTextPrebuildRemaining(), m_Menus.SettingsTextPlanCollectionRemaining());"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("SettingsTextPoolEntryCount()"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("(void)pLoadingCaption;"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("(void)pLoadingMessage;"), std::string::npos);
	const size_t DisabledGuard = PrewarmBody.find("if(g_Config.m_QmSettingsPrewarm == 0)");
	const size_t DisabledReturn = PrewarmBody.find("return;", DisabledGuard);
	const size_t PagePrewarm = PrewarmBody.find("m_Menus.PrewarmSettingsPages();");
	ASSERT_NE(DisabledGuard, std::string::npos);
	ASSERT_NE(DisabledReturn, std::string::npos);
	ASSERT_NE(PagePrewarm, std::string::npos);
	EXPECT_LT(DisabledGuard, DisabledReturn);
	EXPECT_LT(DisabledReturn, PagePrewarm);
	EXPECT_NE(GameClientSource.find("PrewarmSettingsRuntimeCachesDuringLoading(pLoadingDDNetCaption, pLoadingMessageAssets);"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("maximum(CMenus::SettingsRuntimeCacheWarmupSteps() * 4, 1)"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("m_Skins.OnUpdate();"), std::string::npos);
}

TEST(SettingsResourceJobs, LoadingPrewarmAdvanceRequiresNoProgressToFinish)
{
	SSettingsLoadingPrewarmState State;
	State.m_LastBuiltTextContainers = 10;
	State.m_LastMissingTextPlanItems = 2;
	State.m_LastMissingTextPlanCollectionUnits = 1;

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 1);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastBuiltTextContainers, 18);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 1);
	EXPECT_EQ(State.m_LastMissingTextPlanCollectionUnits, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 2);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 1);
	EXPECT_EQ(State.m_LastBuiltTextContainers, 18);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 1);
	EXPECT_EQ(State.m_LastMissingTextPlanCollectionUnits, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 1);
	EXPECT_EQ(State.m_CompletedSteps, 3);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastBuiltTextContainers, 18);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanCollectionUnits, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 0);
	EXPECT_EQ(State.m_CompletedSteps, 4);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastBuiltTextContainers, 18);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanCollectionUnits, 0);
	EXPECT_TRUE(State.m_WarmupReady);
}

TEST(SettingsResourceJobs, LoadingPrewarmProgressDetectionIsStrictlyIncreasing)
{
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(3, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(5, 4));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 2, 2));
}

TEST(SettingsWarmup, TClientReadOnlyPathUsesIsolatedSectionLoaders)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();

	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);

	EXPECT_NE(RenderBody.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderBody.find("CSectionLoader &VisualFontLoader = ReadOnly ? s_VisualFontReadOnlyLoader : s_VisualFontLoader;"), std::string::npos);
	EXPECT_NE(RenderBody.find("CSectionLoader &RightSectionLoader = ReadOnly ? s_RightSectionReadOnlyLoader : s_RightSectionLoader;"), std::string::npos);
	const size_t LeftPrewarmPos = RenderBody.find("if(ReadOnly)\n\t\t{", RenderBody.find("VisualFontLoader.Register"));
	ASSERT_NE(LeftPrewarmPos, std::string::npos);
	const size_t RightColumnPos = RenderBody.find("// ***** RightView *****", LeftPrewarmPos);
	ASSERT_NE(RightColumnPos, std::string::npos);
	const std::string LeftPrewarmBody = RenderBody.substr(LeftPrewarmPos, RightColumnPos - LeftPrewarmPos);
	EXPECT_NE(LeftPrewarmBody.find("VisualFontLoader.Process();"), std::string::npos);

	const size_t RightPrewarmPos = RenderBody.find("if(ReadOnly)\n\t\t{", RenderBody.find("RightSectionLoader.Register"));
	ASSERT_NE(RightPrewarmPos, std::string::npos);
	const size_t RightElsePos = RenderBody.find("else\n\t\t{", RightPrewarmPos);
	ASSERT_NE(RightElsePos, std::string::npos);
	const std::string RightPrewarmBody = RenderBody.substr(RightPrewarmPos, RightElsePos - RightPrewarmPos);
	EXPECT_NE(RightPrewarmBody.find("RightSectionLoader.Process();"), std::string::npos);
	EXPECT_EQ(RightPrewarmBody.find("return;"), std::string::npos);
	const size_t VisibleDeckGuard = RenderBody.find("if(!ReadOnly)", RightPrewarmPos);
	const size_t VisibleDeckRender = RenderBody.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\"", VisibleDeckGuard);
	ASSERT_NE(VisibleDeckGuard, std::string::npos);
	ASSERT_NE(VisibleDeckRender, std::string::npos);
	EXPECT_LT(VisibleDeckGuard, VisibleDeckRender);
}

TEST(SettingsWarmup, TClientSectionLoadersEnableDeferredFarMeasurement)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();

	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);

	const size_t LeftProgressivePos = RenderBody.find("VisualFontLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);");
	ASSERT_NE(LeftProgressivePos, std::string::npos);
	const size_t LeftMaxSectionsPos = RenderBody.find("VisualFontLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?", LeftProgressivePos);
	ASSERT_NE(LeftMaxSectionsPos, std::string::npos);
	const size_t LeftDeferredPos = RenderBody.find("VisualFontLoader.SetDeferredFarMeasurementEnabled(true);", LeftMaxSectionsPos);
	ASSERT_NE(LeftDeferredPos, std::string::npos);
	const size_t LeftViewportPos = RenderBody.find("LeftLoaderViewport.y -= ScrollOffset.y;", LeftDeferredPos);
	ASSERT_NE(LeftViewportPos, std::string::npos);
	const size_t LeftBeginPos = RenderBody.find("VisualFontLoader.Begin(LeftView, LeftLoaderViewport, 5.0f);", LeftViewportPos);
	ASSERT_NE(LeftBeginPos, std::string::npos);

	const size_t RightProgressivePos = RenderBody.find("RightSectionLoader.SetProgressiveEnabled(TClientVisibleTargetFrame);");
	ASSERT_NE(RightProgressivePos, std::string::npos);
	const size_t RightMaxSectionsPos = RenderBody.find("RightSectionLoader.SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?", RightProgressivePos);
	ASSERT_NE(RightMaxSectionsPos, std::string::npos);
	const size_t RightDeferredPos = RenderBody.find("RightSectionLoader.SetDeferredFarMeasurementEnabled(true);", RightMaxSectionsPos);
	ASSERT_NE(RightDeferredPos, std::string::npos);
	const size_t RightViewportPos = RenderBody.find("RightLoaderViewport.y -= ScrollOffset.y;", RightDeferredPos);
	ASSERT_NE(RightViewportPos, std::string::npos);
	const size_t RightBeginPos = RenderBody.find("RightSectionLoader.Begin(RightView, RightLoaderViewport, 5.0f);", RightViewportPos);
	ASSERT_NE(RightBeginPos, std::string::npos);
	EXPECT_EQ(RenderBody.find(".m_ScrollY"), std::string::npos);
}

TEST(SettingsWarmup, TClientVisibleLoadersDoNotHashAllConfigsEveryFrame)
{
	std::ifstream TClientFile(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();
	const size_t RenderPos = TClientSource.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const size_t RenderEnd = TClientSource.find("void CMenus::LoadSettingsRuntimeCacheMetadata()", RenderPos);
	ASSERT_NE(RenderPos, std::string::npos);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = TClientSource.substr(RenderPos, RenderEnd - RenderPos);
	EXPECT_NE(RenderBody.find("MakeSettingsSectionRuntimeKey(LeftView, Graphics(), false)"), std::string::npos);
	EXPECT_EQ(RenderBody.find("HashTClientSettingsConfig("), std::string::npos);
	EXPECT_EQ(RenderBody.find("MakeSettingsSectionRuntimeKey(RightView, Graphics())"), std::string::npos);
}

TEST(SettingsWarmup, SixupTeeUsesUnifiedOuterAndNestedGridScrollProfiles)
{
	std::ifstream Tee7File(TestSourcePath("src/game/client/components/menus_settings7.cpp"));
	ASSERT_TRUE(Tee7File.good());
	std::stringstream Tee7Buffer;
	Tee7Buffer << Tee7File.rdbuf();
	const std::string Tee7Source = Tee7Buffer.str();
	EXPECT_NE(Tee7Source.find("SettingsPageLayout(MainView, UiScale)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("EQmScrollProfile::SETTINGS_OUTER"), std::string::npos);
	EXPECT_NE(Tee7Source.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("RenderSettingsTee7Content(Content, Metrics)"), std::string::npos);
	EXPECT_NE(Tee7Source.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Tee7Source.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Tee7Source.find("RenderOnly ? nullptr : &s_Tee7SettingsScrollRegion"), std::string::npos);
}

TEST(SettingsRuntimeCache, BudgetStopsEveryMainThreadCost)
{
	SSettingsWarmupFrameBudget Budget;
	Budget.m_MaxTextContainers = 1;
	Budget.m_MaxGpuUploads = 1;
	Budget.m_MaxJobResultMerges = 1;

	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::TEXT_BUDGET);

	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);

	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::JOB_RESULT_MERGE));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::JOB_RESULT_MERGE));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
}

TEST(SettingsRuntimeCache, DefaultGpuBudgetAllowsOneSkinUploadBatch)
{
	SSettingsWarmupFrameBudget Budget;
	for(int Upload = 0; Upload < 14; ++Upload)
		EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsRuntimeCache, BudgetStopReasonsMapToProductionMissReasons)
{
	EXPECT_STREQ(SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason::TEXT_BUDGET), "text_budget");
	EXPECT_STREQ(SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason::NONE), "none");
}

TEST(SettingsRuntimeCache, TClientPerfStageNamesAreStable)
{
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::TAB_SHELL), "tclient_tab_shell");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::SECTION_LAYOUT), "tclient_section_layout");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::TEXT_CACHE), "tclient_text_cache");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::RESOURCE_PRETRIGGER), "tclient_resource_pretrigger");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::STATIC_LAYER), "tclient_static_layer");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::INTERACTIVE_LAYER), "tclient_interactive_layer");
}

TEST(SettingsRuntimeCache, PerfReasonNamesAreStable)
{
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::NONE), "none");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::DEPENDENCY_NOT_READY), "dependency_not_ready");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING), "resource_plan_pending");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::JOB_RESULT_PENDING), "job_result_pending");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET), "gpu_upload_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::SHARED_HEAVY_BUDGET), "shared_heavy_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::UPLOAD_BYTES_BUDGET), "upload_bytes_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::OVERSIZED_UPLOAD_DEFERRED), "oversized_upload_deferred");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::TEXT_BUDGET), "text_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::ACTIVE_ITEM), "active_item");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::INVALID_RUNTIME_KEY), "invalid_runtime_key");
}

TEST(SettingsRuntimeCache, InvalidationReasonNamesAreStable)
{
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::LANGUAGE_CHANGED), "language_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::FONT_CHANGED), "font_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::BACKEND_CHANGED), "backend_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED), "window_or_scale_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::DPI_CHANGED), "dpi_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::UI_SCALE_CHANGED), "ui_scale_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::CONFIG_HASH_CHANGED), "config_hash_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::SECTION_SIZE_CHANGED), "section_size_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED), "resource_directory_changed");
}

TEST(SettingsRuntimeCache, ClearsTextPoolOnlyForContentChangingReasons)
{
	// 只有真正改变 label 文字内容、字形或渲染后端的 reason 才全池失效。
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::LANGUAGE_CHANGED));
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::FONT_CHANGED));
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::BACKEND_CHANGED));

	// 布局尺寸 / 控件配置状态变化不影响文字内容或字形；DoMenuLabelStreamed 的
	// SizeChanged / TextChanged / ColorChanged 单 entry 检测已兜底，不需要全池失效。
	// 这是 ingame ESC 打开设置时 OnReset -> CONFIG_HASH_CHANGED 不再清池、
	// 避免“闪 + 卡 + 重加载文本池”的语义保障。
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::DPI_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::UI_SCALE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::CONFIG_HASH_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::SECTION_SIZE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED));
}

TEST(SettingsRuntimeCache, RuntimeKeyMismatchNamesDirtyReason)
{
	SSettingsSectionCacheRuntimeKey Base;
	Base.m_ViewportWidth = 900;
	Base.m_ViewportHeight = 620;
	Base.m_UiScale = 100;
	Base.m_ConfigHash = 10;
	Base.m_LanguageHash = 11;
	Base.m_FontHash = 12;
	Base.m_BackendHash = 13;
	Base.m_WindowHash = 14;

	SSettingsSectionCacheRuntimeKey Language = Base;
	Language.m_LanguageHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Language), ESettingsCacheDirtyReason::LANGUAGE);

	SSettingsSectionCacheRuntimeKey Font = Base;
	Font.m_FontHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Font), ESettingsCacheDirtyReason::FONT);

	SSettingsSectionCacheRuntimeKey Backend = Base;
	Backend.m_BackendHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Backend), ESettingsCacheDirtyReason::GRAPHICS_RESET);

	SSettingsSectionCacheRuntimeKey UiScale = Base;
	UiScale.m_UiScale++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, UiScale), ESettingsCacheDirtyReason::UI_SCALE);

	SSettingsSectionCacheRuntimeKey Viewport = Base;
	Viewport.m_ViewportHeight++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Viewport), ESettingsCacheDirtyReason::WINDOW_SIZE);

	SSettingsSectionCacheRuntimeKey Config = Base;
	Config.m_ConfigHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Config), ESettingsCacheDirtyReason::CONFIG);
}

TEST(SettingsRuntimeCache, CompactVisibleTextIsRejected)
{
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientPetSection"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("Controls:Mouse"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText(nullptr));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientDeferredSummary"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientCompactSummary"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientSummaryBlock"));
}

TEST(SettingsResourceJobs, SkinPlanKeepsSelectedFavoritesThenSorted)
{
	std::vector<SSettingsSkinListEntry> vEntries = {
		{"zeta", false, false},
		{"alpha", false, true},
		{"selected", true, false},
	};
	const SSettingsSkinListPlan Plan = BuildSettingsSkinListPlan(vEntries, 0);

	ASSERT_EQ(Plan.m_vNames.size(), 3u);
	EXPECT_EQ(Plan.m_vNames[0], "selected");
	EXPECT_EQ(Plan.m_vNames[1], "alpha");
	EXPECT_EQ(Plan.m_vNames[2], "zeta");
}

TEST(SettingsResourceJobs, SkinPlanTimeSortKeepsFavoritesThenOrdersGroupsByOfficialDate)
{
	std::vector<SSettingsSkinListEntry> vEntries = {
		{"old_regular", false, false, {}, 20200101, 40},
		{"new_regular", false, false, {}, 20250614, 10},
		{"old_favorite", false, true, {}, 20200102, 30},
		{"new_favorite", false, true, {}, 20250615, 20},
	};
	const SSettingsSkinListPlan Plan = BuildSettingsSkinListPlan(vEntries, 1);

	ASSERT_EQ(Plan.m_vNames.size(), 4u);
	EXPECT_EQ(Plan.m_vNames[0], "new_favorite");
	EXPECT_EQ(Plan.m_vNames[1], "old_favorite");
	EXPECT_EQ(Plan.m_vNames[2], "new_regular");
	EXPECT_EQ(Plan.m_vNames[3], "old_regular");
}

TEST(SettingsResourceJobs, SkinPreviewFitsInsideListRow)
{
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 60.0f, 50.0f), 40.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(34.0f, 60.0f, 50.0f), 24.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 24.0f, 50.0f), 14.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 60.0f, 50.0f, 50.0f, 70.0f), 50.0f * (40.0f / 70.0f));
	EXPECT_FLOAT_EQ(SettingsSkinPreviewSize(50.0f, 35.0f, 50.0f, 70.0f, 40.0f), 50.0f * (25.0f / 70.0f));
	EXPECT_FLOAT_EQ(SettingsSkinPreviewCenterOffset(-10.0f, 30.0f), -10.0f);
	EXPECT_FLOAT_EQ(SettingsSkinPreviewCenterOffset(-35.0f, 15.0f), 10.0f);
}

TEST(SettingsResourceJobs, CachedSkinPreviewCountsAsReady)
{
	EXPECT_FALSE(SettingsSkinListEntryReady(false, false, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(true, false, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(false, true, false));
	EXPECT_TRUE(SettingsSkinListEntryReady(false, false, true));
}

TEST(SettingsResourceJobs, CountryFlagPlanDeduplicatesAndKeepsOrder)
{
	const std::vector<int> vPlan = BuildSettingsCountryFlagWarmupPlan({156, 840, 156, -1});
	ASSERT_EQ(vPlan.size(), 3u);
	EXPECT_EQ(vPlan[0], 156);
	EXPECT_EQ(vPlan[1], 840);
	EXPECT_EQ(vPlan[2], -1);
}

TEST(SettingsResourceJobs, ResourceMergeBudgetStopsBatchWork)
{
	SSettingsResourceMergeBudget Budget;
	Budget.m_MaxListEntries = 2;
	Budget.m_MaxGpuUploads = 1;

	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_FALSE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);

	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(Budget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(Budget));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceJobs, ResourceMergeBudgetHonorsUnifiedFrameBudget)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxListEntries = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxJobResultMerges = 1;

	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(ResourceBudget, &FrameBudget));
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(ResourceBudget, &FrameBudget));
	EXPECT_EQ(ResourceBudget.m_MaxListEntries, 6);
	EXPECT_EQ(ResourceBudget.m_StopReason, ESettingsWarmupStopReason::NONE);

	SSettingsResourceMergeBudget NextBatchBudget;
	NextBatchBudget.m_MaxListEntries = 8;
	EXPECT_FALSE(SettingsResourceConsumeMergeEntry(NextBatchBudget, &FrameBudget));
	EXPECT_EQ(NextBatchBudget.m_MaxListEntries, 8);
	EXPECT_EQ(NextBatchBudget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
}

TEST(SettingsResourceJobs, ResourceGpuUploadBudgetHonorsUnifiedFrameBudget)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 1;

	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(ResourceBudget, &FrameBudget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(ResourceBudget, &FrameBudget));
	EXPECT_EQ(ResourceBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 7);
}

TEST(SettingsResourceJobs, ResourceGpuUploadBudgetCanReserveMultipleUploads)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 3;

	EXPECT_TRUE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 3));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 5);
	EXPECT_FALSE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 1));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 5);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceJobs, DefaultSettingsGpuBudgetAllowsOneSkinUploadBatch)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 42;
	SSettingsWarmupFrameBudget FrameBudget;

	EXPECT_TRUE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 14));
	EXPECT_FALSE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 14));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 28);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceJobs, AssetListLoadingDoesNotBlockVisibleEntries)
{
	EXPECT_TRUE(SettingsAssetListShouldShowBlockingLoading(true, 0));
	EXPECT_FALSE(SettingsAssetListShouldShowBlockingLoading(true, 1));
	EXPECT_FALSE(SettingsAssetListShouldShowBlockingLoading(false, 0));
}

TEST(SettingsResourceJobs, AssetPreviewBudgetedTextureSizeFitsUploadBudget)
{
	const int TextureSize = SettingsAssetPreviewBudgetedTextureSize(
		LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
		ASSET_PREVIEW_MIN_TEXTURE_SIZE,
		512ull * 1024ull * 1024ull,
		0,
		0);
	EXPECT_EQ(TextureSize, ASSET_PREVIEW_MIN_TEXTURE_SIZE);
	EXPECT_LE(PreviewTextureSizeBytesEstimate(TextureSize), ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME);
}

TEST(SettingsResourceJobs, AssetPreviewDecodeCanStartWhileMerging)
{
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(true, false, false));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, true, false));
	EXPECT_TRUE(SettingsAssetListCanStartPreviewDecode(false, false, true));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, false, false));
}

TEST(SettingsResourceJobs, AssetPreviewFinalizeBudgetDefersAfterLimit)
{
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(0, 10.0, 2, 4.0));
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(1, 3.5, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(1, 4.0, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(2, 0.0, 2, 4.0));
}

TEST(SettingsResourceJobs, InactiveWindowBlocksAllAssetStarts)
{
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, false));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, true));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, true));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, false));
}

TEST(SettingsResourceJobs, AssetPreviewPrioritizesCurrentVisibleRange)
{
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(9, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(10, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(15, 10, 20));
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(20, 10, 20));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(21, 10, 20));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(10, -1, 20));
}

TEST(SettingsResourceJobs, WorkshopThumbDecodePrioritizesVisibleDownloadableItems)
{
	EXPECT_TRUE(SettingsWorkshopThumbShouldStartHighPriority(0, 0, 3));
	EXPECT_TRUE(SettingsWorkshopThumbShouldStartHighPriority(3, 0, 3));
	EXPECT_FALSE(SettingsWorkshopThumbShouldStartHighPriority(4, 0, 3));
	EXPECT_FALSE(SettingsWorkshopThumbShouldStartHighPriority(0, -1, 3));
}

TEST(SettingsResourceJobs, VisibleResourceStartsCanUsePriorityBudget)
{
	EXPECT_TRUE(SettingsResourceCanUseHighPriorityBudget(5, 6, 12, false));
	EXPECT_FALSE(SettingsResourceCanUseHighPriorityBudget(6, 6, 12, false));
	EXPECT_TRUE(SettingsResourceCanUseHighPriorityBudget(6, 6, 12, true));
	EXPECT_FALSE(SettingsResourceCanUseHighPriorityBudget(12, 6, 12, true));
}

TEST(SettingsResourceJobs, UploadByteBudgetRejectsOversizedFirstUpload)
{
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(0, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_TRUE(SettingsResourceUploadWithinByteBudget(0, 0, 512 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(1, 512 * 1024, 768 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, OversizedUploadAlwaysDeferredToAvoidFocusBurst)
{
	SSettingsResourceFrameContext IdleVisible{};
	IdleVisible.m_ScrollActive = false;
	IdleVisible.m_PostScrollRecoveryFrames = 0;

	SSettingsResourceFrameContext ScrollActive = IdleVisible;
	ScrollActive.m_ScrollActive = true;

	SSettingsResourceFrameContext Recovery = IdleVisible;
	Recovery.m_PostScrollRecoveryFrames = 2;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, true, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(ScrollActive, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Recovery, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, false, ESettingsResourcePriority::BACKGROUND, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, false, ESettingsResourcePriority::VISIBLE, 0, 512 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, OversizedUploadNeverGetsStableFrameException)
{
	SSettingsResourceFrameContext Stable{};
	Stable.m_ScrollActive = false;
	Stable.m_PostScrollRecoveryFrames = 0;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 1, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, VisibleNormalSizedUploadStillUsesByteBudget)
{
	SSettingsResourceFrameContext Stable{};
	Stable.m_ScrollActive = false;
	Stable.m_PostScrollRecoveryFrames = 0;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 1, 512 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(1, 768 * 1024, 512 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, ScrollActiveResourceStageBudgetBlocksBackgroundUploads)
{
	SSettingsResourceFrameContext ScrollActive{};
	ScrollActive.m_ScrollActive = true;
	ScrollActive.m_PostScrollRecoveryFrames = 0;

	EXPECT_EQ(SettingsResourceFrameStageBudget(ScrollActive, ESettingsResourcePriority::BACKGROUND, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(ScrollActive, ESettingsResourcePriority::VISIBLE, 4, 1), 1);
}

TEST(SettingsResourceJobs, PostScrollRecoveryDefersOversizedUploads)
{
	SSettingsResourceFrameContext Recovery{};
	Recovery.m_ScrollActive = false;
	Recovery.m_PostScrollRecoveryFrames = 3;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Recovery, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, ImmediateScrollInputBlocksHeavyAssetsWorkBeforePersistentStateCatchesUp)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, 0);

	EXPECT_EQ(SettingsResourceSharedHeavyBudget(Idle, 4, 1), 4);
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(ImmediateScroll, 4, 1), 0);
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(ImmediateScroll, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, PostListScrollStateClampsStaleIdleHeavyBudgetBeforeAssetsFinalize)
{
	const SSettingsResourceFrameContext PreListIdle = SettingsBuildFrameContext(false, false, 0);
	const int PreListHeavyBudget = SettingsResourceSharedHeavyBudget(PreListIdle, 4, 1);
	const SSettingsResourceFrameContext PostListScroll = SettingsBuildFrameContext(false, true, 0);

	EXPECT_EQ(PreListHeavyBudget, 4);
	EXPECT_EQ(SettingsResourceClampSharedHeavyBudget(PreListHeavyBudget, PostListScroll, 4, 1), 0);
}

TEST(SettingsResourceJobs, PostListRecoveryStateClampsStaleIdleHeavyBudgetForWorkshopThumbs)
{
	const SSettingsResourceFrameContext PreListIdle = SettingsBuildFrameContext(false, false, 0);
	const int PreListHeavyBudget = SettingsResourceSharedHeavyBudget(PreListIdle, 4, 1);
	const SSettingsResourceFrameContext PostListRecovery = SettingsBuildFrameContext(false, false, 2);

	EXPECT_EQ(PreListHeavyBudget, 4);
	EXPECT_EQ(SettingsResourceClampSharedHeavyBudget(PreListHeavyBudget, PostListRecovery, 4, 1), 1);
}

TEST(SettingsResourceJobs, AssetsLocalListFinalizesHeavyPreviewWorkOnlyAfterListEnd)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t LocalListPos = Source.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalListPos, std::string::npos);
	const size_t LocalListEnd = Source.find("auto ResetSelectedAssetToDefault = [&](const char *pDeletedName) {", LocalListPos);
	ASSERT_NE(LocalListEnd, std::string::npos);
	const std::string LocalListBody = Source.substr(LocalListPos, LocalListEnd - LocalListPos);

	const size_t DoEndPos = LocalListBody.find("const int NewSelected = s_ListBox.DoEnd();");
	const size_t ScrollPos = LocalListBody.find("const bool ListScrollActive = QmMenuUiScrollPerfActive(");
	const size_t FrameContextPos = LocalListBody.find("const SSettingsResourceFrameContext PreviewUploadFrameContext = SettingsBuildFrameContext(");
	const size_t ClampPos = LocalListBody.find("RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(");
	const size_t FinalizePos = LocalListBody.find("FinalizeReadyPreviewDecodes(PreviewUploadFrameContext);");
	const size_t DrainPos = LocalListBody.find("DrainReadyPreviewUploadsAfterList(PreviewUploadFrameContext);");

	ASSERT_NE(DoEndPos, std::string::npos);
	ASSERT_NE(ScrollPos, std::string::npos);
	ASSERT_NE(FrameContextPos, std::string::npos);
	ASSERT_NE(ClampPos, std::string::npos);
	ASSERT_NE(FinalizePos, std::string::npos);
	ASSERT_NE(DrainPos, std::string::npos);

	EXPECT_LT(DoEndPos, ScrollPos);
	EXPECT_LT(ScrollPos, FrameContextPos);
	EXPECT_LT(FrameContextPos, ClampPos);
	EXPECT_LT(ClampPos, FinalizePos);
	EXPECT_LT(FinalizePos, DrainPos);
}

TEST(SettingsResourceJobs, AssetsWorkshopListFinalizesPreviewAndThumbWorkOnlyAfterListEnd)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t WorkshopListPos = Source.find("static CListBox s_WorkshopAssetsListBox;");
	ASSERT_NE(WorkshopListPos, std::string::npos);
	const size_t WorkshopListEnd = Source.find("if(DeleteLocalRequested)", WorkshopListPos);
	ASSERT_NE(WorkshopListEnd, std::string::npos);
	const std::string WorkshopListBody = Source.substr(WorkshopListPos, WorkshopListEnd - WorkshopListPos);

	const size_t DoEndPos = WorkshopListBody.find("const int NewCombinedSelected = s_WorkshopAssetsListBox.DoEnd();");
	const size_t ScrollPos = WorkshopListBody.find("const bool WorkshopListScrollActive = QmMenuUiScrollPerfActive(");
	const size_t FrameContextPos = WorkshopListBody.find("const SSettingsResourceFrameContext WorkshopUploadFrameContext = SettingsBuildFrameContext(");
	const size_t ClampPos = WorkshopListBody.find("RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(");
	const size_t PreviewFinalizePos = WorkshopListBody.find("FinalizeReadyPreviewDecodes(WorkshopUploadFrameContext);");
	const size_t PreviewDrainPos = WorkshopListBody.find("DrainReadyPreviewUploadsAfterList(WorkshopUploadFrameContext);");
	const size_t ThumbFinalizePos = WorkshopListBody.find("FinalizeWorkshopReadyThumbs(WorkshopUploadFrameContext);");
	const size_t ThumbDrainPos = WorkshopListBody.find("DrainWorkshopReadyThumbUploads(WorkshopUploadFrameContext);");

	ASSERT_NE(DoEndPos, std::string::npos);
	ASSERT_NE(ScrollPos, std::string::npos);
	ASSERT_NE(FrameContextPos, std::string::npos);
	ASSERT_NE(ClampPos, std::string::npos);
	ASSERT_NE(PreviewFinalizePos, std::string::npos);
	ASSERT_NE(PreviewDrainPos, std::string::npos);
	ASSERT_NE(ThumbFinalizePos, std::string::npos);
	ASSERT_NE(ThumbDrainPos, std::string::npos);

	EXPECT_LT(DoEndPos, ScrollPos);
	EXPECT_LT(ScrollPos, FrameContextPos);
	EXPECT_LT(FrameContextPos, ClampPos);
	EXPECT_LT(ClampPos, PreviewFinalizePos);
	EXPECT_LT(PreviewFinalizePos, PreviewDrainPos);
	EXPECT_LT(PreviewDrainPos, ThumbFinalizePos);
	EXPECT_LT(ThumbFinalizePos, ThumbDrainPos);
}

TEST(SettingsResourceJobs, AssetsListsBuildFrameContextFromJumpScrollStateBeforeHeavyStages)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("const bool ListJumpScrollActive ="), std::string::npos);
	EXPECT_NE(Source.find("const bool WorkshopListJumpScrollActive ="), std::string::npos);
	EXPECT_NE(Source.find("s_AssetsScrollCooldownFrames > 0, ListScrollActive, ListJumpScrollActive, s_AssetsPostScrollRecoveryFrames"), std::string::npos);
	EXPECT_NE(Source.find("s_AssetsScrollCooldownFrames > 0, WorkshopListScrollActive, WorkshopListJumpScrollActive, s_AssetsPostScrollRecoveryFrames"), std::string::npos);
	EXPECT_NE(Source.find("frame_context=%s jump_scroll=%d"), std::string::npos);
}

TEST(SettingsResourceJobs, VisibleReadyPreviewKeepsUploadPriority)
{
	EXPECT_TRUE(SettingsAssetPreviewShouldPrioritizeVisibleRange(3, 3, 5));
	EXPECT_FALSE(SettingsAssetPreviewShouldPrioritizeVisibleRange(2, 3, 5));
	EXPECT_TRUE(SettingsAssetPreviewShouldUploadHighPriorityFirst(false, true));
	EXPECT_FALSE(SettingsAssetPreviewShouldUploadHighPriorityFirst(true, false));
	EXPECT_FALSE(SettingsAssetPreviewShouldUploadHighPriorityFirst(false, false));
}

TEST(SettingsResourceJobs, AssetsFocusHandlingDoesNotUseWindowRecoveryFrames)
{
	std::ifstream MenusHeaderFile(TestSourcePath("src/game/client/components/menus.h"));
	ASSERT_TRUE(MenusHeaderFile.good());
	std::stringstream MenusHeaderBuffer;
	MenusHeaderBuffer << MenusHeaderFile.rdbuf();
	const std::string MenusHeaderSource = MenusHeaderBuffer.str();

	EXPECT_EQ(MenusHeaderSource.find("m_LastWindowActive"), std::string::npos);
	EXPECT_EQ(MenusHeaderSource.find("m_WindowRecoveryFrames"), std::string::npos);

	std::ifstream MenusSourceFile(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(MenusSourceFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusSourceFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();

	EXPECT_EQ(MenusSource.find("m_WindowRecoveryFrames = 10"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_LastWindowActive = CurrentWindowActive"), std::string::npos);
}

TEST(SettingsResourceJobs, AssetsInactiveWindowBehaviorSkipsRecoveryPurgeAndUsesDirectWindowGate)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("Graphics()->UnloadTexture(&Entity.m_RenderTexture);"), std::string::npos);
	EXPECT_EQ(Source.find("EffectiveMaxPreviewUploadsPerFrame = m_WindowRecoveryFrames > 0 ? 0 : MaxPreviewUploadsPerFrame"), std::string::npos);
	EXPECT_EQ(Source.find("EffectiveMaxWorkshopThumbUploadsPerFrame = m_WindowRecoveryFrames > 0 ? 0 : MaxWorkshopThumbUploadsPerFrame"), std::string::npos);
	EXPECT_NE(Source.find("const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;"), std::string::npos);
	EXPECT_NE(Source.find("if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, HighPriority))"), std::string::npos);
	EXPECT_NE(Source.find("if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, Asset.m_ThumbHighPriority))"), std::string::npos);
	EXPECT_NE(Source.find("if(!WindowActive)\n\t\t\treturn;"), std::string::npos);
	EXPECT_NE(Source.find("LogAssetsPerfStageForClient(Client(), \"assets_window_focus\""), std::string::npos);
}

TEST(SettingsResourceJobs, InactiveWindowBlocksAllNewAssetWorkStarts)
{
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, false));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, true));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, false));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, true));
}

TEST(SettingsResourceJobs, AssetsFocusLogsIncludeTextureMemoryAndResidentPreviewBytes)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("TextureMemoryUsage()"), std::string::npos);
	EXPECT_NE(Source.find("resident_preview_bytes"), std::string::npos);
	EXPECT_NE(Source.find("workshop_resident_preview_bytes"), std::string::npos);
}

TEST(SettingsResourceJobs, EntityBgCorruptInstallProbeReadsOnlyFileHeader)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("IOHANDLE File = pStorage->OpenFile(Asset.m_InstallPath.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);"), std::string::npos);
	EXPECT_NE(Source.find("unsigned char aHeader[16] = {};"), std::string::npos);
	EXPECT_NE(Source.find("const unsigned BytesRead = io_read(File, aHeader, sizeof(aHeader));"), std::string::npos);
	EXPECT_EQ(Source.find("pStorage->ReadFile(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE, &pFileData, &FileSize)"), std::string::npos);
}

TEST(SettingsResourceJobs, AssetsFocusObservationUsesResumeFrameContextAndSwapTelemetry)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("focus_resume=%d"), std::string::npos);
	EXPECT_NE(Source.find("graphics_swap"), std::string::npos);
	EXPECT_NE(Source.find("LogAssetsPerfStageForClient(Client(), \"assets_focus_observation\""), std::string::npos);
}

TEST(SettingsResourceJobs, WorkshopThumbStartAvoidsDuplicateQueuePushForMatchingState)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("Asset.m_ThumbQueuedTier"), std::string::npos);
	EXPECT_NE(Source.find("Asset.m_ThumbQueuedEpoch"), std::string::npos);
	EXPECT_NE(Source.find("Asset.m_ThumbQueuedTab"), std::string::npos);
}

TEST(SettingsResourceJobs, PreviewTierUpgradeReplacesExistingTexturesInsteadOfLeakingOrDropping)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("UnloadTexture(&pItem->m_RenderTexture);"), std::string::npos);
	EXPECT_NE(Source.find("pGraphics->UnloadTexture(&Asset.m_ThumbTexture);"), std::string::npos);
	EXPECT_NE(Source.find("Item.m_PreviewResidentBytes = 0;"), std::string::npos);
	EXPECT_NE(Source.find("SettingsAssetPreviewResidentTextureSatisfiesRequest(\n\t\t\t\t\t\ttrue,\n\t\t\t\t\t\tpAsset->m_ThumbResidentBytes,\n\t\t\t\t\t\tpAsset->m_ThumbRequestedTextureSize)"), std::string::npos);
}

TEST(SettingsResourceJobs, WorkshopRefreshPreservesPreviewRuntimeMetadata)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("NewAsset.m_ThumbRequestedTextureSize = ExistingAsset.m_ThumbRequestedTextureSize;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbResidentBytes = ExistingAsset.m_ThumbResidentBytes;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedTier = ExistingAsset.m_ThumbQueuedTier;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedEpoch = ExistingAsset.m_ThumbQueuedEpoch;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedTab = ExistingAsset.m_ThumbQueuedTab;"), std::string::npos);
}

TEST(SettingsResourceJobs, BudgetedPreviewCanUpgradeTierWhenHigherBudgetReturns)
{
	EXPECT_FALSE(SettingsAssetPreviewResidentTextureSatisfiesRequest(true, PreviewTextureSizeBytesEstimate(512), 1024));
	EXPECT_TRUE(SettingsAssetPreviewResidentTextureSatisfiesRequest(true, PreviewTextureSizeBytesEstimate(1024), 1024));
	EXPECT_TRUE(SettingsAssetPreviewDecodeStartNeeded(false, true, PreviewTextureSizeBytesEstimate(512), 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(false, true, PreviewTextureSizeBytesEstimate(1024), 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(true, false, 0, 1024, false));
	EXPECT_FALSE(SettingsAssetPreviewDecodeStartNeeded(false, false, 0, 1024, true));
}

TEST(SettingsWarmupCleanup, SourceNoLongerReferencesSettingsPageFboPaths)
{
	std::ifstream MenusFile(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(MenusFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();

	EXPECT_EQ(MenusSource.find("PrewarmSettingsPageRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("DrawSettingsPageRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("InvalidateSettingsPageRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("PrewarmSettingsSectionRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("DrawSettingsSectionRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("InvalidateSettingsSectionRuntimeCache"), std::string::npos);
	EXPECT_EQ(MenusSource.find("DestroySettingsPageRuntimeCaches"), std::string::npos);
	EXPECT_EQ(MenusSource.find("PrepareGenericSettingsRuntimeCacheSection"), std::string::npos);
	EXPECT_EQ(MenusSource.find("MakeSettingsPageRuntimeKey"), std::string::npos);
}

TEST(SettingsWarmupCleanup, SourceNoLongerReferencesSettingsRuntimeFboContracts)
{
	std::ifstream RuntimeHeaderFile(TestSourcePath("src/game/client/components/settings_runtime_cache.h"));
	ASSERT_TRUE(RuntimeHeaderFile.good());
	std::stringstream RuntimeHeaderBuffer;
	RuntimeHeaderBuffer << RuntimeHeaderFile.rdbuf();
	const std::string RuntimeHeaderSource = RuntimeHeaderBuffer.str();

	EXPECT_EQ(RuntimeHeaderSource.find("RENDER_TARGET_RECORD"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("FBO_BUDGET"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("GPU_READBACK_BUDGET"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("PREVIEW_CACHE_IO_BUDGET"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("PAGE_FBO_UNSUPPORTED"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("PAGE_FBO_NOT_READY"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SECTION_FBO_NOT_READY"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("m_MaxRenderTargetRecords"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("m_MaxGpuReadbacks"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("m_MaxPreviewCacheIo"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SSettingsPageRuntimeRegistry"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SSettingsRuntimeCacheMetadata"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SSettingsWarmupPageJob"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SETTINGS_PAGE_RUNTIME_CACHE_SLOTS"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsPageRuntimeCacheSlot"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsSectionCanRecordStaticFbo"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsWarmupEnabled"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsRuntimeCachingEnabled"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsInvalidationClearsSectionFbo"), std::string::npos);
	EXPECT_EQ(RuntimeHeaderSource.find("SettingsInvalidationClearsPageFbo"), std::string::npos);

	std::ifstream WarmupHeaderFile(TestSourcePath("src/game/client/components/settings_warmup.h"));
	ASSERT_TRUE(WarmupHeaderFile.good());
	std::stringstream WarmupHeaderBuffer;
	WarmupHeaderBuffer << WarmupHeaderFile.rdbuf();
	const std::string WarmupHeaderSource = WarmupHeaderBuffer.str();
	EXPECT_EQ(WarmupHeaderSource.find("RUNTIME_FBO"), std::string::npos);
	EXPECT_EQ(WarmupHeaderSource.find("SSettingsPageRuntimeCacheState"), std::string::npos);
	EXPECT_EQ(WarmupHeaderSource.find("SettingsPageRuntimeCacheShouldShortCircuit"), std::string::npos);

	std::ifstream ResourceFile(TestSourcePath("src/game/client/components/settings_resource_jobs.cpp"));
	ASSERT_TRUE(ResourceFile.good());
	std::stringstream ResourceBuffer;
	ResourceBuffer << ResourceFile.rdbuf();
	const std::string ResourceSource = ResourceBuffer.str();
	EXPECT_EQ(ResourceSource.find("SettingsPageCacheCanUseRecordedResources"), std::string::npos);
	EXPECT_EQ(ResourceSource.find("SettingsPageRecordedCacheMissReason"), std::string::npos);
	EXPECT_EQ(ResourceSource.find("SettingsPageCanUsePageFbo"), std::string::npos);

	std::ifstream SkinsFile(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(SkinsFile.good());
	std::stringstream SkinsBuffer;
	SkinsBuffer << SkinsFile.rdbuf();
	const std::string SkinsSource = SkinsBuffer.str();
	EXPECT_EQ(SkinsSource.find("FBO_BUDGET"), std::string::npos);
}

TEST(SettingsWarmupCleanup, SourceKeepsTeeMemoryPreviewCacheAndWorkshopThumbCache)
{
	std::ifstream MenusSettingsFile(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(MenusSettingsFile.good());
	std::stringstream MenusSettingsBuffer;
	MenusSettingsBuffer << MenusSettingsFile.rdbuf();
	const std::string MenusSettingsSource = MenusSettingsBuffer.str();

	EXPECT_NE(MenusSettingsSource.find("SSettingsTeeListPreviewCache"), std::string::npos);
	EXPECT_NE(MenusSettingsSource.find("gs_TeeListPreviewCache"), std::string::npos);
	EXPECT_EQ(MenusSettingsSource.find("settings_skin_preview_cache"), std::string::npos);

	std::ifstream AssetsFile(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(AssetsFile.good());
	std::stringstream AssetsBuffer;
	AssetsBuffer << AssetsFile.rdbuf();
	const std::string AssetsSource = AssetsBuffer.str();
	EXPECT_NE(AssetsSource.find("qmclient/workshop/thumbs/%s.webp"), std::string::npos);
	EXPECT_NE(AssetsSource.find("m_ThumbCachePath"), std::string::npos);
}

TEST(SettingsResourceJobs, AssetWarmupTracksAllTabsAndCycles)
{
	bool aReadyTabs[] = {true, false, true};
	EXPECT_FALSE(SettingsAssetWarmupAllTabsReady(aReadyTabs, 3));
	aReadyTabs[1] = true;
	EXPECT_TRUE(SettingsAssetWarmupAllTabsReady(aReadyTabs, 3));
	EXPECT_TRUE(SettingsAssetWarmupAllTabsReady(nullptr, 0));

	EXPECT_EQ(SettingsAssetWarmupNextTab(-1, 3), 0);
	EXPECT_EQ(SettingsAssetWarmupNextTab(0, 3), 1);
	EXPECT_EQ(SettingsAssetWarmupNextTab(2, 3), 0);
	EXPECT_EQ(SettingsAssetWarmupNextTab(0, 0), -1);
}

TEST(SettingsResourceJobs, SkinSnapshotRejectsStaleGeneration)
{
	SSettingsSkinListPlanResult Result;
	Result.m_Generation = 7;
	EXPECT_TRUE(SettingsSkinListPlanGenerationMatches(Result, 7));
	EXPECT_FALSE(SettingsSkinListPlanGenerationMatches(Result, 8));
}

TEST(SettingsResourceJobs, AssetListRejectsStaleJobGeneration)
{
	EXPECT_TRUE(SettingsAssetListJobGenerationMatches(4, 4));
	EXPECT_FALSE(SettingsAssetListJobGenerationMatches(4, 5));
}

TEST(SettingsResourceJobs, SkinListPublishesOnlyCompleteMergedList)
{
	EXPECT_FALSE(SettingsSkinListShouldPublishMergedList(0, 3));
	EXPECT_FALSE(SettingsSkinListShouldPublishMergedList(2, 3));
	EXPECT_TRUE(SettingsSkinListShouldPublishMergedList(3, 3));
	EXPECT_TRUE(SettingsSkinListShouldPublishMergedList(0, 0));
}

TEST(SettingsResourceJobs, SkinListReplacesPublishedEntriesAfterStableDirectory)
{
	EXPECT_TRUE(SettingsSkinListShouldReplacePublishedEntries(0, 3, true, true));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 3, false, false));
	EXPECT_TRUE(SettingsSkinListShouldReplacePublishedEntries(0, 3, false, true));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(10, 20, false, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(20, 10, false, false));
}

TEST(SettingsResourceJobs, SkinListKeepsPublishedEntriesUntilDirectoryScanSettles)
{
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 1, true, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 3, true, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(3, 1, true, false));
}

TEST(SettingsResourceJobs, SkinListSkeletonReadyDoesNotRequirePreviewResources)
{
	SSkinListPlanState State{};
	State.m_DirectoryScanPending = false;
	State.m_MergeComplete = true;
	State.m_ItemCount = 120;
	EXPECT_TRUE(SettingsSkinListSkeletonReady(State));
	EXPECT_FALSE(SettingsSkinListResourcesSettled(State));
}

TEST(SettingsResourceJobs, SkinListReadyMigrationKeepsStructureStable)
{
	SSkinListPlanSnapshot Snapshot{};
	Snapshot.m_ItemCount = 120;

	SSkinListPlanState Loading{};
	Loading.m_DirectoryScanPending = false;
	Loading.m_MergeComplete = true;
	Loading.m_ItemCount = 120;
	Loading.m_BackgroundBacklog = 5;
	Loading.m_VisibleBacklog = 1;

	SSkinListPlanState Settled = Loading;
	Settled.m_BackgroundBacklog = 0;
	Settled.m_VisibleBacklog = 0;

	EXPECT_FALSE(SettingsSkinListResourcesSettled(Loading));
	EXPECT_TRUE(SettingsSkinListResourcesSettled(Settled));
	EXPECT_EQ(Snapshot.m_ItemCount, 120);
}

TEST(SettingsResourceJobs, SourceAdmissionAllowsVisiblePromotionWhenDrainInactive)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		false,
		24,
		192,
		256,
	});

	EXPECT_TRUE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_PromotePriority, ESettingsResourcePriority::VISIBLE);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::NONE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobs, SourceAdmissionBlocksBackgroundPromotionWhenDrainInactive)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::BACKGROUND,
		false,
		24,
		192,
		256,
	});

	EXPECT_FALSE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::DRAIN_INACTIVE);
	EXPECT_TRUE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobs, SourceAdmissionUsesVisibleReserveForVisibleRequests)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		true,
		256,
		192,
		256,
	});

	EXPECT_FALSE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobs, AllVisibleReadyLoggingRequiresVisibleRows)
{
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(true, false, 0));
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(false, false, 28));
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(true, true, 28));
	EXPECT_TRUE(SettingsSkinListShouldLogAllVisibleReady(true, false, 28));
}

TEST(SettingsResourceJobs, SkeletonReadyPublishedCountMatchesSnapshotCount)
{
	SSkinListPlanSnapshot Snapshot{};
	Snapshot.m_ItemCount = 120;
	std::vector<int> vPublished(120, 0);
	EXPECT_EQ(static_cast<int>(vPublished.size()), Snapshot.m_ItemCount);
}

TEST(SettingsResourceJobs, SkinListKeepsPendingPlanAliveUntilPublishGateOpens)
{
	EXPECT_TRUE(SettingsSkinListHasPendingMergeWork(true, 3, 3, 3));
	EXPECT_TRUE(SettingsSkinListHasPendingMergeWork(true, 0, 0, 0));
	EXPECT_FALSE(SettingsSkinListHasPendingMergeWork(false, 3, 3, 3));
}

TEST(SettingsResourceJobs, VisibleSkinListEntriesRequestImmediateLoad)
{
	EXPECT_TRUE(SettingsSkinListShouldRequestImmediateLoad(true));
	EXPECT_FALSE(SettingsSkinListShouldRequestImmediateLoad(false));
}

TEST(SettingsResourceJobs, RuntimeWarmupOnlyRunsOnSettingsPageWhenIdle)
{
	EXPECT_TRUE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, false, false, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, true, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, true, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, true, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, true, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, false, true));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(false, true, false, false, false, false, false));
}

TEST(SettingsResourceJobs, RuntimePrewarmCallsitesRequireVisibleIdleSettingsPage)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");

	EXPECT_EQ(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\ttrue,"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\tm_MenuPage == PAGE_SETTINGS,"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsRuntimeWarmupShouldRun(\n\t\t\t\tg_Config.m_QmSettingsPrewarm != 0,\n\t\t\t\tm_GamePage == PAGE_SETTINGS,"), std::string::npos);
	EXPECT_NE(Menus.find("m_SettingsPageSwitchActive || TransitionActive"), std::string::npos);
}

TEST(SettingsResourceJobs, IdlePrewarmSkipsImmediateModeRenderPasses)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PrewarmPos = Source.find("void CMenus::PrewarmVisibleSettingsResources(CUIRect MainView)");
	ASSERT_NE(PrewarmPos, std::string::npos);
	const size_t PrewarmEnd = Source.find("bool CMenus::OnCursorMove", PrewarmPos);
	ASSERT_NE(PrewarmEnd, std::string::npos);
	const std::string PrewarmBody = Source.substr(PrewarmPos, PrewarmEnd - PrewarmPos);

	EXPECT_EQ(PrewarmBody.find("RenderSettingsTClient(ContentView, true)"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("RenderSettingsQmClient(ContentView, false, true)"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f)"), std::string::npos);
}

TEST(SettingsResourceJobs, SkinListWarmupCountsCoverVisibleAndPrefetchRows)
{
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(180.0f, 50.0f, 1, 2), 6);
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(300.0f, 50.0f, 4, 1), 28);
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(0.0f, 50.0f, 1, 2), 0);
	EXPECT_EQ(SettingsTeeSkinListFirstPageWarmupEntries(300.0f), 28);
	EXPECT_EQ(SettingsTeeSkinListFirstPageWarmupEntries(120.0f), 24);
	EXPECT_EQ(SettingsSkinListPrefetchCount(0, 2, 1, 2, 10), 2);
	EXPECT_EQ(SettingsSkinListPrefetchCount(7, 9, 1, 2, 10), 0);
	EXPECT_EQ(SettingsSkinListBackgroundWarmupCount(20, 6), 6);
	EXPECT_EQ(SettingsSkinListBackgroundWarmupCount(3, 6), 3);
}

TEST(SettingsResourceJobs, SkinBackgroundScanKeepsOneStableStartCursorPerPass)
{
	constexpr size_t ItemCount = 4;
	constexpr size_t StartCursor = 3;
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 0, ItemCount), 3u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 1, ItemCount), 0u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 2, ItemCount), 1u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 3, ItemCount), 2u);
	EXPECT_EQ(SettingsSkinBackgroundScanNextCursor(StartCursor, 2, ItemCount), 1u);
	EXPECT_EQ(SettingsSkinBackgroundScanNextCursor(StartCursor, ItemCount, ItemCount), StartCursor);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 0, 0), 0u);
}

TEST(SettingsResourceJobs, LoadingPrewarmAttemptBudgetReservesExtraTeeSourceSettlePasses)
{
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(0, 0), 33);
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(19, 28), 132);
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(19, 8), 108);
}

TEST(SettingsResourceJobs, LoadingPrewarmBudgetOnlyStopsAfterBudgetAndStall)
{
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 0, 108, 0));
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 0));
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 7));
	EXPECT_FALSE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 8));
	EXPECT_FALSE(SettingsLoadingPrewarmShouldKeepPumping(true, 20, 108, 0));
}

TEST(SettingsResourceJobs, SkinListBackgroundWarmupWaitsForIdleVisibleBacklog)
{
	EXPECT_TRUE(SettingsSkinBackgroundWarmupShouldRun(true, false, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(true, true, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(true, false, true));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(false, false, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupWindowFull(0, 20, 10, 64));
	EXPECT_TRUE(SettingsSkinBackgroundWarmupWindowFull(0, 40, 24, 64));
}

TEST(SettingsResourceJobs, TeeSkinSourceLoadWindowCapsActiveDecodeConcurrency)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(Idle, true, 1600), 256);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Idle, true, 1600), 256);
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(Scroll, true, 1600), SettingsSkinSourceLoadNormalWindow(RecoveryStart, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(RecoveryStart, true, 1600), SettingsSkinSourceLoadNormalWindow(RecoveryEnd, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(RecoveryEnd, true, 1600), SettingsSkinSourceLoadNormalWindow(Idle, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(Scroll, true, 1600), SettingsSkinSourceLoadVisibleWindow(RecoveryStart, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(RecoveryStart, true, 1600), SettingsSkinSourceLoadVisibleWindow(RecoveryEnd, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(RecoveryEnd, true, 1600), SettingsSkinSourceLoadVisibleWindow(Idle, true, 1600));
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(Scroll, true, 1600), 48);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Scroll, true, 1600), 128);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Idle, true, 32), 32);
}

TEST(SettingsResourceJobs, VisibleSkinFinalizeDefersBackgroundSweepsAfterPriorityWork)
{
	EXPECT_TRUE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 1, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(false, 1, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 0, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 2, 2));
}

TEST(SettingsResourceJobs, VisibleSkinFinalizeAllowsBackgroundSweepOnNextFrame)
{
	EXPECT_TRUE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 1, 12));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(false, 0, 12));
}

TEST(SettingsResourceJobs, TeeSkinFinalizeBudgetDefersDuringScrollAndRecovery)
{
	const SSettingsResourceFrameContext Idle = {false, false, 0};
	const SSettingsResourceFrameContext Scrolling = {true, false, 0};
	const SSettingsResourceFrameContext RecoveringStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveringEnd = SettingsBuildFrameContext(false, false, 1);

	EXPECT_EQ(SettingsSkinFinalizeMaxPerFrame(true), 64);
	EXPECT_EQ(SettingsSkinGpuUploadUnits(true), 8);
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Idle, true), SettingsSkinFinalizeMaxPerFrame(true));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Idle, true), SettingsSkinGpuUploadUnits(true));
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, true), 16);
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Scrolling, true), 4);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(Scrolling, true), SettingsSkinFinalizeFrameBudget(RecoveringStart, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringStart, true), SettingsSkinFinalizeFrameBudget(RecoveringEnd, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringEnd, true), SettingsSkinFinalizeFrameBudget(Idle, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(Scrolling, true), SettingsSkinGpuUploadFrameUnits(RecoveringStart, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(RecoveringStart, true), SettingsSkinGpuUploadFrameUnits(RecoveringEnd, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(RecoveringEnd, true), SettingsSkinGpuUploadFrameUnits(Idle, true));
}

TEST(SettingsResourceJobs, TeeSkinFinalizeIdleDrainUsesBoundedMergeBudget)
{
	SSettingsSkinThroughputControllerState State;
	const auto Settled = SettingsSkinThroughputControllerStep({
									  {false, false, 0, true},
									  true,
									  6.5f,
									  6.0f,
									  512,
									  28,
									  28,
									  0,
									  0,
									  0,
									  0,
									  0,
									  20,
									  40,
									  20,
									  12,
									  12,
									  0,
									  0,
									  288,
									  false,
									  "none",
									  "none",
								  },
		State);

	EXPECT_EQ(Settled.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_DRAIN);
	EXPECT_TRUE(Settled.m_BackgroundDrainActive);
	EXPECT_EQ(Settled.m_FinalizeBudgetLimit, 32);

	const SSettingsResourceFrameContext Scrolling = SettingsBuildFrameContext(true, false, 0);
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, true), 16);
	const SSettingsResourceFrameContext RecoveringStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveringEnd = SettingsBuildFrameContext(false, false, 1);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(Scrolling, true), SettingsSkinFinalizeFrameBudget(RecoveringStart, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringStart, true), SettingsSkinFinalizeFrameBudget(RecoveringEnd, true));
}

TEST(SettingsResourceJobs, ActiveTeeSkinFrameBudgetAllowsEightSourceUploadsPerFrame)
{
	SSettingsWarmupFrameBudget Budget;
	SettingsApplyActiveTeeSkinFrameBudget(Budget, true);

	SSettingsResourceMergeBudget UploadBudget;
	UploadBudget.m_MaxGpuUploads = 8;
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_EQ(UploadBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceJobs, TeeSkinGpuUploadLimiterBudgetTracksFrameContext)
{
	const SSettingsResourceFrameContext IdleVisible = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	SSettingsResourceFrameContext IdleDrain = SettingsBuildFrameContext(false, false, 0);
	IdleDrain.m_HighPrioritySettled = true;

	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(IdleVisible, true), 192);
	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(Scroll, true), 96);
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(Scroll, true), SettingsSkinGpuUploadLimiterUnits(RecoveryStart, true));
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(RecoveryStart, true), SettingsSkinGpuUploadLimiterUnits(RecoveryEnd, true));
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(RecoveryEnd, true), SettingsSkinGpuUploadLimiterUnits(IdleVisible, true));
	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(IdleDrain, true), 288);
}

TEST(SettingsResourceJobs, SharedHeavyBudgetTransitionsContinuouslyAfterScroll)
{
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);

	EXPECT_EQ(SettingsResourceSharedHeavyBudget(Scroll, 4, 1), 0);
	EXPECT_LT(SettingsResourceSharedHeavyBudget(Scroll, 4, 1), SettingsResourceSharedHeavyBudget(RecoveryStart, 4, 1));
	EXPECT_LE(SettingsResourceSharedHeavyBudget(RecoveryStart, 4, 1), SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1));
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1), 1);
	EXPECT_LT(SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1), SettingsResourceSharedHeavyBudget(Idle, 4, 1));
}

TEST(SettingsResourceJobs, ThroughputControllerKeepsVisibleBacklogOutOfIdleDrain)
{
	SSettingsSkinThroughputControllerState State;
	const auto VisibleBacklog = SettingsSkinThroughputControllerStep({
										 {false, false, 0, true},
										 true,
										 6.5f,
										 6.0f,
										 512,
										 28,
										 19,
										 9,
										 0,
										 9,
										 0,
										 3,
										 21,
										 29,
										 24,
										 0,
										 0,
										 0,
										 0,
										 192,
										 false,
										 "none",
										 "drain_inactive",
									 },
		State);
	EXPECT_EQ(VisibleBacklog.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_VISIBLE);
	EXPECT_FALSE(VisibleBacklog.m_BackgroundDrainActive);
	EXPECT_EQ(VisibleBacklog.m_BackgroundRequestBudget, 6);

	const auto Settled = SettingsSkinThroughputControllerStep({
									  {false, false, 0, true},
									  true,
									  6.5f,
									  6.0f,
									  512,
									  28,
									  28,
									  0,
									  0,
									  0,
									  0,
									  0,
									  20,
									  40,
									  20,
									  12,
									  12,
									  0,
									  0,
									  288,
									  false,
									  "none",
									  "none",
								  },
		State);
	EXPECT_EQ(Settled.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_DRAIN);
	EXPECT_TRUE(Settled.m_BackgroundDrainActive);
	EXPECT_EQ(Settled.m_BackgroundRequestBudget, 8);
}

TEST(SettingsResourceJobs, ThroughputControllerRelaxesReserveAndExpandsWindowsWhenAdmissionUnderfed)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 192;
	State.m_GpuUploadFrameBudget = 8;
	State.m_FinalizeBudgetLimit = 48;
	State.m_NormalLoadingWindow = 128;
	State.m_VisibleLoadingWindow = 192;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 6.9f,
									 6.7f,
									 512,
									 28,
									 19,
									 9,
									 0,
									 9,
									 0,
									 3,
									 21,
									 29,
									 30,
									 0,
									 0,
									 0,
									 0,
									 192,
									 false,
									 "visible_reserve",
									 "drain_inactive",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::ADMISSION);
	EXPECT_EQ(Output.m_VisibleReserve, 0);
	EXPECT_GT(Output.m_NormalLoadingWindow, 128);
	EXPECT_GT(Output.m_VisibleLoadingWindow, 192);
	EXPECT_TRUE(Output.m_AdmissionUnderfed);
	EXPECT_EQ(Output.m_UnderfedStreak, 1);
}

TEST(SettingsResourceJobs, ThroughputControllerReducesOnlyUploadBudgetOnGpuPressure)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 240;
	State.m_GpuUploadFrameBudget = 10;
	State.m_FinalizeBudgetLimit = 64;
	State.m_NormalLoadingWindow = 192;
	State.m_VisibleLoadingWindow = 224;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 7.0f,
									 6.8f,
									 512,
									 28,
									 20,
									 8,
									 0,
									 8,
									 0,
									 2,
									 18,
									 30,
									 20,
									 0,
									 0,
									 0,
									 0,
									 0,
									 false,
									 "gpu_upload_budget",
									 "none",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::GPU);
	EXPECT_LT(Output.m_GpuUploadLimitUnits, 240);
	EXPECT_EQ(Output.m_FinalizeBudgetLimit, 64);
	EXPECT_EQ(Output.m_NormalLoadingWindow, 192);
}

TEST(SettingsResourceJobs, ThroughputControllerReducesOnlyFinalizeBudgetOnFinalizePressure)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 240;
	State.m_GpuUploadFrameBudget = 10;
	State.m_FinalizeBudgetLimit = 64;
	State.m_NormalLoadingWindow = 192;
	State.m_VisibleLoadingWindow = 224;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 7.0f,
									 6.8f,
									 512,
									 28,
									 20,
									 8,
									 0,
									 8,
									 0,
									 2,
									 18,
									 30,
									 20,
									 0,
									 0,
									 0,
									 0,
									 96,
									 false,
									 "max_per_frame",
									 "none",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::FINALIZE);
	EXPECT_EQ(Output.m_GpuUploadLimitUnits, 240);
	EXPECT_LT(Output.m_FinalizeBudgetLimit, 64);
	EXPECT_EQ(Output.m_NormalLoadingWindow, 192);
}

TEST(SettingsResourceJobs, NonTeeSkinFinalizeBudgetKeepsLegacyLimits)
{
	const SSettingsResourceFrameContext Scrolling = {true, false, 2};
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, false), SettingsSkinFinalizeMaxPerFrame(false));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Scrolling, false), SettingsSkinGpuUploadUnits(false));
}

TEST(SettingsResourceJobs, ImmediateScrollInputKeepsReducedTeeThroughputBeforePersistentStateCatchesUp)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, 0);
	const SSettingsResourceFrameContext PersistentScroll = SettingsBuildFrameContext(true, false, 0);

	EXPECT_FALSE(Idle.m_ScrollActive);
	EXPECT_TRUE(ImmediateScroll.m_ScrollActive);
	EXPECT_TRUE(PersistentScroll.m_ScrollActive);
	EXPECT_GT(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), 0);
	EXPECT_GT(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), 0);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), SettingsSkinFinalizeFrameBudget(Idle, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), SettingsSkinGpuUploadFrameUnits(Idle, true));
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), SettingsSkinFinalizeFrameBudget(PersistentScroll, true));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), SettingsSkinGpuUploadFrameUnits(PersistentScroll, true));
}

TEST(SettingsResourceJobs, JumpScrollUsesSameHeavyBudgetGateAsImmediateScroll)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, false, 0);
	const SSettingsResourceFrameContext JumpScroll = SettingsBuildFrameContext(false, false, true, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, false, 0);

	EXPECT_FALSE(Idle.m_ScrollActive);
	EXPECT_FALSE(Idle.m_JumpScrollActive);
	EXPECT_TRUE(JumpScroll.m_JumpScrollActive);
	EXPECT_FALSE(JumpScroll.m_ScrollActive);
	EXPECT_TRUE(ImmediateScroll.m_ScrollActive);
	EXPECT_FALSE(ImmediateScroll.m_JumpScrollActive);
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(JumpScroll, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(JumpScroll, ESettingsResourcePriority::BACKGROUND, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(JumpScroll, ESettingsResourcePriority::VISIBLE, 4, 1), 1);
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(JumpScroll, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceJobs, AdaptiveBudgetGrowsOnStableFrames)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = 120;
	Input.m_VisibleWaiting = 0;
	Input.m_WindowActive = true;

	const SSettingsAdaptiveBudgetOutput First = SettingsAdaptiveBudgetStep(Input, State);
	const SSettingsAdaptiveBudgetOutput Second = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_EQ(First.m_Mode, ESettingsAdaptiveBudgetMode::IDLE);
	EXPECT_EQ(Second.m_Reason, ESettingsAdaptiveBudgetReason::PROGRESS);
	EXPECT_GE(Second.m_BackgroundTokens, First.m_BackgroundTokens);
	EXPECT_GT(Second.m_TextPrebuildTokens, 0);
	EXPECT_GT(Second.m_DemoMetadataTokens, 0);
}

TEST(SettingsResourceJobs, AdaptiveBudgetCutsBackgroundOnFramePressure)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Stable;
	Stable.m_FrameMsAverage = 4.0f;
	Stable.m_FrameMsP95 = 5.0f;
	Stable.m_TargetFrameMs = 8.333f;
	Stable.m_BackgroundBacklog = 120;
	Stable.m_WindowActive = true;
	SettingsAdaptiveBudgetStep(Stable, State);
	SettingsAdaptiveBudgetStep(Stable, State);

	SSettingsAdaptiveBudgetInput Pressure = Stable;
	Pressure.m_FrameMsAverage = 14.0f;
	Pressure.m_FrameMsP95 = 20.0f;
	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Pressure, State);

	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::FRAME_PRESSURE);
	EXPECT_EQ(Output.m_Reason, ESettingsAdaptiveBudgetReason::FRAME_PRESSURE);
	EXPECT_EQ(Output.m_BackgroundTokens, 0);
	EXPECT_LE(Output.m_PrefetchTokens, 1);
	EXPECT_GE(Output.m_VisibleTokens, 1);
}

TEST(SettingsResourceJobs, AdaptiveBudgetKeepsVisibleTokensDuringScroll)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 5.0f;
	Input.m_FrameMsP95 = 6.0f;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = 120;
	Input.m_VisibleWaiting = 12;
	Input.m_ScrollActive = true;
	Input.m_JumpScrollActive = true;
	Input.m_WindowActive = true;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE);
	EXPECT_EQ(Output.m_BackgroundTokens, 0);
	EXPECT_EQ(Output.m_PrefetchTokens, 0);
	EXPECT_GE(Output.m_VisibleTokens, 1);
	EXPECT_GE(Output.m_GpuUploadTokens, 1);
}

TEST(SettingsResourceJobs, AdaptiveTextBudgetKeepsLowHardCapWhileScrolling)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_ScrollActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextScrollHardCap = 2;
	Input.m_TextIdleHardCap = 64;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE);
	EXPECT_LE(Output.m_TextContainerTokens, 2);
	EXPECT_LE(Output.m_GlyphRasterizeTokens, 1);
	EXPECT_LE(Output.m_GlyphUploadTokens, 1);
}

TEST(SettingsResourceJobs, AdaptiveTextBudgetCanGrowBeyondSixteenOnStableHighHeadroomFrames)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 3.0f;
	Input.m_FrameMsP95 = 4.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextIdleHardCap = 64;
	Input.m_TextScrollHardCap = 2;

	SSettingsAdaptiveBudgetOutput Output;
	for(int i = 0; i < 40; ++i)
		Output = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_GT(Output.m_TextContainerTokens, 16);
	EXPECT_LE(Output.m_TextContainerTokens, 64);
}

TEST(SettingsResourceJobs, AdaptiveTextBudgetShrinksWhenRecentTextWorkIsExpensive)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextIdleHardCap = 64;
	Input.m_TextContainerCreateMsEwma = 3.0f;
	Input.m_GlyphUploadMsEwma = 2.0f;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_Reason, ESettingsAdaptiveBudgetReason::FRAME_PRESSURE);
	EXPECT_LE(Output.m_TextContainerTokens, 2);
}

TEST(SettingsResourceJobs, TeeSkinBackgroundRequestBudgetOnlyRunsOnIdleFrames)
{
	const SSettingsResourceFrameContext Idle = {false, false, 0};
	const SSettingsResourceFrameContext Scrolling = {true, false, 0};
	const SSettingsResourceFrameContext Recovering = {false, false, 2};

	EXPECT_GT(SettingsSkinBackgroundRequestFrameBudget(Idle, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Scrolling, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Recovering, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Idle, false), 0);
}

TEST(SettingsResourceJobs, TeeSkinBackgroundDrainRaisesIdleThroughputBudgets)
{
	SSettingsResourceFrameContext IdleSettled = {false, false, 0};
	IdleSettled.m_HighPrioritySettled = true;
	SSettingsResourceFrameContext RecoveringSettled = {false, false, 2};
	RecoveringSettled.m_HighPrioritySettled = true;
	SSettingsResourceFrameContext ScrollingSettled = {true, false, 0};
	ScrollingSettled.m_HighPrioritySettled = true;

	EXPECT_TRUE(SettingsSkinBackgroundDrainActive(IdleSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(RecoveringSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(ScrollingSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(IdleSettled, false));

	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(IdleSettled, true), 8);
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(IdleSettled, true, 64), 256);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(IdleSettled, true, 64), 256);
	EXPECT_EQ(SettingsSkinSourceCountFuseLimit(IdleSettled, true, 64), 128);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestsStayBlockedThroughScrollCooldownAndRecovery)
{
	int CooldownFrames = 0;
	int RecoveryFrames = 0;

	CooldownFrames = SettingsScrollInteractionCooldown(true, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(true, 0, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(true || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	int PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_GT(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestBudgetTracksRealInflightHeadroom)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 40;
	Input.m_Loading = 60;
	Input.m_BackgroundRequested = 0;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 2;
	Input.m_RecentAdmittedDelta = 2;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RealInflight, 100);
	EXPECT_EQ(Decision.m_RequestBudget, 20);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::NONE);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestBudgetPausesWhenHeadroomIsReservedForVisible)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 64;
	Input.m_Loading = 56;
	Input.m_BackgroundRequested = 0;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 1;
	Input.m_RecentAdmittedDelta = 1;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RealInflight, 120);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::VISIBLE_RESERVE);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestBudgetSlowsStalledProducerWithLargeBacklog)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 4;
	Input.m_Loading = 4;
	Input.m_BackgroundRequested = 160;
	Input.m_CountFuseLimit = 64;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 0;
	Input.m_RecentAdmittedDelta = 0;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestBudgetAllowsAdmittedProgressBelowHardCap)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 4;
	Input.m_Loading = 4;
	Input.m_BackgroundRequested = 160;
	Input.m_CountFuseLimit = 64;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 0;
	Input.m_RecentAdmittedDelta = 3;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 24);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::NONE);
}

TEST(SettingsResourceJobs, TeeBackgroundRequestBudgetCapsHealthyBacklogBeforeQueueInflates)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 8;
	Input.m_Loading = 8;
	Input.m_BackgroundRequested = 256;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 0;
	Input.m_RecentLoadedDelta = 4;
	Input.m_RecentAdmittedDelta = 4;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE);
}

TEST(SettingsResourceJobs, TeeBackgroundWindowGrowsSlowlyWhenDrainIsHealthy)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 64;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 3;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = true;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 65);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::INCREASE);
}

TEST(SettingsResourceJobs, TeeBackgroundWindowShrinksFastWhenVisibleNeedsHeadroom)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 96;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 2;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = true;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 48);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobs, TeeBackgroundWindowShrinksWhenAdmittedWorkStopsCompleting)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 80;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 1;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = true;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 40);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobs, TeeBackgroundWindowShrinksWhenDecodeJobsSaturate)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 72;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 2;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = true;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 36);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobs, TeeOffscreenLifecycleWaitsForNonemptyValidSettledList)
{
	bool DrainSessionActive = true;
	const auto Advance = [&](int TotalEntries, int ValidEntries, int SettledEntries, bool PerfDebugEnabled) {
		const auto Decision = SettingsTeeOffscreenLifecycleDecision({
			TotalEntries,
			ValidEntries,
			SettledEntries,
			DrainSessionActive,
			PerfDebugEnabled,
		});
		if(Decision.m_CompleteDrainSession)
			DrainSessionActive = false;
		return Decision;
	};

	const auto Empty = Advance(0, 0, 0, false);
	EXPECT_FALSE(Empty.m_FullListReady);
	EXPECT_FALSE(Empty.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto MissingContainer = Advance(3, 2, 2, false);
	EXPECT_FALSE(MissingContainer.m_FullListReady);
	EXPECT_FALSE(MissingContainer.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto Loading = Advance(3, 3, 2, false);
	EXPECT_FALSE(Loading.m_FullListReady);
	EXPECT_FALSE(Loading.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto Complete = Advance(3, 3, 3, false);
	EXPECT_TRUE(Complete.m_FullListReady);
	EXPECT_TRUE(Complete.m_CompleteDrainSession);
	EXPECT_FALSE(Complete.m_LogCompletion);
	EXPECT_FALSE(DrainSessionActive);
}

TEST(SettingsResourceJobs, TeeOffscreenLifecycleLogsCompletionOnlyForActivePerfSession)
{
	const auto Inactive = SettingsTeeOffscreenLifecycleDecision({3, 3, 3, false, true});
	EXPECT_TRUE(Inactive.m_FullListReady);
	EXPECT_FALSE(Inactive.m_CompleteDrainSession);
	EXPECT_FALSE(Inactive.m_LogCompletion);

	const auto Active = SettingsTeeOffscreenLifecycleDecision({3, 3, 3, true, true});
	EXPECT_TRUE(Active.m_FullListReady);
	EXPECT_TRUE(Active.m_CompleteDrainSession);
	EXPECT_TRUE(Active.m_LogCompletion);
}

TEST(SettingsResourceJobs, SourceBytesEstimateExceedsZeroForLoadedSkin)
{
	EXPECT_GT(SettingsSkinSourceBytesEstimate(256, 128, 2), 0u);
}

TEST(SettingsResourceJobs, BytesBudgetCanTriggerReclaimBeforeCountFuse)
{
	EXPECT_TRUE(SettingsSkinResidencyShouldReclaim(true, false));
}

TEST(SettingsResourceJobs, CountFuseStillAppliesWhenBytesBudgetIsWithinLimit)
{
	EXPECT_TRUE(SettingsSkinResidencyShouldReclaim(false, true));
}

TEST(SettingsResourceJobs, WorkshopInstalledAssetCanUseWorkshopCatalogAndLocalBytes)
{
	EXPECT_STREQ(SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource::WORKSHOP_CACHE), "workshop-cache");
	EXPECT_STREQ(SettingsWorkshopBytesSourceName(ESettingsWorkshopBytesSource::LOCAL_INSTALL), "local-install");
}

TEST(SettingsResourceJobs, CountryFlagPlanHandlesEmptyInput)
{
	const std::vector<int> vPlan = BuildSettingsCountryFlagWarmupPlan({});
	EXPECT_TRUE(vPlan.empty());
}

TEST(SettingsWarmup, MenuTextPrebuildDoesNotRenderPages)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/menus.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string UiHeader = ReadTestSourceFile("src/game/client/ui.h");
	const std::string UiSource = ReadTestSourceFile("src/game/client/ui.cpp");
	const std::string SettingsCard = ReadTestSourceFile("src/game/client/QmUi/SettingsCard.cpp");
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_TRUE(Menus.find("PrebuildSettingsMenuTextPool(int Budget") != std::string::npos);
	EXPECT_TRUE(Header.find("PrebuildSettingsMenuTextPool(int Budget, const char *pScopeOverride") != std::string::npos);
	EXPECT_TRUE(Header.find("PrebuildSettingsMenuTextPool(Budget, \"target_settings\", \"settings_open\")") != std::string::npos);
	EXPECT_TRUE(Menus.find("event=settings_text_prebuild") != std::string::npos);
	EXPECT_TRUE(Menus.find("built=%d reused=%d remaining=%d budget=%d phase=%s scope=%s operation=%s") != std::string::npos);
	EXPECT_TRUE(Menus.find("phase=before_target") != std::string::npos);
	EXPECT_TRUE(Menus.find("scope=target_settings") != std::string::npos);
	EXPECT_EQ(Header.find("PrebuildVisibleSettingsTextPool"), std::string::npos);
	EXPECT_EQ(Menus.find("void CMenus::PrebuildVisibleSettingsTextPool"), std::string::npos);
	EXPECT_EQ(Menus.find("PrebuildVisibleSettingsTextPool(ContentView"), std::string::npos);
	EXPECT_EQ(Menus.find("RenderSettingsTClient(MainView, true)"), std::string::npos);
	EXPECT_EQ(Menus.find("RenderSettingsQmClient(MainView, false, true)"), std::string::npos);
	EXPECT_NE(UiHeader.find("void BeginRenderOnly();"), std::string::npos);
	EXPECT_NE(UiHeader.find("void EndRenderOnly();"), std::string::npos);
	EXPECT_NE(UiSource.find("if(m_RenderOnlyDepth++ == 0)"), std::string::npos);
	EXPECT_NE(UiSource.find("RenderOnlyClip.x += RenderOnlyClip.w;"), std::string::npos);
	EXPECT_NE(UiSource.find("RenderOnlyClip.y += RenderOnlyClip.h;"), std::string::npos);
	EXPECT_NE(UiSource.find("ClipEnable(&RenderOnlyClip);"), std::string::npos);
	EXPECT_NE(UiSource.find("if(--m_RenderOnlyDepth == 0)"), std::string::npos);
	EXPECT_NE(UiSource.find("ClipDisable();"), std::string::npos);
	EXPECT_NE(SettingsCard.find("SettingsCardShouldDrawChrome(Ctx.m_pUi != nullptr && Ctx.m_pUi->RenderOnly())"), std::string::npos);
	EXPECT_NE(SettingsCard.find("DrawRoundedSurface(Ctx, ChromeRect, Surface, Border, CardRadius"), std::string::npos);
	EXPECT_EQ(SettingsCard.find("ChromeRect.Draw(Surface, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_EQ(SettingsCard.find("ResolveSettingsCardBorderRingClipRects"), std::string::npos);
	EXPECT_EQ(SettingsCard.find("InnerSurface.Margin(BorderWidth, &InnerSurface);"), std::string::npos);
	EXPECT_EQ(SettingsCard.find("ChromeRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_EQ(SettingsCard.find("BorderRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsCardDeckForRenderPass()"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Settings.find("RenderOnly ? nullptr : &s_GeneralSettingsScrollRegion"), std::string::npos);
	EXPECT_NE(Settings.find("RenderOnly ? nullptr : &s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab]"), std::string::npos);
	EXPECT_NE(TClient.find("PrewarmOnly"), std::string::npos);
	EXPECT_NE(QmClient.find("PrewarmOnly"), std::string::npos);
}

TEST(SettingsWarmup, SettingsPrewarmDefaultDisabled)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSettingsPrewarm, qm_settings_prewarm, 0, 0, 1,"), std::string::npos);
	EXPECT_EQ(Config.find("MACRO_CONFIG_INT(QmSettingsPrewarm, qm_settings_prewarm, 1, 0, 1,"), std::string::npos);
}

TEST(SettingsWarmup, SettingsPageCachePrewarmRespectsDisabledConfigAtUnifiedEntry)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const size_t FunctionStart = Menus.find("void CMenus::PrewarmSettingsPages()");
	const size_t FunctionEnd = Menus.find("void CMenus::ConchainBackgroundEntities", FunctionStart);
	ASSERT_NE(FunctionStart, std::string::npos);
	ASSERT_NE(FunctionEnd, std::string::npos);
	const std::string Body = Menus.substr(FunctionStart, FunctionEnd - FunctionStart);

	const size_t DisabledGuard = Body.find("if(g_Config.m_QmSettingsPrewarm == 0)");
	const size_t BindCacheAccess = Body.find("EnsureSettingsBindCache();");
	const size_t SkinListAccess = Body.find("GameClient()->m_Skins.SkinList(0);");
	ASSERT_NE(DisabledGuard, std::string::npos);
	ASSERT_NE(BindCacheAccess, std::string::npos);
	ASSERT_NE(SkinListAccess, std::string::npos);
	EXPECT_LT(DisabledGuard, BindCacheAccess);
	EXPECT_LT(DisabledGuard, SkinListAccess);
	EXPECT_NE(Menus.find("void CMenus::EnsureSettingsBindCache()"), std::string::npos);
	EXPECT_NE(TClient.find("if(!ReadOnly)\n\t\tEnsureSettingsBindCache();"), std::string::npos);
	EXPECT_NE(QmClient.find("if(!PrewarmOnly)\n\t\tEnsureSettingsBindCache();"), std::string::npos);
}

TEST(SettingsWarmup, TClientCardMeasurementCachesUntilRelevantConfigChanges)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	EXPECT_NE(TClient.find("HashTClientSettingsCardLayout(s_aDeckCardSpecs[Index].first)"), std::string::npos);
	EXPECT_EQ(TClient.find("Definition.m_MeasureRevision = HashTClientSettingsConfig();"), std::string::npos);
	EXPECT_EQ(TClient.find("Definition.m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_NE(TClient.find("HashValueFnv1a64(Hash, QmFastInputNormalizedMode(g_Config.m_QmFastInputMode))"), std::string::npos);
	EXPECT_NE(TClient.find("HashValueFnv1a64(Hash, g_Config.m_TcWarListIndicator)"), std::string::npos);
	EXPECT_NE(TClient.find("HashValueFnv1a64(Hash, g_Config.m_TcWarListIndicatorColors)"), std::string::npos);
	EXPECT_NE(TClient.find("for(int RowIndex = 0; RowIndex < (UiMode == 3 ? 4 : 1); ++RowIndex)"), std::string::npos);
	EXPECT_NE(TClient.find("Rows.Next();"), std::string::npos);
	const size_t LayoutHashStart = TClient.find("uint64_t HashTClientSettingsCardLayout(");
	const size_t RuntimeKeyStart = TClient.find("SSettingsSectionCacheRuntimeKey MakeSettingsSectionRuntimeKey(", LayoutHashStart);
	ASSERT_NE(LayoutHashStart, std::string::npos);
	ASSERT_NE(RuntimeKeyStart, std::string::npos);
	const std::string LayoutHash = TClient.substr(LayoutHashStart, RuntimeKeyStart - LayoutHashStart);
	EXPECT_EQ(LayoutHash.find("m_TcNotifyWhenLastColor"), std::string::npos);
}

TEST(SettingsWarmup, RemainingSettingsPagesUseResponsiveContentMetrics)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Controls = ReadTestSourceFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Assets = ReadTestSourceFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(TClient.find("const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(ContentWidth);"), std::string::npos);
	EXPECT_NE(TClient.find("FontSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_NE(TClient.find("LineSize = Metrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(TClient.find("MarginSmall = Metrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Controls.find("ApplyControlsContentMetrics(MainView.w);"), std::string::npos);
	EXPECT_NE(Controls.find("BUTTON_HEIGHT = Metrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(Controls.find("BUTTON_SPACING = Metrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Assets.find("const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(MainView.w);"), std::string::npos);
	EXPECT_NE(Assets.find("Localize(\"Loading assets...\"), ContentMetrics.m_BodySize"), std::string::npos);
	EXPECT_NE(Assets.find("Localize(\"No assets\"), ContentMetrics.m_BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("pCheckBoxValue, float LineHeight, float LineSpacing, float BodySize, float ButtonHeight)"), std::string::npos);
	EXPECT_NE(Settings.find("const float ResolvedButtonHeight = ButtonHeight > 0.0f ? ButtonHeight : LineHeight;"), std::string::npos);
	EXPECT_EQ(Settings.find("Localize(\"Text\"), BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("DoSettingsMenuLabel(SETTINGS_APPEARANCE, Tab, Tab, pLabelTextId, &Label, pLabel, BodySize"), std::string::npos);
	EXPECT_NE(Settings.find("const float LineHeight = SoundMetrics.m_LineHeight;"), std::string::npos);
	EXPECT_NE(Settings.find("const float LineSpacing = SoundMetrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Settings.find("SoundToggleCardHeight = ToggleChromeHeight + LineHeight * ToggleRowCount"), std::string::npos);
	EXPECT_EQ(Settings.find("s_SoundToggleCardHeight"), std::string::npos);
}

TEST(SettingsWarmup, TClientSettingsRowsSeparateControlHeightFromSpacing)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	auto Section = [&](const char *pBegin, const char *pEnd) {
		const size_t Begin = TClient.find(pBegin);
		const size_t End = TClient.find(pEnd, Begin);
		EXPECT_NE(Begin, std::string::npos);
		EXPECT_NE(End, std::string::npos);
		return Begin != std::string::npos && End != std::string::npos ? TClient.substr(Begin, End - Begin) : std::string();
	};

	const std::string RowAllocator = Section("class CTClientSettingsRowAllocator", "static void ApplyTClientContentMetrics");
	EXPECT_NE(RowAllocator.find("m_Column.HSplitTop(MarginSmall, nullptr, &m_Column);"), std::string::npos);
	EXPECT_NE(RowAllocator.find("m_Column.HSplitTop(Height, &Row, &m_Column);"), std::string::npos);

	const std::string Hud = Section("float CMenus::LayoutTClientHudCacheSection", "SSettingsSection CMenus::BuildTClientThemeCacheSection");
	EXPECT_NE(Hud.find("CTClientSettingsRowAllocator Rows(CurrentColumn);"), std::string::npos);
	EXPECT_NE(Hud.find("DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud"), std::string::npos);
	EXPECT_NE(Hud.find("&Row, LineSize);"), std::string::npos);
	EXPECT_EQ(Hud.find("LineSize * 3.0f"), std::string::npos);

	const std::string Nameplates = Section("auto LayoutVisualNameplateSection", "auto LayoutVisualEffectsSection");
	EXPECT_NE(Nameplates.find("const int NameplateRowCount = 7 + (g_Config.m_TcWhiteFeet ? 1 : 0);"), std::string::npos);
	EXPECT_NE(Nameplates.find("TClientSettingsRowsHeight(NameplateRowCount)"), std::string::npos);
	EXPECT_NE(Nameplates.find("CUIRect FeetBox;\n\t\t\tif(g_Config.m_TcWhiteFeet)\n\t\t\t\tFeetBox = Rows.Next();"), std::string::npos);
	EXPECT_EQ(Nameplates.find("LineSize * 7.0f"), std::string::npos);
	EXPECT_EQ(Nameplates.find("&CurrentColumn, LineSize"), std::string::npos);

	const std::string Input = Section("auto LayoutInputSection", "auto LayoutAntiLatencyToolsSection");
	EXPECT_NE(Input.find("CTClientSettingsRowAllocator Rows(CurrentColumn);"), std::string::npos);
	EXPECT_NE(Input.find("for(int RowIndex = 0; RowIndex < (UiMode == 3 ? 4 : 1); ++RowIndex)"), std::string::npos);
	EXPECT_NE(Input.find("DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming"), std::string::npos);
	EXPECT_EQ(Input.find("&CurrentColumn, LineSize"), std::string::npos);

	const std::string FinishName = Section("auto LayoutFinishNameSection", "std::vector<SSettingsSection> vRightSections");
	EXPECT_NE(FinishName.find("CUIRect ToggleRow = Rows.Next();"), std::string::npos);
	EXPECT_NE(FinishName.find("FinishNameBox = Rows.Next();"), std::string::npos);
	EXPECT_EQ(FinishName.find("LineSize + MarginExtraSmall"), std::string::npos);
}

TEST(SettingsWarmup, ControlsCardMeasurementsAvoidIdleBindingRescan)
{
	const std::string Controls = ReadTestSourceFile("src/game/client/components/menus_settings_controls.cpp");
	EXPECT_EQ(Controls.find("Definition.m_MeasureEachFrame = true;"), std::string::npos);
	const size_t ControllerCard = Controls.find("AddCard(vCards, \"deck:controls-controller\"");
	ASSERT_NE(ControllerCard, std::string::npos);
	const size_t ControllerCardEnd = Controls.find(";", ControllerCard);
	ASSERT_NE(ControllerCardEnd, std::string::npos);
	EXPECT_EQ(Controls.substr(ControllerCard, ControllerCardEnd - ControllerCard).find("true"), std::string::npos);
	const size_t RevisionStart = Controls.find("uint64_t CardLayoutRevision");
	const size_t DefinitionsStart = Controls.find("const auto BuildDefinitions", RevisionStart);
	ASSERT_NE(RevisionStart, std::string::npos);
	ASSERT_NE(DefinitionsStart, std::string::npos);
	const std::string RevisionBody = Controls.substr(RevisionStart, DefinitionsStart - RevisionStart);
	EXPECT_EQ(RevisionBody.find("MeasureSettingsMouseHeight()"), std::string::npos);
	EXPECT_EQ(RevisionBody.find("MeasureSettingsJoystickHeight()"), std::string::npos);
	EXPECT_NE(RevisionBody.find("g_Config.m_InpControllerEnable"), std::string::npos);
	EXPECT_NE(RevisionBody.find("pActiveJoystick->GetNumAxes()"), std::string::npos);
	EXPECT_NE(Controls.find("Definition.m_MeasureRevision = MeasureRevision;"), std::string::npos);
	EXPECT_NE(Controls.find("m_BindLayoutRevision"), std::string::npos);
	EXPECT_NE(Controls.find("++m_BindLayoutRevision;"), std::string::npos);
	EXPECT_NE(Controls.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Controls.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Controls.find("ReadOnly ? nullptr : &m_SettingsScrollRegion"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Controls.find("else if(!ReadOnly && !m_vSearchMatches.empty()"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && m_SearchMatchReveal"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
}

TEST(SettingsWarmup, SettingsCardsAvoidIdlePerFrameMeasurement)
{
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string QmClient = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_EQ(Settings.find("Definition.m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_EQ(QmClient.find("m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_EQ(TClient.find("m_MeasureEachFrame = true;"), std::string::npos);
	EXPECT_NE(Settings.find("Definition.m_MeasureRevision = static_cast<uint64_t>"), std::string::npos);
	EXPECT_EQ(QmClient.find("Definition.m_MeasureRevision = static_cast<uint64_t>"), std::string::npos);
	EXPECT_NE(QmClient.find("Definition.m_MeasureRevision = MeasureContentRevision(Id);"), std::string::npos);
}

TEST(SettingsWarmup, TeeOffscreenDrainRequiresExplicitPrewarm)
{
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t AdvanceStart = Settings.find("const auto AdvanceListOffscreen = [this, QueueDummy]() {");
	ASSERT_NE(AdvanceStart, std::string::npos);
	const size_t SkinListLookup = Settings.find("GameClient()->m_Skins.SkinList(QueueDummy)", AdvanceStart);
	ASSERT_NE(SkinListLookup, std::string::npos);
	const size_t SkinListAccess = Settings.find("SkinList.Skins()", AdvanceStart);
	ASSERT_NE(SkinListAccess, std::string::npos);
	const size_t PrewarmGuard = Settings.find("g_Config.m_QmSettingsPrewarm == 0", AdvanceStart);
	ASSERT_NE(PrewarmGuard, std::string::npos);
	EXPECT_LT(PrewarmGuard, SkinListLookup);
	EXPECT_LT(PrewarmGuard, SkinListAccess);
	const size_t ListCard = Settings.find("AddCard(ListSpec", AdvanceStart);
	ASSERT_NE(ListCard, std::string::npos);
	const size_t OffscreenCall = Settings.find("AdvanceListOffscreen();", ListCard);
	ASSERT_NE(OffscreenCall, std::string::npos);
	const size_t CallGuard = Settings.rfind("else if(g_Config.m_QmSettingsPrewarm != 0)", OffscreenCall);
	ASSERT_NE(CallGuard, std::string::npos);
	EXPECT_GT(CallGuard, ListCard);
	EXPECT_NE(Settings.find("m_BackgroundRequestScanComplete", AdvanceStart), std::string::npos);
	EXPECT_NE(Settings.find("m_BackgroundRequestScanRevision != SkinList.Revision()", AdvanceStart), std::string::npos);
	EXPECT_NE(Settings.find("g_Config.m_QmSettingsPrewarm != 0 && VisibleSourceSettled"), std::string::npos);
	EXPECT_NE(Settings.find("g_Config.m_QmSettingsPrewarm != 0 && m_SettingsHighPrioritySettled"), std::string::npos);
}

TEST(SettingsWarmup, CardHeightCacheInvalidatesWhenViewportHeightChanges)
{
	const std::string Deck = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_NE(Deck.find("m_LastViewportHeight"), std::string::npos);
	EXPECT_NE(Deck.find("std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f)"), std::string::npos);
}

TEST(SettingsWarmup, TextPlanCollectionUsesPrewarmOnlyRenderers)
{
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string QmClient = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Settings.find("RenderSettingsTClient(ContentView, CollectingMenuTextPlan);"), std::string::npos);
	EXPECT_NE(Settings.find("RenderSettingsQmClient(ContentView, false, CollectingMenuTextPlan);"), std::string::npos);
	EXPECT_NE(QmClient.find("Ctx.m_pAnim = PrewarmOnly ? nullptr"), std::string::npos);
	EXPECT_NE(QmClient.find("if(!PrewarmOnly)"), std::string::npos);
	EXPECT_EQ(QmClient.find("m_SettingsPageSwitchActive = m_SettingsPageSwitchActive || TabTransitionActive;"), std::string::npos);
	EXPECT_NE(QmClient.find("QmPerfLogPayload(\"perf/qmclient\", aPayload, Client(), CurrentQmUiPerfPage());"), std::string::npos);
}

TEST(SettingsWarmup, MenuTextPrebuildLogsRemainingMissingPlanItems)
{
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const size_t BodyStart = Menus.find("void CMenus::PrebuildSettingsMenuTextPool(int Budget, const char *pScopeOverride, const char *pOperationOverride)");
	ASSERT_NE(BodyStart, std::string::npos);
	const size_t BodyEnd = Menus.find("int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)", BodyStart);
	ASSERT_NE(BodyEnd, std::string::npos);
	const std::string Body = Menus.substr(BodyStart, BodyEnd - BodyStart);

	EXPECT_NE(Body.find("PrebuildSettingsTextPoolForLoading(Budget, pOperationOverride);"), std::string::npos);
	EXPECT_NE(Body.find("m_SettingsMenuTextLastPrebuildStats"), std::string::npos);
	EXPECT_NE(Body.find("m_SettingsMenuTextPlanCursor"), std::string::npos);
	EXPECT_NE(Body.find("const int RemainingMissing = m_SettingsMenuTextLastPrebuildStats.m_Remaining;"), std::string::npos);
	EXPECT_EQ(Body.find("const int RemainingMissing = CountMissingSettingsMenuTextPlanItems();"), std::string::npos);
	EXPECT_NE(Body.find("Built, Reused, RemainingMissing, Budget"), std::string::npos);
	EXPECT_EQ(Body.find("Built, Reused, RemainingBudget, Budget"), std::string::npos);
}

TEST(SettingsWarmup, AssetsUploadPerfLogsGpuUploadBudgetNotByteBudget)
{
	const std::string Assets = ReadTestSourceFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_EQ(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", (int)UploadedBytesThisFrame, (int)MaxPreviewUploadBytesPerFrame"), std::string::npos);
	EXPECT_EQ(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", (int)WorkshopThumbUploadedBytesThisFrame, (int)MaxWorkshopThumbUploadBytesPerFrame"), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", UploadedPreviewsThisFrame, MaxPreviewUploadsPerFrame"), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", WorkshopGpuUploadsThisFrame, MaxWorkshopThumbUploadsPerFrame"), std::string::npos);
}

TEST(SettingsWarmup, TClientVisualSettingsUseStableTextIdsForPrebuildCoverage)
{
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(TClient.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-visual-font-cursor-title\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-hammer-mode\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-cursor-scale\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-wheel-animate-ms\""), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-wheel-animate-off\""), std::string::npos);
}

TEST(SettingsWarmup, TClientSettingsUseTwoLevelFontScale)
{
	const std::string MenusHeader = ReadTestSourceFile("src/game/client/components/menus.h");
	const std::string TClient = ReadTestSourceFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string Ui = ReadTestSourceFile("src/game/client/ui.cpp");
	const std::string KeyBinder = ReadTestSourceFile("src/game/client/components/key_binder.cpp");

	EXPECT_NE(MenusHeader.find("static constexpr float TCLIENT_SETTINGS_BODY_FONT_SIZE = 11.2f;"), std::string::npos);
	EXPECT_NE(TClient.find("constexpr float TCLIENT_BODY_FONT_SIZE = CMenus::TCLIENT_SETTINGS_BODY_FONT_SIZE;"), std::string::npos);
	EXPECT_NE(TClient.find("constexpr float TCLIENT_HEADLINE_FONT_SIZE = 20.0f;"), std::string::npos);
	EXPECT_NE(TClient.find("Props.m_MinimumFontSize = FontSize;"), std::string::npos);
	EXPECT_NE(TClient.find("Props.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_NE(TClient.find("EditBoxFontSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_NE(TClient.find("ColorPickerLabelSize = Metrics.m_BodySize;"), std::string::npos);
	EXPECT_EQ(TClient.find("const float EditBoxFontSize = 12.0f;"), std::string::npos);
	EXPECT_EQ(TClient.find("const float ColorPickerLabelSize = 13.0f;"), std::string::npos);
	EXPECT_EQ(TClient.find("Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : \"\", 11.0f"), std::string::npos);
	EXPECT_EQ(TClient.find("Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f)"), std::string::npos);
	EXPECT_EQ(TClient.find("Height / LineSize * FontSize"), std::string::npos);
	EXPECT_NE(TClient.find("Ui()->DoLabel(&Label, Profile.m_Name, ProfileMetrics.m_BodySize, TEXTALIGN_ML);"), std::string::npos);
	EXPECT_NE(TClient.find("Ui()->DoLabel(&Label, Profile.m_Clan, ProfileMetrics.m_BodySize, TEXTALIGN_ML);"), std::string::npos);
	EXPECT_NE(TClient.find("DoLine_ColorPicker(&ResetId, CurrentSettingsContentMetrics(), &ColorRect, \"\", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);"), std::string::npos);
	EXPECT_NE(TClient.find("DoTClientSettingsButton_Menu(&ResetBtn, \"tclient-config-reset\", Localize(\"Reset\")"), std::string::npos);
	EXPECT_NE(TClient.find("DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false, TCLIENT_BODY_FONT_SIZE)"), std::string::npos);
	EXPECT_NE(TClient.find("DoSettingsDropDown(&Button, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState)"), std::string::npos);
	EXPECT_NE(TClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &Button, Corners"), std::string::npos);
	EXPECT_NE(TClient.find("ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, EntriesSearch, FontSize"), std::string::npos);

	EXPECT_NE(Menus.find("const float BodySize = RequestedFontSize > 0.0f ? RequestedFontSize : CurrentSettingsContentMetrics().m_BodySize;"), std::string::npos);
	EXPECT_NE(Menus.find("const bool FixedFontSize = LabelFontSize > 0.0f;"), std::string::npos);
	EXPECT_NE(Menus.find("const float FontSize = LabelFontSize > 0.0f ? LabelFontSize : Box.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(Menus.find("const float ResolvedBodySize = BodySize > 0.0f ? BodySize : CurrentSettingsContentMetrics().m_BodySize;"), std::string::npos);
	EXPECT_EQ(Menus.find("Page == SETTINGS_TCLIENT ? 14.0f"), std::string::npos);
	EXPECT_NE(Menus.find("Props.m_MinimumFontSize = FixedFontSize ? FontSize : FontSize * 0.7f;"), std::string::npos);
	EXPECT_NE(Menus.find("Props.m_EllipsisAtEnd = FixedFontSize;"), std::string::npos);
	EXPECT_NE(Menus.find("ResolveSettingsCheckboxFontSize(BodySize, RequestedFontSize, pRect->h, Box.h, CUi::ms_FontmodHeight)"), std::string::npos);
	EXPECT_NE(Menus.find("return DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize);"), std::string::npos);
	EXPECT_NE(KeyBinder.find("Props.m_MinimumFontSize = FontSize;"), std::string::npos);
	EXPECT_NE(KeyBinder.find("Props.m_EllipsisAtEnd = true;"), std::string::npos);

	EXPECT_NE(Ui.find("Props.m_FontSize = ResolvedFontSize;"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelFlags = Flags;"), std::string::npos);
	EXPECT_NE(Ui.find("UIElement.Rect(0)->m_FontSize != FontSize"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelMaxWidth != LabelProps.m_MaxWidth"), std::string::npos);
	EXPECT_NE(Ui.find("RectEl.m_LabelFlags != Flags"), std::string::npos);
	EXPECT_NE(Ui.find("State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;"), std::string::npos);
}

TEST(SettingsWarmup, PassiveTooltipOnlyUiHelpersStayOutOfButtonLogic)
{
	const std::string Header = ReadTestSourceFile("src/game/client/ui.h");
	EXPECT_NE(Header.find("void RegisterPassiveHotItem(const void *pId, const CUIRect *pRect);"), std::string::npos);

	const std::string UiSource = ReadTestSourceFile("src/game/client/ui.cpp");
	const size_t HelperPos = UiSource.find("void CUi::RegisterPassiveHotItem(const void *pId, const CUIRect *pRect)");
	ASSERT_NE(HelperPos, std::string::npos);
	const size_t NextFunctionPos = UiSource.find("int CUi::DoButtonLogic", HelperPos);
	ASSERT_NE(NextFunctionPos, std::string::npos);
	const std::string HelperBody = UiSource.substr(HelperPos, NextFunctionPos - HelperPos);

	EXPECT_NE(HelperBody.find("MouseHovered(pRect)"), std::string::npos);
	EXPECT_NE(HelperBody.find("SetHotItem(pId);"), std::string::npos);
	EXPECT_EQ(HelperBody.find("SetActiveItem"), std::string::npos);
	EXPECT_EQ(HelperBody.find("MouseButton("), std::string::npos);

	const std::string MenusSettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	EXPECT_NE(MenusSettingsSource.find("Ui()->RegisterPassiveHotItem(pStatusTooltipId, &StatusIcon);"), std::string::npos);
	EXPECT_NE(MenusSettingsSource.find("Ui()->RegisterPassiveHotItem(&s_HookCollToolTip, &LeftView);"), std::string::npos);
	EXPECT_EQ(MenusSettingsSource.find("Ui()->DoButtonLogic(pStatusTooltipId, 0, &StatusIcon, BUTTONFLAG_NONE);"), std::string::npos);
	EXPECT_EQ(MenusSettingsSource.find("Ui()->DoButtonLogic(&s_HookCollToolTip, 0, &LeftView, BUTTONFLAG_NONE);"), std::string::npos);

	const std::string MenusQmClientSource = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(MenusQmClientSource.find("RegisterPassiveHotItem("), std::string::npos);
}

TEST(SettingsPlayerInfo, ChangesAreDeferredUntilMenuCloses)
{
	const std::string SettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const size_t MarkStart = SettingsSource.find("void CMenus::SetNeedSendInfo(bool Dummy)");
	ASSERT_NE(MarkStart, std::string::npos);
	const size_t MarkEnd = SettingsSource.find("CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection", MarkStart);
	ASSERT_NE(MarkEnd, std::string::npos);
	const std::string MarkBody = SettingsSource.substr(MarkStart, MarkEnd - MarkStart);

	EXPECT_NE(MarkBody.find("bool &NeedSendInfo = Dummy ? m_NeedSendDummyinfo : m_NeedSendinfo;"), std::string::npos);
	EXPECT_NE(MarkBody.find("NeedSendInfo = true;"), std::string::npos);
	EXPECT_EQ(MarkBody.find("GameClient()->SendInfo"), std::string::npos);
	EXPECT_EQ(MarkBody.find("GameClient()->SendDummyInfo"), std::string::npos);

	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const size_t CloseStart = MenusSource.find("void CMenus::SetActive(bool Active)");
	ASSERT_NE(CloseStart, std::string::npos);
	const size_t CloseEnd = MenusSource.find("void CMenus::MarkMenuInteraction", CloseStart);
	ASSERT_NE(CloseEnd, std::string::npos);
	const std::string CloseBody = MenusSource.substr(CloseStart, CloseEnd - CloseStart);

	EXPECT_NE(CloseBody.find("if(!m_MenuActive)"), std::string::npos);
	EXPECT_NE(CloseBody.find("if(m_NeedSendinfo)"), std::string::npos);
	EXPECT_NE(CloseBody.find("GameClient()->SendInfo(false);"), std::string::npos);
	EXPECT_NE(CloseBody.find("if(m_NeedSendDummyinfo)"), std::string::npos);
	EXPECT_NE(CloseBody.find("GameClient()->SendDummyInfo(false);"), std::string::npos);
}
