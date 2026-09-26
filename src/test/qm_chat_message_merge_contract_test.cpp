// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

TEST(QmChatMessageMergeContract, ChatAndConsoleKeepStructuredMergedAuthors)
{
	const std::string ChatHeader = ReadRepoFile("src/game/client/components/chat.h");
	const std::string Chat = ReadRepoFile("src/game/client/components/chat.cpp");
	const std::string ConsoleHeader = ReadRepoFile("src/game/client/components/console.h");
	const std::string Console = ReadRepoFile("src/game/client/components/console.cpp");
	const std::string Translate = ReadRepoFile("src/game/client/components/qmclient/translate/translate.cpp");
	const std::string AddLine = ExtractSourceFunctionBody(Chat, "void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional");

	EXPECT_TRUE(ContainsAll(ChatHeader, {"struct SMergedAuthor", "std::vector<SMergedAuthor> m_vMergedAuthors"}));
	EXPECT_TRUE(ContainsAll(AddLine, {"g_Config.m_QmMessageMerge", "CanMergePlayerMessages(", "!Highlighted &&"}));
	EXPECT_FALSE(ContainsAny(AddLine, {"PreviousLine.m_Team = false;", "PreviousLine.m_TeamNumber = 0;", "PreviousLine.m_ClientId == ClientId"}));
	EXPECT_TRUE(ContainsAll(Chat, {
					      "if(Author.m_ClientId == ClientId)",
					      "Author.m_NameColor = PlayerNameColor(ClientId, NameColor, false);",
					      "\" [%d]: \", Line.m_TimesRepeated + 1",
					      "FlushPendingConsoleLine",
					      "GameClient()->m_GameConsole.PrintLineWithColorSpans",
					      "const bool MergedPlayerMessages = Line.m_TimesRepeated > 0 && !Line.m_vMergedAuthors.empty();",
					      "m_PlayerLine = Line.m_vMergedAuthors.size() <= 1",
				      }));
	EXPECT_TRUE(ContainsAll(ConsoleHeader, {"struct SColorSpan", "m_ColorSpansByExportId", "PrintLineWithColorSpans"}));
	EXPECT_TRUE(ContainsAll(Console, {"m_PendingColorSpansByExportId", "EntryCursor.m_vColorSplits.emplace_back"}));
	EXPECT_NE(Translate.find("for(const CChat::SMergedAuthor &Author : pLine->m_vMergedAuthors)"), std::string::npos);
}

TEST(QmChatMessageMergeContract, SettingIsDefaultLocalizedAndVersioned)
{
	const std::string Config = ReadRepoFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Translations = ReadRepoFile("qmclient_scripts/languages_qmclient/translations/i18n/qmclient.toml");
	const std::string Version = ReadRepoFile("src/game/version.h");
	const size_t MiniFeatures = Menus.find("void CMenus::RenderQmFunctionMiniFeaturesContent(");

	ASSERT_NE(MiniFeatures, std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmMessageMerge, qm_message_merge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Menus.find("RenderCheckbox(&g_Config.m_QmMessageMerge, \"Message merging\", &g_Config.m_QmMessageMerge);", MiniFeatures), std::string::npos);
	EXPECT_TRUE(ContainsAll(Translations, {"key = \"Message merging\"", "simplified_chinese = \"消息合并\""}));
	EXPECT_TRUE(ContainsAll(Version, {"#define QMCLIENT_STABLE_VERSION \"", "#define QMCLIENT_DEV_VERSION \""}));
}
