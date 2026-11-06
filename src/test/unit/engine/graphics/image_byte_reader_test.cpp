#include <engine/gfx/image_loader.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

TEST(ImageByteReader, ConsecutiveReadsReachExactEndWithoutError)
{
	const std::array<uint8_t, 3> aSource{2, 4, 8};
	CByteBufferReader Reader(aSource.data(), aSource.size());
	std::array<uint8_t, 3> aOutput{};
	ASSERT_TRUE(Reader.Read(aOutput.data(), 1));
	ASSERT_TRUE(Reader.Read(aOutput.data() + 1, 2));
	EXPECT_EQ(aOutput, aSource);
	EXPECT_EQ(Reader.Size(), aSource.size());
	EXPECT_FALSE(Reader.Error());
}

TEST(ImageByteReader, OverflowingLengthAfterPartialReadIsRejectedWithoutCopy)
{
	const std::array<uint8_t, 2> aSource{2, 4};
	CByteBufferReader Reader(aSource.data(), aSource.size());
	uint8_t Value = 99;
	ASSERT_TRUE(Reader.Read(&Value, 1));
	Value = 99;
	EXPECT_FALSE(Reader.Read(&Value, std::numeric_limits<size_t>::max()));
	EXPECT_EQ(Value, 99);
	EXPECT_TRUE(Reader.Error());
}

TEST(ImageByteReader, OrdinaryOutOfBoundsReadSetsStickyError)
{
	const std::array<uint8_t, 2> aSource{2, 4};
	CByteBufferReader Reader(aSource.data(), aSource.size());
	std::array<uint8_t, 3> aOutput{99, 99, 99};
	EXPECT_FALSE(Reader.Read(aOutput.data(), aOutput.size()));
	EXPECT_EQ(aOutput, (std::array<uint8_t, 3>{99, 99, 99}));
	EXPECT_TRUE(Reader.Error());
	EXPECT_FALSE(Reader.Read(aOutput.data(), 1));
	EXPECT_EQ(aOutput[0], 99);
}

TEST(ImageByteReader, EmptyAndExhaustedReaderAllowZeroByteReadsWithoutPointers)
{
	CByteBufferReader Empty(nullptr, 0);
	EXPECT_TRUE(Empty.Read(nullptr, 0));
	EXPECT_FALSE(Empty.Error());
	const uint8_t Source = 7;
	uint8_t Value = 0;
	CByteBufferReader Reader(&Source, 1);
	ASSERT_TRUE(Reader.Read(&Value, 1));
	EXPECT_TRUE(Reader.Read(nullptr, 0));
	EXPECT_FALSE(Reader.Error());
}

TEST(ImageByteReader, ZeroByteReadDoesNotClearPreviousError)
{
	CByteBufferReader Reader(nullptr, 0);
	uint8_t Value = 0;
	ASSERT_FALSE(Reader.Read(&Value, 1));
	EXPECT_FALSE(Reader.Read(nullptr, 0));
	EXPECT_TRUE(Reader.Error());
}
