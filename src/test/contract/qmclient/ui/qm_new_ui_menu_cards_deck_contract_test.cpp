// QmNewUi 菜单源码合同：设置卡片甲板状态与生命周期。
// 卡片视觉表面与共享样式合同见 qm_new_ui_menu_cards_surface_contract_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuCardsDeckContract, SettingsCardDeckResetsStateWhenDefinitionViewChanges)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	ASSERT_FALSE(SettingsDeck.empty());
	EXPECT_NE(SettingsDeck.find("void CSettingsCardDeck::ResetDefinitionViewState()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_vContentHeights.clear();"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_ProjectionCache = {};"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, SettingsCardDeckPreLayoutUsesTheLastVisibleAnimatedFrame)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string ButtonLogic = FunctionBody(UiSource, "int CUi::DoButtonLogic(");
	ASSERT_FALSE(SettingsDeck.empty());
	ASSERT_FALSE(ButtonLogic.empty());
	EXPECT_NE(SettingsDeck.find("const SSettingsCardFrame PreLayoutFrame = ResolveSettingsCardDrawFrame(Card.m_Frame, Runtime.m_LastDrawOffsetX, Runtime.m_LastDrawOffsetY);"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutHeaderInput(PreLayoutFrame, CollapsedBeforeHeader)"), std::string::npos);
	const size_t CardLoop = SettingsDeck.find("for(const SPreparedCard &Card : m_vPreparedCards)");
	const size_t ActiveHeaderContinuation = SettingsDeck.find("const bool HasActiveHeaderContinuation = SettingsCardDeckHasActiveItemContinuation", CardLoop);
	const size_t ActiveContentContinuation = SettingsDeck.find("const bool HasActiveContentContinuation = SettingsCardDeckHasActiveItemContinuation", ActiveHeaderContinuation);
	ASSERT_NE(CardLoop, std::string::npos);
	ASSERT_NE(ActiveHeaderContinuation, std::string::npos);
	ASSERT_NE(ActiveContentContinuation, std::string::npos);
	EXPECT_LT(CardLoop, ActiveHeaderContinuation);
	EXPECT_LT(ActiveHeaderContinuation, ActiveContentContinuation);
	EXPECT_NE(SettingsDeck.find("(ControllerVisible || HasActiveHeaderContinuation) && Card.m_pDefinition->m_PreLayoutHeaderInput"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckShouldRunPreLayoutInput(HasPointerInput, HasPendingPreLayoutInput, HasActiveContentContinuation"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckUsesDefaultCollapseControl(HasCustomCollapsedState, static_cast<bool>(Card.m_pDefinition->m_PreLayoutHeaderInput))"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Card.m_pDefinition->m_HeaderAction"), std::string::npos);
	// 自定义折叠状态的卡片改由 m_IsCollapsed 解析，公共按钮与回调都不再改写它。
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutInput(PreLayoutFrame.m_ContentRect)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Runtime.m_LastDrawOffsetY = State.m_DrawOffsetY;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("const bool AllowCurrentPress = UseCurrentHit || (PreLayoutInput() && !IsPopupOpen());"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pBecomingHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("AllowCurrentPress ? QmButtonCurrentPress("), std::string::npos);
	EXPECT_LT(ButtonLogic.find("m_pHotItem = pId;"), ButtonLogic.find("SetActiveItem(pId);"));
}

TEST(QmNewUiMenuCardsDeckContract, DeckPreLayoutPressClearsStaleActiveInput)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const size_t PreLayoutPress = UiSource.find("const bool AllowCurrentPress = UseCurrentHit || (PreLayoutInput() && !IsPopupOpen());");
	ASSERT_NE(PreLayoutPress, std::string::npos);
	const size_t PreLayoutPressEnd = UiSource.find("int CUi::DoDraggableButtonLogic", PreLayoutPress);
	ASSERT_NE(PreLayoutPressEnd, std::string::npos);
	const std::string ButtonLogic = UiSource.substr(PreLayoutPress, PreLayoutPressEnd - PreLayoutPress);
	EXPECT_NE(ButtonLogic.find("CLineInput::GetActiveInput()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("pActiveInput->Deactivate()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pLastActiveItem = nullptr"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("SetActiveItem(nullptr)"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, SettingsDisplayCycleUpdatesAfterTabInputBeforePageRender)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string DeckSource = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string RenderSettings = FunctionBody(SettingsSource, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettings.empty());
	const size_t TabInput = RenderSettings.find("DoButton_MenuTab(&m_aSettingsTabButtons[i]");
	const size_t DisplayCycle = RenderSettings.find("m_SettingsCardDeck.BeginDisplayCycle(");
	const size_t PageRender = RenderSettings.find("RenderSettingsGeneral(ContentView)");
	ASSERT_NE(TabInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	ASSERT_NE(PageRender, std::string::npos);
	EXPECT_LT(TabInput, DisplayCycle);
	EXPECT_LT(DisplayCycle, PageRender);
	EXPECT_EQ(RenderSettings.find("Accent.VSplitLeft(3.0f"), std::string::npos);
	EXPECT_NE(DeckSource.find("const bool TabChanged = m_LastRenderedTab != pTab;"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(DeckSource.find("m_SuppressHoverFeedbackOnce = true;"), std::string::npos);
}

TEST(QmNewUiMenuCardsDeckContract, QmClientDecksIsolateRenderOnlyState)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CreditsSource = ReadTextFile("src/game/client/components/menus_credits.cpp");
	for(const char *pSignature : {
		    "void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsContributors(CUIRect MainView, bool PrewarmOnly)",
	    })
	{
		// 贡献者页已拆到 menus_credits.cpp；其余栖梦 deck 仍在 menus_qmclient.cpp。
		const std::string &SourceFile = str_startswith(pSignature, "void CMenus::RenderSettingsContributors") != nullptr ? CreditsSource : Source;
		const std::string Body = FunctionBody(SourceFile, pSignature);
		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
		EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("qm_card_order::CModel &CardOrderModel = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
		EXPECT_NE(Body.find("ReadOnly ? nullptr : &"), std::string::npos);
		EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	}
}
