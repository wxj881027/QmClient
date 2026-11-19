#include <engine/shared/packer.h>

#include <gtest/gtest.h>

#include <limits>

TEST(UnpackerRaw, ExactRemainingRangeCanBeRead)
{
	const unsigned char aData[] = {1, 2, 3};
	CUnpacker Unpacker;
	Unpacker.Reset(aData, static_cast<int>(sizeof(aData)));
	EXPECT_EQ(Unpacker.GetRaw(1), aData);
	EXPECT_EQ(Unpacker.GetRaw(2), aData + 1);
	EXPECT_FALSE(Unpacker.Error());
}

TEST(UnpackerRaw, HugeRangeAfterPrefixIsRejectedAndErrorStaysSet)
{
	const unsigned char aData[] = {1, 2};
	CUnpacker Unpacker;
	Unpacker.Reset(aData, static_cast<int>(sizeof(aData)));
	ASSERT_EQ(Unpacker.GetRaw(1), aData);
	EXPECT_EQ(Unpacker.GetRaw(std::numeric_limits<int>::max()), nullptr);
	EXPECT_TRUE(Unpacker.Error());
	EXPECT_EQ(Unpacker.GetRaw(0), nullptr);
}

TEST(UnpackerRaw, NegativeRangeIsRejected)
{
	const unsigned char aData[] = {1};
	CUnpacker Unpacker;
	Unpacker.Reset(aData, static_cast<int>(sizeof(aData)));
	EXPECT_EQ(Unpacker.GetRaw(-1), nullptr);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerRaw, ResetRestoresReadingAfterRangeFailure)
{
	const unsigned char aData[] = {1};
	CUnpacker Unpacker;
	Unpacker.Reset(aData, static_cast<int>(sizeof(aData)));
	EXPECT_EQ(Unpacker.GetRaw(2), nullptr);
	Unpacker.Reset(aData, static_cast<int>(sizeof(aData)));
	EXPECT_EQ(Unpacker.GetRaw(1), aData);
	EXPECT_EQ(Unpacker.GetRaw(0), aData + 1);
	EXPECT_FALSE(Unpacker.Error());
}
