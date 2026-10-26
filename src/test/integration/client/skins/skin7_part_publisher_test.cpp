#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <memory_resource>
#include <new>
#include <stdexcept>

namespace
{
	// 只替换纹理设备边界，实际发布器、部件容器和上传限额不替换。
	class CPartGraphics : public CIconBenchmarkGraphics
	{
	public:
		std::array<bool, 64> m_aLive{};
		int m_Attempts = 0;
		int m_FailAt = 0;
		int m_ThrowAt = 0;
		int m_StaleAt = 0;
		int m_NullAt = 0;
		bool m_FailUnload = false;

		CTextureHandle LoadTextureRaw(const CImageInfo &Image, int Flags, const char *pName) override
		{
			++m_Attempts;
			if(m_Attempts == m_ThrowAt)
				throw std::bad_alloc();
			if(m_Attempts == m_FailAt)
				return {};
			if(m_Attempts == m_NullAt)
				return CreateTextureHandle(0);
			auto Handle = CIconBenchmarkGraphics::LoadTextureRaw(Image, Flags, pName);
			m_aLive.at(Handle.Id()) = m_Attempts != m_StaleAt;
			return Handle;
		}

		void UnloadTexture(CTextureHandle *pHandle) override
		{
			if(m_FailUnload)
				throw std::runtime_error("device queue unavailable");
			m_aLive.at(pHandle->Id()) = false;
			pHandle->Invalidate();
		}

		bool IsTextureHandleAllocated(CTextureHandle Handle) const override
		{
			return Handle.IsValid() && Handle.Id() < static_cast<int>(m_aLive.size()) && m_aLive[Handle.Id()];
		}

		int LiveCount() const
		{
			return std::count(m_aLive.begin(), m_aLive.end(), true);
		}
	};

	class CRejectingMemory : public std::pmr::memory_resource
	{
		void *do_allocate(size_t, size_t) override { throw std::bad_alloc(); }
		void do_deallocate(void *, size_t, size_t) override {}
		bool do_is_equal(const memory_resource &Other) const noexcept override { return this == &Other; }
	};

	class CQmSkin7PartPublisherTest : public ::testing::Test
	{
	protected:
		CQmSkin7PartLoadJob::SResult m_Result;
		std::vector<CSkins7::CSkinPart> m_vParts;
		CPartGraphics m_Graphics;
		CGpuUploadLimiter m_Limiter;
		CQmSkin7PartPublisher m_Publisher;

		void SetUp() override
		{
			m_Result.m_OriginalImage.m_Width = m_Result.m_OriginalImage.m_Height = 1;
			m_Result.m_OriginalImage.m_Format = CImageInfo::FORMAT_RGBA;
			m_Result.m_OriginalImage.m_pData = static_cast<uint8_t *>(malloc(4));
			ASSERT_NE(m_Result.m_OriginalImage.m_pData, nullptr);
			mem_copy(m_Result.m_OriginalImage.m_pData, "RGBA", 4);
			m_Result.m_GrayscaleImage = m_Result.m_OriginalImage.DeepCopy();
			ASSERT_NE(m_Result.m_GrayscaleImage.m_pData, nullptr);
			m_Result.m_PartType = 2;
			m_Result.m_Flags = 3;
			str_copy(m_Result.m_aName, "part");
			m_Result.m_pChatAvatarOriginal = std::make_shared<QmChatAvatar::SSource>();
			m_Result.m_Success = true;
			m_Limiter.OnFrameStart();
		}

		bool Publish() { return m_Publisher.TryPublish(m_Result, m_vParts, m_Graphics, m_Limiter); }

		void ExpectPixelsRetained()
		{
			ASSERT_NE(m_Result.m_OriginalImage.m_pData, nullptr);
			ASSERT_NE(m_Result.m_GrayscaleImage.m_pData, nullptr);
			EXPECT_EQ(mem_comp(m_Result.m_OriginalImage.m_pData, "RGBA", 4), 0);
			EXPECT_EQ(mem_comp(m_Result.m_GrayscaleImage.m_pData, "RGBA", 4), 0);
		}

		void TearDown() override
		{
			m_Graphics.m_FailUnload = false;
			EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
			for(auto &Part : m_vParts)
			{
				m_Graphics.UnloadTexture(&Part.m_OriginalTexture);
				m_Graphics.UnloadTexture(&Part.m_ColorableTexture);
			}
			EXPECT_EQ(m_Graphics.LiveCount(), 0);
		}
	};
}

TEST_F(CQmSkin7PartPublisherTest, TwoTexturesAndCpuMetadataCommitTogether)
{
	ASSERT_TRUE(Publish());
	ASSERT_EQ(m_vParts.size(), 1u);
	EXPECT_EQ(m_vParts[0].m_Type, 2);
	EXPECT_EQ(m_vParts[0].m_Flags, 3);
	EXPECT_STREQ(m_vParts[0].m_aName, "part");
	EXPECT_EQ(m_vParts[0].m_pChatAvatarOriginal, m_Result.m_pChatAvatarOriginal);
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(m_vParts[0].m_OriginalTexture));
	EXPECT_TRUE(m_Graphics.IsTextureHandleAllocated(m_vParts[0].m_ColorableTexture));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 2);
	ExpectPixelsRetained();
	EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
}

TEST_F(CQmSkin7PartPublisherTest, FirstInvalidUploadKeepsPixelsForLaterFrame)
{
	m_Graphics.m_FailAt = 1;
	EXPECT_FALSE(Publish());
	EXPECT_TRUE(m_vParts.empty());
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	ExpectPixelsRetained();
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(Publish());
}

TEST_F(CQmSkin7PartPublisherTest, SecondInvalidUploadReleasesFirstBeforeRetry)
{
	m_Graphics.m_FailAt = 2;
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	EXPECT_TRUE(m_vParts.empty());
	ExpectPixelsRetained();
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(Publish());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
}

TEST_F(CQmSkin7PartPublisherTest, UploadExceptionBeforeFirstHandleKeepsResult)
{
	m_Graphics.m_ThrowAt = 1;
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 1);
	ExpectPixelsRetained();
}

TEST_F(CQmSkin7PartPublisherTest, SecondUploadExceptionReleasesCandidateAndPreservesPublishedPart)
{
	ASSERT_TRUE(Publish());
	const int OldId = m_vParts[0].m_OriginalTexture.Id();
	m_Graphics.m_ThrowAt = 4;
	EXPECT_FALSE(Publish());
	ASSERT_EQ(m_vParts.size(), 1u);
	EXPECT_EQ(m_vParts[0].m_OriginalTexture.Id(), OldId);
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	ExpectPixelsRetained();
}

TEST_F(CQmSkin7PartPublisherTest, ContainerAllocationFailureDoesNotStartUpload)
{
	CRejectingMemory Memory;
	std::pmr::vector<CSkins7::CSkinPart> Parts(&Memory);
	EXPECT_FALSE(m_Publisher.TryPublish(m_Result, Parts, m_Graphics, m_Limiter));
	EXPECT_TRUE(Parts.empty());
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 0);
	ExpectPixelsRetained();
}

TEST_F(CQmSkin7PartPublisherTest, InsufficientFrameBudgetDefersWholePart)
{
	m_Limiter.OnFrameStart(1);
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	ExpectPixelsRetained();
	m_Limiter.OnFrameStart(2);
	EXPECT_TRUE(Publish());
}

TEST_F(CQmSkin7PartPublisherTest, FailedCleanupBlocksNewUploadsUntilReleaseSucceeds)
{
	m_Graphics.m_FailAt = 2;
	m_Graphics.m_FailUnload = true;
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
	m_Limiter.OnFrameStart();
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.m_Attempts, 2);
	ExpectPixelsRetained();
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(Publish());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
}

TEST_F(CQmSkin7PartPublisherTest, ExplicitReleaseCanCleanCandidateAfterOwnerCancelsJob)
{
	m_Graphics.m_FailAt = 2;
	m_Graphics.m_FailUnload = true;
	EXPECT_FALSE(Publish());
	m_Result.ClearPayload();
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
	EXPECT_TRUE(m_Publisher.TryRelease(m_Graphics));
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	EXPECT_TRUE(m_vParts.empty());
}

TEST_F(CQmSkin7PartPublisherTest, NullTextureIsNotPublishedOrUnloadedAsOwnedTexture)
{
	m_Graphics.m_NullAt = 2;
	EXPECT_FALSE(Publish());
	EXPECT_TRUE(m_vParts.empty());
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	ExpectPixelsRetained();
}

TEST_F(CQmSkin7PartPublisherTest, StaleHandleIsRejectedBeforePublishingPair)
{
	m_Graphics.m_StaleAt = 2;
	EXPECT_FALSE(Publish());
	EXPECT_TRUE(m_vParts.empty());
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
	ExpectPixelsRetained();
}

TEST_F(CQmSkin7PartPublisherTest, FailedDecodeDoesNotUseTextureBudget)
{
	m_Result.ClearPayload();
	EXPECT_FALSE(Publish());
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 0);
	EXPECT_TRUE(m_vParts.empty());
}

TEST_F(CQmSkin7PartPublisherTest, NativePngJobRetriesGpuFailureAndConsumesOnlyCommittedResult)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	IOHANDLE File = pStorage->OpenFile("part.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_TRUE(CImageLoader::SavePng(File, "part", m_Result.m_OriginalImage));
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	auto pJob = std::make_shared<CQmSkin7PartLoadJob>("part.png", "part", pStorage.get(), IStorage::TYPE_SAVE, 2, 3, [](auto &) {});
	ASSERT_TRUE(Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	const uint8_t *pGray = nullptr;
	m_Graphics.m_FailAt = 2;
	EXPECT_FALSE(pJob->PublishResult([&](const auto &Result) {
		pGray = Result.m_GrayscaleImage.m_pData;
		return m_Publisher.TryPublish(Result, m_vParts, m_Graphics, m_Limiter);
	}));
	ASSERT_NE(pGray, nullptr);
	EXPECT_TRUE(m_vParts.empty());
	m_Limiter.OnFrameStart();
	EXPECT_TRUE(pJob->PublishResult([&](const auto &Result) {
		EXPECT_EQ(Result.m_GrayscaleImage.m_pData, pGray);
		return m_Publisher.TryPublish(Result, m_vParts, m_Graphics, m_Limiter);
	}));
	ASSERT_EQ(m_vParts.size(), 1u);
	EXPECT_FALSE(pJob->PublishResult([](const auto &) {
		ADD_FAILURE() << "committed result reached publisher twice";
		return true;
	}));
	EXPECT_EQ(m_Graphics.m_Attempts, 4);
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	Pool.Shutdown();
}
