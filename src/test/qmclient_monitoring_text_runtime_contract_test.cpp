// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <string>

// 文本渲染运行时与 Ingame ESC 打开路径：渲染器预算快照、Ingame ESC 注册表覆盖、
// 预构建时机先于可见帧，以及默认皮肤加载失败后的发布状态清理。
TEST(QmMonitoringTextRuntimeContract, TextRenderExposesRuntimeBudgetSnapshotForScheduler)
{
	const std::string Header = ReadRepoFile("src/engine/textrender.h");
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");

	EXPECT_NE(Header.find("struct SQmTextRuntimeBudgetSnapshot"), std::string::npos);
	EXPECT_NE(Header.find("virtual SQmTextRuntimeBudgetSnapshot QmTextRuntimeBudgetSnapshot() const"), std::string::npos);
	EXPECT_NE(Text.find("SQmTextRuntimeBudgetSnapshot m_QmLastTextRuntimeBudgetSnapshot"), std::string::npos);
	EXPECT_NE(Text.find("m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerCreateMs"), std::string::npos);
}

TEST(QmMonitoringTextRuntimeContract, IngameEscStableTextRegistryCoversMenubar)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PlanBody = ExtractSourceFunctionBody(Menus, "void CMenus::BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string CollectionBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	ASSERT_FALSE(PlanBody.empty());
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(CollectionBody.empty());

	EXPECT_NE(Header.find("void PrebuildIngameEscTextPoolBeforeOpen(int Budget);"), std::string::npos);
	EXPECT_NE(Menus.find("PrebuildIngameEscTextPoolBeforeOpen("), std::string::npos);
	EXPECT_EQ(Menus.find("PrebuildIngameEscTextPoolBeforeOpen(96);"), std::string::npos);
	EXPECT_NE(CollectionBody.find("const bool IngameEscOperation = str_comp(pOperation, \"ingame_esc_open\") == 0;"), std::string::npos);
	EXPECT_NE(CollectionBody.find("if(IngameEscOperation)"), std::string::npos);
	EXPECT_NE(Menus.find("BuildIngameMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, Screen);"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), std::string::npos);
	for(const char *pId : {
		    "\"ingame-tab-game\"",
		    "\"ingame-tab-players\"",
		    "\"ingame-tab-server-info\"",
		    "\"ingame-tab-browser\"",
		    "\"ingame-tab-ghost\"",
		    "\"ingame-tab-call-vote\"",
	    })
	{
		EXPECT_NE(PlanBody.find(pId), std::string::npos) << pId;
	}
	EXPECT_EQ(Menus.find("plan_status\":\"missing_descriptor\""), std::string::npos);
}

TEST(QmMonitoringTextRuntimeContract, SettingsStableTextMissAndStaleBlockVisibleBuild)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	EXPECT_TRUE(ContainsAll(Source, {"event=settings_text_miss", "event=settings_text_stale", "m_MenuTextPoolVisibleGuard", "StableTextMiss", "StableTextStale"}));
	EXPECT_TRUE(ContainsAll(Source, {"event=settings_text_usage", "m_MenuTextStableCandidatesThisFrame", "m_MenuTextStableHitsThisFrame", "m_MenuTextStableReusedThisFrame"}));
	EXPECT_TRUE(ContainsAll(Source, {"MenuTextPoolSizeForTesting", "CScopedMenuTextVisibleGuard"}));
	EXPECT_EQ(Source.find("context-checkbox-common-"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Source, {
						"scope=%s page=%s tab=%d subtab=%d key=%s reason=%s plan_status=%s operation=%s frame=%",
						"%s:%d:%d:%d:%s:fs%d:al%d:mw%d:us%d:hd%d:cm%d",
						"StyleKey.m_Align",
						"StyleKey.m_MaxWidthBucket",
						"StyleKey.m_HiDpiScaleBucket",
						"StyleKey.m_CompactMode",
					}));
	EXPECT_TRUE(ContainsAll(Source, {
						"const char *CMenus::SettingsPerfStableTextScope(int Page) const",
						"str_comp(SettingsPerfActiveOperation(), \"ingame_esc_open\") == 0",
						"(void)Page;",
						"return str_comp(pActivePage, aPage) == 0 ? \"target_settings\" : \"settings\";",
						"SettingsPerfStableTextScope(Page)",
					}));
	EXPECT_TRUE(ContainsAll(Source, {"LogSettingsTextPoolCoverageGap(Client(), \"settings_text_miss\", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab", "LogSettingsTextPoolCoverageGap(Client(), \"settings_text_stale\", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab"}));
	EXPECT_TRUE(ContainsAll(Source, {"case ESettingsInvalidationReason::DPI_CHANGED: return \"dpi\";", "case ESettingsInvalidationReason::UI_SCALE_CHANGED: return \"ui_scale\";"}));

	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	EXPECT_TRUE(ContainsAll(Header, {
						"void DoSettingsMenuLabel(int Page, int Tab, int Subtab",
						"int DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC",
						"int DoSettingsButton_CheckBox(int Page, int Tab, int Subtab",
						"bool DoSettingsScrollbarOption(int Page, int Tab, int Subtab",
					}));
	EXPECT_NE(Source.find("CUIElement &TextElement = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);"), std::string::npos);
	EXPECT_NE(Source.find("dbg_assert(pBC != nullptr, \"settings menu button requires a stable button container\")"), std::string::npos);
	EXPECT_EQ(Header.find("CButtonContainer *pBC = nullptr"), std::string::npos);
	EXPECT_EQ(Source.find("s_FallbackButton"), std::string::npos);
	EXPECT_NE(Source.find("DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize)"), std::string::npos);
	EXPECT_EQ(Source.find("reason=%s\", pReason != nullptr ? pReason : \"unknown\""), std::string::npos);

	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	EXPECT_TRUE(ContainsAll(TClient, {
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_OverrideButton",
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton",
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_RemoveButton",
					 }));
}

TEST(QmMonitoringTextRuntimeContract, IngameEscPrewarmsStableTextBeforeVisibleFrame)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string CollectionBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	const std::string CollectBody = ExtractSourceFunctionBody(Menus, "void CMenus::CollectSettingsMenuTextPlanUnit(const SSettingsMenuTextPlanCollectionUnit &Unit, CUIRect Screen, CUIRect SettingsMainView)");
	ASSERT_FALSE(OnRenderBody.empty());
	ASSERT_FALSE(CollectionBody.empty());
	ASSERT_FALSE(CollectBody.empty());

	const size_t StartWindowPos = OnRenderBody.find("StartSettingsPerfFixedWindow(\"ingame_esc_open\"");
	const size_t SetActivePos = OnRenderBody.find("SetActive(true);", StartWindowPos);
	const size_t RenderPos = OnRenderBody.find("Render();");
	ASSERT_NE(StartWindowPos, std::string::npos);
	ASSERT_NE(SetActivePos, std::string::npos);
	ASSERT_NE(RenderPos, std::string::npos);
	EXPECT_LT(StartWindowPos, SetActivePos);
	EXPECT_LT(SetActivePos, RenderPos);

	EXPECT_NE(Header.find("uint64_t m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(Header.find("bool m_IngameServerInfoBackgroundPrepareRequested"), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildIngameEscTextPoolBeforeOpen("), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildSettingsMenuTextPool("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("Client()->PerfFrame() > m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime();"), std::string::npos);
	EXPECT_NE(CollectionBody.find("m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1});"), std::string::npos);
	EXPECT_NE(CollectBody.find("case MENU_TEXT_PLAN_UNIT_INGAME_ESC:"), std::string::npos);
	EXPECT_NE(CollectBody.find("BuildIngameMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, Screen);"), std::string::npos);
}

TEST(QmMonitoringTextRuntimeContract, DefaultExtrasFailureClearsPublishedState)
{
	const std::string GameClient = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string LoadExtras = ExtractSourceFunctionBody(GameClient, "void CGameClient::LoadExtrasSkin(const char *pPath, bool AsDir)");
	ASSERT_FALSE(LoadExtras.empty());
	const size_t CandidateFailure = LoadExtras.find("UnloadExtrasSkin(Candidate);");
	const size_t DefaultFailure = LoadExtras.find("if(IsDefault)", CandidateFailure);
	ASSERT_NE(CandidateFailure, std::string::npos);
	ASSERT_NE(DefaultFailure, std::string::npos);
	EXPECT_NE(LoadExtras.find("m_ExtrasSkin = {};", DefaultFailure), std::string::npos);
	EXPECT_NE(LoadExtras.find("m_ExtrasSkinLoaded = false;", DefaultFailure), std::string::npos);
}
