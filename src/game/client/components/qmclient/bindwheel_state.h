#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H

#include "bind_menu.h"

#include <base/vmath.h>

#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

// 菜单呈现方式：圆形轮盘，或圆角矩形面板 HUD。
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

	// 操作须在资源失败时保留原条目；实际vector写入使用无异常复制的固定数组条目。
	template<typename TChange>
	bool TryChangeBinds(TChange &&Change)
	{
		try
		{
			std::forward<TChange>(Change)();
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		InvalidateSelection();
		return true;
	}

	static vec2 AdvanceCursor(vec2 Cursor, vec2 Offset)
	{
		if(!std::isfinite(Cursor.x) || !std::isfinite(Cursor.y))
			return vec2(0, 0);
		if(!std::isfinite(Offset.x) || !std::isfinite(Offset.y))
			return Cursor;
		const double X = double(Cursor.x) + Offset.x, Y = double(Cursor.y) + Offset.y;
		constexpr double Limit = std::numeric_limits<float>::max();
		if(std::abs(X) > Limit || std::abs(Y) > Limit)
			return Cursor;
		return Cursor + Offset;
	}

	static int SelectedWheel(vec2 Mouse, int Count, float InnerRadius = 110.0f)
	{
		if(Count <= 0 || !std::isfinite(Mouse.x) || !std::isfinite(Mouse.y) || length(Mouse) <= InnerRadius)
			return -1;
		const int Segment = static_cast<int>(std::round(angle(Mouse) / (2.0f * pi) * Count));
		const int Index = Segment % Count;
		return Index < 0 ? Index + Count : Index;
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
