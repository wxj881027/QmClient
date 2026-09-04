#include <game/client/components/qmclient/core/qm_config_migration.h>

#include <engine/shared/config.h>
#include <engine/shared/console.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
struct SIntValue
{
	int m_Value;
	int m_Min;
	int m_Max;
};

void TestIntCommand(IConsole::IResult *pResult, void *pUserData)
{
	SIntValue *pValue = static_cast<SIntValue *>(pUserData);
	if(!pValue || !pResult->NumArguments())
		return;
	pValue->m_Value = std::clamp(pResult->GetInteger(0), pValue->m_Min, pValue->m_Max);
}

void RegisterMigrationCommands(CConsole &Console)
{
	for(const auto &Rule : CQmConfigMigration::Rules())
		Console.Register(Rule.m_pCurrentName, Rule.m_IsInteger ? "?i" : "?c", CFGFLAG_CLIENT, nullptr, nullptr, "");
}
}

TEST(QmConfigMigration, CurrentCommandWins)
{
	EXPECT_FALSE(CQmConfigMigration::ShouldApply(true));
	EXPECT_TRUE(CQmConfigMigration::ShouldApply(false));
}

TEST(QmConfigMigration, CommandSegmentsFollowConsoleSeparators)
{
	EXPECT_EQ(CQmConfigMigration::ExtractCommandSegment("tc_player_indicator 1; tc_indicator_inteam 1# comment"), "tc_player_indicator 1");
	EXPECT_EQ(CQmConfigMigration::ExtractCommandSegment("tc_player_indicator \"1;2\"; echo hello"), "tc_player_indicator \"1;2\"");
	EXPECT_FALSE(CQmConfigMigration::ExtractCommandSegment(nullptr).has_value());
}

TEST(QmConfigMigration, ConsoleMigrationPreservesClampAndCaseInsensitiveNames)
{
	g_Config.m_QmConfigMigrationVersion = 0;
	CConsole Console(CFGFLAG_CLIENT);
	RegisterMigrationCommands(Console);
	SIntValue Value{42, 16, 200};
	Console.Register("qm_player_indicator_offset", "?i", CFGFLAG_CLIENT, TestIntCommand, &Value, "");

	CQmConfigMigration Migration;
	Migration.OnConsoleInit(&Console);
	ASSERT_TRUE(Migration.OnUnknownCommand("TC_INDICATOR_OFFSET 15; echo should-not-run"));
	Migration.OnConfigLoaded(&Console);

	EXPECT_EQ(Value.m_Value, 16);
	EXPECT_EQ(g_Config.m_QmConfigMigrationVersion, CQmConfigMigration::CURRENT_VERSION);
}

TEST(QmConfigMigration, InvalidCommandsRemainUnknown)
{
	g_Config.m_QmConfigMigrationVersion = 0;
	CConsole Console(CFGFLAG_CLIENT);
	RegisterMigrationCommands(Console);
	CConfigManager ConfigManager;

	CQmConfigMigration Migration;
	Migration.OnConsoleInit(&Console);
	EXPECT_TRUE(Migration.OnUnknownCommand("tc_player_indicator not-an-integer", &ConfigManager));
	Migration.OnConfigLoaded(&Console, &ConfigManager);
	EXPECT_EQ(g_Config.m_QmConfigMigrationVersion, 0);
}

TEST(QmConfigMigration, CurrentCommandWinsBeforeLegacyValueValidation)
{
	g_Config.m_QmConfigMigrationVersion = 0;
	CConsole Console(CFGFLAG_CLIENT);
	RegisterMigrationCommands(Console);
	CConfigManager ConfigManager;

	CQmConfigMigration Migration;
	Migration.OnConsoleInit(&Console);
	Console.ExecuteLine("qm_player_indicator 1");
	ASSERT_TRUE(Migration.OnUnknownCommand("tc_player_indicator not-an-integer", &ConfigManager));
	Migration.OnConfigLoaded(&Console, &ConfigManager);

	EXPECT_EQ(g_Config.m_QmConfigMigrationVersion, CQmConfigMigration::CURRENT_VERSION);
}

TEST(QmConfigMigration, RulesContainOnlyTheFirstFeatureSlice)
{
	const auto &aRules = CQmConfigMigration::Rules();
	ASSERT_EQ(aRules.size(), CQmConfigMigration::RULE_COUNT);
	EXPECT_STREQ(aRules.front().m_pLegacyName, "tc_player_indicator");
	EXPECT_STREQ(aRules.front().m_pCurrentName, "qm_player_indicator");
	EXPECT_STREQ(aRules.back().m_pLegacyName, "tc_indicator_dead");
	EXPECT_STREQ(aRules.back().m_pCurrentName, "qm_player_indicator_unfreezing_color");
}
