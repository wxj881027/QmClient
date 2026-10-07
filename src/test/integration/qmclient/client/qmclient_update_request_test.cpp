#include <base/hash.h>
#include <base/str.h>

#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/qmclient/update_request.h>
#include <game/client/components/qmclient/update_survey.h>

#include <gtest/gtest.h>

namespace
{
	// 仅替换 HTTP 边界，正文、哈希和完成状态使用生产 IHttpRequest 实现。
	class CResponse : public IHttpRequest
	{
	public:
		explicit CResponse(const std::string &Url) : IHttpRequest(Url.c_str()) {}
		void Header(const char *) override {}
		void Abort() override
		{
			IHttpRequest::Abort();
			OnCompletionInternal(EHttpState::ABORTED);
		}
		void Progress(double Bytes) { m_Current = Bytes; }
		void Reply(int Status, const std::string &Body, std::optional<int64_t> RetryAfter = {})
		{
			m_StatusCode = Status;
			m_ResultRetryAfterSeconds = RetryAfter;
			OnData(Body.data(), Body.size());
			OnCompletionInternal(Status == 200 ? EHttpState::DONE : EHttpState::ERROR);
		}
		void Interrupted() { OnCompletionInternal(EHttpState::ERROR); }
	};

	using EState = qm_update::CUpdateRequest::EState;
	const std::string s_Api = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
	const std::string s_Asset = "https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.zip";

	std::string ReleaseJson()
	{
		std::string Result = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[)";
		bool First = true;
		for(const char *pName : {"QmClient-windows.zip", "QmClient-windows.zip.sig", "QmClient-windows-update.json", "QmClient-windows-update.json.sig"})
		{
			if(!First)
				Result += ",";
			First = false;
			Result += std::string(R"({"name":")") + pName + R"(","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/)" + pName + R"("})";
		}
		return Result + "]}";
	}

	bool ValidRelease(const IHttpRequest &Request)
	{
		unsigned char *pBody = nullptr;
		size_t Size = 0;
		Request.Result(&pBody, &Size);
		SQmClientUpdateRelease Release;
		char aError[256];
		return ParseQmClientUpdateRelease(reinterpret_cast<char *>(pBody), Size, "3.3", Release, aError, sizeof(aError));
	}

	class QmUpdateRequest : public ::testing::Test
	{
	protected:
		qm_update::CSourceRegistry m_Sources;
		qm_update::CUpdateRequest m_Request;
		std::vector<std::shared_ptr<CResponse>> m_vResponses;
		void Begin(qm_update::EResource Resource = qm_update::API, qm_update::CUpdateRequest::TValidator Validator = ValidRelease)
		{
			m_Request.Begin(m_Sources, Resource == qm_update::API ? s_Api : s_Asset, Resource, 0, [this](const std::string &Url) {
				auto pResponse = std::make_shared<CResponse>(Url);
				m_vResponses.push_back(pResponse);
				return pResponse; }, std::move(Validator));
		}
	};
}

TEST_F(QmUpdateRequest, OfficialMetadataTimeoutThenMirrorSucceeds)
{
	Begin();
	ASSERT_EQ(m_vResponses[0]->Url(), s_Api);
	EXPECT_EQ(m_Request.Poll(10), EState::RUNNING);
	ASSERT_TRUE(m_vResponses[0]->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(10.1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	EXPECT_EQ(m_vResponses[1]->Url(), "https://gh-proxy.com/" + s_Api);
	m_vResponses[1]->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(11), EState::SUCCEEDED);
	EXPECT_EQ(m_Request.Poll(12), EState::SUCCEEDED);
	EXPECT_EQ(m_vResponses.size(), 2U);
}

TEST_F(QmUpdateRequest, HtmlMetadataIsRejectedAndNextSourceSucceeds)
{
	Begin();
	m_vResponses[0]->Reply(200, "<!DOCTYPE html><title>blocked</title>");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses[1]->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, MissingFieldsInAllResponsesEndsAsFailure)
{
	Begin();
	m_vResponses[0]->Reply(200, R"({"tag_name":"v3.4"})");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Reply(200, R"({"tag_name":"v3.4","assets":[]})");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_EQ(m_Request.Poll(100), EState::FAILED);
	EXPECT_EQ(m_vResponses.size(), 2U);
}

TEST_F(QmUpdateRequest, HttpErrorsExhaustCandidatesExactlyOnce)
{
	Begin();
	m_vResponses[0]->Reply(503, "unavailable");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Reply(404, "missing");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_EQ(m_Request.Poll(3), EState::FAILED);
	EXPECT_EQ(m_vResponses.size(), 2U);
}

TEST_F(QmUpdateRequest, CancelStopsRequestsAndAllFutureFallback)
{
	Begin();
	m_Request.Cancel();
	EXPECT_TRUE(m_vResponses[0]->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(100), EState::CANCELLED);
	EXPECT_EQ(m_vResponses.size(), 1U);
}

TEST_F(QmUpdateRequest, InterruptedDownloadSwitchesSourceAndVerifiesWholeFile)
{
	const std::string Good = "verified full package";
	const auto Expected = sha256(Good.data(), Good.size());
	Begin(qm_update::RELEASE, [Expected](const IHttpRequest &Request) { return Request.ResultSha256() == Expected; });
	m_vResponses[0]->Progress(5);
	m_vResponses[0]->Interrupted();
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses[1]->Reply(200, Good);
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, WrongHashAndHtmlPackagesNeverBecomeSuccessful)
{
	const std::string Good = "verified full package";
	const auto Expected = sha256(Good.data(), Good.size());
	Begin(qm_update::RELEASE, [Expected](const IHttpRequest &Request) { return Request.ResultSha256() == Expected; });
	m_vResponses[0]->Reply(200, "<!DOCTYPE html>");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Reply(200, "corrupt package");
	EXPECT_EQ(m_Request.Poll(2), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 3U);
	EXPECT_EQ(m_vResponses[2]->Url(), "https://ghfast.top/" + s_Asset);
	m_vResponses[2]->Reply(200, Good);
	EXPECT_EQ(m_Request.Poll(3), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, OfficialRateLimitFallsBackToMirrorWithoutImmediateRetry)
{
	Begin();
	m_vResponses[0]->Reply(429, "rate limit", 900);
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	const auto Candidates = m_Sources.Candidates(s_Api, qm_update::API, 600);
	ASSERT_EQ(Candidates.size(), 1U);
	EXPECT_EQ(Candidates[0].m_Group, "gh-proxy");
	EXPECT_EQ(m_vResponses.size(), 2U);
}

TEST_F(QmUpdateRequest, RestartAfterCancelCreatesFreshRequestWithoutOldResultPublication)
{
	Begin();
	auto pOld = m_vResponses.front();
	m_Request.Cancel();
	Begin();
	ASSERT_EQ(m_vResponses.size(), 2U);
	pOld->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses.back()->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, EverySourceReturningHtmlEndsAsFailure)
{
	Begin();
	m_vResponses[0]->Reply(200, "<html>blocked</html>");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Reply(200, "<html>not a release</html>");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
}

TEST_F(QmUpdateRequest, IncompatiblePlatformAssetsAreRejected)
{
	Begin();
	const std::string Linux = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[{"name":"QmClient-ubuntu.tar.xz","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-ubuntu.tar.xz"}]})";
	m_vResponses[0]->Reply(200, Linux);
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Reply(200, Linux);
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
}

TEST_F(QmUpdateRequest, NoDataProgressAfterPartialDownloadFallsBack)
{
	Begin(qm_update::RELEASE, [](const IHttpRequest &) { return true; });
	m_vResponses[0]->Progress(5);
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	EXPECT_EQ(m_Request.Poll(20), EState::RUNNING);
	EXPECT_FALSE(m_vResponses[0]->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(21), EState::RUNNING);
	EXPECT_TRUE(m_vResponses[0]->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(21.1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses[1]->Reply(200, "complete");
	EXPECT_EQ(m_Request.Poll(22), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, OfficialMetadataSuccessDoesNotRequestAMirror)
{
	Begin();
	ASSERT_EQ(m_vResponses.size(), 1U);
	EXPECT_EQ(m_vResponses.front()->Url(), s_Api);
	m_vResponses.front()->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(1), EState::SUCCEEDED);
	EXPECT_EQ(m_vResponses.size(), 1U);
}

TEST_F(QmUpdateRequest, SurveyPrefersReachableOfficialOverFasterMirrors)
{
	qm_update::CSourceSurvey Survey;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0,
		[this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); m_vResponses.push_back(Request); return Request; });
	ASSERT_EQ(m_vResponses.size(), 3U);
	EXPECT_EQ(m_vResponses[0]->Url(), s_Asset + ".sig");
	m_vResponses[2]->Reply(200, std::string(64, 'a'));
	EXPECT_FALSE(Survey.Poll(m_Sources, 1));
	m_vResponses[1]->Reply(200, std::string(64, 'b'));
	EXPECT_FALSE(Survey.Poll(m_Sources, 2));
	m_vResponses[0]->Reply(200, std::string(64, 'c'));
	ASSERT_TRUE(Survey.Poll(m_Sources, 3));
	const auto Candidates = Survey.Candidates();
	ASSERT_EQ(Candidates.size(), 3U);
	EXPECT_EQ(Candidates[0].m_Group, "github");
	EXPECT_EQ(Candidates[1].m_Group, "ghproxy");
	EXPECT_EQ(Candidates[2].m_Group, "gh-proxy");
	EXPECT_TRUE(m_Sources.Recent().empty());
}

TEST_F(QmUpdateRequest, UnreachableOfficialAndInvalidMirrorLeaveTheHealthyMirror)
{
	qm_update::CSourceSurvey Survey;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0,
		[this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); m_vResponses.push_back(Request); return Request; });
	ASSERT_EQ(m_vResponses.size(), 3U);
	m_vResponses[0]->Interrupted();
	m_vResponses[1]->Reply(200, "<html>blocked</html>");
	m_vResponses[2]->Reply(200, std::string(64, 's'));
	ASSERT_TRUE(Survey.Poll(m_Sources, 1));
	const auto Candidates = Survey.Candidates();
	ASSERT_EQ(Candidates.size(), 1U);
	EXPECT_EQ(Candidates.front().m_Group, "ghproxy");
}

TEST_F(QmUpdateRequest, AllSurveyRoutesFailWithoutAnUnprobedOfficialFallback)
{
	qm_update::CSourceSurvey Survey;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0,
		[this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); m_vResponses.push_back(Request); return Request; });
	EXPECT_FALSE(Survey.Poll(m_Sources, 10));
	for(const auto &Response : m_vResponses)
		EXPECT_TRUE(Response->IsAbortRequested());
	EXPECT_TRUE(Survey.Poll(m_Sources, 10.1));
	EXPECT_TRUE(Survey.Candidates().empty());
}

TEST_F(QmUpdateRequest, SurveyRetriesBrokenOfficialSystemProxyThroughDirectOnce)
{
	qm_update::CSourceSurvey Survey;
	int DirectCount = 0;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0, [this](const std::string &Url) { auto Response = std::make_shared<CResponse>(Url); if(Url == s_Asset + ".sig") Response->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Response); return Response; }, [this, &DirectCount](const std::string &Url) { ++DirectCount; auto Response = std::make_shared<CResponse>(Url); Response->Proxy(""); m_vResponses.push_back(Response); return Response; });
	ASSERT_EQ(m_vResponses.size(), 3U);
	m_vResponses[0]->Interrupted();
	m_vResponses[1]->Reply(200, std::string(64, 'm'));
	m_vResponses[2]->Reply(200, std::string(64, 'm'));
	EXPECT_FALSE(Survey.Poll(m_Sources, 1));
	ASSERT_EQ(DirectCount, 1);
	ASSERT_EQ(m_vResponses.size(), 4U);
	EXPECT_EQ(m_vResponses.back()->Url(), s_Asset + ".sig");
	EXPECT_STREQ(m_vResponses.back()->ProxyUrl(), "");
	m_vResponses.back()->Reply(200, std::string(64, 'o'));
	ASSERT_TRUE(Survey.Poll(m_Sources, 2));
	EXPECT_EQ(Survey.Candidates().front().m_Group, "github");
	EXPECT_TRUE(Survey.Poll(m_Sources, 100));
	EXPECT_EQ(DirectCount, 1);
}

TEST_F(QmUpdateRequest, SurveyOfficialRateLimitDoesNotRetryAnotherRoute)
{
	qm_update::CSourceSurvey Survey;
	int DirectCount = 0;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0, [this](const std::string &Url) { auto Response = std::make_shared<CResponse>(Url); Response->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Response); return Response; }, [&DirectCount](const std::string &Url) { ++DirectCount; return std::make_shared<CResponse>(Url); });
	m_vResponses[0]->Reply(429, "wait", 900);
	m_vResponses[1]->Reply(200, std::string(64, 'm'));
	m_vResponses[2]->Reply(200, std::string(64, 'm'));
	ASSERT_TRUE(Survey.Poll(m_Sources, 1));
	EXPECT_EQ(DirectCount, 0);
	for(const auto &Candidate : Survey.Candidates())
		EXPECT_NE(Candidate.m_Group, "github");
	for(const auto &Candidate : m_Sources.Candidates(s_Asset, qm_update::RELEASE, 899))
		EXPECT_NE(Candidate.m_Group, "github");
	EXPECT_EQ(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 901).front().m_Group, "github");
}

TEST_F(QmUpdateRequest, SurveySystemProxyResolutionDoesNotConsumeTransferBudget)
{
	qm_update::CSourceSurvey Survey;
	qm_update::CSourceRegistry Sources({});
	Survey.Begin(Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0,
		[this](const std::string &Url) { auto Response = std::make_shared<CResponse>(Url); m_vResponses.push_back(Response); return Response; });
	Survey.BeginTransfer(m_vResponses.front(), 8);
	EXPECT_FALSE(Survey.Poll(Sources, 10));
	EXPECT_FALSE(m_vResponses.front()->IsAbortRequested());
	EXPECT_FALSE(Survey.Poll(Sources, 17.9));
	m_vResponses.front()->Reply(200, std::string(64, 'o'));
	EXPECT_TRUE(Survey.Poll(Sources, 18));
	ASSERT_EQ(Survey.Candidates().size(), 1U);
	EXPECT_EQ(Survey.Candidates().front().m_Group, "github");
}

TEST_F(QmUpdateRequest, CancelSurveyAbortsAllRequestsAndReturnsNoCandidate)
{
	qm_update::CSourceSurvey Survey;
	Survey.Begin(m_Sources.Candidates(s_Asset, qm_update::RELEASE, 0), s_Asset + ".sig", 0,
		[this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); m_vResponses.push_back(Request); return Request; });
	Survey.Cancel();
	for(const auto &Request : m_vResponses)
		EXPECT_TRUE(Request->IsAbortRequested());
	EXPECT_TRUE(Survey.Poll(m_Sources, 100));
	EXPECT_TRUE(Survey.Candidates().empty());
}

TEST_F(QmUpdateRequest, RejectedBodyIsDistinguishedFromTransportFailure)
{
	Begin();
	m_vResponses[0]->Reply(503, "unavailable");
	m_Request.Poll(1);
	EXPECT_FALSE(m_Request.RejectedContent());
	m_vResponses[1]->Reply(200, "<html>wrong content</html>");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_TRUE(m_Request.RejectedContent());
}

TEST_F(QmUpdateRequest, FullPackageAlsoTimesOutBeforeFirstByte)
{
	const std::shared_ptr<IHttpRequest> Requests[] = {
		std::make_shared<CResponse>(s_Asset), std::make_shared<CResponse>(s_Asset + ".sig")};
	qm_update::CProgressDeadline Deadlines[2];
	for(auto &Deadline : Deadlines)
		Deadline.Begin(0);
	std::static_pointer_cast<CResponse>(Requests[1])->Reply(200, std::string(64, 's'));
	qm_update::PollDownloadBatch(Requests, Deadlines, 9);
	EXPECT_FALSE(Requests[0]->IsAbortRequested());
	qm_update::PollDownloadBatch(Requests, Deadlines, 10);
	EXPECT_TRUE(Requests[0]->IsAbortRequested());
	EXPECT_FALSE(Requests[1]->IsAbortRequested());
}

TEST_F(QmUpdateRequest, FailedSignatureAbortsStillDownloadingPackage)
{
	const std::shared_ptr<IHttpRequest> Requests[] = {
		std::make_shared<CResponse>(s_Asset), std::make_shared<CResponse>(s_Asset + ".sig")};
	qm_update::CProgressDeadline Deadlines[2];
	for(auto &Deadline : Deadlines)
		Deadline.Begin(0);
	std::static_pointer_cast<CResponse>(Requests[0])->Progress(100);
	std::static_pointer_cast<CResponse>(Requests[1])->Interrupted();
	qm_update::PollDownloadBatch(Requests, Deadlines, 1);
	EXPECT_TRUE(Requests[0]->IsAbortRequested());
}

TEST_F(QmUpdateRequest, LongPackageWithContinuousProgressHasNoTotalTimeout)
{
	const std::shared_ptr<IHttpRequest> Requests[] = {std::make_shared<CResponse>(s_Asset)};
	qm_update::CProgressDeadline Deadlines[1];
	Deadlines[0].Begin(0);
	for(int Second = 1; Second <= 1000; ++Second)
	{
		std::static_pointer_cast<CResponse>(Requests[0])->Progress(Second * 1024);
		qm_update::PollDownloadBatch(Requests, Deadlines, Second);
		ASSERT_FALSE(Requests[0]->IsAbortRequested()) << Second;
	}
	qm_update::PollDownloadBatch(Requests, Deadlines, 1020);
	EXPECT_TRUE(Requests[0]->IsAbortRequested());
}

TEST_F(QmUpdateRequest, ProxyResolutionBudgetDoesNotConsumeFirstByteTransferBudget)
{
	Begin();
	m_Request.BeginTransfer(8);
	EXPECT_EQ(m_Request.Poll(10), EState::RUNNING);
	EXPECT_FALSE(m_vResponses.front()->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(17.9), EState::RUNNING);
	EXPECT_FALSE(m_vResponses.front()->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(18), EState::RUNNING);
	EXPECT_TRUE(m_vResponses.front()->IsAbortRequested());
	EXPECT_EQ(m_Request.Poll(18.1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses.back()->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(19), EState::SUCCEEDED);
}

TEST_F(QmUpdateRequest, AllServicesRateLimitedFailWithoutStartingAnotherRequest)
{
	const auto Candidates = m_Sources.Candidates(s_Api, qm_update::API, 0);
	for(const auto &Source : Candidates)
		m_Sources.Failed(Source, 0, 900);
	Begin();
	EXPECT_EQ(m_Request.Poll(0), EState::FAILED);
	EXPECT_EQ(m_Request.Poll(1), EState::FAILED);
	EXPECT_TRUE(m_vResponses.empty());
}

TEST_F(QmUpdateRequest, FailedResponseKeepsItsCompletedHttpStatusForDiagnostics)
{
	CResponse Response(s_Api);
	Response.Reply(429, "rate limited", 120);
	EXPECT_EQ(Response.State(), EHttpState::ERROR);
	EXPECT_EQ(Response.CompletedStatusCode(), 429);
	EXPECT_EQ(Response.ResultRetryAfterSeconds(), 120);
}

TEST_F(QmUpdateRequest, CompletedFailedBatchCanStartNextSourceWithoutRunningReentry)
{
	const std::shared_ptr<IHttpRequest> Requests[] = {std::make_shared<CResponse>(s_Asset), std::make_shared<CResponse>(s_Asset + ".sig")};
	EXPECT_FALSE(qm_update::CanStartDownloadBatch(false, true, Requests));
	std::static_pointer_cast<CResponse>(Requests[0])->Interrupted();
	EXPECT_FALSE(qm_update::CanStartDownloadBatch(false, true, Requests));
	std::static_pointer_cast<CResponse>(Requests[1])->Interrupted();
	EXPECT_TRUE(qm_update::CanStartDownloadBatch(false, true, Requests));
	EXPECT_FALSE(qm_update::CanStartDownloadBatch(false, false, Requests));
	EXPECT_FALSE(qm_update::CanStartDownloadBatch(true, true, Requests));
}

TEST_F(QmUpdateRequest, OfficialExplicitProxyFailureRetriesDirectOnce)
{
	qm_update::CSourceRegistry Sources({});
	int DirectCount = 0;
	m_Request.Begin(Sources, s_Api, qm_update::API, 0, [this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); Request->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Request); return Request; }, ValidRelease, [this, &DirectCount](const std::string &Url) { ++DirectCount; auto Request = std::make_shared<CResponse>(Url); Request->Proxy(""); m_vResponses.push_back(Request); return Request; });
	m_vResponses[0]->Reply(403, "proxy route refused");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(DirectCount, 1);
	ASSERT_EQ(m_vResponses.size(), 2U);
	EXPECT_TRUE(m_vResponses[1]->ProxyUrl()[0] == '\0');
	m_vResponses[1]->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
	EXPECT_EQ(m_Request.Poll(3), EState::SUCCEEDED);
	EXPECT_EQ(DirectCount, 1);
}

TEST_F(QmUpdateRequest, OfficialDirectFailureEndsWithoutAThirdRoute)
{
	qm_update::CSourceRegistry Sources({});
	int Attempts = 0;
	const auto Factory = [this, &Attempts](const std::string &Url) { ++Attempts; auto Request = std::make_shared<CResponse>(Url); Request->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Request); return Request; };
	m_Request.Begin(Sources, s_Api, qm_update::API, 0, Factory, ValidRelease, Factory);
	m_vResponses[0]->Interrupted();
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	m_vResponses[1]->Interrupted();
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_EQ(m_Request.Poll(100), EState::FAILED);
	EXPECT_EQ(Attempts, 2);
}

TEST_F(QmUpdateRequest, RateLimitOnOfficialProxyDoesNotRetryDirect)
{
	for(const int Status : {429, 503})
	{
		qm_update::CSourceRegistry Sources({});
		int DirectCount = 0;
		m_vResponses.clear();
		m_Request.Begin(Sources, s_Api, qm_update::API, 0, [this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); Request->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Request); return Request; }, ValidRelease, [&DirectCount](const std::string &Url) { ++DirectCount; return std::make_shared<CResponse>(Url); });
		m_vResponses[0]->Reply(Status, "wait", Status == 503 ? std::optional<int64_t>(120) : std::nullopt);
		EXPECT_EQ(m_Request.Poll(1), EState::FAILED) << Status;
		EXPECT_EQ(DirectCount, 0) << Status;
	}
}

TEST_F(QmUpdateRequest, CancelBeforeOfficialDirectFallbackPreventsRouteRetry)
{
	qm_update::CSourceRegistry Sources({});
	int DirectCount = 0;
	m_Request.Begin(Sources, s_Api, qm_update::API, 0, [this](const std::string &Url) { auto Request = std::make_shared<CResponse>(Url); Request->Proxy("http://127.0.0.1:7890"); m_vResponses.push_back(Request); return Request; }, ValidRelease, [&DirectCount](const std::string &Url) { ++DirectCount; return std::make_shared<CResponse>(Url); });
	m_vResponses[0]->Reply(403, "blocked");
	m_Request.Cancel();
	EXPECT_EQ(m_Request.Poll(1), EState::CANCELLED);
	EXPECT_EQ(DirectCount, 0);
}

TEST_F(QmUpdateRequest, PackageRouteRetryRequiresOfficialCompletedProxiedBatch)
{
	const qm_update::CSource Official{"", "github", 1000, qm_update::RELEASE};
	const qm_update::CSource Mirror{"https://ghfast.top/", "ghproxy", 40, qm_update::RELEASE};
	const std::shared_ptr<IHttpRequest> Requests[] = {std::make_shared<CResponse>(s_Asset), std::make_shared<CResponse>(s_Asset + ".sig")};
	Requests[0]->Proxy("http://127.0.0.1:7890");
	EXPECT_FALSE(qm_update::CanRetryOfficialDirect(&Official, false, Requests));
	std::static_pointer_cast<CResponse>(Requests[0])->Interrupted();
	std::static_pointer_cast<CResponse>(Requests[1])->Reply(403, "blocked");
	EXPECT_TRUE(qm_update::CanRetryOfficialDirect(&Official, false, Requests));
	EXPECT_FALSE(qm_update::CanRetryOfficialDirect(&Official, true, Requests));
	EXPECT_FALSE(qm_update::CanRetryOfficialDirect(&Mirror, false, Requests));
	EXPECT_FALSE(qm_update::CanRetryOfficialDirect(nullptr, false, Requests));
	Requests[0]->Proxy("");
	EXPECT_FALSE(qm_update::CanRetryOfficialDirect(&Official, false, Requests));
}

TEST_F(QmUpdateRequest, EqualReleaseWithoutPortablePackageCompletesInformationCheckWithoutFallback)
{
	SQmClientUpdateRelease Info;
	Begin(qm_update::API, [&](const IHttpRequest &Request) {
		unsigned char *pBody = nullptr;
		size_t Size = 0;
		Request.Result(&pBody, &Size);
		char aError[256];
		return ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pBody), Size, "3.4", Info, aError, sizeof(aError), false, true);
	});
	m_vResponses[0]->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(1), EState::SUCCEEDED);
	EXPECT_FALSE(Info.m_NewVersion);
	EXPECT_FALSE(Info.m_PackageAvailable);
	EXPECT_FALSE(m_Request.RejectedContent());
	EXPECT_EQ(m_Request.Poll(100), EState::SUCCEEDED);
	EXPECT_EQ(m_vResponses.size(), 1U);
}

TEST_F(QmUpdateRequest, HtmlFallsBackToNewerReleaseInformationEvenWithoutPortablePackage)
{
	SQmClientUpdateRelease Info;
	Begin(qm_update::API, [&](const IHttpRequest &Request) {
		unsigned char *pBody = nullptr;
		size_t Size = 0;
		Request.Result(&pBody, &Size);
		char aError[256];
		return ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pBody), Size, "3.3", Info, aError, sizeof(aError), false, true);
	});
	m_vResponses[0]->Reply(200, "<html>blocked</html>");
	EXPECT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses[1]->Reply(200, ReleaseJson());
	EXPECT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
	EXPECT_TRUE(Info.m_NewVersion);
	EXPECT_FALSE(CanDownloadQmClientRelease(Info, false, true));
	EXPECT_EQ(m_Request.Poll(100), EState::SUCCEEDED);
	EXPECT_EQ(m_vResponses.size(), 2U);
}

TEST_F(QmUpdateRequest, RecheckAfterPortableAssetsArePublishedEnablesDownloadWithoutChangingReleaseVersion)
{
	SQmClientUpdateRelease Info;
	const auto Validate = [&](const IHttpRequest &Request) {
		unsigned char *pBody = nullptr;
		size_t Size = 0;
		Request.Result(&pBody, &Size);
		char aError[256];
		return ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pBody), Size, "3.3", Info, aError, sizeof(aError), false, true);
	};
	Begin(qm_update::API, Validate);
	m_vResponses.back()->Reply(200, ReleaseJson());
	ASSERT_EQ(m_Request.Poll(1), EState::SUCCEEDED);
	ASSERT_TRUE(Info.m_NewVersion);
	ASSERT_FALSE(CanDownloadQmClientRelease(Info, false, true));
	std::string PortableJson = ReleaseJson();
	const std::string NormalStem = "QmClient-windows";
	for(size_t Offset = 0; (Offset = PortableJson.find(NormalStem, Offset)) != std::string::npos;)
	{
		PortableJson.insert(Offset + NormalStem.size(), "-portable");
		Offset += NormalStem.size() + std::string("-portable").size();
	}
	Begin(qm_update::API, Validate);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses.back()->Reply(200, PortableJson);
	ASSERT_EQ(m_Request.Poll(2), EState::SUCCEEDED);
	EXPECT_STREQ(Info.m_aVersion, "3.4");
	EXPECT_TRUE(Info.m_NewVersion);
	EXPECT_TRUE(Info.m_PackageAvailable);
	EXPECT_TRUE(CanDownloadQmClientRelease(Info, false, true));
	EXPECT_FALSE(m_Request.RejectedContent());
}

TEST_F(QmUpdateRequest, Official429WithoutRetryAfterCoolsDownBeforeUserCanRecheck)
{
	Begin();
	ASSERT_EQ(m_vResponses[0]->Url(), s_Api);
	m_vResponses[0]->Reply(429, "rate limited");
	ASSERT_EQ(m_Request.Poll(1), EState::RUNNING);
	ASSERT_EQ(m_vResponses.size(), 2U);
	m_vResponses.back()->Reply(503, "mirror unavailable");
	EXPECT_EQ(m_Request.Poll(2), EState::FAILED);
	EXPECT_TRUE(m_Sources.Candidates(s_Api, qm_update::API, 3).empty());
	const auto Recovered = m_Sources.Candidates(s_Api, qm_update::API, 303);
	ASSERT_FALSE(Recovered.empty());
	EXPECT_EQ(Recovered.front().m_Group, "github");
}

TEST(QmUpdateRetryDelay, MissingHeaderUsesBoundedCooldownAndLongerServerDelayIsPreserved)
{
	CResponse Missing("https://api.github.com/repos/wxj881027/QmClient/releases/latest");
	EXPECT_EQ(qm_update::SourceRetryDelay(&Missing), 0);
	Missing.Reply(429, "rate limit");
	EXPECT_EQ(qm_update::SourceRetryDelay(&Missing), 300);
	CResponse Longer("https://api.github.com/repos/wxj881027/QmClient/releases/latest");
	Longer.Reply(429, "rate limit", 900);
	EXPECT_EQ(qm_update::SourceRetryDelay(&Longer), 900);
	CResponse OtherError("https://api.github.com/repos/wxj881027/QmClient/releases/latest");
	OtherError.Reply(503, "temporary failure");
	EXPECT_EQ(qm_update::SourceRetryDelay(&OtherError), 0);
}
