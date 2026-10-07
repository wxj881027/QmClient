// 设置与渲染共享的轻量图标颜色兼容策略。
#ifndef GAME_CLIENT_QM_ICON_COLOR_POLICY_H
#define GAME_CLIENT_QM_ICON_COLOR_POLICY_H

#include <base/color.h>

#include <algorithm>
#include <cmath>

// 图标保护共用 sRGB 线性化与相对亮度计算，便于独立测量生产策略。
inline float QmUiIconRelativeLuminance(const ColorRGBA &Color)
{
	const auto Linear = [](float Value) {
		Value = std::clamp(Value, 0.0f, 1.0f);
		return Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f);
	};
	return 0.2126f * Linear(Color.r) + 0.7152f * Linear(Color.g) + 0.0722f * Linear(Color.b);
}

// 只选择描边色，不改变图标本体；黑白分界取两者 WCAG 对比度的交点。
inline ColorRGBA QmUiIconContrastColor(const ColorRGBA &Primary)
{
	const float Luminance = QmUiIconRelativeLuminance(Primary);
	const float Channel = Luminance >= 0.17912878f ? 0.0f : 1.0f;
	return ColorRGBA(Channel, Channel, Channel, Primary.a);
}

// 保护只根据已合成的背景表面开启，达到非文本图标 3:1 对比度时完全省略。
// 低对比时用连续的弱描边，不改本体 RGB；描边色只由背景决定，彩虹经过
// 亮度阈值时不会在黑白间反转，也不会出现开关式的闪烁。
inline ColorRGBA QmUiIconSurfaceProtection(const ColorRGBA &Primary, const ColorRGBA &Surface)
{
	const float Foreground = QmUiIconRelativeLuminance(Primary);
	const float Background = QmUiIconRelativeLuminance(Surface);
	const float Contrast = (std::max(Foreground, Background) + 0.05f) / (std::min(Foreground, Background) + 0.05f);
	const float Deficit = std::clamp((3.0f - Contrast) / 2.0f, 0.0f, 1.0f);
	const float Strength = Deficit * Deficit * (3.0f - 2.0f * Deficit);
	const float Channel = Background >= 0.17912878f ? 0.0f : 1.0f;
	return ColorRGBA(Channel, Channel, Channel, Primary.a * 0.35f * Strength);
}

namespace qm_icon_settings
{
	inline bool CustomColorEnabled(int Preset, int Enabled)
	{
		// 旧版把自定义颜色存为预设 3，读取时保留原有外观。
		return Enabled != 0 || Preset == 3;
	}
}

#endif
