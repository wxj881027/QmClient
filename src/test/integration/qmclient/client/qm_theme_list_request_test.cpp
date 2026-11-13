#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/theme_list_load_request.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/rejectable_allocation_resource.h>
#include <test/test.h>

#include <chrono>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	class CThemeScanFailureStorage : public IStorage
	{
		[[noreturn]] static void Unexpected() { throw std::logic_error("unexpected theme scan storage call"); }

	public:
		IStorage *m_pSource = nullptr;
		bool m_Unknown = false;
		int NumPaths() const override { Unexpected(); }
		bool GetDataPath(const char *, char *, unsigned) const override { Unexpected(); }
		bool GetExecutablePath(const char *, char *, unsigned) const override { Unexpected(); }
		void ListDirectory(int, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			if(m_pSource)
			{
				// 扫描只借隔离保存目录，避免 TYPE_ALL 读到源码/构建目录的主题。
				m_pSource->ListDirectory(IStorage::TYPE_SAVE, pPath, Callback, pUser);
				return;
			}
			Callback("partial.map", 0, IStorage::TYPE_SAVE, pUser);
			if(m_Unknown)
				throw 7;
			throw std::bad_alloc();
		}
		void ListDirectoryInfo(int, const char *, FS_LISTDIR_CALLBACK_FILEINFO, void *) override { Unexpected(); }
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

	class CThemeScanEngine : public IEngine
	{
	public:
		CJobPool m_Pool;
		bool m_Started = false;
		bool m_Accept = true;
		bool m_Throw = false;
		int m_Attempts = 0;
		std::shared_ptr<IJob> m_pLastJob;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			if(m_Throw)
				throw std::bad_alloc();
			if(!m_Accept || !m_Pool.TryAdd(pJob))
				return false;
			m_pLastJob = std::move(pJob);
			return true;
		}
		void ShutdownJobs() override { m_Pool.Shutdown(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class QmThemeListRequest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CThemeScanFailureStorage m_IsolatedStorage;
		CThemeScanFailureStorage m_FailureStorage;
		CThemeScanEngine m_Engine;
		CQmThemeListLoadRequest m_Request;
		std::vector<CTheme> m_Themes;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_IsolatedStorage.m_pSource = m_pStorage.get();
			ASSERT_TRUE(m_pStorage->CreateFolder("themes", IStorage::TYPE_SAVE));
			ASSERT_TRUE(EnsureDefaultThemeEntries(m_Themes));
			m_Engine.m_Started = m_Engine.m_Pool.TryInit(1);
			ASSERT_TRUE(m_Engine.m_Started);
		}
		void TearDown() override
		{
			if(m_Engine.m_Started)
				m_Engine.ShutdownJobs();
		}
		void Store(const char *pPath)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(QmThemeListRequest, CompleteScanPublishesSortedUniqueEntriesAfterDefaults)
{
	ASSERT_NO_FATAL_FAILURE(Store("themes/z.map"));
	ASSERT_NO_FATAL_FAILURE(Store("themes/a.png"));
	ASSERT_NO_FATAL_FAILURE(Store("themes/ignored.txt"));
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	ASSERT_TRUE(m_Request.Poll(m_Themes, 3, 1s));
	ASSERT_EQ(m_Themes.size(), 5u);
	EXPECT_EQ(m_Themes[0].m_Name, "");
	EXPECT_EQ(m_Themes[3].m_Name, "a.png");
	EXPECT_EQ(m_Themes[4].m_Name, "z.map");
	EXPECT_EQ(m_Themes[4].m_IconPath, "themes/z.png");
}

TEST_F(QmThemeListRequest, AllocationScanFailureKeepsCatalogAndRetriesAtDeadline)
{
	m_Themes.emplace_back("old.map", true, false);
	const auto *pOld = m_Themes.data();
	ASSERT_TRUE(m_Request.TryStart(m_FailureStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_EQ(m_Engine.m_pLastJob->State(), IJob::STATE_FAILED);
	EXPECT_FALSE(m_Request.Poll(m_Themes, 3, 2s));
	EXPECT_EQ(m_Themes.data(), pOld);
	ASSERT_EQ(m_Themes.size(), 4u);
	EXPECT_EQ(m_Themes[3].m_Name, "old.map");
	ASSERT_NO_FATAL_FAILURE(Store("themes/new.map"));
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 2999ms));
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 3s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	ASSERT_TRUE(m_Request.Poll(m_Themes, 3, 3s));
	ASSERT_EQ(m_Themes.size(), 5u);
	EXPECT_EQ(m_Themes[3].m_Name, "new.map");
	EXPECT_EQ(m_Themes[4].m_Name, "old.map");
}

TEST_F(QmThemeListRequest, NonStandardScanFailureDoesNotPublishPartialCandidate)
{
	m_FailureStorage.m_Unknown = true;
	ASSERT_TRUE(m_Request.TryStart(m_FailureStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_FALSE(m_Request.Poll(m_Themes, 3, 1s));
	EXPECT_EQ(m_Themes.size(), 3u);
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1999ms));
}

TEST_F(QmThemeListRequest, SubmissionRejectionDoesNotLatchPendingRequest)
{
	m_Engine.m_Accept = false;
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	m_Engine.m_Accept = true;
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_TRUE(m_Request.Poll(m_Themes, 3, 1s));
}

TEST_F(QmThemeListRequest, SubmissionExceptionDoesNotLatchPendingRequest)
{
	m_Engine.m_Throw = true;
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	m_Engine.m_Throw = false;
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_TRUE(m_Request.Poll(m_Themes, 3, 1s));
}

TEST_F(QmThemeListRequest, PublicationAllocationFailureRetriesSameResultWithoutRescanning)
{
	ASSERT_NO_FATAL_FAILURE(Store("themes/new.map"));
	CRejectableAllocationResource Memory;
	std::vector<CTheme, CBorrowedAllocator<CTheme>> Themes(&Memory);
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	Memory.m_Reject = true;
	EXPECT_FALSE(m_Request.Poll(Themes, 3, 1s));
	EXPECT_EQ(Themes.size(), 3u);
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 5s));
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	Memory.m_Reject = false;
	ASSERT_TRUE(m_Request.Poll(Themes, 3, 5s));
	ASSERT_EQ(Themes.size(), 4u);
	EXPECT_EQ(Themes[3].m_Name, "new.map");
}

TEST_F(QmThemeListRequest, ResetDiscardsOldCompletedSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Store("themes/old.map"));
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	m_Request.Reset();
	EXPECT_FALSE(m_Request.Poll(m_Themes, 3, 1s));
	ASSERT_TRUE(m_pStorage->RemoveFile("themes/old.map", IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(Store("themes/new.map"));
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	ASSERT_TRUE(m_Request.Poll(m_Themes, 3, 1s));
	ASSERT_EQ(m_Themes.size(), 4u);
	EXPECT_EQ(m_Themes[3].m_Name, "new.map");
}

TEST_F(QmThemeListRequest, ResetAllowsManualRefreshBeforeFailureDelay)
{
	ASSERT_TRUE(m_Request.TryStart(m_FailureStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_FALSE(m_Request.Poll(m_Themes, 3, 1s));
	m_Request.Reset();
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_TRUE(m_Request.Poll(m_Themes, 3, 1s));
}

TEST_F(QmThemeListRequest, PendingRequestDoesNotSubmitDuplicateScan)
{
	ASSERT_TRUE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 1s));
	EXPECT_FALSE(m_Request.TryStart(m_IsolatedStorage, m_Engine, 2s));
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	ASSERT_TRUE(WaitForJobFinished(*m_Engine.m_pLastJob));
	EXPECT_TRUE(m_Request.Poll(m_Themes, 3, 2s));
}
