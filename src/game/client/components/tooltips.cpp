#include "tooltips.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsCardHelp.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
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
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? 10.0f : 14.0f, Small, false);
}

void CTooltips::DoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? 10.0f : 14.0f, Small, true);
}

void CTooltips::DoInfoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize)
{
	DoToolTip(pId, pNearRect, pText, WidthHint, FontSize, false, true, true);
}

void CTooltips::DoSmallToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float FontSize, float WidthHint)
{
	DoToolTip(pId, pNearRect, pText, WidthHint, FontSize, true, true);
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, const float FontSize, const bool SmallInstant, const bool HoverByRect, const bool Immediate)
{
	if(m_pCardHelp != nullptr)
	{
		const bool ReadOnly = Ui()->RenderOnly();
		const bool Hovered = !ReadOnly && Ui()->MouseHovered(pNearRect) && (HoverByRect || Ui()->HotItem() == pId);
		const bool Focused = !ReadOnly && (Ui()->ActiveItem() == pId || CLineInput::GetActiveInput() == pId);
		m_pCardHelp->Register(reinterpret_cast<uintptr_t>(pId), pText, Hovered, Focused, [&](const char *pHelp) {
			return TextRender()->TextBoundingBox(m_pCardHelp->FontSize(), pHelp, -1, std::max(1.0f, m_pCardHelp->Width())).m_H;
		});
		return;
	}
	if(Ui()->RenderOnly())
		return;
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
	Tooltip.m_Immediate = Immediate;
	if(Tooltip.m_SmallInstant != SmallInstant)
		Tooltip.m_FadeTime = SmallInstant ? 0.0f : 0.75f;
	Tooltip.m_SmallInstant = SmallInstant;
	if(SmallInstant)
		Tooltip.m_FadeTime = 0.0f;

	Tooltip.m_OnScreen = true;

	if(QmTooltipHovered(Tooltip, *Ui()))
	{
		SetActiveTooltip(Tooltip);
	}
}

void CTooltips::OnRender()
{
	if(m_ActiveTooltip.has_value())
	{
		CTooltip &Tooltip = m_ActiveTooltip.value();

		if(!QmTooltipHovered(Tooltip, *Ui()))
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
		const float SecondsBeforeFadeIn = (Tooltip.m_SmallInstant || Tooltip.m_Immediate) ? 0.0f : Tooltip.m_FadeTime;

		const float SecondsSinceActivation = (time_get() - m_HoverTime) / (float)time_freq();
		if(SecondsSinceActivation < SecondsBeforeFadeIn)
			return;
		const bool Animate = !Tooltip.m_SmallInstant && g_Config.m_QmTooltipAnimation && g_Config.m_QmUiMotionLevel > 0;
		const float SecondsFadeIn = !Animate || Tooltip.m_Immediate ? 0.0f : 0.25f;
		const float AlphaFactor = SecondsSinceActivation < SecondsBeforeFadeIn + SecondsFadeIn ? (SecondsSinceActivation - SecondsBeforeFadeIn) / SecondsFadeIn : 1.0f;
		CUiScopedGaussianBlur GaussianBlurScope(Ui(), AlphaFactor);

		const float Scale = QmTooltipScale(SecondsSinceActivation - SecondsBeforeFadeIn, Animate);
		const float BaseFontSize = Tooltip.m_FontSize * (Tooltip.m_SmallInstant ? 1.0f : std::clamp(g_Config.m_QmTooltipFontSize, 10, 24) / 14.0f);
		const float FontSize = BaseFontSize * Scale;
		const float Margin = Tooltip.m_SmallInstant ? 4.0f : 5.0f;
		const float Padding = Tooltip.m_SmallInstant ? 0.0f : 5.0f * Scale;

		const CUIRect *pScreen = Ui()->Screen();
		const float MaxTextWidth = maximum(1.0f, pScreen->w - 2.0f * (Margin + Padding));
		const float TextWidth = Tooltip.m_WidthHint > 0.0f ? minimum(Tooltip.m_WidthHint, MaxTextWidth) : MaxTextWidth;
		const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(BaseFontSize, Tooltip.m_Text.c_str(), -1, TextWidth);
		CUIRect Rect = QmTooltipRect(Tooltip.m_Rect, *pScreen, vec2(BoundingBox.m_W * Scale + 2 * Padding, BoundingBox.m_H * Scale + 2 * Padding), Margin);

		if(!Tooltip.m_SmallInstant)
		{
			ColorRGBA Background = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipBackgroundColor, true));
			Background.a *= AlphaFactor;
			Rect.Draw(Background, IGraphics::CORNER_ALL, Padding);
			Rect.Margin(Padding, &Rect);
		}

		CTextCursor Cursor;
		Cursor.SetPosition(Rect.TopLeft());
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = std::max(1.0f, Rect.w);
		// 极窄视口或超长说明按可见行数收口，保留省略提示，避免文字溢出气泡。
		const int VisibleLines = QmTooltipVisibleLines(Rect.h, FontSize);
		const bool Truncated = BoundingBox.m_H * Scale > Rect.h;
		Cursor.m_MaxLines = Truncated ? std::max(1, VisibleLines - 1) : 0;

		STextContainerIndex TextContainerIndex;
		const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		TextRender()->CreateTextContainer(TextContainerIndex, &Cursor, Tooltip.m_Text.c_str());
		TextRender()->SetRenderFlags(OldRenderFlags);

		if(TextContainerIndex.Valid())
		{
			ColorRGBA TextColor = Tooltip.m_SmallInstant ? TextRender()->DefaultTextColor() : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipTextColor, true));
			TextColor.a *= AlphaFactor;
			ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
			OutlineColor.a *= AlphaFactor;
			Ui()->ClipEnable(&Rect);
			if(!Truncated || VisibleLines > 1)
				TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor);
			if(Truncated)
			{
				CUIRect End = Rect;
				End.y += std::max(0, VisibleLines - 1) * FontSize;
				End.h = FontSize;
				const ColorRGBA OldColor = TextRender()->GetTextColor();
				TextRender()->TextColor(TextColor);
				Ui()->DoLabel(&End, "…", FontSize, TEXTALIGN_TL);
				TextRender()->TextColor(OldColor);
			}
			Ui()->ClipDisable();
		}

		TextRender()->DeleteTextContainer(TextContainerIndex);

		Tooltip.m_OnScreen = false;
	}
}
