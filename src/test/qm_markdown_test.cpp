#include "test.h"

#include <game/client/components/qmclient/qm_markdown.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
	std::string JoinedText(const std::vector<qm_md::SSpan> &vSpans)
	{
		std::string Result;
		for(const qm_md::SSpan &Span : vSpans)
			Result += Span.m_Text;
		return Result;
	}

	const qm_md::SBlock *FindBlock(const std::vector<qm_md::SBlock> &vBlocks, qm_md::EBlockKind Kind, size_t Occurrence = 0)
	{
		size_t Seen = 0;
		for(const qm_md::SBlock &Block : vBlocks)
		{
			if(Block.m_Kind != Kind)
				continue;
			if(Seen == Occurrence)
				return &Block;
			++Seen;
		}
		return nullptr;
	}
}

TEST(QmMarkdown, RecognizesSupportedBlockKinds)
{
	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse(
		"# 一级\n"
		"## 二级\n"
		"### 三级\n"
		"普通段落\n"
		"- 无序\n"
		"1. 有序\n"
		"> 引用\n"
		"---\n");

	ASSERT_EQ(vBlocks.size(), 8u);
	EXPECT_EQ(vBlocks[0].m_Kind, qm_md::EBlockKind::HEADING1);
	EXPECT_EQ(JoinedText(vBlocks[0].m_vSpans), "一级");
	EXPECT_EQ(vBlocks[1].m_Kind, qm_md::EBlockKind::HEADING2);
	EXPECT_EQ(vBlocks[2].m_Kind, qm_md::EBlockKind::HEADING3);
	EXPECT_EQ(vBlocks[3].m_Kind, qm_md::EBlockKind::PARAGRAPH);
	EXPECT_EQ(vBlocks[4].m_Kind, qm_md::EBlockKind::BULLET);
	EXPECT_EQ(vBlocks[5].m_Kind, qm_md::EBlockKind::NUMBERED);
	EXPECT_EQ(vBlocks[5].m_Number, 1);
	EXPECT_EQ(vBlocks[6].m_Kind, qm_md::EBlockKind::QUOTE);
	EXPECT_EQ(vBlocks[7].m_Kind, qm_md::EBlockKind::SEPARATOR);
}

TEST(QmMarkdown, ParsesInlineStylesAndLinks)
{
	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse("普通 **加粗** *斜体* `代码` [官网](https://qmclient.icu) 结尾\n");
	ASSERT_EQ(vBlocks.size(), 1u);
	const std::vector<qm_md::SSpan> &vSpans = vBlocks[0].m_vSpans;
	ASSERT_GE(vSpans.size(), 5u);
	EXPECT_EQ(JoinedText(vSpans), "普通 加粗 斜体 代码 官网 结尾");

	bool Bold = false;
	bool Italic = false;
	bool Code = false;
	bool Link = false;
	for(const qm_md::SSpan &Span : vSpans)
	{
		Bold = Bold || (Span.m_Bold && Span.m_Text == "加粗");
		Italic = Italic || (Span.m_Italic && Span.m_Text == "斜体");
		Code = Code || (Span.m_Code && Span.m_Text == "代码");
		Link = Link || (Span.m_Link == "https://qmclient.icu" && Span.m_Text == "官网");
	}
	EXPECT_TRUE(Bold);
	EXPECT_TRUE(Italic);
	EXPECT_TRUE(Code);
	EXPECT_TRUE(Link);
}

TEST(QmMarkdown, RejectsUnsafeLinksAndKeepsUnknownSyntaxAsText)
{
	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse(
		"[坏链接](javascript:alert(1)) [文件](file:///etc/passwd)\n"
		"<script>alert(1)</script>\n"
		"![图片](https://example.com/a.png)\n");

	std::string All;
	for(const qm_md::SBlock &Block : vBlocks)
	{
		for(const qm_md::SSpan &Span : Block.m_vSpans)
		{
			EXPECT_TRUE(Span.m_Link.empty() || Span.m_Link.rfind("http", 0) == 0);
			All += Span.m_Text;
		}
	}
	// 非法链接与图片退回纯文本，不产生可点击目标。
	EXPECT_NE(All.find("[坏链接](javascript:alert(1))"), std::string::npos);
	EXPECT_NE(All.find("<script>alert(1)</script>"), std::string::npos);
	EXPECT_NE(All.find("![图片](https://example.com/a.png)"), std::string::npos);
}

TEST(QmMarkdown, ParsesSettingsDirectiveAsOwnBlock)
{
	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse(
		"武器动画已支持装填翻转\n"
		"[[settings:qm:weapon_animation|打开设置]]\n"
		"入口：[[settings:qm:lyrics]]\n");

	ASSERT_EQ(vBlocks.size(), 4u);
	EXPECT_EQ(vBlocks[0].m_Kind, qm_md::EBlockKind::PARAGRAPH);
	ASSERT_EQ(vBlocks[1].m_Kind, qm_md::EBlockKind::SETTINGS_BUTTON);
	EXPECT_EQ(vBlocks[1].m_SettingsCardId, "qm:weapon_animation");
	EXPECT_EQ(vBlocks[1].m_SettingsLabel, "打开设置");
	ASSERT_EQ(vBlocks[2].m_Kind, qm_md::EBlockKind::PARAGRAPH);
	EXPECT_EQ(JoinedText(vBlocks[2].m_vSpans), "入口：");
	ASSERT_EQ(vBlocks[3].m_Kind, qm_md::EBlockKind::SETTINGS_BUTTON);
	EXPECT_EQ(vBlocks[3].m_SettingsCardId, "qm:lyrics");
	EXPECT_TRUE(vBlocks[3].m_SettingsLabel.empty());
}

TEST(QmMarkdown, NormalizesLineEndingsAndHandlesEdgeCases)
{
	EXPECT_TRUE(qm_md::Parse(nullptr).empty());
	EXPECT_TRUE(qm_md::Parse("").empty());
	EXPECT_TRUE(qm_md::Parse("\n\n   \n").empty());

	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse("a\r\nb\rc");
	ASSERT_EQ(vBlocks.size(), 3u);
	EXPECT_EQ(JoinedText(vBlocks[0].m_vSpans), "a");
	EXPECT_EQ(JoinedText(vBlocks[1].m_vSpans), "b");
	EXPECT_EQ(JoinedText(vBlocks[2].m_vSpans), "c");

	// 未闭合的标记保持原样，不吞字符。
	const std::vector<qm_md::SBlock> vUnclosed = qm_md::Parse("**没有闭合");
	ASSERT_EQ(vUnclosed.size(), 1u);
	EXPECT_EQ(JoinedText(vUnclosed[0].m_vSpans), "**没有闭合");

	// 数字序号在同一列表内递增，被普通段落打断后重新计数。
	const std::vector<qm_md::SBlock> vNumbers = qm_md::Parse("1. 甲\n2. 乙\n段落\n1. 丙\n");
	ASSERT_EQ(vNumbers.size(), 4u);
	EXPECT_EQ(vNumbers[0].m_Number, 1);
	EXPECT_EQ(vNumbers[1].m_Number, 2);
	EXPECT_EQ(vNumbers[3].m_Number, 1);
}

TEST(QmMarkdown, EnforcesParseLimits)
{
	qm_md::SParseLimits Limits;
	Limits.m_MaxBlocks = 3;
	Limits.m_MaxBytes = 32;
	const std::vector<qm_md::SBlock> vBlocks = qm_md::Parse("# 一\n# 二\n# 三\n# 四\n# 五\n", Limits);
	EXPECT_EQ(vBlocks.size(), 3u);

	std::string Huge;
	for(int i = 0; i < 100; ++i)
		Huge += "行内容很长很长的段落\n";
	const std::vector<qm_md::SBlock> vHuge = qm_md::Parse(Huge.c_str());
	EXPECT_LE(vHuge.size(), 400u);
}

TEST(QmMarkdown, SplitsUtf8ByCodepoint)
{
	const std::vector<std::string> vGlyphs = qm_md::SplitUtf8("a中🙂");
	ASSERT_EQ(vGlyphs.size(), 3u);
	EXPECT_EQ(vGlyphs[0], "a");
	EXPECT_EQ(vGlyphs[1], "中");
	EXPECT_EQ(vGlyphs[2], "🙂");

	std::string Rebuilt;
	for(const std::string &Glyph : vGlyphs)
		Rebuilt += Glyph;
	EXPECT_EQ(Rebuilt, "a中🙂");
}
