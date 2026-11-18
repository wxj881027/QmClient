// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/fs.h>
#include <base/str.h>
#include <base/system.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#include <windows.h>
#endif

TEST(Filesystem, Filename)
{
	EXPECT_STREQ(fs_filename(""), "");
	EXPECT_STREQ(fs_filename("a"), "a");
	EXPECT_STREQ(fs_filename("abc"), "abc");
	EXPECT_STREQ(fs_filename("a/b"), "b");
	EXPECT_STREQ(fs_filename("a/b/c"), "c");
	EXPECT_STREQ(fs_filename("aaaaa/bbbb/ccc"), "ccc");
	EXPECT_STREQ(fs_filename("aaaaa\\bbbb\\ccc"), "ccc");
	EXPECT_STREQ(fs_filename("aaaaa/bbbb\\ccc"), "ccc");
	EXPECT_STREQ(fs_filename("aaaaa\\bbbb/ccc"), "ccc");
}

TEST(Filesystem, SplitFileExtension)
{
	char aName[IO_MAX_PATH_LENGTH];
	char aExt[IO_MAX_PATH_LENGTH];

	fs_split_file_extension("", aName, sizeof(aName), aExt, sizeof(aExt));
	EXPECT_STREQ(aName, "");
	EXPECT_STREQ(aExt, "");

	fs_split_file_extension("name.ext", aName, sizeof(aName), aExt, sizeof(aExt));
	EXPECT_STREQ(aName, "name");
	EXPECT_STREQ(aExt, "ext");

	fs_split_file_extension("name.ext", aName, sizeof(aName)); // extension parameter is optional
	EXPECT_STREQ(aName, "name");

	fs_split_file_extension("name.ext", nullptr, 0, aExt, sizeof(aExt)); // name parameter is optional
	EXPECT_STREQ(aExt, "ext");

	fs_split_file_extension("archive.tar.gz", aName, sizeof(aName), aExt, sizeof(aExt));
	EXPECT_STREQ(aName, "archive.tar");
	EXPECT_STREQ(aExt, "gz");

	fs_split_file_extension("no_dot", aName, sizeof(aName), aExt, sizeof(aExt));
	EXPECT_STREQ(aName, "no_dot");
	EXPECT_STREQ(aExt, "");

	fs_split_file_extension("no_dot", aName, sizeof(aName)); // extension parameter is optional
	EXPECT_STREQ(aName, "no_dot");

	fs_split_file_extension("no_dot", nullptr, 0, aExt, sizeof(aExt)); // name parameter is optional
	EXPECT_STREQ(aExt, "");

	fs_split_file_extension(".dot_first", aName, sizeof(aName), aExt, sizeof(aExt));
	EXPECT_STREQ(aName, ".dot_first");
	EXPECT_STREQ(aExt, "");

	fs_split_file_extension(".dot_first", aName, sizeof(aName)); // extension parameter is optional
	EXPECT_STREQ(aName, ".dot_first");

	fs_split_file_extension(".dot_first", nullptr, 0, aExt, sizeof(aExt)); // name parameter is optional
	EXPECT_STREQ(aExt, "");
}

static void TestNormalizePath(const char *pInput, const char *pExpectedOutput)
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	str_copy(aNormalized, pInput);
	fs_normalize_path(aNormalized);
	EXPECT_STREQ(aNormalized, pExpectedOutput);
}

TEST(Filesystem, NormalizePath)
{
	TestNormalizePath("", "");
	TestNormalizePath("/", "/");
	TestNormalizePath("\\", "/");
	TestNormalizePath("//", "/");
	TestNormalizePath("/////", "/");
	TestNormalizePath("\\\\\\\\\\", "/");
	TestNormalizePath("/a/b/c", "/a/b/c");
	TestNormalizePath("\\a\\b\\c", "/a/b/c");
	TestNormalizePath("C:\\Users", "C:/Users");
	TestNormalizePath("C:\\", "C:");
}

TEST(Filesystem, StoragePath)
{
	char aStoragePath[IO_MAX_PATH_LENGTH];
	ASSERT_FALSE(fs_storage_path("TestAppName", aStoragePath, sizeof(aStoragePath)));
	EXPECT_FALSE(fs_is_relative_path(aStoragePath));
	EXPECT_TRUE(str_endswith_nocase(aStoragePath, "/TestAppName"));
}

#if defined(CONF_PLATFORM_LINUX)
class StoragePathTest : public testing::Test
{
	std::optional<std::string> m_PreviousHome;
	std::optional<std::string> m_PreviousDataHome;

	void SetUp() override
	{
		if(const char *pHome = std::getenv("HOME"))
			m_PreviousHome = pHome;
		if(const char *pDataHome = std::getenv("XDG_DATA_HOME"))
			m_PreviousDataHome = pDataHome;
		ASSERT_EQ(setenv("HOME", "/QmTestHome", 1), 0);
	}

	void TearDown() override
	{
		Restore("HOME", m_PreviousHome);
		Restore("XDG_DATA_HOME", m_PreviousDataHome);
	}

	static void Restore(const char *pName, const std::optional<std::string> &Value)
	{
		if(Value)
			EXPECT_EQ(setenv(pName, Value->c_str(), 1), 0);
		else
			EXPECT_EQ(unsetenv(pName), 0);
	}
};

TEST_F(StoragePathTest, RejectsTruncatedDataRoot)
{
	const std::string DataHome = "/QmTest/" + std::string(180, 'A') + "/" + std::string(180, 'B') + "/" + std::string(180, 'C');
	ASSERT_EQ(setenv("XDG_DATA_HOME", DataHome.c_str(), 1), 0);

	char aPath[IO_MAX_PATH_LENGTH] = "previous";
	EXPECT_EQ(fs_storage_path("DDNet", aPath, sizeof(aPath)), -1);
	EXPECT_STREQ(aPath, "");
}

TEST_F(StoragePathTest, PreservesDataRootCaseAtExactCapacity)
{
	ASSERT_EQ(setenv("XDG_DATA_HOME", "/QmTestData", 1), 0);

	char aPath[sizeof("/QmTestData/ddnet")];
	ASSERT_EQ(fs_storage_path("DDNet", aPath, sizeof(aPath)), 0);
	EXPECT_STREQ(aPath, "/QmTestData/ddnet");
}

TEST_F(StoragePathTest, IgnoresEmptyOrRelativeDataHome)
{
	for(const char *pDataHome : {"", "relative-data"})
	{
		SCOPED_TRACE(pDataHome);
		ASSERT_EQ(setenv("XDG_DATA_HOME", pDataHome, 1), 0);

		char aPath[IO_MAX_PATH_LENGTH];
		ASSERT_EQ(fs_storage_path("DDNet", aPath, sizeof(aPath)), 0);
		EXPECT_STREQ(aPath, "/QmTestHome/.local/share/ddnet");
	}
}

TEST_F(StoragePathTest, AbsoluteDataHomeWorksWithoutHome)
{
	ASSERT_EQ(unsetenv("HOME"), 0);
	ASSERT_EQ(setenv("XDG_DATA_HOME", "/QmTestData", 1), 0);

	char aPath[IO_MAX_PATH_LENGTH];
	ASSERT_EQ(fs_storage_path("DDNet", aPath, sizeof(aPath)), 0);
	EXPECT_STREQ(aPath, "/QmTestData/ddnet");
}
#endif

TEST(Filesystem, DoesNotCreateParentsForTruncatedPath)
{
	CTestInfo Info;
	const std::string Path = std::string(Info.m_aFilename) + "/" + std::string(IO_MAX_PATH_LENGTH, 'x');

	const int Result = fs_makedir_rec_for(Path.c_str());
	const bool Created = fs_is_dir(Info.m_aFilename);
	if(Created)
		EXPECT_EQ(fs_removedir(Info.m_aFilename), 0);

	EXPECT_EQ(Result, -1);
	EXPECT_FALSE(Created);
}

TEST(Filesystem, ExistingFileCannotServeAsDirectory)
{
	CTestInfo Info;
	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_close(File), 0);

	const std::string Child = std::string(Info.m_aFilename) + "/child";
	EXPECT_LT(fs_makedir(Info.m_aFilename), 0);
	EXPECT_LT(fs_makedir_rec_for(Child.c_str()), 0);
	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_EQ(fs_remove(Info.m_aFilename), 0);
}

#if defined(CONF_FAMILY_UNIX)
TEST(Filesystem, RecursiveCreationUsesUnixPathSeparators)
{
	CTestInfo Info;
	for(const char *pParentName : {"parent:", "parent\\with-backslash"})
	{
		SCOPED_TRACE(pParentName);
		const std::string Parent = std::string(Info.m_aFilename) + "/" + pParentName;
		ASSERT_EQ(fs_makedir_rec_for((Parent + "/file").c_str()), 0);
		EXPECT_TRUE(fs_is_dir(Parent.c_str()));
	}
}
#endif

TEST(Filesystem, ExecutablePath)
{
	char aExecutablePath[IO_MAX_PATH_LENGTH];
	ASSERT_FALSE(fs_executable_path(aExecutablePath, sizeof(aExecutablePath)));
	EXPECT_TRUE(fs_is_file(aExecutablePath));
	EXPECT_FALSE(fs_parent_dir(aExecutablePath));
	EXPECT_FALSE(fs_is_relative_path(aExecutablePath));
}

TEST(Filesystem, CurrentDirectoryRejectsTooSmallBuffer)
{
	char aOutput[] = {'!', '?'};
	EXPECT_EQ(fs_getcwd(aOutput, 1), nullptr);
	EXPECT_EQ(aOutput[0], '\0');
	EXPECT_EQ(aOutput[1], '?');
}

#if defined(CONF_FAMILY_WINDOWS)
TEST(Filesystem, CurrentDirectoryAtWindowsDriveRootRemainsAbsolute)
{
	wchar_t aPrevious[IO_MAX_PATH_LENGTH];
	const DWORD Length = GetCurrentDirectoryW(std::size(aPrevious), aPrevious);
	ASSERT_LT(Length, std::size(aPrevious));
	ASSERT_GE(Length, 3u);
	if(aPrevious[1] != L':' || aPrevious[2] != L'\\')
		GTEST_SKIP() << "The test requires a drive-letter working directory";

	const wchar_t aRoot[] = {aPrevious[0], L':', L'\\', L'\0'};
	ASSERT_NE(SetCurrentDirectoryW(aRoot), FALSE);
	char aDirectory[4] = {};
	const bool Read = fs_getcwd(aDirectory, sizeof(aDirectory)) == aDirectory;
	ASSERT_NE(SetCurrentDirectoryW(aPrevious), FALSE);

	const char aExpected[] = {static_cast<char>(aRoot[0]), ':', '/', '\0'};
	ASSERT_TRUE(Read);
	EXPECT_EQ(str_comp_nocase(aDirectory, aExpected), 0);
}

TEST(Filesystem, ExecutablePathIgnoresPreviousWindowsError)
{
	char aExecutablePath[IO_MAX_PATH_LENGTH];
	const DWORD previous_error = GetLastError();
	SetLastError(ERROR_ACCESS_DENIED);
	const int result = fs_executable_path(aExecutablePath, sizeof(aExecutablePath));
	SetLastError(previous_error);

	ASSERT_EQ(result, 0);
	EXPECT_TRUE(fs_is_file(aExecutablePath));
}
#endif

TEST(Filesystem, ExecutablePathRejectsTooSmallBuffer)
{
	char aOutput[] = {'!', '?'};
	EXPECT_EQ(fs_executable_path(aOutput, 1), -1);
	EXPECT_EQ(aOutput[0], '\0');
	EXPECT_EQ(aOutput[1], '?');
}

TEST(Filesystem, CreateCloseDelete)
{
	CTestInfo Info;

	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);
	EXPECT_FALSE(io_close(File));
	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_remove(Info.m_aFilename));
	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
}

TEST(Filesystem, CreateDeleteDirectory)
{
	CTestInfo Info;
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s/test.txt", Info.m_aFilename);

	EXPECT_FALSE(fs_is_dir(Info.m_aFilename));
	EXPECT_FALSE(fs_makedir(Info.m_aFilename));
	EXPECT_TRUE(fs_is_dir(Info.m_aFilename));

	IOHANDLE File = io_open(aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);
	EXPECT_FALSE(io_close(File));

	// Directory removal fails if there are any files left in the directory.
	EXPECT_TRUE(fs_removedir(Info.m_aFilename));
	EXPECT_TRUE(fs_is_dir(Info.m_aFilename));

	EXPECT_FALSE(fs_remove(aFilename));
	EXPECT_FALSE(fs_removedir(Info.m_aFilename));
	EXPECT_FALSE(fs_is_dir(Info.m_aFilename));
}

TEST(Filesystem, CantDeleteDirectoryWithRemove)
{
	CTestInfo Info;
	EXPECT_FALSE(fs_makedir(Info.m_aFilename));
	EXPECT_TRUE(fs_remove(Info.m_aFilename)); // Cannot remove directory with file removal function.
	EXPECT_FALSE(fs_removedir(Info.m_aFilename));
}

TEST(Filesystem, CantDeleteFileWithRemoveDirectory)
{
	CTestInfo Info;
	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);
	EXPECT_FALSE(io_close(File));
	EXPECT_TRUE(fs_removedir(Info.m_aFilename)); // Cannot remove file with directory removal function.
	EXPECT_FALSE(fs_remove(Info.m_aFilename));
}

TEST(Filesystem, DeleteNonexistentFile)
{
	CTestInfo Info;

	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_remove(Info.m_aFilename)); // Can delete a file that does not exist.
}

TEST(Filesystem, DeleteNonexistentDirectory)
{
	CTestInfo Info;

	EXPECT_FALSE(fs_is_dir(Info.m_aFilename));
	EXPECT_FALSE(fs_removedir(Info.m_aFilename)); // Can delete a folder that does not exist.
}

TEST(Filesystem, DeleteFromMissingParent)
{
	CTestInfo Info;
	ASSERT_FALSE(fs_is_dir(Info.m_aFilename));
	const std::string Path = std::string(Info.m_aFilename) + "/child";
	EXPECT_EQ(fs_remove(Path.c_str()), 0);
	EXPECT_EQ(fs_removedir(Path.c_str()), 0);
}

#if defined(CONF_FAMILY_WINDOWS)
static std::string ExtendedTestPath(const char *pPath)
{
	wchar_t aAbsolutePath[IO_MAX_PATH_LENGTH];
	const std::wstring WidePath = windows_utf8_to_wide(pPath);
	const DWORD Length = GetFullPathNameW(WidePath.c_str(), std::size(aAbsolutePath), aAbsolutePath, nullptr);
	if(Length == 0 || Length >= std::size(aAbsolutePath))
		return {};
	const auto AbsolutePath = windows_wide_to_utf8(aAbsolutePath);
	if(!AbsolutePath)
		return {};
	if(str_startswith(AbsolutePath->c_str(), "\\\\?\\"))
		return *AbsolutePath;
	if(str_startswith(AbsolutePath->c_str(), "\\\\"))
		return "\\\\?\\UNC\\" + AbsolutePath->substr(2);
	return "\\\\?\\" + *AbsolutePath;
}

TEST(Filesystem, CreatesParentsBelowExtendedWindowsRoot)
{
	CTestInfo Info;
	const std::string Directory = ExtendedTestPath(Info.m_aFilename);
	ASSERT_FALSE(Directory.empty());
	ASSERT_EQ(fs_makedir_rec_for((Directory + "\\parent\\file").c_str()), 0);
	EXPECT_TRUE(fs_is_dir((Directory + "\\parent").c_str()));
}

TEST(Filesystem, ListsDirectoryWithExtendedWindowsPath)
{
	CTestInfo Info;
	ASSERT_EQ(fs_makedir(Info.m_aFilename), 0);
	const std::string Directory = ExtendedTestPath(Info.m_aFilename);
	ASSERT_FALSE(Directory.empty());
	const std::string Path = Directory + "\\entry.txt";
	IOHANDLE File = io_open(Path.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_close(File), 0);

	const auto Visit = [](const char *pName, int IsDir, int, void *pUser) {
		if(!IsDir && str_comp(pName, "entry.txt") == 0)
			++*static_cast<int *>(pUser);
		return 0;
	};
	int Found = 0;
	EXPECT_EQ(fs_listdir_checked(Directory.c_str(), Visit, 0, &Found), EFsListDirectoryResult::SUCCESS);
	EXPECT_EQ(Found, 1);

	Found = 0;
	fs_listdir_fileinfo(Directory.c_str(), [](const CFsFileInfo *pInfo, int IsDir, int, void *pUser) {
		if(!IsDir && str_comp(pInfo->m_pName, "entry.txt") == 0)
			++*static_cast<int *>(pUser);
		return 0;
	}, 0, &Found);
	EXPECT_EQ(Found, 1);
}

TEST(Filesystem, RejectsInvalidUtf8BeforeCreatingParents)
{
	CTestInfo Info;
	std::string Path = std::string(Info.m_aFilename) + "/local";
	Path.push_back(static_cast<char>(0xff));
	Path += "/file";
	EXPECT_LT(fs_makedir_rec_for(Path.c_str()), 0);
	EXPECT_FALSE(fs_is_dir(Info.m_aFilename));
}

TEST(Filesystem, DeleteReadOnlyFilePreservesContentsOnFailure)
{
	CTestInfo Info;
	ASSERT_EQ(fs_makedir(Info.m_aFilename), 0);
	const std::string Path = std::string(Info.m_aFilename) + "/original.txt";
	IOHANDLE File = io_open(Path.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "old", 3), 3u);
	ASSERT_EQ(io_close(File), 0);

	const auto WidePath = windows_utf8_to_wide(Path.c_str());
	ASSERT_NE(SetFileAttributesW(WidePath.c_str(), FILE_ATTRIBUTE_READONLY), FALSE);
	EXPECT_EQ(fs_remove(Path.c_str()), 1);
	const DWORD Attributes = GetFileAttributesW(WidePath.c_str());
	EXPECT_NE(Attributes, INVALID_FILE_ATTRIBUTES);
	EXPECT_NE(Attributes & FILE_ATTRIBUTE_READONLY, 0u);
	EXPECT_NE(SetFileAttributesW(WidePath.c_str(), FILE_ATTRIBUTE_NORMAL), FALSE);

	File = io_open(Path.c_str(), IOFLAG_READ);
	ASSERT_NE(File, nullptr);
	char aContents[4] = {};
	EXPECT_EQ(io_read(File, aContents, 3), 3u);
	EXPECT_EQ(io_close(File), 0);
	EXPECT_STREQ(aContents, "old");
}

TEST(Filesystem, DeletesOpenFileAtWindowsComponentLimit)
{
	CTestInfo Info;
	ASSERT_EQ(fs_makedir(Info.m_aFilename), 0);
	const std::string Directory = ExtendedTestPath(Info.m_aFilename);
	ASSERT_FALSE(Directory.empty());
	const std::string Path = Directory + "\\" + std::string(255, 'x');
	IOHANDLE File = io_open(Path.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);

	EXPECT_EQ(fs_remove(Path.c_str()), 0);
	EXPECT_FALSE(fs_is_file(Path.c_str()));
	EXPECT_EQ(io_close(File), 0);
}
#endif

TEST(Filesystem, DeleteOpenFile)
{
	CTestInfo Info;

	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_remove(Info.m_aFilename)); // Can delete a file that has open handle.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename)); // File should be gone immediately even before the last file handle is closed.

	EXPECT_FALSE(io_close(File));
}

TEST(Filesystem, DeleteOpenFileMultipleHandles)
{
	CTestInfo Info;

	IOHANDLE FileWrite1 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite1);

	IOHANDLE FileRead1 = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(FileRead1);

	IOHANDLE FileWrite2 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite2);

	IOHANDLE FileRead2 = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(FileRead2);

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_remove(Info.m_aFilename)); // Can delete a file that has multiple open handles.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename)); // File should be gone immediately even before the last file handle is closed.

	EXPECT_FALSE(io_close(FileWrite1));
	EXPECT_FALSE(io_close(FileRead1));
	EXPECT_FALSE(io_close(FileWrite2));
	EXPECT_FALSE(io_close(FileRead2));
}

TEST(Filesystem, RenameFile)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	IOHANDLE FileWrite = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite);
	EXPECT_FALSE(io_close(FileWrite));

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename));
	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameFileCaseOnly)
{
	char aOldFilename[IO_MAX_PATH_LENGTH];
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aOldFilename, sizeof(aOldFilename), ".case.tmp");
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".CASE.tmp");

	IOHANDLE FileWrite = io_open(aOldFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite);
	EXPECT_FALSE(io_close(FileWrite));

	EXPECT_TRUE(fs_is_file(aOldFilename));
	EXPECT_FALSE(fs_rename(aOldFilename, aNewFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameOpenFileCaseOnly)
{
	char aOldFilename[IO_MAX_PATH_LENGTH];
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aOldFilename, sizeof(aOldFilename), ".case.tmp");
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".CASE.tmp");

	IOHANDLE FileWrite = io_open(aOldFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite);

	EXPECT_TRUE(fs_is_file(aOldFilename));
	EXPECT_FALSE(fs_rename(aOldFilename, aNewFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(io_close(FileWrite));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameFolder)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	EXPECT_FALSE(fs_makedir(Info.m_aFilename));

	EXPECT_TRUE(fs_is_dir(Info.m_aFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename));
	EXPECT_FALSE(fs_is_dir(Info.m_aFilename));
	EXPECT_TRUE(fs_is_dir(aNewFilename));

	EXPECT_FALSE(fs_removedir(aNewFilename));
}

TEST(Filesystem, RenameOpenFileSource)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	IOHANDLE FileWrite = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite);

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename)); // Can rename a file that has open handle.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename)); // Rename should take effect immediately.
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(io_close(FileWrite));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameOpenFileSourceMultipleHandles)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	IOHANDLE FileWrite1 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite1);

	IOHANDLE FileRead1 = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(FileRead1);

	IOHANDLE FileWrite2 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite2);

	IOHANDLE FileRead2 = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(FileRead2);

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename)); // Can rename a file that has multiple open handles.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename)); // Rename should take effect immediately.
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(io_close(FileWrite1));
	EXPECT_FALSE(io_close(FileRead1));
	EXPECT_FALSE(io_close(FileWrite2));
	EXPECT_FALSE(io_close(FileRead2));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameTargetFileExists)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	IOHANDLE FileWrite1 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite1);
	EXPECT_FALSE(io_close(FileWrite1));

	IOHANDLE FileWrite2 = io_open(aNewFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite2);
	EXPECT_FALSE(io_close(FileWrite2));

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename)); // Renaming can overwrite the existing target file if it has no open handles.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

TEST(Filesystem, RenameOpenFileDeleteTarget)
{
	char aNewFilename[IO_MAX_PATH_LENGTH];
	CTestInfo Info;
	Info.Filename(aNewFilename, sizeof(aNewFilename), ".renamed");

	IOHANDLE FileWrite1 = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite1);
	EXPECT_FALSE(io_close(FileWrite1));

	IOHANDLE FileWrite2 = io_open(aNewFilename, IOFLAG_WRITE);
	ASSERT_TRUE(FileWrite2);
	EXPECT_FALSE(io_close(FileWrite2));

	IOHANDLE FileRead = io_open(aNewFilename, IOFLAG_READ);
	ASSERT_TRUE(FileRead);

	EXPECT_TRUE(fs_is_file(Info.m_aFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));
	EXPECT_FALSE(fs_rename(Info.m_aFilename, aNewFilename)); // Renaming can overwrite the existing target file even if it has open handles.
	EXPECT_FALSE(fs_is_file(Info.m_aFilename));
	EXPECT_TRUE(fs_is_file(aNewFilename));

	EXPECT_FALSE(io_close(FileRead));

	EXPECT_FALSE(fs_remove(aNewFilename));
}

#if defined(CONF_FAMILY_WINDOWS)
TEST(Filesystem, FailedRenameKeepsExistingDestination)
{
	CTestInfo Info;
	ASSERT_EQ(fs_makedir(Info.m_aFilename), 0);
	const std::string Destination = std::string(Info.m_aFilename) + "/existing.txt";
	IOHANDLE File = io_open(Destination.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "old", 3), 3u);
	ASSERT_EQ(io_close(File), 0);

	// 目录不能移动到自己的子路径。底层文件系统可能报告不同的错误。
	const auto WideSource = windows_utf8_to_wide(Info.m_aFilename);
	const auto WideDestination = windows_utf8_to_wide(Destination.c_str());
	const BOOL Renamed = MoveFileExW(WideSource.c_str(), WideDestination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
	const DWORD FirstError = GetLastError();
	ASSERT_EQ(Renamed, FALSE);
	if(FirstError != ERROR_ACCESS_DENIED)
		GTEST_SKIP() << "Native rename did not reach the access-denied fallback: " << FirstError;

	EXPECT_NE(fs_rename(Info.m_aFilename, Destination.c_str()), 0);
	File = io_open(Destination.c_str(), IOFLAG_READ);
	ASSERT_NE(File, nullptr);
	char aContents[4] = {};
	EXPECT_EQ(io_read(File, aContents, 3), 3u);
	EXPECT_EQ(io_close(File), 0);
	EXPECT_STREQ(aContents, "old");
}
#endif
