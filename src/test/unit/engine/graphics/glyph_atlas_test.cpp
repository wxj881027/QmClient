#include <engine/client/glyph_atlas.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <set>
#include <utility>

namespace
{
	// 仅替换分配边界；分区搜索、切分和提交均由生产图集完成。
	class CFailingMemory
	{
		int m_Remaining = -1;

	public:
		void BeforeAllocate()
		{
			if(m_Remaining == 0)
				throw std::bad_alloc();
			if(m_Remaining > 0)
				--m_Remaining;
		}
		void FailAfter(int SuccessfulAllocations) { m_Remaining = SuccessfulAllocations; }
	};

	template<typename T>
	class CFailingAllocator
	{
		template<typename>
		friend class CFailingAllocator;
		CFailingMemory *m_pMemory;

	public:
		using value_type = T;
		explicit CFailingAllocator(CFailingMemory *pMemory = nullptr) noexcept :
			m_pMemory(pMemory) {}
		template<typename U>
		CFailingAllocator(const CFailingAllocator<U> &Other) noexcept :
			m_pMemory(Other.m_pMemory)
		{
		}

		T *allocate(size_t Count)
		{
			if(m_pMemory != nullptr)
				m_pMemory->BeforeAllocate();
			return std::allocator<T>().allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>().deallocate(pData, Count); }
		template<typename U>
		bool operator==(const CFailingAllocator<U> &Other) const noexcept
		{
			return m_pMemory == Other.m_pMemory;
		}
	};
}

TEST(GlyphAtlas, MappedFreeSectionsRemainUsableAfterLargeSectionsAreConsumed)
{
	CGlyphAtlas<> Atlas;
	Atlas.Clear(64);
	int X = -1, Y = -1;
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	ASSERT_TRUE(Atlas.Add(32, 32, X, Y));
	ASSERT_TRUE(Atlas.Add(32, 32, X, Y));
	EXPECT_EQ(X, 32);
	EXPECT_EQ(Y, 32);
	EXPECT_FALSE(Atlas.Add(8, 8, X, Y));
}

TEST(GlyphAtlas, EveryAllocationFailureBeforeSplitPreservesOriginalFreeSection)
{
	bool Completed = false;
	int FailureCount = 0;
	for(int FailAfter = 0; FailAfter < 32; ++FailAfter)
	{
		SCOPED_TRACE(FailAfter);
		CFailingMemory Memory;
		CGlyphAtlas<CFailingAllocator<std::byte>> Atlas{CFailingAllocator<std::byte>(&Memory)};
		Atlas.Clear(64);
		Memory.FailAfter(FailAfter);
		int X = -1, Y = -1;
		try
		{
			ASSERT_TRUE(Atlas.Add(16, 16, X, Y));
			Completed = true;
			break;
		}
		catch(const std::bad_alloc &)
		{
			++FailureCount;
			EXPECT_EQ(X, -1);
			EXPECT_EQ(Y, -1);
			// 整块仍可取出，不能只剩下已经切出的部分。
			ASSERT_TRUE(Atlas.Add(64, 64, X, Y));
			EXPECT_EQ(X, 0);
			EXPECT_EQ(Y, 0);
			EXPECT_FALSE(Atlas.Add(8, 8, X, Y));
		}
	}
	EXPECT_TRUE(Completed);
	EXPECT_GT(FailureCount, 1);
}

TEST(GlyphAtlas, AllocationFailureWhileSplittingMappedSectionPreservesThatSection)
{
	CFailingMemory Memory;
	CGlyphAtlas<CFailingAllocator<std::byte>> Atlas{CFailingAllocator<std::byte>(&Memory)};
	Atlas.Clear(64);
	int X = -1, Y = -1;
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	ASSERT_TRUE(Atlas.Add(32, 16, X, Y));
	Memory.FailAfter(0);
	X = Y = -1;
	EXPECT_THROW(Atlas.Add(32, 16, X, Y), std::bad_alloc);
	EXPECT_EQ(X, -1);
	EXPECT_EQ(Y, -1);
	ASSERT_TRUE(Atlas.Add(32, 48, X, Y));
	EXPECT_EQ(X, 32);
	EXPECT_EQ(Y, 16);
}

TEST(GlyphAtlas, RepeatedMappedSplitsFillAtlasWithoutOverlappingOccupiedCells)
{
	CGlyphAtlas<> Atlas;
	Atlas.Clear(64);
	std::set<std::pair<int, int>> Occupied;
	int X = -1, Y = -1;
	const auto RecordCells = [&](int Width, int Height) {
		EXPECT_GE(X, 0);
		EXPECT_GE(Y, 0);
		EXPECT_LE(X + Width, 64);
		EXPECT_LE(Y + Height, 64);
		EXPECT_EQ(X % 8, 0);
		EXPECT_EQ(Y % 8, 0);
		for(int CellY = Y; CellY < Y + Height; CellY += 8)
			for(int CellX = X; CellX < X + Width; CellX += 8)
				EXPECT_TRUE(Occupied.emplace(CellX, CellY).second);
	};
	// 分桶只查至少一边相同的空区；条带请求避免把碎片利用率当作分配合同。
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	RecordCells(32, 64);
	for(int Index = 0; Index < 8; ++Index)
	{
		ASSERT_TRUE(Atlas.Add(32, 8, X, Y));
		RecordCells(32, 8);
	}
	EXPECT_EQ(Occupied.size(), 64u);
	EXPECT_FALSE(Atlas.Add(8, 8, X, Y));
}

TEST(GlyphAtlas, GrowthKeepsOldCellsOccupiedAndExposesOnlyNewArea)
{
	CGlyphAtlas<> Atlas;
	Atlas.Clear(64);
	int X = -1, Y = -1;
	ASSERT_TRUE(Atlas.Add(64, 64, X, Y));
	Atlas.IncreaseDimension(128);
	std::set<std::pair<int, int>> Occupied{{0, 0}};
	for(int Index = 0; Index < 3; ++Index)
	{
		ASSERT_TRUE(Atlas.Add(64, 64, X, Y));
		EXPECT_GE(X, 0);
		EXPECT_GE(Y, 0);
		EXPECT_LE(X + 64, 128);
		EXPECT_LE(Y + 64, 128);
		EXPECT_TRUE(Occupied.emplace(X, Y).second);
	}
	EXPECT_FALSE(Atlas.Add(8, 8, X, Y));
}

TEST(GlyphAtlas, InvalidDimensionsLeaveAtlasAndOutputUntouched)
{
	CGlyphAtlas<> Atlas;
	Atlas.Clear(64);
	int X = -1, Y = -1;
	EXPECT_FALSE(Atlas.Add(0, 8, X, Y));
	EXPECT_FALSE(Atlas.Add(8, 0, X, Y));
	EXPECT_FALSE(Atlas.Add(65, 8, X, Y));
	EXPECT_FALSE(Atlas.Add(8, 65, X, Y));
	EXPECT_EQ(X, -1);
	EXPECT_EQ(Y, -1);
	EXPECT_TRUE(Atlas.Add(64, 64, X, Y));
}

TEST(GlyphAtlas, ClearDiscardsPreviousAllocationsAndSize)
{
	CGlyphAtlas<> Atlas;
	Atlas.Clear(64);
	int X = -1, Y = -1;
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	ASSERT_TRUE(Atlas.Add(32, 16, X, Y));
	Atlas.Clear(128);
	ASSERT_TRUE(Atlas.Add(128, 128, X, Y));
	EXPECT_EQ(X, 0);
	EXPECT_EQ(Y, 0);
	EXPECT_FALSE(Atlas.Add(32, 48, X, Y));
}

TEST(GlyphAtlas, ReplacingLargeSectionWithOneCutReusesExistingCapacity)
{
	CFailingMemory Memory;
	CGlyphAtlas<CFailingAllocator<std::byte>> Atlas{CFailingAllocator<std::byte>(&Memory)};
	Atlas.Clear(64);
	Memory.FailAfter(0);
	int X = -1, Y = -1;
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	ASSERT_TRUE(Atlas.Add(32, 64, X, Y));
	EXPECT_EQ(X, 32);
	EXPECT_EQ(Y, 0);
}
