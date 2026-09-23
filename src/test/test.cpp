// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <base/logger.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

#if !defined(DDNET_TEST_SOURCE_DIR)
#define DDNET_TEST_SOURCE_DIR "."
#endif

std::string TestSourcePath(const char *pRelativePath)
{
	if(pRelativePath == nullptr || pRelativePath[0] == '\0')
		return DDNET_TEST_SOURCE_DIR;

	std::string Path = DDNET_TEST_SOURCE_DIR;
	if(!Path.empty() && Path.back() != '/' && Path.back() != '\\')
		Path += '/';
	Path += pRelativePath;
	return Path;
}

std::string ReadTestSourceFile(const char *pRelativePath)
{
	const std::string Path = TestSourcePath(pRelativePath);
	std::ifstream File(Path, std::ios::binary);
	EXPECT_TRUE(File.good()) << Path;
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	return Buffer.str();
}

CTestInfo::CTestInfo()
{
	const ::testing::TestInfo *pTestInfo =
		::testing::UnitTest::GetInstance()->current_test_info();

	// Typed tests have test names like "TestName/0" and "TestName/1", which would result in invalid filenames.
	// Replace the string after the first slash with the name of the typed test and use hyphen instead of slash.
	char aTestCaseName[128];
	str_copy(aTestCaseName, pTestInfo->test_case_name());
	for(int i = 0; i < str_length(aTestCaseName); i++)
	{
		if(aTestCaseName[i] == '/')
		{
			aTestCaseName[i] = '-';
			aTestCaseName[i + 1] = '\0';
			str_append(aTestCaseName, pTestInfo->type_param());
			break;
		}
	}
	str_format(m_aFilenamePrefix, sizeof(m_aFilenamePrefix), "%s.%s-%d",
		aTestCaseName, pTestInfo->name(), process_id());
	Filename(m_aFilename, sizeof(m_aFilename), ".tmp");
	str_format(m_aStoragePath, sizeof(m_aStoragePath), "tmp/tests/%s", m_aFilename);
}

void CTestInfo::Filename(char *pBuffer, size_t BufferLength, const char *pSuffix)
{
	str_format(pBuffer, BufferLength, "%s%s", m_aFilenamePrefix, pSuffix);
}

std::unique_ptr<IStorage> CTestInfo::CreateTestStorage()
{
	fs_makedir_rec_for(m_aStoragePath);
	bool Error = fs_makedir(m_aStoragePath);
	EXPECT_FALSE(Error);
	if(Error)
	{
		return nullptr;
	}
	m_HasCreatedStoragePath = true;
	char aTestPath[IO_MAX_PATH_LENGTH];
	str_copy(aTestPath, ::testing::internal::GetArgvs().front().c_str());
	const char *apArgs[] = {aTestPath};
	return CreateTempStorage(m_aStoragePath, std::size(apArgs), apArgs);
}

class CTestInfoPath
{
public:
	bool m_IsDirectory;
	char m_aData[IO_MAX_PATH_LENGTH];

	bool operator<(const CTestInfoPath &Other) const
	{
		if(m_IsDirectory != Other.m_IsDirectory)
		{
			return m_IsDirectory < Other.m_IsDirectory;
		}
		return str_comp(m_aData, Other.m_aData) > 0;
	}
};

class CTestCollectData
{
public:
	char m_aCurrentDir[IO_MAX_PATH_LENGTH];
	std::vector<CTestInfoPath> *m_pvEntries;
};

int TestCollect(const char *pName, int IsDir, int Unused, void *pUser)
{
	CTestCollectData *pData = (CTestCollectData *)pUser;

	if(str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
	{
		return 0;
	}

	CTestInfoPath Path;
	Path.m_IsDirectory = IsDir;
	str_format(Path.m_aData, sizeof(Path.m_aData), "%s/%s", pData->m_aCurrentDir, pName);
	pData->m_pvEntries->push_back(Path);
	if(Path.m_IsDirectory)
	{
		CTestCollectData DataRecursive;
		str_copy(DataRecursive.m_aCurrentDir, Path.m_aData, sizeof(DataRecursive.m_aCurrentDir));
		DataRecursive.m_pvEntries = pData->m_pvEntries;
		fs_listdir(DataRecursive.m_aCurrentDir, TestCollect, 0, &DataRecursive);
	}
	return 0;
}

void TestDeleteTestStorageFiles(const char *pPath)
{
	std::vector<CTestInfoPath> vEntries;
	CTestCollectData Data;
	str_copy(Data.m_aCurrentDir, pPath, sizeof(Data.m_aCurrentDir));
	Data.m_pvEntries = &vEntries;
	fs_listdir(Data.m_aCurrentDir, TestCollect, 0, &Data);

	CTestInfoPath Path;
	Path.m_IsDirectory = true;
	str_copy(Path.m_aData, Data.m_aCurrentDir, sizeof(Path.m_aData));
	vEntries.push_back(Path);

	// Sorts directories after files.
	std::sort(vEntries.begin(), vEntries.end());

	// Don't delete too many files.
	ASSERT_LE(vEntries.size(), 10);
	for(auto &Entry : vEntries)
	{
		if(Entry.m_IsDirectory)
		{
			ASSERT_FALSE(fs_removedir(Entry.m_aData));
		}
		else
		{
			ASSERT_FALSE(fs_remove(Entry.m_aData));
		}
	}
}

static void WriteTestFileOrDir(const char *pBasePath, const char *pRelativePath, bool Directory)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pBasePath, pRelativePath);
	if(Directory)
	{
		ASSERT_FALSE(fs_makedir(aPath));
		return;
	}

	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	ASSERT_TRUE(File != nullptr);
	static const char *pContents = "tmp";
	io_write(File, pContents, str_length(pContents));
	io_close(File);
}

static void CreateScopedTestStorageFixture(const std::function<void(CTestInfo &)> &Body)
{
	CTestInfo Info;
	Body(Info);
}

CTestInfo::~CTestInfo()
{
	if(!::testing::Test::HasFailure() && m_DeleteTestStorageFilesOnSuccess)
	{
		const char *pCleanupPath = m_HasCreatedStoragePath ? m_aStoragePath : m_aFilename;
		TestDeleteTestStorageFiles(pCleanupPath);
	}
}

int main(int argc, const char **argv)
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	log_set_global_logger_default();
	::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	net_init();
	return RUN_ALL_TESTS();
}

TEST(TestInfo, Sort)
{
	std::vector<CTestInfoPath> vEntries;
	vEntries.resize(3);

	const char aBasePath[] = "test_dir";
	const char aSubPath[] = "test_dir/subdir";
	const char aFilePath[] = "test_dir/subdir/file.txt";

	vEntries[0].m_IsDirectory = true;
	str_copy(vEntries[0].m_aData, aBasePath);

	vEntries[1].m_IsDirectory = true;
	str_copy(vEntries[1].m_aData, aSubPath);

	vEntries[2].m_IsDirectory = false;
	str_copy(vEntries[2].m_aData, aFilePath);

	// Sorts directories after files.
	std::sort(vEntries.begin(), vEntries.end());

	EXPECT_FALSE(vEntries[0].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[0].m_aData, aFilePath), 0);
	EXPECT_TRUE(vEntries[1].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[1].m_aData, aSubPath), 0);
	EXPECT_TRUE(vEntries[2].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[2].m_aData, aBasePath), 0);
}

TEST(TestInfo, DeletesTestStorageOnSuccessByDefault)
{
	CTestInfo Info;
	EXPECT_TRUE(Info.m_DeleteTestStorageFilesOnSuccess);
}

TEST(TestInfo, SuccessfulScopedStorageIsDeletedRecursively)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		str_copy(aPath, Info.m_aFilename, sizeof(aPath));
		ASSERT_FALSE(fs_makedir(Info.m_aFilename));
		WriteTestFileOrDir(Info.m_aFilename, "root.txt", false);
		WriteTestFileOrDir(Info.m_aFilename, "nested", true);
		WriteTestFileOrDir(Info.m_aFilename, "nested/child.txt", false);
		EXPECT_TRUE(fs_is_dir(Info.m_aFilename));
	});

	EXPECT_FALSE(fs_is_dir(aPath));
	EXPECT_FALSE(fs_is_file(aPath));
}

TEST(TestInfo, CreateTestStorageCleansCreatedSaveFilesOnSuccess)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		str_copy(aPath, Info.StoragePath(), sizeof(aPath));
		std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);

		char aSavePath[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, "cleanup.txt", aSavePath, sizeof(aSavePath));
		IOHANDLE File = io_open(aSavePath, IOFLAG_WRITE);
		ASSERT_TRUE(File != nullptr);
		static const char *pContents = "cleanup";
		io_write(File, pContents, str_length(pContents));
		io_close(File);

		EXPECT_TRUE(fs_is_file(aSavePath));
	});

	EXPECT_FALSE(fs_is_dir(aPath));
}

TEST(TestInfo, DisabledCleanupPreservesFilesForInspection)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		Info.m_DeleteTestStorageFilesOnSuccess = false;
		str_copy(aPath, Info.StoragePath(), sizeof(aPath));
		std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);
		WriteTestFileOrDir(Info.StoragePath(), "keep.txt", false);
	});

	EXPECT_TRUE(fs_is_dir(aPath));
	char aFile[IO_MAX_PATH_LENGTH];
	str_format(aFile, sizeof(aFile), "%s/keep.txt", aPath);
	EXPECT_TRUE(fs_is_file(aFile));
	TestDeleteTestStorageFiles(aPath);
}
