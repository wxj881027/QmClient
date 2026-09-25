// 屏幕外实体裁剪的边距与判定（对齐上游 14fc1e9d1e 与 ada53c8cb3）。
// 抽成不依赖引擎的纯函数，便于单测；items.cpp / ghost.cpp 共用同一份语义。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_ITEM_CULLING_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_ITEM_CULLING_LOGIC_H

#include <base/vmath.h>

namespace qm_item_culling
{

	// 边距与上游一致：投射物 ±1 tile、激光 ±0.5 tile、拾取物 x ±1.75 tile / y ±0.75 tile。
	inline constexpr float TILE_SIZE = 64.0f;
	inline constexpr float PROJECTILE_MARGIN = TILE_SIZE;
	inline constexpr float LASER_MARGIN = TILE_SIZE / 2.0f;
	inline constexpr float PICKUP_MARGIN_X = 1.75f * TILE_SIZE;
	inline constexpr float PICKUP_MARGIN_Y = 0.75f * TILE_SIZE;
	// ghost 使用 200x200 盒（上游 ada53c8cb3 在 ghost.cpp 里对屏幕矩形 Expand(100)）。
	inline constexpr float GHOST_MARGIN = 100.0f;

	// 屏幕世界坐标矩形（与 CScreenRect 的 m_TopLeft / m_BottomRight 同义）。
	struct SRect
	{
		float m_X0;
		float m_Y0;
		float m_X1;
		float m_Y1;
	};

	// 闭区间点判定：与 CScreenRect::Inside（内部使用 in_range）一致。
	inline bool IsPointInside(const SRect &Rect, float X, float Y)
	{
		return X >= Rect.m_X0 && X <= Rect.m_X1 && Y >= Rect.m_Y0 && Y <= Rect.m_Y1;
	}

	// 线段与矩形「四向不相交」判定：任一侧两端点都在外才算完全不可见。
	inline bool IsSegmentInside(const SRect &Rect, const vec2 &From, const vec2 &To)
	{
		return !((From.x < Rect.m_X0 && To.x < Rect.m_X0) ||
			 (From.x > Rect.m_X1 && To.x > Rect.m_X1) ||
			 (From.y < Rect.m_Y0 && To.y < Rect.m_Y0) ||
			 (From.y > Rect.m_Y1 && To.y > Rect.m_Y1));
	}

	inline SRect Expand(float X0, float Y0, float X1, float Y1, float MarginX, float MarginY)
	{
		return SRect{X0 - MarginX, Y0 - MarginY, X1 + MarginX, Y1 + MarginY};
	}

	inline bool IsProjectileInside(const SRect &Screen, const vec2 &Pos)
	{
		const SRect Rect = Expand(Screen.m_X0, Screen.m_Y0, Screen.m_X1, Screen.m_Y1, PROJECTILE_MARGIN, PROJECTILE_MARGIN);
		return IsPointInside(Rect, Pos.x, Pos.y);
	}

	inline bool IsPickupInside(const SRect &Screen, const vec2 &Pos)
	{
		const SRect Rect = Expand(Screen.m_X0, Screen.m_Y0, Screen.m_X1, Screen.m_Y1, PICKUP_MARGIN_X, PICKUP_MARGIN_Y);
		return IsPointInside(Rect, Pos.x, Pos.y);
	}

	inline bool IsLaserInside(const SRect &Screen, const vec2 &From, const vec2 &To)
	{
		const SRect Rect = Expand(Screen.m_X0, Screen.m_Y0, Screen.m_X1, Screen.m_Y1, LASER_MARGIN, LASER_MARGIN);
		return IsSegmentInside(Rect, From, To);
	}

	inline bool IsGhostInside(const SRect &Screen, const vec2 &Pos)
	{
		const SRect Rect = Expand(Screen.m_X0, Screen.m_Y0, Screen.m_X1, Screen.m_Y1, GHOST_MARGIN, GHOST_MARGIN);
		return IsPointInside(Rect, Pos.x, Pos.y);
	}

} // namespace qm_item_culling

#endif
