// 设置预热源码合同：加载阶段预热编排。运行时行为保留在 settings_warmup_test.cpp。
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>

TEST(SettingsWarmupLoadingContract, TeePageWarmupStartsSkinSourcePrewarm)
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

TEST(SettingsWarmupLoadingContract, SettingsFrameBudgetResetsBeforeUpdatePhaseConsumers)
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

TEST(SettingsWarmupLoadingContract, LoadingPrewarmDoesNotPumpResourceWork)
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

TEST(SettingsWarmupLoadingContract, SettingsPrewarmDefaultDisabled)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSettingsPrewarm, qm_settings_prewarm, 0, 0, 1,"), std::string::npos);
	EXPECT_EQ(Config.find("MACRO_CONFIG_INT(QmSettingsPrewarm, qm_settings_prewarm, 1, 0, 1,"), std::string::npos);
}

TEST(SettingsWarmupLoadingContract, SettingsPageCachePrewarmRespectsDisabledConfigAtUnifiedEntry)
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

TEST(SettingsWarmupLoadingContract, MenuTextPrebuildDoesNotRenderPages)
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

TEST(SettingsWarmupLoadingContract, MenuTextPrebuildLogsRemainingMissingPlanItems)
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

TEST(SettingsWarmupLoadingContract, TextPlanCollectionUsesPrewarmOnlyRenderers)
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

TEST(SettingsWarmupLoadingContract, TeeOffscreenDrainRequiresExplicitPrewarm)
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

TEST(SettingsWarmupLoadingContract, CardHeightCacheInvalidatesWhenViewportHeightChanges)
{
	const std::string Deck = ReadTestSourceFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_NE(Deck.find("m_LastViewportHeight"), std::string::npos);
	EXPECT_NE(Deck.find("std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f)"), std::string::npos);
}

TEST(SettingsWarmupLoadingContract, AssetsUploadPerfLogsGpuUploadBudgetNotByteBudget)
{
	const std::string Assets = ReadTestSourceFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_EQ(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", (int)UploadedBytesThisFrame, (int)MaxPreviewUploadBytesPerFrame"), std::string::npos);
	EXPECT_EQ(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", (int)WorkshopThumbUploadedBytesThisFrame, (int)MaxWorkshopThumbUploadBytesPerFrame"), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", UploadedPreviewsThisFrame, MaxPreviewUploadsPerFrame"), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsResourcePerf(SETTINGS_ASSETS, \"upload\", WorkshopGpuUploadsThisFrame, MaxWorkshopThumbUploadsPerFrame"), std::string::npos);
}
