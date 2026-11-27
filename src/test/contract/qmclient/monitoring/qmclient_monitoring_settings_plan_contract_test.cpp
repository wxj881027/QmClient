// 请抬头享受阳光｜日子很好 我很我---------致咩子
// Qm monitoring 源码合同：设置菜单文本计划与预热调度。
// 设置界面结构/甲板合同见 qmclient_monitoring_settings_structure_contract_test.cpp；
// 性能窗口遥测合同见 qmclient_monitoring_settings_perf_contract_test.cpp。
#define CONF_TEST 1

#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringSettingsPlanContract, VisibleTextMissBuildsAndRendersInTheSameFrame)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string ElementBody = ExtractSourceFunctionBody(Menus, "CUIElement &CMenus::MenuTextElement(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const SMenuTextStyleKey &StyleKey)");
	const std::string LabelBody = ExtractSourceFunctionBody(Menus, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	ASSERT_FALSE(ElementBody.empty());
	ASSERT_FALSE(LabelBody.empty());

	EXPECT_NE(LabelBody.find("if(NeedsBuild && MenuTextCoverageActive() && Render && !Ui()->MeasurementOnly() && !Ui()->BackgroundRender())"), std::string::npos);
	EXPECT_NE(LabelBody.find("DrainMenuTextContainerBuild(Element, pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor, false, &TextContainerRecreated);"), std::string::npos);
	EXPECT_NE(LabelBody.find("Ui()->RenderLabelTextContainerAligned(*pElementRect, pRect, Align);"), std::string::npos);

	// 可见页 miss 必须插入真实池元素并在当前帧构建；旧实现返回 fallback 占位，
	// 会把整页文字推迟到后续帧。这里只裁出缓存未命中分支，不误伤计划收集分支。
	const size_t MissBranch = ElementBody.find("if(It == m_MenuTextPool.end())");
	const size_t MissBranchEnd = ElementBody.find("else if(It->second.m_Generation != m_MenuTextPoolGeneration)", MissBranch);
	ASSERT_NE(MissBranch, std::string::npos);
	ASSERT_NE(MissBranchEnd, std::string::npos);
	const std::string MissBody = ElementBody.substr(MissBranch, MissBranchEnd - MissBranch);
	EXPECT_NE(MissBody.find("m_MenuTextPool.try_emplace(std::string(Key)).first;"), std::string::npos);
	EXPECT_EQ(MissBody.find("return m_MenuTextFallbackElement;"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, LanguagePlanCollectionCoversEveryLanguageAndDoesNotPolluteVisibleCoverage)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Settings, "void CMenus::PrepareLanguagePageCache(float MainViewWidth, bool ForceComplete, const SSettingsContentMetrics *pMetrics)");
	const std::string LanguageBody = ExtractSourceFunctionBody(Settings, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	ASSERT_FALSE(PrepareBody.empty());
	ASSERT_FALSE(LanguageBody.empty());

	// 语言名是静态内容：计划收集必须无视“缓存已完成”直接遍历全部语言，
	// 同时只能产生 descriptor，不能混入 visible candidates/pool_hit。
	EXPECT_NE(PrepareBody.find("const bool CollectingPlan = m_MenuTextPlanCollecting;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("const CScopedMenuTextCoverageSuppression CoverageSuppression(this);"), std::string::npos);
	EXPECT_NE(PrepareBody.find("if(!CollectingPlan &&"), std::string::npos);
	EXPECT_NE(PrepareBody.find("if(CollectingPlan)"), std::string::npos);

	// 测量/计划收集不得推进真实滚动区，也不得消费首次滚动到当前语言。
	EXPECT_NE(LanguageBody.find("const bool MeasurementPass = Ui()->MeasurementOnly();"), std::string::npos);
	EXPECT_NE(LanguageBody.find("MeasurementPass ? s_MeasurementLanguageScrollRegion : gs_LanguageScrollRegion"), std::string::npos);
	EXPECT_NE(LanguageBody.find("if(!MeasurementPass)\n\t\ts_ScrollToInitialSelection = false;"), std::string::npos);
	EXPECT_NE(LanguageBody.find("MeasurementPass ? nullptr : &ScrollRegion"), std::string::npos);
	EXPECT_NE(LanguageBody.find("SettingsTextElement(SETTINGS_LANGUAGE, -1, LanguageRowTextId(Language))"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, TextPoolPrebuildMayMutateThePoolInsideRenderOnly)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string ElementBody = ExtractSourceFunctionBody(Menus, "CUIElement &CMenus::MenuTextElement(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const SMenuTextStyleKey &StyleKey)");
	const std::string PoolBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string ItemBody = ExtractSourceFunctionBody(Menus, "bool CMenus::PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget)");
	ASSERT_FALSE(ElementBody.empty());
	ASSERT_FALSE(PoolBody.empty());
	ASSERT_FALSE(ItemBody.empty());

	// 预建借用 RenderOnly 只抑制绘制，但必须写真实文本池；布局测量才走临时元素。
	EXPECT_NE(ElementBody.find("Ui()->MeasurementOnly() && !m_MenuTextPoolPrebuildActive"), std::string::npos);
	EXPECT_NE(PoolBody.find("const CScopedMenuTextPoolPrebuild PoolPrebuildScope(this);"), std::string::npos);
	// 容器创建失败（字形未齐）不能算 built 并跳过，否则首屏仍要现场重建。
	EXPECT_NE(ItemBody.find("if(!pRect->m_UITextContainer.Valid())"), std::string::npos);
	EXPECT_NE(ItemBody.find("event=settings_text_prebuild_fail"), std::string::npos);
	EXPECT_NE(PoolBody.find("const bool Built = PrebuildSettingsTextPlanItem(Item, RemainingBudget);\n\t\tif(!Built)\n\t\t\tbreak;"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, PlanCollectionUsesTheSameSafeAreaAsVisibleSettings)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string AdvanceBody = ExtractSourceFunctionBody(Menus, "bool CMenus::AdvanceSettingsMenuTextPlanCollection(int Budget, const char *pOperationOverride, int64_t Deadline)");
	const std::string OuterScreenBody = ExtractSourceFunctionBody(Menus, "CUIRect CMenus::MenuTextOuterScreen()");
	ASSERT_FALSE(AdvanceBody.empty());
	ASSERT_FALSE(OuterScreenBody.empty());

	EXPECT_NE(OuterScreenBody.find("Screen.Margin(ui_token::panel::MARGIN, &Screen);"), std::string::npos);
	EXPECT_NE(AdvanceBody.find("const CUIRect OuterScreen = MenuTextOuterScreen();"), std::string::npos);
	EXPECT_NE(AdvanceBody.find("CollectSettingsMenuTextPlanUnit(Unit, OuterScreen, SettingsMainView, vItems);"), std::string::npos);
	EXPECT_EQ(AdvanceBody.find("const CUIRect Screen = *Ui()->Screen();"), std::string::npos);
	EXPECT_EQ(Header.find("void EnsureSettingsMenuTextPlanReadyForVisible();"), std::string::npos);
	EXPECT_EQ(Menus.find("void CMenus::EnsureSettingsMenuTextPlanReadyForVisible()"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, PlanOperationStaysStableAcrossPrewarmContexts)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Menus, "bool CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	const std::string ReadyBody = ExtractSourceFunctionBody(Menus, "bool CMenus::SettingsMenuTextPrebuildReady(const char *pOperationOverride)");
	ASSERT_FALSE(PrepareBody.empty());
	ASSERT_FALSE(ReadyBody.empty());

	EXPECT_NE(PrepareBody.find("std::string Operation = MenuTextPlanOperation(pOperation);"), std::string::npos);
	EXPECT_NE(ReadyBody.find("const char *pOperation = MenuTextPlanOperation("), std::string::npos);
	EXPECT_NE(ReadyBody.find("m_SettingsMenuTextPlanCollectionOperation == pOperation"), std::string::npos);
	// loading/idle/tab-switch 不能把同一份设置计划拆成不同 operation 并反复清空。
	EXPECT_EQ(PrepareBody.find("std::string Operation = pOperation != nullptr ? pOperation : \"\";"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, PlanCollectionCoversSearchContributorsAndProfiles)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Menus, "bool CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	ASSERT_FALSE(PrepareBody.empty());

	EXPECT_NE(PrepareBody.find("SETTINGS_SEARCH,"), std::string::npos);
	EXPECT_NE(PrepareBody.find("SETTINGS_CONTRIBUTORS,"), std::string::npos);
	EXPECT_NE(PrepareBody.find("SETTINGS_PROFILES,"), std::string::npos);
	EXPECT_NE(PrepareBody.find("qm_tclient_cards::NUMBER_OF_TCLIENT_TABS"), std::string::npos);
	EXPECT_NE(PrepareBody.find("{SETTINGS_CONTROLS, static_cast<int>(CMenusSettingsControls::ETab::NUM)}"), std::string::npos);
	EXPECT_NE(PrepareBody.find("SETTINGS_TEE, 2, 2"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, PendingPlanPrioritizesTheCurrentVisiblePage)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Menus, "bool CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	ASSERT_FALSE(PrepareBody.empty());

	EXPECT_NE(PrepareBody.find("const bool VisibleUnitMatches = !m_vSettingsMenuTextPlanCollectionUnits.empty() &&"), std::string::npos);
	EXPECT_NE(PrepareBody.find("m_vSettingsMenuTextPlanCollectionUnits.front().m_Page == CurrentPage && m_vSettingsMenuTextPlanCollectionUnits.front().m_Tab == CurrentTab"), std::string::npos);
	EXPECT_NE(PrepareBody.find("VisibleUnitMatches)"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, TextPoolEvictionRewindsPrebuildCursor)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TrimBody = ExtractSourceFunctionBody(Menus, "int CMenus::TrimMenuTextPoolForInsert(int64_t Now)");
	ASSERT_FALSE(TrimBody.empty());

	// 墙钟/容量淘汰后，计划游标不能继续停在末尾；否则预建入口会误判 ready，
	// 下一次切页仍要同步重建整页。
	EXPECT_NE(TrimBody.find("if(Evictions > 0)"), std::string::npos);
	EXPECT_NE(TrimBody.find("m_SettingsMenuTextPlanCollectionDirty = true;"), std::string::npos);
	EXPECT_NE(TrimBody.find("SettingsTextPlanProgress().RestartConsumption();"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, IdleMenuContinuesSettingsTextPrewarm)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string UpdateBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnUpdate()");
	ASSERT_FALSE(UpdateBody.empty());

	// 加载阶段的固定步数可能提前耗尽；主菜单/ESC 空闲帧必须继续补齐，
	// 否则首次进入设置或切 tab 会在可见帧集中构建文本容器。
	EXPECT_NE(UpdateBody.find("QmMenuTextIdlePrewarmAllowed("), std::string::npos);
	EXPECT_NE(UpdateBody.find("PrewarmSettingsTextGlyphs(1, 64);"), std::string::npos);
	EXPECT_NE(UpdateBody.find("PrebuildSettingsMenuTextPool(8, \"target_settings\", \"settings_open\");"), std::string::npos);
	EXPECT_NE(UpdateBody.find("PrebuildVisibleSettingsMenuTextPool(64);"), std::string::npos);
	EXPECT_NE(Menus.find("void CMenus::PrebuildVisibleSettingsMenuTextPool(int Budget)"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, SettingsPlanCollectionDoesNotEnterVisibleGuard)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsBody = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsBody.empty());

	EXPECT_NE(RenderSettingsBody.find("std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;"), std::string::npos);
	const size_t GuardCondition = RenderSettingsBody.find("if(!CollectingMenuTextPlan)");
	const size_t GuardEmplace = RenderSettingsBody.find("TextVisibleGuard.emplace(this);");
	ASSERT_NE(GuardCondition, std::string::npos);
	ASSERT_NE(GuardEmplace, std::string::npos);
	EXPECT_LT(GuardCondition, GuardEmplace);
	EXPECT_EQ(RenderSettingsBody.find("const CScopedMenuTextVisibleGuard TextVisibleGuard(this);"), std::string::npos);
}

TEST(QmMonitoringSettingsPlanContract, SettingsPlanCollectionDoesNotSynchronouslyPumpStartupOrEsc)
{
	const std::string GameClient = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrewarmBody = ExtractSourceFunctionBody(GameClient, "void CGameClient::PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage)");
	const std::string RenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string EscBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)");
	ASSERT_FALSE(PrewarmBody.empty());
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(EscBody.empty());

	// 启动 loading 只返回非阻塞入口；全量收集和容器构建由主循环逐帧预算继续。
	EXPECT_NE(PrewarmBody.find("\"startup_text_prewarm_deferred\""), std::string::npos);
	EXPECT_NE(PrewarmBody.find("PrebuildSettingsTextPoolDuringStartup(32)"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("PrewarmSettingsTextGlyphs(4, Request)"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("static_queued=%d"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("PrewarmSettingsTextPoolForLoading("), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("for(int Step"), std::string::npos);

	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(96);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(4);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(3);"), std::string::npos);
	EXPECT_NE(RenderBody.find("StartSettingsPerfFixedWindow(\"ingame_esc_open\""), std::string::npos);
	EXPECT_EQ(EscBody.find("BuildSettingsMenuTextPlan("), std::string::npos);
}
