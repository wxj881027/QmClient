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
			if(Line.size() < 3 || (Line[0] != '-' && Line[0] != '*' && Line[0] != '_'))
				return false;
			for(const char Char : Line)
				if(Char != Line[0])
					return false;
			return true;
		}

		bool ParseSettingsDirective(const std::string &Text, size_t Offset, SBlock &Out, size_t &OutLength)
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
			if(CardId.empty())
				return false;
			Out.m_Kind = EBlockKind::SETTINGS_BUTTON;
			Out.m_SettingsCardId = CardId;
			Out.m_SettingsLabel = Separator == std::string::npos ? std::string() : Trim(Inner.substr(Separator + 1));
			OutLength = End + 2 - Offset;
			return true;
		}

		void AppendSpan(std::vector<SSpan> &vSpans, SSpan Span, size_t &SpanCount, const SParseLimits &Limits)
		{
			if(Span.m_Text.empty() || SpanCount >= Limits.m_MaxSpans)
				return;
			if(!vSpans.empty())
			{
				SSpan &Last = vSpans.back();
				if(Last.m_Bold == Span.m_Bold && Last.m_Italic == Span.m_Italic && Last.m_Code == Span.m_Code && Last.m_Link == Span.m_Link)
				{
					Last.m_Text += Span.m_Text;
					return;
				}
			}
			vSpans.push_back(std::move(Span));
			++SpanCount;
		}

		void ParseInline(const std::string &Line, std::vector<SSpan> &vSpans, std::vector<SBlock> &vButtons, size_t &SpanCount, const SParseLimits &Limits)
		{
			std::string Pending;
			auto Flush = [&](SSpan Style) {
				if(!Pending.empty())
				{
					Style.m_Text = std::move(Pending);
					AppendSpan(vSpans, std::move(Style), SpanCount, Limits);
					Pending.clear();
				}
			};
			for(size_t Index = 0; Index < Line.size() && SpanCount < Limits.m_MaxSpans;)
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
					if(ParseSettingsDirective(Line, Index, Button, Consumed) && vButtons.size() < Limits.m_MaxBlocks)
					{
						Flush({});
						vButtons.push_back(std::move(Button));
						Index += Consumed;
						continue;
					}
				}
				if(Char == '!' && Index + 1 < Line.size() && Line[Index + 1] == '[')
				{
					const size_t CloseBracket = Line.find(']', Index + 2);
					if(CloseBracket != std::string::npos && CloseBracket + 1 < Line.size() && Line[CloseBracket + 1] == '(')
					{
						const size_t CloseParen = Line.find(')', CloseBracket + 2);
						if(CloseParen != std::string::npos)
						{
							// 图片不加载也不可点击，按普通文本保留，避免远端内容触发资源请求。
							Pending += Line.substr(Index, CloseParen + 1 - Index);
							Index = CloseParen + 1;
							continue;
						}
					}
				}
				if(Char == '[')
				{
					const size_t CloseBracket = Line.find(']', Index + 1);
					if(CloseBracket != std::string::npos && CloseBracket + 1 < Line.size() && Line[CloseBracket + 1] == '(')
					{
						const size_t CloseParen = Line.find(')', CloseBracket + 2);
						if(CloseParen != std::string::npos)
						{
							const std::string Label = Line.substr(Index + 1, CloseBracket - Index - 1);
							const std::string Url = Trim(Line.substr(CloseBracket + 2, CloseParen - CloseBracket - 2));
							if(!Label.empty() && IsLinkUrl(Url))
							{
								Flush({});
								AppendSpan(vSpans, {Label, false, false, false, Url}, SpanCount, Limits);
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
						AppendSpan(vSpans, {Line.substr(Index + 1, Close - Index - 1), false, false, true, {}}, SpanCount, Limits);
						Index = Close + 1;
						continue;
					}
				}
				if(Char == '*')
				{
					const bool Bold = Index + 1 < Line.size() && Line[Index + 1] == '*';
					const size_t MarkerLength = Bold ? 2 : 1;
					const size_t Close = Line.find(Bold ? "**" : "*", Index + MarkerLength);
					if(Close != std::string::npos && Close > Index + MarkerLength)
					{
						Flush({});
						AppendSpan(vSpans, {Line.substr(Index + MarkerLength, Close - Index - MarkerLength), Bold, !Bold, false, {}}, SpanCount, Limits);
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
			size_t Length = Lead >= 0xF0 ? 4 : Lead >= 0xE0 ? 3 :
						   Lead >= 0xC0         ? 2 :
									  1;
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
		std::string Text(pMarkdown);
		if(Text.size() > Limits.m_MaxBytes)
			Text.resize(Limits.m_MaxBytes);
		size_t SpanCount = 0;
		int NumberedIndex = 0;
		for(size_t Begin = 0; Begin <= Text.size() && vBlocks.size() < Limits.m_MaxBlocks;)
		{
			const size_t End = Text.find_first_of("\r\n", Begin);
			const std::string Line = Trim(Text.substr(Begin, End == std::string::npos ? std::string::npos : End - Begin));
			if(End == std::string::npos)
				Begin = Text.size() + 1;
			else
			{
				Begin = End + 1;
				if(Text[End] == '\r' && Begin < Text.size() && Text[Begin] == '\n')
					++Begin;
			}
			if(Line.empty())
			{
				NumberedIndex = 0;
				continue;
			}
			SBlock Block;
			std::string Content = Line;
			if(IsSeparatorLine(Line))
				Block.m_Kind = EBlockKind::SEPARATOR;
			else if(StartsWith(Line, "### ") || StartsWith(Line, "#### "))
			{
				Block.m_Kind = EBlockKind::HEADING3;
				Content = Line.substr(Line[3] == '#' ? 5 : 4);
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
				if(Digits > 0 && Digits + 1 < Line.size() && Line[Digits] == '.' && Line[Digits + 1] == ' ')
				{
					Block.m_Kind = EBlockKind::NUMBERED;
					Block.m_Number = ++NumberedIndex;
					Content = Line.substr(Digits + 2);
				}
				else
					NumberedIndex = 0;
			}
			std::vector<SBlock> vButtons;
			if(Block.m_Kind != EBlockKind::SEPARATOR)
				ParseInline(Content, Block.m_vSpans, vButtons, SpanCount, Limits);
			if(Block.m_Kind == EBlockKind::SEPARATOR || !Block.m_vSpans.empty())
				vBlocks.push_back(std::move(Block));
			for(SBlock &Button : vButtons)
			{
				if(vBlocks.size() >= Limits.m_MaxBlocks)
					break;
				vBlocks.push_back(std::move(Button));
			}
		}
		return vBlocks;
	}
}
