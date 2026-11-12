// 真实鉴权状态机、HTTP 请求对象及解析器协作；只控制 HTTP 完成和两个时钟输入。
#include <base/mem.h>

#include <engine/http.h>

#include <game/client/components/qmclient/music_lyrics/qm_spotify_auth.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr std::string_view SECRET = "{\"7\":[1,2]}";
	constexpr std::string_view SERVER_TIME = "{\"serverTime\":1780000000}";

	enum class EFailure
	{
		NONE,
		ALLOCATION,
		LENGTH,
		OTHER,
	};
	struct SNetworkFaults
	{
		EFailure m_Factory = EFailure::NONE;
		EFailure m_Header = EFailure::NONE;
		EFailure m_Run = EFailure::NONE;
		int m_Aborts = 0;
		int m_Destroyed = 0;
	};
	thread_local SNetworkFaults *s_pFaults = nullptr;

	void ThrowFailure(EFailure Failure)
	{
		if(Failure == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(Failure == EFailure::LENGTH)
			throw std::length_error("controlled HTTP preparation limit");
		if(Failure == EFailure::OTHER)
			throw std::logic_error("unrelated HTTP factory failure");
	}

	class CResponse final : public IHttpRequest
	{
		SNetworkFaults *m_pFaults;

	public:
		std::vector<std::string> m_vHeaders;
		int m_Aborts = 0;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl), m_pFaults(s_pFaults) { WriteToMemory(); }
		~CResponse() override
		{
			if(m_pFaults != nullptr)
				++m_pFaults->m_Destroyed;
		}
		void Header(const char *pHeader) override
		{
			if(m_pFaults != nullptr)
				ThrowFailure(std::exchange(m_pFaults->m_Header, EFailure::NONE));
			m_vHeaders.emplace_back(pHeader);
		}
		void Begin() { m_State = EHttpState::RUNNING; }
		void Fail() { m_State = EHttpState::ERROR; }
		void Abort() override
		{
			++m_Aborts;
			if(m_pFaults != nullptr)
				++m_pFaults->m_Aborts;
			IHttpRequest::Abort();
		}
		int64_t ResponseLimit() const { return m_MaxResponseSize; }
		bool Finish(int Status, std::string_view Text)
		{
			auto *pBuffer = static_cast<unsigned char *>(malloc(Text.size() + 1));
			if(pBuffer == nullptr)
				return false;
			if(!Text.empty())
				mem_copy(pBuffer, Text.data(), Text.size());
			pBuffer[Text.size()] = 0;
			free(m_pBuffer);
			m_pBuffer = pBuffer;
			m_ResponseLength = m_BufferSize = Text.size();
			m_StatusCode = Status;
			m_State = EHttpState::DONE;
			return true;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		if(s_pFaults != nullptr)
			ThrowFailure(std::exchange(s_pFaults->m_Factory, EFailure::NONE));
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder final : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			if(s_pFaults != nullptr)
				ThrowFailure(std::exchange(s_pFaults->m_Run, EFailure::NONE));
			auto pResponse = std::static_pointer_cast<CResponse>(std::move(pRequest));
			pResponse->Begin();
			m_vRequests.push_back(std::move(pResponse));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class SpotifyAuth : public ::testing::Test
	{
	protected:
		SNetworkFaults m_Faults;
		SNetworkFaults *m_pPreviousFaults = nullptr;
		CHttpRecorder m_Http;
		CSpotifyAuthPipeline m_Auth{CreateResponse};
		int64_t m_MonotonicNowMs = 1000;
		int64_t m_UnixNowMs = INT64_C(1780000000000);
		const std::string m_SpDc = "test-cookie";

		void SetUp() override { m_pPreviousFaults = std::exchange(s_pFaults, &m_Faults); }
		void TearDown() override
		{
			m_Auth.Reset();
			s_pFaults = m_pPreviousFaults;
		}
		void Poll() { m_Auth.Poll(&m_Http, m_SpDc, m_MonotonicNowMs, m_UnixNowMs); }
		std::shared_ptr<CResponse> Last() { return m_Http.m_vRequests.back(); }
		bool ReachStage(int Stage)
		{
			Poll();
			if(m_Http.m_vRequests.size() != 1 || Stage == 0)
				return m_Http.m_vRequests.size() == 1;
			if(!Last()->Finish(200, SECRET))
				return false;
			Poll();
			Poll();
			if(m_Http.m_vRequests.size() != 2 || Stage == 1)
				return m_Http.m_vRequests.size() == 2;
			if(!Last()->Finish(200, SERVER_TIME))
				return false;
			Poll();
			return m_Http.m_vRequests.size() == 3;
		}
		bool FinishToken(int64_t Expiration, const char *pToken = "new-token")
		{
			return Last()->Finish(200, "{\"accessToken\":\"" + std::string(pToken) +
							   "\",\"accessTokenExpirationTimestampMs\":" + std::to_string(Expiration) + ",\"isAnonymous\":false}");
		}
	};
}

TEST_F(SpotifyAuth, CompletedTokenPublishesTheRealFullStageChainAndKeepsRequestPolicies)
{
	ASSERT_TRUE(ReachStage(2));
	EXPECT_FALSE(m_Auth.HasToken());
	EXPECT_NE(std::string_view(m_Http.m_vRequests[1]->Url()).find("/api/server-time"), std::string_view::npos);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/token?reason=init"), std::string_view::npos);
	EXPECT_NE(std::find(Last()->m_vHeaders.begin(), Last()->m_vHeaders.end(), "Cookie: sp_dc=test-cookie"), Last()->m_vHeaders.end());
	for(const auto &pRequest : m_Http.m_vRequests)
	{
		EXPECT_EQ(pRequest->RequestTimeoutMs(), 10000);
		EXPECT_EQ(pRequest->ResponseLimit(), 2 << 20);
		EXPECT_FALSE(pRequest->FailOnErrorStatusEnabled());
	}
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
	EXPECT_EQ(m_Auth.AccessToken(), "new-token");
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
}

TEST_F(SpotifyAuth, PendingSecretTimeAndTokenNeverResubmitTheirRequests)
{
	Poll();
	for(int Frame = 0; Frame < 3; ++Frame)
		Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	ASSERT_TRUE(Last()->Finish(200, SECRET));
	Poll();
	Poll();
	for(int Frame = 0; Frame < 3; ++Frame)
		Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	for(int Frame = 0; Frame < 3; ++Frame)
		Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 3u);
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
}

TEST_F(SpotifyAuth, RefreshDuringEachPendingStageKeepsTheSameRequestAndCompletesTheChain)
{
	for(int Stage = 0; Stage < 3; ++Stage)
	{
		SCOPED_TRACE(Stage);
		m_Auth.Reset();
		m_Http.m_vRequests.clear();
		ASSERT_TRUE(ReachStage(Stage));
		const auto pPending = Last();
		const size_t PendingCount = m_Http.m_vRequests.size();
		m_Auth.RequestRefresh();
		m_Auth.RequestRefresh();
		for(int Frame = 0; Frame < 3; ++Frame)
		{
			Poll();
			EXPECT_EQ(m_Http.m_vRequests.size(), PendingCount);
			EXPECT_EQ(Last(), pPending);
			EXPECT_EQ(pPending->m_Aborts, 0);
			EXPECT_FALSE(m_Auth.HasToken());
		}
		if(Stage == 0)
		{
			ASSERT_TRUE(pPending->Finish(200, SECRET));
			Poll();
			// secret已解析、server-time尚未提交的阶段也直接接续。
			m_Auth.RequestRefresh();
			Poll();
			ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
		}
		if(Stage < 2)
		{
			ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
			Poll();
		}
		ASSERT_EQ(m_Http.m_vRequests.size(), 3u);
		ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
		Poll();
		EXPECT_TRUE(m_Auth.HasToken());
		EXPECT_EQ(m_Auth.AccessToken(), "new-token");
		EXPECT_EQ(pPending->m_Aborts, 0);
		EXPECT_EQ(m_Faults.m_Aborts, 0);
	}
}

TEST_F(SpotifyAuth, RefreshFromAValidTokenReusesTheSecretAndPublishesTheReplacement)
{
	ASSERT_TRUE(ReachStage(2));
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000, "old-token"));
	Poll();
	ASSERT_TRUE(m_Auth.HasToken());
	m_Auth.RequestRefresh();
	EXPECT_FALSE(m_Auth.HasToken());
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 5u);
	EXPECT_NE(std::string_view(Last()->Url()).find("reason=init"), std::string_view::npos);
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 7200000, "replacement-token"));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
	EXPECT_EQ(m_Auth.AccessToken(), "replacement-token");
	EXPECT_EQ(m_Faults.m_Aborts, 0);
}

TEST_F(SpotifyAuth, RefreshAfterFailureKeepsTheExistingRetryDeadline)
{
	ASSERT_TRUE(ReachStage(2));
	Last()->Fail();
	Poll();
	EXPECT_FALSE(m_Auth.HasToken());
	m_Auth.RequestRefresh();
	m_MonotonicNowMs += 4999;
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	m_Auth.RequestRefresh();
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	++m_MonotonicNowMs;
	Poll();
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 5u);
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
	EXPECT_EQ(m_Auth.AccessToken(), "new-token");
}

TEST_F(SpotifyAuth, TokenNetworkFailureRetriesAfterFiveMonotonicSecondsDespiteAUnixJump)
{
	ASSERT_TRUE(ReachStage(2));
	Last()->Fail();
	Poll();
	EXPECT_FALSE(m_Auth.HasToken());
	m_MonotonicNowMs += 4999;
	m_UnixNowMs += INT64_C(90000000000);
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	++m_MonotonicNowMs;
	Poll();
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 5u);
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
}

TEST_F(SpotifyAuth, CookieRejectionUsesThirtyMonotonicSecondsDespiteAUnixClockRollback)
{
	ASSERT_TRUE(ReachStage(2));
	ASSERT_TRUE(Last()->Finish(403, "{}"));
	Poll();
	EXPECT_FALSE(m_Auth.HasToken());
	m_UnixNowMs -= INT64_C(90000000000);
	m_MonotonicNowMs += 29999;
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	++m_MonotonicNowMs;
	Poll();
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
}

TEST_F(SpotifyAuth, LegacyParametersAreAttemptedOnceBeforeTheNormalFailureBackoff)
{
	ASSERT_TRUE(ReachStage(2));
	ASSERT_TRUE(Last()->Finish(400, "{}"));
	Poll();
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 5u);
	EXPECT_NE(std::string_view(Last()->Url()).find("reason=transport"), std::string_view::npos);
	EXPECT_NE(std::string_view(Last()->Url()).find("&ts=1780000000"), std::string_view::npos);
	ASSERT_TRUE(Last()->Finish(400, "{}"));
	Poll();
	m_MonotonicNowMs += 4999;
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 5u);
	++m_MonotonicNowMs;
	Poll();
	Poll();
	EXPECT_EQ(m_Http.m_vRequests.size(), 6u);
}

TEST_F(SpotifyAuth, ResetAbortsEveryPendingStageAndRejectsItsLateCompletionBeforeARealRetry)
{
	for(int Stage = 0; Stage < 3; ++Stage)
	{
		SCOPED_TRACE(Stage);
		m_Auth.Reset();
		m_Http.m_vRequests.clear();
		ASSERT_TRUE(ReachStage(Stage));
		const auto pOld = Last();
		m_Auth.Reset();
		EXPECT_EQ(pOld->m_Aborts, 1);
		EXPECT_FALSE(m_Auth.HasToken());
		EXPECT_TRUE(m_Auth.AccessToken().empty());
		ASSERT_TRUE(pOld->Finish(200, "{\"accessToken\":\"old-token\",\"accessTokenExpirationTimestampMs\":1780003600000}"));
		const size_t OldCount = m_Http.m_vRequests.size();
		Poll();
		ASSERT_EQ(m_Http.m_vRequests.size(), OldCount + 1);
		EXPECT_FALSE(m_Auth.HasToken());
		ASSERT_TRUE(Last()->Finish(200, SECRET));
		Poll();
		Poll();
		ASSERT_EQ(m_Http.m_vRequests.size(), OldCount + 2);
		ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
		Poll();
		ASSERT_EQ(m_Http.m_vRequests.size(), OldCount + 3);
		ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
		Poll();
		EXPECT_TRUE(m_Auth.HasToken());
		EXPECT_EQ(m_Auth.AccessToken(), "new-token");
	}
}

TEST_F(SpotifyAuth, UnixExpiryStartsRefreshExactlyAtTheOneMinuteBoundary)
{
	ASSERT_TRUE(ReachStage(2));
	const int64_t Expiration = m_UnixNowMs + 3600000;
	ASSERT_TRUE(FinishToken(Expiration));
	Poll();
	ASSERT_TRUE(m_Auth.HasToken());
	m_UnixNowMs = Expiration - 60001;
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
	EXPECT_EQ(m_Http.m_vRequests.size(), 3u);
	++m_UnixNowMs;
	Poll();
	EXPECT_FALSE(m_Auth.HasToken());
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	ASSERT_TRUE(m_Auth.HasToken());
	m_UnixNowMs += 3600001;
	Poll();
	EXPECT_FALSE(m_Auth.HasToken());
}

TEST_F(SpotifyAuth, NonPositiveAbsoluteExpiryStillRejectsTheTokenResponse)
{
	for(const int64_t Expiration : {INT64_C(0), INT64_C(-1)})
	{
		SCOPED_TRACE(Expiration);
		m_Auth.Reset();
		m_Http.m_vRequests.clear();
		ASSERT_TRUE(ReachStage(2));
		ASSERT_TRUE(FinishToken(Expiration));
		Poll();
		EXPECT_FALSE(m_Auth.HasToken());
		EXPECT_TRUE(m_Auth.AccessToken().empty());
	}
}

TEST_F(SpotifyAuth, FailedSecretMirrorsKeepTheirOrderAndFallBackToTheBundledSecret)
{
	for(size_t Mirror = 0; Mirror < 3; ++Mirror)
	{
		Poll();
		ASSERT_EQ(m_Http.m_vRequests.size(), Mirror + 1);
		Last()->Fail();
		Poll();
	}
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 4u);
	EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
	ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
	Poll();
	ASSERT_EQ(m_Http.m_vRequests.size(), 5u);
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
}

TEST_F(SpotifyAuth, KnownHttpPreparationAndSubmissionFailuresBackOffThenRecoverTheRealChain)
{
	enum class EPoint
	{
		FACTORY,
		HEADER,
		RUN,
	};
	for(const auto Point : {EPoint::FACTORY, EPoint::HEADER, EPoint::RUN})
		for(const auto Failure : {EFailure::ALLOCATION, EFailure::LENGTH})
		{
			SCOPED_TRACE(static_cast<int>(Point));
			SCOPED_TRACE(static_cast<int>(Failure));
			m_Auth.Reset();
			m_Http.m_vRequests.clear();
			m_Faults = {};
			m_MonotonicNowMs = 1000;
			if(Point != EPoint::FACTORY)
			{
				Poll();
				ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
				ASSERT_TRUE(Last()->Finish(200, SECRET));
				Poll();
			}
			if(Point == EPoint::FACTORY)
				m_Faults.m_Factory = Failure;
			else if(Point == EPoint::HEADER)
				m_Faults.m_Header = Failure;
			else
				m_Faults.m_Run = Failure;
			EXPECT_NO_THROW(Poll());
			EXPECT_FALSE(m_Auth.HasToken());
			const size_t OldCount = Point == EPoint::FACTORY ? 0u : 1u;
			EXPECT_EQ(m_Http.m_vRequests.size(), OldCount);
			EXPECT_EQ(m_Faults.m_Aborts, Point == EPoint::RUN ? 1 : 0);
			EXPECT_EQ(m_Faults.m_Destroyed, Point == EPoint::FACTORY ? 0 : 1);
			m_MonotonicNowMs += 4999;
			Poll();
			EXPECT_EQ(m_Http.m_vRequests.size(), OldCount);
			++m_MonotonicNowMs;
			if(Point == EPoint::FACTORY)
				ASSERT_TRUE(ReachStage(2));
			else
			{
				Poll();
				Poll();
				ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
				EXPECT_NE(std::string_view(Last()->Url()).find("/api/server-time"), std::string_view::npos);
				ASSERT_TRUE(Last()->Finish(200, SERVER_TIME));
				Poll();
			}
			ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
			Poll();
			EXPECT_TRUE(m_Auth.HasToken());
		}
}

TEST_F(SpotifyAuth, ResourceFailureStillAllowsExplicitResetBeforeTheRetryDeadline)
{
	m_Faults.m_Factory = EFailure::ALLOCATION;
	EXPECT_NO_THROW(Poll());
	EXPECT_FALSE(m_Auth.HasToken());
	m_Auth.Reset();
	ASSERT_TRUE(ReachStage(2));
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
}

TEST_F(SpotifyAuth, UnrelatedFactoryExceptionPropagatesAndExplicitResetCanRestart)
{
	m_Faults.m_Factory = EFailure::OTHER;
	EXPECT_THROW(Poll(), std::logic_error);
	EXPECT_FALSE(m_Auth.HasToken());
	m_Auth.Reset();
	ASSERT_TRUE(ReachStage(2));
	ASSERT_TRUE(FinishToken(m_UnixNowMs + 3600000));
	Poll();
	EXPECT_TRUE(m_Auth.HasToken());
}
