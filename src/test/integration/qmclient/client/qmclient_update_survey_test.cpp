#include <base/system.h>

#include <game/client/components/qmclient/update_survey.h>

#include <gtest/gtest.h>

namespace
{
	class CSampleResponse : public IHttpRequest
	{
	public:
		explicit CSampleResponse(const std::string &Url) : IHttpRequest(Url.c_str()) {}
		void Header(const char *) override {}
		void Reply(int Status, const std::string &Body)
		{
			m_StatusCode = Status;
			OnData(Body.data(), Body.size());
			OnCompletionInternal(Status == 200 ? EHttpState::DONE : EHttpState::ERROR);
		}
		void Abort() override
		{
			IHttpRequest::Abort();
			OnCompletionInternal(EHttpState::ABORTED);
		}
		void Interrupted(const std::string &Body)
		{
			m_StatusCode = 200;
			OnData(Body.data(), Body.size());
			OnCompletionInternal(EHttpState::ERROR);
		}
	};

	class QmUpdateSurvey : public ::testing::Test
	{
	protected:
		qm_update::CSourceRegistry m_Sources;
		qm_update::CSourceSurvey m_Survey;
		std::vector<std::shared_ptr<CSampleResponse>> m_vSignatures;
		std::vector<std::shared_ptr<CSampleResponse>> m_vSamples;
		const std::string m_Package = "https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.zip";
		void Begin()
		{
			m_Survey.Begin(m_Sources.Candidates(m_Package, qm_update::RELEASE, 0), m_Package + ".sig", 0, [this](const std::string &Url) { auto Request = std::make_shared<CSampleResponse>(Url); m_vSignatures.push_back(Request); return Request; }, {}, m_Package, [this](const std::string &Url, bool Direct) { auto Request = std::make_shared<CSampleResponse>(Url); Request->ResponseSample(qm_update::CSourceSurvey::SAMPLE_BYTES); if(Direct) Request->Proxy(""); m_vSamples.push_back(Request); return Request; });
			for(const auto &Request : m_vSignatures)
				Request->Reply(200, std::string(64, 's'));
			ASSERT_FALSE(m_Survey.Poll(m_Sources, 1));
			ASSERT_EQ(m_vSamples.size(), 3U);
		}
		std::string PackageBytes(size_t Size = qm_update::CSourceSurvey::SAMPLE_BYTES)
		{
			std::string Bytes(Size, 'p');
			Bytes.replace(0, 4, "PK\x03\x04");
			return Bytes;
		}
	};
}

TEST_F(QmUpdateSurvey, SmallSignatureSuccessDoesNotChooseSlowOfficialPackageOverFastMirror)
{
	Begin();
	m_vSamples[1]->Reply(200, PackageBytes());
	m_vSamples[2]->Reply(200, PackageBytes());
	EXPECT_FALSE(m_Survey.Poll(m_Sources, 1.5));
	m_vSamples[0]->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 8));
	const auto Candidates = m_Survey.Candidates();
	ASSERT_EQ(Candidates.size(), 3U);
	EXPECT_FALSE(Candidates.front().m_Prefix.empty());
	EXPECT_TRUE(Candidates.back().m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, HealthyOfficialPackageStillHasPriority)
{
	Begin();
	for(const auto &Request : m_vSamples)
		Request->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 2));
	EXPECT_TRUE(m_Survey.Candidates().front().m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, OfficialBelow100KiBUsesMirrorEvenWithoutTwiceTheSpeed)
{
	Begin();
	for(const auto &Request : m_vSamples)
		Request->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 4));
	EXPECT_FALSE(m_Survey.Candidates().front().m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, HtmlPackageIsRejectedEvenWhenSignatureEndpointWorks)
{
	Begin();
	m_vSamples[0]->Reply(200, "<html>gateway error</html>");
	m_vSamples[1]->Reply(200, PackageBytes());
	m_vSamples[2]->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 2));
	ASSERT_EQ(m_Survey.Candidates().size(), 2U);
	for(const auto &Candidate : m_Survey.Candidates())
		EXPECT_FALSE(Candidate.m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, SampleTimeoutKeepsAValidSlowPrefixAsLastResort)
{
	Begin();
	m_vSamples[0]->Interrupted(PackageBytes(16 * 1024));
	m_vSamples[1]->Reply(200, PackageBytes());
	m_vSamples[2]->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 9));
	ASSERT_EQ(m_Survey.Candidates().size(), 3U);
	EXPECT_TRUE(m_Survey.Candidates().back().m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, RateLimitedSampleDoesNotBecomeADownloadCandidate)
{
	Begin();
	m_vSamples[0]->Reply(429, "");
	m_vSamples[1]->Reply(200, PackageBytes());
	m_vSamples[2]->Reply(200, PackageBytes());
	ASSERT_TRUE(m_Survey.Poll(m_Sources, 2));
	for(const auto &Candidate : m_Survey.Candidates())
		EXPECT_FALSE(Candidate.m_Prefix.empty());
	for(const auto &Candidate : m_Sources.Candidates(m_Package, qm_update::RELEASE, 3))
		EXPECT_FALSE(Candidate.m_Prefix.empty());
}

TEST_F(QmUpdateSurvey, CancelStopsPackageSamplesAndClearsCandidates)
{
	Begin();
	m_Survey.Cancel();
	for(const auto &Request : m_vSamples)
		EXPECT_TRUE(Request->IsAbortRequested());
	EXPECT_TRUE(m_Survey.Candidates().empty());
	EXPECT_TRUE(m_Survey.Poll(m_Sources, 100));
}
