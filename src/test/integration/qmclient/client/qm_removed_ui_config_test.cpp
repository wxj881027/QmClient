#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	class CQmRemovedUiConfig : public testing::Test
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

		void TearDown() override
		{
			g_Config = m_PreviousConfig;
		}

		std::string ReadSavedConfig()
		{
			IOHANDLE File = m_pStorage->OpenFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(!File)
				return {};
			char *pText = io_read_all_str(File);
			io_close(File);
			const std::string Text = pText != nullptr ? pText : "";
			free(pText);
			return Text;
		}

		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IConsole> m_pConsole;
		std::unique_ptr<IConfigManager> m_pConfigManager;
	};
}

TEST_F(CQmRemovedUiConfig, LegacySwitchIsNotRegistered)
{
	EXPECT_EQ(m_pConsole->GetCommandInfo("qm_new_ui", CFGFLAG_CLIENT, false), nullptr);
}

TEST_F(CQmRemovedUiConfig, SavingLegacyFileDropsUiSwitchAndPreservesOtherSettings)
{
	const char aLegacyConfig[] =
		"qm_new_ui 0\n"
		"QM_NEW_UI 1\n"
		"\tqm_new_ui\t1\n"
		"qm_new_ui_extra 2\n"
		"qm_future_setting 7\n"
		"cl_showhud 0\n";
	IOHANDLE File = m_pStorage->OpenFile("legacy-ui.cfg", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, aLegacyConfig, sizeof(aLegacyConfig) - 1), sizeof(aLegacyConfig) - 1);
	ASSERT_EQ(io_close(File), 0);

	m_pConsole->SetUnknownCommandCallback([](const char *pCommand, void *pUser) {
		static_cast<IConfigManager *>(pUser)->StoreUnknownCommand(pCommand);
		return true;
	},
		m_pConfigManager.get());
	ASSERT_TRUE(m_pConsole->ExecuteFile("legacy-ui.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE));
	EXPECT_EQ(g_Config.m_ClShowhud, 0);

	for(int SaveIndex = 0; SaveIndex < 2; ++SaveIndex)
	{
		SCOPED_TRACE(SaveIndex);
		ASSERT_TRUE(m_pConfigManager->Save());
		const std::string Saved = ReadSavedConfig();
		EXPECT_EQ(Saved.find("qm_new_ui 0"), std::string::npos);
		EXPECT_EQ(Saved.find("QM_NEW_UI 1"), std::string::npos);
		EXPECT_EQ(Saved.find("qm_new_ui\t1"), std::string::npos);
		EXPECT_NE(Saved.find("qm_new_ui_extra 2"), std::string::npos);
		EXPECT_NE(Saved.find("qm_future_setting 7"), std::string::npos);
		EXPECT_NE(Saved.find("cl_showhud 0"), std::string::npos);
	}
}
