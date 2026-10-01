#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

static void StrBase64Str(char *pBuffer, int BufferSize, const char *pString)
{
	str_base64(pBuffer, BufferSize, pString, str_length(pString));
}

TEST(StrCodec, HexEncode)
{
	char aOut[64];
	const char *pData = "ABCD";
	str_hex(aOut, sizeof(aOut), pData, 0);
	EXPECT_STREQ(aOut, "");
	str_hex(aOut, sizeof(aOut), pData, 1);
	EXPECT_STREQ(aOut, "41 ");
	str_hex(aOut, sizeof(aOut), pData, 2);
	EXPECT_STREQ(aOut, "41 42 ");
	str_hex(aOut, sizeof(aOut), pData, 3);
	EXPECT_STREQ(aOut, "41 42 43 ");
	str_hex(aOut, sizeof(aOut), pData, 4);
	EXPECT_STREQ(aOut, "41 42 43 44 ");

	str_hex(aOut, 1, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex(aOut, 2, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex(aOut, 3, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex(aOut, 4, pData, 4);
	EXPECT_STREQ(aOut, "41 ");
	str_hex(aOut, 5, pData, 4);
	EXPECT_STREQ(aOut, "41 ");
	str_hex(aOut, 6, pData, 4);
	EXPECT_STREQ(aOut, "41 ");
	str_hex(aOut, 7, pData, 4);
	EXPECT_STREQ(aOut, "41 42 ");
	str_hex(aOut, 8, pData, 4);
	EXPECT_STREQ(aOut, "41 42 ");
}

TEST(StrCodec, HexEncodeCstyle)
{
	char aOut[128];
	const char *pData = "ABCD";
	str_hex_cstyle(aOut, sizeof(aOut), pData, 0);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 1);
	EXPECT_STREQ(aOut, "0x41");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 2);
	EXPECT_STREQ(aOut, "0x41, 0x42");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 3);
	EXPECT_STREQ(aOut, "0x41, 0x42, 0x43");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 4);
	EXPECT_STREQ(aOut, "0x41, 0x42, 0x43, 0x44");

	str_hex_cstyle(aOut, 1, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 2, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 3, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 4, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 5, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 6, pData, 4);
	EXPECT_STREQ(aOut, "");
	str_hex_cstyle(aOut, 7, pData, 4);
	EXPECT_STREQ(aOut, "0x41");
	str_hex_cstyle(aOut, 12, pData, 4);
	EXPECT_STREQ(aOut, "0x41");
	str_hex_cstyle(aOut, 13, pData, 4);
	EXPECT_STREQ(aOut, "0x41, 0x42");
	str_hex_cstyle(aOut, 14, pData, 4);
	EXPECT_STREQ(aOut, "0x41, 0x42");

	str_hex_cstyle(aOut, sizeof(aOut), pData, 4, 1);
	EXPECT_STREQ(aOut, "0x41,\n0x42,\n0x43,\n0x44");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 4, 2);
	EXPECT_STREQ(aOut, "0x41, 0x42,\n0x43, 0x44");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 4, 3);
	EXPECT_STREQ(aOut, "0x41, 0x42, 0x43,\n0x44");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 4, 4);
	EXPECT_STREQ(aOut, "0x41, 0x42, 0x43, 0x44");
	str_hex_cstyle(aOut, sizeof(aOut), pData, 4, 500);
	EXPECT_STREQ(aOut, "0x41, 0x42, 0x43, 0x44");
}

TEST(StrCodec, HexDecode)
{
	char aOut[5] = {'a', 'b', 'c', 'd', 0};
	EXPECT_EQ(str_hex_decode(aOut, 0, ""), 0);
	EXPECT_STREQ(aOut, "abcd");
	EXPECT_EQ(str_hex_decode(aOut, 0, " "), 2);
	EXPECT_STREQ(aOut, "abcd");
	EXPECT_EQ(str_hex_decode(aOut, 1, "1"), 2);
	EXPECT_STREQ(aOut + 1, "bcd");
	EXPECT_EQ(str_hex_decode(aOut, 1, "41"), 0);
	EXPECT_STREQ(aOut, "Abcd");
	EXPECT_EQ(str_hex_decode(aOut, 1, "4x"), 1);
	EXPECT_STREQ(aOut + 1, "bcd");
	EXPECT_EQ(str_hex_decode(aOut, 1, "x1"), 1);
	EXPECT_STREQ(aOut + 1, "bcd");
	EXPECT_EQ(str_hex_decode(aOut, 1, "411"), 2);
	EXPECT_STREQ(aOut + 1, "bcd");
	EXPECT_EQ(str_hex_decode(aOut, 4, "41424344"), 0);
	EXPECT_STREQ(aOut, "ABCD");
}

TEST(StrCodec, Base64)
{
	char aBuf[128];
	str_base64(aBuf, sizeof(aBuf), "\0", 1);
	EXPECT_STREQ(aBuf, "AA==");
	str_base64(aBuf, sizeof(aBuf), "\0\0", 2);
	EXPECT_STREQ(aBuf, "AAA=");
	str_base64(aBuf, sizeof(aBuf), "\0\0\0", 3);
	EXPECT_STREQ(aBuf, "AAAA");

	StrBase64Str(aBuf, sizeof(aBuf), "");
	EXPECT_STREQ(aBuf, "");

	// https://en.wikipedia.org/w/index.php?title=Base64&oldid=1033503483#Output_padding
	StrBase64Str(aBuf, sizeof(aBuf), "pleasure.");
	EXPECT_STREQ(aBuf, "cGxlYXN1cmUu");
	StrBase64Str(aBuf, sizeof(aBuf), "leasure.");
	EXPECT_STREQ(aBuf, "bGVhc3VyZS4=");
	StrBase64Str(aBuf, sizeof(aBuf), "easure.");
	EXPECT_STREQ(aBuf, "ZWFzdXJlLg==");
	StrBase64Str(aBuf, sizeof(aBuf), "asure.");
	EXPECT_STREQ(aBuf, "YXN1cmUu");
	StrBase64Str(aBuf, sizeof(aBuf), "sure.");
	EXPECT_STREQ(aBuf, "c3VyZS4=");

	StrBase64Str(aBuf, 4, "pleasure.");
	EXPECT_STREQ(aBuf, "cGx");
	StrBase64Str(aBuf, 5, "pleasure.");
	EXPECT_STREQ(aBuf, "cGxl");
	StrBase64Str(aBuf, 6, "pleasure.");
	EXPECT_STREQ(aBuf, "cGxlY");
}

TEST(StrCodec, Base64Decode)
{
	char aOut[17];
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), ""), 0);
	EXPECT_STREQ(aOut, "XXXXXXXXXXXXXXXX");

	// https://en.wikipedia.org/w/index.php?title=Base64&oldid=1033503483#Output_padding
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "cGxlYXN1cmUu"), 9);
	EXPECT_STREQ(aOut, "pleasure.XXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "bGVhc3VyZS4="), 8);
	EXPECT_STREQ(aOut, "leasure.XXXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "ZWFzdXJlLg=="), 7);
	EXPECT_STREQ(aOut, "easure.XXXXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "YXN1cmUu"), 6);
	EXPECT_STREQ(aOut, "asure.XXXXXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "c3VyZS4="), 5);
	EXPECT_STREQ(aOut, "sure.XXXXXXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "////"), 3);
	EXPECT_STREQ(aOut, "\xff\xff\xffXXXXXXXXXXXXX");
	str_copy(aOut, "XXXXXXXXXXXXXXXX", sizeof(aOut));
	EXPECT_EQ(str_base64_decode(aOut, sizeof(aOut), "CQk+"), 3);
	EXPECT_STREQ(aOut, "		>XXXXXXXXXXXXX");
}

TEST(StrCodec, Base64DecodeError)
{
	char aBuf[128];
	// Wrong padding.
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "A"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AA"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AAA"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "A==="), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "=AAA"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "===="), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AAA=AAAA"), 0);
	// Invalid characters.
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "----"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "A---"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AA--"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AAA-"), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AAAA "), 0);
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "AAA "), 0);
	// Invalid padding values.
	EXPECT_LT(str_base64_decode(aBuf, sizeof(aBuf), "//=="), 0);
	// Wrong output buffer size.
	EXPECT_LT(str_base64_decode(aBuf, 2, "cGxlYXN1cmUu"), 0);
	EXPECT_LT(str_base64_decode(aBuf, 3, "cGxlYXN1cmUu"), 0);
	EXPECT_LT(str_base64_decode(aBuf, 4, "cGxlYXN1cmUu"), 0);
}
