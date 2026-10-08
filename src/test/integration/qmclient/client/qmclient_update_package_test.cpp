#include <game/client/components/qmclient/update_package.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <thread>

namespace
{
	// 只替换网络边界，文件落盘、合并、哈希与后台作业使用生产实现。
	class CPackageResponse : public IHttpRequest
	{
	public:
		bool m_DeferredAbort = false;
		explicit CPackageResponse(const std::string &Url) : IHttpRequest(Url.c_str()) {}
		void Header(const char *) override {}
		std::optional<CHttpByteRange> Range() const { return m_ByteRange; }
		void Running(double Bytes = 0)
		{
			m_State = EHttpState::RUNNING;
			m_Current = Bytes;
		}
		void Reply(int Status, std::string_view Body, std::optional<CHttpByteRange> Range = {}, EHttpState State = EHttpState::DONE, std::optional<int64_t> RetryAfter = {})
		{
			m_StatusCode = Status;
			m_ResultContentRange = Range;
			m_ResultRetryAfterSeconds = RetryAfter;
			if(!BeforeInit() || OnData(Body.data(), Body.size()) != Body.size())
				State = EHttpState::ERROR;
			m_Current = Body.size();
			OnCompletionInternal(State);
		}
		void Abort() override
		{
			if(Done())
				return;
			IHttpRequest::Abort();
			if(m_DeferredAbort)
				return;
			OnCompletionInternal(EHttpState::ABORTED);
		}
	};
	class QmPackageDownload : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_Storage = m_Info.CreateTestStorage();
		CJobPool m_Jobs;
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		std::vector<std::shared_ptr<CPackageResponse>> m_vRequests;
		std::shared_ptr<qm_update::CPackageDownload> m_Download;
		std::string m_Body;
		void AddJob(std::shared_ptr<IJob> Job)
		{
			m_vJobs.push_back(Job);
			m_Jobs.Add(std::move(Job));
		}
		void SetUp() override
		{
			m_Jobs.Init(1);
			m_Body.resize(4 * 1024 * 1024 + 3);
			for(size_t Index = 0; Index < m_Body.size(); ++Index)
				m_Body[Index] = static_cast<char>((Index * 37 + Index / 251) % 256);
			m_Download = std::make_shared<qm_update::CPackageDownload>("https://github.com/wxj881027/QmClient/releases/download/v3.4/file.7z", m_Storage.get(), "package.tmp", 8 * 1024 * 1024, [](const std::shared_ptr<IHttpRequest> &) {}, [this](std::shared_ptr<IJob> Job) { AddJob(std::move(Job)); }, [this](const std::string &Url) { auto Request = std::make_shared<CPackageResponse>(Url); m_vRequests.push_back(Request); return Request; });
		}
		void TearDown() override
		{
			m_Download->Abort();
			m_Jobs.Shutdown();
			m_Download.reset();
			m_vRequests.clear();
		}
		void Probe()
		{
			m_vRequests[0]->Reply(206, std::string_view(m_Body).substr(0, 1), CHttpByteRange{0, 0, static_cast<int64_t>(m_Body.size())});
			m_Download->Poll(1);
			ASSERT_EQ(m_vRequests.size(), 5U);
		}
		void FinishPart(size_t Index)
		{
			const auto Range = *m_vRequests[Index]->Range();
			m_vRequests[Index]->Reply(206, std::string_view(m_Body).substr(Range.m_First, Range.Length()), CHttpByteRange{Range.m_First, Range.m_Last, static_cast<int64_t>(m_Body.size())});
		}
		void Await()
		{
			const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while(!m_Download->Done() && std::chrono::steady_clock::now() < End)
			{
				m_Download->Poll(2);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			ASSERT_EQ(m_Download->State(), EHttpState::DONE);
			EXPECT_EQ(m_Download->ResultSha256(), sha256(m_Body.data(), m_Body.size()));
			void *pData = nullptr;
			unsigned Size = 0;
			ASSERT_TRUE(m_Storage->ReadFile("package.tmp", IStorage::TYPE_SAVE, &pData, &Size));
			EXPECT_EQ(std::string_view(static_cast<char *>(pData), Size), m_Body);
			free(pData);
		}
	};
}

TEST_F(QmPackageDownload, OutOfOrderSegmentsProduceTheExactPackageAndDigest)
{
	Probe();
	for(size_t Index : {4U, 2U, 3U, 1U})
	{
		FinishPart(Index);
		m_Download->Poll(2);
	}
	Await();
}

TEST_F(QmPackageDownload, IgnoredRangeProbeFallsBackToOneWholeFile)
{
	m_vRequests[0]->Reply(200, "", {}, EHttpState::ERROR);
	m_Download->Poll(1);
	ASSERT_EQ(m_vRequests.size(), 2U);
	EXPECT_FALSE(m_vRequests.back()->Range());
	m_vRequests.back()->Reply(200, m_Body);
	Await();
}

TEST_F(QmPackageDownload, RangeSupportLostDuringTransferFallsBackWithoutAppending)
{
	Probe();
	FinishPart(1);
	m_vRequests[2]->Reply(200, "", {}, EHttpState::ERROR);
	m_Download->Poll(2);
	ASSERT_EQ(m_vRequests.size(), 6U);
	EXPECT_TRUE(m_vRequests[3]->IsAbortRequested());
	EXPECT_TRUE(m_vRequests[4]->IsAbortRequested());
	EXPECT_FALSE(m_vRequests.back()->Range());
	m_vRequests.back()->Reply(200, m_Body);
	Await();
}

TEST_F(QmPackageDownload, FailedSegmentRetriesOnceWithoutRedownloadingCompletedSegments)
{
	Probe();
	FinishPart(1);
	m_vRequests[2]->Reply(503, "", {}, EHttpState::ERROR);
	m_Download->Poll(2);
	ASSERT_EQ(m_vRequests.size(), 6U);
	EXPECT_EQ(m_vRequests[1]->State(), EHttpState::DONE);
	FinishPart(3);
	FinishPart(4);
	FinishPart(5);
	Await();
}

TEST_F(QmPackageDownload, RepeatedSegmentFailureEndsTheBatch)
{
	Probe();
	m_vRequests[1]->Reply(503, "", {}, EHttpState::ERROR);
	m_Download->Poll(2);
	ASSERT_EQ(m_vRequests.size(), 6U);
	m_vRequests[5]->Reply(503, "", {}, EHttpState::ERROR);
	m_Download->Poll(3);
	EXPECT_EQ(m_Download->State(), EHttpState::ERROR);
	EXPECT_EQ(m_Download->CompletedStatusCode(), 503);
	EXPECT_TRUE(m_vRequests[2]->IsAbortRequested());
}

TEST_F(QmPackageDownload, RateLimitDoesNotRetryAndPublishesCooldown)
{
	Probe();
	m_vRequests[1]->Reply(429, "", {}, EHttpState::ERROR, 600);
	m_Download->Poll(2);
	EXPECT_EQ(m_vRequests.size(), 5U);
	EXPECT_EQ(m_Download->CompletedStatusCode(), 429);
	EXPECT_EQ(m_Download->ResultRetryAfterSeconds(), 600);
}

TEST_F(QmPackageDownload, CancelDuringSegmentsStopsRequestsAndCannotPublishCompletion)
{
	Probe();
	m_Download->Abort();
	for(size_t Index = 1; Index < m_vRequests.size(); ++Index)
		EXPECT_TRUE(m_vRequests[Index]->IsAbortRequested());
	m_Download->Poll(100);
	EXPECT_EQ(m_Download->State(), EHttpState::ABORTED);
}

TEST_F(QmPackageDownload, CancelDuringMergeDoesNotPublishAReadyPackage)
{
	Probe();
	for(size_t Index = 1; Index <= 4; ++Index)
		FinishPart(Index);
	m_Download->Poll(2);
	m_Download->Abort();
	m_Download->Poll(100);
	EXPECT_EQ(m_Download->State(), EHttpState::ABORTED);
	ASSERT_EQ(m_vJobs.size(), 1U);
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(m_vJobs.front()->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < End)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	ASSERT_EQ(m_vJobs.front()->State(), IJob::STATE_DONE);
	char aPath[IO_MAX_PATH_LENGTH];
	m_Storage->GetCompletePath(IStorage::TYPE_SAVE, "package.tmp", aPath, sizeof(aPath));
	EXPECT_FALSE(fs_is_file(aPath));
}

TEST_F(QmPackageDownload, MissingFirstByteReconnectsWhileCompletedSegmentsStayComplete)
{
	Probe();
	FinishPart(1);
	FinishPart(3);
	FinishPart(4);
	m_vRequests[2]->Running();
	m_Download->Poll(2);
	EXPECT_EQ(m_vRequests.size(), 5U);
	m_Download->Poll(11.9);
	EXPECT_FALSE(m_vRequests[2]->IsAbortRequested());
	m_Download->Poll(12);
	EXPECT_TRUE(m_vRequests[2]->IsAbortRequested());
	ASSERT_EQ(m_vRequests.size(), 6U);
	FinishPart(5);
	Await();
}

TEST_F(QmPackageDownload, TwentySecondsWithoutProgressReconnectsTheAffectedSegment)
{
	Probe();
	FinishPart(1);
	FinishPart(3);
	FinishPart(4);
	m_vRequests[2]->Running(1);
	m_Download->Poll(2);
	m_Download->Poll(21.9);
	EXPECT_FALSE(m_vRequests[2]->IsAbortRequested());
	m_Download->Poll(22);
	EXPECT_TRUE(m_vRequests[2]->IsAbortRequested());
	ASSERT_EQ(m_vRequests.size(), 6U);
	FinishPart(5);
	Await();
}

TEST_F(QmPackageDownload, QueuedProxyRequestsDoNotConsumeTransferDeadline)
{
	m_Download->Poll(100);
	EXPECT_FALSE(m_vRequests[0]->IsAbortRequested());
	EXPECT_EQ(m_Download->State(), EHttpState::RUNNING);
}

TEST_F(QmPackageDownload, LateCancelledSegmentsCannotOverwriteTheNextSourceSession)
{
	Probe();
	for(size_t Index = 1; Index <= 4; ++Index)
		m_vRequests[Index]->m_DeferredAbort = true;
	const auto Previous = m_Download;
	Previous->Abort();
	m_Download = std::make_shared<qm_update::CPackageDownload>("https://gh-proxy.com/https://github.com/wxj881027/QmClient/releases/download/v3.4/file.7z", m_Storage.get(), "next/package.tmp", 8 * 1024 * 1024, [](const std::shared_ptr<IHttpRequest> &) {}, [this](std::shared_ptr<IJob> Job) { m_Jobs.Add(std::move(Job)); }, [this](const std::string &Url) { auto Request = std::make_shared<CPackageResponse>(Url); m_vRequests.push_back(Request); return Request; });
	m_vRequests[5]->Reply(206, std::string_view(m_Body).substr(0, 1), CHttpByteRange{0, 0, static_cast<int64_t>(m_Body.size())});
	m_Download->Poll(1);
	ASSERT_EQ(m_vRequests.size(), 10U);
	// 模拟旧 HTTP 在取消后才完成落盘；新批次始终使用自己的源会话目录。
	const std::string Expected = m_Body;
	m_Body.assign(m_Body.size(), 'x');
	for(size_t Index = 1; Index <= 4; ++Index)
		FinishPart(Index);
	m_Body = Expected;
	for(size_t Index = 6; Index <= 9; ++Index)
		FinishPart(Index);
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(!m_Download->Done() && std::chrono::steady_clock::now() < End)
	{
		m_Download->Poll(2);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ASSERT_EQ(m_Download->State(), EHttpState::DONE);
	EXPECT_EQ(m_Download->ResultSha256(), sha256(m_Body.data(), m_Body.size()));
	EXPECT_EQ(Previous->State(), EHttpState::ABORTED);
	void *pData = nullptr;
	unsigned Size = 0;
	ASSERT_TRUE(m_Storage->ReadFile("next/package.tmp", IStorage::TYPE_SAVE, &pData, &Size));
	EXPECT_EQ(std::string_view(static_cast<char *>(pData), Size), m_Body);
	free(pData);
}
