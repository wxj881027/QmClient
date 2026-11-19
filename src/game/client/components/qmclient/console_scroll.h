#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SCROLL_H

#include <game/client/QmUi/QmScroll.h>

#include <algorithm>

// 控制台滚轮每档滚动的行数。
//
// 控制台是行型滚动面：用户只调全站倍率，这里用固定基准行数和视口上限兜底。
// 默认倍率（qm_ui_scroll_step = 10）每档 3 行；大倍率最多 8 行，窄日志区
// 再按可见行数压低，避免一档翻掉半个屏幕或滚出空白。
inline constexpr int QM_CONSOLE_WHEEL_BASE_LINES = 3;
inline constexpr int QM_CONSOLE_WHEEL_MAX_LINES = 8;

inline int QmConsoleWheelLines(float StepScale, int VisibleLines)
{
	return QmResolveWheelLineCount(QM_CONSOLE_WHEEL_BASE_LINES, StepScale, VisibleLines, QM_CONSOLE_WHEEL_MAX_LINES);
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
