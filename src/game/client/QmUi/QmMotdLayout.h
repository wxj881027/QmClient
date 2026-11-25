#ifndef GAME_CLIENT_QMUI_QMMOTDLAYOUT_H
#define GAME_CLIENT_QMUI_QMMOTDLAYOUT_H

#include <base/str.h>

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// 游戏内「服务器信息」页公告（MOTD）的行级排版。
//
// 服务端常用 Unicode 框线字符（U+2500–U+257F，例如 ┌ ─ ┐ │ └ ┘）画方框。
// 这些字形要首尾相接才是一条线；一旦按「字形自然宽度 + 字距」排版，横向笔画之间
// 就会留出缝，实机看起来像竹节。本模块把公告按行切分并分类：
//   - 框线行：一格里放一个字形，全行统一推进、字间距为 0；
//   - 普通文本行：沿用既有文本排版（自然推进、允许换行）。
// 本模块只做纯计算：不碰渲染、不读全局状态、不分配渲染资源，可直接单测。
namespace QmMotdLayout
{
	enum class ELineMode
	{
		// 普通文本行：交给文本排版，按字形自然宽度推进并允许换行。
		TEXT,
		// 框线行：全部可见字形都是框线/制表字符，按统一格子推进且字间距为 0。
		BOX,
	};

	struct SLine
	{
		// 该行首字节在公告文本里的偏移与字节长度（不含换行符与行尾 \r）。
		int m_ByteOffset = 0;
		int m_ByteLength = 0;
		// 可见字形数（UTF-8 码点，不含行尾 \r）。
		int m_GlyphCount = 0;
		// 实际绘制字形数：超宽框线行在这里被截断，普通行等于字形数。
		int m_VisibleGlyphCount = 0;
		ELineMode m_Mode = ELineMode::TEXT;
		// 框线行每个字形的横向推进（就是格宽）；普通行为 0，表示不参与等宽推进。
		float m_Advance = 0.0f;
		// 行顶相对公告内容左上角的偏移，按统一行高折算。
		float m_Y = 0.0f;
	};

	struct SLayout
	{
		std::vector<SLine> m_vLines;
		// 统一行高：框线行按它往下排；普通行换行后的真实高度由渲染侧按实测推进。
		float m_LineHeight = 0.0f;
		int m_BoxLineCount = 0;

		int LineCount() const { return (int)m_vLines.size(); }
		bool HasBoxLines() const { return m_BoxLineCount > 0; }
		// 按统一行高折算的最小总高度：普通行不换行时就是真实高度。
		float Height() const { return (float)m_vLines.size() * m_LineHeight; }
		// 未排到的行先按一行估算，避免正文每追加一行就改一次缩放。
		float PreviewHeight(int CompletedRows, float CompletedHeight, int CurrentRowLineCount) const
		{
			const int RemainingRows = LineCount() - CompletedRows;
			if(RemainingRows <= 0)
				return CompletedHeight;
			return CompletedHeight + (float)(RemainingRows - 1 + std::max(1, CurrentRowLineCount)) * m_LineHeight;
		}
		// 框线行占据的最大宽度，用于把整段内容缩放塞进公告栏。
		float MaxBoxLineWidth() const
		{
			float MaxWidth = 0.0f;
			for(const SLine &Line : m_vLines)
			{
				if(Line.m_Mode == ELineMode::BOX)
					MaxWidth = std::max(MaxWidth, (float)Line.m_VisibleGlyphCount * Line.m_Advance);
			}
			return MaxWidth;
		}
	};

	enum class EParagraphSource
	{
		NONE,
		COMPLETE,
		BUILDING,
	};

	inline EParagraphSource SelectParagraphSource(bool CompleteReady, bool PartialReady, bool SameText)
	{
		if(PartialReady && (!CompleteReady || !SameText))
			return EParagraphSource::BUILDING;
		return CompleteReady ? EParagraphSource::COMPLETE : EParagraphSource::NONE;
	}

	// 可见公告每帧可以推进一段，后台预排仍等待下一帧和空闲预算。
	inline bool CanAdvanceParagraph(bool Visible, bool FirstPendingFrame, int AvailableTokens)
	{
		return Visible || (!FirstPendingFrame && AvailableTokens > 0);
	}

	// 框线/制表字符：U+2500–U+257F（Box Drawing）与 U+2580–U+259F（Block Elements）。
	// ├ ┤ ┬ ┴ ┼ 都在 U+2500–U+257F 内，字形常用于画框与分栏。
	inline bool IsBoxDrawingCodepoint(uint32_t Codepoint)
	{
		return (Codepoint >= 0x2500 && Codepoint <= 0x257F) || (Codepoint >= 0x2580 && Codepoint <= 0x259F);
	}

	// 扫描一行：统计可见字形数，并判断是否整行都是框线字符（空格与制表符算格内空位）。
	inline void ScanLine(const char *pText, int Length, int &GlyphCount, bool &AllBoxDrawing)
	{
		const char *pCursor = pText;
		const char *pEnd = pText + std::max(0, Length);
		int BoxGlyphs = 0;
		GlyphCount = 0;
		AllBoxDrawing = true;
		while(pCursor < pEnd)
		{
			const char *pBefore = pCursor;
			const int Codepoint = str_utf8_decode(&pCursor);
			// 解码不前进（非法序列）或越过行尾（多字节字符被截断）：这一行剩下的字节不再计入。
			if(pCursor <= pBefore || pCursor > pEnd || Codepoint == 0)
				break;
			++GlyphCount;
			if(IsBoxDrawingCodepoint((uint32_t)Codepoint))
				++BoxGlyphs;
			else if(Codepoint != ' ' && Codepoint != '\t')
				AllBoxDrawing = false;
		}
		// 没有任何框线字符的行（含空行、整行空格、整行普通文字）不算框线行。
		AllBoxDrawing = AllBoxDrawing && BoxGlyphs > 0;
	}

	// 一行是否整行都是框线/制表字符。空行与含普通文字的行都返回 false。
	inline bool IsBoxDrawingLine(const char *pText, int Length)
	{
		int GlyphCount = 0;
		bool AllBoxDrawing = false;
		ScanLine(pText, Length, GlyphCount, AllBoxDrawing);
		return AllBoxDrawing;
	}

	// 按行切分并分类，算出框线行的等宽推进与可见字数。
	// CellAdvance：框线字形在目标字号下的自然推进，由调用点用字体实测传入（不写死像素）。
	inline SLayout Build(const char *pText, float AvailableWidth, float CellAdvance, float LineHeight)
	{
		SLayout Layout;
		Layout.m_LineHeight = std::max(0.0f, LineHeight);
		if(pText == nullptr)
			return Layout;

		const float SafeWidth = std::max(0.0f, AvailableWidth);
		const float SafeCellAdvance = CellAdvance > 0.0f ? CellAdvance : Layout.m_LineHeight;
		// 超宽时把推进压到能放下为止，但压不过下限：再窄笔画会互相吞掉，改为截断可见字数。
		const float MinAdvance = SafeCellAdvance * std::max(0.0f, ui_token::motd::BOX_CELL_MIN_RATIO);

		const char *pCursor = pText;
		for(;;)
		{
			const char *pLineStart = pCursor;
			while(*pCursor != '\0' && *pCursor != '\n')
				++pCursor;
			int LineLength = (int)(pCursor - pLineStart);
			// 服务端可能发 CRLF：行尾 \r 既不是可见字符也不参与排版。
			if(LineLength > 0 && pLineStart[LineLength - 1] == '\r')
				--LineLength;

			SLine Line;
			Line.m_ByteOffset = (int)(pLineStart - pText);
			Line.m_ByteLength = LineLength;

			int GlyphCount = 0;
			bool AllBoxDrawing = false;
			ScanLine(pLineStart, LineLength, GlyphCount, AllBoxDrawing);
			Line.m_GlyphCount = GlyphCount;

			// 分类只看字符本身：格宽缺失时仍判为框线行，只是没有可画的格子。
			if(GlyphCount > 0 && AllBoxDrawing)
			{
				Line.m_Mode = ELineMode::BOX;
				const float WidthAdvance = SafeWidth > 0.0f ? SafeWidth / (float)GlyphCount : 0.0f;
				// 自然推进与「按可用宽度均分」取小值：只有超宽才压缩，绝不拉伸。
				// 这样整行始终是 GlyphCount 个等宽格子，相邻横线首尾相接（字间距为 0）。
				Line.m_Advance = std::min(SafeCellAdvance, std::max(WidthAdvance, MinAdvance));
				const float Fits = Line.m_Advance > 0.0f ? std::floor(SafeWidth / Line.m_Advance + 0.0001f) : 0.0f;
				Line.m_VisibleGlyphCount = (int)std::min((float)GlyphCount, std::max(0.0f, Fits));
				++Layout.m_BoxLineCount;
			}
			else
			{
				Line.m_Mode = ELineMode::TEXT;
				Line.m_Advance = 0.0f;
				Line.m_VisibleGlyphCount = GlyphCount;
			}

			Line.m_Y = (float)Layout.m_vLines.size() * Layout.m_LineHeight;
			Layout.m_vLines.push_back(Line);

			if(*pCursor == '\0')
				break;
			++pCursor; // 跳过换行符
		}
		return Layout;
	}
} // namespace QmMotdLayout

#endif
