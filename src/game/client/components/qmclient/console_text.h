#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_TEXT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_TEXT_H

#include <base/str.h>

#include <engine/textrender.h>

#include <algorithm>
#include <string>
#include <vector>

namespace QmConsoleText
{
	// 着色使用字节位置；选区使用字形序号，显式换行不占字形。
	struct SRange
	{
		int m_StartByte;
		int m_EndByte;
		int m_StartChar;
		int m_EndChar;
	};

	inline SRange Range(const char *pText, const char *pStart, const char *pEnd)
	{
		int StartChar = 0, EndChar = 0;
		for(const char *pScan = pText; pScan < pEnd;)
		{
			const char *pBefore = pScan;
			if(str_utf8_decode(&pScan) != '\n')
			{
				++EndChar;
				if(pBefore < pStart)
					++StartChar;
			}
		}
		return {static_cast<int>(pStart - pText), static_cast<int>(pEnd - pText),
			StartChar, EndChar};
	}

	inline bool IsLinkDelimiter(int Character)
	{
		return Character < 0 || Character < 32 || str_utf8_isspace(Character) ||
		       Character == '<' || Character == '>' || Character == '"' || Character == '\'' || Character == '`' || Character == '\\' ||
		       Character == 0x3001 || Character == 0x3002 || // 顿号、句号
		       Character == 0xff0c || Character == 0xff1b || Character == 0xff1a || Character == 0xff01 || Character == 0xff1f ||
		       Character == 0x201c || Character == 0x201d || Character == 0x2018 || Character == 0x2019 ||
		       Character == 0xff08 || Character == 0xff09 || Character == 0x3010 || Character == 0x3011 ||
		       Character == 0x3008 || Character == 0x3009 || Character == 0x300a || Character == 0x300b;
	}

	inline bool IsLinkPrefixBoundary(const char *pText, const char *pStart)
	{
		if(pStart == pText)
			return true;
		const unsigned char Previous = pStart[-1];
		return !((Previous >= 'a' && Previous <= 'z') || (Previous >= 'A' && Previous <= 'Z') ||
			 (Previous >= '0' && Previous <= '9') || Previous == '_' || Previous == '.' || Previous == '@' || Previous == '/' || Previous == '-');
	}

	inline const char *TrimLinkEnd(const char *pStart, const char *pEnd)
	{
		int Round = 0, Square = 0, Curly = 0;
		for(const char *pScan = pStart; pScan < pEnd; ++pScan)
		{
			Round += (*pScan == '(') - (*pScan == ')');
			Square += (*pScan == '[') - (*pScan == ']');
			Curly += (*pScan == '{') - (*pScan == '}');
		}
		while(pEnd > pStart)
		{
			const char Last = pEnd[-1];
			if(Last == '.' || Last == ',' || Last == ';' || Last == ':' || Last == '!' || Last == '?')
				--pEnd;
			else if(Last == ')' && Round < 0)
			{
				--pEnd;
				++Round;
			}
			else if(Last == ']' && Square < 0)
			{
				--pEnd;
				++Square;
			}
			else if(Last == '}' && Curly < 0)
			{
				--pEnd;
				++Curly;
			}
			else
				break;
		}
		return pEnd;
	}

	inline bool HasLinkHost(const char *pStart, const char *pEnd, int PrefixLength, bool Www)
	{
		const char *pHost = pStart + PrefixLength;
		const char *pHostEnd = pHost;
		while(pHostEnd < pEnd && *pHostEnd != '/' && *pHostEnd != '?' && *pHostEnd != '#')
			++pHostEnd;
		if(pHostEnd == pHost)
			return false;
		// 不把空协议、标点或邮件地址当作可点击的网页链接。
		if(*pHost == '.' || *pHost == ':' || *pHost == ')' || *pHost == ']' || *pHost == '}')
			return false;
		bool Dot = false;
		for(const char *pScan = pHost; pScan < pHostEnd; ++pScan)
		{
			if(*pScan == '@' || *pScan == '(' || *pScan == ')' || *pScan == '{' || *pScan == '}')
				return false;
			Dot |= *pScan == '.' && pScan > pHost && pScan + 1 < pHostEnd;
		}
		return !Www || Dot;
	}

	inline void CollectLinks(const char *pText, std::vector<SRange> &vLinks)
	{
		vLinks.clear();
		if(pText == nullptr)
			return;
		if(!str_find_nocase(pText, "https://") && !str_find_nocase(pText, "http://") && !str_find_nocase(pText, "www."))
			return;
		const char *pScan = pText;
		while(*pScan)
		{
			const char *pStart = pScan;
			int PrefixLength = 0;
			bool Www = false;
			if(str_startswith_nocase(pStart, "https://"))
				PrefixLength = 8;
			else if(str_startswith_nocase(pStart, "http://"))
				PrefixLength = 7;
			else if(str_startswith_nocase(pStart, "www."))
			{
				PrefixLength = 4;
				Www = true;
			}
			if(PrefixLength == 0 || !IsLinkPrefixBoundary(pText, pStart))
			{
				str_utf8_decode(&pScan);
				continue;
			}
			const char *pEnd = pStart + PrefixLength;
			while(*pEnd)
			{
				const char *pNext = pEnd;
				if(IsLinkDelimiter(str_utf8_decode(&pNext)))
					break;
				pEnd = pNext;
			}
			pScan = pEnd;
			pEnd = TrimLinkEnd(pStart, pEnd);
			if(pEnd > pStart + PrefixLength && HasLinkHost(pStart, pEnd, PrefixLength, Www))
				vLinks.push_back(Range(pText, pStart, pEnd));
		}
	}

	inline std::string LinkUrl(const char *pText, const SRange &Link)
	{
		std::string Url(pText + Link.m_StartByte, Link.m_EndByte - Link.m_StartByte);
		if(str_startswith_nocase(Url.c_str(), "www."))
			return "https://" + Url;
		const int PrefixLength = str_startswith_nocase(Url.c_str(), "https://") ? 8 : 7;
		Url.replace(0, PrefixLength, PrefixLength == 8 ? "https://" : "http://");
		return Url;
	}

	inline void CollectSearchMatches(const char *pText, const char *pNeedle, std::vector<SRange> &vMatches)
	{
		vMatches.clear();
		if(pText == nullptr || pNeedle == nullptr || *pNeedle == '\0')
			return;
		const char *pSearch = pText;
		const char *pEnd;
		while(const char *pStart = str_utf8_find_nocase(pSearch, pNeedle, &pEnd))
		{
			vMatches.push_back(Range(pText, pStart, pEnd));
			pSearch = pEnd;
		}
	}

	inline bool ContainsPoint(const std::vector<IGraphics::CQuadItem> &vQuads, vec2 Point)
	{
		return std::any_of(vQuads.begin(), vQuads.end(), [Point](const IGraphics::CQuadItem &Quad) {
			return Point.x >= Quad.m_X && Point.x < Quad.m_X + Quad.m_Width &&
			       Point.y >= Quad.m_Y && Point.y < Quad.m_Y + Quad.m_Height;
		});
	}

	inline STextColorSplit ColorSplitForCharacters(const char *pText, int Start, int Length, ColorRGBA Color)
	{
		const int StartByte = static_cast<int>(str_utf8_offset_chars_to_bytes(pText, std::max(0, Start)));
		const int EndByte = static_cast<int>(str_utf8_offset_chars_to_bytes(pText, std::max(0, Start) + std::max(0, Length)));
		return {StartByte, EndByte - StartByte, Color};
	}

	// 后加入的层覆盖前面的层，输出有序且不重叠，避免文本渲染器漏掉后续颜色。
	inline void ComposeColorSplits(const char *pText, const std::vector<STextColorSplit> &vLayers, std::vector<STextColorSplit> &vResult)
	{
		vResult.clear();
		const int TextLength = str_length(pText);
		if(TextLength <= 0 || vLayers.empty())
			return;
		if(vLayers.size() == 1 && vLayers[0].m_CharIndex == 0 && (vLayers[0].m_Length < 0 || vLayers[0].m_Length >= TextLength))
		{
			vResult.emplace_back(0, TextLength, vLayers[0].m_Color, vLayers[0].m_ColorEnd);
			return;
		}
		std::vector<int> vBoundaries{0, TextLength};
		vBoundaries.reserve(2 + vLayers.size() * 2);
		for(const auto &Layer : vLayers)
		{
			vBoundaries.push_back(std::clamp(Layer.m_CharIndex, 0, TextLength));
			vBoundaries.push_back(Layer.m_Length < 0 ? TextLength : std::clamp(Layer.m_CharIndex + Layer.m_Length, 0, TextLength));
		}
		std::sort(vBoundaries.begin(), vBoundaries.end());
		vBoundaries.erase(std::unique(vBoundaries.begin(), vBoundaries.end()), vBoundaries.end());
		for(size_t i = 1; i < vBoundaries.size(); ++i)
		{
			const int Start = vBoundaries[i - 1];
			const int End = vBoundaries[i];
			// 渲染器只在字形处推进颜色游标；单独的换行层会阻挡下一行首字形的颜色。
			if(std::all_of(pText + Start, pText + End, [](char Character) { return Character == '\n'; }))
				continue;
			for(auto Layer = vLayers.rbegin(); Layer != vLayers.rend(); ++Layer)
			{
				if(Start < Layer->m_CharIndex || (Layer->m_Length >= 0 && Start >= Layer->m_CharIndex + Layer->m_Length))
					continue;
				vResult.emplace_back(Start, End - Start, Layer->m_Color, Layer->m_ColorEnd);
				break;
			}
		}
	}
}

#endif
