#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(SettingsPlayerInfoContract, ChangesAreDeferredUntilMenuCloses)
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
