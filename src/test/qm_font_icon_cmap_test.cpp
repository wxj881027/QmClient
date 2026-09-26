// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/textrender.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	// 随包 Phosphor 字体必须真实包含 textrender.h 声明的每个图标码位：
	// 这里直接解析字体 cmap（format 4），再与 FONT_ICON_* 的编译期码位核对。
	uint16_t FontIconBeU16(const std::string &Data, size_t Offset)
	{
		return static_cast<uint16_t>((static_cast<unsigned char>(Data[Offset]) << 8) | static_cast<unsigned char>(Data[Offset + 1]));
	}

	uint32_t FontIconBeU32(const std::string &Data, size_t Offset)
	{
		return (static_cast<uint32_t>(static_cast<unsigned char>(Data[Offset])) << 24) |
		       (static_cast<uint32_t>(static_cast<unsigned char>(Data[Offset + 1])) << 16) |
		       (static_cast<uint32_t>(static_cast<unsigned char>(Data[Offset + 2])) << 8) |
		       static_cast<uint32_t>(static_cast<unsigned char>(Data[Offset + 3]));
	}

	std::string ReadFontIconBinaryFile(const char *pRelativePath)
	{
		std::ifstream File(TestSourcePath(pRelativePath), std::ios::binary);
		EXPECT_TRUE(File.good()) << TestSourcePath(pRelativePath);
		return std::string(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
	}

	// 只解析 cmap format 4，覆盖 data/fonts 下的两份 Phosphor 字体。
	size_t FontIconCmapSubtable(const std::string &Font)
	{
		EXPECT_GE(Font.size(), 12u);
		const uint16_t NumTables = FontIconBeU16(Font, 4);
		size_t CmapOffset = 0;
		for(uint16_t Table = 0; Table < NumTables; ++Table)
		{
			const size_t Record = 12 + 16u * Table;
			if(Record + 16 > Font.size())
				break;
			if(Font.compare(Record, 4, "cmap") == 0)
				CmapOffset = FontIconBeU32(Font, Record + 8);
		}
		EXPECT_NE(CmapOffset, 0u);
		if(CmapOffset == 0)
			return 0;

		const uint16_t NumSubtables = FontIconBeU16(Font, CmapOffset + 2);
		size_t Subtable = 0;
		for(uint16_t Index = 0; Index < NumSubtables; ++Index)
		{
			const size_t Record = CmapOffset + 4 + 8u * Index;
			if(Record + 8 > Font.size())
				break;
			const size_t Candidate = CmapOffset + FontIconBeU32(Font, Record + 4);
			if(Candidate + 2 > Font.size())
				continue;
			if(FontIconBeU16(Font, Candidate) == 4)
				Subtable = Candidate;
		}
		EXPECT_NE(Subtable, 0u);
		return Subtable;
	}

	int FontIconGlyphIndex(const std::string &Font, size_t Subtable, uint32_t Codepoint)
	{
		if(Subtable == 0 || Codepoint > 0xFFFF)
			return 0;
		const size_t SegCount = FontIconBeU16(Font, Subtable + 6) / 2;
		const size_t EndOffset = Subtable + 14;
		const size_t StartOffset = EndOffset + 2 * SegCount + 2;
		const size_t DeltaOffset = StartOffset + 2 * SegCount;
		const size_t RangeOffsetOffset = DeltaOffset + 2 * SegCount;

		for(size_t Segment = 0; Segment < SegCount; ++Segment)
		{
			const uint32_t End = FontIconBeU16(Font, EndOffset + 2 * Segment);
			const uint32_t Start = FontIconBeU16(Font, StartOffset + 2 * Segment);
			if(Codepoint < Start || Codepoint > End)
				continue;
			const int16_t Delta = static_cast<int16_t>(FontIconBeU16(Font, DeltaOffset + 2 * Segment));
			const uint16_t RangeOffset = FontIconBeU16(Font, RangeOffsetOffset + 2 * Segment);
			if(RangeOffset == 0)
				return static_cast<int>((Codepoint + Delta) & 0xFFFF);
			const size_t GlyphOffset = RangeOffsetOffset + 2 * Segment + RangeOffset + 2 * (Codepoint - Start);
			if(GlyphOffset + 2 > Font.size())
				return 0;
			const uint16_t Glyph = FontIconBeU16(Font, GlyphOffset);
			if(Glyph == 0)
				return 0;
			return static_cast<int>((Glyph + Delta) & 0xFFFF);
		}
		return 0;
	}

	std::vector<uint32_t> FontIconMappedCodepoints(const std::string &Font)
	{
		const size_t Subtable = FontIconCmapSubtable(Font);
		std::vector<uint32_t> Codepoints;
		for(uint32_t Codepoint = 0x20; Codepoint <= 0xFFFF; ++Codepoint)
		{
			if(FontIconGlyphIndex(Font, Subtable, Codepoint) != 0)
				Codepoints.push_back(Codepoint);
		}
		return Codepoints;
	}
}

TEST(QmFontIcons, CodepointsExistInShippedPhosphorFonts)
{
	// 所有随包图标常量必须落在 Phosphor 私有使用区（PUA，U+E000–U+F8FF），
	// 且其码位必须真实存在于随包的两份字体中。
	const std::vector<std::pair<const char *, const char *>> Icons = {
		{"PLUS", FontIcons::FONT_ICON_PLUS},
		{"MINUS", FontIcons::FONT_ICON_MINUS},
		{"LOCK", FontIcons::FONT_ICON_LOCK},
		{"MAGNIFYING_GLASS", FontIcons::FONT_ICON_MAGNIFYING_GLASS},
		{"HEART", FontIcons::FONT_ICON_HEART},
		{"HEART_CRACK", FontIcons::FONT_ICON_HEART_CRACK},
		{"STAR", FontIcons::FONT_ICON_STAR},
		{"XMARK", FontIcons::FONT_ICON_XMARK},
		{"CIRCLE", FontIcons::FONT_ICON_CIRCLE},
		{"ARROW_ROTATE_LEFT", FontIcons::FONT_ICON_ARROW_ROTATE_LEFT},
		{"ARROW_ROTATE_RIGHT", FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT},
		{"FLAG_CHECKERED", FontIcons::FONT_ICON_FLAG_CHECKERED},
		{"BAN", FontIcons::FONT_ICON_BAN},
		{"CIRCLE_CHEVRON_DOWN", FontIcons::FONT_ICON_CIRCLE_CHEVRON_DOWN},
		{"KEY", FontIcons::FONT_ICON_KEY},
		{"LANGUAGE", FontIcons::FONT_ICON_LANGUAGE},
		{"SQUARE_MINUS", FontIcons::FONT_ICON_SQUARE_MINUS},
		{"SQUARE_PLUS", FontIcons::FONT_ICON_SQUARE_PLUS},
		{"SORT_UP", FontIcons::FONT_ICON_SORT_UP},
		{"SORT_DOWN", FontIcons::FONT_ICON_SORT_DOWN},
		{"TRIANGLE_EXCLAMATION", FontIcons::FONT_ICON_TRIANGLE_EXCLAMATION},
		{"HOUSE", FontIcons::FONT_ICON_HOUSE},
		{"BOOKMARK", FontIcons::FONT_ICON_BOOKMARK},
		{"NEWSPAPER", FontIcons::FONT_ICON_NEWSPAPER},
		{"POWER_OFF", FontIcons::FONT_ICON_POWER_OFF},
		{"GEAR", FontIcons::FONT_ICON_GEAR},
		{"PEN_TO_SQUARE", FontIcons::FONT_ICON_PEN_TO_SQUARE},
		{"CLAPPERBOARD", FontIcons::FONT_ICON_CLAPPERBOARD},
		{"EARTH_AMERICAS", FontIcons::FONT_ICON_EARTH_AMERICAS},
		{"NETWORK_WIRED", FontIcons::FONT_ICON_NETWORK_WIRED},
	};

	std::vector<std::pair<std::string, uint32_t>> Codepoints;
	for(const auto &[Name, pLiteral] : Icons)
	{
		const unsigned char *pBytes = reinterpret_cast<const unsigned char *>(pLiteral);
		// 图标字面量统一为 3 字节 UTF-8 PUA 码位。
		ASSERT_EQ(std::strlen(pLiteral), 3u) << "FONT_ICON_" << Name;
		ASSERT_GE(pBytes[0], 0xE0) << "FONT_ICON_" << Name;
		ASSERT_LE(pBytes[0], 0xEF) << "FONT_ICON_" << Name;
		const uint32_t Codepoint =
			((static_cast<uint32_t>(pBytes[0]) & 0x0F) << 12) |
			((static_cast<uint32_t>(pBytes[1]) & 0x3F) << 6) |
			(static_cast<uint32_t>(pBytes[2]) & 0x3F);
		EXPECT_GE(Codepoint, 0xE000u) << "FONT_ICON_" << Name;
		EXPECT_LE(Codepoint, 0xF8FFu) << "FONT_ICON_" << Name;
		Codepoints.emplace_back(Name, Codepoint);
	}
	ASSERT_FALSE(Codepoints.empty());

	const std::string Regular = ReadFontIconBinaryFile("data/qmclient/fonts/Phosphor/Phosphor-Regular.ttf");
	const std::string Bold = ReadFontIconBinaryFile("data/qmclient/fonts/Phosphor/Phosphor-Bold.ttf");
	ASSERT_GT(Regular.size(), 12u);
	ASSERT_GT(Bold.size(), 12u);

	const std::vector<uint32_t> RegularCodepoints = FontIconMappedCodepoints(Regular);
	const std::vector<uint32_t> BoldCodepoints = FontIconMappedCodepoints(Bold);
	EXPECT_FALSE(RegularCodepoints.empty());
	EXPECT_FALSE(BoldCodepoints.empty());

	for(const auto &[Name, Codepoint] : Codepoints)
	{
		EXPECT_TRUE(std::find(RegularCodepoints.begin(), RegularCodepoints.end(), Codepoint) != RegularCodepoints.end())
			<< "FONT_ICON_" << Name << " U+" << std::hex << Codepoint << " 不在 Phosphor-Regular.ttf 中";
		EXPECT_TRUE(std::find(BoldCodepoints.begin(), BoldCodepoints.end(), Codepoint) != BoldCodepoints.end())
			<< "FONT_ICON_" << Name << " U+" << std::hex << Codepoint << " 不在 Phosphor-Bold.ttf 中";
	}
}
