// 使用实际 SFNT 记录与 FreeType face 验证名称，不读取生产源码。
#include <engine/client/qm_font_name_match.h>
#include <engine/client/qm_font_names.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <iterator>
#include <vector>

TEST(QmFontNames, DecodesUnicodeChineseAndSupplementaryNames)
{
	FT_Byte aBytes[] = {0x4E, 0x2D, 0x65, 0x87, 0xD8, 0x3D, 0xDE, 0x00};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aBytes;
	Name.string_len = sizeof(aBytes);
	EXPECT_EQ(QmDecodeSfntName(Name), "中文😀");
	Name.platform_id = 0;
	EXPECT_EQ(QmDecodeSfntName(Name), "中文😀");
}

TEST(QmFontNames, MalformedUtf16AndUnsafeLegacyNamesAreRejected)
{
	FT_Byte aBytes[] = {0xD8, 0x3D, 0x00, 0x41};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aBytes;
	Name.string_len = sizeof(aBytes);
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	Name.string_len = 3;
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	Name.platform_id = 1;
	Name.encoding_id = 0;
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
}

TEST(QmFontNames, MissingUnicodeRecordsUseOnlyValidUtf8Fallback)
{
	FT_Library Library = nullptr;
	ASSERT_EQ(FT_Init_FreeType(&Library), 0);
	std::ifstream File(TestSourcePath("data/fonts/DejaVuSans.ttf"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<unsigned char> Bytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	FT_Face Face = nullptr;
	ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), 0, &Face), 0);
	EXPECT_EQ(QmFontSfntName(Face, 65000, "中文字体"), "中文字体");
	const char aInvalid[] = {static_cast<char>(0xFF), 0};
	EXPECT_TRUE(QmFontSfntName(Face, 65000, aInvalid).empty());
	const SQmFontFaceNames Names = QmFontFaceNames(Face);
	EXPECT_EQ(Names.m_Family, "DejaVu Sans");
	EXPECT_EQ(Names.m_Style, "Book");
	FT_Done_Face(Face);
	FT_Done_FreeType(Library);
}

TEST(QmFontNames, EveryCollectionFaceHasStableUtf8FamilyWithoutDuplicateFiles)
{
	FT_Library Library = nullptr;
	ASSERT_EQ(FT_Init_FreeType(&Library), 0);
	std::ifstream File(TestSourcePath("data/fonts/SourceHanSans.ttc"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<unsigned char> Bytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	FT_Face Collection = nullptr;
	ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), -1, &Collection), 0);
	const FT_Long Count = Collection->num_faces;
	FT_Done_Face(Collection);
	ASSERT_GT(Count, 1);
	bool FoundChinese = false;
	for(FT_Long Index = 0; Index < Count; ++Index)
	{
		SCOPED_TRACE(Index);
		FT_Face Face = nullptr;
		ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), Index, &Face), 0);
		const SQmFontFaceNames Names = QmFontFaceNames(Face);
		EXPECT_FALSE(Names.m_Family.empty());
		EXPECT_TRUE(str_utf8_check(Names.m_Family.c_str()));
		EXPECT_TRUE(str_utf8_check(Names.m_Style.c_str()));
		FoundChinese |= Names.m_Family == "Source Han Sans SC";
		FT_Done_Face(Face);
	}
	EXPECT_TRUE(FoundChinese);
	FT_Done_FreeType(Library);
}

TEST(QmFontNames, QuestionMarkPlaceholdersCannotHideUnicodeNames)
{
	FT_Byte aPlaceholder[] = {0, '?', 0, '?', 0, '?', 0, ' ', 0, '?'};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aPlaceholder;
	Name.string_len = sizeof(aPlaceholder);
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	EXPECT_FALSE(QmFontNameUsable("??? ???"));
	EXPECT_FALSE(QmFontNameUsable("bad�name"));
	EXPECT_TRUE(QmFontNameUsable("Font? Regular"));
	EXPECT_TRUE(QmFontNameUsable("中文字体"));
	EXPECT_TRUE(QmFontSfntName(nullptr, 1, "Fallback").empty());
	EXPECT_TRUE(QmFontFaceNames(nullptr).m_Family.empty());
}

TEST(QmFontNames, LegacyCompactFamilyStyleMatchesOnlyExactNormalizedNames)
{
	EXPECT_TRUE(QmFontNamesEqual("DejaVuSans Book", "DejaVu Sans Book"));
	EXPECT_TRUE(QmFontNamesEqual("dejavu-sans-book", "DejaVu Sans Book"));
	EXPECT_FALSE(QmFontNamesEqual("DejaVuSansExtra Book", "DejaVu Sans Book"));
	EXPECT_FALSE(QmFontNamesEqual("SourceHanSansSC", "Source Han Sans TC"));
}
