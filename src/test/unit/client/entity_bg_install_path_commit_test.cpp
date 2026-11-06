#include <game/client/components/assets_resource_registry.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	constexpr const char *OLD_PATH = "assets/entity_bg/a-long-workshop-installation-file.png";
	constexpr const char *NEW_PATH = "assets/entity_bg/a-long-workshop-installation-file.map";
	constexpr const char *LOCAL_NAME = "entity_bg/a-long-workshop-installation-file";
	constexpr int STORAGE_TYPE = 3;

	// 只替换文件存在和改名边界；规范化、本地名生成及字符串分配均调用生产实现。
	struct SInstallPathBoundary
	{
		bool m_OldExists = true;
		bool m_NewExists = false;
		bool m_AcceptRename = true;
		bool m_ArmAfterRename = false;
		bool m_UnexpectedPath = false;
		int m_ExistsCalls = 0;
		int m_RenameCalls = 0;
		int m_LastStorageType = -1;
		std::optional<CPredictionAllocationFailure> m_AfterRenameFailure;

		bool FileExists(const char *pPath, int StorageType)
		{
			++m_ExistsCalls;
			m_LastStorageType = StorageType;
			if(std::string_view(pPath) == OLD_PATH)
				return m_OldExists;
			if(std::string_view(pPath) == NEW_PATH)
				return m_NewExists;
			m_UnexpectedPath = true;
			return false;
		}
		bool RenameFile(const char *pOldPath, const char *pNewPath, int StorageType)
		{
			++m_RenameCalls;
			m_LastStorageType = StorageType;
			const bool Expected = std::string_view(pOldPath) == OLD_PATH && std::string_view(pNewPath) == NEW_PATH;
			m_UnexpectedPath |= !Expected;
			const bool Accepted = m_AcceptRename && Expected;
			if(Accepted)
			{
				m_OldExists = false;
				m_NewExists = true;
			}
			if(m_ArmAfterRename)
				m_AfterRenameFailure.emplace();
			return Accepted;
		}
	};
}

TEST(EntityBgInstallPathCommit, SuccessfulRenameReturnsAllPreparedMetadataWithoutFurtherAllocation)
{
	SInstallPathBoundary Storage;
	Storage.m_ArmAfterRename = true;
	SEntityBgInstallPathPreparation Result;
	bool Threw = false;
	try
	{
		Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
	}
	catch(const std::bad_alloc &)
	{
		Threw = true;
	}
	const bool Armed = Storage.m_AfterRenameFailure.has_value();
	const auto Attempts = Armed ? Storage.m_AfterRenameFailure->Attempts() : 0;
	Storage.m_AfterRenameFailure.reset();
	EXPECT_FALSE(Threw);
	EXPECT_TRUE(Armed);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Storage.m_RenameCalls, 1);
	EXPECT_FALSE(Storage.m_UnexpectedPath);
	EXPECT_FALSE(Storage.m_OldExists);
	EXPECT_TRUE(Storage.m_NewExists);
	EXPECT_EQ(Storage.m_LastStorageType, STORAGE_TYPE);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, NEW_PATH);
	EXPECT_EQ(Result.m_LocalName, LOCAL_NAME);
}

TEST(EntityBgInstallPathCommit, RejectedRenameReturnsOldPathWithoutPostBoundaryAllocation)
{
	SInstallPathBoundary Storage;
	Storage.m_AcceptRename = false;
	Storage.m_ArmAfterRename = true;
	SEntityBgInstallPathPreparation Result;
	bool Threw = false;
	try
	{
		Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
	}
	catch(const std::bad_alloc &)
	{
		Threw = true;
	}
	const bool Armed = Storage.m_AfterRenameFailure.has_value();
	const auto Attempts = Armed ? Storage.m_AfterRenameFailure->Attempts() : 0;
	Storage.m_AfterRenameFailure.reset();
	EXPECT_FALSE(Threw);
	EXPECT_TRUE(Armed);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_TRUE(Result.m_RenameFailed);
	EXPECT_EQ(Result.m_Path, OLD_PATH);
	EXPECT_TRUE(Result.m_LocalName.empty());
	EXPECT_TRUE(Storage.m_OldExists);
	EXPECT_FALSE(Storage.m_NewExists);
	EXPECT_EQ(Storage.m_RenameCalls, 1);
}

TEST(EntityBgInstallPathCommit, EveryPreparationAllocationCanFailBeforeAnyRename)
{
	std::size_t Allocations;
	{
		SInstallPathBoundary Storage;
		SEntityBgInstallPathPreparation Result;
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
		Allocations = Trace.Attempts();
	}
	ASSERT_GT(Allocations, 0u);
	for(std::size_t Index = 0; Index < Allocations; ++Index)
	{
		SCOPED_TRACE(Index);
		SInstallPathBoundary Storage;
		SEntityBgInstallPathPreparation Result;
		bool Threw = false, Triggered;
		{
			CPredictionAllocationFailure Failure(Index);
			try
			{
				Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_TRUE(Threw);
		EXPECT_EQ(Storage.m_RenameCalls, 0);
		EXPECT_TRUE(Storage.m_OldExists);
		EXPECT_FALSE(Storage.m_NewExists);
	}
}

TEST(EntityBgInstallPathCommit, ReturnedMetadataOwnsItsNamesAfterSourceStringIsDestroyed)
{
	SInstallPathBoundary Storage;
	SEntityBgInstallPathPreparation Result;
	{
		std::string Source = OLD_PATH;
		Result = PrepareEntityBgWorkshopInstallPath(&Storage, Source, STORAGE_TYPE);
	}
	EXPECT_EQ(Result.m_Path, NEW_PATH);
	EXPECT_EQ(Result.m_LocalName, LOCAL_NAME);
	EXPECT_FALSE(Result.m_RenameFailed);
}

TEST(EntityBgInstallPathCommit, ExistingTargetStagesCanonicalIdentityWithoutRenamingEitherFile)
{
	SInstallPathBoundary Storage;
	Storage.m_NewExists = true;
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
	EXPECT_EQ(Result.m_Path, NEW_PATH);
	EXPECT_EQ(Result.m_LocalName, LOCAL_NAME);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
	EXPECT_TRUE(Storage.m_OldExists);
	EXPECT_TRUE(Storage.m_NewExists);
}

TEST(EntityBgInstallPathCommit, MissingOldFileStagesCanonicalIdentityWithoutRename)
{
	SInstallPathBoundary Storage;
	Storage.m_OldExists = false;
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, OLD_PATH, STORAGE_TYPE);
	EXPECT_EQ(Result.m_Path, NEW_PATH);
	EXPECT_EQ(Result.m_LocalName, LOCAL_NAME);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
	EXPECT_FALSE(Storage.m_OldExists);
	EXPECT_FALSE(Storage.m_NewExists);
}

TEST(EntityBgInstallPathCommit, AlreadyCanonicalPathStagesLocalNameWithoutStorageProbes)
{
	SInstallPathBoundary Storage;
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, NEW_PATH, STORAGE_TYPE);
	EXPECT_EQ(Result.m_Path, NEW_PATH);
	EXPECT_EQ(Result.m_LocalName, LOCAL_NAME);
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Storage.m_ExistsCalls, 0);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
}

TEST(EntityBgInstallPathCommit, UnrelatedPathHasNoReplacementLocalNameAndDoesNotTouchStorage)
{
	SInstallPathBoundary Storage;
	const auto Result = PrepareEntityBgWorkshopInstallPath(&Storage, "maps/unchanged.png", STORAGE_TYPE);
	EXPECT_EQ(Result.m_Path, "maps/unchanged.png");
	EXPECT_TRUE(Result.m_LocalName.empty());
	EXPECT_FALSE(Result.m_RenameFailed);
	EXPECT_EQ(Storage.m_ExistsCalls, 0);
	EXPECT_EQ(Storage.m_RenameCalls, 0);
}
