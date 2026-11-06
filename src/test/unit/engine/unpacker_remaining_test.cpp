#include <engine/shared/packer.h>

#include <gtest/gtest.h>

TEST(UnpackerRemaining, ResetReportsFullInputWithoutConsumingIt)
{
	char aInput[] = "value";
	CUnpacker Unpacker;
	Unpacker.Reset(aInput, sizeof(aInput));
	EXPECT_EQ(Unpacker.RemainingSize(), sizeof(aInput));
	EXPECT_EQ(Unpacker.RemainingSize(), sizeof(aInput));
	EXPECT_FALSE(Unpacker.Error());
}

TEST(UnpackerRemaining, StringAndRawReadsReachAnErrorFreeEnd)
{
	char aInput[] = {'a', '\0', 'x', 'y'};
	CUnpacker Unpacker;
	Unpacker.Reset(aInput, sizeof(aInput));
	EXPECT_STREQ(Unpacker.GetString(), "a");
	EXPECT_EQ(Unpacker.RemainingSize(), 2);
	EXPECT_EQ(Unpacker.GetRaw(2), reinterpret_cast<const unsigned char *>(aInput + 2));
	EXPECT_EQ(Unpacker.RemainingSize(), 0);
	EXPECT_FALSE(Unpacker.Error());
}

TEST(UnpackerRemaining, FailedReadKeepsRemainingBytesAndErrorState)
{
	char aInput[] = {'x', 'y'};
	CUnpacker Unpacker;
	Unpacker.Reset(aInput, sizeof(aInput));
	EXPECT_EQ(Unpacker.GetRaw(3), nullptr);
	EXPECT_TRUE(Unpacker.Error());
	EXPECT_EQ(Unpacker.RemainingSize(), 2);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerRemaining, EmptyAndInvalidResetsHaveDifferentErrorStates)
{
	CUnpacker Unpacker;
	Unpacker.Reset(nullptr, 0);
	EXPECT_EQ(Unpacker.RemainingSize(), 0);
	EXPECT_FALSE(Unpacker.Error());
	Unpacker.Reset(nullptr, 1);
	EXPECT_EQ(Unpacker.RemainingSize(), 0);
	EXPECT_TRUE(Unpacker.Error());
	Unpacker.Reset(nullptr, -1);
	EXPECT_EQ(Unpacker.RemainingSize(), 0);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerRemaining, IntegerReadsConsumeOnlyTheirEncodedBytes)
{
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(128);
	const int FirstSize = Packer.Size();
	Packer.AddInt(-7);
	ASSERT_FALSE(Packer.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	EXPECT_EQ(Unpacker.GetInt(), 128);
	EXPECT_EQ(Unpacker.RemainingSize(), Packer.Size() - FirstSize);
	EXPECT_EQ(Unpacker.GetInt(), -7);
	EXPECT_EQ(Unpacker.RemainingSize(), 0);
	EXPECT_FALSE(Unpacker.Error());
}
