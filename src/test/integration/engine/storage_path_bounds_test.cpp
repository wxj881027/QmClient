#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{
	class CStoragePathBoundsTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void Write(const char *pName, const char *pContents)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pContents, std::strlen(pContents)), std::strlen(pContents));
			EXPECT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pName)
		{
			const std::unique_ptr<char, decltype(&free)> pContents(m_pStorage->ReadFileStr(pName, IStorage::TYPE_SAVE), free);
			EXPECT_NE(pContents, nullptr);
			return pContents ? pContents.get() : "";
		}
	};

#if defined(CONF_FAMILY_UNIX)
	class CStorageLongPathTest : public CStoragePathBoundsTest
	{
	protected:
		static constexpr char LEAF[] = "retained.map";
		const std::string m_FirstDirectory = std::string(230, 'a');
		std::string m_Directory;
		std::string m_RetainedPath;

		void SetUp() override
		{
			CStoragePathBoundsTest::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			const size_t FixedBytes = std::strlen(m_Info.StoragePath()) + m_FirstDirectory.size() + std::strlen(LEAF) + 3;
			ASSERT_LT(FixedBytes, IO_MAX_PATH_LENGTH - 1);
			const size_t LastDirectoryBytes = IO_MAX_PATH_LENGTH - 1 - FixedBytes;
			ASSERT_LE(LastDirectoryBytes, 255u);
			m_Directory = m_FirstDirectory + "/" + std::string(LastDirectoryBytes, 'b');
			m_RetainedPath = m_Directory + "/" + LEAF;
			ASSERT_TRUE(m_pStorage->CreateFolder(m_FirstDirectory.c_str(), IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder(m_Directory.c_str(), IStorage::TYPE_SAVE));
		}
	};
#endif
}

TEST_F(CStoragePathBoundsTest, WriteWithShortPathOutputLeavesExistingFileIntact)
{
	ASSERT_NO_FATAL_FAILURE(Write("retained.map", "original"));
	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "retained.map", aPath, sizeof(aPath));
	std::vector<char> vResolved(std::strlen(aPath) + 1, '?');
	const std::unique_ptr<void, decltype(&io_close)> File(
		m_pStorage->OpenFile("retained.map.other", IOFLAG_WRITE, IStorage::TYPE_SAVE, vResolved.data(), static_cast<int>(vResolved.size())), io_close);
	EXPECT_FALSE(File);
	EXPECT_EQ(vResolved.front(), '\0');
	EXPECT_EQ(Read("retained.map"), "original");
}

TEST_F(CStoragePathBoundsTest, FindFileWithShortOutputReportsNoMatchAndAllowsRetry)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("maps/rotation", IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(Write("maps/rotation/test.map", "map"));

	char aShortPath[5];
	EXPECT_FALSE(m_pStorage->FindFile("test.map", "maps", IStorage::TYPE_SAVE, aShortPath, sizeof(aShortPath)));
	EXPECT_STREQ(aShortPath, "");

	char aPath[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(m_pStorage->FindFile("test.map", "maps", IStorage::TYPE_SAVE, aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "maps/rotation/test.map");
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(CStorageLongPathTest, RenameToOverlongPathKeepsDestinationAndCandidate)
{
	ASSERT_NO_FATAL_FAILURE(Write(m_RetainedPath.c_str(), "original"));
	ASSERT_NO_FATAL_FAILURE(Write("candidate.map", "candidate"));

	const std::string OverlongPath = m_RetainedPath + ".other";
	EXPECT_FALSE(m_pStorage->RenameFile("candidate.map", OverlongPath.c_str(), IStorage::TYPE_SAVE, false));
	EXPECT_EQ(Read(m_RetainedPath.c_str()), "original");
	EXPECT_EQ(Read("candidate.map"), "candidate");
}

TEST_F(CStorageLongPathTest, RemoveOfOverlongPathKeepsExistingFile)
{
	ASSERT_NO_FATAL_FAILURE(Write(m_RetainedPath.c_str(), "original"));

	const std::string OverlongPath = m_RetainedPath + ".other";
	EXPECT_FALSE(m_pStorage->RemoveFile(OverlongPath.c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(m_RetainedPath.c_str()), "original");
}

TEST_F(CStorageLongPathTest, RecursiveSearchSkipsMatchesThatCannotBeOpenedWithinPathLimit)
{
	const std::string Leaf = std::string(LEAF) + ".other";
	const std::string FullPath = std::string(m_Info.StoragePath()) + "/" + m_Directory + "/" + Leaf;
	ASSERT_GT(FullPath.size(), IO_MAX_PATH_LENGTH - 1);
	// 原生文件系统可以存放此文件，但完整路径超出存储层可打开的范围。
	IOHANDLE File = io_open(FullPath.c_str(), IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_close(File), 0);

	char aPath[IO_MAX_PATH_LENGTH];
	const bool Found = m_pStorage->FindFile(Leaf.c_str(), m_FirstDirectory.c_str(), IStorage::TYPE_SAVE, aPath, sizeof(aPath));
	std::set<std::string> Entries;
	const size_t Count = m_pStorage->FindFiles(Leaf.c_str(), m_FirstDirectory.c_str(), IStorage::TYPE_SAVE, &Entries);
	ASSERT_EQ(fs_remove(FullPath.c_str()), 0);

	EXPECT_FALSE(Found);
	EXPECT_STREQ(aPath, "");
	EXPECT_EQ(Count, 0u);
	EXPECT_TRUE(Entries.empty());
}

TEST_F(CStorageLongPathTest, SaveRootCannotBeRegisteredAtATruncatedPath)
{
	const std::string FullPath = std::string(m_Info.StoragePath()) + "/" + m_RetainedPath + ".other";
	ASSERT_GT(FullPath.size(), IO_MAX_PATH_LENGTH - 1);
	ASSERT_EQ(fs_makedir(FullPath.c_str()), 0);

	const std::string ExecutablePath = ::testing::internal::GetArgvs().front();
	const char *apArgs[] = {ExecutablePath.c_str()};
	const auto pStorage = CreateTempStorage(FullPath.c_str(), 1, apArgs);
	ASSERT_EQ(fs_removedir(FullPath.c_str()), 0);

	EXPECT_EQ(pStorage, nullptr);
}

TEST_F(CStoragePathBoundsTest, DirectoryReadErrorDoesNotPublishHashes)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("asset", IStorage::TYPE_SAVE));
	SHA256_DIGEST Sha256 = sha256("previous", 8);
	const SHA256_DIGEST PreviousSha256 = Sha256;
	unsigned Crc = 123;

	EXPECT_FALSE(m_pStorage->CalculateHashes("asset", IStorage::TYPE_SAVE, &Sha256, &Crc));
	EXPECT_EQ(Sha256, PreviousSha256);
	EXPECT_EQ(Crc, 123u);
}
#endif
