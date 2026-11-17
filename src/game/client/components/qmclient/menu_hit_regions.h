#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_HIT_REGIONS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MENU_HIT_REGIONS_H

#include <game/client/ui_rect.h>

#include <array>

// ESC 菜单当前帧实际绘制出的面板命中区域。页面在拿到最终布局矩形后登记，
// 点击判定只认这些矩形，既不会把大页面里的空白误判成“菜单外”，也不会为了
// 容纳某个页面写死一块放大区域。
struct SQmMenuHitRegions
{
	static constexpr int MAX_REGIONS = 12;

	std::array<CUIRect, MAX_REGIONS> m_aRects{};
	int m_Count = 0;

	void Reset()
	{
		m_Count = 0;
	}

	void Add(const CUIRect &Rect)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f || m_Count >= MAX_REGIONS)
			return;
		m_aRects[m_Count++] = Rect;
	}

	bool Contains(vec2 Point) const
	{
		for(int Index = 0; Index < m_Count; ++Index)
		{
			if(m_aRects[Index].Inside(Point))
				return true;
		}
		return false;
	}
};

// 外部点击只有在既没有命中交互控件、也没落在任一可见菜单面板上时才关闭菜单。
inline bool QmMenuOutsideClickShouldClose(vec2 ClickPos, bool HasHotItem, const SQmMenuHitRegions &Regions)
{
	return !HasHotItem && !Regions.Contains(ClickPos);
}

#endif
