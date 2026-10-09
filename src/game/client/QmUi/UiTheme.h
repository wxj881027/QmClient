/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITHEME_H
#define GAME_CLIENT_QMUI_UITHEME_H

#include "UiTokens.h"

#include <base/color.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>
#include <optional>

// 透明表面先与底层合成，完全透明时由底层决定前景，避免按不可见 RGB 选色。
inline ColorRGBA CompositeUiSurface(ColorRGBA Surface, ColorRGBA Backdrop = ui_token::color::SURFACE_BACKDROP)
{
	const float BackdropAlpha = std::clamp(Backdrop.a, 0.0f, 1.0f);
	Backdrop = ColorRGBA(Backdrop.r * BackdropAlpha + ui_token::color::SURFACE_BACKDROP.r * (1.0f - BackdropAlpha), Backdrop.g * BackdropAlpha + ui_token::color::SURFACE_BACKDROP.g * (1.0f - BackdropAlpha), Backdrop.b * BackdropAlpha + ui_token::color::SURFACE_BACKDROP.b * (1.0f - BackdropAlpha), 1.0f);
	const float Alpha = std::clamp(Surface.a, 0.0f, 1.0f);
	return ColorRGBA(Surface.r * Alpha + Backdrop.r * (1.0f - Alpha), Surface.g * Alpha + Backdrop.g * (1.0f - Alpha), Surface.b * Alpha + Backdrop.b * (1.0f - Alpha), 1.0f);
}

inline float UiSurfaceLuminance(ColorRGBA Color)
{
	const auto Linear = [](float Value) { Value = std::clamp(Value, 0.0f, 1.0f); return Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f); };
	return 0.2126f * Linear(Color.r) + 0.7152f * Linear(Color.g) + 0.0722f * Linear(Color.b);
}

inline ColorRGBA ResolveUiSurfaceForeground(ColorRGBA Surface, ColorRGBA Backdrop = ui_token::color::SURFACE_BACKDROP)
{
	const float Luminance = UiSurfaceLuminance(CompositeUiSurface(Surface, Backdrop));
	return Luminance > 0.179f ? ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
}

// 手动文本模式保持用户选择；非法模式回退自动，图标策略独立。
inline ColorRGBA ResolveUiTextColor(ColorRGBA Surface, int Mode, unsigned CustomColor, ColorRGBA Backdrop = ui_token::color::SURFACE_BACKDROP)
{
	switch(Mode)
	{
	case 1: return ColorRGBA(1, 1, 1, 1);
	case 2: return ColorRGBA(0, 0, 0, 1);
	case 3: return color_cast<ColorRGBA>(ColorHSLA(CustomColor)).WithAlpha(1.0f);
	default: return ResolveUiSurfaceForeground(Surface, Backdrop);
	}
}

inline ColorRGBA ResolveConfiguredTextColor(ColorRGBA Surface)
{
	return ResolveUiTextColor(Surface, g_Config.m_QmUiTextColorMode, g_Config.m_QmUiTextCustomColor);
}

// 反馈独立叠加，不修改基础表面的用户颜色或按钮几何。
inline ColorRGBA ResolveUiIconButtonFeedback(ColorRGBA Surface, bool Enabled, bool Hovered, bool Pressed)
{
	if(!Enabled || (!Hovered && !Pressed))
		return ColorRGBA(0, 0, 0, 0);
	return ResolveUiSurfaceForeground(Surface).WithAlpha(Pressed ? ui_token::feedback::ICON_PRESS_ALPHA : ui_token::feedback::ICON_HOVER_ALPHA);
}

// 自定义图标色保持可辨识时保留；颜色与表面接近时由公共前景兜底。
inline ColorRGBA ResolveUiSurfaceIconColor(ColorRGBA Surface, ColorRGBA Preferred)
{
	const float Background = UiSurfaceLuminance(CompositeUiSurface(Surface));
	const float Foreground = UiSurfaceLuminance(Preferred);
	const float Contrast = (std::max(Background, Foreground) + 0.05f) / (std::min(Background, Foreground) + 0.05f);
	return Contrast >= 3.0f ? Preferred : ResolveUiSurfaceForeground(Surface).WithAlpha(Preferred.a);
}

// 色盘显示与点击提交共用 HSV 坐标，右上角是当前色相的纯色。
inline ColorHSVA ResolveUiColorPickerSelection(float Hue, float X, float Y, float Alpha)
{
	return ColorHSVA(std::clamp(Hue, 0.0f, 1.0f), std::clamp(X, 0.0f, 1.0f), 1.0f - std::clamp(Y, 0.0f, 1.0f), std::clamp(Alpha, 0.0f, 1.0f));
}

struct SUiTheme
{
	ColorRGBA m_Surface;
	ColorRGBA m_SurfaceHovered;
	ColorRGBA m_SurfaceFocused;
	ColorRGBA m_Border;
	ColorRGBA m_BorderHovered;
	ColorRGBA m_BorderFocused;
	ColorRGBA m_InputSurface;
	ColorRGBA m_InputSurfaceFocused;
	ColorRGBA m_FocusRing;
	ColorRGBA m_Accent;
	ColorRGBA m_Selected;
	ColorRGBA m_TextTitle;
	ColorRGBA m_TextBody;
	ColorRGBA m_TextSmall;
	float m_FocusRingWidth = 2.0f;
	float m_FocusRingInset = 1.0f;
};

struct SUiToggleStyle
{
	ColorRGBA m_Track;
	ColorRGBA m_Knob;
	ColorRGBA m_Border;
};

inline SUiToggleStyle ResolveUiToggleStyle(const SUiTheme &Theme, ColorRGBA ControlSurface, ColorRGBA Backdrop, bool Value, bool Enabled)
{
	SUiToggleStyle Style;
	Style.m_Track = Value ? Theme.m_Accent : ControlSurface;
	Style.m_Knob = ResolveUiSurfaceForeground(Style.m_Track, Backdrop);
	if(!Enabled)
	{
		Style.m_Track.a *= 0.65f;
		Style.m_Knob.a *= 0.65f;
	}
	return Style;
}

struct SUiSliderStyle
{
	ColorRGBA m_Track;
	ColorRGBA m_Fill;
	ColorRGBA m_Handle;
	ColorRGBA m_Border;
};

inline SUiSliderStyle ResolveUiSliderStyle(const SUiTheme &Theme, ColorRGBA Backdrop, bool Hovered, bool Pressed, bool Enabled = true)
{
	const ColorRGBA Foreground = ResolveUiSurfaceForeground(Backdrop);
	SUiSliderStyle Style;
	Style.m_Track = Foreground.WithAlpha(0.25f);
	Style.m_Fill = Theme.m_Accent.WithMultipliedAlpha(0.85f);
	Style.m_Handle = ResolveUiSurfaceIconColor(Backdrop, Theme.m_Accent);
	Style.m_Border = Foreground.WithAlpha(Enabled && (Hovered || Pressed) ? 0.70f : 0.30f);
	if(!Enabled)
	{
		Style.m_Track.a *= 0.65f;
		Style.m_Fill.a *= 0.65f;
		Style.m_Handle.a *= 0.65f;
	}
	return Style;
}

inline SUiTheme ResolveUiTheme(const ColorHSLA BaseColor, float Opacity, const ColorHSLA FocusColor = ColorHSLA(0.60f, 0.78f, 0.52f, 1.0f), const ColorHSLA AccentColor = ColorHSLA(0x8FDDAD), const ColorHSLA SelectedColor = ColorHSLA(0x8FDDAD))
{
	Opacity = std::clamp(Opacity, 0.0f, 1.0f);
	const ColorRGBA SurfaceBase = color_cast<ColorRGBA>(BaseColor);
	const ColorRGBA AccentBase = color_cast<ColorRGBA>(AccentColor);
	const float AccentAlpha = std::clamp(AccentBase.a, 0.0f, 1.0f);
	const ColorRGBA SelectedBase = color_cast<ColorRGBA>(SelectedColor);
	SUiTheme Theme;
	Theme.m_Surface = SurfaceBase.WithAlpha(std::clamp(std::max(SurfaceBase.a, 0.70f) * Opacity, 0.0f, 1.0f));
	Theme.m_SurfaceHovered = ColorRGBA(
		std::clamp(Theme.m_Surface.r * 1.06f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.g * 1.06f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.b * 1.06f, 0.0f, 1.0f), Theme.m_Surface.a);
	Theme.m_SurfaceFocused = Theme.m_SurfaceHovered;
	Theme.m_Border = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Opacity);
	Theme.m_BorderHovered = AccentBase.WithAlpha(0.45f * Opacity * AccentAlpha);
	Theme.m_BorderFocused = AccentBase.WithAlpha(0.75f * Opacity * AccentAlpha);
	Theme.m_InputSurface = ColorRGBA(
		std::clamp(Theme.m_Surface.r * 0.88f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.g * 0.88f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.b * 0.88f, 0.0f, 1.0f), Theme.m_Surface.a);
	Theme.m_InputSurfaceFocused = Theme.m_InputSurface;
	// 焦点环是键盘与输入焦点的唯一稳定反馈，不随低表面透明度弱化到不可辨认。
	Theme.m_FocusRing = color_cast<ColorRGBA>(FocusColor).WithAlpha(std::clamp(std::max(0.60f, 0.90f * Opacity), 0.0f, 1.0f));
	Theme.m_Accent = AccentBase.WithAlpha(AccentAlpha);
	Theme.m_Selected = SelectedBase.WithAlpha(0.22f);
	Theme.m_TextTitle = ResolveConfiguredTextColor(Theme.m_Surface);
	Theme.m_TextBody = Theme.m_TextTitle;
	Theme.m_TextSmall = Theme.m_TextTitle;
	return Theme;
}

// 二级面板统一读取独立背景色与透明度，边框沿用全局卡片配置。
inline SUiTheme ResolveSecondaryPanelTheme(unsigned BackgroundColor, int Opacity, unsigned BorderColor)
{
	SUiTheme Theme = ResolveUiTheme(ColorHSLA(BackgroundColor), 1.0f);
	Theme.m_Surface = Theme.m_Surface.WithAlpha(std::clamp(Opacity / 100.0f, 0.0f, 1.0f));
	Theme.m_Border = color_cast<ColorRGBA>(ColorHSLA(BorderColor, true));
	Theme.m_TextTitle = ResolveConfiguredTextColor(Theme.m_Surface);
	Theme.m_TextBody = Theme.m_TextTitle;
	Theme.m_TextSmall = Theme.m_TextTitle;
	return Theme;
}

// 配置颜色直接转为表面，不按透明度猜测角色。
inline ColorRGBA ResolveDropdownSurface(unsigned BackgroundColor, int Opacity)
{
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(BackgroundColor));
	return Color.WithAlpha(std::clamp(Opacity / 100.0f, 0.0f, 1.0f));
}

// 普通按钮与下拉触发器沿用已保存配置键。
inline ColorRGBA ResolveUiControlSurface(unsigned BackgroundColor, int Opacity, bool Enabled = true)
{
	return ResolveDropdownSurface(BackgroundColor, Opacity).WithMultipliedAlpha(Enabled ? 1.0f : 0.65f);
}

inline ColorRGBA ResolveConfiguredControlSurface(bool Enabled = true)
{
	return ResolveUiControlSurface(g_Config.m_QmUiDropdownColor, g_Config.m_QmUiDropdownOpacity, Enabled);
}

// 显式状态色保留原始透明度，普通图标按钮才回退到配置表面。
inline ColorRGBA ResolveConfiguredIconButtonSurface(const std::optional<ColorRGBA> &ButtonColor, bool Enabled = true)
{
	return ButtonColor.value_or(ResolveConfiguredControlSurface(Enabled));
}

inline ColorRGBA ResolveConfiguredDropdownSurface()
{
	return ResolveDropdownSurface(g_Config.m_QmUiDropdownColor, g_Config.m_QmUiDropdownOpacity);
}

inline ColorRGBA ResolveConfiguredInputSurface(bool Enabled = true)
{
	return ResolveUiControlSurface(g_Config.m_QmUiInputColor, g_Config.m_QmUiInputOpacity, Enabled);
}

inline SUiTheme ResolveConfiguredDropdownListTheme()
{
	return ResolveSecondaryPanelTheme(g_Config.m_QmUiDropdownListColor, g_Config.m_QmUiDropdownListOpacity, g_Config.m_QmUiCardBorderColor);
}

inline SUiTheme ResolveConfiguredSecondaryPanelTheme()
{
	return ResolveSecondaryPanelTheme(g_Config.m_QmUiPopupColor, g_Config.m_QmUiPopupOpacity, g_Config.m_QmUiCardBorderColor);
}

inline SUiTheme ResolveInputFallbackTheme(const unsigned FocusColor)
{
	// 某些设置页的旧调用点只提供最小 IUiContext。回退主题仍必须与
	// SettingsUiContext 使用相同的用户颜色和透明度，避免同一输入组件出现两套外壳。
	return ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(FocusColor), ColorHSLA(g_Config.m_QmUiAccentColor).WithAlpha(g_Config.m_QmUiAccentOpacity / 100.0f), ColorHSLA(g_Config.m_QmUiSelectedColor));
}

#endif
