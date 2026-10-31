#include <engine/storage.h>

#include <game/client/components/qmclient/chat_emoji.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace
{
	void Pixel(CImageInfo &Image)
	{
		Image.m_pData = static_cast<uint8_t *>(malloc(4));
		if(!Image.m_pData)
			throw std::bad_alloc();
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
	}
	bool WaitFor(const std::function<bool()> &Condition)
	{
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while(!Condition())
		{
			if(std::chrono::steady_clock::now() >= Deadline)
				return false;
			std::this_thread::yield();
		}
		return true;
	}
	class QmChatEmojiLoadIntegration : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_PoolRunning = false;
		CQmChatEmojiLoadState m_State;
		void SetUp() override
		{
			m_PoolRunning = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolRunning);
		}
		void TearDown() override
		{
			if(m_PoolRunning)
				m_Pool.Shutdown();
		}
		bool Start()
		{
			return m_State.TryStart([](EQmChatEmoji) { return std::make_shared<CQmChatEmojiLoadJob>(Pixel); },
				[&](const auto &pJob) { return m_Pool.TryAdd(pJob); });
		}
		bool WaitDone()
		{
			auto pJob = m_State.Job();
			return pJob && WaitFor([&] { return pJob->State() == IJob::STATE_DONE; });
		}
		void Consume()
		{
			m_State.Poll([] { return true; }, [](CImageInfo &Image, EQmChatEmoji) {
				Image.Free();
				return true; });
		}
	};
}

TEST_F(QmChatEmojiLoadIntegration, RealJobsKeepFifoOrderAndDeduplicateRequests)
{
	m_State.Request(EQmChatEmoji::LOVE);
	m_State.Request(EQmChatEmoji::NO);
	EXPECT_FALSE(m_State.Request(EQmChatEmoji::LOVE));
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_State.Loading(), EQmChatEmoji::LOVE);
	ASSERT_TRUE(WaitDone());
	Consume();
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_State.Loading(), EQmChatEmoji::NO);
	ASSERT_TRUE(WaitDone());
	Consume();
	EXPECT_FALSE(m_State.Job());
	EXPECT_EQ(m_State.PendingCount(), 0u);
}

TEST_F(QmChatEmojiLoadIntegration, ClosedPoolRejectionDoesNotPublishTheStillQueuedJob)
{
	CJobPool Closed;
	m_State.Request(EQmChatEmoji::LOVE);
	auto pJob = std::make_shared<CQmChatEmojiLoadJob>(Pixel);
	EXPECT_FALSE(m_State.TryStart([&](EQmChatEmoji) { return pJob; }, [&](const auto &pCandidate) { return Closed.TryAdd(pCandidate); }));
	EXPECT_EQ(pJob->State(), IJob::STATE_QUEUED);
	EXPECT_FALSE(m_State.Job());
	EXPECT_EQ(m_State.PendingCount(), 1u);
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	Consume();
}

TEST_F(QmChatEmojiLoadIntegration, ResourceFailureDiscardsPartialPixelsAndRetriesBehindOtherRequests)
{
	m_State.Request(EQmChatEmoji::LOVE);
	m_State.Request(EQmChatEmoji::NO);
	ASSERT_TRUE(m_State.TryStart([](EQmChatEmoji) { return std::make_shared<CQmChatEmojiLoadJob>([](CImageInfo &Image) {
								Pixel(Image);
								throw std::bad_alloc();
							}); }, [&](const auto &pJob) { return m_Pool.TryAdd(pJob); }));
	ASSERT_TRUE(WaitDone());
	EXPECT_TRUE(m_State.Job()->RetryableFailure());
	EXPECT_EQ(m_State.Job()->Image()->m_pData, nullptr);
	Consume();
	EXPECT_EQ(m_State.PendingCount(), 2u);
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_State.Loading(), EQmChatEmoji::NO);
	ASSERT_TRUE(WaitDone());
	Consume();
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_State.Loading(), EQmChatEmoji::LOVE);
	ASSERT_TRUE(WaitDone());
	Consume();
}

TEST_F(QmChatEmojiLoadIntegration, UnknownDecodeExceptionFallsBackWithoutPublishingPartialPixels)
{
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(m_State.TryStart([](EQmChatEmoji) { return std::make_shared<CQmChatEmojiLoadJob>([](CImageInfo &Image) {
								Pixel(Image);
								throw 7;
							}); }, [&](const auto &pJob) { return m_Pool.TryAdd(pJob); }));
	ASSERT_TRUE(WaitDone());
	EXPECT_FALSE(m_State.Job()->RetryableFailure());
	EXPECT_EQ(m_State.Job()->Image()->m_pData, nullptr);
	Consume();
	EXPECT_EQ(m_State.PendingCount(), 0u);
	EXPECT_FALSE(m_State.Request(EQmChatEmoji::LOVE));
}

TEST_F(QmChatEmojiLoadIntegration, UploadBudgetRetainsTheCompletedImage)
{
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	auto pJob = m_State.Job();
	const auto *pPixels = pJob->Image()->m_pData;
	bool Uploaded = false;
	m_State.Poll([] { return false; }, [&](CImageInfo &, EQmChatEmoji) { Uploaded = true; return true; });
	EXPECT_FALSE(Uploaded);
	EXPECT_EQ(m_State.Job(), pJob);
	EXPECT_EQ(pJob->Image()->m_pData, pPixels);
	Consume();
	EXPECT_FALSE(m_State.Job());
}

TEST_F(QmChatEmojiLoadIntegration, UploadAllocationFailureBeforeConsumptionRetriesTheSameImage)
{
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	auto pJob = m_State.Job();
	const auto *pPixels = pJob->Image()->m_pData;
	m_State.Poll([] { return true; }, [](CImageInfo &, EQmChatEmoji) -> bool { throw std::bad_alloc(); });
	EXPECT_EQ(m_State.Job(), pJob);
	EXPECT_EQ(pJob->Image()->m_pData, pPixels);
	Consume();
}

TEST_F(QmChatEmojiLoadIntegration, ConsumedUploadFailureSchedulesReloadWithoutDuplicateRequests)
{
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	m_State.Poll([] { return true; }, [](CImageInfo &Image, EQmChatEmoji) -> bool {
		Image.Free();
		throw std::length_error("upload"); });
	EXPECT_FALSE(m_State.Job());
	EXPECT_EQ(m_State.PendingCount(), 1u);
	EXPECT_FALSE(m_State.Request(EQmChatEmoji::LOVE));
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	Consume();
}

TEST_F(QmChatEmojiLoadIntegration, RefusedUploadWithIntactPixelsRetainsTheCompletedImage)
{
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(Start());
	ASSERT_TRUE(WaitDone());
	auto pJob = m_State.Job();
	m_State.Poll([] { return true; }, [](CImageInfo &, EQmChatEmoji) { return false; });
	EXPECT_EQ(m_State.Job(), pJob);
	EXPECT_NE(pJob->Image()->m_pData, nullptr);
	Consume();
}

TEST_F(QmChatEmojiLoadIntegration, ClearDoesNotPublishTheResultOfAnAlreadyRunningJob)
{
	struct SControl
	{
		std::atomic<bool> m_Started{false}, m_Release{false};
	};
	auto pControl = std::make_shared<SControl>();
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(m_State.TryStart([&](EQmChatEmoji) { return std::make_shared<CQmChatEmojiLoadJob>([pControl](CImageInfo &Image) {
								 pControl->m_Started = true;
								 const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
								 while(!pControl->m_Release && std::chrono::steady_clock::now() < Deadline)
									 std::this_thread::yield();
								 Pixel(Image);
							 }); }, [&](const auto &pJob) { return m_Pool.TryAdd(pJob); }));
	auto pJob = m_State.Job();
	const bool WasStarted = WaitFor([&] { return pControl->m_Started.load(); });
	m_State.Clear();
	pControl->m_Release = true;
	EXPECT_TRUE(WasStarted);
	ASSERT_TRUE(WaitFor([&] { return pJob->State() == IJob::STATE_DONE; }));
	bool Uploaded = false;
	m_State.Poll([] { return true; }, [&](CImageInfo &, EQmChatEmoji) { Uploaded = true; return true; });
	EXPECT_FALSE(Uploaded);
	EXPECT_TRUE(m_State.Request(EQmChatEmoji::LOVE));
}

TEST_F(QmChatEmojiLoadIntegration, NativeFactoryLoadsTheBundledImageThroughStorageAndWebP)
{
#if defined(CONF_WEBP)
	m_pStorage = m_Info.CreateTestStorage();
	ASSERT_NE(m_pStorage, nullptr);
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(m_State.TryStart([&](EQmChatEmoji Emoji) { return QmCreateChatEmojiLoadJob(m_pStorage.get(), Emoji); },
		[&](const auto &pJob) { return m_Pool.TryAdd(pJob); }));
	ASSERT_TRUE(WaitDone());
	ASSERT_NE(m_State.Job()->Image()->m_pData, nullptr);
	EXPECT_EQ(m_State.Job()->Image()->m_Width, 1260u);
	EXPECT_EQ(m_State.Job()->Image()->m_Height, 1244u);
	Consume();
#else
	GTEST_SKIP() << "WebP support is not enabled";
#endif
}

TEST_F(QmChatEmojiLoadIntegration, InvalidFileKeepsTheExistingOneAttemptTextFallback)
{
	m_pStorage = m_Info.CreateTestStorage();
	ASSERT_NE(m_pStorage, nullptr);
	const char *pPath = "invalid-emoji.webp";
	IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const auto Written = io_write(File, "invalid", 7);
	const int Closed = io_close(File);
	ASSERT_EQ(Written, 7u);
	ASSERT_EQ(Closed, 0);
	m_State.Request(EQmChatEmoji::LOVE);
	ASSERT_TRUE(m_State.TryStart([&](EQmChatEmoji) { return std::make_shared<CQmChatEmojiLoadJob>([pStorage = m_pStorage.get(), pPath](CImageInfo &Image) { QmLoadChatEmojiImage(pStorage, pPath, Image); }); }, [&](const auto &pJob) { return m_Pool.TryAdd(pJob); }));
	ASSERT_TRUE(WaitDone());
	EXPECT_EQ(m_State.Job()->Image()->m_pData, nullptr);
	Consume();
	EXPECT_EQ(m_State.PendingCount(), 0u);
	EXPECT_FALSE(m_State.Request(EQmChatEmoji::LOVE));
}
