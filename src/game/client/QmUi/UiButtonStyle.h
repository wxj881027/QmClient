#ifndef GAME_CLIENT_QMUI_UIBUTTONSTYLE_H
#define GAME_CLIENT_QMUI_UIBUTTONSTYLE_H

#include "UiTheme.h"

struct SUiSecondaryButtonStyle
{
	ColorRGBA m_Fill;
	ColorRGBA m_Border;
};

// 次级按钮保留配置底色，用公共明暗反馈保证透明或浅色主题也有可见反馈。
inline SUiSecondaryButtonStyle ResolveUiSecondaryButtonStyle(ColorRGBA Surface, ColorRGBA Backdrop, bool Enabled, bool Hovered, bool Pressed)
{
	SUiSecondaryButtonStyle Style{Surface, ui_token::color::BORDER_SUBTLE};
	// 拖出按钮时收起按压反馈，点击提交仍由 CUi 的捕获/释放逻辑负责。
	if(!Enabled || !Hovered)
		return Style;
	const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Surface, Backdrop), true, Hovered, Pressed);
	const float BaseAlpha = std::clamp(Surface.a, 0.0f, 1.0f) * (1.0f - Feedback.a);
	const float Alpha = Feedback.a + BaseAlpha;
	Style.m_Fill = ColorRGBA(
		(Feedback.r * Feedback.a + Surface.r * BaseAlpha) / Alpha,
		(Feedback.g * Feedback.a + Surface.g * BaseAlpha) / Alpha,
		(Feedback.b * Feedback.a + Surface.b * BaseAlpha) / Alpha, Alpha);
	Style.m_Border = Feedback.WithAlpha(ui_token::feedback::ICON_BORDER_ALPHA);
	return Style;
}

#endif
