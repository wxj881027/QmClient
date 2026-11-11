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
