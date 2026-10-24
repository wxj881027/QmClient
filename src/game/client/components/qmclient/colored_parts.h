// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COLORED_PARTS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COLORED_PARTS_H

#include <base/color.h>
#include <base/str.h>

#include <engine/shared/console.h>
#include <engine/textrender.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

class CColoredParts
{
public:
	class CColoredPart
	{
	public:
		int m_Index;
		ColorRGBA m_Color;

		CColoredPart(int Index, const ColorRGBA &Color) :
			m_Index(Index), m_Color(Color) {}
	};

private:
	std::unique_ptr<char[]> m_pBuffer; // 构造中途失败时也要回收已申请的缓冲。
	const char *m_pText = nullptr; // Points to valid string (owned or original)
	std::vector<CColoredPart> m_vColors;
	bool m_ParseFailed = false;

public:
	const char *Text() const { return m_pText; }
	const std::vector<CColoredPart> &Colors() const { return m_vColors; }
	bool ParseFailed() const { return m_ParseFailed; }

	// 显示路径的可选颜色准备失败时借用原文，不留下部分解析对象。
	static CColoredParts ParseOrBorrow(const char *pInput, bool Parse)
	{
		try
		{
			return CColoredParts(pInput, Parse);
		}
		catch(const std::bad_alloc &)
		{
			CColoredParts Fallback(pInput, false);
			Fallback.m_ParseFailed = true;
			return Fallback;
		}
		catch(const std::length_error &)
		{
			CColoredParts Fallback(pInput, false);
			Fallback.m_ParseFailed = true;
			return Fallback;
		}
	}

	~CColoredParts() = default;
	CColoredParts(const CColoredParts &) = delete;
	CColoredParts &operator=(const CColoredParts &) = delete;
	CColoredParts(CColoredParts &&Other) noexcept :
		m_pBuffer(std::move(Other.m_pBuffer)),
		m_pText(Other.m_pText),
		m_vColors(std::move(Other.m_vColors)),
		m_ParseFailed(Other.m_ParseFailed)
	{
		Other.m_pText = nullptr;
	}
	CColoredParts &operator=(CColoredParts &&Other) noexcept
	{
		if(this != &Other)
		{
			m_pBuffer = std::move(Other.m_pBuffer);
			m_pText = Other.m_pText;
			m_vColors = std::move(Other.m_vColors);
			m_ParseFailed = Other.m_ParseFailed;
			Other.m_pText = nullptr;
		}
		return *this;
	}
	CColoredParts(const char *pInput, bool Parse)
	{
		if(!Parse || !str_find(pInput, "[["))
		{
			m_pText = pInput;
			return;
		}

		// Allocate a writable buffer
		const int Length = str_length(pInput);
		m_pBuffer.reset(new char[static_cast<size_t>(Length) + 1]);
		m_pText = m_pBuffer.get();

		char *pWrite = m_pBuffer.get();
		const char *pRead = pInput;
		while(*pRead)
		{
			const char *pMarkerStart = str_find(pRead, "[[");
			if(!pMarkerStart)
			{
				while((*pWrite++ = *pRead++))
					;
				break;
			}

			// Copy plain text before the marker
			while(pRead < pMarkerStart)
				*pWrite++ = *pRead++;

			const char *pMarkerEnd = str_find(pMarkerStart + 2, "]]");
			if(!pMarkerEnd)
			{
				// No closing marker, copy rest
				while((*pWrite++ = *pRead++))
					;
				break;
			}

			// Extract marker content
			char aBuf[128];
			str_copy(aBuf, pMarkerStart + 2, std::min<int>(sizeof(aBuf), pMarkerEnd - pMarkerStart - 2 + 1));
			const auto Color = CConsole::ColorParse(aBuf, 0.0f);
			if(Color.has_value())
				m_vColors.emplace_back(static_cast<int>(pWrite - m_pBuffer.get()), color_cast<ColorRGBA>(*Color));

			// Skip over marker
			pRead = pMarkerEnd + 2;
		}

		*pWrite = '\0';
	}

	void AddSplitsToCursor(CTextCursor &Cursor) const
	{
		if(Colors().size() > Cursor.m_vColorSplits.max_size() - Cursor.m_vColorSplits.size())
			throw std::length_error("Color split capacity is not representable");
		// 先检查所有后续取色算式，失败时不改动已有分段。
		for(size_t Index = 0; Index < Colors().size(); ++Index)
		{
			const int64_t Start = static_cast<int64_t>(Cursor.m_CharCount) + Colors()[Index].m_Index;
			const int Length = Index + 1 == Colors().size() ? 9999 : Colors()[Index + 1].m_Index - Colors()[Index].m_Index;
			const int64_t End = Start + Length;
			if(Start < std::numeric_limits<int>::min() || Start > std::numeric_limits<int>::max() ||
				End > std::numeric_limits<int>::max() || End - 1 < std::numeric_limits<int>::min())
				throw std::length_error("Color split range is not representable");
		}
		Cursor.m_vColorSplits.reserve(Cursor.m_vColorSplits.size() + Colors().size());
		for(int PartIndex = 0; PartIndex < (int)Colors().size(); ++PartIndex)
		{
			const auto &Part = Colors()[PartIndex];
			int Length;
			if(PartIndex == (int)Colors().size() - 1)
				Length = 9999;
			else
				Length = Colors()[PartIndex + 1].m_Index - Part.m_Index;
			Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount + Part.m_Index, Length, Part.m_Color);
		}
	}

	bool TryAddSplitsToCursor(CTextCursor &Cursor) const
	{
		try
		{
			AddSplitsToCursor(Cursor);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
};

#endif
