// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiMarkdown.h"

#include <base/system.h>

#include <algorithm>
#include <cstddef>

namespace
{
	// 代码围栏用转义写法，避免源码里出现三个连续反引号干扰阅读与工具链。
	constexpr const char *FENCE = "\x60\x60\x60";

	std::string TrimCopy(const std::string &Text)
	{
		size_t Begin = 0;
		size_t End = Text.size();
		while(Begin < End && static_cast<unsigned char>(Text[Begin]) <= ' ')
			++Begin;
		while(End > Begin && static_cast<unsigned char>(Text[End - 1]) <= ' ')
			--End;
		return Text.substr(Begin, End - Begin);
	}

	bool StartsWith(const std::string &Text, const char *pPrefix)
	{
		const size_t Length = str_length(pPrefix);
		return Text.size() >= Length && Text.compare(0, Length, pPrefix) == 0;
	}

	// ---、***、___ 都当作分割线；至少三个同样字符。
	bool IsDivider(const std::string &Text)
	{
		if(Text.size() < 3)
			return false;
		const char Ch = Text[0];
		if(Ch != '-' && Ch != '*' && Ch != '_')
			return false;
		int Count = 0;
		for(const char Current : Text)
		{
			if(Current == Ch)
				++Count;
			else if(Current != ' ')
				return false;
		}
		return Count >= 3;
	}

	int CountIndent(const std::string &Text)
	{
		int Spaces = 0;
		for(const char Ch : Text)
		{
			if(Ch == ' ')
				++Spaces;
			else if(Ch == '\t')
				Spaces += 2;
			else
				break;
		}
		return Spaces / 2;
	}

	// 支持 "- item"、"* item"、"1. item"、"2) item"，并要求标记后必须有空格。
	bool MatchListMarker(const std::string &Trimmed, bool *pOrdered, int *pOrder)
	{
		*pOrdered = false;
		*pOrder = 1;
		if(Trimmed.size() >= 2 && (Trimmed[0] == '-' || Trimmed[0] == '*' || Trimmed[0] == '+') && Trimmed[1] == ' ')
			return true;
		size_t Digits = 0;
		while(Digits < Trimmed.size() && Trimmed[Digits] >= '0' && Trimmed[Digits] <= '9')
			++Digits;
		if(Digits == 0 || Digits > 4 || Digits + 1 >= Trimmed.size())
			return false;
		if(Trimmed[Digits] != '.' && Trimmed[Digits] != ')')
			return false;
		if(Trimmed[Digits + 1] != ' ')
			return false;
		*pOrdered = true;
		int Order = 0;
		for(size_t Index = 0; Index < Digits; ++Index)
			Order = Order * 10 + (Trimmed[Index] - '0');
		*pOrder = Order > 0 ? Order : 1;
		return true;
	}

	bool ParseImageLine(const std::string &Trimmed, std::string *pAlt, std::string *pPath)
	{
		if(!StartsWith(Trimmed, "!["))
			return false;
		const size_t CloseBracket = Trimmed.find(']', 2);
		if(CloseBracket == std::string::npos || CloseBracket + 1 >= Trimmed.size() || Trimmed[CloseBracket + 1] != '(')
			return false;
		const size_t CloseParen = Trimmed.rfind(')');
		if(CloseParen == std::string::npos || CloseParen < CloseBracket + 2)
			return false;
		std::string Path = TrimCopy(Trimmed.substr(CloseBracket + 2, CloseParen - CloseBracket - 2));
		// 允许 "path 说明" 的可选标题，这里只保留路径本身。
		const size_t Space = Path.find(' ');
		if(Space != std::string::npos)
			Path = Path.substr(0, Space);
		if(Path.empty())
			return false;
		*pAlt = TrimCopy(Trimmed.substr(2, CloseBracket - 2));
		*pPath = Path;
		return true;
	}

	bool IsTableSeparator(const std::string &Trimmed)
	{
		if(Trimmed.empty() || Trimmed[0] != '|')
			return false;
		bool HasDash = false;
		for(const char Ch : Trimmed)
		{
			if(Ch == '-')
				HasDash = true;
			else if(Ch != '|' && Ch != ':' && Ch != ' ')
				return false;
		}
		return HasDash;
	}

	void SplitTableRow(const std::string &Trimmed, std::vector<std::vector<SWikiSpan>> *pCells)
	{
		std::string Body = Trimmed;
		if(!Body.empty() && Body.front() == '|')
			Body.erase(0, 1);
		if(!Body.empty() && Body.back() == '|')
			Body.pop_back();
		size_t Start = 0;
		while(true)
		{
			const size_t End = Body.find('|', Start);
			const std::string Cell = TrimCopy(Body.substr(Start, End == std::string::npos ? std::string::npos : End - Start));
			pCells->emplace_back();
			CWikiMarkdown::ParseInline(Cell, &pCells->back());
			if(End == std::string::npos)
				break;
			Start = End + 1;
		}
	}

	std::vector<char> ParseTableAligns(const std::string &Trimmed)
	{
		std::vector<std::vector<SWikiSpan>> vCells;
		SplitTableRow(Trimmed, &vCells);
		std::vector<char> vAligns;
		vAligns.reserve(vCells.size());
		for(const auto &Cell : vCells)
		{
			std::string Text;
			for(const SWikiSpan &Span : Cell)
				Text += Span.m_Text;
			const bool Left = !Text.empty() && Text.front() == ':';
			const bool Right = !Text.empty() && Text.back() == ':';
			vAligns.push_back(Left && Right ? 'c' : (Right ? 'r' : 'l'));
		}
		return vAligns;
	}

	bool IsHeadingLine(const std::string &Trimmed, int *pLevel, std::string *pText)
	{
		int Level = 0;
		while(Level < static_cast<int>(Trimmed.size()) && Trimmed[Level] == '#')
			++Level;
		if(Level < 1 || Level > 6)
			return false;
		if(Level < static_cast<int>(Trimmed.size()) && Trimmed[Level] != ' ')
			return false;
		*pLevel = Level;
		*pText = TrimCopy(Level < static_cast<int>(Trimmed.size()) ? Trimmed.substr(Level) : std::string());
		return true;
	}

	bool IsBlockStart(const std::string &Trimmed)
	{
		if(Trimmed.empty())
			return true;
		if(StartsWith(Trimmed, FENCE))
			return true;
		if(StartsWith(Trimmed, "$$") || IsDivider(Trimmed) || StartsWith(Trimmed, "![") || Trimmed[0] == '|' || Trimmed[0] == '>')
			return true;
		int Level = 0;
		std::string HeadingText;
		if(IsHeadingLine(Trimmed, &Level, &HeadingText))
			return true;
		bool Ordered = false;
		int Order = 1;
		return MatchListMarker(Trimmed, &Ordered, &Order);
	}

	void ParseInlineInto(const std::string &Text, unsigned BaseStyle, std::vector<SWikiSpan> *pOut)
	{
		std::string Plain;
		const auto FlushPlain = [&]() {
			if(!Plain.empty())
			{
				pOut->push_back({Plain, BaseStyle, {}});
				Plain.clear();
			}
		};

		size_t Index = 0;
		while(Index < Text.size())
		{
			const char Ch = Text[Index];
			if(Ch == '\\' && Index + 1 < Text.size())
			{
				Plain.push_back(Text[Index + 1]);
				Index += 2;
				continue;
			}
			if(Ch == '*' && Index + 1 < Text.size() && Text[Index + 1] == '*')
			{
				const size_t Close = Text.find("**", Index + 2);
				if(Close != std::string::npos && Close > Index + 2)
				{
					FlushPlain();
					ParseInlineInto(Text.substr(Index + 2, Close - Index - 2), BaseStyle | WIKI_INLINE_BOLD, pOut);
					Index = Close + 2;
					continue;
				}
			}
			if(Ch == '`')
			{
				const size_t Close = Text.find('`', Index + 1);
				if(Close != std::string::npos && Close > Index + 1)
				{
					FlushPlain();
					pOut->push_back({Text.substr(Index + 1, Close - Index - 1), BaseStyle | WIKI_INLINE_CODE, {}});
					Index = Close + 1;
					continue;
				}
			}
			if(Ch == '$' && Index + 1 < Text.size())
			{
				const size_t Close = Text.find('$', Index + 1);
				if(Close != std::string::npos && Close > Index + 1)
				{
					FlushPlain();
					const std::string Source = Text.substr(Index + 1, Close - Index - 1);
					pOut->push_back({Source, BaseStyle | WIKI_INLINE_MATH, Source});
					Index = Close + 1;
					continue;
				}
			}
			if(Ch == '[')
			{
				const size_t CloseBracket = Text.find(']', Index + 1);
				if(CloseBracket != std::string::npos && CloseBracket + 1 < Text.size() && Text[CloseBracket + 1] == '(')
				{
					const size_t CloseParen = Text.find(')', CloseBracket + 2);
					if(CloseParen != std::string::npos)
					{
						FlushPlain();
						const std::string Label = Text.substr(Index + 1, CloseBracket - Index - 1);
						const std::string Target = Text.substr(CloseBracket + 2, CloseParen - CloseBracket - 2);
						pOut->push_back({Label, BaseStyle | WIKI_INLINE_LINK, Target});
						Index = CloseParen + 1;
						continue;
					}
				}
			}
			// 斜体只在两侧都不是空格时生效，避免把 "2*3*4" 之类的普通文本吃掉。
			if((Ch == '*' || Ch == '_') && Index + 1 < Text.size() && static_cast<unsigned char>(Text[Index + 1]) > ' ')
			{
				const size_t Close = Text.find(Ch, Index + 1);
				if(Close != std::string::npos && Close > Index + 1 && static_cast<unsigned char>(Text[Close - 1]) > ' ')
				{
					FlushPlain();
					ParseInlineInto(Text.substr(Index + 1, Close - Index - 1), BaseStyle | WIKI_INLINE_ITALIC, pOut);
					Index = Close + 1;
					continue;
				}
			}
			// 多字节字符整段拷贝，避免按字节切坏 UTF-8。
			int Length = 1;
			const unsigned char Lead = static_cast<unsigned char>(Ch);
			if(Lead >= 0xF0)
				Length = 4;
			else if(Lead >= 0xE0)
				Length = 3;
			else if(Lead >= 0xC0)
				Length = 2;
			if(Index + Length > Text.size())
				Length = 1;
			Plain.append(Text, Index, Length);
			Index += Length;
		}
		FlushPlain();
	}
} // namespace

void CWikiMarkdown::ParseInline(const std::string &Text, std::vector<SWikiSpan> *pOut)
{
	if(pOut == nullptr)
		return;
	pOut->clear();
	if(Text.empty())
		return;
	ParseInlineInto(Text, 0, pOut);
}

bool CWikiMarkdown::Parse(const char *pMarkdown)
{
	m_vBlocks.clear();
	m_Title.clear();
	if(pMarkdown == nullptr)
		return false;

	// 统一换行，避免 Windows 换行在段落中被当成空行。
	std::vector<std::string> vLines;
	std::string Source(pMarkdown);
	size_t Start = 0;
	while(Start <= Source.size())
	{
		size_t End = Source.find('\n', Start);
		if(End == std::string::npos)
			End = Source.size();
		std::string Line = Source.substr(Start, End - Start);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		vLines.push_back(std::move(Line));
		if(End == Source.size())
			break;
		Start = End + 1;
	}

	size_t Index = 0;
	while(Index < vLines.size())
	{
		const std::string Trimmed = TrimCopy(vLines[Index]);
		if(Trimmed.empty())
		{
			++Index;
			continue;
		}

		// 代码块：围栏起始，内容原样保留。
		if(StartsWith(Trimmed, FENCE))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::CODE;
			++Index;
			while(Index < vLines.size() && !StartsWith(TrimCopy(vLines[Index]), FENCE))
			{
				if(!Block.m_Text.empty())
					Block.m_Text += '\n';
				Block.m_Text += vLines[Index];
				++Index;
			}
			if(Index < vLines.size())
				++Index;
			m_vBlocks.push_back(std::move(Block));
			continue;
		}

		// 数学块：$$ 单独成行，或 $$...$$ 单行写法。
		if(StartsWith(Trimmed, "$$"))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::MATH;
			std::string Body = TrimCopy(Trimmed.substr(2));
			if(Body.size() >= 2 && Body.compare(Body.size() - 2, 2, "$$") == 0)
			{
				Block.m_Text = TrimCopy(Body.substr(0, Body.size() - 2));
				++Index;
			}
			else
			{
				Body.clear();
				++Index;
				while(Index < vLines.size())
				{
					const std::string Inner = TrimCopy(vLines[Index]);
					if(Inner.size() >= 2 && Inner.compare(Inner.size() - 2, 2, "$$") == 0)
					{
						const std::string Head = TrimCopy(Inner.substr(0, Inner.size() - 2));
						if(!Head.empty())
						{
							if(!Body.empty())
								Body += '\n';
							Body += Head;
						}
						++Index;
						break;
					}
					if(!Body.empty())
						Body += '\n';
					Body += vLines[Index];
					++Index;
				}
				Block.m_Text = TrimCopy(Body);
			}
			m_vBlocks.push_back(std::move(Block));
			continue;
		}

		int HeadingLevel = 0;
		std::string HeadingText;
		if(IsHeadingLine(Trimmed, &HeadingLevel, &HeadingText))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::HEADING;
			Block.m_Level = HeadingLevel;
			ParseInline(HeadingText, &Block.m_Spans);
			if(HeadingLevel == 1 && m_Title.empty())
			{
				for(const SWikiSpan &Span : Block.m_Spans)
					m_Title += Span.m_Text;
			}
			m_vBlocks.push_back(std::move(Block));
			++Index;
			continue;
		}

		if(IsDivider(Trimmed))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::DIVIDER;
			m_vBlocks.push_back(std::move(Block));
			++Index;
			continue;
		}

		std::string AltText;
		std::string MediaPath;
		if(ParseImageLine(Trimmed, &AltText, &MediaPath))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::IMAGE;
			Block.m_AltText = AltText;
			Block.m_MediaPath = MediaPath;
			m_vBlocks.push_back(std::move(Block));
			++Index;
			continue;
		}

		if(Trimmed[0] == '|' && Index + 1 < vLines.size() && IsTableSeparator(TrimCopy(vLines[Index + 1])))
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::TABLE;
			Block.m_Rows.emplace_back();
			SplitTableRow(Trimmed, &Block.m_Rows.back().m_Cells);
			Block.m_vAligns = ParseTableAligns(TrimCopy(vLines[Index + 1]));
			Index += 2;
			while(Index < vLines.size())
			{
				const std::string Row = TrimCopy(vLines[Index]);
				if(Row.empty() || Row[0] != '|')
					break;
				Block.m_Rows.emplace_back();
				SplitTableRow(Row, &Block.m_Rows.back().m_Cells);
				++Index;
			}
			size_t Columns = Block.m_vAligns.size();
			for(const SWikiTableRow &Row : Block.m_Rows)
				Columns = std::max(Columns, Row.m_Cells.size());
			Block.m_vAligns.resize(Columns, 'l');
			// 列数补齐：缺的格子留空，绘制时不会错位。
			for(SWikiTableRow &Row : Block.m_Rows)
				Row.m_Cells.resize(Columns);
			m_vBlocks.push_back(std::move(Block));
			continue;
		}

		if(Trimmed[0] == '>')
		{
			SWikiBlock Block;
			Block.m_Type = EWikiBlockType::QUOTE;
			std::string Text;
			while(Index < vLines.size())
			{
				const std::string Inner = TrimCopy(vLines[Index]);
				if(Inner.empty() || Inner[0] != '>')
					break;
				const std::string Content = TrimCopy(Inner.substr(1));
				if(!Content.empty())
				{
					if(!Text.empty())
						Text += ' ';
					Text += Content;
				}
				++Index;
			}
			ParseInline(Text, &Block.m_Spans);
			m_vBlocks.push_back(std::move(Block));
			continue;
		}

		bool Ordered = false;
		int Order = 1;
		if(MatchListMarker(Trimmed, &Ordered, &Order))
		{
			SWikiBlock Block;
			Block.m_Type = Ordered ? EWikiBlockType::ORDERED : EWikiBlockType::BULLET;
			Block.m_Level = std::min(3, CountIndent(vLines[Index]));
			Block.m_Order = Order;
			const size_t MarkerLength = Ordered ? (Trimmed.find_first_of(".)") + 2) : 2;
			ParseInline(TrimCopy(MarkerLength < Trimmed.size() ? Trimmed.substr(MarkerLength) : std::string()), &Block.m_Spans);
			m_vBlocks.push_back(std::move(Block));
			++Index;
			continue;
		}

		// 普通段落：吃掉连续的非空行，用空格连接。
		SWikiBlock Block;
		Block.m_Type = EWikiBlockType::PARAGRAPH;
		std::string Text;
		while(Index < vLines.size())
		{
			const std::string Line = TrimCopy(vLines[Index]);
			if(Line.empty())
				break;
			if(!Text.empty() && IsBlockStart(Line))
				break;
			if(!Text.empty())
				Text += ' ';
			Text += Line;
			++Index;
		}
		if(Text.empty())
		{
			++Index;
			continue;
		}
		ParseInline(Text, &Block.m_Spans);
		m_vBlocks.push_back(std::move(Block));
	}

	return !m_vBlocks.empty();
}
