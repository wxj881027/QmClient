// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef TEST_TEST_H
#define TEST_TEST_H

#include <base/system.h>

#include <cstddef>
#include <memory>
#include <string>

class IStorage;

std::string TestSourcePath(const char *pRelativePath);
std::string ReadTestSourceFile(const char *pRelativePath);

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

void TestDeleteTestStorageFiles(const char *pPath);

class CTestInfo
{
public:
	CTestInfo();
	~CTestInfo();
	std::unique_ptr<IStorage> CreateTestStorage();
	const char *StoragePath() const { return m_aStoragePath; }
	bool m_DeleteTestStorageFilesOnSuccess = true;
	void Filename(char *pBuffer, size_t BufferLength, const char *pSuffix);
	char m_aFilenamePrefix[128];
	char m_aFilename[128];
	char m_aStoragePath[IO_MAX_PATH_LENGTH];
	bool m_HasCreatedStoragePath = false;
};
#endif // TEST_TEST_H
