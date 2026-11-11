// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/QmUi/ColorPickerPalette.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>
#include <vector>

// 取色器「我的调色板」配置串编解码：行为测试（纯函数接口，无 UI 依赖）。

TEST(QmColorPickerPalette, EmptyConfigParsesToEmptyPalette)
{
	EXPECT_TRUE(qm_picker_palette::Parse("").empty());
	EXPECT_TRUE(qm_picker_palette::Parse(nullptr).empty());
	EXPECT_TRUE(qm_picker_palette::Parse(";;;").empty());
	EXPECT_TRUE(qm_picker_palette::Parse("没有分隔符").empty());
}

TEST(QmColorPickerPalette, ParsesNamesAndColors)
{
	const auto vSwatches = qm_picker_palette::Parse("红|#ff0000;蓝|#0000ff80");
	ASSERT_EQ(vSwatches.size(), 2u);
	EXPECT_EQ(vSwatches[0].m_aName, std::string("红"));
	EXPECT_EQ(vSwatches[0].m_Color, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_EQ(vSwatches[1].m_aName, std::string("蓝"));
	EXPECT_EQ(vSwatches[1].m_Color, ColorRGBA(0.0f, 0.0f, 1.0f, 128.0f / 255.0f));
}

TEST(QmColorPickerPalette, CorruptedEntriesAreSkipped)
{
	// 第二个条目缺分隔符、第三个颜色非法、第四个恢复，只保留首尾。
	const auto vSwatches = qm_picker_palette::Parse("好|#00ff00;坏条目;坏2|#zzzzzz;又好|#123456");
	ASSERT_EQ(vSwatches.size(), 2u);
	EXPECT_EQ(vSwatches[0].m_aName, std::string("好"));
	EXPECT_EQ(vSwatches[1].m_aName, std::string("又好"));
	EXPECT_EQ(vSwatches[1].m_Color, ColorRGBA(0x12 / 255.0f, 0x34 / 255.0f, 0x56 / 255.0f, 1.0f));
}

TEST(QmColorPickerPalette, SerializeRoundTripsNamesAndColors)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches;
	qm_picker_palette::SSwatch First;
	str_copy(First.m_aName, "我的颜色");
	First.m_Color = ColorRGBA(0.2f, 0.4f, 0.6f, 0.8f);
	qm_picker_palette::SSwatch Second;
	str_copy(Second.m_aName, "带;竖|线的名字");
	Second.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f);
	vSwatches.push_back(First);
	vSwatches.push_back(Second);

	char aBuf[4096];
	qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), vSwatches);
	const auto vParsed = qm_picker_palette::Parse(aBuf);
	ASSERT_EQ(vParsed.size(), 2u);
	EXPECT_EQ(vParsed[0].m_aName, std::string("我的颜色"));
	EXPECT_EQ(vParsed[0].m_Color, First.m_Color);
	// 分隔符在序列化前已被替换，往返后名称保持稳定。
	EXPECT_EQ(vParsed[1].m_aName, std::string("带 竖 线的名字"));
	EXPECT_EQ(vParsed[1].m_Color, Second.m_Color);
}

TEST(QmColorPickerPalette, SerializeRespectsBufferLimit)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches;
	for(int i = 0; i < qm_picker_palette::MAX_SWATCHES; i++)
	{
		qm_picker_palette::SSwatch Swatch;
		str_format(Swatch.m_aName, sizeof(Swatch.m_aName), "颜色 %d", i);
		Swatch.m_Color = ColorRGBA(0.5f, 0.5f, 0.5f, 1.0f);
		vSwatches.push_back(Swatch);
	}

	char aBuf[64]; // 只装得下少数条目
	qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), vSwatches);
	EXPECT_LT(str_length(aBuf), 64);
	EXPECT_EQ(aBuf[str_length(aBuf)], '\0');
	// 截断后的串仍能完整解析出整数个条目。
	const auto vParsed = qm_picker_palette::Parse(aBuf);
	EXPECT_GT(vParsed.size(), 0u);
	EXPECT_LT((int)vParsed.size(), qm_picker_palette::MAX_SWATCHES);
}

TEST(QmColorPickerPalette, ParseCapsSwatchCount)
{
	char aBuf[4096];
	aBuf[0] = '\0';
	// 条目数超过上限才能验证“解析截断”，上限本身也是被测对象的一部分。
	for(int i = 0; i < qm_picker_palette::MAX_SWATCHES + 8; i++)
	{
		char aEntry[64];
		str_format(aEntry, sizeof(aEntry), "%s色%d|#112233", i > 0 ? ";" : "", i);
		str_append(aBuf, aEntry, (int)sizeof(aBuf));
	}
	const auto vSwatches = qm_picker_palette::Parse(aBuf);
	EXPECT_EQ(vSwatches.size(), (size_t)qm_picker_palette::MAX_SWATCHES);
}

TEST(QmColorPickerPalette, NextUniqueNameSkipsTakenNumbersAndReservedSwatch)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches(3);
	str_copy(vSwatches[0].m_aName, "自定义 1");
	str_copy(vSwatches[1].m_aName, "自定义 3");
	str_copy(vSwatches[2].m_aName, "红");
	qm_picker_palette::SSwatch Reserved{};
	str_copy(Reserved.m_aName, "自定义 2");

	char aName[qm_picker_palette::MAX_NAME_BYTES];
	qm_picker_palette::NextUniqueName(aName, (int)sizeof(aName), "自定义", vSwatches, nullptr, -1);
	EXPECT_EQ(aName, std::string("自定义 2"));

	// 撤销缓冲里的「自定义 2」也要避开：否则撤回后会出现两个同名声块。
	qm_picker_palette::NextUniqueName(aName, (int)sizeof(aName), "自定义", vSwatches, &Reserved, -1);
	EXPECT_EQ(aName, std::string("自定义 4"));

	// 前缀不同互不影响。
	qm_picker_palette::NextUniqueName(aName, (int)sizeof(aName), "颜色", vSwatches, &Reserved, -1);
	EXPECT_EQ(aName, std::string("颜色 1"));

	// 正在改名的条目跳过自己：清空名字后回退名从它自己的位置重新取号。
	qm_picker_palette::NextUniqueName(aName, (int)sizeof(aName), "自定义", vSwatches, nullptr, 1);
	EXPECT_EQ(aName, std::string("自定义 2"));
}

TEST(QmColorPickerPalette, ParseHexColorSupportsShorthand)
{
	ColorRGBA Color;
	EXPECT_TRUE(qm_picker_palette::ParseHexColor("#f00", Color));
	EXPECT_EQ(Color, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));

	EXPECT_TRUE(qm_picker_palette::ParseHexColor("0f0", Color));
	EXPECT_EQ(Color, ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f));

	EXPECT_TRUE(qm_picker_palette::ParseHexColor("#00f8", Color));
	EXPECT_EQ(Color, ColorRGBA(0.0f, 0.0f, 1.0f, 136.0f / 255.0f));
}

TEST(QmColorPickerPalette, ParseHexColorRejectsMalformedHex)
{
	ColorRGBA Color;
	const char *apInvalid[] = {"", "#", "##ff0000", "12345", "1234567", "123456789", "ff00g0", "ff0000zz", "ff0000 ", "0x112233"};
	for(const char *pInvalid : apInvalid)
	{
		SCOPED_TRACE(pInvalid);
		EXPECT_FALSE(qm_picker_palette::ParseHexColor(pInvalid, Color));
	}
	EXPECT_FALSE(qm_picker_palette::ParseHexColor(nullptr, Color));
}

TEST(QmColorPickerPalette, ParseHexColorAcceptsUppercaseAndAlphaBoundaries)
{
	ColorRGBA Color;
	ASSERT_TRUE(qm_picker_palette::ParseHexColor("#FF000080", Color));
	EXPECT_EQ(Color, ColorRGBA(1.0f, 0.0f, 0.0f, 128.0f / 255.0f));

	ASSERT_TRUE(qm_picker_palette::ParseHexColor("A1B2C3", Color));
	EXPECT_EQ(Color, ColorRGBA(0xA1 / 255.0f, 0xB2 / 255.0f, 0xC3 / 255.0f, 1.0f));

	// 全透明黑与全不透明白都不能被当成“没有 alpha”或丢失字节。
	ASSERT_TRUE(qm_picker_palette::ParseHexColor("#00000000", Color));
	EXPECT_EQ(Color, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
	ASSERT_TRUE(qm_picker_palette::ParseHexColor("#FFFFFFFF", Color));
	EXPECT_EQ(Color, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	// 4 位简写的末位是 alpha，不是被忽略的多余字符。
	ASSERT_TRUE(qm_picker_palette::ParseHexColor("#f00F", Color));
	EXPECT_EQ(Color, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST(QmColorPickerPalette, SanitizeNameReplacesBothSeparatorsAndStopsAtNul)
{
	char aName[] = "a;b|c";
	qm_picker_palette::SanitizeName(aName, (int)sizeof(aName));
	EXPECT_STREQ(aName, "a b c");
	// 重复消毒不应再改任何字节。
	qm_picker_palette::SanitizeName(aName, (int)sizeof(aName));
	EXPECT_STREQ(aName, "a b c");

	// 只扫到 NUL 为止：终止符之后的字节属于别处，不能被改写。
	char aEmbedded[] = {'x', ';', 'y', '\0', ';', 'z', '\0'};
	qm_picker_palette::SanitizeName(aEmbedded, (int)sizeof(aEmbedded));
	EXPECT_STREQ(aEmbedded, "x y");
	EXPECT_EQ(aEmbedded[4], ';');
	EXPECT_EQ(aEmbedded[5], 'z');
}

TEST(QmColorPickerPalette, SerializeEmptyPaletteWritesEmptyString)
{
	char aBuf[32];
	memset(aBuf, 'x', sizeof(aBuf));
	qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), {});
	EXPECT_EQ(aBuf[0], '\0');
}

TEST(QmColorPickerPalette, SerializeWritesNoPartialEntryWhenFirstEntryDoesNotFit)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches(1);
	str_copy(vSwatches[0].m_aName, "abc");
	vSwatches[0].m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

	char aBuf[16];
	memset(aBuf, 'x', sizeof(aBuf));
	// 条目 "abc|#ffffffff" 共 13 字节，加 NUL 需要 14；13 字节缓冲一个字符都不能写。
	qm_picker_palette::Serialize(aBuf, 13, vSwatches);
	EXPECT_EQ(aBuf[0], '\0');

	qm_picker_palette::Serialize(aBuf, 14, vSwatches);
	EXPECT_STREQ(aBuf, "abc|#ffffffff");
	EXPECT_EQ(aBuf[13], '\0');

	qm_picker_palette::Serialize(aBuf, 1, vSwatches);
	EXPECT_EQ(aBuf[0], '\0');
}

TEST(QmColorPickerPalette, SerializeParseRoundTripAtFullCapacity)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches;
	const auto Add = [&](const char *pName, const ColorRGBA &Color) {
		qm_picker_palette::SSwatch Swatch;
		str_copy(Swatch.m_aName, pName);
		Swatch.m_Color = Color;
		vSwatches.push_back(Swatch);
	};
	// 边界名字：刚好占满 27 字节、含分隔符、纯多字节、空名字。
	Add(std::string(qm_picker_palette::MAX_NAME_BYTES - 1, 'A').c_str(), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
	Add((std::string(qm_picker_palette::MAX_NAME_BYTES - 2, 'a') + ";").c_str(), ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f / 255.0f));
	Add("中中中中中中中中中", ColorRGBA(1.0f, 0.0f, 0.0f, 128.0f / 255.0f));
	Add("", ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	for(int i = 4; i < qm_picker_palette::MAX_SWATCHES; i++)
	{
		char aName[qm_picker_palette::MAX_NAME_BYTES];
		str_format(aName, sizeof(aName), "色%d", i);
		// 颜色分量必须落在 8 位字节网格上：Serialize 按截断取整，0.5 会变成 127/255。
		const int AlphaByte = i % 3 == 0 ? 0 : (i % 3 == 1 ? 128 : 255);
		Add(aName, ColorRGBA(i / 255.0f, (255 - i) / 255.0f, ((i * 7) % 256) / 255.0f, AlphaByte / 255.0f));
	}

	char aBuf[4096];
	qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), vSwatches);
	ASSERT_LT(str_length(aBuf), (int)sizeof(aBuf) - 1);

	const auto vParsed = qm_picker_palette::Parse(aBuf);
	ASSERT_EQ(vParsed.size(), (size_t)qm_picker_palette::MAX_SWATCHES);
	EXPECT_EQ(vParsed[0].m_aName, std::string(qm_picker_palette::MAX_NAME_BYTES - 1, 'A'));
	EXPECT_EQ(vParsed[1].m_aName, std::string(qm_picker_palette::MAX_NAME_BYTES - 2, 'a') + " ");
	EXPECT_EQ(vParsed[2].m_aName, std::string("中中中中中中中中中"));
	EXPECT_STREQ(vParsed[3].m_aName, "");
	for(int i = 0; i < qm_picker_palette::MAX_SWATCHES; i++)
	{
		SCOPED_TRACE(i);
		EXPECT_EQ(vParsed[i].m_Color, vSwatches[i].m_Color);
	}
}

TEST(QmColorPickerPalette, ParseTruncatesNameToMaxNameBytes)
{
	const std::string Overlong(qm_picker_palette::MAX_NAME_BYTES + 2, 'A');
	const auto vParsed = qm_picker_palette::Parse((Overlong + "|#ff0000;b|#00ff00").c_str());
	ASSERT_EQ(vParsed.size(), 2u);
	EXPECT_EQ(vParsed[0].m_aName, std::string(qm_picker_palette::MAX_NAME_BYTES - 1, 'A'));
	EXPECT_EQ(vParsed[0].m_Color, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_STREQ(vParsed[1].m_aName, "b");
	EXPECT_EQ(vParsed[1].m_Color, ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(QmColorPickerPalette, ParseKeepsEntryWithEmptyName)
{
	const auto vParsed = qm_picker_palette::Parse("|#ff0000;red|#00ff00");
	ASSERT_EQ(vParsed.size(), 2u);
	EXPECT_STREQ(vParsed[0].m_aName, "");
	EXPECT_EQ(vParsed[0].m_Color, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_STREQ(vParsed[1].m_aName, "red");
}

TEST(QmColorPickerPalette, SerializeClampsOutOfRangeComponents)
{
	std::vector<qm_picker_palette::SSwatch> vSwatches(1);
	str_copy(vSwatches[0].m_aName, "x");
	vSwatches[0].m_Color = ColorRGBA(1.5f, -0.2f, 0.5f, 2.0f);

	char aBuf[64];
	qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), vSwatches);
	const auto vParsed = qm_picker_palette::Parse(aBuf);
	ASSERT_EQ(vParsed.size(), 1u);
	// 越界分量被压回 [0,1]；颜色按截断取整，0.5 → 127。
	EXPECT_EQ(vParsed[0].m_Color, ColorRGBA(1.0f, 0.0f, 127.0f / 255.0f, 1.0f));
}

TEST(QmColorPickerPalette, ParseScansEntriesWithinSemicolonBoundary)
{
	// 首个条目是数 KB 的无分隔符名字：搜索 '|' 必须限制在条目内，
	// 否则会跨过分号读到后面条目的竖线，颜色片段长度变负并越界写。
	char aBuf[4096];
	aBuf[0] = '\0';
	const std::string Giant(3527, 'J');
	str_append(aBuf, Giant.c_str(), (int)sizeof(aBuf));
	for(int i = 0; i < 47; i++)
	{
		char aEntry[32];
		str_format(aEntry, sizeof(aEntry), ";s%d|#112233", i);
		str_append(aBuf, aEntry, (int)sizeof(aBuf));
	}
	str_append(aBuf, ";tail|#1122334", (int)sizeof(aBuf));
	ASSERT_LT(str_length(aBuf), (int)sizeof(aBuf));

	const auto vParsed = qm_picker_palette::Parse(aBuf);
	ASSERT_EQ(vParsed.size(), 47u);
	for(int i = 0; i < 47; i++)
	{
		SCOPED_TRACE(i);
		char aExpected[8];
		str_format(aExpected, sizeof(aExpected), "s%d", i);
		EXPECT_STREQ(vParsed[i].m_aName, aExpected);
		EXPECT_EQ(vParsed[i].m_Color, ColorRGBA(0x11 / 255.0f, 0x22 / 255.0f, 0x33 / 255.0f, 1.0f));
	}
}
