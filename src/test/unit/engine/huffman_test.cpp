#include <base/mem.h>

#include <engine/shared/huffman.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(Huffman, CompressionInputSizeZero)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[64];
	unsigned char aCompressed[2048];
	unsigned char aDecompressed[2048];

	const int CompressedSize = Huffman.Compress(aInput, 0, aCompressed, sizeof(aCompressed));
	const unsigned char aExpected[] = {0x8A, 0x1B};

	ASSERT_EQ(CompressedSize, (int)sizeof(aExpected));
	EXPECT_EQ(mem_comp(aCompressed, aExpected, CompressedSize), 0);

	const int UncompressedSize = Huffman.Decompress(aCompressed, CompressedSize, aDecompressed, sizeof(aDecompressed));
	ASSERT_EQ(UncompressedSize, 0);
}

TEST(Huffman, CompressionShouldNotChangeData)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[64];
	unsigned char aCompressed[2048];
	unsigned char aDecompressed[2048];

	for(int InputMod = 0x00; InputMod <= 0xFFFF; ++InputMod)
	{
		mem_zero(aInput, sizeof(aInput));
		mem_zero(aCompressed, sizeof(aCompressed));
		mem_zero(aDecompressed, sizeof(aDecompressed));
		aInput[0] = InputMod & 0xFF;
		aInput[1] = (InputMod >> 8) & 0xFF;

		const int CompressedSize = Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));
		const int MaxSize = InputMod <= 0xFF ? 12 : 14;
		ASSERT_GE(CompressedSize, 10);
		ASSERT_LE(CompressedSize, MaxSize);

		const int UncompressedSize = Huffman.Decompress(aCompressed, CompressedSize, aDecompressed, sizeof(aDecompressed));
		ASSERT_EQ(UncompressedSize, (int)sizeof(aInput));
		EXPECT_EQ(mem_comp(aInput, aDecompressed, UncompressedSize), 0);
	}
}

TEST(Huffman, CompressionCompatible)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[64];
	unsigned char aCompressed[2048];

	mem_zero(aInput, sizeof(aInput));
	mem_zero(aCompressed, sizeof(aCompressed));

	// compress 1-7 followed by a bunch of nullbytes
	for(int i = 0; i < 8; i++)
		aInput[i] = i;

	const int Size = Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));

	const unsigned char aExpected[] = {0x51, 0x58, 0x78, 0x76, 0x1B, 0xB7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xc5, 0x0D};

	ASSERT_EQ(Size, (int)sizeof(aExpected)) << "The compression is not compatible with older/other implementations anymore";
	EXPECT_EQ(mem_comp(aCompressed, aExpected, Size), 0) << "The compression is not compatible with older/other implementations anymore";
}

TEST(Huffman, CompressionNoTrailingNull)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[64];
	unsigned char aCompressed[2048];
	unsigned char aDecompressed[2048];

	mem_zero(aInput, sizeof(aInput));
	mem_zero(aCompressed, sizeof(aCompressed));
	mem_zero(aDecompressed, sizeof(aDecompressed));
	aInput[0] = 0x15;

	const int CompressedSize = Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));

	const unsigned char aExpected[] = {0xBE, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x15, 0x37};

	ASSERT_EQ(CompressedSize, (int)sizeof(aExpected));
	EXPECT_EQ(mem_comp(aCompressed, aExpected, CompressedSize), 0);

	const int UncompressedSize = Huffman.Decompress(aCompressed, CompressedSize, aDecompressed, sizeof(aDecompressed));
	ASSERT_EQ(UncompressedSize, (int)sizeof(aInput));
	EXPECT_EQ(mem_comp(aInput, aDecompressed, UncompressedSize), 0);
}

TEST(Huffman, CompressionTruncated)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[64];
	unsigned char aCompressed[2048];

	mem_zero(aInput, sizeof(aInput));
	mem_zero(aCompressed, sizeof(aCompressed));

	// compress 1-7 followed by a bunch of nullbytes
	for(int i = 0; i < 8; i++)
		aInput[i] = i;

	for(size_t CompressedSize = 1; CompressedSize <= 14; ++CompressedSize)
	{
		EXPECT_EQ(Huffman.Compress(aInput, sizeof(aInput), aCompressed, CompressedSize), -1) << "Compression expected to fail with size " << CompressedSize;
	}
	for(size_t CompressedSize = 15; CompressedSize <= 20; ++CompressedSize)
	{
		EXPECT_EQ(Huffman.Compress(aInput, sizeof(aInput), aCompressed, CompressedSize), 15) << "Compression expected to succeed with size " << CompressedSize;
	}
}

TEST(Huffman, DecompressionTableLookupIntegerOverflow)
{
	CHuffman Huffman;
	Huffman.Init();

	// Test data found by fuzzing
	const unsigned char aInput1[] = {0x1A};
	const unsigned char aInput2[] = {0x62, 0x91, 0x62, 0xA9};
	const unsigned char aInput3[] = {0x4C, 0x04, 0xFE, 0x00, 0x68};
	unsigned char aUncompressed[2048];

	EXPECT_EQ(Huffman.Decompress(aInput1, sizeof(aInput1), aUncompressed, sizeof(aUncompressed)), -1);
	EXPECT_EQ(Huffman.Decompress(aInput2, sizeof(aInput2), aUncompressed, sizeof(aUncompressed)), -1);
	EXPECT_EQ(Huffman.Decompress(aInput3, sizeof(aInput3), aUncompressed, sizeof(aUncompressed)), -1);
}

TEST(Huffman, TruncatedInputCannotEmitSymbolsFromMissingBits)
{
	CHuffman Huffman;
	Huffman.Init();
	unsigned char aInput[8];
	unsigned char aCompressed[64];
	unsigned char aOutput[64];
	for(int Symbol = 0; Symbol < 256; ++Symbol)
	{
		for(int Padding = 0; Padding < 8; ++Padding)
		{
			SCOPED_TRACE(testing::Message() << Symbol << ":" << Padding);
			mem_zero(aInput, sizeof(aInput));
			aInput[Padding] = Symbol;
			const int Size = Huffman.Compress(aInput, Padding + 1, aCompressed, sizeof(aCompressed));
			ASSERT_GT(Size, 0);
			for(int Prefix = 1; Prefix < Size && Prefix <= 4; ++Prefix)
			{
				SCOPED_TRACE(Prefix);
				for(size_t i = 0; i < sizeof(aOutput); ++i)
					aOutput[i] = i % 2 ? 0xa5 : 0x5a;
				EXPECT_EQ(Huffman.Decompress(aCompressed, Prefix, aOutput, sizeof(aOutput)), -1);
				// 每个符号至少消耗一位；失败也不能用不存在的比特输出更多符号。
				for(size_t i = Prefix * 8; i < sizeof(aOutput); ++i)
					EXPECT_EQ(aOutput[i], i % 2 ? 0xa5 : 0x5a) << i;
			}
		}
	}
}

TEST(Huffman, DecompressionExactCapacityAndShortCapacityPreserveOuterGuards)
{
	CHuffman Huffman;
	Huffman.Init();
	const unsigned char aInput[] = {0, 1, 2, 3, 4, 5, 6, 7};
	unsigned char aCompressed[64];
	const int Size = Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));
	ASSERT_GT(Size, 0);
	unsigned char aOutput[sizeof(aInput) + 2];
	std::fill_n(aOutput, sizeof(aOutput), 0xa5);
	ASSERT_EQ(Huffman.Decompress(aCompressed, Size, aOutput + 1, sizeof(aInput)), sizeof(aInput));
	EXPECT_EQ(mem_comp(aOutput + 1, aInput, sizeof(aInput)), 0);
	EXPECT_EQ(aOutput[0], 0xa5);
	EXPECT_EQ(aOutput[sizeof(aOutput) - 1], 0xa5);
	std::fill_n(aOutput, sizeof(aOutput), 0xa5);
	EXPECT_EQ(Huffman.Decompress(aCompressed, Size, aOutput + 1, sizeof(aInput) - 1), -1);
	EXPECT_EQ(aOutput[0], 0xa5);
	EXPECT_EQ(aOutput[sizeof(aInput)], 0xa5);
	EXPECT_EQ(aOutput[sizeof(aOutput) - 1], 0xa5);
}

TEST(Huffman, TruncatedStreamFailureDoesNotAffectNextDecode)
{
	CHuffman Huffman;
	Huffman.Init();
	const unsigned char aEmptyStream[] = {0x8a, 0x1b};
	unsigned char aOutput[16];
	EXPECT_EQ(Huffman.Decompress(aEmptyStream, 1, aOutput, sizeof(aOutput)), -1);
	EXPECT_EQ(Huffman.Decompress(aEmptyStream, sizeof(aEmptyStream), aOutput, sizeof(aOutput)), 0);
}
