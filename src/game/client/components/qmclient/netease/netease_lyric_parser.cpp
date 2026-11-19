#include "netease_lyric_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace NeteaseLyrics
{
	namespace
	{
		std::string_view Trim(std::string_view Value)
		{
			while(!Value.empty() && std::isspace((unsigned char)Value.front()) != 0)
				Value.remove_prefix(1);
			while(!Value.empty() && std::isspace((unsigned char)Value.back()) != 0)
				Value.remove_suffix(1);
			return Value;
		}

		bool ParseUnsigned(std::string_view Value, int64_t *pOut)
		{
			if(pOut == nullptr || Value.empty())
				return false;
			int64_t Number = 0;
			const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
			if(Result.ec != std::errc{} || Result.ptr != Value.data() + Value.size() || Number < 0)
				return false;
			*pOut = Number;
			return true;
		}

		bool ParseLrcTime(std::string_view Value, int64_t *pOut)
		{
			if(pOut == nullptr)
				return false;
			Value = Trim(Value);
			const size_t Colon = Value.find(':');
			if(Colon == std::string_view::npos || Colon == 0 || Colon + 1 >= Value.size())
				return false;
			int64_t Minutes = 0;
			if(!ParseUnsigned(Value.substr(0, Colon), &Minutes))
				return false;
			std::string_view SecondsPart = Value.substr(Colon + 1);
			const size_t Dot = SecondsPart.find_first_of(".,");
			std::string_view WholeSeconds = Dot == std::string_view::npos ? SecondsPart : SecondsPart.substr(0, Dot);
			if(WholeSeconds.empty())
				return false;
			int64_t Seconds = 0;
			if(!ParseUnsigned(WholeSeconds, &Seconds) || Seconds >= 60)
				return false;
			int64_t FractionMs = 0;
			if(Dot != std::string_view::npos)
			{
				const std::string_view Fraction = SecondsPart.substr(Dot + 1);
				if(Fraction.empty() || Fraction.size() > 3)
					return false;
				int64_t FractionValue = 0;
				if(!ParseUnsigned(Fraction, &FractionValue))
					return false;
				if(Fraction.size() == 1)
					FractionMs = FractionValue * 100;
				else if(Fraction.size() == 2)
					FractionMs = FractionValue * 10;
				else
					FractionMs = FractionValue;
			}
			if(Minutes > (std::numeric_limits<int64_t>::max() - Seconds * 1000 - FractionMs) / 60000)
				return false;
			*pOut = Minutes * 60000 + Seconds * 1000 + FractionMs;
			return true;
		}

		bool ParseYrcPair(std::string_view Value, int64_t *pFirst, int64_t *pSecond, bool *pHasThird = nullptr)
		{
			const size_t FirstComma = Value.find(',');
			if(FirstComma == std::string_view::npos)
				return false;
			const size_t SecondComma = Value.find(',', FirstComma + 1);
			if(pHasThird != nullptr)
				*pHasThird = SecondComma != std::string_view::npos;
			const std::string_view Second = SecondComma == std::string_view::npos ? Value.substr(FirstComma + 1) : Value.substr(FirstComma + 1, SecondComma - FirstComma - 1);
			if(!ParseUnsigned(Trim(Value.substr(0, FirstComma)), pFirst) || !ParseUnsigned(Trim(Second), pSecond))
				return false;
			if(SecondComma != std::string_view::npos)
			{
				int64_t Reserved = 0;
				if(!ParseUnsigned(Trim(Value.substr(SecondComma + 1)), &Reserved))
					return false;
			}
			return true;
		}

		size_t FindYrcWordMarker(std::string_view Body, size_t Offset)
		{
			for(size_t Open = Body.find('(', Offset); Open != std::string_view::npos; Open = Body.find('(', Open + 1))
			{
				size_t NumberEnd = Open + 1;
				while(NumberEnd < Body.size() && std::isspace((unsigned char)Body[NumberEnd]) != 0)
					++NumberEnd;
				const size_t NumberStart = NumberEnd;
				while(NumberEnd < Body.size() && Body[NumberEnd] >= '0' && Body[NumberEnd] <= '9')
					++NumberEnd;
				if(NumberEnd == NumberStart)
					continue;
				while(NumberEnd < Body.size() && std::isspace((unsigned char)Body[NumberEnd]) != 0)
					++NumberEnd;
				// 普通括号属于歌词正文；数字和逗号开头的标记仍交给解析器检查，保留损坏歌词的回退行为。
				if(NumberEnd < Body.size() && Body[NumberEnd] == ',')
					return Open;
			}
			return std::string_view::npos;
		}

		size_t Utf8Width(std::string_view Text, size_t Offset)
		{
			if(Offset >= Text.size())
				return 0;
			const uint8_t Lead = (uint8_t)Text[Offset];
			if(Lead <= 0x7F)
				return 1;
			size_t Width = 0;
			uint32_t Codepoint = 0;
			uint32_t Minimum = 0;
			if((Lead & 0xE0) == 0xC0)
			{
				Width = 2;
				Codepoint = Lead & 0x1F;
				Minimum = 0x80;
			}
			else if((Lead & 0xF0) == 0xE0)
			{
				Width = 3;
				Codepoint = Lead & 0x0F;
				Minimum = 0x800;
			}
			else if((Lead & 0xF8) == 0xF0)
			{
				Width = 4;
				Codepoint = Lead & 0x07;
				Minimum = 0x10000;
			}
			else
				return 0;
			if(Width > Text.size() - Offset)
				return 0;
			for(size_t Byte = 1; Byte < Width; ++Byte)
			{
				const uint8_t Continuation = (uint8_t)Text[Offset + Byte];
				if((Continuation & 0xC0) != 0x80)
					return 0;
				Codepoint = (Codepoint << 6) | (Continuation & 0x3F);
			}
			if(Codepoint < Minimum || Codepoint > 0x10FFFF || (Codepoint >= 0xD800 && Codepoint <= 0xDFFF))
				return 0;
			return Width;
		}

		bool FinishTimeline(std::vector<SLine> &&vParsed, STimeline *pOut, std::string *pError)
		{
			if(pOut == nullptr)
				return false;
			pOut->Clear();
			if(vParsed.empty())
			{
				if(pError)
					*pError = "no valid timed lyric lines";
				return false;
			}
			FinalizeTimelineLines(vParsed);
			pOut->m_vLines.reserve(vParsed.size());
			for(SLine &Parsed : vParsed)
			{
				if(Parsed.m_Text.empty())
					continue;
				SLine Line;
				Line.m_StartMs = Parsed.m_StartMs;
				Line.m_EndMs = Parsed.m_EndMs;
				Line.m_Text = std::move(Parsed.m_Text);
				Line.m_vWords = std::move(Parsed.m_vWords);
				pOut->m_vLines.push_back(std::move(Line));
			}
			pOut->m_HasTiming = !pOut->m_vLines.empty();
			if(!pOut->m_HasTiming && pError)
				*pError = "timed lines have no text";
			return pOut->m_HasTiming;
		}
	}

	void FinalizeTimelineLines(std::vector<SLine> &vLines)
	{
		FinalizeTimelineLines(vLines, [](auto &Line) -> auto & { return Line; });
	}

	bool IsValidUtf8(std::string_view Text)
	{
		for(size_t Index = 0; Index < Text.size();)
		{
			const size_t Width = Utf8Width(Text, Index);
			if(Width == 0)
				return false;
			Index += Width;
		}
		return true;
	}

	std::string TruncateUtf8(std::string_view Text, size_t MaxBytes)
	{
		if(MaxBytes == 0)
			return {};
		const size_t Limit = std::min(Text.size(), MaxBytes);
		size_t End = 0;
		while(End < Limit)
		{
			const size_t Width = Utf8Width(Text, End);
			if(Width == 0 || Width > Limit - End)
				break;
			End += Width;
		}
		return std::string(Text.substr(0, End));
	}

	bool ParseLrc(std::string_view Text, STimeline *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		pOut->Clear();
		if(!IsValidUtf8(Text))
		{
			if(pError)
				*pError = "invalid UTF-8";
			return false;
		}
		std::vector<SLine> vParsed;
		size_t Offset = 0;
		while(Offset <= Text.size())
		{
			const size_t Newline = Text.find('\n', Offset);
			std::string_view Line = Text.substr(Offset, Newline == std::string_view::npos ? Text.size() - Offset : Newline - Offset);
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			Line = Trim(Line);
			if(Line.size() >= 3 && (unsigned char)Line[0] == 0xEF && (unsigned char)Line[1] == 0xBB && (unsigned char)Line[2] == 0xBF)
				Line.remove_prefix(3);
			std::vector<int64_t> Times;
			std::string_view Probe = Line;
			while(!Probe.empty() && Probe.front() == '[')
			{
				const size_t Close = Probe.find(']');
				if(Close == std::string_view::npos)
					break;
				int64_t TimeMs = 0;
				if(!ParseLrcTime(Probe.substr(1, Close - 1), &TimeMs))
					break;
				Times.push_back(TimeMs);
				Probe = Trim(Probe.substr(Close + 1));
			}
			if(!Times.empty())
			{
				for(const int64_t TimeMs : Times)
					vParsed.push_back({TimeMs, -1, std::string(Probe), {}});
			}
			if(Newline == std::string_view::npos)
				break;
			Offset = Newline + 1;
		}
		return FinishTimeline(std::move(vParsed), pOut, pError);
	}

	bool ParseYrc(std::string_view Text, STimeline *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		pOut->Clear();
		if(!IsValidUtf8(Text))
		{
			if(pError)
				*pError = "invalid UTF-8";
			return false;
		}
		std::vector<SLine> vParsed;
		size_t Offset = 0;
		while(Offset <= Text.size())
		{
			const size_t Newline = Text.find('\n', Offset);
			std::string_view Line = Trim(Text.substr(Offset, Newline == std::string_view::npos ? Text.size() - Offset : Newline - Offset));
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			if(Line.size() >= 3 && (unsigned char)Line[0] == 0xEF && (unsigned char)Line[1] == 0xBB && (unsigned char)Line[2] == 0xBF)
				Line.remove_prefix(3);
			if(!Line.empty() && Line.front() == '[')
			{
				const size_t Close = Line.find(']');
				int64_t StartMs = 0;
				int64_t DurationMs = 0;
				if(Close != std::string_view::npos && ParseYrcPair(Line.substr(1, Close - 1), &StartMs, &DurationMs))
				{
					SLine Parsed;
					Parsed.m_StartMs = StartMs;
					Parsed.m_EndMs = DurationMs > 0 && StartMs <= std::numeric_limits<int64_t>::max() - DurationMs ? StartMs + DurationMs : -1;
					std::string_view Body = Line.substr(Close + 1);
					std::string PlainText;
					bool MalformedLine = false;
					size_t BodyOffset = 0;
					while(BodyOffset < Body.size())
					{
						const size_t Marker = FindYrcWordMarker(Body, BodyOffset);
						if(Marker != BodyOffset)
						{
							const size_t End = Marker == std::string_view::npos ? Body.size() : Marker;
							PlainText.append(Body.substr(BodyOffset, End - BodyOffset));
							BodyOffset = End;
							continue;
						}
						const size_t PairClose = Body.find(')', BodyOffset + 1);
						if(PairClose == std::string_view::npos)
						{
							MalformedLine = true;
							break;
						}
						int64_t WordOffsetMs = 0;
						int64_t WordDurationMs = 0;
						bool AbsoluteWordTime = false;
						if(!ParseYrcPair(Body.substr(BodyOffset + 1, PairClose - BodyOffset - 1), &WordOffsetMs, &WordDurationMs, &AbsoluteWordTime))
						{
							MalformedLine = true;
							break;
						}
						const size_t WordStart = PairClose + 1;
						const size_t NextMarker = FindYrcWordMarker(Body, WordStart);
						const size_t WordEnd = NextMarker == std::string_view::npos ? Body.size() : NextMarker;
						const std::string WordText(Body.substr(WordStart, WordEnd - WordStart));
						PlainText += WordText;
						// 网易云三字段 YRC 使用绝对词起点；两字段兼容格式使用
						// 相对行起点的偏移，不根据数值大小猜测单位或语义。
						if(!AbsoluteWordTime && StartMs > std::numeric_limits<int64_t>::max() - WordOffsetMs)
						{
							BodyOffset = WordEnd;
							continue;
						}
						const int64_t WordStartMs = AbsoluteWordTime ? WordOffsetMs : StartMs + WordOffsetMs;
						// 零时长标点和空格仍占正文字符，必须保留在逐字时间轴中。
						if(WordStartMs <= std::numeric_limits<int64_t>::max() - WordDurationMs)
							Parsed.m_vWords.push_back({WordStartMs, WordStartMs + WordDurationMs, WordText});
						BodyOffset = WordEnd;
					}
					if(!MalformedLine)
					{
						Parsed.m_Text = std::move(PlainText);
						vParsed.push_back(std::move(Parsed));
					}
				}
			}
			if(Newline == std::string_view::npos)
				break;
			Offset = Newline + 1;
		}
		return FinishTimeline(std::move(vParsed), pOut, pError);
	}

	bool ParseRawLyrics(const SRawLyrics &Raw, STimeline *pOut, std::string *pError)
	{
		if(!Raw.m_Yrc.empty() && ParseYrc(Raw.m_Yrc, pOut, pError))
			return true;
		if(!Raw.m_Lrc.empty() && ParseLrc(Raw.m_Lrc, pOut, pError))
			return true;
		if(pOut)
			pOut->Clear();
		return false;
	}
} // namespace NeteaseLyrics
