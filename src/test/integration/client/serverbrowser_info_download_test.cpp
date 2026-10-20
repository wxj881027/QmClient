#include <base/fs.h>

#include <engine/http_response_validation.h>
#include <engine/storage.h>

#include <test/support/info_download_response.h>
#include <test/support/serverbrowser_fixture.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
	class ServerBrowserInfoDownload : public ServerBrowserTests::CServerBrowserFixture
	{
	protected:
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			CServerBrowserFixture::SetUp();
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CServerBrowserTestAccess::SetStorage(m_Browser, m_pStorage.get());
		}
		std::string Info(const char *pId)
		{
			return std::string(R"({"communities":[{"id":")") + pId +
			       R"(","name":"Community","icon":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://community.test/icon"},"servers":[{"name":"Country","flagId":1,"servers":{"Race":["127.0.0.1:8301"]}}]}]})";
		}
		void LoadInitial()
		{
			ASSERT_NO_FATAL_FAILURE(WriteInfoDownloadCache(m_pStorage.get(), DDNET_INFO_FILE, Info("old")));
			ASSERT_NE(m_Browser.LoadDDNetInfo(), nullptr);
		}
		std::shared_ptr<CInfoDownloadTestRequest> Request()
		{
			return std::make_shared<CInfoDownloadTestRequest>(m_pStorage.get(), DDNET_INFO_FILE);
		}
		std::string Cached() { return ReadInfoDownloadCache(m_pStorage.get(), DDNET_INFO_FILE); }
	};
}

TEST_F(ServerBrowserInfoDownload, ValidResponseStaysTemporaryUntilMetadataIsAccepted)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pOldInfo = CServerBrowserTestAccess::Info(m_Browser);
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("new")));
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pOldInfo);
	ASSERT_NE(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(Cached(), Info("new"));
	EXPECT_NE(m_Browser.Community("new"), nullptr);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), pRequest->ResultSha256());
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(ServerBrowserInfoDownload, MalformedResponseRetainsDiskAndPublishedMetadata)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pOldInfo = CServerBrowserTestAccess::Info(m_Browser);
	const auto *pOldCommunity = m_Browser.Community("old");
	const auto Digest = m_Browser.DDNetInfoSha256();
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish("{invalid"));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pOldInfo);
	EXPECT_EQ(m_Browser.Community("old"), pOldCommunity);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(ServerBrowserInfoDownload, NonObjectRootsNeverReplaceTheCache)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	for(const char *pBody : {"[]", "null", "7", "\"text\""})
	{
		SCOPED_TRACE(pBody);
		auto pRequest = Request();
		ASSERT_NO_FATAL_FAILURE(pRequest->Finish(pBody));
		EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
		EXPECT_EQ(Cached(), Info("old"));
		EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	}
}

TEST_F(ServerBrowserInfoDownload, EmptyResponseDoesNotReplaceTheCache)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(""));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
}

TEST_F(ServerBrowserInfoDownload, MetadataPreparationFailureDiscardsTheCandidateAndAllowsRetry)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pOldInfo = CServerBrowserTestAccess::Info(m_Browser);
	m_Browser.m_CustomCommunitiesFunction = [](std::vector<json_value *> &) { throw std::bad_alloc(); };
	auto pRejected = Request();
	ASSERT_NO_FATAL_FAILURE(pRejected->Finish(Info("new")));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRejected.get()), nullptr);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pOldInfo);
	EXPECT_EQ(pRejected->State(), EHttpState::ERROR);
	EXPECT_FALSE(fs_is_file(pRejected->TemporaryPath()));
	m_Browser.m_CustomCommunitiesFunction = {};
	auto pRetry = Request();
	ASSERT_NO_FATAL_FAILURE(pRetry->Finish(Info("new")));
	ASSERT_NE(m_Browser.LoadDDNetInfo(pRetry.get()), nullptr);
	EXPECT_EQ(Cached(), Info("new"));
}

TEST_F(ServerBrowserInfoDownload, FailedFilePublicationDoesNotCommitPreparedMetadata)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pOldInfo = CServerBrowserTestAccess::Info(m_Browser);
	const auto Digest = m_Browser.DDNetInfoSha256();
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("new")));
	ASSERT_EQ(fs_remove(pRequest->TemporaryPath()), 0);
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pOldInfo);
	EXPECT_EQ(m_Browser.DDNetInfoSha256(), Digest);
}

TEST_F(ServerBrowserInfoDownload, CancelledCompletedResponseCannotReplaceTheCache)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("new")));
	pRequest->Abort();
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(pRequest->State(), EHttpState::ERROR);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_NE(m_Browser.Community("old"), nullptr);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(ServerBrowserInfoDownload, FailedTransferNeverConsumesTheDownloadedBody)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("new"), EHttpState::ERROR));
	EXPECT_EQ(m_Browser.LoadDDNetInfo(pRequest.get()), nullptr);
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_NE(m_Browser.Community("old"), nullptr);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}

TEST_F(ServerBrowserInfoDownload, UnconsumedResponseDestructionDiscardsItsTemporaryFile)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	std::string TemporaryPath;
	{
		auto pRequest = Request();
		ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("new")));
		TemporaryPath = pRequest->TemporaryPath();
		EXPECT_TRUE(fs_is_file(TemporaryPath.c_str()));
	}
	EXPECT_EQ(Cached(), Info("old"));
	EXPECT_FALSE(fs_is_file(TemporaryPath.c_str()));
}

TEST_F(ServerBrowserInfoDownload, KnownDigestCanPublishWithoutRebuildingMetadata)
{
	ASSERT_NO_FATAL_FAILURE(LoadInitial());
	const auto *pOldInfo = CServerBrowserTestAccess::Info(m_Browser);
	auto pRequest = Request();
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(Info("old")));
	ASSERT_EQ(m_Browser.DDNetInfoSha256(), pRequest->ResultSha256());
	CHttpResponseValidation Validation(*pRequest);
	EXPECT_TRUE(Validation.Commit());
	EXPECT_FALSE(Validation.Commit());
	EXPECT_EQ(pRequest->State(), EHttpState::DONE);
	EXPECT_EQ(CServerBrowserTestAccess::Info(m_Browser), pOldInfo);
	EXPECT_FALSE(fs_is_file(pRequest->TemporaryPath()));
}
