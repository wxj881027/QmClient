/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITHEME_H
#define GAME_CLIENT_QMUI_UITHEME_H

#include "UiTokens.h"

#include <base/color.h>
#include <base/system.h>

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
	return Luminance > 0.179f ? ui_token::color::NEUTRAL_BLACK : ui_token::color::NEUTRAL_WHITE;
}

// 外溢光（搜索框聚焦光晕这类"从控件边缘散出去的光"）在浅色表面上会明显更亮：
// 同一份 alpha 压在白卡上，比压在深色卡上扎眼得多。按表面亮度线性压一档——
// 最亮表面只留 55% 强度，深色主题几乎不变。这样同一个令牌在浅/深两套主题里
// 都落在"看得出、但不抢描边"的区间，而不是各自换一套数值。
inline float ResolveUiSurfaceGlowScale(ColorRGBA Surface)
{
	const float Luminance = std::clamp(UiSurfaceLuminance(CompositeUiSurface(Surface)), 0.0f, 1.0f);
	return 1.0f - Luminance * ui_token::glow::LIGHT_SURFACE_DIM;
}

// 文本颜色模式：0 白 / 1 黑 / 2 彩虹 / 3 自定义。越界值按白处理——配置被手改成
// 4 时不该让整屏文字变黑压在深色表面上。
// 「自动对比」档已删除：同一屏里浅色胶囊上翻黑、深色卡片上翻白，并排读起来像两套
// 主题，且用户无法从设置里判断某块文字到底是什么颜色。
// 彩虹只换绘制色（容器不烘色段），所以色相逐帧流动也不触发文本容器重建。
inline ColorRGBA ResolveUiRainbowTextColor(const float Phase, const float Alpha = 1.0f)
{
	return color_cast<ColorRGBA>(ColorHSLA(Phase - std::floor(Phase), 0.72f, 0.68f, 1.0f)).WithAlpha(Alpha);
}

inline ColorRGBA ResolveUiTextColor(const int Mode, const unsigned CustomColor, const float RainbowPhase = 0.0f)
{
	switch(Mode)
	{
	case 1: return ui_token::color::TEXT_FORCED_DARK;
	case 2: return ResolveUiRainbowTextColor(RainbowPhase);
	case 3: return color_cast<ColorRGBA>(ColorHSLA(CustomColor)).WithAlpha(1.0f);
	default: return ui_token::color::TEXT_FORCED_LIGHT;
	}
}

// 全界面共用一条彩虹相位（6 秒一圈）：文字面积比卡片标题大，慢到看不出在流动，
// 但比标题快一档，免得整屏文字长时间停在同一个颜色上。
inline float UiRainbowTextPhase()
{
	const double Seconds = (double)time_get() / (double)time_freq();
	return (float)std::fmod(Seconds / 6.0, 1.0);
}

// 强调色的 RGB（满透明）。选中项、滑块、行反馈都从这里取，避免每处各自
// 把 qm_ui_accent_color 与 qm_ui_accent_opacity 各拼一次。
inline ColorRGBA ResolveConfiguredAccentColor()
{
	return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiAccentColor)).WithAlpha(1.0f);
}

inline ColorRGBA ResolveConfiguredTextColor()
{
	return ResolveUiTextColor(g_Config.m_QmUiTextColorMode, g_Config.m_QmUiTextCustomColor, UiRainbowTextPhase());
}

// 反馈独立叠加，不修改基础表面的用户颜色或按钮几何。
inline ColorRGBA ResolveUiIconButtonFeedback(ColorRGBA Surface, bool Enabled, bool Hovered, bool Pressed)
{
	if(!Enabled || (!Hovered && !Pressed))
		return ui_token::color::COLOR_TRANSPARENT;
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
	// 旋钮是独立的前景角色，默认保持白色，不借用自动文本对比色。
	ColorRGBA m_ToggleKnob = ui_token::color::NEUTRAL_WHITE;
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

inline SUiToggleStyle ResolveUiToggleStyle(const SUiTheme &Theme, ColorRGBA ControlSurface, ColorRGBA, bool Value, bool Enabled)
{
	SUiToggleStyle Style;
	Style.m_Track = Value ? Theme.m_Accent : ControlSurface;
	Style.m_Knob = Theme.m_ToggleKnob;
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
	// 轨道与边框保持可读的基础强度（0.25/0.30 在卡片表面几乎不可见，用户
	// 反馈「颜色带透明度、没反馈」）；hover/press 由边框与滑块几何反馈承担。
	Style.m_Track = Foreground.WithAlpha(0.32f);
	Style.m_Fill = Theme.m_Accent.WithMultipliedAlpha(0.85f);
	Style.m_Handle = ResolveUiSurfaceIconColor(Backdrop, Theme.m_Accent);
	Style.m_Border = Foreground.WithAlpha(Enabled && (Hovered || Pressed) ? 0.85f : 0.45f);
	if(!Enabled)
	{
		Style.m_Track.a *= 0.65f;
		Style.m_Fill.a *= 0.65f;
		Style.m_Handle.a *= 0.65f;
	}
	return Style;
}

// 焦点环不再单独配置：它就是强调色。同一套界面里出现第二个「强调色」时，用户调了
// 强调色却发现输入框焦点环还是另一种颜色，只会以为设置没生效。
inline SUiTheme ResolveUiTheme(const ColorHSLA BaseColor, float Opacity, const ColorHSLA AccentColor = ColorHSLA(0x8FDDAD))
{
	Opacity = std::clamp(Opacity, 0.0f, 1.0f);
	const ColorRGBA SurfaceBase = color_cast<ColorRGBA>(BaseColor);
	const ColorRGBA AccentBase = color_cast<ColorRGBA>(AccentColor);
	const float AccentAlpha = std::clamp(AccentBase.a, 0.0f, 1.0f);
	SUiTheme Theme;
	Theme.m_Surface = SurfaceBase.WithAlpha(std::clamp(std::max(SurfaceBase.a, 0.70f) * Opacity, 0.0f, 1.0f));
	Theme.m_SurfaceHovered = ColorRGBA(
		std::clamp(Theme.m_Surface.r * 1.06f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.g * 1.06f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.b * 1.06f, 0.0f, 1.0f), Theme.m_Surface.a);
	Theme.m_SurfaceFocused = Theme.m_SurfaceHovered;
	Theme.m_Border = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Opacity);
	// 悬停/聚焦的高亮边框固定白色：边框只表达「这里有反馈」，不参与强调色语义。
	// 跟随强调色时，一个偏红的强调色会把每个按钮的鼠标悬停都染红，看起来像错误态。
	Theme.m_BorderHovered = ui_token::color::NEUTRAL_WHITE.WithAlpha(0.45f * Opacity);
	Theme.m_BorderFocused = ui_token::color::NEUTRAL_WHITE.WithAlpha(0.75f * Opacity);
	Theme.m_InputSurface = ColorRGBA(
		std::clamp(Theme.m_Surface.r * 0.88f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.g * 0.88f, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.b * 0.88f, 0.0f, 1.0f), Theme.m_Surface.a);
	Theme.m_InputSurfaceFocused = Theme.m_InputSurface;
	// 焦点环是键盘与输入焦点的唯一稳定反馈，不随低表面透明度弱化到不可辨认；
	// 颜色跟随强调色，避免同一界面出现两个强调色。
	Theme.m_FocusRing = AccentBase.WithAlpha(std::clamp(std::max(0.60f, 0.90f * Opacity), 0.0f, 1.0f));
	Theme.m_Accent = AccentBase.WithAlpha(AccentAlpha);
	// 选中项不再单独配置：选中就是强调色，只是压到行反馈用的 22% 透明度。
	Theme.m_Selected = AccentBase.WithAlpha(0.22f);
	Theme.m_TextTitle = ResolveConfiguredTextColor();
	Theme.m_TextBody = Theme.m_TextTitle;
	Theme.m_TextSmall = Theme.m_TextTitle;
	return Theme;
}

// 二级面板统一读取独立背景色与透明度，边框沿用全局卡片配置。
// 强调色/焦点/选中跟随用户配置：弹窗内的主按钮、滚动条与设置页共享同一套强调色，不回落到硬编码默认值。
inline SUiTheme ResolveSecondaryPanelTheme(unsigned BackgroundColor, int Opacity, unsigned BorderColor)
{
	SUiTheme Theme = ResolveUiTheme(ColorHSLA(BackgroundColor), 1.0f,
		ColorHSLA(g_Config.m_QmUiAccentColor).WithAlpha(g_Config.m_QmUiAccentOpacity / 100.0f));
	Theme.m_Surface = Theme.m_Surface.WithAlpha(std::clamp(Opacity / 100.0f, 0.0f, 1.0f));
	Theme.m_Border = color_cast<ColorRGBA>(ColorHSLA(BorderColor, true));
	Theme.m_TextTitle = ResolveConfiguredTextColor();
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
// 表面只使用自身透明度与禁用衰减；父层入场透明度由绘制层应用，界面背景透明度不属于按钮角色。
inline ColorRGBA ResolveUiControlSurface(unsigned BackgroundColor, int Opacity, bool Enabled = true)
{
	return ResolveDropdownSurface(BackgroundColor, Opacity).WithMultipliedAlpha(Enabled ? 1.0f : 0.65f);
}

// 控件底（按钮、下拉触发器、输入框）共用一份配置：三者外观本就同档，
// 拆成多份只会让同一页里出现两种「按钮色」。
inline ColorRGBA ResolveConfiguredControlSurface(bool Enabled = true)
{
	return ResolveUiControlSurface(g_Config.m_QmUiControlColor, g_Config.m_QmUiControlOpacity, Enabled);
}

// 显式状态色保留原始透明度，普通图标按钮才回退到配置表面。
inline ColorRGBA ResolveConfiguredIconButtonSurface(const std::optional<ColorRGBA> &ButtonColor, bool Enabled = true)
{
	return ButtonColor.value_or(ResolveConfiguredControlSurface(Enabled));
}

inline ColorRGBA ResolveConfiguredDropdownSurface()
{
	return ResolveDropdownSurface(g_Config.m_QmUiControlColor, g_Config.m_QmUiControlOpacity);
}

// 输入框与按钮同底：输入框不再单独配色，避免「按钮是深灰、输入框是蓝灰」。
inline ColorRGBA ResolveConfiguredInputSurface(bool Enabled = true)
{
	return ResolveUiControlSurface(g_Config.m_QmUiControlColor, g_Config.m_QmUiControlOpacity, Enabled);
}

// 展开列表（下拉浮层）与二级菜单共用一份「弹层背景」：两者都浮在内容之上，
// 分成两份配置后同一层里的两个浮层会出现两种底色。
inline SUiTheme ResolveConfiguredDropdownListTheme()
{
	return ResolveSecondaryPanelTheme(g_Config.m_QmUiLayerColor, g_Config.m_QmUiLayerOpacity, g_Config.m_QmUiCardBorderColor);
}

inline SUiTheme ResolveConfiguredSecondaryPanelTheme()
{
	return ResolveSecondaryPanelTheme(g_Config.m_QmUiLayerColor, g_Config.m_QmUiLayerOpacity, g_Config.m_QmUiCardBorderColor);
}

inline SUiTheme ResolveInputFallbackTheme()
{
	// 某些设置页的旧调用点只提供最小 IUiContext。回退主题仍必须与
	// SettingsUiContext 使用相同的用户颜色和透明度，避免同一输入组件出现两套外壳。
	return ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiAccentColor).WithAlpha(g_Config.m_QmUiAccentOpacity / 100.0f));
}

#endif
