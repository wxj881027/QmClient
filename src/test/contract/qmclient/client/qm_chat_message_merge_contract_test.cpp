// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

TEST(QmChatMessageMergeContract, SettingIsDefaultAndLocalized)
{
	const std::string Config = ReadRepoFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Translations = ReadRepoFile("qmclient_scripts/languages_qmclient/translations/i18n/qmclient.toml");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmMessageMerge, qm_message_merge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Translations, {"key = \"Message merging\"", "simplified_chinese = \"消息合并\""}));
}
