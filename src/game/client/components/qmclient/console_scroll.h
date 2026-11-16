#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H

#include <algorithm>
#include <cmath>

// 控制台滚轮每档滚动的行数。
//
// 滚轮步长是像素口径（qm_ui_scroll_step，默认 10），而控制台行高随字号变化：
// 直接按像素滚，小字号一次翻太多、大字号又几乎不动。这里先按当前行高把像素
// 折算成行数，再夹在 [1, 3]——默认 10px 步长只滚一行，与已发布版本一致；
// 调大步长最多三行，避免一档翻掉半个屏幕。
inline int QmConsoleWheelLines(float StepPixels, float LineHeight)
{
	const float SafeStep = StepPixels > 0.0f ? StepPixels : 10.0f;
	const float SafeLine = LineHeight > 0.0f ? LineHeight : 1.0f;
	return std::clamp(static_cast<int>(std::lround(SafeStep / SafeLine)), 1, 3);
}

struct SQmConsoleLogClipBounds
{
	float m_Top = 0.0f;
	float m_Bottom = 0.0f;
	float Height() const { return std::max(0.0f, m_Bottom - m_Top); }
};

// 日志区域裁剪：从工具栏下沿开始，不能按整行向下取整。
// 视口高度通常不是行高整数倍，取整会裁掉顶部不足一行的部分，
// 滚到最旧一行时就表现为“首行看不到，再继续滚才出现”。
inline SQmConsoleLogClipBounds QmConsoleResolveLogClipBounds(float LogTop, float LogBottom, float PixelScale)
{
	const float Scale = std::max(0.0f, PixelScale);
	SQmConsoleLogClipBounds Bounds;
	Bounds.m_Top = std::max(0.0f, LogTop) * Scale;
	Bounds.m_Bottom = std::max(Bounds.m_Top, (LogBottom + 2.0f) * Scale);
	return Bounds;
}

// 首条可见日志之前的内容位移。SubLine 是“越过整行后还差多少像素”，
// 内容必须继续向下补这段距离；符号反了会让弹簧/滚轮的半行阶段往回走，
// 到最旧端时也会把顶行额外推出裁剪区。
inline float QmConsoleLineStackOffset(int ActiveLine, int SkippedLines, float LineHeight, float SubLine)
{
	return (float)(ActiveLine - SkippedLines) * std::max(0.0f, LineHeight) + SubLine;
}

#endif
