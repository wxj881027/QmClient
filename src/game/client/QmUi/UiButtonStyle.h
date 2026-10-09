#ifndef GAME_CLIENT_QMUI_UIBUTTONSTYLE_H
#define GAME_CLIENT_QMUI_UIBUTTONSTYLE_H

#include "UiTheme.h"

struct SUiSecondaryButtonStyle
{
	ColorRGBA m_Fill;
	ColorRGBA m_Border;
};

enum class EUiButtonRole
{
	SECONDARY,
	PRIMARY,
	ICON,
	LIST_ENTRY,
};

struct SUiButtonState
{
	bool m_Enabled = true;
	bool m_Hovered = false;
	bool m_Pressed = false;
	bool m_Selected = false;
};

// 保留基础表面的透明度；状态反馈作为前景覆盖层合成。
inline ColorRGBA BlendUiButtonSurface(ColorRGBA Surface, ColorRGBA Overlay)
{
	const float OverlayAlpha = std::clamp(Overlay.a, 0.0f, 1.0f);
	const float BaseAlpha = std::clamp(Surface.a, 0.0f, 1.0f) * (1.0f - OverlayAlpha);
	const float Alpha = OverlayAlpha + BaseAlpha;
	if(Alpha <= 0.0f)
		return Surface.WithAlpha(0.0f);
	return ColorRGBA(
		(Overlay.r * OverlayAlpha + Surface.r * BaseAlpha) / Alpha,
		(Overlay.g * OverlayAlpha + Surface.g * BaseAlpha) / Alpha,
		(Overlay.b * OverlayAlpha + Surface.b * BaseAlpha) / Alpha, Alpha);
}

// 次级按钮保留配置底色，用公共明暗反馈保证透明或浅色主题也有可见反馈。
inline SUiSecondaryButtonStyle ResolveUiSecondaryButtonStyle(ColorRGBA Surface, ColorRGBA Backdrop, bool Enabled, bool Hovered, bool Pressed)
{
	SUiSecondaryButtonStyle Style{Surface, ui_token::color::BORDER_SUBTLE};
	// 拖出按钮时收起按压反馈，点击提交仍由 CUi 的捕获/释放逻辑负责。
	if(!Enabled || !Hovered)
		return Style;
	const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Surface, Backdrop), true, Hovered, Pressed);
	Style.m_Fill = BlendUiButtonSurface(Surface, Feedback);
	Style.m_Border = Feedback.WithAlpha(ui_token::feedback::ICON_BORDER_ALPHA);
	return Style;
}

// 滑块沿用基础轨道选出的前景，悬浮遮罩和轨道动画不再触发黑白反转。
inline SUiToggleStyle ResolveUiToggleFeedbackStyle(const SUiToggleStyle &Base, ColorRGBA Track, ColorRGBA Backdrop, bool Enabled, bool Hovered, bool Pressed)
{
	const auto Feedback = ResolveUiSecondaryButtonStyle(Track, Backdrop, Enabled, Hovered, Pressed);
	SUiToggleStyle Style = Base;
	Style.m_Track = Feedback.m_Fill;
	Style.m_Border = Feedback.m_Border;
	return Style;
}

// 同类按钮只保留用途差异，主题、禁用和鼠标反馈由这一处解析。
inline SUiSecondaryButtonStyle ResolveUiButtonStyle(EUiButtonRole Role, ColorRGBA Surface, ColorRGBA Backdrop, const SUiTheme &Theme, const SUiButtonState &State)
{
	if(Role == EUiButtonRole::PRIMARY)
		Surface = Theme.m_Accent.WithAlpha(Theme.m_Accent.a * 0.18f);
	if(State.m_Selected)
		Surface = BlendUiButtonSurface(Surface, Theme.m_Selected);
	if(!State.m_Enabled)
		Surface.a *= 0.65f;
	if(Role == EUiButtonRole::PRIMARY && State.m_Enabled && State.m_Hovered)
		Surface = Theme.m_Accent;
	auto Style = ResolveUiSecondaryButtonStyle(Surface, Backdrop, State.m_Enabled, State.m_Hovered, State.m_Pressed);
	if(Role == EUiButtonRole::LIST_ENTRY && (!State.m_Enabled || !State.m_Hovered))
		Style.m_Border = ColorRGBA();
	return Style;
}

#endif
