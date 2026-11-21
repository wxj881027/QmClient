#include <base/str.h>
#include <base/system.h>

#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/public_word_filter.h>

#include <gtest/gtest.h>
#include <test/support/map_http_recorder.h>
#include <test/test.h>

#include <string>

TEST(QmPublicWordFilterCacheWrite, FailedReplacementRemovesItsCandidateAndKeepsPublishedWords)
{
	constexpr const char *CACHE_PATH = qm_storage::WORD_FILTER_CACHE_FILE;
	CTestInfo TestInfo;
	auto pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(qm_storage::EnsureFolder(pStorage.get(), qm_storage::CACHE_REMOTE_DIR));
	ASSERT_TRUE(pStorage->CreateFolder(CACHE_PATH, IStorage::TYPE_SAVE));
	char aCandidate[IO_MAX_PATH_LENGTH];
	str_format(aCandidate, sizeof(aCandidate), "%s.%d.tmp", CACHE_PATH, pid());
	IOHANDLE Candidate = pStorage->OpenFile(aCandidate, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(Candidate, nullptr);
	ASSERT_EQ(io_close(Candidate), 0);

	MapHttpTest::CHttpRecorder Http;
	QmPublicWordFilter::CClient Client(MapHttpTest::CreateResponse);
	Client.Update(&Http, pStorage.get(), true, 100);
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	const std::string Response = std::string(QmPublicWordFilter::RESPONSE_HEADER) + "example";
	Http.m_vRequests.back()->Finish(Response.c_str());
	Client.Update(&Http, pStorage.get(), true, 101);

	EXPECT_FALSE(pStorage->FileExists(aCandidate, IStorage::TYPE_SAVE));
	EXPECT_TRUE(pStorage->FolderExists(CACHE_PATH, IStorage::TYPE_SAVE));
	EXPECT_TRUE(Client.State().HasSnapshot());
	std::string Text = "example";
	EXPECT_TRUE(Client.State().Filter(Text, "*", false));
	EXPECT_EQ(Text, "*");
}
