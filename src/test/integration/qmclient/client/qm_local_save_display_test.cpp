#include <engine/storage.h>

#include <game/client/components/qmclient/local_save_display.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <new>
#include <string_view>

namespace
{
	class QmLocalSaveDisplayJobs : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		CQmLocalSaveDisplayCache m_Cache;
		std::shared_ptr<IJob> m_pLastJob;
		char m_aPath[IO_MAX_PATH_LENGTH]{};
		bool m_PoolStarted = false;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "saves.csv", m_aPath, sizeof(m_aPath));
			ASSERT_TRUE(Write("date,player,map,old\n"));
			m_PoolStarted = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolStarted);
		}
		void TearDown() override
		{
			if(m_PoolStarted)
				m_Pool.Shutdown();
		}
		bool Write(std::string_view Text)
		{
			IOHANDLE File = io_open(m_aPath, IOFLAG_WRITE);
			if(!File)
				return false;
			const bool Written = io_write(File, Text.data(), Text.size()) == Text.size() && io_error(File) == 0;
			return io_close(File) == 0 && Written;
		}
		bool Submit(std::shared_ptr<IJob> pJob)
		{
			m_pLastJob = pJob;
			return m_Pool.TryAdd(std::move(pJob));
		}
		bool Refresh(int64_t Now)
		{
			return m_Cache.Refresh(m_aPath, Now, 20, [this](auto pJob) { return Submit(std::move(pJob)); });
		}
		testing::AssertionResult Complete(int64_t Now)
		{
			if(!m_pLastJob)
				return testing::AssertionFailure() << "no submitted load job";
			auto Done = WaitForJobDone(*m_pLastJob);
			if(!Done)
				return Done;
			if(Refresh(Now))
				return testing::AssertionFailure() << "publication unexpectedly queued another job";
			return testing::AssertionSuccess();
		}
		void ExpectCode(const char *pCode) const
		{
			ASSERT_TRUE(m_Cache.Ready());
			ASSERT_TRUE(m_Cache.FileExists());
			ASSERT_EQ(m_Cache.Entries().size(), 1u);
			EXPECT_EQ(m_Cache.Entries()[0].m_Code, pCode);
		}
	};
}

TEST_F(QmLocalSaveDisplayJobs, KeepsSnapshotUntilFinishedResultIsPublished)
{
	ASSERT_TRUE(Refresh(100));
	EXPECT_FALSE(m_Cache.Ready());
	ASSERT_TRUE(Complete(101));
	ExpectCode("old");
	ASSERT_TRUE(Write("date,player,map,new\n"));
	std::shared_ptr<IJob> Deferred;
	ASSERT_TRUE(m_Cache.Refresh(m_aPath, 121, 20, [&](auto pJob) { Deferred = std::move(pJob); return true; }));
	EXPECT_EQ(Deferred->State(), IJob::STATE_QUEUED);
	EXPECT_FALSE(Refresh(122));
	ExpectCode("old");
	ASSERT_TRUE(m_Pool.TryAdd(Deferred));
	m_pLastJob = std::move(Deferred);
	ASSERT_TRUE(Complete(123));
	ExpectCode("new");
}

TEST_F(QmLocalSaveDisplayJobs, MissingFilePublishesSuccessfulAbsence)
{
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(Complete(101));
	ExpectCode("old");
	ASSERT_EQ(fs_remove(m_aPath), 0);
	ASSERT_TRUE(Refresh(121));
	ASSERT_TRUE(Complete(122));
	EXPECT_TRUE(m_Cache.Ready());
	EXPECT_FALSE(m_Cache.FileExists());
	EXPECT_TRUE(m_Cache.Entries().empty());
}

TEST_F(QmLocalSaveDisplayJobs, EmptyFileRemainsDistinctFromMissing)
{
	ASSERT_TRUE(Write(""));
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(Complete(101));
	EXPECT_TRUE(m_Cache.Ready());
	EXPECT_TRUE(m_Cache.FileExists());
	EXPECT_TRUE(m_Cache.Entries().empty());
}

TEST_F(QmLocalSaveDisplayJobs, MalformedReadKeepsOldSnapshotAndLaterValidFileRecovers)
{
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(Complete(101));
	const char aCorrupt[] = "date,player,map,broken\0suffix\n";
	ASSERT_TRUE(Write(std::string_view(aCorrupt, sizeof(aCorrupt) - 1)));
	ASSERT_TRUE(Refresh(121));
	ASSERT_TRUE(Complete(122));
	ExpectCode("old");
	ASSERT_TRUE(Write("date,player,map,recovered\n"));
	ASSERT_TRUE(Refresh(142));
	ASSERT_TRUE(Complete(143));
	ExpectCode("recovered");
}

TEST_F(QmLocalSaveDisplayJobs, FailedInitialReadStaysUnreadyUntilSuccessfulRetry)
{
	const char aCorrupt[]{'a', '\0', 'b'};
	ASSERT_TRUE(Write(std::string_view(aCorrupt, sizeof(aCorrupt))));
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(Complete(101));
	EXPECT_FALSE(m_Cache.Ready());
	EXPECT_TRUE(m_Cache.Entries().empty());
	ASSERT_TRUE(Write("date,player,map,recovered\n"));
	ASSERT_TRUE(Refresh(121));
	ASSERT_TRUE(Complete(122));
	ExpectCode("recovered");
}

TEST_F(QmLocalSaveDisplayJobs, RejectedPoolAndPreparationFailureKeepOldSnapshotAndRecover)
{
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(Complete(101));
	EXPECT_FALSE(m_Cache.Refresh(m_aPath, 121, 20, [this](auto pJob) { return Submit(std::move(pJob)); }, [](const char *) -> std::shared_ptr<CQmLocalSaveDisplayCache::CLoadJob> { throw std::bad_alloc(); }));
	ExpectCode("old");
	CJobPool ClosedPool;
	EXPECT_FALSE(m_Cache.Refresh(m_aPath, 142, 20, [&](auto pJob) { return ClosedPool.TryAdd(std::move(pJob)); }));
	ExpectCode("old");
	ASSERT_TRUE(Write("date,player,map,recovered\n"));
	ASSERT_TRUE(Refresh(163));
	ASSERT_TRUE(Complete(164));
	ExpectCode("recovered");
}

TEST_F(QmLocalSaveDisplayJobs, ResetDiscardsPreviousCompletionAndAllowsImmediateNewRequest)
{
	ASSERT_TRUE(Refresh(100));
	ASSERT_TRUE(WaitForJobDone(*m_pLastJob));
	auto pOld = m_pLastJob;
	m_Cache.Reset();
	EXPECT_FALSE(m_Cache.Ready());
	EXPECT_TRUE(m_Cache.Entries().empty());
	ASSERT_TRUE(Write("date,player,map,new\n"));
	ASSERT_TRUE(Refresh(101));
	ASSERT_TRUE(Complete(102));
	ExpectCode("new");
	EXPECT_EQ(pOld->State(), IJob::STATE_DONE);
}

TEST_F(QmLocalSaveDisplayJobs, JobOwnsPathAndCanFinishAfterCacheDestruction)
{
	std::shared_ptr<IJob> pJob;
	{
		CQmLocalSaveDisplayCache Cache;
		char aPath[IO_MAX_PATH_LENGTH];
		str_copy(aPath, m_aPath);
		ASSERT_TRUE(Cache.Refresh(aPath, 100, 20, [&](auto pCandidate) { pJob = std::move(pCandidate); return true; }));
		aPath[0] = '\0';
	}
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	const auto pLoaded = std::static_pointer_cast<CQmLocalSaveDisplayCache::CLoadJob>(pJob);
	EXPECT_TRUE(pLoaded->m_Succeeded);
	ASSERT_EQ(pLoaded->m_vEntries.size(), 1u);
	EXPECT_EQ(pLoaded->m_vEntries[0].m_Code, "old");
}
