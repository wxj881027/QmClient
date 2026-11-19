#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H

#include "bind_menu.h"

#include <base/vmath.h>

#include <cmath>
#include <optional>
#include <utility>

enum class EQmBindWheelLayout
{
	WHEEL,
	LIST,
};

class CQmBindWheelState
{
	bool m_Active = false;
	std::optional<int> m_PendingBind;
	qm_bind_menu::EMenu m_Menu = qm_bind_menu::EMenu::CUSTOM;

public:
	bool IsActive() const { return m_Active; }
	qm_bind_menu::EMenu Menu() const { return m_Menu; }
	void Open(qm_bind_menu::EMenu Menu)
	{
		// 按住触发键会重复下发“按下”；同一菜单重复打开不能清空当前选择。
		if(m_Active && m_Menu == Menu)
			return;
		m_Active = true;
		m_Menu = Menu;
		m_PendingBind.reset();
	}
	void TriggerRelease(qm_bind_menu::EMenu Menu, int SelectedBind)
	{
		if(!m_Active || m_Menu != Menu)
			return;
		ReleaseActive(SelectedBind);
	}
	void ReleaseActive(int SelectedBind)
	{
		if(!m_Active)
			return;
		// 触发键释放是两种布局共同的结束条件：提交当前悬停项并关闭。
		m_PendingBind = SelectedBind;
		m_Active = false;
	}
	void ApplyClick(int SelectedBind)
	{
		if(!m_Active)
			return;
		m_PendingBind = SelectedBind;
		m_Active = false;
	}
	void Cancel()
	{
		m_Active = false;
		InvalidateSelection();
	}
	void InvalidateSelection() { m_PendingBind.reset(); }
	std::optional<int> TakePendingBind() { return std::exchange(m_PendingBind, std::nullopt); }

	static int SelectedWheel(vec2 Mouse, int Count, float InnerRadius = 110.0f)
	{
		if(Count <= 0 || length(Mouse) <= InnerRadius)
			return -1;
		const int Segment = static_cast<int>(std::round(angle(Mouse) / (2.0f * pi) * Count));
		return (Segment % Count + Count) % Count;
	}

	static int SelectedPanel(vec2 Mouse, float Left, float Top, float Width, float RowHeight, int Count, int ScrollOffset, float RowSpacing = 0.0f)
	{
		return SelectedListBind(Mouse, Left, Top, Width, RowHeight, Count, ScrollOffset, RowSpacing);
	}

	// 面板布局：鼠标落在第几行。Top 是可见行区域的顶边，ScrollOffset 是已滚过的行数。
	static int SelectedListBind(vec2 Mouse, float Left, float Top, float Width, float RowHeight, int Count, int ScrollOffset, float RowSpacing = 0.0f)
	{
		if(Count <= 0 || RowHeight <= 0.0f || RowSpacing < 0.0f)
			return -1;
		if(Mouse.x < Left || Mouse.x > Left + Width)
			return -1;
		const float Pitch = RowHeight + RowSpacing;
		const float RelativeY = Mouse.y - Top;
		if(RelativeY < 0.0f)
			return -1;
		const int VisibleRow = static_cast<int>(std::floor(RelativeY / Pitch));
		if(RelativeY - VisibleRow * Pitch > RowHeight)
			return -1;
		const int Row = VisibleRow + ScrollOffset;
		if(Row < 0 || Row >= Count)
			return -1;
		return Row;
	}

	static int SelectedWheelByAngle(vec2 Direction, int Count)
	{
		return SelectedWheel(Direction, Count);
	}
};

#endif
