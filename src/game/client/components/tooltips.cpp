#include "tooltips.h"

#include <game/client/ui.h>

#include <algorithm>

CTooltips::CTooltips()
{
	CTooltips::OnReset();
}

void CTooltips::OnReset()
{
	m_HoverTime = -1;
	m_Tooltips.clear();
	ClearActiveTooltip();
}

void CTooltips::SetActiveTooltip(CTooltip &Tooltip)
{
	m_ActiveTooltip.emplace(Tooltip);
}

inline void CTooltips::ClearActiveTooltip()
{
	m_ActiveTooltip.reset();
	m_PreviousTooltip.reset();
}

// TClient
void CTooltips::SetFadeTime(const void *pId, float Time)
{
	uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto Iter = m_Tooltips.find(Id);
	if(Iter != m_Tooltips.end())
	{
		Iter->second.m_FadeTime = Time;
	}
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	DoToolTip(pId, pNearRect, pText, WidthHint, 14.0f, false, false);
}

void CTooltips::DoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	DoToolTip(pId, pNearRect, pText, WidthHint, 14.0f, false, true);
}

void CTooltips::DoSmallToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float FontSize, float WidthHint)
{
	DoToolTip(pId, pNearRect, pText, WidthHint, FontSize, true, true);
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, const float FontSize, const bool SmallInstant, const bool HoverByRect)
{
	uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto &[Entry, WasInserted] = m_Tooltips.emplace(Id, CTooltip{
									  pId,
									  *pNearRect,
									  pText != nullptr ? pText : "",
									  WidthHint,
									  false});
	CTooltip &Tooltip = Entry->second;

	if(!WasInserted)
	{
		Tooltip.m_Rect = *pNearRect; // update in case of window resize
		Tooltip.m_Text = pText != nullptr ? pText : ""; // update in case of language change
	}
	Tooltip.m_FontSize = std::max(1.0f, FontSize);
	Tooltip.m_WidthHint = WidthHint;
	Tooltip.m_HoverByRect = HoverByRect;
	if(Tooltip.m_SmallInstant != SmallInstant)
		Tooltip.m_FadeTime = SmallInstant ? 0.0f : 0.75f;
	Tooltip.m_SmallInstant = SmallInstant;
	if(SmallInstant)
		Tooltip.m_FadeTime = 0.0f;

	Tooltip.m_OnScreen = true;

	if(HoverByRect ? Ui()->MouseHovered(pNearRect) : Ui()->HotItem() == Tooltip.m_pId)
	{
		SetActiveTooltip(Tooltip);
	}
}

void CTooltips::OnRender()
{
	if(m_ActiveTooltip.has_value())
	{
		CTooltip &Tooltip = m_ActiveTooltip.value();

		if((!Tooltip.m_HoverByRect && Ui()->HotItem() != Tooltip.m_pId) || !Tooltip.m_Rect.Inside(Ui()->MousePos()))
		{
			Tooltip.m_OnScreen = false;
			ClearActiveTooltip();
			return;
		}
		if(!Tooltip.m_OnScreen)
			return;

		// Reset hover time if a different tooltip is active.
		// Only reset hover time when rendering, because multiple tooltips can be
		// activated in the same frame, but only the last one should be rendered.
		if(!m_PreviousTooltip.has_value() || &m_PreviousTooltip.value().get() != &Tooltip)
			m_HoverTime = time_get();
		m_PreviousTooltip.emplace(Tooltip);

		// 小字提示立即显示，普通提示继续使用原有延迟和淡入。
		const float SecondsBeforeFadeIn = Tooltip.m_SmallInstant ? 0.0f : Tooltip.m_FadeTime;

		const float SecondsSinceActivation = (time_get() - m_HoverTime) / (float)time_freq();
		if(SecondsSinceActivation < SecondsBeforeFadeIn)
			return;
		const float SecondsFadeIn = Tooltip.m_SmallInstant ? 0.0f : 0.25f;
		const float AlphaFactor = SecondsSinceActivation < SecondsBeforeFadeIn + SecondsFadeIn ? (SecondsSinceActivation - SecondsBeforeFadeIn) / SecondsFadeIn : 1.0f;
		CUiScopedGaussianBlur GaussianBlurScope(Ui(), AlphaFactor);

		const float FontSize = Tooltip.m_FontSize;
		const float Margin = Tooltip.m_SmallInstant ? 4.0f : 5.0f;
		const float Padding = Tooltip.m_SmallInstant ? 0.0f : 5.0f;

		const CUIRect *pScreen = Ui()->Screen();
		const float MaxTextWidth = maximum(1.0f, pScreen->w - 2.0f * (Margin + Padding));
		const float TextWidth = Tooltip.m_WidthHint > 0.0f ? minimum(Tooltip.m_WidthHint, MaxTextWidth) : MaxTextWidth;
		const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, Tooltip.m_Text.c_str(), -1, TextWidth);
		CUIRect Rect;
		Rect.w = BoundingBox.m_W + 2 * Padding;
		Rect.h = BoundingBox.m_H + 2 * Padding;

		Rect.w = minimum(Rect.w, pScreen->w - 2 * Margin);
		Rect.h = minimum(Rect.h, pScreen->h - 2 * Margin);
		Rect.x = pScreen->x + Margin;
		Rect.y = pScreen->y + Margin;

		// Try the top side.
		if(Tooltip.m_Rect.y - Rect.h - Margin > pScreen->y)
		{
			Rect.x = std::clamp(Ui()->MouseX() - Rect.w / 2.0f, Margin, pScreen->w - Rect.w - Margin);
			Rect.y = Tooltip.m_Rect.y - Rect.h - Margin;
		}
		// Try the bottom side.
		else if(Tooltip.m_Rect.y + Tooltip.m_Rect.h + Margin + Rect.h < pScreen->y + pScreen->h)
		{
			Rect.x = std::clamp(Ui()->MouseX() - Rect.w / 2.0f, Margin, pScreen->w - Rect.w - Margin);
			Rect.y = Tooltip.m_Rect.y + Tooltip.m_Rect.h + Margin;
		}
		// Try the right side.
		else if(Tooltip.m_Rect.x + Tooltip.m_Rect.w + Margin + Rect.w < pScreen->w)
		{
			Rect.x = Tooltip.m_Rect.x + Tooltip.m_Rect.w + Margin;
			Rect.y = std::clamp(Ui()->MouseY() - Rect.h / 2.0f, Margin, pScreen->h - Rect.h - Margin);
		}
		// Try the left side.
		else if(Tooltip.m_Rect.x - Rect.w - Margin > pScreen->x)
		{
			Rect.x = Tooltip.m_Rect.x - Rect.w - Margin;
			Rect.y = std::clamp(Ui()->MouseY() - Rect.h / 2.0f, Margin, pScreen->h - Rect.h - Margin);
		}

		if(!Tooltip.m_SmallInstant)
		{
			Rect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.8f * AlphaFactor), IGraphics::CORNER_ALL, Padding);
			Rect.Margin(Padding, &Rect);
		}

		CTextCursor Cursor;
		Cursor.SetPosition(Rect.TopLeft());
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = TextWidth;

		STextContainerIndex TextContainerIndex;
		const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		TextRender()->CreateTextContainer(TextContainerIndex, &Cursor, Tooltip.m_Text.c_str());
		TextRender()->SetRenderFlags(OldRenderFlags);

		if(TextContainerIndex.Valid())
		{
			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			TextColor.a *= AlphaFactor;
			ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
			OutlineColor.a *= AlphaFactor;
			TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor);
		}

		TextRender()->DeleteTextContainer(TextContainerIndex);

		Tooltip.m_OnScreen = false;
	}
}
