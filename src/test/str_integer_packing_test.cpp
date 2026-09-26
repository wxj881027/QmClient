#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrIntegerPacking, StrToInts)
{
	int aInts[8];

	StrToInts(aInts, 1, "a");
	EXPECT_EQ(aInts[0], 0xE1808000);

	StrToInts(aInts, 1, "ab");
	EXPECT_EQ(aInts[0], 0xE1E28000);

	StrToInts(aInts, 1, "abc");
	EXPECT_EQ(aInts[0], 0xE1E2E300);

	StrToInts(aInts, 2, "abcd");
	EXPECT_EQ(aInts[0], 0xE1E2E3E4);
	EXPECT_EQ(aInts[1], 0x80808000);

	StrToInts(aInts, 2, "abcde");
	EXPECT_EQ(aInts[0], 0xE1E2E3E4);
	EXPECT_EQ(aInts[1], 0xE5808000);

	StrToInts(aInts, 2, "abcdef");
	EXPECT_EQ(aInts[0], 0xE1E2E3E4);
	EXPECT_EQ(aInts[1], 0xE5E68000);

	StrToInts(aInts, 2, "abcdefg");
	EXPECT_EQ(aInts[0], 0xE1E2E3E4);
	EXPECT_EQ(aInts[1], 0xE5E6E700);

	StrToInts(aInts, 2, "öüä");
	EXPECT_EQ(aInts[0], 0x4336433C);
	EXPECT_EQ(aInts[1], 0x43248000);

	StrToInts(aInts, 3, "aβい🐘");
	EXPECT_EQ(aInts[0], 0xE14E3263);
	EXPECT_EQ(aInts[1], 0x0104701F);
	EXPECT_EQ(aInts[2], 0x10188000);

	// long padding
	StrToInts(aInts, 4, "abc");
	EXPECT_EQ(aInts[0], 0xE1E2E380);
	EXPECT_EQ(aInts[1], 0x80808080);
	EXPECT_EQ(aInts[2], 0x80808080);
	EXPECT_EQ(aInts[3], 0x80808000);
}

TEST(StrIntegerPacking, IntsToStr)
{
	int aInts[8];
	char aStr[sizeof(aInts)];

	aInts[0] = 0xE1808000;
	EXPECT_TRUE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "a");

	aInts[0] = 0xE1E28000;
	EXPECT_TRUE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "ab");

	aInts[0] = 0xE1E2E300;
	EXPECT_TRUE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abc");

	aInts[0] = 0xE1E2E3E4;
	aInts[1] = 0x80808000;
	EXPECT_TRUE(IntsToStr(aInts, 2, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abcd");

	aInts[0] = 0xE1E2E3E4;
	aInts[1] = 0xE5808000;
	EXPECT_TRUE(IntsToStr(aInts, 2, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abcde");

	aInts[0] = 0xE1E2E3E4;
	aInts[1] = 0xE5E68000;
	EXPECT_TRUE(IntsToStr(aInts, 2, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abcdef");

	aInts[0] = 0xE1E2E3E4;
	aInts[1] = 0xE5E6E700;
	EXPECT_TRUE(IntsToStr(aInts, 2, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abcdefg");

	aInts[0] = 0x4336433C;
	aInts[1] = 0x43248000;
	EXPECT_TRUE(IntsToStr(aInts, 2, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "öüä");

	aInts[0] = 0xE14E3263;
	aInts[1] = 0x0104701F;
	aInts[2] = 0x10188000;
	EXPECT_TRUE(IntsToStr(aInts, 3, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "aβい🐘");

	// long padding
	aInts[0] = 0xE1E2E380;
	aInts[1] = 0x80808080;
	aInts[2] = 0x80808080;
	aInts[3] = 0x80808000;
	EXPECT_TRUE(IntsToStr(aInts, 4, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abc");

	// early null character (0x80)
	aInts[0] = 0xE1E2E380;
	aInts[1] = 0xE1E2E3E4;
	aInts[2] = 0xE1E2E3E4;
	aInts[3] = 0xE1E2E300;
	EXPECT_TRUE(IntsToStr(aInts, 4, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abc");

	// invalid UTF-8
	aInts[0] = 0xE17FE200;
	EXPECT_FALSE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "");

	// invalid UTF-8 (0x00 in string data, which is translated to '\x80')
	aInts[0] = 0xE100E200;
	EXPECT_FALSE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "");

	// invalid UTF-8
	aInts[0] = 0xE1E2E36F;
	aInts[1] = 0x3F40E4E5;
	aInts[2] = 0xE67FE700;
	EXPECT_FALSE(IntsToStr(aInts, 3, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "");

	// invalid UTF-8 and missing null-terminator
	aInts[0] = 0x7F7F7F7F;
	EXPECT_FALSE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "");

	// missing null-terminator at the end is ignored
	aInts[0] = 0xE1E2E3E4;
	EXPECT_TRUE(IntsToStr(aInts, 1, aStr, std::size(aStr)));
	EXPECT_STREQ(aStr, "abc");

	// basic fuzzing: no input integer should result in invalid UTF-8 in the string
	for(int i = 0; i <= 0xFFFFFF; i += 7) // checking all values takes a bit too long
	{
		mem_zero(aStr, std::size(aStr));
		aInts[0] = 0xE1 << 24 | i;
		aInts[1] = i << 8 | 0xE2;
		aInts[2] = 0xE3 << 24 | i;
		const bool ConversionResult = IntsToStr(aInts, 3, aStr, std::size(aStr));
		// ensure null-termination before calling str_utf8_check
		ASSERT_TRUE(mem_has_null(aStr, std::size(aStr)));
		ASSERT_TRUE(str_utf8_check(aStr));
		ASSERT_TRUE(ConversionResult || aStr[0] == '\0');
	}
}

#if defined(CONF_FAMILY_WINDOWS)
TEST(StrIntegerPacking, WindowsUtf8WideConversion)
{
	const char *apUtf8Strings[] = {
		"",
		"abc",
		"a bb ccc dddd       eeeee",
		"öüä",
		"привет Наташа",
		"ąçęįǫų",
		"DDNet最好了",
		"aβい🐘"};
	const wchar_t *apWideStrings[] = {
		L"",
		L"abc",
		L"a bb ccc dddd       eeeee",
		L"öüä",
		L"привет Наташа",
		L"ąçęįǫų",
		L"DDNet最好了",
		L"aβい🐘"};
	static_assert(std::size(apUtf8Strings) == std::size(apWideStrings));
	for(size_t i = 0; i < std::size(apUtf8Strings); i++)
	{
		const std::optional<std::string> ConvertedUtf8 = windows_wide_to_utf8(apWideStrings[i]);
		const std::wstring ConvertedWide = windows_utf8_to_wide(apUtf8Strings[i]);
		ASSERT_TRUE(ConvertedUtf8.has_value());
		EXPECT_STREQ(ConvertedUtf8.value().c_str(), apUtf8Strings[i]);
		EXPECT_STREQ(ConvertedWide.c_str(), apWideStrings[i]);
	}
}
#endif
