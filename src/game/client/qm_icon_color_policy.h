// 设置与渲染共享的轻量图标颜色兼容策略。
#ifndef GAME_CLIENT_QM_ICON_COLOR_POLICY_H
#define GAME_CLIENT_QM_ICON_COLOR_POLICY_H

#include <base/color.h>

#include <algorithm>
#include <cmath>

// 只选择描边色，不改变图标本体；黑白分界取两者 WCAG 对比度的交点。
inline ColorRGBA QmUiIconContrastColor(const ColorRGBA &Primary)
{
	const auto Linear = [](float Value) {
		Value = std::clamp(Value, 0.0f, 1.0f);
		return Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f);
	};
	const float Luminance = 0.2126f * Linear(Primary.r) + 0.7152f * Linear(Primary.g) + 0.0722f * Linear(Primary.b);
	const float Channel = Luminance >= 0.17912878f ? 0.0f : 1.0f;
	return ColorRGBA(Channel, Channel, Channel, Primary.a);
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
