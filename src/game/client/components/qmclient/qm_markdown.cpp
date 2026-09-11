// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_markdown.h"

#include <cstring>
#include <utility>

namespace qm_md
{
	namespace
	{
		bool IsBlank(char Char)
		{
			return Char == ' ' || Char == '\t';
		}

		std::string Trim(const std::string &Text)
		{
			size_t Begin = 0;
			size_t End = Text.size();
			while(Begin < End && IsBlank(Text[Begin]))
				++Begin;
			while(End > Begin && IsBlank(Text[End - 1]))
				--End;
			return Text.substr(Begin, End - Begin);
		}

		bool StartsWith(const std::string &Text, const char *pPrefix)
		{
			const size_t Length = std::strlen(pPrefix);
			return Text.size() >= Length && Text.compare(0, Length, pPrefix) == 0;
		}

		bool IsLinkUrl(const std::string &Url)
		{
			return StartsWith(Url, "http://") || StartsWith(Url, "https://");
		}

		bool IsSeparatorLine(const std::string &Line)
		{
			if(Line.size() < 3)
				return false;
			const char Char = Line[0];
			if(Char != '-' && Char != '*' && Char != '_')
				return false;
			for(const char LineChar : Line)
			{
				if(LineChar != Char)
					return false;
			}
			return true;
		}

		// 尝试解析一个 [[settings:卡片id|按钮文字]] 指令，成功时输出片段并返回消耗的长度。
		bool ParseSettingsDirective(const std::string &Text, size_t Offset, SBlock &OutBlock, size_t &OutLength)
		{
			constexpr const char *pPrefix = "[[settings:";
			const size_t PrefixLength = std::strlen(pPrefix);
			if(Text.compare(Offset, PrefixLength, pPrefix) != 0)
				return false;
			const size_t End = Text.find("]]", Offset + PrefixLength);
			if(End == std::string::npos)
				return false;

			const std::string Inner = Text.substr(Offset + PrefixLength, End - Offset - PrefixLength);
			const size_t Separator = Inner.find('|');
			const std::string CardId = Trim(Separator == std::string::npos ? Inner : Inner.substr(0, Separator));
			const std::string Label = Separator == std::string::npos ? std::string() : Trim(Inner.substr(Separator + 1));
			if(CardId.empty())
				return false;

			OutBlock.m_Kind = EBlockKind::SETTINGS_BUTTON;
			OutBlock.m_SettingsCardId = CardId;
			OutBlock.m_SettingsLabel = Label;
			OutLength = End + 2 - Offset;
			return true;
		}

		void AppendSpan(std::vector<SSpan> &vSpans, SSpan Span)
		{
			if(Span.m_Text.empty())
				return;
			// 相邻且样式相同的片段合并，减少渲染时的绘制次数。
			if(!vSpans.empty())
			{
				SSpan &Last = vSpans.back();
				if(Last.m_Bold == Span.m_Bold && Last.m_Italic == Span.m_Italic &&
					Last.m_Code == Span.m_Code && Last.m_Link == Span.m_Link)
				{
					Last.m_Text += Span.m_Text;
					return;
				}
			}
			vSpans.push_back(std::move(Span));
		}

		// 行内解析：粗体 / 斜体 / 行内代码 / 链接 / 设置指令。
		// pExtraBlocks 收集行内出现的设置按钮（渲染时排在正文之后）。
		void ParseInline(const std::string &Line, std::vector<SSpan> &vSpans, std::vector<SBlock> &vExtraBlocks, const SParseLimits &Limits)
		{
			std::string Pending;
			const auto Flush = [&](SSpan Style) {
				if(Pending.empty())
					return;
				Style.m_Text = Pending;
				AppendSpan(vSpans, std::move(Style));
				Pending.clear();
			};

			for(size_t Index = 0; Index < Line.size() && vSpans.size() < Limits.m_MaxSpans;)
			{
				const char Char = Line[Index];
				if(Char == '\\' && Index + 1 < Line.size())
				{
					Pending.push_back(Line[Index + 1]);
					Index += 2;
					continue;
				}
				if(Char == '[' && Index + 1 < Line.size() && Line[Index + 1] == '[')
				{
					SBlock Button;
					size_t Consumed = 0;
					if(ParseSettingsDirective(Line, Index, Button, Consumed) && vExtraBlocks.size() < Limits.m_MaxBlocks)
					{
						Flush({});
						vExtraBlocks.push_back(std::move(Button));
						Index += Consumed;
						continue;
					}
				}
				if(Char == '!')
				{
					// 图片语法不渲染图片：保留 alt 文字，丢弃链接，避免出现可点击的"图片"。
					const size_t Bracket = Index + 1;
					if(Bracket < Line.size() && Line[Bracket] == '[')
					{
						const size_t CloseBracket = Line.find(']', Bracket + 1);
						const bool HasTarget = CloseBracket != std::string::npos && CloseBracket + 1 < Line.size() && Line[CloseBracket + 1] == '(';
						if(HasTarget)
						{
							const size_t CloseParen = Line.find(')', CloseBracket + 2);
							if(CloseParen != std::string::npos)
							{
								Pending += Line.substr(Bracket + 1, CloseBracket - Bracket - 1);
								Index = CloseParen + 1;
								continue;
							}
						}
					}
				}
				if(Char == '[')
				{
					const size_t CloseBracket = Line.find(']', Index + 1);
					const bool HasTarget = CloseBracket != std::string::npos && CloseBracket + 1 < Line.size() && Line[CloseBracket + 1] == '(';
					if(HasTarget)
					{
						const size_t CloseParen = Line.find(')', CloseBracket + 2);
						if(CloseParen != std::string::npos)
						{
							const std::string Label = Line.substr(Index + 1, CloseBracket - Index - 1);
							const std::string Url = Trim(Line.substr(CloseBracket + 2, CloseParen - CloseBracket - 2));
							if(!Label.empty() && IsLinkUrl(Url))
							{
								Flush({});
								SSpan Link;
								Link.m_Text = Label;
								Link.m_Link = Url;
								AppendSpan(vSpans, std::move(Link));
								Index = CloseParen + 1;
								continue;
							}
						}
					}
				}
				if(Char == '`')
				{
					const size_t Close = Line.find('`', Index + 1);
					if(Close != std::string::npos && Close > Index + 1)
					{
						Flush({});
						SSpan Code;
						Code.m_Text = Line.substr(Index + 1, Close - Index - 1);
						Code.m_Code = true;
						AppendSpan(vSpans, std::move(Code));
						Index = Close + 1;
						continue;
					}
				}
				if(Char == '*')
				{
					const bool Bold = Index + 1 < Line.size() && Line[Index + 1] == '*';
					const char *pMarker = Bold ? "**" : "*";
					const size_t MarkerLength = Bold ? 2 : 1;
					const size_t Close = Line.find(pMarker, Index + MarkerLength);
					if(Close != std::string::npos && Close > Index + MarkerLength)
					{
						Flush({});
						SSpan Emphasis;
						Emphasis.m_Text = Line.substr(Index + MarkerLength, Close - Index - MarkerLength);
						Emphasis.m_Bold = Bold;
						Emphasis.m_Italic = !Bold;
						AppendSpan(vSpans, std::move(Emphasis));
						Index = Close + MarkerLength;
						continue;
					}
				}
				Pending.push_back(Char);
				++Index;
			}
			Flush({});
		}
	}

	std::vector<std::string> SplitUtf8(const std::string &Text)
	{
		std::vector<std::string> vResult;
		for(size_t Index = 0; Index < Text.size();)
		{
			const unsigned char Lead = (unsigned char)Text[Index];
			size_t Length = 1;
			if(Lead >= 0xF0)
				Length = 4;
			else if(Lead >= 0xE0)
				Length = 3;
			else if(Lead >= 0xC0)
				Length = 2;
			if(Index + Length > Text.size())
				Length = 1;
			vResult.push_back(Text.substr(Index, Length));
			Index += Length;
		}
		return vResult;
	}

	std::vector<SBlock> Parse(const char *pMarkdown, const SParseLimits &Limits)
	{
		std::vector<SBlock> vBlocks;
		if(pMarkdown == nullptr)
			return vBlocks;

		std::string Text = pMarkdown;
		if(Text.size() > Limits.m_MaxBytes)
			Text.resize(Limits.m_MaxBytes);
		// 归一化换行，兼容 CRLF 与单独的 CR。
		std::string Normalized;
		Normalized.reserve(Text.size());
		for(size_t Index = 0; Index < Text.size(); ++Index)
		{
			if(Text[Index] == '\r')
			{
				if(Index + 1 < Text.size() && Text[Index + 1] == '\n')
					continue;
				Normalized.push_back('\n');
				continue;
			}
			Normalized.push_back(Text[Index]);
		}

		std::vector<std::string> vLines;
		{
			std::string Current;
			for(const char Char : Normalized)
			{
				if(Char == '\n')
				{
					vLines.push_back(Current);
					Current.clear();
					continue;
				}
				Current.push_back(Char);
			}
			vLines.push_back(Current);
		}

		int NumberedIndex = 0;
		for(const std::string &RawLine : vLines)
		{
			if(vBlocks.size() >= Limits.m_MaxBlocks)
				break;
			const std::string Line = Trim(RawLine);
			if(Line.empty())
			{
				NumberedIndex = 0;
				continue;
			}

			std::vector<SBlock> vExtraBlocks;
			SBlock Block;
			std::string Content = Line;
			if(IsSeparatorLine(Line))
			{
				Block.m_Kind = EBlockKind::SEPARATOR;
			}
			else if(StartsWith(Line, "#### "))
			{
				Block.m_Kind = EBlockKind::HEADING3;
				Content = Line.substr(5);
			}
			else if(StartsWith(Line, "### "))
			{
				Block.m_Kind = EBlockKind::HEADING3;
				Content = Line.substr(4);
			}
			else if(StartsWith(Line, "## "))
			{
				Block.m_Kind = EBlockKind::HEADING2;
				Content = Line.substr(3);
			}
			else if(StartsWith(Line, "# "))
			{
				Block.m_Kind = EBlockKind::HEADING1;
				Content = Line.substr(2);
			}
			else if(StartsWith(Line, "> "))
			{
				Block.m_Kind = EBlockKind::QUOTE;
				Content = Line.substr(2);
			}
			else if(StartsWith(Line, "- ") || StartsWith(Line, "* ") || StartsWith(Line, "+ "))
			{
				Block.m_Kind = EBlockKind::BULLET;
				Content = Line.substr(2);
			}
			else
			{
				size_t Digits = 0;
				while(Digits < Line.size() && Line[Digits] >= '0' && Line[Digits] <= '9')
					++Digits;
				const bool Ordered = Digits > 0 && Digits + 1 < Line.size() && Line[Digits] == '.' && Line[Digits + 1] == ' ';
				if(Ordered)
				{
					++NumberedIndex;
					Block.m_Kind = EBlockKind::NUMBERED;
					Block.m_Number = NumberedIndex;
					Content = Line.substr(Digits + 2);
				}
				else
				{
					NumberedIndex = 0;
				}
			}

			if(Block.m_Kind != EBlockKind::SEPARATOR)
				ParseInline(Content, Block.m_vSpans, vExtraBlocks, Limits);
			// 整行只有跳转指令时不再压入空段落，避免渲染出空行。
			if(Block.m_Kind != EBlockKind::SEPARATOR && Block.m_vSpans.empty())
			{
				if(vExtraBlocks.empty())
					continue;
			}
			else
			{
				vBlocks.push_back(std::move(Block));
			}
			for(SBlock &Extra : vExtraBlocks)
			{
				if(vBlocks.size() >= Limits.m_MaxBlocks)
					break;
				vBlocks.push_back(std::move(Extra));
			}
		}
		return vBlocks;
	}
}
