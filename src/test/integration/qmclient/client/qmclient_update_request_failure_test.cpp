#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/qmclient/update_request.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using CRequest = qm_update::CUpdateRequest;
	using EState = CRequest::EState;
	const std::string s_Api = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
	const std::string s_Release = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[]})";

	// 网络和取消是替换边界；正文接收、完成发布与发布信息解析沿用生产实现。
	class CMetadataResponse : public IHttpRequest
	{
	public:
		bool m_ThrowOnAbort = false;
		int m_Aborts = 0;
		explicit CMetadataResponse(const std::string &Url) :
			IHttpRequest(Url.c_str()) {}
		void Header(const char *) override {}
		void Reply(int Status, const std::string &Body, std::optional<int64_t> RetryAfter = {})
		{
			m_StatusCode = Status;
			m_ResultRetryAfterSeconds = RetryAfter;
			OnData(Body.data(), Body.size());
			OnCompletionInternal(Status == 200 ? EHttpState::DONE : EHttpState::ERROR);
		}
		void Abort() override
		{
			++m_Aborts;
			IHttpRequest::Abort();
			if(m_ThrowOnAbort)
				throw std::runtime_error("metadata cancellation failed");
			OnCompletionInternal(EHttpState::ABORTED);
		}
	};

	class QmUpdateMetadataFailure : public ::testing::Test
	{
	protected:
		qm_update::CSourceRegistry m_Sources;
		CRequest m_Request;
		SQmClientUpdateRelease m_ParsedRelease;
		std::vector<std::shared_ptr<CMetadataResponse>> m_vResponses;
		int m_FactoryCalls = 0;
		int m_DirectCalls = 0;
		int m_ValidationCalls = 0;
		int m_FailFactoryAt = -1;
		int m_NullFactoryAt = -1;
		bool m_Proxied = false;
		bool m_EnableDirect = false;
		bool m_FailDirect = false;
		bool m_NullDirect = false;
		bool m_FailValidation = false;
		bool m_UnknownValidationFailure = false;

		std::shared_ptr<CMetadataResponse> Create(const std::string &Url)
		{
			auto Response = std::make_shared<CMetadataResponse>(Url);
			if(m_Proxied)
				Response->Proxy("http://127.0.0.1:7890");
			m_vResponses.push_back(Response);
			return Response;
		}
		bool Validate(const IHttpRequest &Request)
		{
			++m_ValidationCalls;
			if(m_FailValidation)
				throw std::bad_alloc();
			if(m_UnknownValidationFailure)
				throw 17;
			unsigned char *pBody = nullptr;
			size_t Size = 0;
			Request.Result(&pBody, &Size);
			char aError[256];
			return ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pBody), Size, "3.3", m_ParsedRelease, aError, sizeof(aError));
		}
		void Begin()
		{
			CRequest::TFactory Direct;
			if(m_EnableDirect)
				Direct = [&](const std::string &Url) -> std::shared_ptr<IHttpRequest> {
					++m_DirectCalls;
					if(m_FailDirect)
						throw std::bad_alloc();
					if(m_NullDirect)
						return nullptr;
					auto Response = Create(Url);
					Response->Proxy("");
					return Response;
				};
			m_Request.Begin(m_Sources, s_Api, qm_update::API, 0, [&](const std::string &Url) -> std::shared_ptr<IHttpRequest> {
					const int Index = m_FactoryCalls++;
					if(Index == m_FailFactoryAt)
						throw std::bad_alloc();
					if(Index == m_NullFactoryAt)
						return nullptr;
					return Create(Url); }, [&](const IHttpRequest &Request) { return Validate(Request); }, std::move(Direct));
		}
		void ExpectLocalFailure()
		{
			EXPECT_EQ(m_Request.State(), EState::FAILED);
			EXPECT_TRUE(m_Request.LocalError());
			const int Factories = m_FactoryCalls, Directs = m_DirectCalls, Validations = m_ValidationCalls;
			EXPECT_EQ(m_Request.Poll(100), EState::FAILED);
			EXPECT_EQ(m_Request.Poll(101), EState::FAILED);
			EXPECT_EQ(m_FactoryCalls, Factories);
			EXPECT_EQ(m_DirectCalls, Directs);
			EXPECT_EQ(m_ValidationCalls, Validations);
		}
	};
}

TEST_F(QmUpdateMetadataFailure, FirstFactoryExceptionPublishesFailureWithoutARequest)
{
	m_FailFactoryAt = 0;
	EXPECT_NO_THROW(Begin());
	ExpectLocalFailure();
	EXPECT_EQ(m_Request.Request(), nullptr);
	EXPECT_TRUE(m_vResponses.empty());
}

TEST_F(QmUpdateMetadataFailure, NullFactoryResultIsLocalFailureWithoutSourceFallback)
{
	m_NullFactoryAt = 0;
	Begin();
	ExpectLocalFailure();
	EXPECT_EQ(m_FactoryCalls, 1);
	EXPECT_FALSE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, ValidatorAllocationExceptionIsTerminalAndNotContentRejection)
{
	Begin();
	ASSERT_EQ(m_vResponses.size(), 1u);
	m_vResponses[0]->Reply(200, s_Release);
	m_FailValidation = true;
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_FALSE(m_Request.RejectedContent());
	EXPECT_EQ(m_ValidationCalls, 1);
}

TEST_F(QmUpdateMetadataFailure, UnknownValidatorExceptionUsesTheSameLocalTerminalState)
{
	Begin();
	ASSERT_EQ(m_vResponses.size(), 1u);
	m_vResponses[0]->Reply(200, s_Release);
	m_UnknownValidationFailure = true;
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_FALSE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, LocalMirrorValidationFailureDoesNotCoolDownTheHealthyMirror)
{
	Begin();
	m_vResponses[0]->Reply(503, "unavailable");
	ASSERT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2u);
	m_vResponses[1]->Reply(200, s_Release);
	m_FailValidation = true;
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	ExpectLocalFailure();
	const auto Candidates = m_Sources.Candidates(s_Api, qm_update::API, 3);
	EXPECT_NE(std::find_if(Candidates.begin(), Candidates.end(), [](const qm_update::CSource &Source) { return Source.m_Group == "gh-proxy"; }), Candidates.end());
}

TEST_F(QmUpdateMetadataFailure, NewCheckAfterValidationFailureCanParseAndPublishRealReleaseInfo)
{
	Begin();
	m_vResponses[0]->Reply(200, s_Release);
	m_FailValidation = true;
	ASSERT_EQ(m_Request.Poll(1), EState::FAILED);
	m_FailValidation = false;
	Begin();
	ASSERT_EQ(m_vResponses.size(), 2u);
	EXPECT_FALSE(m_Request.LocalError());
	m_vResponses[1]->Reply(200, s_Release);
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
	EXPECT_TRUE(m_ParsedRelease.m_NewVersion);
	EXPECT_STREQ(m_ParsedRelease.m_aVersion, "3.4");
	EXPECT_FALSE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, FirstFactoryFailureCanBeReplacedByAFreshCheck)
{
	m_FailFactoryAt = 0;
	Begin();
	ASSERT_TRUE(m_Request.LocalError());
	m_FailFactoryAt = -1;
	Begin();
	ASSERT_EQ(m_vResponses.size(), 1u);
	EXPECT_EQ(m_Request.State(), EState::RUNNING);
	EXPECT_FALSE(m_Request.LocalError());
	m_vResponses[0]->Reply(200, s_Release);
	EXPECT_EQ(m_Request.Poll(1), EState::SUCCEEDED);
}

TEST_F(QmUpdateMetadataFailure, MirrorFactoryFailureStopsInsteadOfRepeatingThePreviousResponse)
{
	Begin();
	m_vResponses[0]->Reply(503, "unavailable");
	m_FailFactoryAt = 1;
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_EQ(m_FactoryCalls, 2);
	EXPECT_EQ(m_vResponses.size(), 1u);
}

TEST_F(QmUpdateMetadataFailure, DirectFallbackFactoryExceptionDoesNotStartAMirrorAfterLocalFailure)
{
	m_EnableDirect = true;
	m_Proxied = true;
	m_FailDirect = true;
	Begin();
	m_vResponses[0]->Reply(403, "proxy refused");
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_EQ(m_DirectCalls, 1);
	EXPECT_EQ(m_FactoryCalls, 1);
}

TEST_F(QmUpdateMetadataFailure, NullDirectFallbackIsNotRetriedAsANetworkFailure)
{
	m_EnableDirect = true;
	m_Proxied = true;
	m_NullDirect = true;
	Begin();
	m_vResponses[0]->Reply(403, "proxy refused");
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_EQ(m_DirectCalls, 1);
}

TEST_F(QmUpdateMetadataFailure, CancelAfterLocalFailureNeverRestartsTheAttempt)
{
	m_FailFactoryAt = 0;
	Begin();
	m_Request.Cancel();
	EXPECT_EQ(m_Request.State(), EState::CANCELLED);
	EXPECT_EQ(m_Request.Poll(100), EState::CANCELLED);
	EXPECT_EQ(m_Request.Source(), nullptr);
	EXPECT_EQ(m_FactoryCalls, 1);
}

TEST_F(QmUpdateMetadataFailure, ThrowingAbortStillReleasesRequestAndPublishesCancelledState)
{
	Begin();
	m_vResponses[0]->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(m_Request.Cancel());
	EXPECT_EQ(m_Request.State(), EState::CANCELLED);
	EXPECT_EQ(m_Request.Request(), nullptr);
	EXPECT_EQ(m_Request.Source(), nullptr);
	EXPECT_TRUE(m_vResponses[0]->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(100), EState::CANCELLED);
}

TEST_F(QmUpdateMetadataFailure, DestructorDoesNotPropagateExternalCancellationFailure)
{
	auto Request = std::make_unique<CRequest>();
	Request->Begin(m_Sources, s_Api, qm_update::API, 0, [&](const std::string &Url) { return Create(Url); }, [&](const IHttpRequest &Response) { return Validate(Response); });
	ASSERT_EQ(m_vResponses.size(), 1u);
	m_vResponses[0]->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(Request.reset());
	EXPECT_TRUE(m_vResponses[0]->IsAbortRequested());
}

TEST_F(QmUpdateMetadataFailure, NewCheckReplacesThrowingCancelledRequestAndIgnoresItsLateResponse)
{
	Begin();
	const auto Previous = m_vResponses[0];
	Previous->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(Begin());
	ASSERT_EQ(m_vResponses.size(), 2u);
	Previous->Reply(200, s_Release);
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	EXPECT_EQ(m_ValidationCalls, 0);
	m_vResponses[1]->Reply(200, s_Release);
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
}

TEST_F(QmUpdateMetadataFailure, TimeoutCancellationExceptionBecomesOneLocalFailure)
{
	Begin();
	m_vResponses[0]->m_ThrowOnAbort = true;
	EXPECT_EQ(m_Request.Poll(10), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_EQ(m_vResponses[0]->m_Aborts, 1);
}

TEST_F(QmUpdateMetadataFailure, LocalFailureAfterARejectedResponsePreservesRejectionHistory)
{
	Begin();
	m_vResponses[0]->Reply(200, "<html>blocked</html>");
	m_FailFactoryAt = 1;
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	ExpectLocalFailure();
	EXPECT_TRUE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, OrdinaryTransportExhaustionDoesNotSetLocalError)
{
	Begin();
	m_vResponses[0]->Reply(503, "unavailable");
	ASSERT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2u);
	m_vResponses[1]->Reply(503, "unavailable");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_FALSE(m_Request.LocalError());
	EXPECT_FALSE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, OrdinaryContentRejectionDoesNotSetLocalError)
{
	Begin();
	m_vResponses[0]->Reply(200, "<html>blocked</html>");
	ASSERT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2u);
	m_vResponses[1]->Reply(200, "<html>blocked</html>");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_FALSE(m_Request.LocalError());
	EXPECT_TRUE(m_Request.RejectedContent());
}

TEST_F(QmUpdateMetadataFailure, CoolingAllSourcesIsUnavailableRatherThanLocalPreparationFailure)
{
	for(const auto &Source : m_Sources.Candidates(s_Api, qm_update::API, 0))
		m_Sources.Failed(Source, 0, 900);
	Begin();
	EXPECT_EQ(m_Request.State(), EState::FAILED);
	EXPECT_EQ(m_Request.Request(), nullptr);
	EXPECT_FALSE(m_Request.LocalError());
	EXPECT_EQ(m_FactoryCalls, 0);
}
