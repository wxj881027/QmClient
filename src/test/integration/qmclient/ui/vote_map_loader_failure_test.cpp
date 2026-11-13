#include <base/str.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <game/client/components/qmclient/vote_map_loader.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>
#include <utility>

namespace
{
	enum class EFailure
	{
		NONE,
		EMPTY,
		ALLOCATION,
		UNKNOWN
	};
	thread_local EFailure s_FactoryFailure = EFailure::NONE;
	constexpr const char *ALPHA = R"([{"name":"Alpha","type":"Novice","difficulty":1}])";
	constexpr const char *BETA = R"([{"name":"Beta","type":"Brutal","difficulty":3}])";
	class CResponse : public IHttpRequest
	{
	public:
		bool m_Aborted = false, m_ThrowAbort = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override
		{
			m_Aborted = true;
			if(m_ThrowAbort)
				throw 7;
		}
		void Finish(const char *pJson)
		{
			free(m_pBuffer);
			m_ResponseLength = str_length(pJson);
			m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength + 1));
			ASSERT_NE(m_pBuffer, nullptr);
			mem_copy(m_pBuffer, pJson, m_ResponseLength + 1);
			m_BufferSize = m_ResponseLength + 1;
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}
	};
	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		if(s_FactoryFailure == EFailure::EMPTY)
			return nullptr;
		if(s_FactoryFailure == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(s_FactoryFailure == EFailure::UNKNOWN)
			throw 7;
		return std::make_unique<CResponse>(pUrl);
	}
	class CHttpRecorder : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::shared_ptr<CResponse> m_pLastSeen;
		bool m_ThrowBefore = false, m_ThrowAfter = false;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_pLastSeen = std::static_pointer_cast<CResponse>(pRequest);
			if(m_ThrowBefore)
				throw 7;
			m_vRequests.push_back(m_pLastSeen);
			if(m_ThrowAfter)
				throw 7;
		}
		bool HasIpresolveBug() const override { return false; }
	};
	class CDeferredEngine : public IEngine
	{
	public:
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		bool m_Accept = true, m_ThrowAfter = false;
		EFailure m_Failure = EFailure::NONE;
		void Init() override {}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			if(m_Failure == EFailure::ALLOCATION)
				throw std::bad_alloc();
			if(m_Failure == EFailure::UNKNOWN)
				throw 7;
			if(!m_Accept)
				return false;
			m_vJobs.push_back(std::move(pJob));
			if(m_ThrowAfter)
				throw 7;
			return true;
		}
		void CompleteJobs()
		{
			CJobPool Pool;
			ASSERT_TRUE(Pool.TryInit(1));
			for(const auto &pJob : m_vJobs)
				ASSERT_TRUE(Pool.TryAdd(pJob));
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
	struct SContext
	{
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		CQmVoteMapLoader m_Loader{CreateResponse};
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
			m_Http.m_vRequests.back()->Finish(ALPHA);
			Publish();
			ASSERT_EQ(m_Loader.Maps().size(), 1u);
		}
	};
}

class VoteMapLoaderFailure : public ::testing::Test
{
	EFailure m_Previous = EFailure::NONE;
	void SetUp() override
	{
		m_Previous = s_FactoryFailure;
		s_FactoryFailure = EFailure::NONE;
	}
	void TearDown() override { s_FactoryFailure = m_Previous; }
};

TEST_F(VoteMapLoaderFailure, StartupAllocationFailuresLeaveAnErrorAndAllowExplicitRetry)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 32; ++Budget)
	{
		SCOPED_TRACE(Budget);
		SContext C;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			C.Update();
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_TRUE(C.m_Loader.Error());
		EXPECT_FALSE(C.m_Loader.Loading());
		C.m_Loader.Refresh(&C.m_Http);
		C.m_Http.m_vRequests.back()->Finish(ALPHA);
		C.Publish();
		EXPECT_FALSE(C.m_Loader.Error());
		ASSERT_EQ(C.m_Loader.Maps().size(), 1u);
		EXPECT_EQ(C.m_Loader.Revision(), 1u);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST_F(VoteMapLoaderFailure, EmptyOrThrowingFactoryPreservesCatalogUntilARefreshSucceeds)
{
	for(EFailure Failure : {EFailure::EMPTY, EFailure::ALLOCATION, EFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		SContext C;
		ASSERT_NO_FATAL_FAILURE(C.LoadCatalog());
		s_FactoryFailure = Failure;
		EXPECT_NO_THROW(C.m_Loader.Refresh(&C.m_Http));
		s_FactoryFailure = EFailure::NONE;
		EXPECT_TRUE(C.m_Loader.Error());
		EXPECT_FALSE(C.m_Loader.Loading());
		EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Alpha");
		EXPECT_EQ(C.m_Loader.Revision(), 1u);
		C.m_Loader.Refresh(&C.m_Http);
		C.m_Http.m_vRequests.back()->Finish(BETA);
		C.Publish();
		EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Beta");
		EXPECT_EQ(C.m_Loader.Revision(), 2u);
	}
}

TEST_F(VoteMapLoaderFailure, HttpSubmissionExceptionAbortsItsRequestAndPreservesCatalog)
{
	for(bool After : {false, true})
	{
		SCOPED_TRACE(After);
		SContext C;
		ASSERT_NO_FATAL_FAILURE(C.LoadCatalog());
		C.m_Http.m_ThrowBefore = !After;
		C.m_Http.m_ThrowAfter = After;
		EXPECT_NO_THROW(C.m_Loader.Refresh(&C.m_Http));
		ASSERT_NE(C.m_Http.m_pLastSeen, nullptr);
		EXPECT_TRUE(C.m_Http.m_pLastSeen->m_Aborted);
		EXPECT_TRUE(C.m_Loader.Error());
		EXPECT_FALSE(C.m_Loader.Loading());
		EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Alpha");
		C.m_Http.m_ThrowBefore = C.m_Http.m_ThrowAfter = false;
		C.m_Loader.Refresh(&C.m_Http);
		C.m_Http.m_vRequests.back()->Finish(BETA);
		C.Publish();
		EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Beta");
	}
}

TEST_F(VoteMapLoaderFailure, ParsePreparationAllocationFailureRetriesTheCompletedResponse)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 32; ++Budget)
	{
		SCOPED_TRACE(Budget);
		SContext C;
		C.Update();
		C.m_Http.m_vRequests.back()->Finish(ALPHA);
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			C.Update();
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_TRUE(C.m_Loader.Error());
		EXPECT_TRUE(C.m_Loader.Loading());
		EXPECT_EQ(C.m_Http.m_vRequests.size(), 1u);
		C.Update();
		ASSERT_EQ(C.m_Engine.m_vJobs.size(), 1u);
		C.m_Engine.CompleteJobs();
		C.Update();
		EXPECT_EQ(C.m_Http.m_vRequests.size(), 1u);
		EXPECT_FALSE(C.m_Loader.Error());
		EXPECT_EQ(C.m_Loader.Revision(), 1u);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST_F(VoteMapLoaderFailure, QueueRejectionKeepsThePreviousCatalogAndRetriesWithoutDownloading)
{
	SContext C;
	ASSERT_NO_FATAL_FAILURE(C.LoadCatalog());
	C.m_Loader.Refresh(&C.m_Http);
	C.m_Http.m_vRequests.back()->Finish(BETA);
	C.m_Engine.m_Accept = false;
	C.Update();
	C.Update();
	EXPECT_TRUE(C.m_Loader.Error());
	EXPECT_TRUE(C.m_Loader.Loading());
	EXPECT_TRUE(C.m_Engine.m_vJobs.empty());
	EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Alpha");
	EXPECT_EQ(C.m_Loader.Revision(), 1u);
	C.m_Engine.m_Accept = true;
	C.Publish();
	EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Beta");
	EXPECT_EQ(C.m_Loader.Revision(), 2u);
	EXPECT_EQ(C.m_Http.m_vRequests.size(), 2u);
}

TEST_F(VoteMapLoaderFailure, QueueExceptionBeforeAcceptanceRetainsTheResponseForRetry)
{
	for(EFailure Failure : {EFailure::ALLOCATION, EFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		SContext C;
		C.Update();
		C.m_Http.m_vRequests.back()->Finish(ALPHA);
		C.m_Engine.m_Failure = Failure;
		EXPECT_NO_THROW(C.Update());
		EXPECT_TRUE(C.m_Loader.Error());
		EXPECT_TRUE(C.m_Engine.m_vJobs.empty());
		C.m_Engine.m_Failure = EFailure::NONE;
		C.Publish();
		EXPECT_EQ(C.m_Loader.Revision(), 1u);
		EXPECT_EQ(C.m_Http.m_vRequests.size(), 1u);
	}
}

TEST_F(VoteMapLoaderFailure, QueueExceptionAfterAcceptanceCannotPublishTheDiscardedJob)
{
	SContext C;
	C.Update();
	C.m_Http.m_vRequests.back()->Finish(ALPHA);
	C.m_Engine.m_ThrowAfter = true;
	EXPECT_NO_THROW(C.Update());
	ASSERT_EQ(C.m_Engine.m_vJobs.size(), 1u);
	C.m_Engine.CompleteJobs();
	EXPECT_TRUE(C.m_Loader.Maps().empty());
	C.m_Engine.m_ThrowAfter = false;
	C.Update();
	EXPECT_TRUE(C.m_Loader.Maps().empty());
	ASSERT_EQ(C.m_Engine.m_vJobs.size(), 1u);
	C.m_Engine.CompleteJobs();
	C.Update();
	EXPECT_EQ(C.m_Loader.Revision(), 1u);
	EXPECT_EQ(C.m_Http.m_vRequests.size(), 1u);
}

TEST_F(VoteMapLoaderFailure, SuspendDiscardsARejectedResponseAndReopeningRequestsFreshData)
{
	SContext C;
	C.Update();
	const auto pOld = C.m_Http.m_vRequests.back();
	pOld->Finish(ALPHA);
	C.m_Engine.m_Accept = false;
	C.Update();
	C.m_Loader.Suspend();
	EXPECT_TRUE(pOld->m_Aborted);
	EXPECT_FALSE(C.m_Loader.Loading());
	C.m_Engine.m_Accept = true;
	C.Update();
	EXPECT_TRUE(C.m_Loader.Maps().empty());
	ASSERT_EQ(C.m_Http.m_vRequests.size(), 2u);
	C.m_Http.m_vRequests.back()->Finish(BETA);
	C.Publish();
	ASSERT_EQ(C.m_Loader.Maps().size(), 1u);
	EXPECT_EQ(C.m_Loader.Maps()[0].m_Name, "Beta");
}

TEST_F(VoteMapLoaderFailure, SelectionCopyFailureHidesOldStatisticsAndCanRetryTheNewMap)
{
	int Failures = 0;
	bool Completed = false;
	const QmVoteMaps::SMap Alpha{"Alpha", "", "Novice"};
	const QmVoteMaps::SMap Beta{std::string(70, 'B'), std::string(70, 'M'), "Brutal"};
	const std::string Details = "{\"name\":\"" + Beta.m_Name + "\",\"type\":\"Brutal\",\"finishers\":22}";
	for(size_t Budget = 0; Budget < 32; ++Budget)
	{
		SCOPED_TRACE(Budget);
		SContext C;
		ASSERT_NO_FATAL_FAILURE(C.LoadCatalog());
		C.m_Loader.Select(&Alpha, 0.0f);
		C.Update();
		C.m_Http.m_vRequests.back()->Finish(R"({"name":"Alpha","type":"Novice","finishers":10})");
		C.Publish();
		ASSERT_NE(C.m_Loader.Details(), nullptr);
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			C.m_Loader.Select(&Beta, 1.0f);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(C.m_Loader.Details(), nullptr);
		EXPECT_TRUE(C.m_Loader.DetailsError());
		C.Update(2.0f);
		EXPECT_EQ(C.m_Http.m_vRequests.size(), 2u);
		C.m_Loader.Select(&Beta, 2.0f);
		C.Update(2.4f);
		ASSERT_EQ(C.m_Http.m_vRequests.size(), 3u);
		C.m_Http.m_vRequests.back()->Finish(Details.c_str());
		C.Publish();
		ASSERT_NE(C.m_Loader.Details(), nullptr);
		EXPECT_EQ(C.m_Loader.Details()->m_Finishers, 22);
		EXPECT_FALSE(C.m_Loader.DetailsError());
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST_F(VoteMapLoaderFailure, AbortExceptionDuringSuspendStillDiscardsTheRequest)
{
	SContext C;
	C.Update();
	C.m_Http.m_vRequests.back()->m_ThrowAbort = true;
	EXPECT_NO_THROW(C.m_Loader.Suspend());
	EXPECT_FALSE(C.m_Loader.Loading());
	C.Update();
	EXPECT_EQ(C.m_Http.m_vRequests.size(), 2u);
}

TEST_F(VoteMapLoaderFailure, AbortExceptionCannotEscapeTheLoaderDestructor)
{
	CHttpRecorder Http;
	CDeferredEngine Engine;
	EXPECT_NO_THROW({
		CQmVoteMapLoader Loader(CreateResponse);
		Loader.Update(&Http, &Engine, 1.0f);
		Http.m_vRequests.back()->m_ThrowAbort = true;
	});
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	EXPECT_TRUE(Http.m_vRequests[0]->m_Aborted);
}
