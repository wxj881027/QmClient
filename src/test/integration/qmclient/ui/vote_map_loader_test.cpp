#include <game/client/components/qmclient/vote_map_loader.h>

#include <base/str.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

namespace
{
	class CResponse : public IHttpRequest
	{
	public:
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) : IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_Aborted = true; }
		void Fail() { m_State = EHttpState::ERROR; }
		void Finish(const char *pJson)
		{
			free(m_pBuffer);
			m_ResponseLength = str_length(pJson);
			m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength + 1));
			mem_copy(m_pBuffer, pJson, m_ResponseLength + 1);
			m_BufferSize = m_ResponseLength + 1;
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CResponse>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class CDeferredEngine : public IEngine
	{
	public:
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { m_vJobs.push_back(std::move(pJob)); }
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void CompleteJobs()
		{
			CJobPool Pool;
			Pool.Init(1);
			for(const auto &pJob : m_vJobs)
				Pool.Add(pJob);
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			for(const auto &pJob : m_vJobs)
			{
				while(pJob->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < Deadline)
					std::this_thread::yield();
				EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
			}
			Pool.Shutdown();
			m_vJobs.clear();
		}
	};

	class VoteMapLoader : public ::testing::Test
	{
	protected:
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		CQmVoteMapLoader m_Loader{CreateResponse};
		const QmVoteMaps::SMap m_Alpha{"Alpha", "Author", "Novice"};
		const QmVoteMaps::SMap m_Beta{"Beta", "Author", "Brutal"};
		void Update(float Now = 1.0f) { m_Loader.Update(&m_Http, &m_Engine, Now); }
		void Publish()
		{
			Update();
			m_Engine.CompleteJobs();
			Update();
		}
		void LoadCatalog()
		{
			Update();
			ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
			m_Http.m_vRequests[0]->Finish(R"([{"name":"Alpha","type":"Novice","difficulty":1}])");
			Publish();
			ASSERT_EQ(m_Loader.Maps().size(), 1u);
		}
	};
}

TEST_F(VoteMapLoader, PublishesCatalogOnlyAfterBackgroundParsingCompletes)
{
	Update();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Http.m_vRequests[0]->Finish(R"([{"name":"Alpha","type":"Novice","difficulty":1}])");
	Update();
	EXPECT_TRUE(m_Loader.Maps().empty());
	EXPECT_TRUE(m_Loader.Loading());
	m_Engine.CompleteJobs();
	Update();
	ASSERT_EQ(m_Loader.Maps().size(), 1u);
	EXPECT_EQ(m_Loader.Revision(), 1u);
	EXPECT_FALSE(m_Loader.Loading());
}

TEST_F(VoteMapLoader, FailedRefreshKeepsCatalogAndExplicitRetryRecovers)
{
	ASSERT_NO_FATAL_FAILURE(LoadCatalog());
	m_Loader.Refresh(&m_Http);
	m_Http.m_vRequests.back()->Fail();
	Update();
	EXPECT_TRUE(m_Loader.Error());
	EXPECT_EQ(m_Loader.Maps()[0].m_Name, "Alpha");
	EXPECT_EQ(m_Loader.Revision(), 1u);
	m_Loader.Refresh(&m_Http);
	m_Http.m_vRequests.back()->Finish("invalid json");
	Publish();
	EXPECT_TRUE(m_Loader.Error());
	EXPECT_EQ(m_Loader.Maps()[0].m_Name, "Alpha");
	m_Loader.Refresh(&m_Http);
	m_Http.m_vRequests.back()->Finish(R"([{"name":"Beta","type":"Brutal","difficulty":3}])");
	Publish();
	EXPECT_FALSE(m_Loader.Error());
	EXPECT_EQ(m_Loader.Maps()[0].m_Name, "Beta");
	EXPECT_EQ(m_Loader.Revision(), 2u);
}

TEST_F(VoteMapLoader, RefreshDiscardsAlreadyQueuedOldCatalog)
{
	Update();
	m_Http.m_vRequests.back()->Finish(R"([{"name":"Old","type":"Novice","difficulty":1}])");
	Update();
	m_Loader.Refresh(&m_Http);
	m_Engine.CompleteJobs();
	Update();
	EXPECT_TRUE(m_Loader.Maps().empty());
	m_Http.m_vRequests.back()->Finish(R"([{"name":"New","type":"Novice","difficulty":1}])");
	Publish();
	ASSERT_EQ(m_Loader.Maps().size(), 1u);
	EXPECT_EQ(m_Loader.Maps()[0].m_Name, "New");
}

TEST_F(VoteMapLoader, DebouncesSelectionAndDoesNotRepeatDetailRequestEveryFrame)
{
	ASSERT_NO_FATAL_FAILURE(LoadCatalog());
	m_Loader.Select(&m_Alpha, 10.0f);
	Update(10.1f);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	m_Loader.Select(&m_Beta, 10.2f);
	Update(10.4f);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	Update(10.6f);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	m_Loader.Select(&m_Beta, 10.7f);
	Update(11.0f);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(VoteMapLoader, SelectionChangeRejectsQueuedStatisticsForPreviousMap)
{
	ASSERT_NO_FATAL_FAILURE(LoadCatalog());
	m_Loader.Select(&m_Alpha, 0.0f);
	Update();
	m_Http.m_vRequests.back()->Finish(R"({"name":"Alpha","type":"Novice","finishers":10,"average_time":60})");
	Update();
	m_Loader.Select(&m_Beta, 1.0f);
	m_Engine.CompleteJobs();
	Update(1.1f);
	EXPECT_EQ(m_Loader.Details(), nullptr);
	Update(1.5f);
	m_Http.m_vRequests.back()->Finish(R"({"name":"Beta","type":"Brutal","finishers":20,"average_time":120})");
	Publish();
	ASSERT_NE(m_Loader.Details(), nullptr);
	EXPECT_EQ(m_Loader.Details()->m_Finishers, 20);
}

TEST_F(VoteMapLoader, DetailFailureCanRetryWithoutReloadingCatalog)
{
	ASSERT_NO_FATAL_FAILURE(LoadCatalog());
	m_Loader.Select(&m_Alpha, 0.0f);
	Update();
	m_Http.m_vRequests.back()->Fail();
	Update();
	EXPECT_TRUE(m_Loader.DetailsError());
	m_Loader.RetryDetails();
	Update();
	ASSERT_EQ(m_Http.m_vRequests.size(), 3u);
	m_Http.m_vRequests.back()->Finish(R"({"name":"Alpha","type":"Novice","finishers":10})");
	Publish();
	ASSERT_NE(m_Loader.Details(), nullptr);
	EXPECT_FALSE(m_Loader.DetailsError());
	EXPECT_EQ(m_Loader.Revision(), 1u);
}

TEST_F(VoteMapLoader, SuspendCancelsTransportAndReopeningReusesSuccessfulCatalog)
{
	ASSERT_NO_FATAL_FAILURE(LoadCatalog());
	m_Loader.Select(&m_Alpha, 0.0f);
	Update();
	const auto pRequest = m_Http.m_vRequests.back();
	m_Loader.Suspend();
	m_Loader.Suspend();
	EXPECT_TRUE(pRequest->m_Aborted);
	pRequest->Finish(R"({"name":"Alpha","type":"Novice","finishers":99})");
	Update();
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_EQ(m_Loader.Details(), nullptr);
	EXPECT_EQ(m_Loader.Maps().size(), 1u);
	m_Loader.Select(&m_Alpha, 0.0f);
	Update();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
}

TEST_F(VoteMapLoader, SuspendDiscardsQueuedCatalogAndAllowsFreshOpen)
{
	Update();
	m_Http.m_vRequests.back()->Finish(R"([{"name":"Old","type":"Novice","difficulty":1}])");
	Update();
	m_Loader.Suspend();
	m_Engine.CompleteJobs();
	Update();
	EXPECT_TRUE(m_Loader.Maps().empty());
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}
