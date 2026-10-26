#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/skin7_part_load_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <new>
#include <stdexcept>

namespace
{
	using TJob = CQmSkin7PartLoadJob;

	// 只隔离依赖生成素材布局的准备边界；读文件、PNG、灰度和发布均走真实实现。
	void PrepareBoundary(TJob::SResult &Result)
	{
		Result.m_BloodColor = ColorRGBA(1, 0, 0);
	}

	void FailAllocation(TJob::SResult &)
	{
		throw std::bad_alloc();
	}

	void FailAfterPartialPreparation(TJob::SResult &Result)
	{
		auto pSource = std::make_shared<QmChatAvatar::SSource>();
		pSource->m_aSprites[QmChatAvatar::BODY].m_vRgba.resize(16, 42);
		Result.m_pChatAvatarOriginal = std::move(pSource);
		throw std::runtime_error("preparation failure");
	}

	class CQmSkin7PartLoadJobTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_PoolActive = false;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			m_PoolActive = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolActive);
		}

		void TearDown() override
		{
			if(m_PoolActive)
				m_Pool.Shutdown();
		}

		void WritePng(CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
		{
			std::array<uint8_t, 4 * 4 * 4> aPixels;
			aPixels.fill(144);
			CImageInfo Image;
			Image.m_Width = 4;
			Image.m_Height = 4;
			Image.m_Format = Format;
			Image.m_pData = aPixels.data();
			if(Format == CImageInfo::FORMAT_RGBA)
			{
				for(size_t i = 0; i < aPixels.size(); i += 4)
				{
					aPixels[i + 1] = 48;
					aPixels[i + 2] = 12;
					aPixels[i + 3] = 200;
				}
			}
			IOHANDLE File = m_pStorage->OpenFile("skins7/part.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, "part", Image));
		}

		std::shared_ptr<TJob> Queue(TJob::TPrepareVisuals pfnPrepare = PrepareBoundary)
		{
			auto pJob = std::make_shared<TJob>("skins7/part.png", "part", m_pStorage.get(), IStorage::TYPE_SAVE, 2, 3, pfnPrepare);
			EXPECT_TRUE(m_Pool.TryAdd(pJob));
			return pJob;
		}

		void ExpectFailedResult(const TJob::SResult &Result)
		{
			EXPECT_FALSE(Result.m_Success);
			EXPECT_EQ(Result.m_OriginalImage.m_pData, nullptr);
			EXPECT_EQ(Result.m_GrayscaleImage.m_pData, nullptr);
			EXPECT_EQ(Result.m_QmSkinOutline, nullptr);
			EXPECT_EQ(Result.m_pChatAvatarOriginal, nullptr);
			EXPECT_EQ(Result.m_pChatAvatarColorable, nullptr);
		}
	};
}

TEST_F(CQmSkin7PartLoadJobTest, NativePngPublishesOwnedPixelsAndMetadata)
{
	WritePng();
	auto pJob = Queue();
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	pJob.reset();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_OriginalImage.m_pData, nullptr);
	ASSERT_NE(Result.m_GrayscaleImage.m_pData, nullptr);
	EXPECT_NE(Result.m_OriginalImage.m_pData, Result.m_GrayscaleImage.m_pData);
	EXPECT_EQ(Result.m_OriginalImage.m_Width, 4u);
	EXPECT_EQ(Result.m_OriginalImage.m_pData[0], 144);
	EXPECT_EQ(Result.m_GrayscaleImage.m_pData[3], 200);
	EXPECT_EQ(Result.m_GrayscaleImage.m_pData[0], Result.m_GrayscaleImage.m_pData[1]);
	EXPECT_EQ(Result.m_GrayscaleImage.m_pData[1], Result.m_GrayscaleImage.m_pData[2]);
	EXPECT_NE(Result.m_OriginalImage.m_pData[0], Result.m_GrayscaleImage.m_pData[0]);
	EXPECT_EQ(Result.m_BloodColor.r, 1.0f);
	EXPECT_EQ(Result.m_PartType, 2);
	EXPECT_EQ(Result.m_Flags, 3);
	EXPECT_STREQ(Result.m_aName, "part");
}

TEST_F(CQmSkin7PartLoadJobTest, MissingFileCompletesWithFailedResult)
{
	auto pJob = Queue();
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}

TEST_F(CQmSkin7PartLoadJobTest, MalformedPngCompletesWithFailedResult)
{
	IOHANDLE File = m_pStorage->OpenFile("skins7/part.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "invalid PNG", 11), 11u);
	EXPECT_EQ(io_close(File), 0);
	auto pJob = Queue();
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}

TEST_F(CQmSkin7PartLoadJobTest, RgbPngKeepsExistingFormatRejection)
{
	WritePng(CImageInfo::FORMAT_RGB);
	auto pJob = Queue();
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}

TEST_F(CQmSkin7PartLoadJobTest, PreparationAllocationExceptionCompletesInsteadOfLeavingPending)
{
	WritePng();
	auto pJob = Queue(FailAllocation);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}

TEST_F(CQmSkin7PartLoadJobTest, PartialPreparationExceptionDiscardsPixelsAndSharedPayload)
{
	WritePng();
	auto pJob = Queue(FailAfterPartialPreparation);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	ASSERT_TRUE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}

TEST_F(CQmSkin7PartLoadJobTest, LaterJobCanSucceedAfterPreparationException)
{
	WritePng();
	auto pFailed = Queue(FailAllocation);
	ASSERT_TRUE(WaitForJobDone(*pFailed));
	ASSERT_TRUE(pFailed->IsCompleted());
	ExpectFailedResult(pFailed->GetResult());
	auto pReady = Queue();
	ASSERT_TRUE(WaitForJobDone(*pReady));
	ASSERT_TRUE(pReady->IsCompleted());
	EXPECT_TRUE(pReady->GetResult().m_Success);
}

TEST_F(CQmSkin7PartLoadJobTest, TakingResultTwiceDoesNotReplayOrInvalidateFirstPixels)
{
	WritePng();
	auto pJob = Queue();
	ASSERT_TRUE(WaitForJobDone(*pJob));
	auto Result = pJob->GetResult();
	ExpectFailedResult(pJob->GetResult());
	pJob.reset();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_OriginalImage.m_pData, nullptr);
	EXPECT_EQ(Result.m_OriginalImage.m_pData[0], 144);
}

TEST_F(CQmSkin7PartLoadJobTest, AbortedBeforeQueueDoesNotPublishSuccess)
{
	WritePng();
	auto pJob = std::make_shared<TJob>("skins7/part.png", "part", m_pStorage.get(), IStorage::TYPE_SAVE, 2, 3, PrepareBoundary);
	ASSERT_TRUE(pJob->Abort());
	EXPECT_TRUE(m_Pool.TryAdd(pJob));
	m_Pool.Shutdown();
	m_PoolActive = false;
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
	EXPECT_FALSE(pJob->IsCompleted());
	ExpectFailedResult(pJob->GetResult());
}
