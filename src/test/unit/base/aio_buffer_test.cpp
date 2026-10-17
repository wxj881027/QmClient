#include <base/aio_buffer.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace
{
	std::string Read(CAsyncIOBuffer &Buffer, unsigned Size = 64)
	{
		std::string Result(Size, '\0');
		Result.resize(Buffer.Read(Result.data(), Size));
		return Result;
	}

	void *FailAllocation(size_t)
	{
		return nullptr;
	}
}

TEST(AsyncBuffer, EmptyReadAndZeroWriteDoNotAccessPointers)
{
	CAsyncIOBuffer Buffer;
	ASSERT_TRUE(Buffer.IsValid());
	EXPECT_TRUE(Buffer.Empty());
	EXPECT_EQ(Buffer.Read(nullptr, 0), 0u);
	EXPECT_EQ(Buffer.Read(nullptr, 64), 0u);
	EXPECT_TRUE(Buffer.Append(nullptr, 0));
	EXPECT_TRUE(Buffer.Empty());
}

TEST(AsyncBuffer, PartialReadsKeepFifoOrder)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abc", 3));
	ASSERT_TRUE(Buffer.Append("def", 3));
	EXPECT_EQ(Read(Buffer, 2), "ab");
	EXPECT_EQ(Buffer.Size(), 4u);
	EXPECT_EQ(Read(Buffer), "cdef");
	EXPECT_TRUE(Buffer.Empty());
}

TEST(AsyncBuffer, WrappedWritesAndReadsKeepFifoOrder)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abcdef", 6));
	ASSERT_EQ(Read(Buffer, 5), "abcde");
	ASSERT_TRUE(Buffer.Append("ghijkl", 6));
	EXPECT_EQ(Read(Buffer, 4), "fghi");
	EXPECT_EQ(Read(Buffer), "jkl");
	EXPECT_TRUE(Buffer.Empty());
}

TEST(AsyncBuffer, GrowthPreservesBothWrappedSpans)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abcdef", 6));
	ASSERT_EQ(Read(Buffer, 5), "abcde");
	ASSERT_TRUE(Buffer.Append("ghijk", 5));
	ASSERT_TRUE(Buffer.Append("lmnopqrst", 9));
	EXPECT_EQ(Read(Buffer), "fghijklmnopqrst");
}

TEST(AsyncBuffer, FullCapacityWriteRemainsReadable)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abcdefgh", 8));
	EXPECT_FALSE(Buffer.Empty());
	EXPECT_EQ(Buffer.Size(), 8u);
	EXPECT_EQ(Read(Buffer), "abcdefgh");
}

TEST(AsyncBuffer, GrowthFailurePreservesWrappedQueueAndAllowsRetry)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abcdef", 6));
	ASSERT_EQ(Read(Buffer, 5), "abcde");
	ASSERT_TRUE(Buffer.Append("ghijk", 5));
	EXPECT_FALSE(Buffer.Append("lost", 4, FailAllocation));
	EXPECT_EQ(Buffer.Size(), 6u);
	ASSERT_TRUE(Buffer.Append("lmnop", 5));
	EXPECT_EQ(Read(Buffer), "fghijklmnop");
}

TEST(AsyncBuffer, InitialAllocationFailureLeavesEmptyInvalidBuffer)
{
	CAsyncIOBuffer Buffer(8, FailAllocation);
	EXPECT_FALSE(Buffer.IsValid());
	EXPECT_TRUE(Buffer.Empty());
	EXPECT_EQ(Buffer.Read(nullptr, 64), 0u);
	EXPECT_FALSE(Buffer.Append("abc", 3));
}

TEST(AsyncBuffer, UnrepresentableWriteDoesNotReadInputOrChangeQueue)
{
	CAsyncIOBuffer Buffer(8);
	ASSERT_TRUE(Buffer.Append("abc", 3));
	EXPECT_FALSE(Buffer.Append(nullptr, std::numeric_limits<unsigned>::max() - 3));
	EXPECT_EQ(Read(Buffer), "abc");
	EXPECT_FALSE(Buffer.Append(nullptr, std::numeric_limits<unsigned>::max()));
	EXPECT_TRUE(Buffer.Append("recovered", 9));
	EXPECT_EQ(Read(Buffer), "recovered");
}

TEST(AsyncBuffer, GrowthAboveHalfTheIntegerRangeDoesNotWrapOrLoop)
{
	CAsyncIOBuffer Buffer(8);
	EXPECT_FALSE(Buffer.Append(nullptr, std::numeric_limits<unsigned>::max() - 1, [](size_t Size) -> void * {
		EXPECT_EQ(Size, std::numeric_limits<unsigned>::max());
		return nullptr;
	}));
	EXPECT_TRUE(Buffer.Empty());
	ASSERT_TRUE(Buffer.Append("ok", 2));
	EXPECT_EQ(Read(Buffer), "ok");
}
