// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

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
	// 这里只约束文本计划的稳定注册 ID；运行时计划选择由 SettingsLoadingPrewarm 行为测试验证。
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PlanBody = ExtractSourceFunctionBody(Menus, "void CMenus::BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)");
	ASSERT_FALSE(PlanBody.empty());
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
	EXPECT_NE(Source.find("return DoButton_MenuInternal(pBC, pText, Icon, pFallbackIcon ? pFallbackIcon : \"\", Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize"), std::string::npos);
	EXPECT_EQ(Source.find("reason=%s\", pReason != nullptr ? pReason : \"unknown\""), std::string::npos);
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
