#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <game/client/components/countryflags.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <cstdlib>
#include <deque>
#include <memory>
#include <new>
#include <vector>

namespace
{
	using CJob = CCountryFlags::CCountryFlagLoadJob;

	class CCountryFlagJob : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_Initialized = false;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_Pool.TryInit(1));
			m_Initialized = true;
		}
		void TearDown() override
		{
			if(m_Initialized)
				m_Pool.Shutdown();
			m_pStorage.reset();
		}
		void WritePng()
		{
			std::unique_ptr<uint8_t, decltype(&free)> pPixels(static_cast<uint8_t *>(malloc(4)), free);
			ASSERT_NE(pPixels, nullptr);
			pPixels.get()[0] = 21;
			pPixels.get()[1] = 43;
			pPixels.get()[2] = 65;
			pPixels.get()[3] = 255;
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = pPixels.get();
			IOHANDLE File = m_pStorage->OpenFile("flag.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, "flag.png", Image));
		}
	};

	class CThrowingCountryFlagJob : public CJob
	{
	protected:
		void Run() override
		{
			Load([](void **, unsigned *) -> bool { throw std::bad_alloc(); },
				[](const void *, unsigned, CImageInfo &) { return false; });
		}

	public:
		CThrowingCountryFlagJob() :
			CJob("throwing.png", 42, nullptr) {}
	};
}

TEST_F(CCountryFlagJob, AcceptedNativePngResultOutlivesJobAndPool)
{
	WritePng();
	std::deque<std::shared_ptr<CJob>> Pending;
	std::vector<bool> Triggered(8, false);
	ASSERT_TRUE(CCountryFlags::TryStartFlagLoad(Pending, Triggered, 7, [&]() { return std::make_shared<CJob>("flag.png", 42, m_pStorage.get(), 7); }, [&](const std::shared_ptr<CJob> &pJob) { return m_Pool.TryAdd(pJob); }));
	ASSERT_EQ(Pending.size(), 1u);
	EXPECT_TRUE(Triggered[7]);
	ASSERT_TRUE(WaitForJobDone(*Pending.front()));
	EXPECT_TRUE(Pending.front()->IsCompleted());
	auto Result = Pending.front()->GetResult();
	Pending.clear();
	m_Pool.Shutdown();
	m_Initialized = false;
	ASSERT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
	EXPECT_EQ(Result.m_FlagIndex, 7u);
	EXPECT_EQ(Result.m_Image.m_Width, 1u);
	EXPECT_EQ(Result.m_Image.m_Height, 1u);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 21);
}

TEST_F(CCountryFlagJob, MissingFilePublishesFailedCompletion)
{
	auto pJob = std::make_shared<CJob>("missing.png", 42, m_pStorage.get());
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	EXPECT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
}

TEST_F(CCountryFlagJob, InvalidPngPublishesFailedCompletion)
{
	IOHANDLE File = m_pStorage->OpenFile("invalid.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const bool Written = io_write(File, "invalid", 7) == 7;
	io_close(File);
	ASSERT_TRUE(Written);
	auto pJob = std::make_shared<CJob>("invalid.png", 42, m_pStorage.get());
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	EXPECT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
}

TEST_F(CCountryFlagJob, ClosedPoolRejectionLeavesFlagRetryableAndUntracked)
{
	m_Pool.Shutdown();
	m_Initialized = false;
	std::deque<std::shared_ptr<CJob>> Pending;
	std::vector<bool> Triggered{false};
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(Pending, Triggered, 0, [&]() { return std::make_shared<CJob>("flag.png", 42, m_pStorage.get()); }, [&](const std::shared_ptr<CJob> &pJob) { return m_Pool.TryAdd(pJob); }));
	EXPECT_FALSE(Triggered[0]);
	EXPECT_TRUE(Pending.empty());
}

TEST_F(CCountryFlagJob, WorkerExceptionPublishesFailedComponentResult)
{
	auto pJob = std::make_shared<CThrowingCountryFlagJob>();
	ASSERT_TRUE(m_Pool.TryAdd(pJob));
	ASSERT_TRUE(WaitForJobDone(*pJob));
	EXPECT_TRUE(pJob->IsCompleted());
	auto Result = pJob->GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
}
