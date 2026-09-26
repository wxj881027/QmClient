#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmLocalizationContract, LocalizedDropdownNamesAreNotStaticHeapPointers)
{
	const std::string Chat = ReadTestSourceFile("src/game/client/components/chat.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Localization = ReadTestSourceFile("src/game/localization.cpp");
	EXPECT_EQ(Chat.find("static const char *s_apBackendNames[] = {Localize"), std::string::npos);
	EXPECT_EQ(Menus.find("static std::vector<const char *> s_LlmProviderDropDownNames ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apSourceNames[] ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apOutgoingModeNames[] ="), std::string::npos);
	EXPECT_NE(Localization.find("if(LocalizationIsContextLine(pLine))"), std::string::npos);
	EXPECT_NE(Localization.find("Couldn't open language file '%s'"), std::string::npos);
}
