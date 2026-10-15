#include <game/client/components/qmclient/custom_community_info.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace
{
	class CCommunityResponse : public IHttpRequest
	{
	public:
		explicit CCommunityResponse(const std::string &Body, EHttpState State = EHttpState::DONE) : IHttpRequest("https://community.test/info.json")
		{
			WriteToMemory();
			m_pBuffer = static_cast<unsigned char *>(malloc(Body.size() + 1));
			memcpy(m_pBuffer, Body.c_str(), Body.size() + 1);
			m_ResponseLength = Body.size();
			m_State = State;
		}
		void Header(const char *) override {}
	};
}

TEST(QmCustomCommunityInfo, CompletedRefreshUsesMemoryWithoutAStorageDependency)
{
	CCommunityResponse Response(R"({"communities":[{"id":"axiom"}]})");
	auto pInfo = QmLoadCustomCommunityInfo(nullptr, &Response);
	ASSERT_NE(pInfo, nullptr);
	EXPECT_STREQ((*pInfo)["communities"][0]["id"], "axiom");
}

TEST(QmCustomCommunityInfo, StartupCacheLoadsBeforeAnOnlineRefresh)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::string Cache = R"({"communities":[{"id":"cached"}]})";
	IOHANDLE File = pStorage->OpenFile(QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File, Cache.data(), Cache.size()), Cache.size());
	io_close(File);
	auto pCached = QmLoadCustomCommunityInfo(pStorage.get());
	ASSERT_NE(pCached, nullptr);
	EXPECT_STREQ((*pCached)["communities"][0]["id"], "cached");
	CCommunityResponse Response(R"({"communities":[{"id":"live"}]})");
	auto pLive = QmLoadCustomCommunityInfo(pStorage.get(), &Response);
	ASSERT_NE(pLive, nullptr);
	EXPECT_STREQ((*pLive)["communities"][0]["id"], "live");
}

TEST(QmCustomCommunityInfo, InvalidRefreshDoesNotFallBackToSynchronousDiskRead)
{
	for(const std::string &Body : {std::string(""), std::string("{"), std::string("[]"), std::string("null")})
	{
		SCOPED_TRACE(Body);
		CCommunityResponse Response(Body);
		EXPECT_EQ(QmLoadCustomCommunityInfo(nullptr, &Response), nullptr);
	}
	CCommunityResponse Valid(R"({"communities":[]})");
	EXPECT_NE(QmLoadCustomCommunityInfo(nullptr, &Valid), nullptr);
}

TEST(QmCustomCommunityInfo, PendingFailedAndAbortedResponsesDoNotAccessResults)
{
	for(const auto State : {EHttpState::QUEUED, EHttpState::RUNNING, EHttpState::ERROR, EHttpState::ABORTED})
	{
		SCOPED_TRACE(static_cast<int>(State));
		CCommunityResponse Response(R"({"communities":[]})", State);
		EXPECT_EQ(QmLoadCustomCommunityInfo(nullptr, &Response), nullptr);
	}
}

TEST(QmCustomCommunityInfo, StartupCacheIsReadAndParsedByWorkerBeforePublication)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::string Body = R"({"communities":[{"id":"cached"}]})";
	IOHANDLE File = pStorage->OpenFile(QMCLIENT_CUSTOM_COMMUNITIES_INFO_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	io_write(File, Body.data(), Body.size());
	io_close(File);
	// 构造和排队不读取文件；只有真实 worker 完成后主线程才取得独占结果。
	auto pJob = std::make_shared<CQmCustomCommunityCacheJob>(pStorage.get());
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(pJob->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
	auto pResult = pJob->TakeInfo();
	ASSERT_NE(pResult, nullptr);
	EXPECT_STREQ((*pResult)["communities"][0]["id"], "cached");
	EXPECT_EQ(pJob->TakeInfo(), nullptr);
}
