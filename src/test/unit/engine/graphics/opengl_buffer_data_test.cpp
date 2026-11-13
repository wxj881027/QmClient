#include <engine/client/backend/opengl/buffer_data.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <new>
#include <utility>

TEST(OpenGLBufferData, NewStorageHasNoReadableBytes)
{
	COpenGLBufferData Data;
	EXPECT_EQ(Data.Data(), nullptr);
	EXPECT_EQ(Data.Size(), 0U);
	EXPECT_TRUE(Data.Contains(0, 0));
	EXPECT_FALSE(Data.Contains(0, 1));
	EXPECT_FALSE(Data.Contains(1, 0));
}

TEST(OpenGLBufferData, AssignmentOwnsItsInputCopy)
{
	std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(aSource.size(), aSource.data()));
	aSource[0] = 9;
	EXPECT_EQ(Data.Size(), 4U);
	EXPECT_NE(Data.Data(), aSource.data());
	EXPECT_EQ(Data.Data()[0], 1);
	EXPECT_EQ(Data.Data()[3], 4);
}

TEST(OpenGLBufferData, FailedAllocationPreservesThePreviousStorage)
{
	const std::array<uint8_t, 3> aSource{2, 4, 6};
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(aSource.size(), aSource.data()));
	const auto *pPrevious = Data.Data();
	int Calls = 0;
	EXPECT_FALSE(Data.Assign(8, nullptr, [&](size_t Size) -> void * {
		++Calls;
		EXPECT_EQ(Size, 8U);
		return nullptr;
	}));
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(Data.Data(), pPrevious);
	EXPECT_EQ(Data.Size(), aSource.size());
	EXPECT_EQ(Data.Data()[2], 6);
}

TEST(OpenGLBufferData, ThrowingAllocationPreservesThePreviousStorage)
{
	const uint8_t Value = 7;
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(1, &Value));
	EXPECT_FALSE(Data.Assign(8, nullptr, [](size_t) -> void * { throw std::bad_alloc(); }));
	ASSERT_EQ(Data.Size(), 1U);
	EXPECT_EQ(Data.Data()[0], Value);
}

TEST(OpenGLBufferData, ZeroSizeReplacesThePreviousStorageWithoutAllocating)
{
	const uint8_t Value = 7;
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(1, &Value));
	int Calls = 0;
	EXPECT_TRUE(Data.Assign(0, nullptr, [&](size_t) -> void * {
		++Calls;
		return nullptr;
	}));
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Data.Size(), 0U);
	EXPECT_EQ(Data.Data(), nullptr);
}

TEST(OpenGLBufferData, OversizedRequestIsRejectedBeforeAllocation)
{
	COpenGLBufferData Data;
	int Calls = 0;
	EXPECT_FALSE(Data.Assign(std::numeric_limits<size_t>::max(), nullptr, [&](size_t) -> void * {
		++Calls;
		return nullptr;
	}));
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Data.Size(), 0U);
}

TEST(OpenGLBufferData, ReservedStorageAcceptsSuccessiveUploadChunks)
{
	const std::array<uint8_t, 4> aFirst{1, 2, 3, 4};
	const std::array<uint8_t, 4> aSecond{5, 6, 7, 8};
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(8, nullptr));
	ASSERT_TRUE(Data.Write(0, aFirst.size(), aFirst.data()));
	ASSERT_TRUE(Data.Write(4, aSecond.size(), aSecond.data()));
	EXPECT_EQ(Data.Data()[0], 1);
	EXPECT_EQ(Data.Data()[3], 4);
	EXPECT_EQ(Data.Data()[4], 5);
	EXPECT_EQ(Data.Data()[7], 8);
}

TEST(OpenGLBufferData, InvalidWriteLeavesBytesUnchanged)
{
	const std::array<uint8_t, 3> aSource{1, 2, 3};
	const uint8_t Replacement = 9;
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(aSource.size(), aSource.data()));
	EXPECT_FALSE(Data.Write(3, 1, &Replacement));
	EXPECT_FALSE(Data.Write(1, std::numeric_limits<size_t>::max(), &Replacement));
	EXPECT_FALSE(Data.Write(std::numeric_limits<size_t>::max(), 1, &Replacement));
	EXPECT_FALSE(Data.Write(0, 1, nullptr));
	EXPECT_EQ(Data.Data()[0], 1);
	EXPECT_EQ(Data.Data()[2], 3);
}

TEST(OpenGLBufferData, LastByteAndEmptyEndAreValidWriteRanges)
{
	const std::array<uint8_t, 3> aSource{1, 2, 3};
	const uint8_t Replacement = 9;
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(aSource.size(), aSource.data()));
	EXPECT_TRUE(Data.Write(2, 1, &Replacement));
	EXPECT_TRUE(Data.Write(3, 0, nullptr));
	EXPECT_EQ(Data.Data()[0], 1);
	EXPECT_EQ(Data.Data()[2], 9);
}

TEST(OpenGLBufferData, CopyUsesTheRequestedSourceAndDestinationRanges)
{
	const std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	const std::array<uint8_t, 4> aDestination{5, 6, 7, 8};
	COpenGLBufferData Source, Destination;
	ASSERT_TRUE(Source.Assign(aSource.size(), aSource.data()));
	ASSERT_TRUE(Destination.Assign(aDestination.size(), aDestination.data()));
	ASSERT_TRUE(Destination.CopyFrom(Source, 1, 2, 2));
	EXPECT_EQ(Destination.Data()[0], 5);
	EXPECT_EQ(Destination.Data()[1], 6);
	EXPECT_EQ(Destination.Data()[2], 2);
	EXPECT_EQ(Destination.Data()[3], 3);
	EXPECT_EQ(Source.Data()[1], 2);
}

TEST(OpenGLBufferData, CopyRejectsInvalidSourceOrDestinationWithoutMutation)
{
	const std::array<uint8_t, 2> aSource{1, 2};
	COpenGLBufferData Source, Destination;
	ASSERT_TRUE(Source.Assign(aSource.size(), aSource.data()));
	ASSERT_TRUE(Destination.Assign(aSource.size(), aSource.data()));
	EXPECT_FALSE(Destination.CopyFrom(Source, 1, 0, 2));
	EXPECT_FALSE(Destination.CopyFrom(Source, 0, 1, 2));
	EXPECT_FALSE(Destination.CopyFrom(Source, 0, 0, std::numeric_limits<size_t>::max()));
	EXPECT_EQ(Destination.Data()[0], 1);
	EXPECT_EQ(Destination.Data()[1], 2);
}

TEST(OpenGLBufferData, SelfCopySupportsOverlappingRanges)
{
	const std::array<uint8_t, 5> aSource{1, 2, 3, 4, 5};
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(aSource.size(), aSource.data()));
	ASSERT_TRUE(Data.CopyFrom(Data, 0, 1, 4));
	EXPECT_EQ(Data.Data()[0], 1);
	EXPECT_EQ(Data.Data()[1], 1);
	EXPECT_EQ(Data.Data()[2], 2);
	EXPECT_EQ(Data.Data()[4], 4);
}

TEST(OpenGLBufferData, MoveTransfersStorageAndEmptiesTheSource)
{
	const uint8_t Value = 7;
	COpenGLBufferData Source;
	ASSERT_TRUE(Source.Assign(1, &Value));
	const auto *pData = Source.Data();
	COpenGLBufferData Destination(std::move(Source));
	EXPECT_EQ(Source.Size(), 0U);
	EXPECT_EQ(Source.Data(), nullptr);
	EXPECT_EQ(Destination.Data(), pData);
	EXPECT_EQ(Destination.Data()[0], Value);
	Source = std::move(Destination);
	EXPECT_EQ(Destination.Size(), 0U);
	EXPECT_EQ(Destination.Data(), nullptr);
	EXPECT_EQ(Source.Data(), pData);
}

TEST(OpenGLBufferData, ResetAndFailedAllocationAllowLaterReuse)
{
	const uint8_t Value = 7;
	COpenGLBufferData Data;
	ASSERT_TRUE(Data.Assign(1, &Value));
	Data.Reset();
	Data.Reset();
	EXPECT_FALSE(Data.Write(0, 1, &Value));
	EXPECT_FALSE(Data.Assign(1, &Value, [](size_t) -> void * { return nullptr; }));
	ASSERT_TRUE(Data.Assign(1, &Value));
	EXPECT_EQ(Data.Data()[0], Value);
}
