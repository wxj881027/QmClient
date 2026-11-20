#include <engine/client/graphics_vertex_budget.h>

#include <gtest/gtest.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	template<int Capacity = 8>
	struct CTrace
	{
		int m_Pending = 0;
		int m_VerticesPerItem = 2;
		std::vector<std::pair<size_t, int>> m_vWrites;
		std::vector<int> m_vFlushes;
		void Flush()
		{
			m_vFlushes.push_back(m_Pending);
			m_Pending = 0;
		}
		void Write(size_t Offset, int Batch)
		{
			m_vWrites.emplace_back(Offset, Batch);
			EXPECT_TRUE(CommitGraphicsVertices<Capacity>(Batch * m_VerticesPerItem, m_Pending, [&] { Flush(); }));
		}
		bool Append(size_t Count)
		{
			return WriteGraphicsVertexBatches<Capacity>(Count, m_VerticesPerItem, m_Pending, [&](size_t Offset, int Batch) { Write(Offset, Batch); }, [&] { Flush(); });
		}
	};
}

TEST(GraphicsVertexCommit, SmallWritesKeepTheExistingEarlyFlushRule)
{
	int Pending = 2;
	std::vector<int> vFlushes;
	const auto Flush = [&] {
		vFlushes.push_back(Pending);
		Pending = 0;
	};
	EXPECT_TRUE(CommitGraphicsVertices<8>(2, Pending, Flush));
	EXPECT_EQ(Pending, 4);
	EXPECT_TRUE(vFlushes.empty());
	EXPECT_TRUE(CommitGraphicsVertices<8>(2, Pending, Flush));
	EXPECT_EQ(Pending, 0);
	EXPECT_EQ(vFlushes, (std::vector<int>{6}));
}

TEST(GraphicsVertexCommit, InvalidCountsCannotChangeThePendingState)
{
	int Pending = 2, Calls = 0;
	EXPECT_FALSE(CommitGraphicsVertices<8>(-1, Pending, [&] { ++Calls; }));
	EXPECT_FALSE(CommitGraphicsVertices<8>(7, Pending, [&] { ++Calls; }));
	EXPECT_EQ(Pending, 2);
	Pending = -1;
	EXPECT_FALSE(CommitGraphicsVertices<8>(1, Pending, [&] { ++Calls; }));
	Pending = 9;
	EXPECT_FALSE(CommitGraphicsVertices<8>(1, Pending, [&] { ++Calls; }));
	EXPECT_EQ(Calls, 0);
}

TEST(GraphicsVertexCommit, EmptyCommitDoesNotFlushAnOrdinaryPartialBatch)
{
	int Pending = 2, Calls = 0;
	EXPECT_TRUE(CommitGraphicsVertices<8>(0, Pending, [&] { ++Calls; }));
	EXPECT_EQ(Pending, 2);
	EXPECT_EQ(Calls, 0);
}

TEST(GraphicsVertexBatches, EmptyInputDoesNotCallEitherBoundary)
{
	CTrace<> Trace;
	Trace.m_Pending = 8;
	EXPECT_TRUE(Trace.Append(0));
	EXPECT_EQ(Trace.m_Pending, 8);
	EXPECT_TRUE(Trace.m_vWrites.empty());
	EXPECT_TRUE(Trace.m_vFlushes.empty());
}

TEST(GraphicsVertexBatches, SmallInputKeepsOneOriginalWrite)
{
	CTrace<> Trace;
	Trace.m_Pending = 2;
	ASSERT_TRUE(Trace.Append(1));
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 1}}));
	EXPECT_EQ(Trace.m_Pending, 4);
	EXPECT_TRUE(Trace.m_vFlushes.empty());
}

TEST(GraphicsVertexBatches, FullPendingBufferIsFlushedBeforeAnyNewWrite)
{
	CTrace<> Trace;
	Trace.m_Pending = 8;
	bool SawEmptyBeforeWrite = false;
	ASSERT_TRUE(WriteGraphicsVertexBatches<8>(1, 2, Trace.m_Pending, [&](size_t Offset, int Batch) {
		SawEmptyBeforeWrite = Trace.m_Pending == 0;
		Trace.Write(Offset, Batch); }, [&] { Trace.Flush(); }));
	EXPECT_TRUE(SawEmptyBeforeWrite);
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{8}));
	EXPECT_EQ(Trace.m_Pending, 2);
}

TEST(GraphicsVertexBatches, FragmentTooSmallForOneWholeItemIsFlushedFirst)
{
	CTrace<> Trace;
	Trace.m_Pending = 7;
	ASSERT_TRUE(Trace.Append(1));
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{7}));
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 1}}));
	EXPECT_EQ(Trace.m_Pending, 2);
}

TEST(GraphicsVertexBatches, RemainingSpaceAndNextBatchRetainTheCompleteInputRange)
{
	CTrace<> Trace;
	Trace.m_Pending = 4;
	ASSERT_TRUE(Trace.Append(3));
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 2}, {2, 1}}));
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{8}));
	EXPECT_EQ(Trace.m_Pending, 2);
}

TEST(GraphicsVertexBatches, TriangleQuadBatchesLeaveTheIncompleteVertexSlotUnused)
{
	CTrace<13> Trace;
	Trace.m_VerticesPerItem = 6;
	ASSERT_TRUE(Trace.Append(5));
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 2}, {2, 2}, {4, 1}}));
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{12, 12}));
	EXPECT_EQ(Trace.m_Pending, 6);
}

TEST(GraphicsVertexBatches, QuadInputKeepsEveryItemAcrossSeveralFullBuffers)
{
	CTrace<> Trace;
	Trace.m_VerticesPerItem = 4;
	ASSERT_TRUE(Trace.Append(5));
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 2}, {2, 2}, {4, 1}}));
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{8, 8, 4}));
	EXPECT_EQ(Trace.m_Pending, 0);
}

TEST(GraphicsVertexBatches, FlushFailureStopsBeforeOverwritingTheFullBuffer)
{
	int Pending = 8, Writes = 0;
	EXPECT_THROW(WriteGraphicsVertexBatches<8>(2, 2, Pending, [&](size_t, int) { ++Writes; }, [] { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Writes, 0);
	EXPECT_EQ(Pending, 8);
}

TEST(GraphicsVertexBatches, FirstWriteFailureDoesNotAttemptLaterInput)
{
	int Pending = 0, Writes = 0, Flushes = 0;
	EXPECT_THROW(WriteGraphicsVertexBatches<8>(9, 2, Pending, [&](size_t, int) {
		++Writes;
		throw std::runtime_error("write stopped"); }, [&] { ++Flushes; }), std::runtime_error);
	EXPECT_EQ(Writes, 1);
	EXPECT_EQ(Flushes, 0);
}

TEST(GraphicsVertexBatches, LaterWriteFailureDoesNotReplayTheAcceptedPrefix)
{
	CTrace<> Trace;
	int Writes = 0;
	EXPECT_THROW(WriteGraphicsVertexBatches<8>(9, 2, Trace.m_Pending, [&](size_t Offset, int Batch) {
		if(++Writes == 2)
			throw std::bad_alloc();
		Trace.Write(Offset, Batch); }, [&] { Trace.Flush(); }), std::bad_alloc);
	EXPECT_EQ(Writes, 2);
	EXPECT_EQ(Trace.m_vWrites, (std::vector<std::pair<size_t, int>>{{0, 4}}));
	EXPECT_EQ(Trace.m_vFlushes, (std::vector<int>{8}));
}

TEST(GraphicsVertexBatches, FlushThatRetainsPendingDataCannotCauseAnUnboundedLoop)
{
	int Pending = 8, Writes = 0, Flushes = 0;
	EXPECT_FALSE(WriteGraphicsVertexBatches<8>(9, 2, Pending, [&](size_t, int) { ++Writes; }, [&] { ++Flushes; }));
	EXPECT_EQ(Flushes, 1);
	EXPECT_EQ(Writes, 0);
}

TEST(GraphicsVertexBatches, InvalidStateOrPrimitiveSizeDoesNotReachAnyBoundary)
{
	for(const auto [PendingValue, Vertices] : {std::pair{-1, 2}, std::pair{9, 2}, std::pair{0, 0}, std::pair{0, -2}, std::pair{0, 9}})
	{
		int Pending = PendingValue, Calls = 0;
		EXPECT_FALSE(WriteGraphicsVertexBatches<8>(1, Vertices, Pending, [&](size_t, int) { ++Calls; }, [&] { ++Calls; }));
		EXPECT_EQ(Calls, 0);
	}
}

TEST(GraphicsVertexBatches, BrokenWriterStateStopsBeforeTheNextBatch)
{
	int Pending = 0, Calls = 0;
	EXPECT_FALSE(WriteGraphicsVertexBatches<8>(9, 2, Pending, [&](size_t, int) {
		++Calls;
		Pending = 9; }, [] {}));
	EXPECT_EQ(Calls, 1);
}

TEST(GraphicsVertexBatches, SizeTInputIsLimitedBeforeNarrowingTheFirstBatch)
{
	int Pending = 0, FirstBatch = 0;
	size_t FirstOffset = 1;
	EXPECT_THROW(WriteGraphicsVertexBatches<8>(std::numeric_limits<size_t>::max(), 2, Pending, [&](size_t Offset, int Batch) {
		FirstOffset = Offset;
		FirstBatch = Batch;
		throw std::bad_alloc(); }, [] {}), std::bad_alloc);
	EXPECT_EQ(FirstOffset, 0u);
	EXPECT_EQ(FirstBatch, 4);
}
