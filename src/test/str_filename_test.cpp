#include "str_test_helpers.h"

#include <base/mem.h>
#include <base/str.h>
#include <base/windows.h>

#include <engine/server/server.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(StrFilename, SanitizeFilename)
{
	TestInplace<str_sanitize_filename>("", "");
	TestInplace<str_sanitize_filename>("a", "a");
	TestInplace<str_sanitize_filename>("Merhaba dünya!", "Merhaba dünya!");
	TestInplace<str_sanitize_filename>("привет Наташа", "привет Наташа");
	TestInplace<str_sanitize_filename>("ąçęįǫų", "ąçęįǫų");
	TestInplace<str_sanitize_filename>("DDNet最好了", "DDNet最好了");
	TestInplace<str_sanitize_filename>("aβい🐘", "aβい🐘");
	TestInplace<str_sanitize_filename>("foo.bar", "foo.bar");
	TestInplace<str_sanitize_filename>("foo.bar.baz", "foo.bar.baz");
	TestInplace<str_sanitize_filename>(".a..b...c....d", ".a..b...c....d");
	TestInplace<str_sanitize_filename>("\n", " ");
	TestInplace<str_sanitize_filename>("\r", " ");
	TestInplace<str_sanitize_filename>("\t", " ");
	TestInplace<str_sanitize_filename>("a\n", "a ");
	TestInplace<str_sanitize_filename>("a\rb", "a b");
	TestInplace<str_sanitize_filename>("\tb", " b");
	TestInplace<str_sanitize_filename>("\n\n", "  ");
	TestInplace<str_sanitize_filename>("\x1C", " ");
	TestInplace<str_sanitize_filename>("\x1D", " ");
	TestInplace<str_sanitize_filename>("\x1E", " ");
	TestInplace<str_sanitize_filename>("\x1F", " ");
	TestInplace<str_sanitize_filename>("\u007F", " ");
	TestInplace<str_sanitize_filename>("\\", " ");
	TestInplace<str_sanitize_filename>("/", " ");
	TestInplace<str_sanitize_filename>("|", " ");
	TestInplace<str_sanitize_filename>(":", " ");
	TestInplace<str_sanitize_filename>("*", " ");
	TestInplace<str_sanitize_filename>("?", " ");
	TestInplace<str_sanitize_filename>("<", " ");
	TestInplace<str_sanitize_filename>(">", " ");
	TestInplace<str_sanitize_filename>("\"", " ");
	TestInplace<str_sanitize_filename>("\\/|:*?<>\"", "         ");
}

TEST(StrFilename, ValidFilename)
{
	EXPECT_TRUE(str_valid_filename("a"));
	EXPECT_TRUE(str_valid_filename("abc"));
	EXPECT_TRUE(str_valid_filename("abc abc"));
	EXPECT_TRUE(str_valid_filename("aa bb ccc dddd eeeee"));
	EXPECT_TRUE(str_valid_filename("öüä"));
	EXPECT_TRUE(str_valid_filename("привет Наташа"));
	EXPECT_TRUE(str_valid_filename("ąçęįǫų"));
	EXPECT_TRUE(str_valid_filename("DDNet最好了"));
	EXPECT_TRUE(str_valid_filename("aβい🐘"));
	EXPECT_TRUE(str_valid_filename("foo.bar"));
	EXPECT_TRUE(str_valid_filename("foo.bar.baz"));
	EXPECT_TRUE(str_valid_filename(".a..b...c....d"));

	EXPECT_FALSE(str_valid_filename(""));
	EXPECT_FALSE(str_valid_filename("aa\nbb"));
	EXPECT_FALSE(str_valid_filename("aa\rbb"));
	EXPECT_FALSE(str_valid_filename("aa\tbb"));
	EXPECT_FALSE(str_valid_filename("aa\u001Cbb"));
	EXPECT_FALSE(str_valid_filename("aa\u001Dbb"));
	EXPECT_FALSE(str_valid_filename("aa\u001Ebb"));
	EXPECT_FALSE(str_valid_filename("aa\u001Fbb"));
	EXPECT_FALSE(str_valid_filename("aa\u007Fbb"));
	EXPECT_FALSE(str_valid_filename("aa\\bb"));
	EXPECT_FALSE(str_valid_filename("aa/bb"));
	EXPECT_FALSE(str_valid_filename("aa|bb"));
	EXPECT_FALSE(str_valid_filename("aa:bb"));
	EXPECT_FALSE(str_valid_filename("aa*bb"));
	EXPECT_FALSE(str_valid_filename("aa?bb"));
	EXPECT_FALSE(str_valid_filename("aa<bb"));
	EXPECT_FALSE(str_valid_filename("aa>bb"));
	EXPECT_FALSE(str_valid_filename("aa\"bb"));
	EXPECT_FALSE(str_valid_filename("\\/|:*?<>\""));
	EXPECT_FALSE(str_valid_filename("aa bb")); // EM QUAD
	EXPECT_FALSE(str_valid_filename("aa​bb")); // ZERO WIDTH SPACE
	EXPECT_FALSE(str_valid_filename(" abc"));
	EXPECT_FALSE(str_valid_filename("   abc"));
	EXPECT_FALSE(str_valid_filename("abc "));
	EXPECT_FALSE(str_valid_filename("abc   "));
	EXPECT_FALSE(str_valid_filename("abc   abc"));
	EXPECT_FALSE(str_valid_filename(" abc abc "));
	EXPECT_FALSE(str_valid_filename("   abc   abc   "));
	EXPECT_FALSE(str_valid_filename("abc."));
	EXPECT_FALSE(str_valid_filename("abc..."));
	EXPECT_FALSE(str_valid_filename("abc... "));
	EXPECT_FALSE(str_valid_filename("abc ..."));

	// reserved names
	EXPECT_FALSE(str_valid_filename("con"));
	EXPECT_FALSE(str_valid_filename("CON"));
	EXPECT_FALSE(str_valid_filename("cOn"));
	EXPECT_FALSE(str_valid_filename("con.txt"));
	EXPECT_FALSE(str_valid_filename("con.tar.gz"));
	EXPECT_FALSE(str_valid_filename("CON.TAR.GZ"));
	EXPECT_FALSE(str_valid_filename("PRN"));
	EXPECT_FALSE(str_valid_filename("AUX"));
	EXPECT_FALSE(str_valid_filename("NUL"));
	EXPECT_FALSE(str_valid_filename("COM4"));
	EXPECT_FALSE(str_valid_filename("lpt²"));
	// reserved names allowed as prefix if not separated by period
	EXPECT_TRUE(str_valid_filename("console"));
	EXPECT_TRUE(str_valid_filename("console.log"));
	EXPECT_TRUE(str_valid_filename("console.tar.gz"));
	EXPECT_TRUE(str_valid_filename("Auxiliary"));
	EXPECT_TRUE(str_valid_filename("Null"));
	EXPECT_TRUE(str_valid_filename("Null.txt"));
}

TEST(StrFilename, CompFilename)
{
	EXPECT_EQ(str_comp_filenames("a", "a"), 0);
	EXPECT_LT(str_comp_filenames("a", "b"), 0);
	EXPECT_GT(str_comp_filenames("b", "a"), 0);
	EXPECT_EQ(str_comp_filenames("A", "a"), 0);
	EXPECT_LT(str_comp_filenames("A", "b"), 0);
	EXPECT_GT(str_comp_filenames("b", "A"), 0);
	EXPECT_LT(str_comp_filenames("a", "B"), 0);
	EXPECT_GT(str_comp_filenames("B", "a"), 0);
	EXPECT_EQ(str_comp_filenames("1A", "1a"), 0);
	EXPECT_LT(str_comp_filenames("1a", "1B"), 0);
	EXPECT_GT(str_comp_filenames("1B", "1a"), 0);
	EXPECT_LT(str_comp_filenames("1a", "1b"), 0);
	EXPECT_GT(str_comp_filenames("1b", "1a"), 0);
	EXPECT_GT(str_comp_filenames("12a", "1B"), 0);
	EXPECT_LT(str_comp_filenames("1B", "12a"), 0);
	EXPECT_GT(str_comp_filenames("10a", "1B"), 0);
	EXPECT_LT(str_comp_filenames("1B", "10a"), 0);
	EXPECT_GT(str_comp_filenames("10a", "00B"), 0);
	EXPECT_LT(str_comp_filenames("00B", "10a"), 0);
	EXPECT_GT(str_comp_filenames("10a", "09B"), 0);
	EXPECT_LT(str_comp_filenames("09B", "10a"), 0);
	EXPECT_LT(str_comp_filenames("abc", "abcd"), 0);
	EXPECT_GT(str_comp_filenames("abcd", "abc"), 0);
	EXPECT_LT(str_comp_filenames("abc2", "abcd1"), 0);
	EXPECT_GT(str_comp_filenames("abcd1", "abc2"), 0);
	EXPECT_LT(str_comp_filenames("abc50", "abcd"), 0);
	EXPECT_GT(str_comp_filenames("abcd", "abc50"), 0);
	EXPECT_EQ(str_comp_filenames("file0", "file0"), 0);
	EXPECT_LT(str_comp_filenames("file0", "file1"), 0);
	EXPECT_GT(str_comp_filenames("file1", "file0"), 0);
	EXPECT_LT(str_comp_filenames("file1", "file09"), 0);
	EXPECT_GT(str_comp_filenames("file09", "file1"), 0);
	EXPECT_LT(str_comp_filenames("file1", "file009"), 0);
	EXPECT_GT(str_comp_filenames("file009", "file1"), 0);
	EXPECT_GT(str_comp_filenames("file10", "file00"), 0);
	EXPECT_LT(str_comp_filenames("file00", "file10"), 0);
	EXPECT_GT(str_comp_filenames("file10", "file09"), 0);
	EXPECT_LT(str_comp_filenames("file09", "file10"), 0);
	EXPECT_LT(str_comp_filenames("file13", "file37"), 0);
	EXPECT_GT(str_comp_filenames("file37", "file13"), 0);
	EXPECT_LT(str_comp_filenames("file1.ext", "file09.ext"), 0);
	EXPECT_GT(str_comp_filenames("file09.ext", "file1.ext"), 0);
	EXPECT_LT(str_comp_filenames("file1.ext", "file009.ext"), 0);
	EXPECT_GT(str_comp_filenames("file009.ext", "file1.ext"), 0);
	EXPECT_EQ(str_comp_filenames("file0.ext", "file0.ext"), 0);
	EXPECT_LT(str_comp_filenames("file13.ext", "file37.ext"), 0);
	EXPECT_GT(str_comp_filenames("file37.ext", "file13.ext"), 0);
	EXPECT_LT(str_comp_filenames("FILE13.EXT", "file37.ext"), 0);
	EXPECT_GT(str_comp_filenames("file37.ext", "FILE13.EXT"), 0);
	EXPECT_GT(str_comp_filenames("file10.ext", "file00.ext"), 0);
	EXPECT_LT(str_comp_filenames("file00.ext", "file10.ext"), 0);
	EXPECT_GT(str_comp_filenames("file10.ext", "file09.ext"), 0);
	EXPECT_LT(str_comp_filenames("file09.ext", "file10.ext"), 0);
	EXPECT_LT(str_comp_filenames("file42", "file1337"), 0);
	EXPECT_GT(str_comp_filenames("file1337", "file42"), 0);
	EXPECT_LT(str_comp_filenames("file42.ext", "file1337.ext"), 0);
	EXPECT_GT(str_comp_filenames("file1337.ext", "file42.ext"), 0);
	EXPECT_GT(str_comp_filenames("file4414520", "file2055"), 0);
	EXPECT_LT(str_comp_filenames("file4414520", "file205523151812419"), 0);
	EXPECT_LT(str_comp_filenames("file1", "file1a"), 0);
	EXPECT_GT(str_comp_filenames("file1a", "file1"), 0);
	EXPECT_LT(str_comp_filenames("Kobra 1", "Kobra 1 v2"), 0);
	EXPECT_GT(str_comp_filenames("Kobra 1 v2", "Kobra 1"), 0);
	EXPECT_LT(str_comp_filenames("Kobra 1 v2", "Kobra 1 v3"), 0);
	EXPECT_GT(str_comp_filenames("Kobra 1 v3", "Kobra 1 v2"), 0);
}
