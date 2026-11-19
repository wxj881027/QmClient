#include <game/client/components/qmclient/local_save_display.h>

#include <gtest/gtest.h>

#include <limits>
#include <new>

namespace
{
	using CLoadJob = CQmLocalSaveDisplayCache::CLoadJob;
	class CCancelableLoadJob : public CLoadJob
	{
	public:
		explicit CCancelableLoadJob(const char *pPath) :
			CLoadJob(pPath) { Abortable(true); }
	};
}

TEST(QmLocalSaveDisplayRequest, RejectionDoesNotInstallWaitingJobAndRetryKeepsRefreshInterval)
{
	CQmLocalSaveDisplayCache Cache;
	std::weak_ptr<CLoadJob> Rejected;
	int Submissions = 0;
	const auto Submit = [&](std::shared_ptr<CLoadJob> pJob) {
		++Submissions;
		Rejected = pJob;
		return false;
	};
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20, Submit));
	EXPECT_TRUE(Rejected.expired());
	EXPECT_FALSE(Cache.Refresh("saves.csv", 120, 20, Submit));
	EXPECT_EQ(Submissions, 1);
	EXPECT_FALSE(Cache.Refresh("saves.csv", 121, 20, Submit));
	EXPECT_EQ(Submissions, 2);
	EXPECT_FALSE(Cache.Ready());
}

TEST(QmLocalSaveDisplayRequest, FactoryAllocationFailureDoesNotSubmitAndCanRecover)
{
	CQmLocalSaveDisplayCache Cache;
	int Submissions = 0;
	const auto Submit = [&](auto) { ++Submissions; return true; };
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20, Submit,
		[](const char *) -> std::shared_ptr<CLoadJob> { throw std::bad_alloc(); }));
	EXPECT_EQ(Submissions, 0);
	EXPECT_FALSE(Cache.Refresh("saves.csv", 120, 20, Submit));
	EXPECT_TRUE(Cache.Refresh("saves.csv", 121, 20, Submit));
	EXPECT_EQ(Submissions, 1);
}

TEST(QmLocalSaveDisplayRequest, NullFactoryResultDoesNotBecomePending)
{
	CQmLocalSaveDisplayCache Cache;
	int Submissions = 0;
	const auto Submit = [&](auto) { ++Submissions; return true; };
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20, Submit,
		[](const char *) { return std::shared_ptr<CLoadJob>{}; }));
	EXPECT_EQ(Submissions, 0);
	EXPECT_TRUE(Cache.Refresh("saves.csv", 121, 20, Submit));
}

TEST(QmLocalSaveDisplayRequest, UnknownPreparationExceptionAllowsLaterRefresh)
{
	CQmLocalSaveDisplayCache Cache;
	const auto Submit = [](auto) { return true; };
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20, Submit,
		[](const char *) -> std::shared_ptr<CLoadJob> { throw 7; }));
	EXPECT_TRUE(Cache.Refresh("saves.csv", 121, 20, Submit));
}

TEST(QmLocalSaveDisplayRequest, ThrowingSubmitterReleasesCandidateAndCanRetry)
{
	CQmLocalSaveDisplayCache Cache;
	std::weak_ptr<CLoadJob> Candidate;
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20,
		[&](std::shared_ptr<CLoadJob> pJob) -> bool { Candidate = pJob; throw 7; }));
	EXPECT_TRUE(Candidate.expired());
	EXPECT_TRUE(Cache.Refresh("saves.csv", 121, 20, [](auto) { return true; }));
}

TEST(QmLocalSaveDisplayRequest, QueuedJobSuppressesDuplicatesUntilReset)
{
	CQmLocalSaveDisplayCache Cache;
	std::shared_ptr<CLoadJob> Queued;
	int Submissions = 0;
	const auto Submit = [&](std::shared_ptr<CLoadJob> pJob) { ++Submissions; Queued = std::move(pJob); return true; };
	ASSERT_TRUE(Cache.Refresh("saves.csv", 100, 20, Submit));
	EXPECT_FALSE(Cache.Refresh("saves.csv", 1000, 20, Submit));
	EXPECT_EQ(Submissions, 1);
	Cache.Reset();
	EXPECT_FALSE(Cache.Ready());
	EXPECT_EQ(Queued->State(), IJob::STATE_QUEUED);
	EXPECT_TRUE(Cache.Refresh("saves.csv", 1001, 20, Submit));
	EXPECT_EQ(Submissions, 2);
}

TEST(QmLocalSaveDisplayRequest, AbortedJobResultsAreNotReadAndDoNotBlockRetry)
{
	CQmLocalSaveDisplayCache Cache;
	std::shared_ptr<CLoadJob> Canceled;
	const auto Submit = [&](std::shared_ptr<CLoadJob> pJob) { Canceled = std::move(pJob); return true; };
	ASSERT_TRUE(Cache.Refresh("saves.csv", 100, 20, Submit,
		[](const char *pPath) { return std::make_shared<CCancelableLoadJob>(pPath); }));
	ASSERT_TRUE(Canceled->Abort());
	Canceled->m_Succeeded = true;
	Canceled->m_FileExists = true;
	Canceled->m_vEntries.resize(1);
	Canceled->m_vEntries[0].m_Code = "canceled-result";
	EXPECT_FALSE(Cache.Refresh("saves.csv", 101, 20, Submit));
	EXPECT_FALSE(Cache.Ready());
	EXPECT_TRUE(Cache.Entries().empty());
	EXPECT_TRUE(Cache.Refresh("saves.csv", 121, 20, Submit));
}

TEST(QmLocalSaveDisplayRequest, ClockRollbackAllowsRefreshWithoutSignedSubtraction)
{
	CQmLocalSaveDisplayCache Cache;
	int Attempts = 0;
	const auto Submit = [&](auto) { ++Attempts; return false; };
	EXPECT_FALSE(Cache.Refresh("saves.csv", 100, 20, Submit));
	EXPECT_FALSE(Cache.Refresh("saves.csv", 90, 20, Submit));
	EXPECT_EQ(Attempts, 2);
	EXPECT_FALSE(Cache.Refresh("saves.csv", 91, 20, Submit));
	EXPECT_EQ(Attempts, 2);
}

TEST(QmLocalSaveDisplayRequest, ExtremeClockDifferenceIsComparedWithoutOverflow)
{
	CQmLocalSaveDisplayCache Cache;
	int Attempts = 0;
	const auto Submit = [&](auto) { ++Attempts; return false; };
	EXPECT_FALSE(Cache.Refresh("saves.csv", std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), Submit));
	EXPECT_FALSE(Cache.Refresh("saves.csv", std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(), Submit));
	EXPECT_EQ(Attempts, 2);
}
