#ifndef GAME_CLIENT_QMUI_QMCONSOLESCROLL_H
#define GAME_CLIENT_QMUI_QMCONSOLESCROLL_H

#include <game/client/QmUi/QmScroll.h>

#include <algorithm>
#include <cmath>

// 一个滚轮档的标准增量（与 CUi 滚轮归属帧、聊天历史同口径：上滚 +120、下滚 -120）。
// 滚轮是离散输入，控制台只在输入事件里报方向，偏移推进统一交给 QmScroll 的
// AddWheelImpulse，所以这里声明的是"一档"的语义常量，不让调用点自己写 120。
inline constexpr float QM_CONSOLE_SCROLL_NOTCH = 120.0f;

// 控制台日志滚动推进：物理本体是 CQmScrollState（列表滚动区、聊天历史、设置页共用同一套
// 滚轮档位、弹簧、摩擦与橡皮筋），这里只补控制台的两条特化：
//
// 1) 行锚语义。日志按行组织：选区命中、状态栏 "Lines x - y"、Ctrl+F 搜索跳转都以行为单位，
//    绘制也是按整行向上累积。所以像素偏移在读取时折回「整行 + 亚行余量」：行号给既有逻辑用，
//    余量给绘制与命中用，两边读同一份结果，不会各算一次，也不会各留一套滚动值。
// 2) 贴底跟随。偏移停在最新一行时新日志自动跟随（等价于旧的 m_BacklogCurLine == 0）；
//    离开底部后新日志只把内容整体推开，视口停在原来那几行。
//
// 输入趟（滚轮事件、翻页命令）发生在绘制之前，拿不到本帧几何，因此这些操作作用在
// 上一次 Advance 的几何快照上，下一次 Advance 推进时一起结算。
class CQmConsoleScrollRuntime
{
public:
	struct SFrame
	{
		// 距最新一行隐藏了多少整行，以及不足一行的像素余量；
		// 内容整体下移 (m_Line * 行高 + m_SubLine) 像素，负余量表示越界把内容往上拉。
		int m_Line = 0;
		float m_SubLine = 0.0f;
		float m_Offset = 0.0f;
		float m_MaxOffset = 0.0f;
		// 视口贴住最新一行：状态栏显示 Following，新日志自动跟随。
		bool m_Following = true;
	};

	void Reset()
	{
		m_State.Reset();
		m_Metrics = {};
		m_Config = {};
		m_LineHeight = 1.0f;
		m_TotalLines = 0;
		m_HasGeometry = false;
		m_Following = true;
	}

	// 每帧推进：ViewportSize 是日志区高度，TotalLines 是当前筛选下的全部行数。
	SFrame Advance(float Dt, float ViewportSize, int TotalLines, float LineHeight, const SQmScrollConfig &Config)
	{
		const float SafeLine = std::max(1.0f, LineHeight);
		const int Total = std::max(0, TotalLines);
		SQmScrollMetrics Metrics;
		Metrics.m_ViewportSize = std::max(0.0f, ViewportSize);
		Metrics.m_ContentSize = (float)Total * SafeLine;

		// 新日志到达：锁定态按增长量把偏移抬高，视口仍停原来那几行；跟随态不用动——
		// 偏移本身就是"距最新一行多少像素"，内容在最新端增长时贴底位置不变。
		if(m_HasGeometry && Total != m_TotalLines && !m_Following && Metrics.MaxOffset() > 0.0f)
			m_State.SetOffset(m_State.Offset() + (float)(Total - m_TotalLines) * SafeLine, Metrics, Config);

		m_LineHeight = SafeLine;
		m_Metrics = Metrics;
		m_Config = Config;
		m_TotalLines = Total;
		m_HasGeometry = true;

		m_State.Advance(Dt, Metrics, Config);
		const SFrame Frame = FrameFor(m_State.Offset());
		m_Following = Frame.m_Following;
		return Frame;
	}

	// 输入趟与命令回调：都用上一次 Advance 的几何快照，先落状态、下一帧统一推进。
	void Wheel(int Steps)
	{
		if(Steps == 0 || !m_HasGeometry)
			return;
		// 公共实现里 WheelDelta 的正负与偏移方向相反，符号换算只留在这一处，调用点只报方向。
		m_State.AddWheelImpulse(-(float)Steps * QM_CONSOLE_SCROLL_NOTCH, m_Metrics, m_Config);
	}

	void Page(int Direction)
	{
		if(Direction == 0 || !m_HasGeometry)
			return;
		ScrollToOffset(m_State.Offset() + (float)Direction * std::max(0.0f, m_Metrics.m_ViewportSize));
	}

	void ScrollToLine(int Line)
	{
		ScrollToOffset((float)std::max(0, Line) * m_LineHeight);
	}

	void ScrollToNewest() { ScrollToOffset(0.0f); }

	void ScrollToOldest() { ScrollToOffset(m_Metrics.MaxOffset()); }

	// 滑块与触控拖动：直接把位移落到手指位置，不经弹簧（跟手）。
	void DragToOffset(float Offset) { m_State.SetOffset(Offset, m_Metrics, m_Config, true); }

	// 静止态吸附到整行：不吸附时每帧的亚行余量会让正文在整数像素边界上抖动，
	// 视觉上就是「滚动后内容区在闪」。动画与拖动期间保持连续偏移。
	void SnapToNearestLine()
	{
		if(!m_HasGeometry || m_LineHeight <= 0.0f)
			return;
		const float Snapped = std::round(m_State.Offset() / m_LineHeight) * m_LineHeight;
		if(std::abs(Snapped - m_State.Offset()) > 0.01f)
			m_State.SetOffset(Snapped, m_Metrics, m_Config);
	}

	bool Animating() const { return m_State.Animating(); }

	void DragBy(float Pixels) { DragToOffset(m_State.Offset() + Pixels); }

	float Offset() const { return m_State.Offset(); }
	float MaxOffset() const { return m_Metrics.MaxOffset(); }
	bool Following() const { return m_Following; }

private:
	void ScrollToOffset(float Offset)
	{
		if(!m_HasGeometry || m_Metrics.MaxOffset() <= 0.0f)
			return;
		m_State.ScrollTo(std::clamp(Offset, 0.0f, m_Metrics.MaxOffset()), m_Metrics, m_Config);
	}

	SFrame FrameFor(float Offset) const
	{
		SFrame Frame;
		Frame.m_MaxOffset = m_Metrics.MaxOffset();
		// 行锚上限与旧的 MaxScroll 同义：总行数减可见行数，选区与状态栏不必再改口径。
		const int VisibleLines = std::max(1, (int)std::floor(m_Metrics.m_ViewportSize / m_LineHeight));
		const int MaxLine = std::max(0, m_TotalLines - VisibleLines);
		// 越界时行号夹在合法区间内，越界量全部落到亚行余量上，所以橡皮筋那一段
		// 读出来的位移就是越界距离，回弹目标也就是合法边界本身。
		Frame.m_Line = std::clamp((int)std::floor(Offset / m_LineHeight), 0, MaxLine);
		Frame.m_SubLine = Offset - (float)Frame.m_Line * m_LineHeight;
		Frame.m_Offset = Offset;
		Frame.m_Following = Offset <= m_Config.m_RestDistance;
		return Frame;
	}

	CQmScrollState m_State;
	SQmScrollMetrics m_Metrics;
	SQmScrollConfig m_Config;
	float m_LineHeight = 1.0f;
	int m_TotalLines = 0;
	bool m_HasGeometry = false;
	bool m_Following = true;
};

#endif
