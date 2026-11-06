#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
	class CAdmissionEngine : public IEngine
	{
	public:
		bool m_Accept = true;
		bool m_Throw = false;
		int m_RejectsRemaining = 0;
		int m_Attempts = 0;
		CJobPool *m_pPool = nullptr;
		std::weak_ptr<IJob> m_LastAttempt;
		std::vector<std::shared_ptr<IJob>> m_vAccepted;

		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			m_LastAttempt = pJob;
			if(m_Throw)
				throw std::bad_alloc();
			if(m_RejectsRemaining > 0)
			{
				--m_RejectsRemaining;
				return false;
			}
			if(!m_Accept)
				return false;
			if(m_pPool != nullptr)
				return m_pPool->TryAdd(std::move(pJob));
			m_vAccepted.push_back(std::move(pJob));
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class QmScreenshotThumbnailSubmission : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CQmScreenshotManager m_Manager;
		CAdmissionEngine m_Engine;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Request(const char *pPath = "source.png")
		{
			ASSERT_NE(m_Manager.LoadThumbnail(pPath, IStorage::TYPE_SAVE), nullptr);
		}
		void Pump() { m_Manager.PumpThumbnails(nullptr, m_pStorage.get(), &m_Engine, nullptr); }
		void ExpectPending()
		{
			const auto *pThumbnail = m_Manager.LoadThumbnail("source.png", IStorage::TYPE_SAVE);
			ASSERT_NE(pThumbnail, nullptr);
			EXPECT_TRUE(pThumbnail->m_Loading);
			EXPECT_FALSE(pThumbnail->m_LoadFailed);
		}
		bool StoreSource()
		{
			std::array<uint8_t, 4> aPixel{23, 45, 67, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel.data();
			return CImageLoader::SavePng(m_pStorage->OpenFile("source.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "source.png", Image);
		}
	};
}

TEST_F(QmScreenshotThumbnailSubmission, AcceptedVisibleRequestIsNotSubmittedTwice)
{
	Request();
	Pump();
	ASSERT_EQ(m_Engine.m_vAccepted.size(), 1u);
	Request();
	Pump();
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	EXPECT_EQ(m_Engine.m_vAccepted.size(), 1u);
}

TEST_F(QmScreenshotThumbnailSubmission, RejectedRequestReleasesCandidateAndCanRetryNextFrame)
{
	m_Engine.m_Accept = false;
	Request();
	Pump();
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	ExpectPending();
	m_Engine.m_Accept = true;
	Pump();
	EXPECT_EQ(m_Engine.m_Attempts, 2);
	EXPECT_EQ(m_Engine.m_vAccepted.size(), 1u);
}

TEST_F(QmScreenshotThumbnailSubmission, ThrowingAdmissionReleasesCandidateAndCanRetryNextFrame)
{
	m_Engine.m_Throw = true;
	Request();
	EXPECT_NO_THROW(Pump());
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	ExpectPending();
	m_Engine.m_Throw = false;
	Pump();
	EXPECT_EQ(m_Engine.m_Attempts, 2);
	EXPECT_EQ(m_Engine.m_vAccepted.size(), 1u);
}

TEST_F(QmScreenshotThumbnailSubmission, RejectedRequestDoesNotConsumeSlotsForOtherVisibleItems)
{
	m_Engine.m_RejectsRemaining = 1;
	for(const char *pPath : {"first.png", "second.png", "third.png", "fourth.png", "fifth.png"})
		Request(pPath);
	Pump();
	EXPECT_EQ(m_Engine.m_Attempts, 5);
	EXPECT_EQ(m_Engine.m_vAccepted.size(), (size_t)QM_SCREENSHOT_THUMBNAIL_MAX_CONCURRENT_JOBS);
}

TEST_F(QmScreenshotThumbnailSubmission, ClosedNativePoolRejectsAndReopenedPoolDecodesRetry)
{
	ASSERT_TRUE(StoreSource());
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	Pool.Shutdown();
	m_Engine.m_pPool = &Pool;
	Request();
	Pump();
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	ExpectPending();

	ASSERT_TRUE(Pool.TryInit(1));
	Pump();
	auto pJob = m_Engine.m_LastAttempt.lock();
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(WaitForJobDone(*pJob));
	auto pImageJob = std::static_pointer_cast<CQmScreenshotImageJob>(pJob);
	ASSERT_NE(pImageJob->Image(), nullptr);
	EXPECT_EQ(pImageJob->Image()->m_Width, 1u);
	EXPECT_EQ(pImageJob->Image()->m_Height, 1u);
	EXPECT_EQ(pImageJob->Image()->m_pData[0], 23);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
	Pool.Shutdown();
	m_Engine.m_pPool = nullptr;
}
