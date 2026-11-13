#include <engine/storage.h>

#include <game/client/components/assets_resource_registry.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>

namespace
{
	// 只在改名系统调用边界注入失败，存在性和文件内容均使用真实隔离存储。
	struct SStorageBoundary
	{
		IStorage &m_Storage;
		bool m_RejectRename = false;
		int m_ExistsCalls = 0;
		int m_RenameCalls = 0;

		explicit SStorageBoundary(IStorage &Storage) :
			m_Storage(Storage) {}
		bool FileExists(const char *pPath, int Type)
		{
			++m_ExistsCalls;
			return m_Storage.FileExists(pPath, Type);
		}
		bool RenameFile(const char *pOldPath, const char *pNewPath, int Type)
		{
			++m_RenameCalls;
			return !m_RejectRename && m_Storage.RenameFile(pOldPath, pNewPath, Type);
		}
	};

	class EntityBgInstallPath : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("assets", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("assets/entity_bg", IStorage::TYPE_SAVE));
		}
		bool Write(const char *pPath, const char *pText)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(!File)
				return false;
			const auto Size = str_length(pText);
			const bool Written = io_write(File, pText, Size) == static_cast<unsigned>(Size);
			return io_close(File) == 0 && Written;
		}
		void ExpectContent(const char *pPath, const char *pExpected)
		{
			std::unique_ptr<char, decltype(&std::free)> pText(m_pStorage->ReadFileStr(pPath, IStorage::TYPE_SAVE), &std::free);
			ASSERT_NE(pText, nullptr);
			EXPECT_STREQ(pText.get(), pExpected);
		}
	};
}

TEST_F(EntityBgInstallPath, FailedRenameKeepsExistingPathAndCanRetryWithoutLosingFile)
{
	ASSERT_TRUE(Write("assets/entity_bg/item.png", "original"));
	SStorageBoundary Storage{*m_pStorage};
	Storage.m_RejectRename = true;
	const auto Failed = PrepareEntityBgWorkshopInstallPath(&Storage, "assets/entity_bg/item.png", IStorage::TYPE_SAVE);
	EXPECT_TRUE(Failed.m_RenameFailed);
	EXPECT_EQ(Failed.m_Path, "assets/entity_bg/item.png");
	EXPECT_FALSE(m_pStorage->FileExists("assets/entity_bg/item.map", IStorage::TYPE_SAVE));
	ExpectContent(Failed.m_Path.c_str(), "original");
	Storage.m_RejectRename = false;
	const auto Retried = PrepareEntityBgWorkshopInstallPath(&Storage, Failed.m_Path, IStorage::TYPE_SAVE);
	EXPECT_FALSE(Retried.m_RenameFailed);
	EXPECT_EQ(Retried.m_Path, "assets/entity_bg/item.map");
	EXPECT_EQ(Storage.m_RenameCalls, 2);
	EXPECT_FALSE(m_pStorage->FileExists("assets/entity_bg/item.png", IStorage::TYPE_SAVE));
	ExpectContent(Retried.m_Path.c_str(), "original");
}

TEST_F(EntityBgInstallPath, SuccessfulRenameReturnsExistingCanonicalFile)
{
	ASSERT_TRUE(Write("assets/entity_bg/item.webp", "data"));
	const auto Result = PrepareEntityBgWorkshopInstallPath(m_pStorage.get(), "assets/entity_bg/item.webp", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "assets/entity_bg/item.map");
	EXPECT_EQ(RebuildEntityBgWorkshopLocalName(Result.m_Path), "entity_bg/item");
	EXPECT_FALSE(m_pStorage->FileExists("assets/entity_bg/item.webp", IStorage::TYPE_SAVE));
	ExpectContent(Result.m_Path.c_str(), "data");
}

TEST_F(EntityBgInstallPath, ExistingCanonicalTargetKeepsPriorityWithoutOverwritingEitherFile)
{
	ASSERT_TRUE(Write("assets/entity_bg/item.png", "old"));
	ASSERT_TRUE(Write("assets/entity_bg/item.map", "canonical"));
	SStorageBoundary Storage{*m_pStorage};
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, "assets/entity_bg/item.png", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "assets/entity_bg/item.map");
	EXPECT_EQ(Storage.m_RenameCalls, 0);
	ExpectContent("assets/entity_bg/item.png", "old");
	ExpectContent(Result.m_Path.c_str(), "canonical");
}

TEST_F(EntityBgInstallPath, MissingOldFileStillNormalizesMetadataWithoutAttemptingRename)
{
	SStorageBoundary Storage{*m_pStorage};
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, "assets/entity_bg/item.png", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "assets/entity_bg/item.map");
	EXPECT_EQ(Storage.m_RenameCalls, 0);
	EXPECT_FALSE(m_pStorage->FileExists(Result.m_Path.c_str(), IStorage::TYPE_SAVE));
}

TEST_F(EntityBgInstallPath, MissingStorageKeepsMetadataOnlyNormalization)
{
	const auto Result = PrepareEntityBgWorkshopInstallPath(static_cast<IStorage *>(nullptr), "assets/entity_bg/item.png", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "assets/entity_bg/item.map");
}

TEST_F(EntityBgInstallPath, AlreadyCanonicalPathDoesNotProbeOrRename)
{
	ASSERT_TRUE(Write("assets/entity_bg/item.map", "ready"));
	SStorageBoundary Storage{*m_pStorage};
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, "assets/entity_bg/item.map", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "assets/entity_bg/item.map");
	EXPECT_EQ(Storage.m_ExistsCalls, 0);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
	ExpectContent(Result.m_Path.c_str(), "ready");
}

TEST_F(EntityBgInstallPath, OtherFolderPathIsNotRewrittenOrTouched)
{
	SStorageBoundary Storage{*m_pStorage};
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, "maps/item.png", IStorage::TYPE_SAVE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, "maps/item.png");
	EXPECT_EQ(Storage.m_ExistsCalls, 0);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
}
