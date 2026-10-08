#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/qm_default_profile.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	class CQmLegacyConfigLoad : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_pConsole = CreateConsole(CFGFLAG_CLIENT);
			m_pConfigManager.reset(CreateConfigManager());
			m_pKernel->RegisterInterface(m_pStorage.get(), false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfigManager.get(), false);
			m_pConsole->Init();
			m_pConfigManager->Init();
			m_pConsole->StoreCommands(false);
			g_Config.m_ClSaveSettings = 1;
		}

		void TearDown() override { g_Config = m_PreviousConfig; }

		void WriteConfig(const char *pName, const char *pContents)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pContents, str_length(pContents)), static_cast<unsigned>(str_length(pContents)));
			EXPECT_EQ(io_close(File), 0);
		}

		void LoadConfig(const char *pName)
		{
			ASSERT_TRUE(m_pConsole->ExecuteFile(pName, IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE));
		}

		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IConfigManager> m_pConfigManager;
	};
}

TEST_F(CQmLegacyConfigLoad, OldFileLoadsIntegerStringAndColorThenSavesOnlyNewNames)
{
	WriteConfig("legacy.cfg", "tc_show_chat_client 0\ntc_jump_hint_text \"old text\"\ntc_jump_hint_color 128\n");
	LoadConfig("legacy.cfg");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 0);
	EXPECT_STREQ(g_Config.m_QmJumpHintText, "old text");
	EXPECT_EQ(g_Config.m_QmJumpHintColor, 128u);
	ASSERT_TRUE(m_pConfigManager->Save());
	IOHANDLE File = m_pStorage->OpenFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	char *pText = io_read_all_str(File);
	io_close(File);
	ASSERT_NE(pText, nullptr);
	const std::string Saved = pText;
	free(pText);
	EXPECT_NE(Saved.find("qm_show_chat_client 0"), std::string::npos);
	EXPECT_NE(Saved.find("qm_jump_hint_text \"old text\""), std::string::npos);
	EXPECT_EQ(Saved.find("tc_show_chat_client"), std::string::npos);
	EXPECT_EQ(Saved.find("tc_jump_hint_"), std::string::npos);
}

TEST_F(CQmLegacyConfigLoad, NewKeyWinsInEitherOrderEvenWhenItExplicitlySetsTheDefault)
{
	WriteConfig("mixed.cfg", "qm_show_chat_client 1\ntc_show_chat_client 0\ntc_frozen_tees_hud 0\nqm_frozen_tees_hud 1\n");
	LoadConfig("mixed.cfg");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 1);
	EXPECT_EQ(g_Config.m_QmShowFrozenHud, 1);
	LoadConfig("mixed.cfg");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 1);
	EXPECT_EQ(g_Config.m_QmShowFrozenHud, 1);
}

TEST_F(CQmLegacyConfigLoad, NestedOldFileCannotReplaceNewValueOrSkipFollowingCommands)
{
	WriteConfig("nested.cfg", "tc_show_chat_client 0; cl_showhud 0\n");
	WriteConfig("outer.cfg", "qm_show_chat_client 1\nexec nested.cfg\n");
	LoadConfig("outer.cfg");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 1);
	EXPECT_EQ(g_Config.m_ClShowhud, 0);
	m_pConsole->ExecuteLine("tc_show_chat_client 0");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 0);
}

TEST_F(CQmLegacyConfigLoad, InvalidNewValueDoesNotSuppressValidLegacyValue)
{
	WriteConfig("invalid.cfg", "qm_show_chat_client invalid\ntc_show_chat_client 0\n");
	LoadConfig("invalid.cfg");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 0);
}

TEST_F(CQmLegacyConfigLoad, LegacyToggleResetAndCommandLookupUseCanonicalVariable)
{
	m_pConsole->ExecuteLine("tc_show_chat_client 0");
	m_pConsole->ExecuteLine("toggle tc_show_chat_client 0 1");
	EXPECT_EQ(g_Config.m_QmShowChatClient, 1);
	m_pConsole->ExecuteLine("TC_SHOW_CHAT_CLIENT 0");
	m_pConsole->ExecuteLine("reset tc_show_chat_client");
	EXPECT_EQ(g_Config.m_QmShowChatClient, DefaultConfig::QmShowChatClient);
	const auto *pInfo = m_pConsole->GetCommandInfo("tc_show_chat_client", CFGFLAG_CLIENT, false);
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ(pInfo->Name(), "qm_show_chat_client");
}

TEST_F(CQmLegacyConfigLoad, LegacyWriteRunsCanonicalCommandChainAndMarksExplicitValue)
{
	int Changes = 0;
	m_pConsole->Chain("qm_custom_font", [](IConsole::IResult *pResult, void *pUser, IConsole::FCommandCallback pfnCallback, void *pCallbackUser) {
		pfnCallback(pResult, pCallbackUser);
		if(pResult->NumArguments() > 0)
			++*static_cast<int *>(pUser);
	},
		&Changes);
	EXPECT_FALSE(QmConfigValueWasExplicitlySet(*m_pConfigManager, "qm_custom_font"));
	m_pConsole->ExecuteLine("qm_custom_font");
	EXPECT_FALSE(QmConfigValueWasExplicitlySet(*m_pConfigManager, "qm_custom_font"));
	m_pConsole->ExecuteLine("tc_custom_font \"chosen font\"");
	EXPECT_TRUE(QmConfigValueWasExplicitlySet(*m_pConfigManager, "qm_custom_font"));
	EXPECT_EQ(Changes, 1);
	QmInitializeDefaultProfile(g_Config, true, [this](const char *pName) { return QmConfigValueWasExplicitlySet(*m_pConfigManager, pName); });
	EXPECT_STREQ(g_Config.m_QmCustomFont, "chosen font");
}
