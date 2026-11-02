#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H

#include <algorithm>
#include <cmath>

// 控制台滚轮每档滚动的行数。
//
// 滚轮步长是像素口径（qm_ui_scroll_step，默认 10），而控制台行高随字号变化：
// 直接按像素滚，小字号一次翻太多、大字号又几乎不动。这里先按当前行高把像素
// 折算成行数，再夹在 [3, 8]——一档至少能看出移动，又不至于把一屏文字翻过去。
inline int QmConsoleWheelLines(float StepPixels, float LineHeight)
{
	const float SafeStep = StepPixels > 0.0f ? StepPixels : 10.0f;
	const float SafeLine = LineHeight > 0.0f ? LineHeight : 1.0f;
	return std::clamp(static_cast<int>(std::lround(SafeStep / SafeLine)), 3, 8);
}

#endif
