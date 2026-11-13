// 请抬头享受阳光｜日子很好 我很我---------致咩子
// Qm monitoring 源码合同：设置界面结构、卡片甲板与注册表导航。
// 文本计划/预热调度合同见 qmclient_monitoring_settings_plan_contract_test.cpp；
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
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringSettingsStructureContract, SettingsUiMigrationFinalStructureContract)
{
	const std::array<const char *, 11> apRetiredSymbols = {
		"RenderQmSettingsGlassCard",
		"BeginSettingsCardDeck",
		"RegisterSettingsCardDeckItemFromFrame",
		"LegacyTextFieldEx",
		"DoSettingsSliderInputField",
		"m_TClientSettingsCardDragState",
		"m_SettingsCardDeckOrders",
		"ForceShowScrollbar",
		"CachedHeightForStableCardId",
		"DrawTClientCacheSectionBox",
		"InsetTClientCacheSectionContent",
	};
	const std::filesystem::path ClientPath = TestSourcePath("src/game/client");
	for(const std::filesystem::directory_entry &Entry : std::filesystem::recursive_directory_iterator(ClientPath))
	{
		if(!Entry.is_regular_file() || (Entry.path().extension() != ".cpp" && Entry.path().extension() != ".h"))
			continue;
		std::ifstream File(Entry.path());
		ASSERT_TRUE(File.good()) << Entry.path().string();
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		for(const char *pSymbol : apRetiredSymbols)
			EXPECT_EQ(Source.find(pSymbol), std::string::npos) << Entry.path().string() << ": " << pSymbol;
	}

	const std::string ScrollHeader = ReadRepoFile("src/game/client/ui_scrollregion.h");
	EXPECT_EQ(CountSubstring(ScrollHeader, "CQmScrollState m_ScrollState;"), 1u);
	EXPECT_EQ(ScrollHeader.find("float m_ScrollPos;"), std::string::npos);
	EXPECT_EQ(ScrollHeader.find("float m_AnimTargetScrollPos;"), std::string::npos);

	const std::array<std::pair<const char *, const char *>, 4> aNonCardScopes = {{
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFilters(CUIRect View)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFriends(CUIRect View)"},
		{"src/game/client/components/menus_settings.cpp", "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)"},
	}};
	for(const auto &[pPath, pSignature] : aNonCardScopes)
	{
		const std::string Body = ExtractSourceFunctionBody(ReadRepoFile(pPath), pSignature);
		ASSERT_FALSE(Body.empty()) << pSignature;
		EXPECT_EQ(Body.find("SettingsCard("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.Render("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.RenderCached("), std::string::npos) << pSignature;
	}
}

TEST(QmMonitoringSettingsStructureContract, RenderOnlyNumericFieldsAndDropDownsDoNotMutateControlState)
{
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp") + ReadRepoFile("src/game/client/ui_popups.cpp");
	const std::string IntegerField = ExtractSourceFunctionBody(Forms, "SInputFieldResult IntegerField(");
	const std::string NumericField = ExtractSourceFunctionBody(Forms, "bool NumericField(");
	const std::string DropDown = ExtractSourceFunctionBody(Ui, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	ASSERT_FALSE(IntegerField.empty());
	ASSERT_FALSE(NumericField.empty());
	ASSERT_FALSE(DropDown.empty());
	EXPECT_NE(DropDown.find("m_DropDownFontSize > 0.0f ? m_DropDownFontSize"), std::string::npos);
	EXPECT_NE(DropDown.find("State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;"), std::string::npos);
	EXPECT_NE(DropDown.find("ButtonProps.m_FontSize = ResolvedFontSize;"), std::string::npos);

	const size_t IntegerRenderOnly = IntegerField.find("if(Ctx.m_pUi->RenderOnly())");
	const size_t IntegerWrite = IntegerField.find("*pValue = ClampedValue;");
	ASSERT_NE(IntegerRenderOnly, std::string::npos);
	ASSERT_NE(IntegerWrite, std::string::npos);
	EXPECT_LT(IntegerRenderOnly, IntegerWrite);

	const size_t NumericRenderOnly = NumericField.find("if(RenderOnly)");
	const size_t NumericStateWrite = NumericField.find("pState->m_LastSyncedStoredValue = *pValue;");
	const size_t NumericInputWrite = NumericField.find("pInput->SetInteger(DisplayValue);");
	ASSERT_NE(NumericRenderOnly, std::string::npos);
	ASSERT_NE(NumericStateWrite, std::string::npos);
	ASSERT_NE(NumericInputWrite, std::string::npos);
	EXPECT_LT(NumericRenderOnly, NumericStateWrite);
	EXPECT_LT(NumericRenderOnly, NumericInputWrite);

	const size_t DropDownRenderOnly = DropDown.find("if(RenderOnly())");
	const size_t DropDownStateInit = DropDown.find("if(!State.m_Init)");
	ASSERT_NE(DropDownRenderOnly, std::string::npos);
	ASSERT_NE(DropDownStateInit, std::string::npos);
	EXPECT_LT(DropDownRenderOnly, DropDownStateInit);
}

TEST(QmMonitoringSettingsStructureContract, SettingsLoadingPrewarmApiIsPublic)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const size_t StartLoading = Header.find("void StartLoading(int Total);");
	const size_t PrewarmSettingsPages = Header.find("void PrewarmSettingsPages();");
	const size_t IsInit = Header.find("bool IsInit() const");
	ASSERT_NE(StartLoading, std::string::npos);
	ASSERT_NE(PrewarmSettingsPages, std::string::npos);
	ASSERT_NE(IsInit, std::string::npos);

	EXPECT_GT(PrewarmSettingsPages, StartLoading);
	EXPECT_LT(PrewarmSettingsPages, IsInit);
}
