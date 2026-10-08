#include <engine/shared/qm_legacy_config.h>

#include <gtest/gtest.h>

TEST(QmLegacyConfig, KnownAliasesIncludeExistingJumpHintWithoutRenamingTencentSettings)
{
	EXPECT_STREQ(QmLegacyConfig::CanonicalName("tc_show_chat_client"), "qm_show_chat_client");
	EXPECT_STREQ(QmLegacyConfig::CanonicalName("TC_JUMP_HINT"), "qm_jump_hint");
	EXPECT_STREQ(QmLegacyConfig::CanonicalName("qm_translate_tc_secret_id"), "qm_translate_tc_secret_id");
	EXPECT_STREQ(QmLegacyConfig::CanonicalName("tc_unknown_future_option"), "tc_unknown_future_option");
}

TEST(QmLegacyConfig, ExplicitCanonicalWriteWinsOverLegacyFilesButAllowsInteractiveAlias)
{
	QmLegacyConfig::CWritePrecedence Writes;
	EXPECT_TRUE(Writes.ShouldExecute("tc_show_chat_client", true, true));
	EXPECT_TRUE(Writes.ShouldExecute("qm_show_chat_client", true, true));
	EXPECT_FALSE(Writes.ShouldExecute("tc_show_chat_client", true, true));
	EXPECT_FALSE(Writes.ShouldExecute("TC_SHOW_CHAT_CLIENT", true, true));
	EXPECT_TRUE(Writes.ShouldExecute("tc_show_chat_client", true, false));
	EXPECT_TRUE(Writes.ShouldExecute("tc_frozen_tees_hud", true, true));
}

TEST(QmLegacyConfig, QueryDoesNotBlockOldFileAndFreshSessionDoesNotInheritPrecedence)
{
	QmLegacyConfig::CWritePrecedence Writes;
	EXPECT_TRUE(Writes.ShouldExecute("qm_show_chat_client", false, true));
	EXPECT_TRUE(Writes.ShouldExecute("tc_show_chat_client", true, true));
	EXPECT_TRUE(Writes.ShouldExecute("qm_show_chat_client", true, false));
	EXPECT_FALSE(Writes.ShouldExecute("tc_show_chat_client", true, true));
	QmLegacyConfig::CWritePrecedence FreshWrites;
	EXPECT_TRUE(FreshWrites.ShouldExecute("tc_show_chat_client", true, true));
}

TEST(QmLegacyBind, MigratesOnlyConfigCommandsAndConfigNameArguments)
{
	const char *pOld = "tc_show_chat_client 0; toggle tc_frozen_tees_hud 0 1;+toggle tc_jump_hint 1 0;+toggle_restore tc_jump_hint 1; reset tc_jump_hint";
	const char *pNew = "qm_show_chat_client 0; toggle qm_frozen_tees_hud 0 1;+toggle qm_jump_hint 1 0;+toggle_restore qm_jump_hint 1; reset qm_jump_hint";
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand(pOld), pNew);
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand(pNew), pNew);
}

TEST(QmLegacyBind, ChatTextQuotedValuesAndUnrelatedCommandsStayLiteral)
{
	const char *pCommand = R"(say "tc_jump_hint; toggle tc_show_chat_client 0 1"; echo tc_show_chat_client; qm_translate_tc_secret_id "tc_jump_hint"; tc_unknown_future_option 2)";
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand(pCommand), pCommand);
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand("tc_jump_hint_text \"tc_jump_hint\""), "qm_jump_hint_text \"tc_jump_hint\"");
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand("toggle \"tc_jump_hint\" 0 1 # tc_show_chat_client"), "toggle \"qm_jump_hint\" 0 1 # tc_show_chat_client");
}

TEST(QmLegacyBind, NestedBindMigratesCommandWithoutChangingEscapedChatMessage)
{
	const char *pOld = R"(bind x "toggle tc_jump_hint 0 1;say \"tc_jump_hint\"")";
	const char *pNew = R"(bind x "toggle qm_jump_hint 0 1;say \"tc_jump_hint\"")";
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand(pOld), pNew);
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand("bind x tc_show_chat_client 0"), "bind x qm_show_chat_client 0");
}

TEST(QmLegacyBind, IncompleteNestedQuoteIsPreservedForConsoleErrorHandling)
{
	const char *pCommand = "bind x \"toggle tc_jump_hint 0 1";
	EXPECT_EQ(QmLegacyConfig::MigrateBindCommand(pCommand), pCommand);
}
