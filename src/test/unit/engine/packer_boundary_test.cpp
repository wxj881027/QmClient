#include <engine/shared/packer.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace
{
	class CExternalBufferPacker : public CAbstractPacker
	{
	public:
		CExternalBufferPacker(unsigned char *pBuffer, size_t Size) :
			CAbstractPacker(pBuffer, Size) { Reset(); }
	};
}

TEST(PackerBoundary, FullBufferRejectsEmptyStringWithoutOverwritingCanary)
{
	std::array<unsigned char, 6> aData{0xa5, 0, 0, 0, 0, 0xa5};
	CExternalBufferPacker Packer(aData.data() + 1, 4);
	const unsigned char aPayload[] = {1, 2, 3, 4};
	Packer.AddRaw(aPayload, 4);
	ASSERT_FALSE(Packer.Error());
	Packer.AddString("");
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 4);
	EXPECT_EQ(aData.front(), 0xa5);
	EXPECT_EQ(aData.back(), 0xa5);
}

TEST(PackerBoundary, ZeroCapacityRejectsEvenAnEmptyString)
{
	unsigned char Canary = 0xa5;
	CExternalBufferPacker Packer(&Canary, 0);
	Packer.AddString("");
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 0);
	EXPECT_EQ(Canary, 0xa5);
}

TEST(PackerBoundary, OneByteFitsExactlyOneEmptyString)
{
	unsigned char aData[] = {0xa5, 0xa5};
	CExternalBufferPacker Packer(aData, 1);
	Packer.AddString("");
	EXPECT_FALSE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 1);
	EXPECT_EQ(aData[0], 0);
	Packer.AddString("");
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(aData[1], 0xa5);
}

TEST(PackerBoundary, MultibyteStringFitsOnlyWithItsTerminator)
{
	unsigned char aData[5] = {};
	aData[4] = 0xa5;
	CExternalBufferPacker Packer(aData, 4);
	Packer.AddString("中");
	EXPECT_FALSE(Packer.Error());
	EXPECT_STREQ(reinterpret_cast<const char *>(aData), "中");
	EXPECT_EQ(Packer.Size(), 4);
	EXPECT_EQ(aData[4], 0xa5);
}

TEST(PackerBoundary, NegativeRawLengthCannotWriteOrMoveCursor)
{
	unsigned char aData[] = {0xa5, 0xa5};
	CExternalBufferPacker Packer(aData, 1);
	for(int Size : {-1, std::numeric_limits<int>::min()})
	{
		SCOPED_TRACE(Size);
		Packer.Reset();
		Packer.AddRaw(aData + 1, Size);
		EXPECT_TRUE(Packer.Error());
		EXPECT_EQ(Packer.Size(), 0);
		EXPECT_EQ(aData[0], 0xa5);
		EXPECT_EQ(aData[1], 0xa5);
	}
}

TEST(PackerBoundary, HugeRawLengthIsRejectedBeforeAccessingSmallSource)
{
	unsigned char aData[2] = {};
	CExternalBufferPacker Packer(aData, 1);
	Packer.AddRaw(aData + 1, std::numeric_limits<int>::max());
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 0);
}

TEST(PackerBoundary, NullSourceIsAllowedOnlyForEmptyRawData)
{
	unsigned char Byte = 0xa5;
	CExternalBufferPacker Packer(&Byte, 1);
	Packer.AddRaw(nullptr, 0);
	EXPECT_FALSE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 0);
	Packer.AddRaw(nullptr, 1);
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Byte, 0xa5);
}

TEST(PackerBoundary, ResetRestoresWritingAfterCapacityError)
{
	unsigned char aData[4] = {};
	const unsigned char aPayload[] = {1, 2, 3, 4};
	CExternalBufferPacker Packer(aData, sizeof(aData));
	Packer.AddRaw(aPayload, 4);
	Packer.AddRaw(aPayload, 1);
	Packer.AddInt(1);
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 4);
	Packer.Reset();
	Packer.AddString("ok");
	EXPECT_FALSE(Packer.Error());
	EXPECT_STREQ(reinterpret_cast<const char *>(aData), "ok");
}

TEST(PackerBoundary, StringWithoutTerminatorSpacePreservesExistingPrefix)
{
	unsigned char aData[] = {0, 0, 0xa5, 0xa5};
	const unsigned char aPrefix[] = {1, 2};
	CExternalBufferPacker Packer(aData, 3);
	Packer.AddRaw(aPrefix, 2);
	Packer.AddString("x");
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), 2);
	EXPECT_EQ(aData[0], 1);
	EXPECT_EQ(aData[1], 2);
	EXPECT_EQ(aData[2], 0xa5);
	EXPECT_EQ(aData[3], 0xa5);
}
