#include <game/client/components/qmclient/settings_asset_list_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
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
		EFsListDirectoryResult ListDirectoryChecked(int, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			if(m_Fail)
			{
				if(Callback("partial", 1, IStorage::TYPE_SAVE, pUser))
					return EFsListDirectoryResult::STOPPED;
				if(m_Unknown)
					throw 7;
				throw std::bad_alloc();
			}
			return m_pSource->ListDirectoryChecked(IStorage::TYPE_SAVE, pPath, Callback, pUser);
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

	// 目录枚举边界按已合并的逻辑目录给出数据；不复制任务的过滤、预算或发布算法。
	class CScriptedAssetScanStorage : public CAssetScanStorage
	{
	public:
		struct SEntry
		{
			std::string m_Name;
			int m_IsDir;
			int m_Type = IStorage::TYPE_SAVE;
		};
		std::map<std::string, std::vector<SEntry>> m_Directories;
		std::map<std::string, EFsListDirectoryResult> m_Results;
		std::vector<std::string> m_vCalls;
		bool m_Cycle = false;
		bool m_VisitedOtherRoot = false;
		size_t m_MaxCalls = 64;

		EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			// 旧实现即使失去进度也只会让此测试失败，不会无界等待或耗尽测试进程栈。
			if(m_vCalls.size() >= m_MaxCalls)
				throw std::logic_error("scripted scan did not terminate within its guard");
			m_vCalls.emplace_back(pPath);
			if(m_Cycle && std::string_view(pPath).starts_with("maps"))
			{
				if(Callback("partial.map", 0, IStorage::TYPE_SAVE, pUser) || Callback("loop", 1, IStorage::TYPE_SAVE, pUser))
					return EFsListDirectoryResult::STOPPED;
				return EFsListDirectoryResult::SUCCESS;
			}
			const auto It = m_Directories.find(pPath);
			if(It != m_Directories.end())
			{
				for(const SEntry &Entry : It->second)
				{
					if(Type != IStorage::TYPE_ALL && Type != Entry.m_Type)
						continue;
					if(Entry.m_Type != IStorage::TYPE_SAVE)
						m_VisitedOtherRoot = true;
					if(Callback(Entry.m_Name.c_str(), Entry.m_IsDir, Entry.m_Type, pUser))
						return EFsListDirectoryResult::STOPPED;
				}
			}
			const auto Result = m_Results.find(pPath);
			if(Result != m_Results.end())
				return Result->second;
			return It == m_Directories.end() ? EFsListDirectoryResult::NOT_FOUND : EFsListDirectoryResult::SUCCESS;
		}
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			(void)ListDirectoryChecked(Type, pPath, Callback, pUser);
		}
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override
		{
			struct SBridge
			{
				FS_LISTDIR_CALLBACK_FILEINFO m_Callback;
				void *m_pUser;
			} Bridge{Callback, pUser};
			(void)ListDirectoryChecked(Type, pPath, [](const char *pName, int IsDir, int DirType, void *pRaw) {
				auto &Context = *static_cast<SBridge *>(pRaw);
				const CFsFileInfo Info{pName, -1, -1};
				return Context.m_Callback(&Info, IsDir, DirType, Context.m_pUser); }, &Bridge);
		}
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
		std::shared_ptr<CAssetListLoadJob> EntityJob(IStorage &Storage, const CAssetListLoadJob::SScanLimits &Limits = {})
		{
			return std::make_shared<CAssetListLoadJob>(CAssetListLoadJob::ASSET_TYPE_ENTITY_BG, &Storage, Limits);
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
	ASSERT_NO_FATAL_FAILURE(Store("assets/game/standalone.png"));
	auto pJob = NewJob();
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ASSERT_TRUE(pJob->Succeeded());
	EXPECT_EQ(pJob->Generation(), 23);
	EXPECT_EQ(pJob->GetType(), CAssetListLoadJob::ASSET_TYPE_GAME);
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 3u);
	EXPECT_STREQ(Entries[0].m_aName, "a");
	EXPECT_STREQ(Entries[1].m_aName, "standalone");
	EXPECT_STREQ(Entries[2].m_aName, "z");
	EXPECT_TRUE(Entries[0].m_IsDir);
	EXPECT_FALSE(Entries[1].m_IsDir);
	EXPECT_TRUE(Entries[2].m_IsDir);
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

TEST_F(QmSettingsAssetListJob, DepthLimitAllowsItsLastLevelAndRejectsTheNextWithoutPublishing)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"one", 1}};
	Storage.m_Directories["maps/one"] = {{"two", 1}};
	Storage.m_Directories["maps/one/two"] = {{"ready.map", 0}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxDepth = 2;
	auto pExact = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	ASSERT_TRUE(pExact->Succeeded());
	auto Entries = pExact->TakeEntries();
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_STREQ(Entries[0].m_aName, "one/two/ready");
	Storage.m_Directories["maps/one/two"].push_back({"over", 1});
	auto pOver = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_TRUE(pOver->IsCompleted());
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
	Storage.m_Directories["maps/one/two"].pop_back();
	auto pRetry = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pRetry));
	EXPECT_TRUE(pRetry->Succeeded());
	EXPECT_EQ(pRetry->TakeEntries().size(), 1u);
}

TEST_F(QmSettingsAssetListJob, DirectoryBudgetIsSharedAcrossRootsAndAllowsExactlyItsLimit)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"one", 1}};
	Storage.m_Directories["maps/one"] = {{"ready.map", 0}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxDirectoryCalls = 3; // 可选 assets 根、maps 根与 one。
	auto pExact = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	EXPECT_TRUE(pExact->Succeeded());
	EXPECT_EQ(pExact->TakeEntries().size(), 1u);
	Storage.m_Directories["maps/one"].push_back({"over", 1});
	Storage.m_vCalls.clear();
	auto pOver = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
	EXPECT_EQ(Storage.m_vCalls.size(), 3u);
}

TEST_F(QmSettingsAssetListJob, VisitBudgetCountsIgnoredEntriesButNotDotNavigation)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["assets/entity_bg"] = {{".", 1}, {"..", 1}, {".hidden", 1}, {"partial.map", 0}};
	Storage.m_Directories["maps"] = {{"ready.map", 0}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxNonDotVisits = 3;
	auto pExact = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	EXPECT_TRUE(pExact->Succeeded());
	EXPECT_EQ(pExact->TakeEntries().size(), 2u);
	Storage.m_Directories["assets/entity_bg"].push_back({"ignored.txt", 0});
	auto pOver = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, EmitBudgetIsSharedAcrossTheTwoMapSources)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["assets/entity_bg"] = {{"first.map", 0}};
	Storage.m_Directories["maps"] = {{"second.map", 0}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxEntries = 2;
	auto pExact = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	EXPECT_TRUE(pExact->Succeeded());
	EXPECT_EQ(pExact->TakeEntries().size(), 2u);
	Storage.m_Directories["maps"].push_back({"third.map", 0});
	auto pOver = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, FlatDirectoryEmissionAlsoHonorsTheSharedEntryBudget)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["assets/game"] = {{"first", 1}, {"second", 1}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxEntries = 2;
	auto pExact = std::make_shared<CAssetListLoadJob>(CAssetListLoadJob::ASSET_TYPE_GAME, &Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	EXPECT_TRUE(pExact->Succeeded());
	EXPECT_EQ(pExact->TakeEntries().size(), 2u);
	Storage.m_Directories["assets/game"].push_back({"third", 1});
	auto pOver = std::make_shared<CAssetListLoadJob>(CAssetListLoadJob::ASSET_TYPE_GAME, &Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, CycleStyleDirectoryGraphStopsAtTheDefaultDepthBudget)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Cycle = true;
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->IsCompleted());
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
	EXPECT_EQ(Storage.m_vCalls.size(), CAssetListLoadJob::SScanLimits{}.m_MaxDepth + 2);
}

TEST_F(QmSettingsAssetListJob, OverlongFullPathIsRejectedBeforeItCanReenterItsTruncatedParent)
{
	CScriptedAssetScanStorage Storage;
	const std::string Parent(IO_MAX_PATH_LENGTH - 6, 'p');
	const std::string FullParent = "maps/" + Parent;
	Storage.m_Directories["maps"] = {{Parent, 1}};
	Storage.m_Directories[FullParent] = {{"child", 1}};
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
	EXPECT_EQ(Storage.m_vCalls, (std::vector<std::string>{"assets/entity_bg", "maps", FullParent}));
}

TEST_F(QmSettingsAssetListJob, MaximumFittingPathSucceedsAndTheNextByteFailsWithoutTruncation)
{
	CScriptedAssetScanStorage Storage;
	const std::string Stem(IO_MAX_PATH_LENGTH - 10, 'p');
	Storage.m_Directories["maps"] = {{Stem + ".map", 0}};
	auto pExact = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pExact));
	ASSERT_TRUE(pExact->Succeeded());
	auto Entries = pExact->TakeEntries();
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_EQ(std::string(Entries[0].m_aName), Stem);
	Storage.m_Directories["maps"] = {{Stem + "x.map", 0}};
	auto pOver = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pOver));
	EXPECT_FALSE(pOver->Succeeded());
	EXPECT_TRUE(pOver->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, OverlongRelativeNameDoesNotBecomeATruncatedAssetKey)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{std::string(IO_MAX_PATH_LENGTH, 'p') + ".map", 0}};
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, RecursiveDirectoriesKeepTypeAllSoLaterRootsCanContributeChildren)
{
	CScriptedAssetScanStorage Storage;
	// 首根的同名目录已由存储层去重，子目录仍包含两个根各自独有的内容。
	Storage.m_Directories["maps"] = {{"shared", 1, IStorage::TYPE_SAVE}};
	Storage.m_Directories["maps/shared"] = {{"first.map", 0, IStorage::TYPE_SAVE}, {"second.map", 0, 1}};
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	ASSERT_TRUE(pJob->Succeeded());
	auto Entries = pJob->TakeEntries();
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_STREQ(Entries[0].m_aName, "shared/first");
	EXPECT_STREQ(Entries[1].m_aName, "shared/second");
	EXPECT_TRUE(Storage.m_VisitedOtherRoot);
}

TEST_F(QmSettingsAssetListJob, BudgetStopDoesNotContinueToLaterStorageRoots)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"first.map", 0, IStorage::TYPE_SAVE}, {"over.map", 0, IStorage::TYPE_SAVE}, {"later.map", 0, 1}};
	CAssetListLoadJob::SScanLimits Limits;
	Limits.m_MaxNonDotVisits = 1;
	auto pJob = EntityJob(Storage, Limits);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
	EXPECT_FALSE(Storage.m_VisitedOtherRoot);
}

TEST_F(QmSettingsAssetListJob, CheckedIoFailureDoesNotPublishPartialEntriesAndTheNextScanCanRecover)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"partial.map", 0}};
	Storage.m_Results["maps"] = EFsListDirectoryResult::IO_FAILURE;
	auto pFailed = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pFailed));
	EXPECT_TRUE(pFailed->IsCompleted());
	EXPECT_FALSE(pFailed->Succeeded());
	EXPECT_TRUE(pFailed->TakeEntries().empty());
	Storage.m_Results["maps"] = EFsListDirectoryResult::SUCCESS;
	auto pRetry = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pRetry));
	EXPECT_TRUE(pRetry->Succeeded());
	EXPECT_EQ(pRetry->TakeEntries().size(), 1u);
}

TEST_F(QmSettingsAssetListJob, StoppedCheckedEnumerationDoesNotPublishItsPartialEntries)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"partial.map", 0}};
	Storage.m_Results["maps"] = EFsListDirectoryResult::STOPPED;
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, UnsupportedCheckedBackendDoesNotFallBackToLegacyPartialSuccess)
{
	CScriptedAssetScanStorage Storage;
	Storage.m_Directories["maps"] = {{"partial.map", 0}};
	Storage.m_Results["maps"] = EFsListDirectoryResult::UNSUPPORTED;
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_FALSE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}

TEST_F(QmSettingsAssetListJob, MissingOptionalRootsRemainASuccessfulEmptySnapshot)
{
	CScriptedAssetScanStorage Storage;
	auto pJob = EntityJob(Storage);
	ASSERT_NO_FATAL_FAILURE(Execute(pJob));
	EXPECT_TRUE(pJob->Succeeded());
	EXPECT_TRUE(pJob->TakeEntries().empty());
}
