#include <engine/client/gpu_upload_limiter.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <new>
#include <utility>

namespace
{
	class CThumbnailEngine : public IEngine
	{
	public:
		CJobPool m_Pool;
		std::weak_ptr<IJob> m_LastJob;
		int m_Submissions = 0;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Submissions;
			m_LastJob = pJob;
			return m_Pool.TryAdd(std::move(pJob));
		}
		void ShutdownJobs() override { m_Pool.Shutdown(); }
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	// 只替换设备边界；读取、解码、任务池、缓存状态和淘汰均执行生产实现。
	class CThumbnailGraphics : public CIconBenchmarkGraphics
	{
	public:
		enum class EUpload
		{
			NORMAL,
			THROW_BEFORE_MOVE,
			THROW_AFTER_MOVE,
			INVALID
		};
		EUpload m_Upload = EUpload::NORMAL;
		bool m_ThrowUnload = false;
		int m_FailUnloadOnAttempt = 0;
		int m_UploadAttempts = 0;
		int m_UnloadAttempts = 0;
		int m_Unloads = 0;
		int m_ActiveTextures = 0;
		CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pName) override
		{
			++m_UploadAttempts;
			if(m_Upload == EUpload::THROW_BEFORE_MOVE)
				throw std::bad_alloc();
			if(m_Upload == EUpload::THROW_AFTER_MOVE || m_Upload == EUpload::INVALID)
			{
				Image.Free();
				if(m_Upload == EUpload::THROW_AFTER_MOVE)
					throw std::bad_alloc();
				return {};
			}
			auto Handle = CIconBenchmarkGraphics::LoadTextureRawMove(Image, Flags, pName);
			++m_ActiveTextures;
			return Handle;
		}
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			++m_UnloadAttempts;
			if(m_ThrowUnload || m_UnloadAttempts == m_FailUnloadOnAttempt)
				throw std::bad_alloc();
			if(pHandle->IsValid())
			{
				++m_Unloads;
				--m_ActiveTextures;
				pHandle->Invalidate();
			}
		}
	};

	class QmScreenshotThumbnailUpload : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CThumbnailEngine m_Engine;
		CQmScreenshotManager m_Manager;
		CThumbnailGraphics m_Graphics;
		CGpuUploadLimiter m_Limiter;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_Engine.m_Pool.TryInit(1));
		}
		void TearDown() override
		{
			m_Engine.m_Pool.Shutdown();
			m_Graphics.m_ThrowUnload = false;
			m_Manager.ClearThumbnails(&m_Graphics);
		}
		void Decode(const char *pPath = "source.png")
		{
			std::array<uint8_t, 4> aPixel{23, 45, 67, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel.data();
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE), pPath, Image));
			ASSERT_NE(m_Manager.LoadThumbnail(pPath, IStorage::TYPE_SAVE), nullptr);
			m_Manager.PumpThumbnails(nullptr, m_pStorage.get(), &m_Engine, nullptr);
			auto pJob = m_Engine.m_LastJob.lock();
			ASSERT_NE(pJob, nullptr);
			ASSERT_TRUE(WaitForJobDone(*pJob));
			ASSERT_NE(std::static_pointer_cast<CQmScreenshotImageJob>(pJob)->Image(), nullptr);
		}
		void Pump() { m_Manager.PumpThumbnails(&m_Graphics, m_pStorage.get(), &m_Engine, &m_Limiter); }
		const CQmScreenshotManager::SThumbnail *Thumbnail() { return m_Manager.LoadThumbnail("source.png", IStorage::TYPE_SAVE); }
	};
}

TEST_F(QmScreenshotThumbnailUpload, SuccessfulUploadIsPublishedOnceAndClearReleasesItOnce)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_TRUE(pThumbnail->m_Texture.IsValid());
	EXPECT_EQ(pThumbnail->m_Width, 1);
	EXPECT_EQ(pThumbnail->m_Height, 1);
	EXPECT_FALSE(pThumbnail->m_Loading);
	EXPECT_FALSE(pThumbnail->m_LoadFailed);
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 1);
	m_Manager.ClearThumbnails(&m_Graphics);
	m_Manager.ClearThumbnails(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 0);
}

TEST_F(QmScreenshotThumbnailUpload, FailureBeforePixelMoveRetainsImageAndDiscardsStaleFrameRequests)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	const auto pJob = std::static_pointer_cast<CQmScreenshotImageJob>(m_Engine.m_LastJob.lock());
	ASSERT_NE(pJob, nullptr);
	ASSERT_NE(m_Manager.LoadThumbnail("obsolete.png", IStorage::TYPE_SAVE), nullptr);
	m_Graphics.m_Upload = CThumbnailGraphics::EUpload::THROW_BEFORE_MOVE;
	EXPECT_NO_THROW(Pump());
	ASSERT_NE(pJob->Image(), nullptr);
	EXPECT_EQ(pJob->Image()->m_pData[0], 23);
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_TRUE(pThumbnail->m_Loading);
	EXPECT_EQ(pThumbnail->m_Width, 0);
	EXPECT_FALSE(pThumbnail->m_Texture.IsValid());
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 0);
	m_Graphics.m_Upload = CThumbnailGraphics::EUpload::NORMAL;
	Pump();
	EXPECT_EQ(m_Engine.m_Submissions, 1);
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 2);
	EXPECT_TRUE(Thumbnail()->m_Texture.IsValid());
	EXPECT_EQ(pJob->Image(), nullptr);
}

TEST_F(QmScreenshotThumbnailUpload, FailureAfterPixelMoveEndsAsFailedWithoutReusingConsumedImage)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	const auto pJob = std::static_pointer_cast<CQmScreenshotImageJob>(m_Engine.m_LastJob.lock());
	ASSERT_NE(pJob, nullptr);
	m_Graphics.m_Upload = CThumbnailGraphics::EUpload::THROW_AFTER_MOVE;
	EXPECT_NO_THROW(Pump());
	EXPECT_EQ(pJob->Image(), nullptr);
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 0);
	m_Graphics.m_Upload = CThumbnailGraphics::EUpload::NORMAL;
	Pump();
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_FALSE(pThumbnail->m_Loading);
	EXPECT_TRUE(pThumbnail->m_LoadFailed);
	EXPECT_FALSE(pThumbnail->m_Texture.IsValid());
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
	EXPECT_EQ(m_Engine.m_Submissions, 1);
}

TEST_F(QmScreenshotThumbnailUpload, InvalidReturnedTextureKeepsExistingTerminalFailureAndBudgetPolicy)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	m_Graphics.m_Upload = CThumbnailGraphics::EUpload::INVALID;
	Pump();
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_FALSE(pThumbnail->m_Loading);
	EXPECT_TRUE(pThumbnail->m_LoadFailed);
	EXPECT_FALSE(pThumbnail->m_Texture.IsValid());
	EXPECT_EQ(m_Limiter.UploadsThisFrame(), 1);
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
}

TEST_F(QmScreenshotThumbnailUpload, ExhaustedUploadBudgetRetainsDecodedImageUntilNextFrame)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	const auto pJob = std::static_pointer_cast<CQmScreenshotImageJob>(m_Engine.m_LastJob.lock());
	ASSERT_NE(pJob, nullptr);
	m_Limiter.OnFrameStart(0);
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 0);
	ASSERT_NE(pJob->Image(), nullptr);
	m_Limiter.OnFrameStart(1);
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
	EXPECT_TRUE(Thumbnail()->m_Texture.IsValid());
	EXPECT_EQ(pJob->Image(), nullptr);
}

TEST_F(QmScreenshotThumbnailUpload, EvictionFailureRetainsTextureOwnershipAndRetriesNextFrame)
{
	for(int Index = 0; Index <= QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT; ++Index)
	{
		SCOPED_TRACE(Index);
		char aPath[64];
		str_format(aPath, sizeof(aPath), "source-%d.png", Index);
		ASSERT_NO_FATAL_FAILURE(Decode(aPath));
		m_Limiter.OnFrameStart();
		m_Graphics.m_ThrowUnload = Index == QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT;
		EXPECT_NO_THROW(Pump());
	}
	EXPECT_EQ(m_Graphics.m_ActiveTextures, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT + 1);
	EXPECT_EQ(m_Graphics.m_UnloadAttempts, 1);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	m_Graphics.m_ThrowUnload = false;
	Pump();
	EXPECT_EQ(m_Graphics.m_ActiveTextures, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	m_Manager.ClearThumbnails(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 0);
	EXPECT_EQ(m_Graphics.m_Unloads, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT + 1);
}

TEST_F(QmScreenshotThumbnailUpload, FailedClearRetainsOwnershipAndBlocksOldCacheUntilFrameRetry)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	m_Graphics.m_ThrowUnload = true;
	EXPECT_NO_THROW(m_Manager.ClearThumbnails(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 1);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	EXPECT_EQ(Thumbnail(), nullptr);
	EXPECT_EQ(m_Manager.LoadThumbnail("new.png", IStorage::TYPE_SAVE), nullptr);
	EXPECT_NO_THROW(Pump());
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
	EXPECT_EQ(m_Engine.m_Submissions, 1);
	m_Graphics.m_ThrowUnload = false;
	Pump();
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 0);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_TRUE(pThumbnail->m_Texture.IsValid());
	EXPECT_EQ(m_Engine.m_Submissions, 2);
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 2);
}

TEST_F(QmScreenshotThumbnailUpload, PartialClearContinuesPastFailureWithoutUnloadingSuccessfulEntryTwice)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	ASSERT_NO_FATAL_FAILURE(Decode("second.png"));
	Pump();
	ASSERT_EQ(m_Graphics.m_ActiveTextures, 2);
	m_Graphics.m_FailUnloadOnAttempt = 1;
	EXPECT_NO_THROW(m_Manager.ClearThumbnails(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_UnloadAttempts, 2);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 1);
	EXPECT_EQ(Thumbnail(), nullptr);
	m_Manager.ClearThumbnails(&m_Graphics);
	m_Manager.ClearThumbnails(&m_Graphics);
	EXPECT_EQ(m_Graphics.m_UnloadAttempts, 3);
	EXPECT_EQ(m_Graphics.m_Unloads, 2);
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 0);
}

TEST_F(QmScreenshotThumbnailUpload, ClearWithoutGraphicsRetainsTextureUntilGraphicsBecomesAvailable)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	m_Manager.ClearThumbnails(nullptr);
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 1);
	EXPECT_EQ(Thumbnail(), nullptr);
	m_Manager.PumpThumbnails(nullptr, m_pStorage.get(), &m_Engine, nullptr);
	EXPECT_EQ(m_Graphics.m_UnloadAttempts, 0);
	EXPECT_EQ(m_Engine.m_Submissions, 1);
	Pump();
	EXPECT_EQ(m_Graphics.m_ActiveTextures, 0);
	EXPECT_EQ(m_Graphics.m_Unloads, 1);
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 2);
}

TEST_F(QmScreenshotThumbnailUpload, ClearWithoutGpuResourcesDropsDecodedTaskAndAllowsFreshSubmission)
{
	ASSERT_NO_FATAL_FAILURE(Decode());
	m_Manager.ClearThumbnails(nullptr);
	ASSERT_NO_FATAL_FAILURE(Decode());
	Pump();
	EXPECT_EQ(m_Engine.m_Submissions, 2);
	EXPECT_EQ(m_Graphics.m_UploadAttempts, 1);
	const auto *pThumbnail = Thumbnail();
	ASSERT_NE(pThumbnail, nullptr);
	EXPECT_TRUE(pThumbnail->m_Texture.IsValid());
}
