#ifndef GAME_CLIENT_QMUI_QMNEONRAINBOW_H
#define GAME_CLIENT_QMUI_QMNEONRAINBOW_H

#include <base/color.h>

#include <cmath>

// 好友跟随光环与无限钩标记共用的霓虹彩虹。
// 两个指示器只有取同一套公式，界面上才像"同一种彩虹"：色相绕边界流动，
// 亮度随高光波起伏（Wave），并混入少量白色提高亮度，细描边才像霓虹灯而不是暗色轮廓。
// Hue 为任意实数，按 1.0 循环；Wave 为 0..1 的沿路径位置决定的高光强度；Alpha 为整体透明度。
inline ColorRGBA QmNeonRainbowColor(float Hue, float Wave, float Alpha)
{
	// ColorHSLA 的色相不自动循环，先归一化到 [0,1)，否则超过一圈会落到黑色。
	Hue -= std::floor(Hue);
	const ColorRGBA Rainbow = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.86f, 0.60f + Wave * 0.18f, 1.0f));
	constexpr float Lift = 0.16f;
	return ColorRGBA(
		Lift + (1.0f - Lift) * Rainbow.r,
		Lift + (1.0f - Lift) * Rainbow.g,
		Lift + (1.0f - Lift) * Rainbow.b,
		Alpha);
}

#endif
