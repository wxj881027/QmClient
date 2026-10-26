#include <base/fs.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cerrno>
#include <map>
#include <string>
#include <utility>

#if defined(CONF_FAMILY_UNIX)
#include <unistd.h>
#endif

namespace
{
	using EResult = EFsListDirectoryResult;
	struct SVisitor
	{
		std::map<std::string, std::pair<int, int>> m_Entries;
		int m_Calls = 0;
		bool m_Stop = false;
		bool m_Throw = false;
		bool m_SetErrno = false;
	};
	int Visit(const char *pName, int IsDir, int Type, void *pUser)
	{
		auto &Visitor = *static_cast<SVisitor *>(pUser);
		if(str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
			return 0;
		++Visitor.m_Calls;
		Visitor.m_Entries.emplace(pName, std::make_pair(IsDir, Type));
		if(Visitor.m_Throw)
			throw 7;
		if(Visitor.m_SetErrno)
			errno = EIO;
		return Visitor.m_Stop;
	}

	class CCheckedDirectoryTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void Write(const char *pPath)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Path(const char *pPath)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aPath, sizeof(aPath));
			return aPath;
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("checked_empty", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("checked_files", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("checked_files/child", IStorage::TYPE_SAVE));
			Write("checked_files/皮肤.png");
		}
	};

	class CCheckedStorageDirectoryTest : public CCheckedDirectoryTest
	{
	protected:
		std::unique_ptr<IStorage> m_pLayered;
		std::string m_Relative;
		std::string m_SaveDirectory;
		std::string m_DataDirectory;

		void SetUp() override
		{
			CCheckedDirectoryTest::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			for(const char *pPath : {"layer_save", "layer_app", "layer_app/data", "layer_app/data/mapres"})
				ASSERT_TRUE(m_pStorage->CreateFolder(pPath, IStorage::TYPE_SAVE));
			m_SaveDirectory = Path("layer_save");
			m_DataDirectory = Path("layer_app/data");
			const auto Executable = Path("layer_app/client.exe");
			const char *apArgs[] = {Executable.c_str()};
			m_pLayered = CreateTempStorage(m_SaveDirectory.c_str(), 1, apArgs);
			ASSERT_NE(m_pLayered, nullptr);
			m_Relative = std::string("checked_") + m_Info.m_aFilename;
			ASSERT_EQ(fs_makedir((m_SaveDirectory + "/" + m_Relative).c_str()), 0);
			ASSERT_EQ(fs_makedir((m_DataDirectory + "/" + m_Relative).c_str()), 0);
		}
		void WriteAbsolute(const std::string &Path)
		{
			IOHANDLE File = io_open(Path.c_str(), IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CCheckedDirectoryTest, MissingDirectoryIsDistinctFromEmptySuccess)
{
	SVisitor Missing, Empty;
	EXPECT_EQ(fs_listdir_checked(Path("checked_missing").c_str(), Visit, 42, &Missing), EResult::NOT_FOUND);
	EXPECT_EQ(Missing.m_Calls, 0);
	EXPECT_EQ(fs_listdir_checked(Path("checked_empty").c_str(), Visit, 42, &Empty), EResult::SUCCESS);
	EXPECT_TRUE(Empty.m_Entries.empty());
}

TEST_F(CCheckedDirectoryTest, FileUsedAsDirectoryReturnsIoFailureWithoutEntries)
{
	SVisitor Visitor;
	EXPECT_EQ(fs_listdir_checked(Path("checked_files/皮肤.png").c_str(), Visit, 42, &Visitor), EResult::IO_FAILURE);
	EXPECT_EQ(Visitor.m_Calls, 0);
}

TEST_F(CCheckedDirectoryTest, NativeEnumerationKeepsUtf8NamesDirectoryFlagsAndCallerType)
{
	SVisitor Visitor;
	EXPECT_EQ(fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Visitor), EResult::SUCCESS);
	ASSERT_EQ(Visitor.m_Entries.size(), 2u);
	EXPECT_EQ(Visitor.m_Entries.at("child"), std::make_pair(1, 42));
	EXPECT_EQ(Visitor.m_Entries.at("皮肤.png"), std::make_pair(0, 42));
}

TEST_F(CCheckedDirectoryTest, ExplicitCallbackStopIsDistinctFromReadFailure)
{
	SVisitor Visitor;
	Visitor.m_Stop = true;
	EXPECT_EQ(fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Visitor), EResult::STOPPED);
	EXPECT_EQ(Visitor.m_Calls, 1);
}

TEST_F(CCheckedDirectoryTest, CallbackExceptionPropagatesAndNextEnumerationCanSucceed)
{
	SVisitor Throwing;
	Throwing.m_Throw = true;
	EXPECT_THROW(fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Throwing), int);
	SVisitor Next;
	EXPECT_EQ(fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Next), EResult::SUCCESS);
	EXPECT_EQ(Next.m_Entries.size(), 2u);
}

TEST_F(CCheckedDirectoryTest, OversizedPathIsRejectedWithoutCallingVisitor)
{
	SVisitor Visitor;
	const std::string TooLong(IO_MAX_PATH_LENGTH, 'x');
	EXPECT_EQ(fs_listdir_checked(TooLong.c_str(), Visit, 42, &Visitor), EResult::IO_FAILURE);
	EXPECT_EQ(Visitor.m_Calls, 0);
}

TEST_F(CCheckedDirectoryTest, LegacyAndCheckedNormalEnumerationReturnSameNamesAndKinds)
{
	SVisitor Legacy, Checked;
	fs_listdir(Path("checked_files").c_str(), Visit, 42, &Legacy);
	EXPECT_EQ(fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Checked), EResult::SUCCESS);
	EXPECT_EQ(Legacy.m_Entries, Checked.m_Entries);
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(CCheckedDirectoryTest, CallbackErrnoDoesNotTurnNormalReaddirEndIntoFailure)
{
	const int Previous = errno;
	SVisitor Visitor;
	Visitor.m_SetErrno = true;
	const auto Result = fs_listdir_checked(Path("checked_files").c_str(), Visit, 42, &Visitor);
	errno = Previous;
	EXPECT_EQ(Result, EResult::SUCCESS);
}
#endif

TEST_F(CCheckedStorageDirectoryTest, NativeStorageKeepsFirstRootNameShadowing)
{
	WriteAbsolute(m_SaveDirectory + "/" + m_Relative + "/shared");
	ASSERT_EQ(fs_makedir((m_DataDirectory + "/" + m_Relative + "/shared").c_str()), 0);
	WriteAbsolute(m_DataDirectory + "/" + m_Relative + "/data_only");
	SVisitor Visitor;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ALL, m_Relative.c_str(), Visit, &Visitor), EResult::SUCCESS);
	EXPECT_EQ(Visitor.m_Entries.at("shared"), std::make_pair(0, IStorage::TYPE_SAVE));
	EXPECT_TRUE(Visitor.m_Entries.count("data_only"));
}

TEST_F(CCheckedStorageDirectoryTest, MissingOptionalRootDoesNotHideAvailableLaterRoot)
{
	ASSERT_EQ(fs_removedir((m_SaveDirectory + "/" + m_Relative).c_str()), 0);
	WriteAbsolute(m_DataDirectory + "/" + m_Relative + "/data_only");
	SVisitor Visitor;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ALL, m_Relative.c_str(), Visit, &Visitor), EResult::SUCCESS);
	EXPECT_TRUE(Visitor.m_Entries.count("data_only"));
}

TEST_F(CCheckedStorageDirectoryTest, FirstRootIoFailureIsNotHiddenBySuccessfulLaterRoot)
{
	ASSERT_EQ(fs_removedir((m_SaveDirectory + "/" + m_Relative).c_str()), 0);
	WriteAbsolute(m_SaveDirectory + "/" + m_Relative);
	WriteAbsolute(m_DataDirectory + "/" + m_Relative + "/data_only");
	SVisitor Visitor;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ALL, m_Relative.c_str(), Visit, &Visitor), EResult::IO_FAILURE);
	EXPECT_TRUE(Visitor.m_Entries.empty());
}

TEST_F(CCheckedStorageDirectoryTest, StopEndsCheckedStorageSearchAcrossRoots)
{
	WriteAbsolute(m_SaveDirectory + "/" + m_Relative + "/save_only");
	WriteAbsolute(m_DataDirectory + "/" + m_Relative + "/data_only");
	SVisitor Visitor;
	Visitor.m_Stop = true;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ALL, m_Relative.c_str(), Visit, &Visitor), EResult::STOPPED);
	EXPECT_EQ(Visitor.m_Calls, 1);
}

TEST_F(CCheckedStorageDirectoryTest, JoinedPathOverflowReturnsFailureInsteadOfTruncatedDirectory)
{
	SVisitor Visitor;
	const std::string TooLong(IO_MAX_PATH_LENGTH, 'x');
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_SAVE, TooLong.c_str(), Visit, &Visitor), EResult::IO_FAILURE);
	EXPECT_EQ(Visitor.m_Calls, 0);
}

TEST_F(CCheckedStorageDirectoryTest, AbsoluteDirectorySearchPreservesRequestedType)
{
	WriteAbsolute(m_SaveDirectory + "/" + m_Relative + "/entry");
	std::string Absolute = m_SaveDirectory + "/" + m_Relative;
	if(fs_is_relative_path(Absolute.c_str()))
	{
		char aCurrent[IO_MAX_PATH_LENGTH];
		ASSERT_TRUE(fs_getcwd(aCurrent, sizeof(aCurrent)));
		Absolute = std::string(aCurrent) + "/" + Absolute;
	}
	SVisitor Visitor;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ABSOLUTE, Absolute.c_str(), Visit, &Visitor), EResult::SUCCESS);
	EXPECT_EQ(Visitor.m_Entries.at("entry"), std::make_pair(0, IStorage::TYPE_ABSOLUTE));
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(CCheckedStorageDirectoryTest, BrokenLinkStillShadowsSameNameFromLaterRoot)
{
	const auto Link = m_SaveDirectory + "/" + m_Relative + "/shared";
	ASSERT_EQ(symlink("missing_link_target", Link.c_str()), 0);
	WriteAbsolute(m_DataDirectory + "/" + m_Relative + "/shared");
	SVisitor Visitor;
	EXPECT_EQ(m_pLayered->ListDirectoryChecked(IStorage::TYPE_ALL, m_Relative.c_str(), Visit, &Visitor), EResult::SUCCESS);
	EXPECT_EQ(Visitor.m_Entries.at("shared"), std::make_pair(0, IStorage::TYPE_SAVE));
}
#endif
