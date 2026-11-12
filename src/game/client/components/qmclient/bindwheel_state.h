#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H

#include <base/vmath.h>

#include <cmath>
#include <optional>
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

public:
	bool IsActive() const { return m_Active; }
	void Open()
	{
		m_Active = true;
		m_PendingBind.reset();
	}
	void Release(int SelectedBind)
	{
		if(m_Active)
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

	static int SelectedBind(vec2 Mouse, int Count)
	{
		if(Count <= 0 || length(Mouse) <= 110.0f)
			return -1;
		const int Segment = static_cast<int>(std::round(angle(Mouse) / (2.0f * pi) * Count));
		return (Segment % Count + Count) % Count;
	}

	// 面板布局：鼠标落在第几行。Top 是可见行区域的顶边，ScrollOffset 是已滚过的行数。
	static int SelectedListBind(vec2 Mouse, float Left, float Top, float Width, float RowHeight, int Count, int ScrollOffset)
	{
		if(Count <= 0 || RowHeight <= 0.0f)
			return -1;
		if(Mouse.x < Left || Mouse.x > Left + Width)
			return -1;
		const int Row = static_cast<int>(std::floor((Mouse.y - Top) / RowHeight)) + ScrollOffset;
		if(Row < 0 || Row >= Count)
			return -1;
		return Row;
	}
};

#endif
