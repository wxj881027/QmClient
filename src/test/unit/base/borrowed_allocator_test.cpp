#include <base/borrowed_allocator.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class CTrackingResource : public IAllocationResource
	{
		struct SBlock
		{
			void *m_pData = nullptr;
			size_t m_Size = 0;
			size_t m_Alignment = 0;
		};
		std::array<SBlock, 64> m_aBlocks{};

	public:
		int m_Allocations = 0;
		int m_Live = 0;
		bool m_Reject = false;
		bool m_Null = false;
		bool m_Unknown = false;
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			++m_Allocations;
			if(m_Unknown)
				throw 7;
			if(m_Reject)
				throw std::bad_alloc();
			if(m_Null)
				return nullptr;
			for(auto &Block : m_aBlocks)
			{
				if(Block.m_pData)
					continue;
				Block = {AllocateDefault(Bytes, Alignment), Bytes, Alignment};
				++m_Live;
				return Block.m_pData;
			}
			throw std::bad_alloc();
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			for(auto &Block : m_aBlocks)
			{
				if(Block.m_pData != pData)
					continue;
				EXPECT_EQ(Block.m_Size, Bytes);
				EXPECT_EQ(Block.m_Alignment, Alignment);
				DeallocateDefault(pData, Block.m_Size, Block.m_Alignment);
				Block = {};
				--m_Live;
				return;
			}
			ADD_FAILURE() << "释放指针不属于该内存域";
		}
	};

	template<typename T>
	using TVector = std::vector<T, CBorrowedAllocator<T>>;
	using TString = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
	struct alignas(256) SAligned
	{
		int m_Value = 0;
	};
}

TEST(BorrowedAllocator, DefaultVectorStoresValuesWithoutExternalResource)
{
	TVector<int> Values;
	Values.assign({3, 5, 7});
	EXPECT_EQ(Values.get_allocator().Resource(), nullptr);
	EXPECT_EQ(Values.size(), 3u);
	EXPECT_EQ(Values[2], 7);
}

TEST(BorrowedAllocator, InjectedResourceReceivesPairedSizeAlignmentAndRelease)
{
	CTrackingResource Memory;
	{
		TVector<int> Values(&Memory);
		Values.assign({1, 2, 3});
		Values.reserve(Values.capacity() + 1);
		EXPECT_EQ(Values.get_allocator().Resource(), &Memory);
		EXPECT_GT(Memory.m_Live, 0);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, DefaultAllocatorSupportsOverAlignedValues)
{
	TVector<SAligned> Values(2);
	Values[1].m_Value = 41;
	EXPECT_EQ(reinterpret_cast<uintptr_t>(Values.data()) % alignof(SAligned), 0u);
	EXPECT_EQ(Values[1].m_Value, 41);
}

TEST(BorrowedAllocator, InjectedDefaultHeapSupportsOverAlignedValues)
{
	CTrackingResource Memory;
	{
		TVector<SAligned> Values(&Memory);
		Values.resize(2);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(Values.data()) % alignof(SAligned), 0u);
		EXPECT_GT(Memory.m_Live, 0);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, CountMultiplicationOverflowDoesNotCallResource)
{
	CTrackingResource Memory;
	CBorrowedAllocator<uint64_t> Allocator(&Memory);
	EXPECT_THROW(Allocator.allocate(std::numeric_limits<size_t>::max()), std::bad_array_new_length);
	EXPECT_EQ(Memory.m_Allocations, 0);
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, NullResourceResultBecomesAllocationFailure)
{
	CTrackingResource Memory;
	TVector<int> Values(&Memory);
	Memory.m_Null = true;
	EXPECT_THROW(Values.push_back(5), std::bad_alloc);
	EXPECT_TRUE(Values.empty());
	Memory.m_Null = false;
	Values.push_back(7);
	EXPECT_EQ(Values[0], 7);
}

TEST(BorrowedAllocator, RejectedVectorGrowthRetainsBytesAndCanRetry)
{
	CTrackingResource Memory;
	{
		TVector<int> Values({2, 3}, &Memory);
		const auto *pOld = Values.data();
		const auto Capacity = Values.capacity();
		Memory.m_Reject = true;
		EXPECT_THROW(Values.reserve(Capacity + 1), std::bad_alloc);
		EXPECT_EQ(Values.data(), pOld);
		EXPECT_EQ(Values.size(), 2u);
		EXPECT_EQ(Values[1], 3);
		Memory.m_Reject = false;
		Values.reserve(Capacity + 1);
		EXPECT_EQ(Values[0], 2);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, UnknownResourceExceptionPreservesOriginalContainer)
{
	CTrackingResource Memory;
	TVector<int> Values(&Memory);
	const int Live = Memory.m_Live;
	Memory.m_Unknown = true;
	try
	{
		Values.push_back(1);
		FAIL() << "预期分配异常";
	}
	catch(int Code)
	{
		EXPECT_EQ(Code, 7);
	}
	EXPECT_TRUE(Values.empty());
	EXPECT_EQ(Memory.m_Live, Live);
}

TEST(BorrowedAllocator, ReboundAllocatorKeepsResourceIdentity)
{
	CTrackingResource First, Second;
	CBorrowedAllocator<int> Ints(&First);
	std::allocator_traits<decltype(Ints)>::rebind_alloc<SAligned> Aligned(Ints);
	EXPECT_EQ(Aligned.Resource(), &First);
	EXPECT_TRUE(Aligned == Ints);
	EXPECT_FALSE(Aligned == CBorrowedAllocator<int>(&Second));
	EXPECT_FALSE(Ints == CBorrowedAllocator<int>());
}

TEST(BorrowedAllocator, MapNodeRebindUsesAndReleasesSameResource)
{
	CTrackingResource Memory;
	{
		using TMap = std::map<int, std::string, std::less<int>, CBorrowedAllocator<std::pair<const int, std::string>>>;
		TMap Map(&Memory);
		Map.emplace(3, "three");
		Map.emplace(1, "one");
		EXPECT_EQ(Map.begin()->first, 1);
		EXPECT_EQ(Map.at(3), "three");
		EXPECT_GT(Memory.m_Live, 0);
		Map.erase(1);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, CopyConstructionUsesDefaultHeapWithoutBorrowingSourceResource)
{
	CTrackingResource Memory;
	TVector<int> Copy;
	{
		TVector<int> Original({11, 13}, &Memory);
		const int Calls = Memory.m_Allocations;
		TVector<int> Candidate(Original);
		EXPECT_EQ(Candidate.get_allocator().Resource(), nullptr);
		EXPECT_EQ(Memory.m_Allocations, Calls);
		Copy = std::move(Candidate);
	}
	EXPECT_EQ(Memory.m_Live, 0);
	EXPECT_EQ(Copy[1], 13);
}

TEST(BorrowedAllocator, CopyAssignmentRetainsDestinationResource)
{
	CTrackingResource First, Second;
	{
		TVector<int> Source({11, 13}, &First);
		TVector<int> Destination(&Second);
		Destination = Source;
		EXPECT_EQ(Destination.get_allocator().Resource(), &Second);
		EXPECT_EQ(Destination[0], 11);
		EXPECT_GT(Second.m_Live, 0);
	}
	EXPECT_EQ(First.m_Live, 0);
	EXPECT_EQ(Second.m_Live, 0);
}

TEST(BorrowedAllocator, MoveConstructionKeepsBorrowAndTransfersStorage)
{
	CTrackingResource Memory;
	{
		TVector<int> Source({17, 19}, &Memory);
		const auto *pOld = Source.data();
		TVector<int> Destination(std::move(Source));
		EXPECT_EQ(Destination.get_allocator().Resource(), &Memory);
		EXPECT_EQ(Destination.data(), pOld);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, MoveAssignmentAcrossResourcesDoesNotStealForeignAllocation)
{
	CTrackingResource First, Second;
	{
		TVector<int> Source({23, 29}, &First);
		const auto *pOld = Source.data();
		TVector<int> Destination(&Second);
		Destination = std::move(Source);
		EXPECT_EQ(Destination.get_allocator().Resource(), &Second);
		EXPECT_NE(Destination.data(), pOld);
		EXPECT_EQ(Destination[1], 29);
	}
	EXPECT_EQ(First.m_Live, 0);
	EXPECT_EQ(Second.m_Live, 0);
}

TEST(BorrowedAllocator, SwapWithinResourceTransfersStorageWithoutAllocating)
{
	CTrackingResource Memory;
	{
		TVector<int> First({31}, &Memory), Second({37, 41}, &Memory);
		const int Calls = Memory.m_Allocations;
		Memory.m_Reject = true;
		First.swap(Second);
		EXPECT_EQ(First[1], 41);
		EXPECT_EQ(Second[0], 31);
		EXPECT_EQ(First.get_allocator().Resource(), &Memory);
		EXPECT_EQ(Memory.m_Allocations, Calls);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST(BorrowedAllocator, StringMoveKeepsPreparedTextAndResource)
{
	static_assert(std::is_nothrow_move_constructible_v<TString>);
	CTrackingResource Memory;
	{
		TString Text(4096, 'x', &Memory);
		const auto *pOld = Text.data();
		TString Moved(std::move(Text));
		EXPECT_EQ(Moved.get_allocator().Resource(), &Memory);
		EXPECT_EQ(Moved.size(), 4096u);
		EXPECT_EQ(Moved.back(), 'x');
		EXPECT_EQ(Moved.data(), pOld);
	}
	EXPECT_EQ(Memory.m_Live, 0);
}
