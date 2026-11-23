#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <memory>
#include <vector>

CTestInfo::CTestInfo()
{
	const ::testing::TestInfo *pTestInfo =
		::testing::UnitTest::GetInstance()->current_test_info();

	// Typed tests have test names like "TestName/0" and "TestName/1", which would result in invalid filenames.
	// Replace the string after the first slash with the name of the typed test and use hyphen instead of slash.
	char aTestCaseName[128];
	str_copy(aTestCaseName, pTestInfo->test_case_name());
	for(int i = 0; aTestCaseName[i] != '\0'; i++)
	{
		if(aTestCaseName[i] == '/')
		{
			aTestCaseName[i] = '-';
			aTestCaseName[i + 1] = '\0';
			str_append(aTestCaseName, pTestInfo->type_param());
			break;
		}
	}
	// 存档根改为绝对路径后，过长的测试名会在 Windows 的 260 字符上限处触发
	// 「文件名或扩展名太长」，这里保留可读前缀并用内容哈希维持同名截断后的唯一性。
	char aFullName[192];
	str_format(aFullName, sizeof(aFullName), "%s.%s", aTestCaseName, pTestInfo->name());
	uint32_t NameHash = 2166136261u;
	for(const char *pCursor = aFullName; *pCursor != '\0'; pCursor++)
	{
		NameHash ^= (unsigned char)*pCursor;
		NameHash *= 16777619u;
	}
	char aShortName[33];
	str_copy(aShortName, aFullName, sizeof(aShortName));
	str_format(m_aFilenamePrefix, sizeof(m_aFilenamePrefix), "%s-%08x-%d",
		aShortName, NameHash, pid());
	Filename(m_aFilename, sizeof(m_aFilename), ".tmp");
	str_format(m_aStoragePath, sizeof(m_aStoragePath), "tmp/tests/%s", m_aFilename);
}

void CTestInfo::Filename(char *pBuffer, size_t BufferLength, const char *pSuffix)
{
	str_format(pBuffer, BufferLength, "%s%s", m_aFilenamePrefix, pSuffix);
}

std::unique_ptr<IStorage> CTestInfo::CreateTestStorage()
{
	// 隔离存储的存档根用绝对路径：客户端实际的 $USERDIR/$EXEDIR 也是绝对路径，
	// 相对根会让 GetCompletePath 的结果再喂回 storage 时被二次拼接。
	if(fs_is_relative_path(m_aStoragePath))
	{
		char aCurrentDir[IO_MAX_PATH_LENGTH];
		char aAbsolute[IO_MAX_PATH_LENGTH];
		if(fs_getcwd(aCurrentDir, sizeof(aCurrentDir)) &&
			str_format(aAbsolute, sizeof(aAbsolute), "%s/%s", aCurrentDir, m_aStoragePath) < (int)sizeof(aAbsolute))
		{
			str_copy(m_aStoragePath, aAbsolute);
		}
	}
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

class CTestCollectData
{
public:
	char m_aCurrentDir[IO_MAX_PATH_LENGTH];
	std::vector<CTestInfoPath> *m_pvEntries;
};

static int TestCollect(const char *pName, int IsDir, int Unused, void *pUser)
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

	// 只用 m_aFilename 写文件的用例里，这条路径是文件而不是存档目录。
	CTestInfoPath Path;
	Path.m_IsDirectory = fs_is_dir(Data.m_aCurrentDir);
	str_copy(Path.m_aData, Data.m_aCurrentDir, sizeof(Path.m_aData));
	vEntries.push_back(Path);

	// 先文件后目录；目录按路径长度降序，保证子目录先于父目录删除。
	std::sort(vEntries.begin(), vEntries.end(), [](const CTestInfoPath &Left, const CTestInfoPath &Right) {
		if(Left.m_IsDirectory != Right.m_IsDirectory)
			return !Left.m_IsDirectory;
		if(Left.m_IsDirectory)
			return str_length(Left.m_aData) > str_length(Right.m_aData);
		return str_comp(Left.m_aData, Right.m_aData) < 0;
	});

	// 只删除本次测试目录内的条目；前缀不符说明路径解析异常，中止以免误删其他目录。
	const size_t PathLength = str_length(pPath);
	for(const auto &Entry : vEntries)
	{
		if(str_length(Entry.m_aData) == PathLength)
			continue; // 根目录自身。
		ASSERT_EQ(str_comp_num(Entry.m_aData, pPath, (int)PathLength), 0) << Entry.m_aData;
		ASSERT_GT(str_length(Entry.m_aData), PathLength) << Entry.m_aData;
	}
	for(auto &Entry : vEntries)
	{
		if(Entry.m_IsDirectory)
		{
			// fs_is_dir 跟随符号链接：指向目录的链接在 POSIX 上要用 unlink 删除，
			// rmdir 会以 ENOTDIR 失败。真目录删除失败时下面的断言仍会报出来。
			if(fs_removedir(Entry.m_aData) != 0)
				ASSERT_FALSE(fs_remove(Entry.m_aData)) << Entry.m_aData;
		}
		else
		{
			ASSERT_FALSE(fs_remove(Entry.m_aData));
		}
	}
}

CTestInfo::~CTestInfo()
{
	if(!::testing::Test::HasFailure() && m_DeleteTestStorageFilesOnSuccess)
	{
		const char *pCleanupPath = m_HasCreatedStoragePath ? m_aStoragePath : m_aFilename;
		TestDeleteTestStorageFiles(pCleanupPath);
	}
}
