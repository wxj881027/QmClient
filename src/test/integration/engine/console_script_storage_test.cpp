#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	class CConsoleScriptSession
	{
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole;

		static void RecordValue(IConsole::IResult *pResult, void *pUser)
		{
			static_cast<std::vector<int> *>(pUser)->push_back(pResult->GetInteger(0));
		}

	public:
		CConsoleScriptSession(IStorage *pStorage, int Flags, std::vector<int> &vValues) :
			m_pConsole(CreateConsole(Flags))
		{
			m_pKernel->RegisterInterface(pStorage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pConsole->Init();
			m_pConsole->StoreCommands(false);
			m_pConsole->Register("script_value", "i[value]", CFGFLAG_CLIENT | CFGFLAG_SERVER, RecordValue, &vValues, "");
		}

		IConsole *Console() { return m_pConsole.get(); }
	};

	class CConsoleScriptStorage : public testing::Test
	{
	protected:
		const std::filesystem::path m_PreviousCurrentDirectory = std::filesystem::current_path();
		CTestInfo m_Info;
		std::filesystem::path m_Root;
		std::filesystem::path m_Save;
		std::filesystem::path m_ExecutableDirectory;
		std::filesystem::path m_WorkingDirectory;
		std::unique_ptr<IStorage> m_pStorage;
		std::vector<int> m_vValues;
		std::unique_ptr<CConsoleScriptSession> m_pSession;

		void SetUp() override
		{
			// 所有路径均归属本项测试，程序目录与工作目录刻意分开。
			auto pInitialStorage = m_Info.CreateTestStorage();
			ASSERT_NE(pInitialStorage, nullptr);
			m_Root = std::filesystem::absolute(m_Info.StoragePath());
			m_Save = m_Root / "save";
			m_ExecutableDirectory = m_Root / "client files";
			m_WorkingDirectory = m_Root / "working";
			std::filesystem::create_directories(m_Save);
			std::filesystem::create_directories(m_ExecutableDirectory / "data/mapres");
			std::filesystem::create_directories(m_WorkingDirectory);
			std::filesystem::current_path(m_WorkingDirectory);
			const std::string Save = m_Save.string();
			const std::string Executable = (m_ExecutableDirectory / "client.exe").string();
			const char *apArgs[] = {Executable.c_str()};
			m_pStorage = CreateTempStorage(Save.c_str(), 1, apArgs);
			ASSERT_NE(m_pStorage, nullptr);
			m_pSession = std::make_unique<CConsoleScriptSession>(m_pStorage.get(), CFGFLAG_CLIENT, m_vValues);
		}

		void TearDown() override
		{
			m_pSession.reset();
			m_pStorage.reset();
			std::filesystem::current_path(m_PreviousCurrentDirectory);
		}

		void Write(const std::filesystem::path &Path, std::string_view Text)
		{
			std::filesystem::create_directories(Path.parent_path());
			std::ofstream File(Path, std::ios::binary);
			ASSERT_TRUE(File.is_open());
			File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
			ASSERT_TRUE(File.good());
		}

		IConsole *Console() { return m_pSession->Console(); }
	};
}

TEST_F(CConsoleScriptStorage, ClientExecLoadsProgramSiblingWithoutAddingAResourceSearchPath)
{
	Write(m_ExecutableDirectory / "keys.cfg", "script_value 17\n");
	const int NumPaths = m_pStorage->NumPaths();
	Console()->ExecuteLine("exec keys.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{17}));
	EXPECT_EQ(m_pStorage->NumPaths(), NumPaths);
	EXPECT_FALSE(m_pStorage->FileExists("keys.cfg", IStorage::TYPE_ALL));
	EXPECT_FALSE(m_pStorage->FileExists("keys.cfg", IStorage::TYPE_SAVE));
}

TEST_F(CConsoleScriptStorage, ExistingSavedScriptKeepsPriorityOverProgramSibling)
{
	Write(m_Save / "keys.cfg", "script_value 11\n");
	Write(m_ExecutableDirectory / "keys.cfg", "script_value 22\n");
	Console()->ExecuteLine("exec keys.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{11}));
}

TEST_F(CConsoleScriptStorage, ExistingSearchPathKeepsPriorityOverProgramSibling)
{
	Write(m_WorkingDirectory / "keys.cfg", "script_value 33\n");
	Write(m_ExecutableDirectory / "keys.cfg", "script_value 22\n");
	Console()->ExecuteLine("exec keys.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{33}));
}

TEST_F(CConsoleScriptStorage, NestedExecResolvesProgramRelativeSubdirectory)
{
	Write(m_ExecutableDirectory / "outer.cfg", "script_value 1\nexec cfg/inner.cfg\nscript_value 3\n");
	Write(m_ExecutableDirectory / "cfg/inner.cfg", "script_value 2\n");
	Console()->ExecuteLine("exec outer.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{1, 2, 3}));
}

TEST_F(CConsoleScriptStorage, MissingScriptCanBeCreatedAndRetriedWithoutStaleRecursionState)
{
	Console()->ExecuteLine("exec later.cfg");
	EXPECT_TRUE(m_vValues.empty());
	Write(m_ExecutableDirectory / "later.cfg", "script_value 4\n");
	Console()->ExecuteLine("exec later.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{4}));
}

TEST_F(CConsoleScriptStorage, RecursiveSiblingExecRetainsTheExistingRecursionGuard)
{
	Write(m_ExecutableDirectory / "loop.cfg", "exec loop.cfg\nscript_value 5\n");
	Console()->ExecuteLine("exec loop.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{5}));
}

TEST_F(CConsoleScriptStorage, ToggleExecUsesTheSameProgramSiblingFallback)
{
	Write(m_ExecutableDirectory / "press.cfg", "script_value 6\n");
	Write(m_ExecutableDirectory / "release.cfg", "script_value 7\n");
	Console()->ExecuteLine("+toggle_exec press.cfg release.cfg");
	EXPECT_EQ(m_vValues, (std::vector<int>{6, 7}));
}

TEST_F(CConsoleScriptStorage, FailedScriptStopsItsCommandsAndCanBeRetried)
{
	bool Fail = true;
	Console()->Register("script_may_fail", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		if(*static_cast<bool *>(pUser))
			throw std::bad_alloc();
	}, &Fail, "");
	Write(m_Save / "retry.cfg", "script_value 1\nscript_may_fail\nscript_value 2\n");

	EXPECT_FALSE(Console()->ExecuteFile("retry.cfg"));
	EXPECT_EQ(m_vValues, (std::vector<int>{1}));

	Fail = false;
	EXPECT_TRUE(Console()->ExecuteFile("retry.cfg"));
	EXPECT_EQ(m_vValues, (std::vector<int>{1, 1, 2}));
}

TEST_F(CConsoleScriptStorage, FailingIncludedScriptReturnsControlToItsParent)
{
	Console()->Register("script_failure", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {
		throw std::bad_alloc();
	}, nullptr, "");
	Write(m_Save / "outer.cfg", "script_value 1\nexec inner.cfg\nscript_value 3\n");
	Write(m_Save / "inner.cfg", "script_failure\nscript_value 2\n");

	EXPECT_NO_THROW(Console()->ExecuteLine("exec outer.cfg"));
	EXPECT_EQ(m_vValues, (std::vector<int>{1, 3}));
}

TEST_F(CConsoleScriptStorage, NonCommandAllStorageLoadingDoesNotUseProgramSiblingFallback)
{
	Write(m_ExecutableDirectory / "autoexec.cfg", "script_value 14\n");
	EXPECT_FALSE(Console()->ExecuteFile("autoexec.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_ALL));
	EXPECT_TRUE(m_vValues.empty());
}

TEST_F(CConsoleScriptStorage, SaveOnlyLoadingDoesNotUseProgramSiblingFallback)
{
	Write(m_ExecutableDirectory / "autoexec.cfg", "script_value 8\n");
	Write(m_ExecutableDirectory / "qmclient/settings.cfg", "script_value 9\n");
	EXPECT_FALSE(Console()->ExecuteFile("autoexec.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE));
	EXPECT_FALSE(Console()->ExecuteFile("qmclient/settings.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_vValues.empty());
}

TEST_F(CConsoleScriptStorage, AbsoluteStorageTypeKeepsItsExplicitPathRules)
{
	Write(m_ExecutableDirectory / "keys.cfg", "script_value 10\n");
	EXPECT_FALSE(Console()->ExecuteFile("keys.cfg", IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_ABSOLUTE));
	const std::string Absolute = (m_ExecutableDirectory / "keys.cfg").string();
	EXPECT_TRUE(Console()->ExecuteFile(Absolute.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_ABSOLUTE));
	EXPECT_EQ(m_vValues, (std::vector<int>{10}));
}

TEST_F(CConsoleScriptStorage, ServerConsoleDoesNotUseProgramSiblingFallback)
{
	Write(m_ExecutableDirectory / "server.cfg", "script_value 12\n");
	CConsoleScriptSession Server(m_pStorage.get(), CFGFLAG_SERVER, m_vValues);
	Server.Console()->ExecuteLine("exec server.cfg");
	EXPECT_TRUE(m_vValues.empty());
}

TEST_F(CConsoleScriptStorage, RelativeExecDoesNotEscapeStorageOrProgramDirectories)
{
	Write(m_Root / "outside.cfg", "script_value 13\n");
	Console()->ExecuteLine("exec ../outside.cfg");
	Console()->ExecuteLine("exec ..\\outside.cfg");
	EXPECT_TRUE(m_vValues.empty());
}

TEST_F(CConsoleScriptStorage, ProgramPathRejectsTruncationAndClearsFailedOutput)
{
	char aPath[4] = "old";
	EXPECT_FALSE(m_pStorage->GetExecutablePath("keys.cfg", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "");
}
