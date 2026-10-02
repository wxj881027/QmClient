#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

TEST(QmHudNotificationContract, SimplifiedChineseTranslationsContainPromptCatalogEntries)
{
	std::ifstream File(std::string(DDNET_TEST_SOURCE_DIR) + "/qmclient_scripts/languages_qmclient/translations/i18n/qmclient.toml", std::ios::binary);
	ASSERT_TRUE(File.good());
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Translations = Buffer.str();

	EXPECT_NE(Translations.find("key = \"Scoreboard point check\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"计分板积分检查\""), std::string::npos);
	EXPECT_NE(Translations.find("key = \"Team can't be saved while a dragger is active\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"有拖拽器生效时不能保存队伍存档\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"本服务器不允许查看队伍前 5 名\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"你现在可以用锤子攻击其他玩家\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"你现在不能用锤子攻击其他玩家\""), std::string::npos);
}
