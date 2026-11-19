#include <engine/shared/map.h>

#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
	using TTileBuffer = std::unique_ptr<CTile, decltype(&free)>;
}

TEST(MapTileskip, ExpandsRunsAndResetsCompressionFields)
{
	const std::array<CTile, 2> aSource{{{7, TILEFLAG_XFLIP, 1, 0}, {9, TILEFLAG_YFLIP, 1, 0}}};
	TTileBuffer pTiles(CMap::DecodeTileskip(4, aSource.data(), aSource.size()), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(pTiles.get()[0].m_Index, 7);
	EXPECT_EQ(pTiles.get()[1].m_Index, 7);
	EXPECT_EQ(pTiles.get()[2].m_Index, 9);
	EXPECT_EQ(pTiles.get()[3].m_Index, 9);
	EXPECT_EQ(pTiles.get()[1].m_Flags, TILEFLAG_XFLIP);
	EXPECT_EQ(pTiles.get()[3].m_Flags, TILEFLAG_YFLIP);
	for(size_t i = 0; i < 4; ++i)
	{
		EXPECT_EQ(pTiles.get()[i].m_Skip, 0);
		EXPECT_EQ(pTiles.get()[i].m_MustBe0, 0);
	}
}

TEST(MapTileskip, TruncatedStreamDoesNotRequestItsLargeDeclaredAllocation)
{
	const CTile Source{7, 0, 0, 0};
	int AllocationCalls = 0;
	auto *pTiles = CMap::DecodeTileskip(64 * 1024 * 1024, &Source, 1, [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	});
	EXPECT_EQ(pTiles, nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, NonzeroPaddingInLaterRunRejectsBeforeAllocation)
{
	const std::array<CTile, 2> aSource{{{7, 0, 1, 0}, {9, 0, 1, 1}}};
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(4, aSource.data(), aSource.size(), [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, TrailingRecordsRejectBeforeAllocation)
{
	const std::array<CTile, 2> aSource{{{7, 0, 3, 0}, {9, 0, 0, 0}}};
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(4, aSource.data(), aSource.size(), [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, MissingStreamRejectsBeforeAllocation)
{
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(4, nullptr, 0, [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, EmptyOutputDoesNotAllocate)
{
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(0, nullptr, 0, [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, ByteSizeOverflowRejectsBeforeAllocation)
{
	const CTile Source{7, 0, 0, 0};
	int AllocationCalls = 0;
	const size_t Count = std::numeric_limits<size_t>::max() / sizeof(CTile) + 1;
	EXPECT_EQ(CMap::DecodeTileskip(Count, &Source, 1, [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, AllocationFailureReturnsNoOutput)
{
	const CTile Source{7, 0, 3, 0};
	int AllocationCalls = 0;
	size_t RequestedBytes = 0;
	EXPECT_EQ(CMap::DecodeTileskip(4, &Source, 1, [&](size_t Bytes) -> void * {
		++AllocationCalls;
		RequestedBytes = Bytes;
		return nullptr;
	}),
		nullptr);
	EXPECT_EQ(AllocationCalls, 1);
	EXPECT_EQ(RequestedBytes, 4 * sizeof(CTile));
}

TEST(MapTileskip, ValidStreamCanRetryAfterAllocationFailure)
{
	const CTile Source{7, TILEFLAG_XFLIP, 3, 0};
	int AllocationCalls = 0;
	const auto Allocate = [&](size_t Bytes) -> void * {
		return ++AllocationCalls == 1 ? nullptr : malloc(Bytes);
	};
	EXPECT_EQ(CMap::DecodeTileskip(4, &Source, 1, Allocate), nullptr);
	TTileBuffer pTiles(CMap::DecodeTileskip(4, &Source, 1, Allocate), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(AllocationCalls, 2);
	EXPECT_EQ(pTiles.get()[3].m_Index, 7);
	EXPECT_EQ(pTiles.get()[3].m_Flags, TILEFLAG_XFLIP);
}

TEST(MapTileskip, FinalRunClipsToTheRequestedAllocation)
{
	const CTile Source{7, TILEFLAG_YFLIP, 255, 0};
	size_t RequestedBytes = 0;
	TTileBuffer pTiles(CMap::DecodeTileskip(4, &Source, 1, [&](size_t Bytes) -> void * {
		RequestedBytes = Bytes;
		return malloc(Bytes);
	}),
		&free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(RequestedBytes, 4 * sizeof(CTile));
	EXPECT_EQ(pTiles.get()[3].m_Index, 7);
	EXPECT_EQ(pTiles.get()[3].m_Flags, TILEFLAG_YFLIP);
}

TEST(MapTileskip, MaximumSkipExpandsAllItsTiles)
{
	const CTile Source{7, 255, 255, 0};
	TTileBuffer pTiles(CMap::DecodeTileskip(256, &Source, 1), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(pTiles.get()[255].m_Index, 7);
	EXPECT_EQ(pTiles.get()[255].m_Flags, 255);
	EXPECT_EQ(pTiles.get()[255].m_Skip, 0);
}

TEST(MapTileskip, ByteBudgetRejectsBeforeCallingAllocator)
{
	const CTile Source{7, 0, 3, 0};
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(4, &Source, 1, [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr; }, 4 * sizeof(CTile) - 1), nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, ExactByteBudgetAllowsTheRequestedOutput)
{
	const CTile Source{7, TILEFLAG_XFLIP, 3, 0};
	size_t RequestedBytes = 0;
	TTileBuffer pTiles(CMap::DecodeTileskip(4, &Source, 1, [&](size_t Bytes) -> void * {
		RequestedBytes = Bytes;
		return malloc(Bytes); }, 4 * sizeof(CTile)), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(RequestedBytes, 4 * sizeof(CTile));
	EXPECT_EQ(pTiles.get()[3].m_Index, 7);
}

TEST(MapTileskip, UnalignedByteBudgetDoesNotPadTheAllocation)
{
	const CTile Source{7, 0, 3, 0};
	size_t RequestedBytes = 0;
	TTileBuffer pTiles(CMap::DecodeTileskip(4, &Source, 1, [&](size_t Bytes) -> void * {
		RequestedBytes = Bytes;
		return malloc(Bytes); }, 4 * sizeof(CTile) + sizeof(CTile) - 1), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(RequestedBytes, 4 * sizeof(CTile));
}

TEST(MapTileskip, BudgetFailureCanRetryWithSufficientCapacity)
{
	const CTile Source{7, 0, 3, 0};
	int AllocationCalls = 0;
	const auto Allocate = [&](size_t Bytes) -> void * {
		++AllocationCalls;
		return malloc(Bytes);
	};
	EXPECT_EQ(CMap::DecodeTileskip(4, &Source, 1, Allocate, 0), nullptr);
	EXPECT_EQ(AllocationCalls, 0);
	TTileBuffer pTiles(CMap::DecodeTileskip(4, &Source, 1, Allocate, 4 * sizeof(CTile)), &free);
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(AllocationCalls, 1);
	EXPECT_EQ(pTiles.get()[3].m_Index, 7);
}

TEST(MapTileskip, DatafileByteLimitRejectsValidLargeStreamWithoutGibibyteAllocation)
{
	const size_t DestCount = (size_t)std::numeric_limits<int>::max() / sizeof(CTile) + 1;
	// 输入约 8 MiB；故障分配器永远返回空，测试不会实际分配 GiB 输出。
	const std::vector<CTile> vSource((DestCount + 255) / 256, CTile{7, 0, 255, 0});
	int AllocationCalls = 0;
	EXPECT_EQ(CMap::DecodeTileskip(DestCount, vSource.data(), vSource.size(), [&](size_t) -> void * {
		++AllocationCalls;
		return nullptr; }, std::numeric_limits<int>::max()), nullptr);
	EXPECT_EQ(AllocationCalls, 0);
}

TEST(MapTileskip, FailedExtractionLeavesCallerStorageUntouched)
{
	const std::array<CTile, 2> aSource{{{7, 0, 1, 0}, {9, 0, 1, 1}}};
	std::array<CTile, 4> aDestination{};
	aDestination.fill(CTile{77, 11, 13, 15});
	EXPECT_FALSE(CMap::ExtractTiles(aDestination.data(), aDestination.size(), aSource.data(), aSource.size()));
	for(const auto &Tile : aDestination)
	{
		EXPECT_EQ(Tile.m_Index, 77);
		EXPECT_EQ(Tile.m_Flags, 11);
		EXPECT_EQ(Tile.m_Skip, 13);
		EXPECT_EQ(Tile.m_MustBe0, 15);
	}
}

TEST(MapTileskip, CallerExtractionClipsTheFinalRunWithoutWritingPastTheOutput)
{
	const CTile Source{7, 0, 255, 0};
	std::array<CTile, 5> aDestination{};
	aDestination[4].m_Index = 77;
	ASSERT_TRUE(CMap::ExtractTiles(aDestination.data(), 4, &Source, 1));
	EXPECT_EQ(aDestination[3].m_Index, 7);
	EXPECT_EQ(aDestination[4].m_Index, 77);
}

TEST(MapTileskip, EmptyCallerExtractionRetainsItsExistingResult)
{
	EXPECT_TRUE(CMap::ExtractTiles(nullptr, 0, nullptr, 0));
	const CTile Source{7, 0, 0, 0};
	EXPECT_FALSE(CMap::ExtractTiles(nullptr, 0, &Source, 1));
}
