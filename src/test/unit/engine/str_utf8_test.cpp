#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

template<size_t BufferSize = 128>
static void TestStrUtf8ToLower(const char *pInput, const char *pOutput)
{
	char aBuf[BufferSize];
	str_utf8_tolower(pInput, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, pOutput);
}

TEST(StrUtf8, Dist)
{
	EXPECT_EQ(str_utf8_dist("aaa", "aaa"), 0);
	EXPECT_EQ(str_utf8_dist("123", "123"), 0);
	EXPECT_EQ(str_utf8_dist("", ""), 0);
	EXPECT_EQ(str_utf8_dist("a", "b"), 1);
	EXPECT_EQ(str_utf8_dist("", "aaa"), 3);
	EXPECT_EQ(str_utf8_dist("123", ""), 3);
	EXPECT_EQ(str_utf8_dist("ä", ""), 1);
	EXPECT_EQ(str_utf8_dist("Hëllö", "Hello"), 2);
	// https://en.wikipedia.org/w/index.php?title=Levenshtein_distance&oldid=828480025#Example
	EXPECT_EQ(str_utf8_dist("kitten", "sitting"), 3);
	EXPECT_EQ(str_utf8_dist("flaw", "lawn"), 2);
	EXPECT_EQ(str_utf8_dist("saturday", "sunday"), 3);
}

TEST(StrUtf8, Utf8Isspace)
{
	EXPECT_TRUE(str_utf8_isspace(0x200b)); // Zero-width space
	EXPECT_TRUE(str_utf8_isspace(' '));
	EXPECT_FALSE(str_utf8_isspace('a'));
	// Control characters.
	for(char c = 0; c < 0x20; c++)
	{
		EXPECT_TRUE(str_utf8_isspace(c));
	}
}

TEST(StrUtf8, Utf8SkipWhitespaces)
{
	EXPECT_STREQ(str_utf8_skip_whitespaces("abc"), "abc");
	EXPECT_STREQ(str_utf8_skip_whitespaces("abc   "), "abc   ");
	EXPECT_STREQ(str_utf8_skip_whitespaces("    abc"), "abc");
	EXPECT_STREQ(str_utf8_skip_whitespaces("\xe2\x80\x8b abc"), "abc");
}

TEST(StrUtf8, Utf8TrimRight)
{
	char A1[] = "abc";
	str_utf8_trim_right(A1);
	EXPECT_STREQ(A1, "abc");
	char A2[] = "   abc";
	str_utf8_trim_right(A2);
	EXPECT_STREQ(A2, "   abc");
	char A3[] = "abc   ";
	str_utf8_trim_right(A3);
	EXPECT_STREQ(A3, "abc");
	char A4[] = "abc \xe2\x80\x8b";
	str_utf8_trim_right(A4);
	EXPECT_STREQ(A4, "abc");
}

TEST(StrUtf8, Utf8CompConfusables)
{
	EXPECT_TRUE(str_utf8_comp_confusable("abc", "abc") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("rn", "m") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("m", "rn") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("rna", "ma") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("ma", "rna") == 0);
	EXPECT_FALSE(str_utf8_comp_confusable("mA", "rna") == 0);
	EXPECT_FALSE(str_utf8_comp_confusable("ma", "rnA") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("arn", "am") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("am", "arn") == 0);
	EXPECT_FALSE(str_utf8_comp_confusable("Am", "arn") == 0);
	EXPECT_FALSE(str_utf8_comp_confusable("am", "Arn") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("l", "ӏ") == 0); // CYRILLIC SMALL LETTER PALOCHKA
	EXPECT_TRUE(str_utf8_comp_confusable("i", "¡") == 0); // INVERTED EXCLAMATION MARK
	EXPECT_FALSE(str_utf8_comp_confusable("o", "x") == 0);
	EXPECT_TRUE(str_utf8_comp_confusable("aceiou", "ąçęįǫų") == 0);
}

TEST(StrUtf8, Utf8ToSkeleton)
{
	int aBuf[32];
	EXPECT_EQ(str_utf8_to_skeleton("abc", aBuf, 0), 0);
	EXPECT_EQ(str_utf8_to_skeleton("", aBuf, std::size(aBuf)), 0);
	EXPECT_EQ(str_utf8_to_skeleton("abc", aBuf, std::size(aBuf)), 3);
	EXPECT_EQ(aBuf[0], 'a');
	EXPECT_EQ(aBuf[1], 'b');
	EXPECT_EQ(aBuf[2], 'c');
	EXPECT_EQ(str_utf8_to_skeleton("m", aBuf, std::size(aBuf)), 2);
	EXPECT_EQ(aBuf[0], 'r');
	EXPECT_EQ(aBuf[1], 'n');
	EXPECT_EQ(str_utf8_to_skeleton("rn", aBuf, std::size(aBuf)), 2);
	EXPECT_EQ(aBuf[0], 'r');
	EXPECT_EQ(aBuf[1], 'n');
	EXPECT_EQ(str_utf8_to_skeleton("ӏ", aBuf, std::size(aBuf)), 1); // CYRILLIC SMALL LETTER PALOCHKA
	EXPECT_EQ(aBuf[0], 'i');
	EXPECT_EQ(str_utf8_to_skeleton("¡", aBuf, std::size(aBuf)), 1); // INVERTED EXCLAMATION MARK
	EXPECT_EQ(aBuf[0], 'i');
	EXPECT_EQ(str_utf8_to_skeleton("ąçęįǫų", aBuf, std::size(aBuf)), 6);
	EXPECT_EQ(aBuf[0], 'a');
	EXPECT_EQ(aBuf[1], 'c');
	EXPECT_EQ(aBuf[2], 'e');
	EXPECT_EQ(aBuf[3], 'i');
	EXPECT_EQ(aBuf[4], 'o');
	EXPECT_EQ(aBuf[5], 'u');
}

TEST(StrUtf8, Utf8ToLowerCodepoint)
{
	EXPECT_TRUE(str_utf8_tolower_codepoint('A') == 'a');
	EXPECT_TRUE(str_utf8_tolower_codepoint('z') == 'z');
	EXPECT_TRUE(str_utf8_tolower_codepoint(192) == 224); // À -> à
	EXPECT_TRUE(str_utf8_tolower_codepoint(7882) == 7883); // Ị -> ị
}

TEST(StrUtf8, Utf8ToLower)
{
	// See https://stackoverflow.com/a/18689585
	TestStrUtf8ToLower<>("", "");
	TestStrUtf8ToLower<>("a", "a");
	TestStrUtf8ToLower<>("A", "a");
	TestStrUtf8ToLower<>("z", "z");
	TestStrUtf8ToLower<>("Z", "z");
	TestStrUtf8ToLower<>("ABC", "abc");
	TestStrUtf8ToLower<>("ÖÜÄẞ", "öüäß");
	TestStrUtf8ToLower<>("Iİ", "ii");
	TestStrUtf8ToLower<>("Ϊ", "ϊ");
	TestStrUtf8ToLower<>("Į", "į");
	TestStrUtf8ToLower<>("Җ", "җ");
	TestStrUtf8ToLower<>("Ѹ", "ѹ");
	TestStrUtf8ToLower<>("Ǆ", "ǆ");
	TestStrUtf8ToLower<>("ⒹⒹＮＥＴ", "ⓓⓓｎｅｔ");
	TestStrUtf8ToLower<>("Ⱥ", "ⱥ"); // lower case uses more bytes than upper case

	TestStrUtf8ToLower<1>("ABC", "");
	TestStrUtf8ToLower<2>("ABC", "a");
	TestStrUtf8ToLower<3>("ABC", "ab");
	TestStrUtf8ToLower<4>("ABC", "abc");

	TestStrUtf8ToLower<1>("ȺȺȺ", "");
	TestStrUtf8ToLower<2>("ȺȺȺ", "");
	TestStrUtf8ToLower<3>("ȺȺȺ", "");
	TestStrUtf8ToLower<4>("ȺȺȺ", "ⱥ");
	TestStrUtf8ToLower<5>("ȺȺȺ", "ⱥ");
	TestStrUtf8ToLower<6>("ȺȺȺ", "ⱥ");
	TestStrUtf8ToLower<7>("ȺȺȺ", "ⱥⱥ");
	TestStrUtf8ToLower<8>("ȺȺȺ", "ⱥⱥ");
	TestStrUtf8ToLower<9>("ȺȺȺ", "ⱥⱥ");
	TestStrUtf8ToLower<10>("ȺȺȺ", "ⱥⱥⱥ");
	TestStrUtf8ToLower<11>("ȺȺȺ", "ⱥⱥⱥ");
}

TEST(StrUtf8, Utf8CompNocase)
{
	EXPECT_TRUE(str_utf8_comp_nocase("ÖlÜ", "ölü") == 0);
	EXPECT_TRUE(str_utf8_comp_nocase("ÜlÖ", "ölü") > 0); // ü > ö
	EXPECT_TRUE(str_utf8_comp_nocase("ÖlÜ", "ölüa") < 0); // NULL < a
	EXPECT_TRUE(str_utf8_comp_nocase("ölüa", "ÖlÜ") > 0); // a < NULL

#if (CHAR_MIN < 0)
	const char a[2] = {CHAR_MIN, 0};
	const char b[2] = {0, 0};
	EXPECT_TRUE(str_utf8_comp_nocase(a, b) > 0);
	EXPECT_TRUE(str_utf8_comp_nocase(b, a) < 0);
#endif

	EXPECT_TRUE(str_utf8_comp_nocase_num("ÖlÜ", "ölüa", 5) == 0);
	EXPECT_TRUE(str_utf8_comp_nocase_num("ÖlÜ", "ölüa", 6) != 0);
	EXPECT_TRUE(str_utf8_comp_nocase_num("a", "z", 0) == 0);
	EXPECT_TRUE(str_utf8_comp_nocase_num("a", "z", 1) != 0);
}

TEST(StrUtf8, Utf8FindNocase)
{
	const char *pStr = "abc";
	const char *pEnd;
	EXPECT_EQ(str_utf8_find_nocase(pStr, "a", &pEnd), pStr);
	EXPECT_EQ(pEnd, pStr + str_length("a"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "b", &pEnd), pStr + str_length("a"));
	EXPECT_EQ(pEnd, pStr + str_length("ab"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "c", &pEnd), pStr + str_length("ab"));
	EXPECT_EQ(pEnd, pStr + str_length("abc"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "d", &pEnd), nullptr);
	EXPECT_EQ(pEnd, nullptr);

	EXPECT_EQ(str_utf8_find_nocase(pStr, "A", &pEnd), pStr);
	EXPECT_EQ(pEnd, pStr + str_length("a"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "B", &pEnd), pStr + str_length("a"));
	EXPECT_EQ(pEnd, pStr + str_length("ab"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "C", &pEnd), pStr + str_length("ab"));
	EXPECT_EQ(pEnd, pStr + str_length("abc"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "D", &pEnd), nullptr);
	EXPECT_EQ(pEnd, nullptr);

	pStr = "ÄÖÜ";
	EXPECT_EQ(str_utf8_find_nocase(pStr, "ä", &pEnd), pStr);
	EXPECT_EQ(pEnd, pStr + str_length("Ä"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "ö", &pEnd), pStr + str_length("Ä"));
	EXPECT_EQ(pEnd, pStr + str_length("ÄÖ"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "ü", &pEnd), pStr + str_length("ÄÖ"));
	EXPECT_EQ(pEnd, pStr + str_length("ÄÖÜ"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "z", &pEnd), nullptr);
	EXPECT_EQ(pEnd, nullptr);

	// Both 'I' and 'İ' map to 'i'
	pStr = "antimatter";
	EXPECT_EQ(str_utf8_find_nocase(pStr, "I", &pEnd), pStr + str_length("ant"));
	EXPECT_EQ(pEnd, pStr + str_length("anti"));
	EXPECT_EQ(str_utf8_find_nocase(pStr, "İ", &pEnd), pStr + str_length("ant"));
	EXPECT_EQ(pEnd, pStr + str_length("anti"));
	pStr = "ANTIMATTER";
	EXPECT_EQ(str_utf8_find_nocase(pStr, "i", &pEnd), pStr + str_length("ANT"));
	EXPECT_EQ(pEnd, pStr + str_length("ANTI"));
	pStr = "ANTİMATTER";
	EXPECT_EQ(str_utf8_find_nocase(pStr, "i", &pEnd), pStr + str_length("ANT"));
	EXPECT_EQ(pEnd, pStr + str_length("ANTİ"));
}

TEST(StrUtf8, Utf8FixTruncation)
{
	char aaBuf[][32] = {
		"",
		"\xff",
		"abc",
		"abc\xff",
		"blub\xffxyz",
		"привет Наташа\xff",
		"до свидания\xffОлег",
	};
	const char *apExpected[] = {
		"",
		"",
		"abc",
		"abc",
		"blub\xffxyz",
		"привет Наташа",
		"до свидания\xffОлег",
	};
	for(unsigned i = 0; i < std::size(aaBuf); i++)
	{
		EXPECT_EQ(str_utf8_fix_truncation(aaBuf[i]), str_length(apExpected[i]));
		EXPECT_STREQ(aaBuf[i], apExpected[i]);
	}
}

TEST(StrUtf8, Utf8Stats)
{
	size_t Size, Count;

	str_utf8_stats("abc", 4, 3, &Size, &Count);
	EXPECT_EQ(Size, 3);
	EXPECT_EQ(Count, 3);

	str_utf8_stats("abc", 2, 3, &Size, &Count);
	EXPECT_EQ(Size, 1);
	EXPECT_EQ(Count, 1);

	str_utf8_stats("", 1, 0, &Size, &Count);
	EXPECT_EQ(Size, 0);
	EXPECT_EQ(Count, 0);

	str_utf8_stats("abcde", 6, 5, &Size, &Count);
	EXPECT_EQ(Size, 5);
	EXPECT_EQ(Count, 5);

	str_utf8_stats("любовь", 13, 6, &Size, &Count);
	EXPECT_EQ(Size, 12);
	EXPECT_EQ(Count, 6);

	str_utf8_stats("abc愛", 7, 4, &Size, &Count);
	EXPECT_EQ(Size, 6);
	EXPECT_EQ(Count, 4);

	str_utf8_stats("abc愛", 6, 4, &Size, &Count);
	EXPECT_EQ(Size, 3);
	EXPECT_EQ(Count, 3);

	str_utf8_stats("любовь", 13, 3, &Size, &Count);
	EXPECT_EQ(Size, 6);
	EXPECT_EQ(Count, 3);
}

TEST(StrUtf8, Utf8OffsetBytesToChars)
{
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("", 0), 0);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("", 100), 0);

	EXPECT_EQ(str_utf8_offset_bytes_to_chars("abc", 0), 0);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("abc", 1), 1);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("abc", 2), 2);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("abc", 3), 3);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("abc", 100), 3);

	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 0), 0);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 2), 1);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 4), 2);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 6), 3);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 8), 4);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 10), 5);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 12), 6);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("любовь", 100), 6);

	EXPECT_EQ(str_utf8_offset_bytes_to_chars("DDNet最好了", 5), 5);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("DDNet最好了", 8), 6);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("DDNet最好了", 11), 7);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("DDNet最好了", 14), 8);
	EXPECT_EQ(str_utf8_offset_bytes_to_chars("DDNet最好了", 100), 8);
}

TEST(StrUtf8, Utf8OffsetCharsToBytes)
{
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("", 0), 0);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("", 100), 0);

	EXPECT_EQ(str_utf8_offset_chars_to_bytes("abc", 0), 0);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("abc", 1), 1);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("abc", 2), 2);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("abc", 3), 3);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("abc", 100), 3);

	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 0), 0);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 1), 2);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 2), 4);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 3), 6);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 4), 8);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 5), 10);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 6), 12);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("любовь", 100), 12);

	EXPECT_EQ(str_utf8_offset_chars_to_bytes("DDNet最好了", 5), 5);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("DDNet最好了", 6), 8);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("DDNet最好了", 7), 11);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("DDNet最好了", 8), 14);
	EXPECT_EQ(str_utf8_offset_chars_to_bytes("DDNet最好了", 100), 14);
}
