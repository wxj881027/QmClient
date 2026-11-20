// QmNewUi 菜单源码合同：设置卡片甲板状态与生命周期。
// 卡片视觉表面与共享样式合同见 qm_new_ui_menu_cards_surface_contract_test.cpp。
#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

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
