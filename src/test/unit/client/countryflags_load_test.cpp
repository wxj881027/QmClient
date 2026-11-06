#include <game/client/components/countryflags.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <deque>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	using CJob = CCountryFlags::CCountryFlagLoadJob;
	using EStatus = CJob::ELoadStatus;
	using CJobPtr = std::shared_ptr<CJob>;

	struct SAllocation
	{
		bool m_Fail = false;
	};

	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		SAllocation *m_pState;
		explicit CAllocator(SAllocation *pState) :
			m_pState(pState) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			if(m_pState->m_Fail)
				throw std::bad_alloc();
			return std::allocator<T>{}.allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>{}.deallocate(pData, Count); }
		template<typename U, typename... TArgs>
		void construct(U *pData, TArgs &&...Args)
		{
			if(m_pState->m_Fail)
				throw std::bad_alloc();
			::new(static_cast<void *>(pData)) U(std::forward<TArgs>(Args)...);
		}
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
		template<typename U>
		bool operator!=(const CAllocator<U> &Other) const
		{
			return !(*this == Other);
		}
	};

	class CFlagAdmission : public ::testing::Test
	{
	protected:
		SAllocation m_Allocation;
		std::deque<CJobPtr, CAllocator<CJobPtr>> m_Pending{CAllocator<CJobPtr>(&m_Allocation)};
		std::vector<bool> m_Triggered{false, false};
		int m_Creates = 0, m_Submits = 0;
		CJobPtr Create()
		{
			++m_Creates;
			return std::make_shared<CJob>("flag.png", 42, nullptr);
		}
		bool Start()
		{
			return CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, [&]() { return Create(); }, [&](const CJobPtr &) { ++m_Submits; return true; });
		}
	};

	void FillImage(CImageInfo &Image)
	{
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(4));
		ASSERT_NE(Image.m_pData, nullptr);
		Image.m_pData[0] = 73;
	}

	bool ReadBytes(void **ppData, unsigned *pSize)
	{
		*ppData = malloc(4);
		*pSize = 4;
		EXPECT_NE(*ppData, nullptr);
		return *ppData != nullptr;
	}
}

TEST_F(CFlagAdmission, AcceptedJobIsTrackedBeforeSubmissionAndThenMarkedTriggered)
{
	EXPECT_TRUE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, [&]() { return Create(); }, [&](const CJobPtr &pJob) {
			EXPECT_EQ(m_Pending.size(), 1u);
			EXPECT_EQ(m_Pending.back(), pJob);
			EXPECT_FALSE(m_Triggered[0]);
			++m_Submits;
			return true; }));
	EXPECT_TRUE(m_Triggered[0]);
	EXPECT_EQ(m_Creates, 1);
	EXPECT_EQ(m_Submits, 1);
}

TEST_F(CFlagAdmission, AlreadyTriggeredEntryDoesNotCreateOrSubmitAnotherJob)
{
	ASSERT_TRUE(Start());
	EXPECT_FALSE(Start());
	EXPECT_EQ(m_Pending.size(), 1u);
	EXPECT_EQ(m_Creates, 1);
	EXPECT_EQ(m_Submits, 1);
}

TEST_F(CFlagAdmission, OutOfRangeEntryDoesNotCreateOrSubmitJob)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, m_Triggered.size(), [&]() { return Create(); }, [&](const CJobPtr &) { ++m_Submits; return true; }));
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_EQ(m_Creates, 0);
	EXPECT_EQ(m_Submits, 0);
}

TEST_F(CFlagAdmission, FactoryAllocationFailureLeavesEntryRetryable)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, []() -> CJobPtr { throw std::bad_alloc(); }, [](const CJobPtr &) { return true; }));
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_TRUE(Start());
	EXPECT_EQ(m_Submits, 1);
}

TEST_F(CFlagAdmission, TrackingFailurePreservesOlderJobAndDoesNotSubmitCandidate)
{
	const auto pOlder = Create();
	m_Pending.push_back(pOlder);
	m_Allocation.m_Fail = true;
	EXPECT_FALSE(Start());
	ASSERT_EQ(m_Pending.size(), 1u);
	EXPECT_EQ(m_Pending.front(), pOlder);
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_EQ(m_Submits, 0);
	m_Allocation.m_Fail = false;
	EXPECT_TRUE(Start());
	EXPECT_EQ(m_Pending.size(), 2u);
	EXPECT_EQ(m_Submits, 1);
}

TEST_F(CFlagAdmission, RejectedSubmissionRemovesTrackingAndAllowsOneRetry)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, [&]() { return Create(); }, [](const CJobPtr &) { return false; }));
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_TRUE(Start());
	EXPECT_EQ(m_Pending.size(), 1u);
	EXPECT_EQ(m_Submits, 1);
}

TEST_F(CFlagAdmission, SubmissionAllocationFailureRollsBackTracking)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, [&]() { return Create(); }, [](const CJobPtr &) -> bool { throw std::bad_alloc(); }));
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_TRUE(Start());
}

TEST_F(CFlagAdmission, CapacityFailureLeavesEntryAndQueueUnchanged)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, []() -> CJobPtr { throw std::length_error("flag path too large"); }, [](const CJobPtr &) { return true; }));
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
}

TEST_F(CFlagAdmission, UnexpectedSubmissionExceptionPropagatesAfterRollback)
{
	EXPECT_THROW(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, [&]() { return Create(); }, [](const CJobPtr &) -> bool { throw std::runtime_error("submission failed"); }), std::runtime_error);
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
}

TEST_F(CFlagAdmission, NullFactoryResultDoesNotPublishTracking)
{
	EXPECT_FALSE(CCountryFlags::TryStartFlagLoad(m_Pending, m_Triggered, 0, []() -> CJobPtr { return nullptr; }, [&](const CJobPtr &) { ++m_Submits; return true; }));
	EXPECT_FALSE(m_Triggered[0]);
	EXPECT_TRUE(m_Pending.empty());
	EXPECT_EQ(m_Submits, 0);
}

TEST(CountryFlagLoad, SuccessfulDecodePublishesOwnedPixelsAndCountryCode)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_FALSE(Job.IsCompleted());
	EXPECT_EQ(Job.Load(ReadBytes, [](const void *pData, unsigned Size, CImageInfo &Image) {
		EXPECT_NE(pData, nullptr);
		EXPECT_EQ(Size, 4u);
		FillImage(Image);
		return true;
	}),
		EStatus::SUCCESS);
	EXPECT_TRUE(Job.IsCompleted());
	auto Result = Job.GetResult();
	EXPECT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_pData[0], 73);
}

TEST(CountryFlagLoad, ReadFailureCompletesWithoutCallingDecoder)
{
	CJob Job("flag.png", 42, nullptr, 5);
	int Decodes = 0;
	EXPECT_EQ(Job.Load([](void **, unsigned *) { return false; },
			  [&](const void *, unsigned, CImageInfo &) { ++Decodes; return true; }),
		EStatus::READ_FAILED);
	EXPECT_TRUE(Job.IsCompleted());
	auto Result = Job.GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
	EXPECT_EQ(Result.m_FlagIndex, 5u);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Decodes, 0);
}

TEST(CountryFlagLoad, DecodeFailureClearsPartialImageAndPublishesTerminalFailure)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_EQ(Job.Load(ReadBytes, [](const void *, unsigned, CImageInfo &Image) {
		FillImage(Image);
		return false;
	}),
		EStatus::DECODE_FAILED);
	EXPECT_TRUE(Job.IsCompleted());
	auto Result = Job.GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_Width, 0u);
}

TEST(CountryFlagLoad, ReadExceptionAfterOutputAllocationStillCompletes)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_EQ(Job.Load([](void **ppData, unsigned *pSize) -> bool {
		ReadBytes(ppData, pSize);
		throw std::bad_alloc(); }, [](const void *, unsigned, CImageInfo &) { return true; }), EStatus::EXCEPTION);
	EXPECT_TRUE(Job.IsCompleted());
	auto Result = Job.GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
}

TEST(CountryFlagLoad, DecoderExceptionClearsPixelsAndPublishesFailure)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_EQ(Job.Load(ReadBytes, [](const void *, unsigned, CImageInfo &Image) -> bool {
		FillImage(Image);
		throw std::runtime_error("decoder failed after allocation");
	}),
		EStatus::EXCEPTION);
	EXPECT_TRUE(Job.IsCompleted());
	auto Result = Job.GetResult();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
}

TEST(CountryFlagLoad, UnknownDecoderExceptionAlsoPublishesTerminalFailure)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_EQ(Job.Load(ReadBytes, [](const void *, unsigned, CImageInfo &) -> bool { throw 17; }), EStatus::EXCEPTION);
	EXPECT_TRUE(Job.IsCompleted());
	EXPECT_FALSE(Job.GetResult().m_Success);
}

TEST(CountryFlagLoad, ResultCanOnlyTransferPixelsOnce)
{
	CJob Job("flag.png", 42, nullptr);
	Job.Load(ReadBytes, [](const void *, unsigned, CImageInfo &Image) { FillImage(Image); return true; });
	auto First = Job.GetResult();
	auto Second = Job.GetResult();
	ASSERT_NE(First.m_Image.m_pData, nullptr);
	EXPECT_TRUE(First.m_Success);
	EXPECT_FALSE(Second.m_Success);
	EXPECT_EQ(Second.m_Image.m_pData, nullptr);
}

TEST(CountryFlagLoad, QueryBeforeCompletionDoesNotConsumeFutureResult)
{
	CJob Job("flag.png", 42, nullptr);
	EXPECT_FALSE(Job.GetResult().m_Success);
	EXPECT_FALSE(Job.IsCompleted());
	Job.Load(ReadBytes, [](const void *, unsigned, CImageInfo &Image) { FillImage(Image); return true; });
	auto Result = Job.GetResult();
	EXPECT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_CountryCode, 42);
}

TEST(CountryFlagLoad, MovingResultTransfersOwnershipAndClearsSourceSuccess)
{
	CJob::SResult Original;
	Original.m_CountryCode = 42;
	Original.m_FlagIndex = 7;
	Original.m_Success = true;
	FillImage(Original.m_Image);
	auto *pPixels = Original.m_Image.m_pData;
	CJob::SResult Moved(std::move(Original));
	EXPECT_EQ(Moved.m_Image.m_pData, pPixels);
	EXPECT_EQ(Moved.m_CountryCode, 42);
	EXPECT_EQ(Moved.m_FlagIndex, 7u);
	EXPECT_TRUE(Moved.m_Success);
	EXPECT_EQ(Original.m_Image.m_pData, nullptr);
	EXPECT_FALSE(Original.m_Success);
}

TEST(CountryFlagLoad, MoveAssignmentReplacesPreviousImageAndLeavesSourceEmpty)
{
	CJob::SResult First;
	CJob::SResult Second;
	FillImage(First.m_Image);
	FillImage(Second.m_Image);
	Second.m_Success = true;
	Second.m_FlagIndex = 9;
	auto *pPixels = Second.m_Image.m_pData;
	First = std::move(Second);
	EXPECT_EQ(First.m_Image.m_pData, pPixels);
	EXPECT_TRUE(First.m_Success);
	EXPECT_EQ(First.m_FlagIndex, 9u);
	EXPECT_EQ(Second.m_Image.m_pData, nullptr);
	EXPECT_FALSE(Second.m_Success);
	auto &Alias = First;
	First = std::move(Alias);
	EXPECT_EQ(First.m_Image.m_pData, pPixels);
}

TEST(CountryFlagLoadTarget, DuplicateCountryCodeUsesRequestedIndex)
{
	std::vector<CCountryFlags::CCountryFlag> Flags(2);
	Flags[0].m_CountryCode = Flags[1].m_CountryCode = 42;
	CJob::SResult Result;
	Result.m_CountryCode = 42;
	Result.m_FlagIndex = 1;
	auto *pTarget = CCountryFlags::FindFlagLoadTarget(Flags, Result);
	ASSERT_EQ(pTarget, &Flags[1]);
	pTarget->m_Loaded = true;
	EXPECT_FALSE(Flags[0].m_Loaded);
	EXPECT_TRUE(Flags[1].m_Loaded);
}

TEST(CountryFlagLoadTarget, OutOfRangeResultHasNoTarget)
{
	std::vector<CCountryFlags::CCountryFlag> Flags(1);
	Flags[0].m_CountryCode = 42;
	CJob::SResult Result;
	Result.m_CountryCode = 42;
	Result.m_FlagIndex = Flags.size();
	EXPECT_EQ(CCountryFlags::FindFlagLoadTarget(Flags, Result), nullptr);
}

TEST(CountryFlagLoadTarget, CountryCodeMismatchRejectsStaleIndex)
{
	std::vector<CCountryFlags::CCountryFlag> Flags(1);
	Flags[0].m_CountryCode = 43;
	CJob::SResult Result;
	Result.m_CountryCode = 42;
	EXPECT_EQ(CCountryFlags::FindFlagLoadTarget(Flags, Result), nullptr);
}
