#include <game/client/components/qmclient/update_survey.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	// 只替换传输/工厂失败；正文、采样和完成状态沿用 IHttpRequest。
	class CSurveyResponse : public IHttpRequest
	{
	public:
		bool m_ThrowOnAbort = false;
		int m_Aborts = 0;
		explicit CSurveyResponse(const std::string &Url) :
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
				throw std::runtime_error("survey cancellation failed");
			OnCompletionInternal(EHttpState::ABORTED);
		}
	};

	class QmUpdateSurveyFailure : public ::testing::Test
	{
	protected:
		const std::string m_Package = "https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.zip";
		qm_update::CSourceRegistry m_Sources;
		qm_update::CSourceSurvey m_Survey;
		std::vector<std::shared_ptr<CSurveyResponse>> m_vSignatures;
		std::vector<std::shared_ptr<CSurveyResponse>> m_vSamples;
		int m_FactoryCalls = 0;
		int m_SampleCalls = 0;
		int m_DirectCalls = 0;
		int m_FailFactoryAt = -1;
		int m_NullFactoryAt = -1;
		int m_FailSampleAt = -1;
		int m_NullSampleAt = -1;
		bool m_Proxied = false;
		bool m_FailDirect = false;
		bool m_NullDirect = false;
		bool m_FailDirectSample = false;
		bool m_NullDirectSample = false;

		std::shared_ptr<CSurveyResponse> Create(const std::string &Url, bool Direct, bool Sample)
		{
			auto Response = std::make_shared<CSurveyResponse>(Url);
			if(m_Proxied && !Direct)
				Response->Proxy("http://127.0.0.1:7890");
			if(Sample)
			{
				Response->ResponseSample(qm_update::CSourceSurvey::SAMPLE_BYTES);
				m_vSamples.push_back(Response);
			}
			else
				m_vSignatures.push_back(Response);
			return Response;
		}
		void Begin(qm_update::CSourceSurvey &Survey)
		{
			Survey.Begin(m_Sources.Candidates(m_Package, qm_update::RELEASE, 0), m_Package + ".sig", 0, [this](const std::string &Url) -> std::shared_ptr<IHttpRequest> {
					const int Index = m_FactoryCalls++;
					if(Index == m_FailFactoryAt)
						throw std::bad_alloc();
					if(Index == m_NullFactoryAt)
						return nullptr;
					return Create(Url, false, false); }, [this](const std::string &Url) -> std::shared_ptr<IHttpRequest> {
					++m_DirectCalls;
					if(m_FailDirect)
						throw std::bad_alloc();
					if(m_NullDirect)
						return nullptr;
					return Create(Url, true, false); }, m_Package, [this](const std::string &Url, bool Direct) -> std::shared_ptr<IHttpRequest> {
					const int Index = m_SampleCalls++;
					if(Index == m_FailSampleAt || (Direct && m_FailDirectSample))
						throw std::bad_alloc();
					if(Index == m_NullSampleAt || (Direct && m_NullDirectSample))
						return nullptr;
					return Create(Url, Direct, true); });
		}
		void Begin() { Begin(m_Survey); }
		void ExpectLocalFailure()
		{
			EXPECT_FALSE(m_Survey.Running());
			EXPECT_TRUE(m_Survey.LocalError());
			EXPECT_TRUE(m_Survey.Candidates().empty());
			const int Factories = m_FactoryCalls, Samples = m_SampleCalls, Directs = m_DirectCalls;
			EXPECT_TRUE(m_Survey.Poll(m_Sources, 100));
			EXPECT_TRUE(m_Survey.Poll(m_Sources, 101));
			EXPECT_EQ(m_FactoryCalls, Factories);
			EXPECT_EQ(m_SampleCalls, Samples);
			EXPECT_EQ(m_DirectCalls, Directs);
		}
		void ExpectSourcesAvailable()
		{
			EXPECT_EQ(m_Sources.Candidates(m_Package, qm_update::RELEASE, 2).size(), 3u);
		}
		void FinishCurrentSurvey(size_t SignatureStart = 0, size_t SampleStart = 0)
		{
			for(size_t Index = SignatureStart; Index < m_vSignatures.size(); ++Index)
				m_vSignatures[Index]->Reply(200, std::string(64, 's'));
			ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
			for(size_t Index = SampleStart; Index < m_vSamples.size(); ++Index)
				m_vSamples[Index]->Reply(200, std::string("PK\x03\x04payload"));
			ASSERT_TRUE(m_Survey.Poll(m_Sources, 2));
			EXPECT_FALSE(m_Survey.LocalError());
			EXPECT_EQ(m_Survey.Candidates().size(), 3u);
		}
	};
}

TEST_F(QmUpdateSurveyFailure, FirstFactoryExceptionPublishesLocalFailure)
{
	m_FailFactoryAt = 0;
	EXPECT_NO_THROW(Begin());
	ExpectLocalFailure();
	EXPECT_TRUE(m_vSignatures.empty());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, LaterFactoryExceptionCancelsAlreadyStartedSignatures)
{
	m_FailFactoryAt = 2;
	EXPECT_NO_THROW(Begin());
	ExpectLocalFailure();
	ASSERT_EQ(m_vSignatures.size(), 2u);
	for(const auto &Response : m_vSignatures)
		EXPECT_EQ(Response->m_Aborts, 1);
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, NullSignatureFactoryCancelsSiblingsWithoutDegradingSources)
{
	m_NullFactoryAt = 1;
	Begin();
	ExpectLocalFailure();
	ASSERT_EQ(m_vSignatures.size(), 1u);
	EXPECT_TRUE(m_vSignatures.front()->IsAbortRequested());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, SampleFactoryExceptionCancelsPendingSignatureSiblings)
{
	Begin();
	m_FailSampleAt = 0;
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 1));
	ExpectLocalFailure();
	EXPECT_TRUE(m_vSignatures[1]->IsAbortRequested());
	EXPECT_TRUE(m_vSignatures[2]->IsAbortRequested());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, NullSampleFactoryIsLocalFailureWithoutSourcePenalty)
{
	Begin();
	m_NullSampleAt = 0;
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 1));
	ExpectLocalFailure();
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, DirectSignatureFactoryExceptionCancelsMirrorSignatures)
{
	m_Proxied = true;
	m_FailDirect = true;
	Begin();
	m_vSignatures.front()->Reply(502, "");
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 1));
	ExpectLocalFailure();
	EXPECT_EQ(m_DirectCalls, 1);
	EXPECT_TRUE(m_vSignatures[1]->IsAbortRequested());
	EXPECT_TRUE(m_vSignatures[2]->IsAbortRequested());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, NullDirectSignatureFactoryDoesNotStartAnotherRoute)
{
	m_Proxied = true;
	m_NullDirect = true;
	Begin();
	m_vSignatures.front()->Reply(502, "");
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 1));
	ExpectLocalFailure();
	EXPECT_EQ(m_DirectCalls, 1);
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, DirectSampleFactoryExceptionCancelsRemainingProbes)
{
	m_Proxied = true;
	Begin();
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
	ASSERT_EQ(m_vSamples.size(), 1u);
	m_vSamples.front()->Reply(502, "");
	m_FailDirectSample = true;
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 2));
	ExpectLocalFailure();
	EXPECT_EQ(m_SampleCalls, 2);
	EXPECT_TRUE(m_vSignatures[1]->IsAbortRequested());
	EXPECT_TRUE(m_vSignatures[2]->IsAbortRequested());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, NullDirectSampleFactoryIsTerminalLocalFailure)
{
	m_Proxied = true;
	Begin();
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
	ASSERT_EQ(m_vSamples.size(), 1u);
	m_vSamples.front()->Reply(502, "");
	m_NullDirectSample = true;
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 2));
	ExpectLocalFailure();
	EXPECT_EQ(m_SampleCalls, 2);
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, TimeoutCancellationExceptionStillCancelsSiblings)
{
	Begin();
	m_vSignatures.front()->m_ThrowOnAbort = true;
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 10));
	ExpectLocalFailure();
	for(const auto &Response : m_vSignatures)
		EXPECT_EQ(Response->m_Aborts, 1);
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, ExplicitCancelContinuesAfterOneAbortThrows)
{
	Begin();
	m_vSignatures.front()->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(m_Survey.Cancel());
	EXPECT_FALSE(m_Survey.Running());
	EXPECT_FALSE(m_Survey.LocalError());
	EXPECT_TRUE(m_Survey.Candidates().empty());
	for(const auto &Response : m_vSignatures)
		EXPECT_EQ(Response->m_Aborts, 1);
	EXPECT_NO_THROW(m_Survey.Cancel());
	for(const auto &Response : m_vSignatures)
		EXPECT_EQ(Response->m_Aborts, 1);
}

TEST_F(QmUpdateSurveyFailure, DestructorCancelsSiblingsDespiteOneAbortException)
{
	auto Survey = std::make_unique<qm_update::CSourceSurvey>();
	Begin(*Survey);
	ASSERT_EQ(m_vSignatures.size(), 3u);
	m_vSignatures.front()->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(Survey.reset());
	for(const auto &Response : m_vSignatures)
		EXPECT_EQ(Response->m_Aborts, 1);
}

TEST_F(QmUpdateSurveyFailure, NewSurveyAfterLocalFailureCanCompleteNormally)
{
	m_FailFactoryAt = 1;
	Begin();
	ExpectLocalFailure();
	const size_t OldSignatures = m_vSignatures.size();
	m_FailFactoryAt = -1;
	Begin();
	EXPECT_TRUE(m_Survey.Running());
	EXPECT_FALSE(m_Survey.LocalError());
	FinishCurrentSurvey(OldSignatures);
}

TEST_F(QmUpdateSurveyFailure, LateOldSampleAfterFailureDoesNotPopulateNewCandidates)
{
	Begin();
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
	ASSERT_EQ(m_vSamples.size(), 1u);
	m_FailSampleAt = 1;
	m_vSignatures[1]->Reply(200, std::string(64, 's'));
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 2));
	ExpectLocalFailure();
	const size_t OldSignatures = m_vSignatures.size(), OldSamples = m_vSamples.size();
	m_FailSampleAt = -1;
	Begin();
	m_vSamples.front()->Reply(200, std::string("PK\x03\x04payload"));
	EXPECT_FALSE(m_Survey.Poll(m_Sources, 0.5));
	EXPECT_TRUE(m_Survey.Candidates().empty());
	FinishCurrentSurvey(OldSignatures, OldSamples);
}

TEST_F(QmUpdateSurveyFailure, RepeatedBeginCancelsOldProbesBeforeStartingNewSurvey)
{
	Begin();
	const size_t OldSignatures = m_vSignatures.size();
	Begin();
	for(size_t Index = 0; Index < OldSignatures; ++Index)
	{
		EXPECT_TRUE(m_vSignatures[Index]->IsAbortRequested());
		m_vSignatures[Index]->Reply(502, "");
	}
	FinishCurrentSurvey(OldSignatures);
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, LaterLocalFailureDiscardsPreviouslyReachableMirror)
{
	Begin();
	m_vSignatures[1]->Reply(200, std::string(64, 's'));
	ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
	ASSERT_EQ(m_vSamples.size(), 1u);
	m_vSamples.front()->Reply(200, std::string("PK\x03\x04payload"));
	ASSERT_FALSE(m_Survey.Poll(m_Sources, 2));
	m_FailSampleAt = 1;
	m_vSignatures.front()->Reply(200, std::string(64, 's'));
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 3));
	ExpectLocalFailure();
	EXPECT_TRUE(m_vSignatures[2]->IsAbortRequested());
	ExpectSourcesAvailable();
}

TEST_F(QmUpdateSurveyFailure, LocalFailurePreservesPreviouslyRecordedRateLimit)
{
	m_Proxied = true;
	Begin();
	m_vSignatures.front()->Reply(429, "", 600);
	m_vSignatures[1]->Reply(200, std::string(64, 's'));
	m_FailSampleAt = 0;
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 1));
	ExpectLocalFailure();
	EXPECT_EQ(m_DirectCalls, 0);
	const auto Candidates = m_Sources.Candidates(m_Package, qm_update::RELEASE, 2);
	ASSERT_EQ(Candidates.size(), 2u);
	for(const auto &Source : Candidates)
		EXPECT_FALSE(Source.m_Prefix.empty());
	EXPECT_EQ(m_Sources.Candidates(m_Package, qm_update::RELEASE, 601).size(), 3u);
}
