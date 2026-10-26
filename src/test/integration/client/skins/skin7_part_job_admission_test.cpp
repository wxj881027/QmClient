#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/skin7_info_job.h>
#include <game/client/components/qmclient/skin7_part_load_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <new>
#include <vector>

namespace
{
	using TJob = CQmSkin7PartLoadJob;
	using TParts = std::vector<CQmSkin7InfoJob::SPart>;

	void PrepareBoundary(TJob::SResult &) {}

	class CAdmissionEngine : public IEngine
	{
	public:
		bool m_Accept = true;
		bool m_Throw = false;
		int m_Attempts = 0;
		CJobPool *m_pPool = nullptr;
		std::vector<std::shared_ptr<IJob>> m_vAccepted;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			++m_Attempts;
			if(m_Throw)
				throw std::bad_alloc();
			if(!m_Accept)
				return false;
			if(m_pPool)
				return m_pPool->TryAdd(std::move(pJob));
			m_vAccepted.push_back(std::move(pJob));
			return true;
		}
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
	};

	class CTrackingMemory : public IAllocationResource
	{
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			return IAllocationResource::AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pMemory, size_t Bytes, size_t Alignment) noexcept override
		{
			IAllocationResource::DeallocateDefault(pMemory, Bytes, Alignment);
		}

	public:
		bool m_Fail = false;
	};

	class CQmSkin7PartAdmissionTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CAdmissionEngine m_Engine;
		TParts m_Parts{{0, IStorage::TYPE_SAVE, "first.png"}, {0, IStorage::TYPE_SAVE, "second.png"}};
		std::deque<std::shared_ptr<TJob>> m_Pending;
		size_t m_NextPart = 0;
		bool m_FactoryThrows = false;
		std::weak_ptr<TJob> m_LastCreated;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
		}

		std::shared_ptr<TJob> Create(const CQmSkin7InfoJob::SPart &Part)
		{
			if(m_FactoryThrows)
				throw std::bad_alloc();
			const std::string Path = "skins7/" + Part.m_Name;
			auto pJob = std::make_shared<TJob>(Path.c_str(), Part.m_Name.c_str(), m_pStorage.get(), Part.m_StorageType, Part.m_Part, 0, PrepareBoundary);
			m_LastCreated = pJob;
			return pJob;
		}

		bool Queue()
		{
			return QmTryQueueNextSkin7Part(m_Parts, m_NextPart, m_Pending, m_Engine, [this](const auto &Part) { return Create(Part); });
		}

		void WritePng(const char *pName)
		{
			const std::string Path = "skins7/" + std::string(pName);
			std::array<uint8_t, 4> aPixel{23, 45, 67, 255};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixel.data();
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(File, pName, Image));
		}
	};
}

TEST_F(CQmSkin7PartAdmissionTest, FactoryFailureKeepsDescriptorAndRetrySubmitsSameEntry)
{
	m_FactoryThrows = true;
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, 0u);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	m_FactoryThrows = false;
	ASSERT_TRUE(Queue());
	EXPECT_EQ(m_NextPart, 1u);
	EXPECT_EQ(m_Pending.size(), 1u);
	EXPECT_EQ(m_Engine.m_Attempts, 1);
}

TEST_F(CQmSkin7PartAdmissionTest, TrackingAllocationFailurePreservesEarlierJobsAndDoesNotSubmitCandidate)
{
	CTrackingMemory Memory;
	std::deque<std::shared_ptr<TJob>, CBorrowedAllocator<std::shared_ptr<TJob>>> Pending(&Memory);
	TParts Parts(2048, {0, IStorage::TYPE_SAVE, "part.png"});
	size_t Next = 0;
	ASSERT_TRUE(QmTryQueueNextSkin7Part(Parts, Next, Pending, m_Engine, [this](const auto &Part) { return Create(Part); }));
	Memory.m_Fail = true;
	bool Failed = false;
	while(Next < Parts.size())
	{
		const size_t Before = Next;
		const int Attempts = m_Engine.m_Attempts;
		if(QmTryQueueNextSkin7Part(Parts, Next, Pending, m_Engine, [this](const auto &Part) { return Create(Part); }))
			continue;
		Failed = true;
		EXPECT_EQ(Next, Before);
		EXPECT_EQ(Pending.size(), Before);
		EXPECT_EQ(m_Engine.m_Attempts, Attempts);
		EXPECT_TRUE(m_LastCreated.expired());
		break;
	}
	ASSERT_TRUE(Failed);
	ASSERT_EQ(Pending.size(), m_Engine.m_vAccepted.size());
	for(size_t i = 0; i < Pending.size(); ++i)
		EXPECT_EQ(Pending[i], m_Engine.m_vAccepted[i]);
	const size_t Before = Next;
	Memory.m_Fail = false;
	ASSERT_TRUE(QmTryQueueNextSkin7Part(Parts, Next, Pending, m_Engine, [this](const auto &Part) { return Create(Part); }));
	EXPECT_EQ(Next, Before + 1);
	EXPECT_EQ(Pending.size(), Before + 1);
}

TEST_F(CQmSkin7PartAdmissionTest, RejectionRollsBackOnlyCandidateAndKeepsNextDescriptor)
{
	ASSERT_TRUE(Queue());
	const auto pFirst = m_Pending.front();
	m_Engine.m_Accept = false;
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, 1u);
	ASSERT_EQ(m_Pending.size(), 1u);
	EXPECT_EQ(m_Pending.front(), pFirst);
	EXPECT_TRUE(m_LastCreated.expired());
	m_Engine.m_Accept = true;
	ASSERT_TRUE(Queue());
	EXPECT_EQ(m_NextPart, 2u);
	EXPECT_EQ(m_Pending.size(), 2u);
}

TEST_F(CQmSkin7PartAdmissionTest, AdmissionExceptionWithdrawsTrackingAndAllowsRetry)
{
	m_Engine.m_Throw = true;
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, 0u);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_TRUE(m_LastCreated.expired());
	m_Engine.m_Throw = false;
	ASSERT_TRUE(Queue());
	EXPECT_EQ(m_NextPart, 1u);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
}

TEST_F(CQmSkin7PartAdmissionTest, EmptyCandidateKeepsSkipContractWithoutEngineSubmission)
{
	EXPECT_TRUE(QmTryQueueNextSkin7Part(m_Parts, m_NextPart, m_Pending, m_Engine, [](const auto &) -> std::shared_ptr<TJob> { return nullptr; }));
	EXPECT_EQ(m_NextPart, 1u);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
}

TEST_F(CQmSkin7PartAdmissionTest, ExhaustedInputDoesNotCreateOrResubmitJobs)
{
	m_NextPart = m_Parts.size();
	m_FactoryThrows = true;
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, m_Parts.size());
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
}

TEST_F(CQmSkin7PartAdmissionTest, ClosedNativePoolKeepsEntryAndReopenedPoolDecodesRetry)
{
	WritePng("first.png");
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	Pool.Shutdown();
	m_Engine.m_pPool = &Pool;
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, 0u);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_TRUE(m_LastCreated.expired());
	ASSERT_TRUE(Pool.TryInit(1));
	ASSERT_TRUE(Queue());
	ASSERT_EQ(m_Pending.size(), 1u);
	ASSERT_TRUE(WaitForJobDone(*m_Pending.front()));
	ASSERT_TRUE(m_Pending.front()->IsCompleted());
	auto Result = m_Pending.front()->GetResult();
	ASSERT_TRUE(Result.m_Success);
	ASSERT_NE(Result.m_OriginalImage.m_pData, nullptr);
	EXPECT_EQ(Result.m_OriginalImage.m_pData[0], 23);
	EXPECT_EQ(m_NextPart, 1u);
}

TEST_F(CQmSkin7PartAdmissionTest, AcceptedEntriesKeepOrderAndAreNotRepeatedAfterExhaustion)
{
	WritePng("first.png");
	WritePng("second.png");
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	m_Engine.m_pPool = &Pool;
	ASSERT_TRUE(Queue());
	ASSERT_TRUE(Queue());
	EXPECT_FALSE(Queue());
	EXPECT_EQ(m_NextPart, 2u);
	EXPECT_EQ(m_Engine.m_Attempts, 2);
	ASSERT_EQ(m_Pending.size(), 2u);
	for(size_t i = 0; i < m_Pending.size(); ++i)
	{
		ASSERT_TRUE(WaitForJobDone(*m_Pending[i]));
		auto Result = m_Pending[i]->GetResult();
		ASSERT_TRUE(Result.m_Success);
		EXPECT_STREQ(Result.m_aName, m_Parts[i].m_Name.c_str());
	}
}
