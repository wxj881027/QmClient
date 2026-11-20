#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	class CAllocationConfigSession
	{
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole{CreateConsole(CFGFLAG_CLIENT)};
		CConfigManager m_Config{CHeapAllocationProbe::Allocate};

	public:
		explicit CAllocationConfigSession(IStorage &Storage)
		{
			m_pKernel->RegisterInterface(&Storage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface<IConfigManager>(&m_Config, false);
			m_pConsole->Init();
		}
		void Init()
		{
			m_Config.Init();
			m_pConsole->StoreCommands(false);
		}
		IConfigManager &Config() { return m_Config; }
		bool Load() { return m_pConsole->ExecuteFile("qmclient/settings.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE, true); }
	};
	class ConfigAllocationFailure : public testing::Test
	{
		CConfig m_Previous = g_Config;
		CTestInfo m_Info;

	protected:
		CHeapAllocationProbe m_Probe;
		std::unique_ptr<IStorage> m_pStorage;
		static constexpr const char *OLD = "future_setting 7\ncl_showhud 0\n";
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			IOHANDLE File = m_pStorage->OpenFile("qmclient/settings.cfg", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, OLD, str_length(OLD)), static_cast<unsigned>(str_length(OLD)));
			EXPECT_EQ(io_close(File), 0);
		}
		void TearDown() override { g_Config = m_Previous; }
		std::string Read()
		{
			std::unique_ptr<char, decltype(&free)> Text(m_pStorage->ReadFileStr("qmclient/settings.cfg", IStorage::TYPE_SAVE), &free);
			EXPECT_NE(Text, nullptr);
			return Text ? Text.get() : "";
		}
		std::string OversizedUnknown() { return "future_large \"" + std::string(m_Probe.m_LastSize + 1, 'x') + "\""; }
	};
}

TEST_F(ConfigAllocationFailure, FirstBlockFailureThrowsAndBothSaveModesKeepDiskContents)
{
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	CAllocationConfigSession Session(*m_pStorage);
	EXPECT_THROW(Session.Init(), std::bad_alloc);
	EXPECT_FALSE(Session.Config().Save());
	EXPECT_FALSE(Session.Config().Save(true));
	EXPECT_EQ(Read(), OLD);
}

TEST_F(ConfigAllocationFailure, GrowthFailureDoesNotAllowPartialConfigurationToBeSaved)
{
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	m_Probe.m_FailAt = 2;
	CAllocationConfigSession Session(*m_pStorage);
	EXPECT_THROW(Session.Init(), std::bad_alloc);
	EXPECT_EQ(m_Probe.m_Requests, 2);
	EXPECT_FALSE(Session.Config().Save(true));
	EXPECT_EQ(Read(), OLD);
}

TEST_F(ConfigAllocationFailure, InitiallyMissingBlockCanRecoverBeforeInitialization)
{
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	CAllocationConfigSession Session(*m_pStorage);
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	ASSERT_NO_THROW(Session.Init());
	ASSERT_TRUE(Session.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, 0);
	ASSERT_TRUE(Session.Config().Save(true));
	EXPECT_NE(Read().find("future_setting 7"), std::string::npos);
}

TEST_F(ConfigAllocationFailure, LostUnknownCommandBlocksSavingEvenAfterLaterCommandsSucceed)
{
	CAllocationConfigSession Session(*m_pStorage);
	ASSERT_NO_THROW(Session.Init());
	const std::string Unknown = OversizedUnknown();
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_NO_THROW(Session.Config().StoreUnknownCommand(Unknown.c_str()));
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	Session.Config().StoreUnknownCommand("future_other 2");
	EXPECT_FALSE(Session.Config().Save());
	EXPECT_FALSE(Session.Config().Save(true));
	EXPECT_EQ(Read(), OLD);
}

TEST_F(ConfigAllocationFailure, FreshSessionCanReloadTheOriginalFileAfterUnknownCommandFailure)
{
	{
		CAllocationConfigSession Session(*m_pStorage);
		ASSERT_NO_THROW(Session.Init());
		const std::string Unknown = OversizedUnknown();
		m_Probe.m_Failure = CHeapAllocationProbe::EFailure::UNKNOWN;
		Session.Config().StoreUnknownCommand(Unknown.c_str());
		EXPECT_FALSE(Session.Config().Save(true));
	}
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	CAllocationConfigSession Fresh(*m_pStorage);
	ASSERT_NO_THROW(Fresh.Init());
	ASSERT_TRUE(Fresh.Load());
	ASSERT_TRUE(Fresh.Config().Save(true));
	EXPECT_NE(Read().find("future_setting 7"), std::string::npos);
}
