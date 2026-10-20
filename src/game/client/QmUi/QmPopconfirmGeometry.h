#ifndef GAME_CLIENT_QMUI_QMPOPCONFIRMGEOMETRY_H
#define GAME_CLIENT_QMUI_QMPOPCONFIRMGEOMETRY_H

#include <base/math.h>

#include <game/client/ui_rect.h>

#include <algorithm>

// 气泡确认的定位规则：优先挂在触发控件下方（右缘对齐），下方放不下翻到上方，
// 最后整体夹紧到视口内。纯几何、不依赖渲染上下文，便于行为测试。
namespace qm_popconfirm_layout
{
	inline constexpr float OFFSET = 6.0f;
	inline constexpr float SCREEN_MARGIN = 4.0f;

	inline CUIRect ResolveRect(const CUIRect &Anchor, float Width, float Height, const CUIRect &Screen)
	{
		CUIRect Rect;
		Rect.w = Width;
		Rect.h = Height;
		Rect.x = Anchor.x + Anchor.w - Width;
		Rect.y = Anchor.y + Anchor.h + OFFSET;
		if(Rect.y + Rect.h > Screen.y + Screen.h - SCREEN_MARGIN)
			Rect.y = Anchor.y - Height - OFFSET;
		const float MinX = Screen.x + SCREEN_MARGIN;
		const float MaxX = std::max(MinX, Screen.x + Screen.w - Width - SCREEN_MARGIN);
		const float MinY = Screen.y + SCREEN_MARGIN;
		const float MaxY = std::max(MinY, Screen.y + Screen.h - Height - SCREEN_MARGIN);
		Rect.x = std::clamp(Rect.x, MinX, MaxX);
		Rect.y = std::clamp(Rect.y, MinY, MaxY);
		return Rect;
	}
} // namespace qm_popconfirm_layout

#endif // GAME_CLIENT_QMUI_QMPOPCONFIRMGEOMETRY_H
