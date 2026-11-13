#include <base/borrowed_allocator.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/theme_catalog.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/rejectable_allocation_resource.h>
#include <test/test.h>

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace
{
	bool DecodePng(const void *pData, unsigned Size, const char *pName, CImageInfo &Image)
	{
		return CImageLoader::LoadPng(pData, Size, pName, Image);
	}
	class CThemeAdmissionEngine : public IEngine
	{
	public:
		bool m_Accept = true;
		bool m_Throw = false;
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
	class QmThemeIconAdmission : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CThemeAdmissionEngine m_Engine;
		CTheme m_Theme{"theme", true, true};
		std::vector<std::shared_ptr<CQmThemeIconLoadJob>> m_vJobs;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Theme.m_IconPath = "icon.png";
			std::array<uint8_t, 4> aPixel{23, 45, 67, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel.data();
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("icon.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "icon.png", Image));
		}
		bool Queue() { return TryQueueThemeIcon(m_Theme, m_vJobs, *m_pStorage, m_Engine, DecodePng); }
		void ExpectUnrequested()
		{
			EXPECT_FALSE(m_Theme.m_IconLoadRequested);
			EXPECT_FALSE(m_Theme.m_IconLoadFailed);
			EXPECT_TRUE(m_vJobs.empty());
		}
	};
}

TEST_F(QmThemeIconAdmission, RejectionReleasesCandidateAndRestoresRetryState)
{
	m_Engine.m_Accept = false;
	EXPECT_FALSE(Queue());
	ExpectUnrequested();
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	m_Engine.m_Accept = true;
	ASSERT_TRUE(Queue());
	EXPECT_TRUE(m_Theme.m_IconLoadRequested);
	EXPECT_EQ(m_vJobs.size(), 1u);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
}

TEST_F(QmThemeIconAdmission, AdmissionAllocationExceptionRollsBackTrackingAndCanRetry)
{
	m_Engine.m_Throw = true;
	EXPECT_FALSE(Queue());
	ExpectUnrequested();
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	m_Engine.m_Throw = false;
	ASSERT_TRUE(Queue());
	EXPECT_TRUE(m_Theme.m_IconLoadRequested);
	EXPECT_EQ(m_vJobs.size(), 1u);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
}

TEST_F(QmThemeIconAdmission, TrackingAllocationFailureDoesNotSubmitAndSameContainerRecovers)
{
	CRejectableAllocationResource Memory;
	Memory.m_Reject = true;
	std::vector<std::shared_ptr<CQmThemeIconLoadJob>, CBorrowedAllocator<std::shared_ptr<CQmThemeIconLoadJob>>> Jobs(&Memory);
	EXPECT_FALSE(TryQueueThemeIcon(m_Theme, Jobs, *m_pStorage, m_Engine, DecodePng));
	EXPECT_TRUE(Jobs.empty());
	EXPECT_FALSE(m_Theme.m_IconLoadRequested);
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	Memory.m_Reject = false;
	ASSERT_TRUE(TryQueueThemeIcon(m_Theme, Jobs, *m_pStorage, m_Engine, DecodePng));
	EXPECT_EQ(Jobs.size(), 1u);
	EXPECT_TRUE(m_Theme.m_IconLoadRequested);
	EXPECT_EQ(m_Engine.m_Attempts, 1);
}

TEST_F(QmThemeIconAdmission, AcceptedRequestIsNotSubmittedTwice)
{
	ASSERT_TRUE(Queue());
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	EXPECT_EQ(m_vJobs.size(), 1u);
}

TEST_F(QmThemeIconAdmission, MissingFileKeepsExistingFailureTerminalWithoutSubmission)
{
	m_Theme.m_IconPath = "missing.png";
	EXPECT_FALSE(Queue());
	EXPECT_TRUE(m_Theme.m_IconLoadFailed);
	EXPECT_FALSE(m_Theme.m_IconLoadRequested);
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	EXPECT_TRUE(m_vJobs.empty());
}

TEST_F(QmThemeIconAdmission, EmptyIconPathStaysUnrequestedWithoutBeingMarkedFailed)
{
	m_Theme.m_IconPath.clear();
	EXPECT_FALSE(Queue());
	ExpectUnrequested();
	EXPECT_EQ(m_Engine.m_Attempts, 0);
}

TEST_F(QmThemeIconAdmission, ClosedNativePoolRejectsAndReopenedPoolDecodesRetry)
{
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	Pool.Shutdown();
	m_Engine.m_pPool = &Pool;
	EXPECT_FALSE(Queue());
	ExpectUnrequested();
	EXPECT_TRUE(m_Engine.m_LastAttempt.expired());
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Queue());
	ASSERT_EQ(m_vJobs.size(), 1u);
	ASSERT_TRUE(WaitForJobDone(*m_vJobs[0]));
	EXPECT_TRUE(m_vJobs[0]->IsCompleted());
	auto Result = m_vJobs[0]->TakeResult();
	EXPECT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 23);
	Pool.Shutdown();
	m_Engine.m_pPool = nullptr;
}
