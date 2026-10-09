#ifndef GAME_CLIENT_QMUI_QMCONSOLEUI_H
#define GAME_CLIENT_QMUI_QMCONSOLEUI_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
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

inline SToolbarLayout LayoutToolbar(float Width, float FilterWidth, float ActionWidth)
{
	const float Available = std::max(0.0f, Width - 20.0f);
	const bool Split = FilterWidth > 0.0f && ActionWidth > 0.0f && FilterWidth + ActionWidth + 10.0f > Available;
	const float FilterScale = FilterWidth > 0.0f ? std::min(1.0f, Available / FilterWidth) : 1.0f;
	const float ActionScale = ActionWidth > 0.0f ? std::min(1.0f, Available / ActionWidth) : 1.0f;
	return {Split, Split ? 52.0f : 26.0f, FilterScale, ActionScale,
		std::max(10.0f, Width - 10.0f - ActionWidth * ActionScale)};
}

inline void DrawPanel(CUi *pUi, const CUIRect &Rect, ColorRGBA Color)
{
	const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
	Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);
}

// 控制台使用独立的鼠标/触摸按下位置；统一在按钮内松开时触发一次。
inline bool Button(CUi *pUi, const CUIRect &Rect, const char *pLabel, float FontSize, bool Selected,
	vec2 PressPosition, vec2 MousePosition, bool MouseDown, bool Released, bool Enabled)
{
	const bool Hovered = Rect.Inside(MousePosition);
	const bool PressedInside = Rect.Inside(PressPosition);
	const bool Pressed = Enabled && MouseDown && PressedInside;
	const ColorRGBA Fill = Selected ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(Enabled ? 1.0f : 0.65f) : ResolveConfiguredControlSurface(Enabled);
	const CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
	DrawRoundedSurface(pUi, Rect, Fill, ColorRGBA(), ui_token::radius::BASE);
	const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Fill), Enabled, Hovered, Pressed);
	if(Feedback.a > 0.0f)
		DrawRoundedSurface(pUi, Rect, Feedback, ColorRGBA(), ui_token::radius::BASE);
	const CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
	SLabelProperties Props;
	Props.m_MaxWidth = Rect.w;
	Props.m_MinimumFontSize = FontSize;
	Props.m_EllipsisAtEnd = true;
	pUi->DoLabel(&Rect, pLabel, FontSize, TEXTALIGN_MC, Props);
	return Enabled && Released && PressedInside && Hovered;
}
}

#endif
