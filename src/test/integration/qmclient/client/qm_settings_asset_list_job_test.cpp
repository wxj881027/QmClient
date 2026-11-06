#include <game/client/components/qmclient/settings_asset_list_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	class CAssetScanStorage : public IStorage
	{
		[[noreturn]] static void Unexpected() { throw std::logic_error("unexpected asset scan storage call"); }

	public:
		IStorage *m_pSource = nullptr;
		bool m_Fail = false;
		bool m_Unknown = false;
		int NumPaths() const override { Unexpected(); }
		bool GetDataPath(const char *, char *, unsigned) const override { Unexpected(); }
		bool GetExecutablePath(const char *, char *, unsigned) const override { Unexpected(); }
		void ListDirectory(int, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			if(m_Fail)
			{
				Callback("partial", 1, IStorage::TYPE_SAVE, pUser);
				if(m_Unknown)
					throw 7;
				throw std::bad_alloc();
			}
			// 仅替换文件系统边界，正常扫描只读临时保存域。
			m_pSource->ListDirectory(IStorage::TYPE_SAVE, pPath, Callback, pUser);
		}
		void ListDirectoryInfo(int, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override
		{
			m_pSource->ListDirectoryInfo(IStorage::TYPE_SAVE, pPath, Callback, pUser);
		}
		IOHANDLE OpenFile(const char *, int, int, char *, int) override { Unexpected(); }
		bool FileExists(const char *, int) override { Unexpected(); }
		bool FolderExists(const char *, int) override { Unexpected(); }
		bool ReadFile(const char *, int, void **, unsigned *) override { Unexpected(); }
		char *ReadFileStr(const char *, int) override { Unexpected(); }
		bool RetrieveTimes(const char *, int, time_t *, time_t *) override { Unexpected(); }
		bool CalculateHashes(const char *, int, SHA256_DIGEST *, unsigned *) override { Unexpected(); }
		bool FindFile(const char *, const char *, int, char *, int) override { Unexpected(); }
		size_t FindFiles(const char *, const char *, int, std::set<std::string> *) override { Unexpected(); }
		bool RemoveFile(const char *, int) override { Unexpected(); }
		bool RemoveFolder(const char *, int) override { Unexpected(); }
		bool RenameFile(const char *, const char *, int, bool) override { Unexpected(); }
		bool CreateFolder(const char *, int) override { Unexpected(); }
		void GetCompletePath(int, const char *, char *, unsigned) override { Unexpected(); }
		bool RemoveBinaryFile(const char *) override { Unexpected(); }
		bool RenameBinaryFile(const char *, const char *) override { Unexpected(); }
		const char *GetBinaryPath(const char *, char *, unsigned) override { Unexpected(); }
		const char *GetBinaryPathAbsolute(const char *, char *, unsigned) override { Unexpected(); }
	};

	class CListFailureAfterPublication : public CAssetListLoadJob
	{
		void Run() override
		{
			CAssetListLoadJob::Run();
			throw std::bad_alloc();
		}

	public:
		explicit CListFailureAfterPublication(IStorage *pStorage) :
			CAssetListLoadJob(ASSET_TYPE_GAME, pStorage) {}
	};

	class QmSettingsAssetListJob : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CAssetScanStorage m_ScanStorage;
		CJobPool m_Pool;
		bool m_Started = false;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_ScanStorage.m_pSource = m_pStorage.get();
			ASSERT_TRUE(m_pStorage->CreateFolder("assets", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("assets/game", IStorage::TYPE_SAVE));
			m_Started = m_Pool.TryInit(1);
			ASSERT_TRUE(m_Started);
		}
		void TearDown() override
		{
			if(m_Started)
				m_Pool.Shutdown();
		}
		void Folder(const char *pName) { ASSERT_TRUE(m_pStorage->CreateFolder(pName, IStorage::TYPE_SAVE)); }
		void Store(const char *pName)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_close(File), 0);
		}
		std::shared_ptr<CAssetListLoadJob> NewJob()
		{
			auto pJob = std::make_shared<CAssetListLoadJob>(CAssetListLoadJob::ASSET_TYPE_GAME, &m_ScanStorage);
			pJob->SetGeneration(23);
			return pJob;
		}
		void Execute(const std::shared_ptr<IJob> &pJob)
		{
			ASSERT_TRUE(m_Pool.TryAdd(pJob));
			ASSERT_TRUE(WaitForJobFinished(*pJob));
		}
	};
}

TEST_F(QmSettingsAssetListJob, DirectoryScanPublishesSortedEntriesAndPreservesRequestGeneration)
{
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/z"));
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/a"));
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/default"));
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/.hidden"));
	ASSERT_NO_FATAL_FAILURE(Store("assets/game/ignored.png"));
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ASSERT_TRUE(pJob->Succeeded());
	EXPECT_EQ(pJob->Generation(), 23);
	EXPECT_EQ(pJob->GetType(), CAssetListLoadJob::ASSET_TYPE_GAME);
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_STREQ(Entries[0].m_aName, "a");
	EXPECT_STREQ(Entries[1].m_aName, "z");
	EXPECT_TRUE(Entries[0].m_IsDir);
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, AllocationExceptionEndsWaitWithoutPublishingPartialEntries)
{
	m_ScanStorage.m_Fail = true;
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	EXPECT_TRUE(pJob->IsCompleted());
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, UnknownExceptionEndsWaitAndNextScanCanSucceed)
{
	m_ScanStorage.m_Fail = m_ScanStorage.m_Unknown = true;
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->IsCompleted());
	EXPECT_FALSE(pJob->Succeeded());
	m_ScanStorage.m_Fail = false;
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/recovered"));
	auto pNext = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pNext));
	ASSERT_TRUE(pNext->Succeeded());
	auto Entries = pNext->TakeEntries();
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_STREQ(Entries[0].m_aName, "recovered");
}

TEST_F(QmSettingsAssetListJob, EmptyDirectoryIsSuccessfulEmptySnapshot)
{
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, TakingBeforePublicationDoesNotConsumeFutureEntries)
{
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/ready"));
	auto pJob = NewJob();
	EXPECT_FALSE(pJob->IsCompleted());
	EXPECT_TRUE(pJob->TakeEntries().empty());
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_STREQ(Entries[0].m_aName, "ready");
}

TEST_F(QmSettingsAssetListJob, LaterExecutionFailurePreservesIndependentlyPublishedSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Folder("assets/game/ready"));
	auto pJob = std::make_shared<CListFailureAfterPublication>(&m_ScanStorage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_EQ(pJob->State(), IJob::STATE_FAILED);
	ASSERT_TRUE(pJob->Succeeded());
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_STREQ(Entries[0].m_aName, "ready");
}

TEST_F(QmSettingsAssetListJob, EntityBackgroundRecursionKeepsRelativeNamesAndRemovesCrossRootDuplicates)
{
	for(const char *pPath : {"assets/entity_bg", "assets/entity_bg/folder", "maps", "maps/entity_bg", "maps/entity_bg/folder", "maps/folder"})
		ASSERT_NO_FATAL_FAILURE(Folder(pPath));
	ASSERT_NO_FATAL_FAILURE(Store("assets/entity_bg/folder/a.map"));
	ASSERT_NO_FATAL_FAILURE(Store("maps/entity_bg/folder/a.map"));
	ASSERT_NO_FATAL_FAILURE(Store("maps/folder/b.png"));
	auto pJob = std::make_shared<CAssetListLoadJob>(CAssetListLoadJob::ASSET_TYPE_ENTITY_BG, &m_ScanStorage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ASSERT_TRUE(pJob->Succeeded());
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_STREQ(Entries[0].m_aName, "entity_bg/folder/a");
	EXPECT_STREQ(Entries[1].m_aName, "folder/b.png");
}
