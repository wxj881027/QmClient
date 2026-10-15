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
