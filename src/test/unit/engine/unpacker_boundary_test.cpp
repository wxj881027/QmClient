#include <engine/shared/packer.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>

TEST(UnpackerBoundary, NegativeResetLengthProducesAnErrorWithoutAnInvalidRange)
{
	const unsigned char Byte = 1;
	CUnpacker Unpacker;
	Unpacker.Reset(&Byte, -1);
	EXPECT_TRUE(Unpacker.Error());
	EXPECT_EQ(Unpacker.CompleteData(), &Byte);
	EXPECT_EQ(Unpacker.CompleteSize(), 0);
	EXPECT_EQ(Unpacker.GetInt(), 0);
}

TEST(UnpackerBoundary, NullNonemptyInputIsRejected)
{
	CUnpacker Unpacker;
	Unpacker.Reset(nullptr, 1);
	EXPECT_TRUE(Unpacker.Error());
	EXPECT_EQ(Unpacker.CompleteSize(), 0);
	EXPECT_EQ(Unpacker.GetRaw(0), nullptr);
}

TEST(UnpackerBoundary, NullEmptyInputRetainsIdentityAndAllowsDefaultReads)
{
	CUnpacker Unpacker;
	Unpacker.Reset(nullptr, 0);
	EXPECT_FALSE(Unpacker.Error());
	EXPECT_EQ(Unpacker.CompleteData(), nullptr);
	EXPECT_EQ(Unpacker.CompleteSize(), 0);
	EXPECT_EQ(Unpacker.GetIntOrDefault(7), 7);
	EXPECT_EQ(Unpacker.GetUncompressedIntOrDefault(9), 9);
	EXPECT_EQ(Unpacker.GetRaw(0), nullptr);
	EXPECT_FALSE(Unpacker.Error());
	EXPECT_EQ(Unpacker.GetInt(), 0);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerBoundary, ExactNativeIntegerDecodesWithoutChangingItsFormat)
{
	const int Value = 0x12345678;
	CUnpacker Unpacker;
	Unpacker.Reset(&Value, sizeof(Value));
	EXPECT_EQ(Unpacker.GetUncompressedInt(), Value);
	EXPECT_FALSE(Unpacker.Error());
	EXPECT_EQ(Unpacker.GetUncompressedIntOrDefault(7), 7);
}

TEST(UnpackerBoundary, TruncatedNativeIntegerIsRejected)
{
	const std::array<unsigned char, sizeof(int) - 1> aData{};
	for(int Size = 0; Size < static_cast<int>(sizeof(int)); ++Size)
	{
		SCOPED_TRACE(Size);
		CUnpacker Unpacker;
		Unpacker.Reset(aData.data(), Size);
		EXPECT_EQ(Unpacker.GetUncompressedInt(), 0);
		EXPECT_TRUE(Unpacker.Error());
	}
}

TEST(UnpackerBoundary, TrailingByteCannotSupplyAnotherNativeInteger)
{
	const int Value = 123;
	std::array<unsigned char, sizeof(int) + 1> aData{};
	std::memcpy(aData.data(), &Value, sizeof(Value));
	CUnpacker Unpacker;
	Unpacker.Reset(aData.data(), static_cast<int>(aData.size()));
	ASSERT_EQ(Unpacker.GetUncompressedInt(), Value);
	EXPECT_EQ(Unpacker.GetUncompressedInt(), 0);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerBoundary, UnterminatedStringSetsAnError)
{
	char aData[] = {'a', 'b', 'c'};
	CUnpacker Unpacker;
	Unpacker.Reset(aData, sizeof(aData));
	EXPECT_STREQ(Unpacker.GetString(), "");
	EXPECT_TRUE(Unpacker.Error());
}

TEST(UnpackerBoundary, ExistingControlSanitizationAndWhitespaceSkippingRemain)
{
	char aData[] = "  A\nB";
	CUnpacker Unpacker;
	Unpacker.Reset(aData, sizeof(aData));
	EXPECT_STREQ(Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES), "A B");
	EXPECT_FALSE(Unpacker.Error());
}
