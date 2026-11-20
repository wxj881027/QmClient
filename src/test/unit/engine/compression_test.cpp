// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/shared/compression.h>

#include <gtest/gtest.h>

#include <array>

static const int DATA[] = {0, 1, -1, 32, 64, 256, -512, 12345, -123456, 1234567, 12345678, 123456789, 2147483647, (-2147483647 - 1)};
static const int NUM = std::size(DATA);
static const int SIZES[NUM] = {1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5};

TEST(CVariableInt, RoundtripPackUnpack)
{
	for(int i = 0; i < NUM; i++)
	{
		unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED];
		int Result;
		EXPECT_EQ(CVariableInt::Pack(aPacked, DATA[i], sizeof(aPacked)) - aPacked, (ptrdiff_t)SIZES[i]);
		EXPECT_EQ(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked, (ptrdiff_t)SIZES[i]);
		EXPECT_EQ(Result, DATA[i]);
	}
}

TEST(CVariableInt, UnpackInvalid)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED];
	for(auto &Byte : aPacked)
		Byte = 0xFF;

	int Result;
	EXPECT_EQ(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked, (ptrdiff_t)CVariableInt::MAX_BYTES_PACKED);
	EXPECT_EQ(Result, (-2147483647 - 1));

	aPacked[0] &= ~0x40; // unset sign bit

	EXPECT_EQ(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked, (ptrdiff_t)CVariableInt::MAX_BYTES_PACKED);
	EXPECT_EQ(Result, 2147483647);
}

TEST(CVariableInt, PackBufferTooSmall)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED / 2]; // too small
	EXPECT_EQ(CVariableInt::Pack(aPacked, 2147483647, sizeof(aPacked)), (const unsigned char *)0x0);
}

TEST(CVariableInt, UnpackBufferTooSmall)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED / 2];
	for(auto &Byte : aPacked)
		Byte = 0xFF; // extended bits are set, but buffer ends too early

	int UnusedResult;
	EXPECT_EQ(CVariableInt::Unpack(aPacked, &UnusedResult, sizeof(aPacked)), (const unsigned char *)0x0);
}

TEST(CVariableInt, RoundtripCompressDecompress)
{
	unsigned char aCompressed[NUM * CVariableInt::MAX_BYTES_PACKED];
	int aDecompressed[NUM];
	long ExpectedCompressedSize = 0;
	for(auto Size : SIZES)
		ExpectedCompressedSize += Size;

	long CompressedSize = CVariableInt::Compress(DATA, sizeof(DATA), aCompressed, sizeof(aCompressed));
	ASSERT_EQ(CompressedSize, ExpectedCompressedSize);
	long DecompressedSize = CVariableInt::Decompress(aCompressed, ExpectedCompressedSize, aDecompressed, sizeof(aDecompressed));
	ASSERT_EQ(DecompressedSize, sizeof(DATA));
	for(int i = 0; i < NUM; i++)
	{
		EXPECT_EQ(aDecompressed[i], DATA[i]);
	}
}

TEST(CVariableInt, CompressBufferTooSmall)
{
	unsigned char aCompressed[NUM]; // too small
	long CompressedSize = CVariableInt::Compress(DATA, sizeof(DATA), aCompressed, sizeof(aCompressed));
	ASSERT_EQ(CompressedSize, -1);
}

TEST(CVariableInt, DecompressBufferTooSmall)
{
	unsigned char aCompressed[] = {0x00, 0x01, 0x40, 0x20, 0x80, 0x01, 0x80, 0x04, 0xFF, 0x07, 0xB9, 0xC0, 0x01};
	int aUncompressed[4]; // too small
	long CompressedSize = CVariableInt::Decompress(aCompressed, sizeof(aCompressed), aUncompressed, sizeof(aUncompressed));
	ASSERT_EQ(CompressedSize, -1);
}

TEST(CVariableInt, CompressUnalignedSourceKeepsWireBytes)
{
	const int aValues[] = {0, 1, -1, 64, 2147483647, (-2147483647 - 1)};
	const unsigned char aExpected[] = {0x00, 0x01, 0x40, 0x80, 0x01, 0xbf, 0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0f};
	alignas(int) std::array<unsigned char, sizeof(aValues) + 2> aInput{};
	mem_copy(aInput.data() + 1, aValues, sizeof(aValues));
	std::array<unsigned char, sizeof(aExpected)> aOutput{};
	ASSERT_EQ(CVariableInt::Compress(aInput.data() + 1, sizeof(aValues), aOutput.data(), aOutput.size()), sizeof(aExpected));
	EXPECT_EQ(mem_comp(aOutput.data(), aExpected, sizeof(aExpected)), 0);
}

TEST(CVariableInt, DecompressUnalignedDestinationPreservesGuards)
{
	const unsigned char aPacked[] = {0x00, 0x01, 0x40, 0x80, 0x01, 0xbf, 0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0f};
	const int aExpected[] = {0, 1, -1, 64, 2147483647, (-2147483647 - 1)};
	alignas(int) std::array<unsigned char, sizeof(aExpected) + 2> aOutput;
	aOutput.fill(0xa5);
	ASSERT_EQ(CVariableInt::Decompress(aPacked, sizeof(aPacked), aOutput.data() + 1, sizeof(aExpected)), sizeof(aExpected));
	EXPECT_EQ(mem_comp(aOutput.data() + 1, aExpected, sizeof(aExpected)), 0);
	EXPECT_EQ(aOutput.front(), 0xa5);
	EXPECT_EQ(aOutput.back(), 0xa5);
}

TEST(CVariableInt, EveryIntegerByteOffsetRoundTrips)
{
	for(size_t Offset = 0; Offset < alignof(int); ++Offset)
	{
		SCOPED_TRACE(Offset);
		alignas(int) std::array<unsigned char, sizeof(DATA) + alignof(int)> aInput{};
		alignas(int) std::array<unsigned char, sizeof(DATA) + alignof(int)> aOutput{};
		std::array<unsigned char, NUM * CVariableInt::MAX_BYTES_PACKED> aPacked;
		mem_copy(aInput.data() + Offset, DATA, sizeof(DATA));
		const long PackedSize = CVariableInt::Compress(aInput.data() + Offset, sizeof(DATA), aPacked.data(), aPacked.size());
		ASSERT_GT(PackedSize, 0);
		ASSERT_EQ(CVariableInt::Decompress(aPacked.data(), PackedSize, aOutput.data() + Offset, sizeof(DATA)), sizeof(DATA));
		EXPECT_EQ(mem_comp(aOutput.data() + Offset, DATA, sizeof(DATA)), 0);
	}
}

TEST(CVariableInt, EmptyBatchAcceptsNullBuffers)
{
	EXPECT_EQ(CVariableInt::Compress(nullptr, 0, nullptr, 0), 0);
	EXPECT_EQ(CVariableInt::Decompress(nullptr, 0, nullptr, 0), 0);
}

TEST(CVariableInt, NegativeBatchLengthsAreRejectedBeforeAccess)
{
	int Value = 7;
	unsigned char aBuffer[8] = {};
	EXPECT_EQ(CVariableInt::Compress(nullptr, -4, aBuffer, sizeof(aBuffer)), -1);
	EXPECT_EQ(CVariableInt::Compress(&Value, sizeof(Value), nullptr, -1), -1);
	EXPECT_EQ(CVariableInt::Decompress(nullptr, -1, &Value, sizeof(Value)), -1);
	EXPECT_EQ(CVariableInt::Decompress(aBuffer, 1, nullptr, -4), -1);
	EXPECT_EQ(Value, 7);
}

TEST(CVariableInt, IncompleteNativeIntegerInputIsRejectedWithoutWriting)
{
	const std::array<unsigned char, 3> aInput = {1, 2, 3};
	std::array<unsigned char, 8> aOutput;
	aOutput.fill(0xa5);
	const auto Original = aOutput;
	EXPECT_EQ(CVariableInt::Compress(aInput.data(), aInput.size(), aOutput.data(), aOutput.size()), -1);
	EXPECT_EQ(aOutput, Original);
}

TEST(CVariableInt, NonIntegralOutputCapacityIsRejectedWithoutWriting)
{
	const unsigned char aInput[] = {0};
	std::array<unsigned char, sizeof(int) + 1> aOutput;
	aOutput.fill(0xa5);
	const auto Original = aOutput;
	EXPECT_EQ(CVariableInt::Decompress(aInput, sizeof(aInput), aOutput.data(), aOutput.size()), -1);
	EXPECT_EQ(aOutput, Original);
}

TEST(CVariableInt, MissingNonemptyBatchBuffersAreRejected)
{
	int Value = 7;
	unsigned char Byte = 0;
	EXPECT_EQ(CVariableInt::Compress(nullptr, sizeof(Value), &Byte, sizeof(Byte)), -1);
	EXPECT_EQ(CVariableInt::Compress(&Value, sizeof(Value), nullptr, 1), -1);
	EXPECT_EQ(CVariableInt::Decompress(nullptr, 1, &Value, sizeof(Value)), -1);
	EXPECT_EQ(CVariableInt::Decompress(&Byte, sizeof(Byte), nullptr, sizeof(Value)), -1);
	EXPECT_EQ(Value, 7);
	EXPECT_EQ(Byte, 0);
}

TEST(CVariableInt, ZeroOutputCapacityRejectsNonemptyBatch)
{
	int Value = 7;
	unsigned char Byte = 0;
	EXPECT_EQ(CVariableInt::Compress(&Value, sizeof(Value), nullptr, 0), -1);
	EXPECT_EQ(CVariableInt::Decompress(&Byte, sizeof(Byte), nullptr, 0), -1);
}

TEST(CVariableInt, TruncatedIntegerDoesNotPublishPartialNativeInteger)
{
	const unsigned char aInput[] = {0x01, 0x80};
	std::array<int, 2> aOutput = {42, 43};
	EXPECT_EQ(CVariableInt::Decompress(aInput, sizeof(aInput), aOutput.data(), sizeof(aOutput)), -1);
	EXPECT_EQ(aOutput[0], 1);
	EXPECT_EQ(aOutput[1], 43);
}

TEST(CVariableInt, FifthByteContinuationRetainsExistingMaskPolicy)
{
	const unsigned char aInput[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
	std::array<int, 2> aOutput{};
	ASSERT_EQ(CVariableInt::Decompress(aInput, sizeof(aInput), aOutput.data(), sizeof(aOutput)), sizeof(aOutput));
	EXPECT_EQ(aOutput[0], (-2147483647 - 1));
	EXPECT_EQ(aOutput[1], 1);
}

TEST(CVariableInt, TruncatedPackedIntegerNeverReadsBeyondDeclaredPrefix)
{
	const unsigned char aInput[] = {0xbf, 0xff, 0xff, 0xff, 0x0f};
	for(int Size = 1; Size < (int)sizeof(aInput); ++Size)
	{
		SCOPED_TRACE(Size);
		int Output = 42;
		EXPECT_EQ(CVariableInt::Decompress(aInput, Size, &Output, sizeof(Output)), -1);
		EXPECT_EQ(Output, 42);
	}
}
