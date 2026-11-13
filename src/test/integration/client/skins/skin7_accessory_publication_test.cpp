#include <game/client/components/qmclient/skin7_info_job.h>
#include <game/client/components/qmclient/skin7_part_publisher.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/skin7_test_graphics.h>
#include <test/test.h>

#include <array>
#include <new>

namespace
{
	class CQmSkin7AccessoryPublicationTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_PoolActive = false;
		std::shared_ptr<CQmSkin7InfoJob> m_pJob;
		CSkin7TestGraphics m_Graphics;
		CGpuUploadLimiter m_Limiter;
		CQmSkin7PartPublisher m_Publisher;
		IGraphics::CTextureHandle m_aTargets[2];
		int m_Next = 0;
		const uint8_t *m_pLastPixels = nullptr;

		void WriteImage(const char *pPath, size_t Width, size_t Height)
		{
			std::array<uint8_t, 12 * 5 * 4> aPixels;
			aPixels.fill(123);
			CImageInfo Image;
			Image.m_Width = Width;
			Image.m_Height = Height;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixels.data();
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, pPath, Image));
		}

		bool Reload()
		{
			m_pJob = std::make_shared<CQmSkin7InfoJob>(m_pStorage.get());
			return m_Pool.TryAdd(m_pJob) && static_cast<bool>(WaitForJobDone(*m_pJob));
		}

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			WriteImage("skins7/xmas_hat.png", 2, 4);
			WriteImage("skins7/bot.png", 12, 5);
			ASSERT_FALSE(HasFatalFailure());
			m_PoolActive = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolActive);
			ASSERT_TRUE(Reload());
			m_Limiter.OnFrameStart();
		}

		bool PublishNext()
		{
			return m_pJob->PublishNextAccessory(m_Next, [&](const CImageInfo &Image, bool Hat) {
				m_pLastPixels = Image.m_pData;
				return m_Publisher.TryPublishAccessory(Image, m_aTargets[Hat ? 0 : 1], m_Graphics, m_Limiter,
					Hat ? "skins7/xmas_hat.png" : "skins7/bot.png");
			});
		}

		void SeedOldHat()
		{
			std::array<uint8_t, 4> aPixels{1, 2, 3, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixels.data();
			m_aTargets[0] = m_Graphics.LoadTextureRaw(Image, 0, "old hat");
		}

		void TearDown() override
		{
			if(m_PoolActive)
				m_Pool.Shutdown();
			m_Graphics.m_FailUnload = false;
			EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
			for(auto &Target : m_aTargets)
				if(Target.IsValid() && !Target.IsNullTexture())
					m_Graphics.UnloadTexture(&Target);
			EXPECT_EQ(m_Graphics.LiveCount(), 0);
		}
	};
}

TEST_F(CQmSkin7AccessoryPublicationTest, NativeJobPublishesHatThenBotAndStopsAtEnd)
{
	ASSERT_TRUE(PublishNext());
	EXPECT_EQ(m_Next, 1);
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(m_aTargets[0]));
	EXPECT_FALSE(m_aTargets[1].IsValid());
	ASSERT_TRUE(PublishNext());
	EXPECT_EQ(m_Next, 2);
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(m_aTargets[1]));
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Graphics.m_Attempts, 2);
	auto Hat = m_pJob->TakeXmasHat();
	auto Bot = m_pJob->TakeBot();
	EXPECT_EQ(Hat.m_pData, nullptr);
	EXPECT_EQ(Bot.m_pData, nullptr);
	Hat.Free();
	Bot.Free();
}

TEST_F(CQmSkin7AccessoryPublicationTest, InvalidUploadKeepsOldTexturePixelsAndCursorUntilRetry)
{
	SeedOldHat();
	const auto Old = m_aTargets[0];
	m_Graphics.m_FailAt = 2;
	EXPECT_FALSE(PublishNext());
	const auto *pPixels = m_pLastPixels;
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(pPixels[0], 123);
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_aTargets[0].Id(), Old.Id());
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(Old));
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pPixels);
	EXPECT_EQ(m_Next, 1);
	EXPECT_FALSE(m_Graphics.IsTextureHandleAllocated(Old));
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
}

TEST_F(CQmSkin7AccessoryPublicationTest, UploadExceptionRetainsNativeImageForLaterFrame)
{
	m_Graphics.m_ThrowAt = 1;
	EXPECT_FALSE(PublishNext());
	const auto *pPixels = m_pLastPixels;
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 1);
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pPixels);
}

TEST_F(CQmSkin7AccessoryPublicationTest, BotFailureRetriesBotWithoutReplayingCommittedHat)
{
	ASSERT_TRUE(PublishNext());
	const auto Hat = m_aTargets[0];
	m_Graphics.m_FailAt = 2;
	EXPECT_FALSE(PublishNext());
	const auto *pBotPixels = m_pLastPixels;
	ASSERT_NE(pBotPixels, nullptr);
	EXPECT_EQ(m_Next, 1);
	EXPECT_EQ(m_aTargets[0].Id(), Hat.Id());
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(Hat));
	EXPECT_FALSE(m_aTargets[1].IsValid());
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pBotPixels);
	EXPECT_EQ(m_Next, 2);
	EXPECT_EQ(m_Graphics.m_Attempts, 3);
	EXPECT_EQ(m_aTargets[0].Id(), Hat.Id());
}

TEST_F(CQmSkin7AccessoryPublicationTest, ZeroBudgetKeepsNativeImageAndCursorWithoutUpload)
{
	m_Limiter.OnFrameStart(0);
	EXPECT_FALSE(PublishNext());
	const auto *pPixels = m_pLastPixels;
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	m_Limiter.OnFrameStart(1);
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pPixels);
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 1);
}

TEST_F(CQmSkin7AccessoryPublicationTest, FailedOldTextureUnloadRetainsCandidateAndBlocksFurtherUploads)
{
	SeedOldHat();
	const auto Old = m_aTargets[0];
	m_Graphics.m_FailUnload = true;
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(Old));
	m_Limiter.OnFrameStart();
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Graphics.m_Attempts, 2);
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_Next, 1);
	EXPECT_FALSE(m_Graphics.IsTextureHandleAllocated(Old));
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
}

TEST_F(CQmSkin7AccessoryPublicationTest, MissingHatClearsOldTextureWithoutUsingUploadBudget)
{
	SeedOldHat();
	const auto Old = m_aTargets[0];
	ASSERT_TRUE(m_pStorage->RemoveFile("skins7/xmas_hat.png", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Reload());
	m_Limiter.OnFrameStart(0);
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_Next, 1);
	EXPECT_EQ(m_pLastPixels, nullptr);
	EXPECT_FALSE(m_aTargets[0].IsValid());
	EXPECT_FALSE(m_Graphics.IsTextureHandleAllocated(Old));
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 0);
	EXPECT_EQ(m_Graphics.m_Attempts, 1);
	EXPECT_FALSE(PublishNext());
	m_Limiter.OnFrameStart(1);
	EXPECT_TRUE(PublishNext());
}

TEST_F(CQmSkin7AccessoryPublicationTest, MissingImageUnloadFailureKeepsCursorUntilOldTextureReleased)
{
	SeedOldHat();
	ASSERT_TRUE(m_pStorage->RemoveFile("skins7/xmas_hat.png", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Reload());
	m_Graphics.m_FailUnload = true;
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_Next, 1);
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
}

TEST_F(CQmSkin7AccessoryPublicationTest, ThrowingConsumerDoesNotConsumeImageOrAdvanceCursor)
{
	const uint8_t *pPixels = nullptr;
	EXPECT_FALSE(m_pJob->PublishNextAccessory(m_Next, [&](const CImageInfo &Image, bool Hat) -> bool {
		EXPECT_TRUE(Hat);
		pPixels = Image.m_pData;
		throw std::bad_alloc();
	}));
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(pPixels[0], 123);
	EXPECT_EQ(m_Next, 0);
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pPixels);
}

TEST_F(CQmSkin7AccessoryPublicationTest, RejectedConsumerKeepsSameHatUntilAccepted)
{
	const uint8_t *pPixels = nullptr;
	EXPECT_FALSE(m_pJob->PublishNextAccessory(m_Next, [&](const CImageInfo &Image, bool Hat) {
		EXPECT_TRUE(Hat);
		pPixels = Image.m_pData;
		return false;
	}));
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(m_Next, 0);
	EXPECT_TRUE(PublishNext());
	EXPECT_EQ(m_pLastPixels, pPixels);
}

TEST_F(CQmSkin7AccessoryPublicationTest, CancelledCompletedJobDoesNotPublishOrAdvanceCursor)
{
	ASSERT_TRUE(m_pJob->Abort());
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
}

TEST_F(CQmSkin7AccessoryPublicationTest, UnqueuedJobDoesNotPublishAndCanLaterCompleteNormally)
{
	m_pJob = std::make_shared<CQmSkin7InfoJob>(m_pStorage.get());
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	ASSERT_TRUE(m_Pool.TryAdd(m_pJob));
	ASSERT_TRUE(WaitForJobDone(*m_pJob));
	EXPECT_TRUE(PublishNext());
}

TEST_F(CQmSkin7AccessoryPublicationTest, CandidateCanBeReleasedAfterCpuJobIsDiscarded)
{
	SeedOldHat();
	m_Graphics.m_FailUnload = true;
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	m_pJob.reset();
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
	EXPECT_EQ(m_Next, 0);
}

TEST_F(CQmSkin7AccessoryPublicationTest, NullTextureResultDoesNotReplaceOldImage)
{
	SeedOldHat();
	const auto Old = m_aTargets[0];
	m_Graphics.m_NullAt = 2;
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_aTargets[0].Id(), Old.Id());
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
}

TEST_F(CQmSkin7AccessoryPublicationTest, StaleTextureResultIsDiscardedBeforeReplacingOldImage)
{
	SeedOldHat();
	const auto Old = m_aTargets[0];
	m_Graphics.m_StaleAt = 2;
	EXPECT_FALSE(PublishNext());
	EXPECT_EQ(m_Next, 0);
	EXPECT_EQ(m_aTargets[0].Id(), Old.Id());
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
}
