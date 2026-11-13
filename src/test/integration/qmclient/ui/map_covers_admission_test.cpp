#include <base/str.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/map_covers.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	class CResponse final : public IHttpRequest
	{
	public:
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_State = EHttpState::ABORTED; }
		void Fail() { m_State = EHttpState::ERROR; }
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder final : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::vector<std::string> m_vUrls;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vUrls.emplace_back(pRequest->Url());
			m_vRequests.push_back(std::static_pointer_cast<CResponse>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
		void CompleteRequests()
		{
			for(const auto &pRequest : m_vRequests)
				pRequest->Fail();
		}
	};

	class CNoJobsEngine final : public IEngine
	{
	public:
		int m_Jobs = 0;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob>) override { ++m_Jobs; }
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class CAdmissionGraphics final : public CIconBenchmarkGraphics
	{
	public:
		using IGraphics::CreateTextureHandle;
	};

	class MapCoversAdmission : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CNoJobsEngine m_Engine;
		CAdmissionGraphics m_Graphics;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		std::vector<std::string> Urls(int Count)
		{
			std::vector<std::string> vUrls;
			for(int i = 0; i < Count; ++i)
			{
				char aUrl[128];
				str_format(aUrl, sizeof(aUrl), "https://ddnet.org/ranks/maps/admission_candidate_%03d.png", i);
				vUrls.emplace_back(aUrl);
			}
			return vUrls;
		}
		void Queue(CQmMapCovers &Covers, const std::vector<std::string> &vUrls, std::size_t Count)
		{
			for(std::size_t i = 0; i < Count; ++i)
			{
				CQmMapCovers::SImage Image;
				EXPECT_FALSE(Covers.Get(vUrls[i].c_str(), CQmMapCovers::ERole::ROW, &Image));
				EXPECT_FALSE(Image.m_Texture.IsValid());
			}
		}
		void Update(CQmMapCovers &Covers, CHttpRecorder &Http)
		{
			Covers.Update(&Http, &m_Engine, &m_Graphics, m_pStorage.get(), 1.0f);
		}
		void Drain(CQmMapCovers &Covers, CHttpRecorder &Http, std::size_t MaximumRequests)
		{
			// 下载由设备边界明确失败，逐帧腾出原有两个槽位；不接入网络或线程。
			for(std::size_t Frame = 0; Frame <= MaximumRequests + 1 && Covers.Loading(); ++Frame)
			{
				Http.CompleteRequests();
				Update(Covers, Http);
			}
			EXPECT_FALSE(Covers.Loading());
			EXPECT_EQ(m_Engine.m_Jobs, 0);
		}
	};
}

TEST_F(MapCoversAdmission, EveryNativeAdmissionFailurePreservesTheQueueAndRetriesEachUrlOnce)
{
	for(const std::size_t Existing : {0u, 1u, 95u})
	{
		const auto vUrls = Urls(static_cast<int>(Existing + 1));
		std::size_t Allocations;
		bool Available;
		{
			CQmMapCovers Covers(CreateResponse);
			Queue(Covers, vUrls, Existing);
			CQmMapCovers::SImage Image;
			{
				CPredictionAllocationFailure Count(std::numeric_limits<std::size_t>::max());
				Available = Covers.Get(vUrls.back().c_str(), CQmMapCovers::ERole::ROW, &Image);
				Allocations = Count.Attempts();
			}
			ASSERT_FALSE(Available);
			ASSERT_TRUE(Covers.Loading());
		}
		ASSERT_GT(Allocations, 0u);
		ASSERT_LE(Allocations, 32u);
		for(std::size_t Budget = 0; Budget < Allocations; ++Budget)
		{
			SCOPED_TRACE(::testing::Message() << "existing=" << Existing << " allocation=" << Budget);
			CQmMapCovers Covers(CreateResponse);
			CHttpRecorder Http;
			Queue(Covers, vUrls, Existing);
			CQmMapCovers::SImage Image{m_Graphics.CreateTextureHandle(99)};
			bool Triggered;
			{
				CPredictionAllocationFailure Fault(Budget);
				Available = Covers.Get(vUrls.back().c_str(), CQmMapCovers::ERole::ROW, &Image);
				Triggered = Fault.Triggered();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_FALSE(Available);
			EXPECT_FALSE(Image.m_Texture.IsValid());
			EXPECT_EQ(Covers.Loading(), Existing != 0);
			EXPECT_FALSE(Covers.Get(vUrls.back().c_str(), CQmMapCovers::ERole::ROW, &Image));
			EXPECT_TRUE(Covers.Loading());
			Drain(Covers, Http, vUrls.size());
			EXPECT_EQ(Http.m_vUrls, vUrls);
			for(const auto &Url : vUrls)
				EXPECT_EQ(std::count(Http.m_vUrls.begin(), Http.m_vUrls.end(), Url), 1);
			Covers.Shutdown(&m_Graphics);
		}
	}
}

TEST_F(MapCoversAdmission, RepeatedUrlAndRoleRequestsDoNotDuplicateTheDownload)
{
	CQmMapCovers Covers(CreateResponse);
	CHttpRecorder Http;
	const auto vUrls = Urls(1);
	CQmMapCovers::SImage Image;
	for(const auto Role : {CQmMapCovers::ERole::ROW, CQmMapCovers::ERole::DETAILS})
	{
		for(int i = 0; i < 3; ++i)
			EXPECT_FALSE(Covers.Get(vUrls.front().c_str(), Role, &Image));
	}
	Update(Covers, Http);
	ASSERT_EQ(Http.m_vUrls, vUrls);
	EXPECT_TRUE(Covers.Loading());
	EXPECT_FALSE(Covers.Get(vUrls.front().c_str(), CQmMapCovers::ERole::ROW, &Image));
	EXPECT_FALSE(Covers.Get(vUrls.front().c_str(), CQmMapCovers::ERole::DETAILS, &Image));
	Drain(Covers, Http, 1);
	EXPECT_EQ(Http.m_vUrls, vUrls);
	Covers.Shutdown(&m_Graphics);
}

TEST_F(MapCoversAdmission, QueueLimitKeepsAllNinetySixTasksAndTheRejectedUrlCanRetry)
{
	CQmMapCovers Covers(CreateResponse);
	CHttpRecorder Http;
	const auto vUrls = Urls(97);
	Queue(Covers, vUrls, vUrls.size());
	EXPECT_TRUE(Covers.Loading());
	Drain(Covers, Http, 96);
	EXPECT_EQ(Http.m_vUrls, (std::vector<std::string>(vUrls.begin(), vUrls.end() - 1)));
	CQmMapCovers::SImage Image;
	EXPECT_FALSE(Covers.Get(vUrls.back().c_str(), CQmMapCovers::ERole::ROW, &Image));
	EXPECT_TRUE(Covers.Loading());
	Drain(Covers, Http, 1);
	EXPECT_EQ(Http.m_vUrls, vUrls);
	Covers.Shutdown(&m_Graphics);
}
