// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiMarkdownRender.h"

#include "QmWikiMarkdown.h"
#include "QmWikiMedia.h"
#include "UiContext.h"
#include "UiTokens.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace
{
	constexpr float LINE_SPACING = 1.45f;

	float StyleSize(unsigned Style, float Size)
	{
		return (Style & WIKI_INLINE_CODE) != 0 ? Size * 0.94f : Size;
	}

	// 宽字符（CJK 与全角标点）逐字断行，其余按词断行。
	bool IsWideCodepoint(int Codepoint)
	{
		return (Codepoint >= 0x1100 && Codepoint <= 0x115F) ||
		       (Codepoint >= 0x2E80 && Codepoint <= 0x303E) ||
		       (Codepoint >= 0x3041 && Codepoint <= 0x33FF) ||
		       (Codepoint >= 0x3400 && Codepoint <= 0x4DBF) ||
		       (Codepoint >= 0x4E00 && Codepoint <= 0x9FFF) ||
		       (Codepoint >= 0xA000 && Codepoint <= 0xA4CF) ||
		       (Codepoint >= 0xAC00 && Codepoint <= 0xD7A3) ||
		       (Codepoint >= 0xF900 && Codepoint <= 0xFAFF) ||
		       (Codepoint >= 0xFE30 && Codepoint <= 0xFE4F) ||
		       (Codepoint >= 0xFF00 && Codepoint <= 0xFF60) ||
		       (Codepoint >= 0xFFE0 && Codepoint <= 0xFFE6);
	}

	std::string MathSlug(const std::string &Source);
	const SWikiMedia *FindMathMedia(const std::string &Source, std::string *pOutPath);

	struct SToken
	{
		std::string m_Text;
		unsigned m_Style = 0;
		float m_Width = 0.0f;
		// 链接目标一路带到绘制阶段，点击时才知道跳到哪里。
		std::string m_Target;
		// 行内公式预渲染成图片后，这里存图集路径；为空表示按文字绘制。
		std::string m_MediaPath;
	};

	struct STextPiece
	{
		std::string m_Text;
		unsigned m_Style = 0;
		float m_X = 0.0f;
		float m_Width = 0.0f;
		std::string m_Target;
		std::string m_MediaPath;
	};

	struct SLine
	{
		std::vector<STextPiece> m_Pieces;
		float m_Width = 0.0f;
	};

	struct SBlockLayout
	{
		float m_Height = 0.0f;
		float m_FontSize = 14.0f;
		float m_LineHeight = 0.0f;
		float m_Indent = 0.0f;
		std::vector<SLine> m_Lines;
		// 表格
		bool m_Table = false;
		float m_CellPad = 0.0f;
		std::vector<float> m_vColWidths;
		std::vector<float> m_vRowHeights;
		std::vector<std::vector<SLine>> m_vCells;
		// 图片
		bool m_HasMedia = false;
		float m_MediaHeight = 0.0f;
		float m_MediaWidth = 0.0f;
		std::string m_MediaPath;
	};

	void TokenizeSpans(const std::vector<SWikiSpan> &vSpans, float Size, ITextRender *pTextRender, std::vector<SToken> *pTokens)
	{
		for(const SWikiSpan &Span : vSpans)
		{
			// 行内公式若已有预渲染图，按行高缩放成行内图片，保证和文字基线一致。
			if((Span.m_Style & WIKI_INLINE_MATH) != 0)
			{
				std::string Path;
				const SWikiMedia *pMedia = FindMathMedia(Span.m_Target.empty() ? Span.m_Text : Span.m_Target, &Path);
				if(pMedia != nullptr)
				{
					SToken Token;
					Token.m_Style = Span.m_Style;
					Token.m_MediaPath = Path;
					Token.m_Width = Size * 1.25f * qm_wiki_media::Aspect(*pMedia);
					pTokens->push_back(std::move(Token));
					continue;
				}
			}
			std::string Word;
			const auto FlushWord = [&]() {
				if(!Word.empty())
				{
					SToken Token;
					Token.m_Text = Word;
					Token.m_Style = Span.m_Style;
					Token.m_Target = Span.m_Target;
					Token.m_Width = pTextRender->TextWidth(StyleSize(Span.m_Style, Size), Word.c_str());
					pTokens->push_back(std::move(Token));
					Word.clear();
				}
			};
			const char *pCursor = Span.m_Text.c_str();
			while(*pCursor != '\0')
			{
				const char *pStart = pCursor;
				const int Codepoint = str_utf8_decode(&pCursor);
				const size_t Bytes = static_cast<size_t>(pCursor - pStart);
				if(Codepoint == ' ')
				{
					if(!Word.empty())
					{
						Word += ' ';
						FlushWord();
					}
					continue;
				}
				if(IsWideCodepoint(Codepoint))
				{
					FlushWord();
					SToken Token;
					Token.m_Text.assign(pStart, Bytes);
					Token.m_Style = Span.m_Style;
					Token.m_Target = Span.m_Target;
					Token.m_Width = pTextRender->TextWidth(StyleSize(Span.m_Style, Size), Token.m_Text.c_str());
					pTokens->push_back(std::move(Token));
					continue;
				}
				Word.append(pStart, Bytes);
			}
			FlushWord();
		}
	}

	void BuildLines(const std::vector<SWikiSpan> &vSpans, float MaxWidth, float Size, ITextRender *pTextRender, std::vector<SLine> *pLines)
	{
		pLines->clear();
		if(vSpans.empty())
		{
			pLines->emplace_back();
			return;
		}
		std::vector<SToken> vTokens;
		TokenizeSpans(vSpans, Size, pTextRender, &vTokens);

		SLine Line;
		float X = 0.0f;
		const float Width = std::max(1.0f, MaxWidth);
		for(const SToken &Token : vTokens)
		{
			const bool TooWide = Token.m_Width > Width;
			if(TooWide)
			{
				// 超长词逐字符拆行，避免整块溢出右边界。
				const char *pCursor = Token.m_Text.c_str();
				while(*pCursor != '\0')
				{
					const char *pStart = pCursor;
					str_utf8_decode(&pCursor);
					const std::string Unit(pStart, static_cast<size_t>(pCursor - pStart));
					const float UnitWidth = pTextRender->TextWidth(StyleSize(Token.m_Style, Size), Unit.c_str());
					if(X > 0.0f && X + UnitWidth > Width)
					{
						pLines->push_back(std::move(Line));
						Line = SLine();
						X = 0.0f;
					}
					Line.m_Pieces.push_back({Unit, Token.m_Style, X, UnitWidth, Token.m_Target, Token.m_MediaPath});
					X += UnitWidth;
					Line.m_Width = X;
				}
				continue;
			}
			if(X > 0.0f && X + Token.m_Width > Width)
			{
				pLines->push_back(std::move(Line));
				Line = SLine();
				X = 0.0f;
			}
			Line.m_Pieces.push_back({Token.m_Text, Token.m_Style, X, Token.m_Width, Token.m_Target, Token.m_MediaPath});
			X += Token.m_Width;
			Line.m_Width = X;
		}
		if(!Line.m_Pieces.empty() || pLines->empty())
			pLines->push_back(std::move(Line));
	}

	float SingleLineWidth(const std::vector<SWikiSpan> &vSpans, float Size, ITextRender *pTextRender)
	{
		float Width = 0.0f;
		for(const SWikiSpan &Span : vSpans)
			Width += pTextRender->TextWidth(StyleSize(Span.m_Style, Size), Span.m_Text.c_str());
		return Width;
	}

	ColorRGBA PieceColor(const SWikiRenderStyle &Style, const ColorRGBA &Base, unsigned Flags)
	{
		if((Flags & (WIKI_INLINE_CODE | WIKI_INLINE_MATH | WIKI_INLINE_LINK)) != 0)
			return Style.m_Accent;
		return Base;
	}

	void DrawLines(const IUiContext &Ctx, const std::vector<SLine> &vLines, float Left, float Top, float LineHeight, float FontSize, const SWikiRenderStyle &Style, ColorRGBA BaseColor, SWikiLinkContext *pLinks, int Align = -1, float AlignWidth = 0.0f)
	{
		ITextRender *pTextRender = Ctx.m_pTextRender;
		if(pTextRender == nullptr)
			return;
		float Y = Top;
		for(const SLine &Line : vLines)
		{
			float Offset = 0.0f;
			if(Align == 1)
				Offset = (AlignWidth - Line.m_Width) * 0.5f;
			else if(Align == 2)
				Offset = AlignWidth - Line.m_Width;
			for(const STextPiece &Piece : Line.m_Pieces)
			{
				if(!Piece.m_MediaPath.empty())
				{
					const SWikiMedia *pMedia = qm_wiki_media::Get(Piece.m_MediaPath.c_str());
					if(pMedia != nullptr)
					{
						const float MediaHeight = StyleSize(Piece.m_Style, FontSize) * 1.25f;
						CUIRect MediaRect;
						MediaRect.x = Left + Offset + Piece.m_X;
						MediaRect.y = Y + (LineHeight - MediaHeight) * 0.5f;
						MediaRect.w = std::max(1.0f, MediaHeight * qm_wiki_media::Aspect(*pMedia));
						MediaRect.h = MediaHeight;
						qm_wiki_media::Draw(*pMedia, MediaRect, Style.m_Text);
					}
					continue;
				}
				if(Piece.m_Text.empty())
					continue;
				const float PieceSize = StyleSize(Piece.m_Style, FontSize);
				ColorRGBA Color = PieceColor(Style, BaseColor, Piece.m_Style);
				const float X = Left + Offset + Piece.m_X;
				const bool IsLink = (Piece.m_Style & WIKI_INLINE_LINK) != 0 && !Piece.m_Target.empty();
				CUIRect HitRect(X, Y, std::max(2.0f, Piece.m_Width), LineHeight);
				const bool Hovered = IsLink && Ctx.m_pUi != nullptr && Ctx.m_pUi->MouseHovered(&HitRect);
				if(Hovered)
					Color = Style.m_Text;
				pTextRender->TextColor(Color);
				pTextRender->Text(X, Y, PieceSize, Piece.m_Text.c_str());
				if((Piece.m_Style & WIKI_INLINE_BOLD) != 0)
					pTextRender->Text(X + 0.4f * Style.m_UiScale, Y, PieceSize, Piece.m_Text.c_str());
				if(IsLink && pLinks != nullptr)
				{
					// 悬停时画下划线，明确这是可以点的。
					if(Hovered)
					{
						CUIRect Underline;
						Underline.x = X;
						Underline.y = Y + PieceSize * 1.02f;
						Underline.w = Piece.m_Width;
						Underline.h = std::max(1.0f, Style.m_UiScale);
						Underline.Draw(Color, IGraphics::CORNER_ALL, 0.0f);
					}
					if(pLinks->m_pOnClick != nullptr && pLinks->m_Index < 512 && Ctx.m_pUi != nullptr && !Ctx.m_pUi->RenderOnly())
					{
						int *pId = &pLinks->m_aIds[pLinks->m_Index++];
						if(Ctx.m_pUi->DoButtonLogic(pId, 0, &HitRect, BUTTONFLAG_LEFT))
							(*pLinks->m_pOnClick)(Piece.m_Target.c_str());
					}
				}
			}
			Y += LineHeight;
		}
		pTextRender->TextColor(Style.m_Text);
	}

	// 标题、列表项需要整段加粗：复制一份 spans 并补上样式位。
	std::vector<SWikiSpan> WithBold(const std::vector<SWikiSpan> &vSpans)
	{
		std::vector<SWikiSpan> vResult = vSpans;
		for(SWikiSpan &Span : vResult)
			Span.m_Style |= WIKI_INLINE_BOLD;
		return vResult;
	}

	float HeadingFontSize(int Level, const SWikiRenderStyle &Style)
	{
		switch(Level)
		{
		case 1: return Style.m_BodySize * 1.55f;
		case 2: return Style.m_BodySize * 1.28f;
		case 3: return Style.m_BodySize * 1.10f;
		default: return Style.m_BodySize;
		}
	}

	float TopMargin(const SWikiBlock &Block, const SWikiRenderStyle &Style, bool First, size_t Index, const std::vector<SWikiBlock> &vBlocks)
	{
		if(First)
			return 0.0f;
		if(Block.m_Type == EWikiBlockType::HEADING)
			return Block.m_Level <= 2 ? Style.m_BodySize * 1.6f : Style.m_BodySize * 1.1f;
		if(Block.m_Type == EWikiBlockType::DIVIDER)
			return Style.m_BodySize * 0.55f;
		// 同一列表里的相邻项不额外加空行。
		if((Block.m_Type == EWikiBlockType::BULLET || Block.m_Type == EWikiBlockType::ORDERED) && Index > 0)
		{
			const EWikiBlockType Previous = vBlocks[Index - 1].m_Type;
			if(Previous == Block.m_Type)
				return 0.0f;
		}
		return Style.m_BodySize * 0.55f;
	}

	float BottomMargin(const SWikiBlock &Block, const SWikiRenderStyle &Style)
	{
		switch(Block.m_Type)
		{
		case EWikiBlockType::HEADING: return Style.m_BodySize * 0.45f;
		case EWikiBlockType::CODE:
		case EWikiBlockType::MATH: return Style.m_BodySize * 0.15f;
		case EWikiBlockType::TABLE: return Style.m_BodySize * 0.15f;
		default: return 0.0f;
		}
	}

	std::string MathSlug(const std::string &Source)
	{
		std::string Slug;
		for(const char Ch : Source)
		{
			const unsigned char Byte = static_cast<unsigned char>(Ch);
			if(std::isalnum(Byte) != 0 || Ch == '-' || Ch == '_')
				Slug += Ch;
			else if(!Slug.empty() && Slug.back() != '_')
				Slug += '_';
			if(Slug.size() >= 72)
				break;
		}
		while(!Slug.empty() && Slug.back() == '_')
			Slug.pop_back();
		if(Slug.empty())
			Slug = "formula";
		return std::string("math/") + Slug + ".webp";
	}

	// 公式优先找预渲染的 webp，其次兼容旧的 png；都缺就退回文字。
	const SWikiMedia *FindMathMedia(const std::string &Source, std::string *pOutPath)
	{
		std::string Path = MathSlug(Source);
		const SWikiMedia *pMedia = qm_wiki_media::Get(Path.c_str());
		if(pMedia != nullptr)
		{
			*pOutPath = Path;
			return pMedia;
		}
		Path.resize(Path.size() - 5);
		Path += ".png";
		pMedia = qm_wiki_media::Get(Path.c_str());
		if(pMedia != nullptr)
			*pOutPath = Path;
		return pMedia;
	}

	void LayoutLines(const std::vector<SWikiSpan> &vSpans, float Width, float FontSize, ITextRender *pTextRender, SBlockLayout *pLayout)
	{
		pLayout->m_FontSize = FontSize;
		pLayout->m_LineHeight = FontSize * LINE_SPACING;
		BuildLines(vSpans, Width, FontSize, pTextRender, &pLayout->m_Lines);
		pLayout->m_Height = pLayout->m_Lines.size() * pLayout->m_LineHeight;
	}

	void LayoutTable(const SWikiBlock &Block, float Width, const SWikiRenderStyle &Style, ITextRender *pTextRender, SBlockLayout *pLayout)
	{
		const size_t Columns = Block.m_vAligns.size();
		const float Pad = 7.0f * Style.m_UiScale;
		const float LineHeight = Style.m_BodySize * LINE_SPACING;
		pLayout->m_Table = true;
		pLayout->m_CellPad = Pad;
		pLayout->m_FontSize = Style.m_BodySize;
		pLayout->m_LineHeight = LineHeight;
		if(Columns == 0 || Block.m_Rows.empty())
			return;

		std::vector<float> vDesired(Columns, 0.0f);
		for(const SWikiTableRow &Row : Block.m_Rows)
		{
			for(size_t Col = 0; Col < Columns; ++Col)
			{
				const float CellWidth = SingleLineWidth(Row.m_Cells[Col], Style.m_BodySize, pTextRender);
				vDesired[Col] = std::max(vDesired[Col], CellWidth);
			}
		}
		const float Available = std::max(1.0f, Width);
		const float MinColumn = Style.m_BodySize * 3.0f;
		float SumCells = 0.0f;
		for(size_t Col = 0; Col < Columns; ++Col)
			SumCells += std::max(MinColumn, vDesired[Col]) + Pad * 2.0f;
		float Scale = SumCells > Available ? Available / SumCells : 1.0f;

		std::vector<float> vWidths(Columns, 0.0f);
		float SumWidths = 0.0f;
		for(size_t Col = 0; Col < Columns; ++Col)
		{
			vWidths[Col] = std::max(MinColumn, vDesired[Col]) * Scale;
			SumWidths += vWidths[Col];
		}
		const float Extra = Available - SumWidths - Pad * 2.0f * Columns;
		if(Extra > 0.0f)
		{
			const float TotalDesired = std::max(1.0f, SumCells);
			for(size_t Col = 0; Col < Columns; ++Col)
				vWidths[Col] += Extra * (std::max(MinColumn, vDesired[Col]) / TotalDesired);
		}
		pLayout->m_vColWidths = vWidths;

		pLayout->m_vCells.resize(Block.m_Rows.size() * Columns);
		pLayout->m_vRowHeights.resize(Block.m_Rows.size(), 0.0f);
		for(size_t Row = 0; Row < Block.m_Rows.size(); ++Row)
		{
			float RowHeight = 0.0f;
			for(size_t Col = 0; Col < Columns; ++Col)
			{
				std::vector<SWikiSpan> vSpans = Block.m_Rows[Row].m_Cells[Col];
				if(Row == 0)
					vSpans = WithBold(vSpans);
				SBlockLayout CellLayout;
				LayoutLines(vSpans, std::max(1.0f, vWidths[Col] - Pad * 2.0f), Style.m_BodySize, pTextRender, &CellLayout);
				RowHeight = std::max(RowHeight, CellLayout.m_Height);
				pLayout->m_vCells[Row * Columns + Col] = std::move(CellLayout.m_Lines);
			}
			pLayout->m_vRowHeights[Row] = RowHeight + Pad * 2.0f;
		}
		float Total = 0.0f;
		for(const float RowHeight : pLayout->m_vRowHeights)
			Total += RowHeight;
		pLayout->m_Height = Total;
	}

	SBlockLayout LayoutBlock(const IUiContext &Ctx, const SWikiBlock &Block, float Width, const SWikiRenderStyle &Style)
	{
		ITextRender *pTextRender = Ctx.m_pTextRender;
		SBlockLayout Layout;
		const float BodySize = Style.m_BodySize;
		switch(Block.m_Type)
		{
		case EWikiBlockType::HEADING:
		{
			const float Size = HeadingFontSize(Block.m_Level, Style);
			LayoutLines(WithBold(Block.m_Spans), Width, Size, pTextRender, &Layout);
			break;
		}
		case EWikiBlockType::BULLET:
		case EWikiBlockType::ORDERED:
		{
			const float Indent = BodySize * 1.5f + Block.m_Level * BodySize * 1.4f;
			Layout.m_Indent = Indent;
			LayoutLines(Block.m_Spans, std::max(1.0f, Width - Indent), BodySize, pTextRender, &Layout);
			break;
		}
		case EWikiBlockType::QUOTE:
		{
			const float Indent = BodySize * 1.1f;
			Layout.m_Indent = Indent;
			LayoutLines(Block.m_Spans, std::max(1.0f, Width - Indent), BodySize, pTextRender, &Layout);
			break;
		}
		case EWikiBlockType::CODE:
		case EWikiBlockType::MATH:
		{
			const float Pad = 8.0f * Style.m_UiScale;
			const float FontSize = BodySize * 0.95f;
			Layout.m_FontSize = FontSize;
			Layout.m_LineHeight = FontSize * LINE_SPACING;
			Layout.m_CellPad = Pad;
			std::vector<SWikiSpan> vSpans;
			if(Block.m_Type == EWikiBlockType::MATH)
			{
				std::string Path;
				const SWikiMedia *pMedia = FindMathMedia(Block.m_Text, &Path);
				if(pMedia != nullptr)
				{
					const float Aspect = qm_wiki_media::Aspect(*pMedia);
					Layout.m_HasMedia = true;
					Layout.m_MediaWidth = std::max(1.0f, Width - Pad * 2.0f);
					Layout.m_MediaHeight = std::min(Layout.m_MediaWidth / std::max(0.05f, Aspect), BodySize * 12.0f);
					Layout.m_MediaPath = Path;
					Layout.m_Height = Layout.m_MediaHeight + Pad * 2.0f;
					break;
				}
			}
			// 代码与未预渲染的公式都按等宽段落绘制，公式额外居中。
			SWikiSpan Span;
			Span.m_Text = Block.m_Text;
			Span.m_Style = WIKI_INLINE_CODE;
			vSpans.push_back(std::move(Span));
			BuildLines(vSpans, std::max(1.0f, Width - Pad * 2.0f), FontSize, pTextRender, &Layout.m_Lines);
			Layout.m_Height = Layout.m_Lines.size() * Layout.m_LineHeight + Pad * 2.0f;
			break;
		}
		case EWikiBlockType::DIVIDER:
		{
			Layout.m_Height = 1.0f;
			break;
		}
		case EWikiBlockType::IMAGE:
		{
			const SWikiMedia *pMedia = qm_wiki_media::Get(Block.m_MediaPath.c_str());
			if(pMedia != nullptr)
			{
				const float Aspect = std::max(0.05f, qm_wiki_media::Aspect(*pMedia));
				Layout.m_HasMedia = true;
				Layout.m_MediaHeight = std::min(Width / Aspect, BodySize * 26.0f);
				Layout.m_MediaWidth = Layout.m_MediaHeight * Aspect;
				Layout.m_Height = Layout.m_MediaHeight + BodySize * 0.35f;
			}
			else
			{
				// 缺图时保留占位框，作者能一眼看出路径写错。
				Layout.m_Height = BodySize * 5.5f;
				LayoutLines(Block.m_Spans, Width, BodySize, pTextRender, &Layout);
			}
			break;
		}
		case EWikiBlockType::TABLE:
		{
			LayoutTable(Block, Width, Style, pTextRender, &Layout);
			break;
		}
		case EWikiBlockType::PARAGRAPH:
		default:
		{
			LayoutLines(Block.m_Spans, Width, BodySize, pTextRender, &Layout);
			break;
		}
		}
		return Layout;
	}

	void PaintBlock(const IUiContext &Ctx, const SWikiBlock &Block, const SBlockLayout &Layout, const CUIRect &Area, const SWikiRenderStyle &Style, SWikiLinkContext *pLinks)
	{
		ITextRender *pTextRender = Ctx.m_pTextRender;
		if(pTextRender == nullptr)
			return;
		const float BodySize = Style.m_BodySize;
		switch(Block.m_Type)
		{
		case EWikiBlockType::HEADING:
		{
			const ColorRGBA Color = Block.m_Level <= 2 ? Style.m_Text : Style.m_Text.WithMultipliedAlpha(0.88f);
			DrawLines(Ctx, Layout.m_Lines, Area.x, Area.y, Layout.m_LineHeight, Layout.m_FontSize, Style, Color, pLinks);
			break;
		}
		case EWikiBlockType::BULLET:
		case EWikiBlockType::ORDERED:
		{
			const float Indent = Layout.m_Indent;
			const float MarkerWidth = BodySize * 1.3f;
			if(!Layout.m_Lines.empty())
			{
				pTextRender->TextColor(Style.m_Muted);
				if(Block.m_Type == EWikiBlockType::BULLET)
				{
					pTextRender->Text(Area.x + Block.m_Level * BodySize * 1.4f + BodySize * 0.15f, Area.y, BodySize, "\xe2\x80\xa2");
				}
				else
				{
					char aNumber[16];
					str_format(aNumber, sizeof(aNumber), "%d.", Block.m_Order);
					const float NumberWidth = pTextRender->TextWidth(BodySize, aNumber);
					pTextRender->Text(Area.x + Block.m_Level * BodySize * 1.4f + MarkerWidth - NumberWidth, Area.y, BodySize, aNumber);
				}
			}
			DrawLines(Ctx, Layout.m_Lines, Area.x + Indent, Area.y, Layout.m_LineHeight, Layout.m_FontSize, Style, Style.m_Text, pLinks);
			break;
		}
		case EWikiBlockType::QUOTE:
		{
			CUIRect Bar;
			Bar.x = Area.x + BodySize * 0.2f;
			Bar.y = Area.y;
			Bar.w = 2.0f * Style.m_UiScale;
			Bar.h = Layout.m_Height;
			Bar.Draw(Style.m_Accent.WithMultipliedAlpha(0.55f), IGraphics::CORNER_ALL, 1.0f);
			DrawLines(Ctx, Layout.m_Lines, Area.x + Layout.m_Indent, Area.y, Layout.m_LineHeight, Layout.m_FontSize, Style, Style.m_Muted, pLinks);
			break;
		}
		case EWikiBlockType::CODE:
		case EWikiBlockType::MATH:
		{
			const float Pad = Layout.m_CellPad > 0.0f ? Layout.m_CellPad : 8.0f * Style.m_UiScale;
			CUIRect Box = Area;
			Box.h = Layout.m_Height;
			Box.Draw(Style.m_CodeSurface, IGraphics::CORNER_ALL, ui_token::radius::TIGHT * Style.m_UiScale);
			if(Layout.m_HasMedia)
			{
				CUIRect Media;
				Media.x = Area.x + (Area.w - Layout.m_MediaWidth) * 0.5f;
				Media.y = Area.y + Pad;
				Media.w = Layout.m_MediaWidth;
				Media.h = Layout.m_MediaHeight;
				const SWikiMedia *pMedia = qm_wiki_media::Get(Layout.m_MediaPath.c_str());
				if(pMedia != nullptr)
					qm_wiki_media::Draw(*pMedia, Media, ui_token::color::NEUTRAL_WHITE);
			}
			else
			{
				DrawLines(Ctx, Layout.m_Lines, Area.x + Pad, Area.y + Pad, Layout.m_LineHeight, Layout.m_FontSize, Style, Style.m_Text, pLinks);
			}
			break;
		}
		case EWikiBlockType::DIVIDER:
		{
			CUIRect Line;
			Line.x = Area.x;
			Line.y = Area.y;
			Line.w = Area.w;
			Line.h = std::max(1.0f, Style.m_UiScale);
			Line.Draw(Style.m_Line, IGraphics::CORNER_ALL, 0.0f);
			break;
		}
		case EWikiBlockType::IMAGE:
		{
			const SWikiMedia *pMedia = qm_wiki_media::Get(Block.m_MediaPath.c_str());
			if(pMedia != nullptr && Layout.m_HasMedia)
			{
				CUIRect Media;
				Media.w = Layout.m_MediaWidth;
				Media.h = Layout.m_MediaHeight;
				Media.x = Area.x + (Area.w - Media.w) * 0.5f;
				Media.y = Area.y;
				qm_wiki_media::Draw(*pMedia, Media, ui_token::color::NEUTRAL_WHITE);
			}
			else
			{
				CUIRect Box = Area;
				Box.h = Layout.m_Height;
				Box.Draw(Style.m_CodeSurface, IGraphics::CORNER_ALL, ui_token::radius::TIGHT * Style.m_UiScale);
				char aText[512];
				if(!Block.m_AltText.empty())
					str_format(aText, sizeof(aText), "%s (%s)", Localize("Image not found"), Block.m_MediaPath.c_str());
				else
					str_format(aText, sizeof(aText), "%s: %s", Localize("Image not found"), Block.m_MediaPath.c_str());
				std::vector<SWikiSpan> vSpans;
				CWikiMarkdown::ParseInline(aText, &vSpans);
				std::vector<SLine> vLines;
				BuildLines(vSpans, std::max(1.0f, Area.w - BodySize * 2.0f), BodySize, pTextRender, &vLines);
				const float TextY = Area.y + (Layout.m_Height - vLines.size() * BodySize * LINE_SPACING) * 0.5f;
				DrawLines(Ctx, vLines, Area.x + BodySize, TextY, BodySize * LINE_SPACING, BodySize, Style, Style.m_Muted, pLinks);
			}
			break;
		}
		case EWikiBlockType::TABLE:
		{
			if(Layout.m_Table)
			{
				const float Pad = Layout.m_CellPad;
				float Y = Area.y;
				for(size_t Row = 0; Row < Layout.m_vRowHeights.size(); ++Row)
				{
					const float RowHeight = Layout.m_vRowHeights[Row];
					if(Row > 0)
					{
						CUIRect Line;
						Line.x = Area.x;
						Line.y = Y;
						Line.w = Area.w;
						Line.h = std::max(1.0f, Style.m_UiScale);
						Line.Draw(Style.m_Line, IGraphics::CORNER_ALL, 0.0f);
					}
					float X = Area.x;
					for(size_t Col = 0; Col < Layout.m_vColWidths.size(); ++Col)
					{
						const std::vector<SLine> &vCell = Layout.m_vCells[Row * Layout.m_vColWidths.size() + Col];
						const int Align = Col < Block.m_vAligns.size() && Block.m_vAligns[Col] == 'c' ? 1 : (Col < Block.m_vAligns.size() && Block.m_vAligns[Col] == 'r' ? 2 : -1);
						const ColorRGBA Color = Row == 0 ? Style.m_Text : Style.m_Text.WithMultipliedAlpha(0.92f);
						DrawLines(Ctx, vCell, X + Pad, Y + Pad, Layout.m_LineHeight, Style.m_BodySize, Style, Color, pLinks, Align, Layout.m_vColWidths[Col] - Pad * 2.0f);
						X += Layout.m_vColWidths[Col];
					}
					Y += RowHeight;
				}
			}
			break;
		}
		case EWikiBlockType::PARAGRAPH:
		default:
		{
			DrawLines(Ctx, Layout.m_Lines, Area.x, Area.y, Layout.m_LineHeight, Layout.m_FontSize, Style, Style.m_Text, pLinks);
			break;
		}
		}
	}
} // namespace

float qm_wiki_markdown::RenderBlocks(const IUiContext &Ctx, const std::vector<SWikiBlock> &vBlocks, CUIRect &Body, const SWikiRenderStyle &Style, CScrollRegion *pScroll, SWikiLinkContext *pLinks, int ScrollToBlock)
{
	if(Ctx.m_pTextRender == nullptr)
		return 0.0f;
	const float StartY = Body.y;
	const float Width = std::max(1.0f, Body.w);
	for(size_t Index = 0; Index < vBlocks.size(); ++Index)
	{
		const SWikiBlock &Block = vBlocks[Index];
		const float Margin = TopMargin(Block, Style, Index == 0, Index, vBlocks);
		if(Margin > 0.0f)
			Body.HSplitTop(Margin, nullptr, &Body);
		const SBlockLayout Layout = LayoutBlock(Ctx, Block, Width, Style);
		const float Height = std::max(0.0f, Layout.m_Height);
		CUIRect Slot;
		Body.HSplitTop(Height, &Slot, &Body);
		// 目录跳转：AddRect 的第二个参数让滚动区把这一块顶到可视区。
		const bool Draw = pScroll == nullptr || pScroll->AddRect(Slot, static_cast<int>(Index) == ScrollToBlock);
		if(Draw)
			PaintBlock(Ctx, Block, Layout, Slot, Style, pLinks);
		const float Bottom = BottomMargin(Block, Style);
		if(Bottom > 0.0f)
			Body.HSplitTop(Bottom, nullptr, &Body);
	}
	return Body.y - StartY;
}
