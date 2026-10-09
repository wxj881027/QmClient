#ifndef GAME_CLIENT_QMUI_QMCONSOLEUI_H
#define GAME_CLIENT_QMUI_QMCONSOLEUI_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/components/qmclient/console_appearance.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

namespace QmConsoleUi
{
	struct SToolbarLayout
	{
		bool m_SplitRows;
		float m_Height;
		float m_FilterScale;
		float m_ActionScale;
		float m_ActionX;
	};

	inline SToolbarLayout LayoutToolbar(float Width, float FilterWidth, float ActionWidth, float RowHeight = 26.0f)
	{
		const float Available = std::max(0.0f, Width - 20.0f);
		const bool Split = FilterWidth > 0.0f && ActionWidth > 0.0f && FilterWidth + ActionWidth + 10.0f > Available;
		const float FilterScale = FilterWidth > 0.0f ? std::min(1.0f, Available / FilterWidth) : 1.0f;
		const float ActionScale = ActionWidth > 0.0f ? std::min(1.0f, Available / ActionWidth) : 1.0f;
		return {Split, RowHeight * (Split ? 2.0f : 1.0f), FilterScale, ActionScale,
			std::max(10.0f, Width - 10.0f - ActionWidth * ActionScale)};
	}

	inline void DrawPanel(CUi *pUi, const CUIRect &Rect, ColorRGBA Color)
	{
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);
	}

	// 控制台使用独立的鼠标/触摸按下位置；统一在按钮内松开时触发一次。
	inline bool Button(CUi *pUi, const CUIRect &Rect, const char *pLabel, float FontSize, bool Selected,
		vec2 PressPosition, vec2 MousePosition, bool MouseDown, bool Released, bool Enabled, float FilterIndicatorWidth = 0.0f, const QmConsoleAppearance::SPalette *pPalette = nullptr)
	{
		const bool Hovered = Rect.Inside(MousePosition);
		const bool PressedInside = Rect.Inside(PressPosition);
		const bool Pressed = Enabled && MouseDown && PressedInside;
		ColorRGBA Fill;
		if(pPalette != nullptr)
			Fill = Selected ? pPalette->m_SelectedButton : pPalette->m_Panel;
		else
			Fill = Selected ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(Enabled ? 1.0f : 0.65f) : ResolveConfiguredControlSurface(Enabled);
		const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		DrawRoundedSurface(pUi, Rect, Fill, ColorRGBA(), ui_token::radius::BASE);
		const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Fill), Enabled, Hovered, Pressed);
		if(Feedback.a > 0.0f)
			DrawRoundedSurface(pUi, Rect, Feedback, ColorRGBA(), ui_token::radius::BASE);
		const CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
		if(pPalette != nullptr)
		{
			pUi->TextRender()->TextColor(pPalette->m_aColors[QmConsoleAppearance::TEXT]);
			pUi->TextRender()->TextOutlineColor(ResolveUiSurfaceForeground(pPalette->m_aColors[QmConsoleAppearance::TEXT]).WithAlpha(pUi->TextRender()->GetTextOutlineColor().a));
		}
		CUIRect Label = Rect;
		if(FilterIndicatorWidth > 0.0f)
		{
			// 分类按钮固定预留标记位置，避免切换状态时文字跳动；被筛掉的分类显示禁用符号。
			CUIRect Indicator;
			Label.VSplitLeft(std::min(FilterIndicatorWidth, Label.w), &Indicator, &Label);
			if(!Selected)
			{
				const float IconSize = std::min({Indicator.w, Indicator.h, FontSize * 1.25f});
				const CUIRect Icon = {Indicator.x + (Indicator.w - IconSize) * 0.5f, Indicator.y + (Indicator.h - IconSize) * 0.5f, IconSize, IconSize};
				const CQmIconSemanticColorScope SemanticColorScope;
				pUi->DrawQmIcon(Icon, EQmIcon::BAN, FontIcons::FONT_ICON_BAN, pUi->TextRender()->GetTextColor());
			}
		}
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_MinimumFontSize = FontSize;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_MC, Props);
		return Enabled && Released && PressedInside && Hovered;
	}
}

#endif
