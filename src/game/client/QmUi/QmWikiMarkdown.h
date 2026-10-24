// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMWIKIMARKDOWN_H
#define GAME_CLIENT_QMUI_QMWIKIMARKDOWN_H

#include <string>
#include <vector>

// Wiki 文案使用“简易 Markdown”：足够写说明、图片、表格和公式，
// 但不追求 CommonMark 全兼容。解析结果只描述结构和内联样式，
// 排版与绘制交给 QmWikiMarkdownRender，便于单元测试和复用。
enum class EWikiBlockType
{
	PARAGRAPH,
	HEADING,
	BULLET,
	ORDERED,
	QUOTE,
	DIVIDER,
	CODE,
	TABLE,
	IMAGE,
	MATH,
};

enum
{
	WIKI_INLINE_BOLD = 1 << 0,
	WIKI_INLINE_ITALIC = 1 << 1,
	WIKI_INLINE_CODE = 1 << 2,
	WIKI_INLINE_LINK = 1 << 3,
	WIKI_INLINE_MATH = 1 << 4,
};

struct SWikiSpan
{
	std::string m_Text;
	unsigned m_Style = 0;
	// 链接保存目标地址；数学片段保存 TeX 源，供预渲染图片查找。
	std::string m_Target;
};

struct SWikiTableRow
{
	std::vector<std::vector<SWikiSpan>> m_Cells;
};

struct SWikiBlock
{
	EWikiBlockType m_Type = EWikiBlockType::PARAGRAPH;
	// 标题级别（1 起）或列表缩进层级（0 起）。
	int m_Level = 0;
	// 有序列表的显示序号，从 1 起。
	int m_Order = 1;
	std::vector<SWikiSpan> m_Spans;
	std::vector<SWikiTableRow> m_Rows;
	// 每列对齐：'l' / 'c' / 'r'。
	std::vector<char> m_vAligns;
	// 图片/动画的相对路径与替代文本。
	std::string m_MediaPath;
	std::string m_AltText;
	// 代码块与数学块的原文（保留换行）。
	std::string m_Text;
};

class CWikiMarkdown
{
public:
	// 解析整篇文档；失败时清空结果并返回 false（当前只在空文档时发生）。
	bool Parse(const char *pMarkdown);
	const std::vector<SWikiBlock> &Blocks() const { return m_vBlocks; }
	// 文档第一个一级标题，没有则为空。
	const std::string &Title() const { return m_Title; }

	// 单独解析一段内联文本，供表格单元格等复用。
	static void ParseInline(const std::string &Text, std::vector<SWikiSpan> *pOut);

private:
	std::string m_Title;
	std::vector<SWikiBlock> m_vBlocks;
};

#endif
