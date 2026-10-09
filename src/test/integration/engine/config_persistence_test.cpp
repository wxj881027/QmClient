#include <base/windows.h>

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
#include <string_view>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

namespace
{
	constexpr const char *SETTINGS_PATH = "qmclient/settings.cfg";

	class CConfigSession
	{
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole{CreateConsole(CFGFLAG_CLIENT)};
		std::unique_ptr<IConfigManager> m_pConfig{CreateConfigManager()};

	public:
		explicit CConfigSession(IStorage *pStorage)
		{
			m_pKernel->RegisterInterface(pStorage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfig.get(), false);
			m_pConsole->Init();
			m_pConfig->Init();
			m_pConsole->StoreCommands(false);
			g_Config.m_ClSaveSettings = 1;
		}

		IConfigManager *Config() { return m_pConfig.get(); }
		bool Load(const char *pPath = SETTINGS_PATH)
		{
			return m_pConsole->ExecuteFile(pPath, IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE, true);
		}
	};

	class CConfigPersistence : public testing::Test
	{
	protected:
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}

		void TearDown() override { g_Config = m_PreviousConfig; }

		void Write(const char *pPath, std::string_view Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pPath = SETTINGS_PATH)
		{
			void *pData = nullptr;
			unsigned Length = 0;
			const bool Success = m_pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pData, &Length);
			std::unique_ptr<void, decltype(&free)> pContent(pData, free);
			EXPECT_TRUE(Success);
			return Success ? std::string(static_cast<const char *>(pData), Length) : std::string();
		}
	};
}

TEST_F(CConfigPersistence, RepeatedSaveUpdatesOwnBaselineAndReloadsSettings)
{
	CConfigSession Session(m_pStorage.get());
	g_Config.m_ClShowhud = 0;
	ASSERT_TRUE(Session.Config()->Save());
	EXPECT_NE(Read().find("cl_showhud 0"), std::string::npos);
	g_Config.m_ClShowhud = 1;
	ASSERT_TRUE(Session.Config()->Save());
	EXPECT_EQ(Read().find("cl_showhud 0"), std::string::npos);

	CConfigSession Reloaded(m_pStorage.get());
	ASSERT_TRUE(Reloaded.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, 1);
}

TEST_F(CConfigPersistence, DisabledSaveLeavesSettingsUntilExplicitForcedSave)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	CConfigSession Session(m_pStorage.get());
	ASSERT_TRUE(Session.Load());
	g_Config.m_ClShowhud = 1;
	g_Config.m_ClSaveSettings = 0;
	EXPECT_TRUE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 0\n");
	ASSERT_TRUE(Session.Config()->Save(true));
	EXPECT_EQ(Read().find("cl_showhud 0"), std::string::npos);
}

TEST_F(CConfigPersistence, StaleSessionCannotOverwriteNewerSaveEvenWhenForced)
{
	CConfigSession First(m_pStorage.get());
	CConfigSession Stale(m_pStorage.get());
	// 两个管理器保存独立的启动基线；全局配置值显式切换为对应实例的内存值。
	g_Config.m_ClShowhud = 0;
	ASSERT_TRUE(First.Config()->Save());
	const std::string Newer = Read();
	g_Config.m_ClShowhud = 1;
	EXPECT_FALSE(Stale.Config()->Save());
	EXPECT_FALSE(Stale.Config()->Save(true));
	EXPECT_FALSE(Stale.Config()->Save());
	EXPECT_EQ(Read(), Newer);
}

TEST_F(CConfigPersistence, NewSessionCanLoadAndSaveAfterAnotherSessionWrites)
{
	CConfigSession First(m_pStorage.get());
	g_Config.m_ClShowhud = 0;
	ASSERT_TRUE(First.Config()->Save());

	CConfigSession Reloaded(m_pStorage.get());
	ASSERT_TRUE(Reloaded.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, 0);
	g_Config.m_ClShowhud = 1;
	ASSERT_TRUE(Reloaded.Config()->Save());
	EXPECT_EQ(Read().find("cl_showhud 0"), std::string::npos);
}

TEST_F(CConfigPersistence, NewlyCreatedExternalSettingsAreNotOverwritten)
{
	CConfigSession Session(m_pStorage.get());
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 0\n");
}

TEST_F(CConfigPersistence, SameLengthExternalChangeIsNotOverwritten)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	CConfigSession Session(m_pStorage.get());
	ASSERT_TRUE(Session.Load());
	Write(SETTINGS_PATH, "cl_showhud 1\n");
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 1\n");
}

TEST_F(CConfigPersistence, DeletedExternalSettingsAreNotRecreatedByStaleSession)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	CConfigSession Session(m_pStorage.get());
	ASSERT_TRUE(Session.Load());
	ASSERT_TRUE(m_pStorage->RemoveFile(SETTINGS_PATH, IStorage::TYPE_SAVE));
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_FALSE(m_pStorage->FileExists(SETTINGS_PATH, IStorage::TYPE_SAVE));
}

TEST_F(CConfigPersistence, CommandDomainConflictPreventsOverwritingMainSettings)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	Write("qmclient/qmclient_profiles.cfg", "echo original\n");
	CConfigSession Session(m_pStorage.get());
	Write("qmclient/qmclient_profiles.cfg", "echo external\n");
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 0\n");
	EXPECT_EQ(Read("qmclient/qmclient_profiles.cfg"), "echo external\n");
}

TEST_F(CConfigPersistence, InvalidUtf8RejectsWholeLoadAndProtectsOriginal)
{
	const std::string Invalid = "cl_showhud 0\nplayer_name \"bad\xff\"\n";
	Write(SETTINGS_PATH, Invalid);
	CConfigSession Session(m_pStorage.get());
	EXPECT_FALSE(Session.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, DefaultConfig::ClShowhud);
	EXPECT_FALSE(Session.Config()->Save(true));
	EXPECT_EQ(Read(), Invalid);
}

TEST_F(CConfigPersistence, ControlCharacterRejectsWholeLoadAndProtectsOriginal)
{
	const std::string Invalid = "cl_showhud 0\necho bad\x01\n";
	Write(SETTINGS_PATH, Invalid);
	CConfigSession Session(m_pStorage.get());
	EXPECT_FALSE(Session.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, DefaultConfig::ClShowhud);
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), Invalid);
}

TEST_F(CConfigPersistence, NullByteRejectsWholeLoadAndProtectsOriginal)
{
	std::string Invalid = "cl_showhud 0\n";
	Invalid.push_back('\0');
	Invalid += "echo ignored\n";
	Write(SETTINGS_PATH, Invalid);
	CConfigSession Session(m_pStorage.get());
	EXPECT_FALSE(Session.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, DefaultConfig::ClShowhud);
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), Invalid);
}

TEST_F(CConfigPersistence, ValidBomCrLfAndChineseTextLoadAndSave)
{
	Write(SETTINGS_PATH, "\xef\xbb\xbf" "cl_showhud 0\r\nplayer_name \"中文名字\"\r\n");
	CConfigSession Session(m_pStorage.get());
	ASSERT_TRUE(Session.Load());
	EXPECT_EQ(g_Config.m_ClShowhud, 0);
	EXPECT_STREQ(g_Config.m_PlayerName, "中文名字");
	ASSERT_TRUE(Session.Config()->Save());
	EXPECT_NE(Read().find("中文名字"), std::string::npos);
}

TEST_F(CConfigPersistence, LegacyConfigCanBeLoadedAndSavedToMergedFile)
{
	Write("settings_qmclient.cfg", "tc_show_chat_client 0\n");
	CConfigSession Session(m_pStorage.get());
	ASSERT_TRUE(Session.Load("settings_qmclient.cfg"));
	ASSERT_TRUE(Session.Config()->Save(true));
	EXPECT_NE(Read().find("qm_show_chat_client 0"), std::string::npos);
	EXPECT_EQ(Read().find("tc_show_chat_client"), std::string::npos);
}

TEST_F(CConfigPersistence, StagingFailureKeepsAllExistingConfigFiles)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	Write("qmclient/qmclient_profiles.cfg", "echo original\n");
	CConfigSession Session(m_pStorage.get());
	char aTempPath[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTempPath, sizeof(aTempPath), "qmclient/qmclient_profiles.cfg");
	ASSERT_TRUE(m_pStorage->CreateFolder(aTempPath, IStorage::TYPE_SAVE));
	g_Config.m_ClShowhud = 1;
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 0\n");
	EXPECT_EQ(Read("qmclient/qmclient_profiles.cfg"), "echo original\n");
	ASSERT_TRUE(m_pStorage->RemoveFolder(aTempPath, IStorage::TYPE_SAVE));
	EXPECT_TRUE(Session.Config()->Save());
}

TEST_F(CConfigPersistence, ChangeDuringSerializationKeepsExternalSettings)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	CConfigSession Session(m_pStorage.get());
	Session.Config()->RegisterCallback([](IConfigManager *, void *pUser) {
		auto *pStorage = static_cast<IStorage *>(pUser);
		IOHANDLE File = pStorage->OpenFile(SETTINGS_PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		const char *pContent = "cl_showhud 1\n";
		EXPECT_EQ(io_write(File, pContent, str_length(pContent)), static_cast<unsigned>(str_length(pContent)));
		EXPECT_EQ(io_close(File), 0);
	}, m_pStorage.get());
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 1\n");
}

TEST_F(CConfigPersistence, ConcurrentWriterIsRejectedAndLockIsReleasedAfterSave)
{
	CConfigSession First(m_pStorage.get());
	CConfigSession Other(m_pStorage.get());
	struct SContext
	{
		IConfigManager *m_pOther;
		bool m_Attempted = false;
		bool m_Result = true;
	} Context{Other.Config()};
	First.Config()->RegisterCallback([](IConfigManager *, void *pUser) {
		auto &Context = *static_cast<SContext *>(pUser);
		Context.m_Attempted = true;
		Context.m_Result = Context.m_pOther->Save();
	}, &Context);
	ASSERT_TRUE(First.Config()->Save());
	EXPECT_TRUE(Context.m_Attempted);
	EXPECT_FALSE(Context.m_Result);
	// 保存返回后锁必须已释放，本实例可以再次保存。
	EXPECT_TRUE(First.Config()->Save());
}

TEST_F(CConfigPersistence, ReentrantSaveDoesNotDisturbOuterStagedFiles)
{
	CConfigSession Session(m_pStorage.get());
	bool NestedResult = true;
	Session.Config()->RegisterCallback([](IConfigManager *pConfig, void *pUser) {
		*static_cast<bool *>(pUser) = pConfig->Save();
	}, &NestedResult);
	g_Config.m_ClShowhud = 0;
	ASSERT_TRUE(Session.Config()->Save());
	EXPECT_FALSE(NestedResult);
	EXPECT_NE(Read().find("cl_showhud 0"), std::string::npos);
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CConfigPersistence, ReadOnlyTargetRemainsInPlaceWhenReplacementFails)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	CConfigSession Session(m_pStorage.get());
	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, SETTINGS_PATH, aPath, sizeof(aPath));
	const std::wstring Path = windows_utf8_to_wide(aPath);
	ASSERT_NE(SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_READONLY), FALSE);
	struct SRestoreAttributes
	{
		const std::wstring &m_Path;
		~SRestoreAttributes() { SetFileAttributesW(m_Path.c_str(), FILE_ATTRIBUTE_NORMAL); }
	} Restore{Path};
	g_Config.m_ClShowhud = 1;
	EXPECT_FALSE(Session.Config()->Save());
	EXPECT_EQ(Read(), "cl_showhud 0\n");
	EXPECT_NE(GetFileAttributesW(Path.c_str()) & FILE_ATTRIBUTE_READONLY, 0u);
	char aTempPath[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTempPath, sizeof(aTempPath), SETTINGS_PATH);
	EXPECT_TRUE(m_pStorage->FileExists(aTempPath, IStorage::TYPE_SAVE));
	ASSERT_NE(SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_NORMAL), FALSE);
	EXPECT_TRUE(Session.Config()->Save());
}

TEST_F(CConfigPersistence, ExclusiveExternalReaderCausesLoadAndSaveFailureWithoutDataLoss)
{
	Write(SETTINGS_PATH, "cl_showhud 0\n");
	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, SETTINGS_PATH, aPath, sizeof(aPath));
	const std::wstring Path = windows_utf8_to_wide(aPath);
	const HANDLE File = CreateFileW(Path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(File, INVALID_HANDLE_VALUE);
	struct SCloseHandle
	{
		HANDLE m_File;
		~SCloseHandle()
		{
			if(m_File != INVALID_HANDLE_VALUE)
				CloseHandle(m_File);
		}
	} Close{File};
	CConfigSession Session(m_pStorage.get());
	EXPECT_FALSE(Session.Load());
	EXPECT_FALSE(Session.Config()->Save(true));
	ASSERT_NE(CloseHandle(Close.m_File), FALSE);
	Close.m_File = INVALID_HANDLE_VALUE;
	EXPECT_EQ(Read(), "cl_showhud 0\n");
	CConfigSession Reloaded(m_pStorage.get());
	EXPECT_TRUE(Reloaded.Load());
	EXPECT_TRUE(Reloaded.Config()->Save());
}
#endif
