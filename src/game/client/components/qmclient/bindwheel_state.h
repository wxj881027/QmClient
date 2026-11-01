#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BINDWHEEL_STATE_H

#include <base/vmath.h>

#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

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

	static int SelectedBind(vec2 Mouse, int Count)
	{
		if(Count <= 0 || !std::isfinite(Mouse.x) || !std::isfinite(Mouse.y) || length(Mouse) <= 110.0f)
			return -1;
		const int Segment = static_cast<int>(std::round(angle(Mouse) / (2.0f * pi) * Count));
		const int Index = Segment % Count;
		return Index < 0 ? Index + Count : Index;
	}
};

#endif
