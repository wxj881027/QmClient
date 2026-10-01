#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_spectator_layout
{
	struct SSelectorLayout
	{
		CUIRect m_Panel;
		CUIRect m_Mouse;
		CUIRect m_SearchRow;
		CUIRect m_Status;
	};

	inline SSelectorLayout Build(vec2 Center, float HalfWidth, bool HasTeleSearch)
	{
		constexpr float Padding = 20.0f;
		SSelectorLayout Layout{};
		Layout.m_Panel = {Center.x - HalfWidth, Center.y - 300.0f, HalfWidth * 2.0f, 600.0f};
		if(HasTeleSearch)
		{
			const float SearchWidth = std::max(0.0f, std::min(560.0f, Layout.m_Panel.w - Padding * 2.0f));
			Layout.m_SearchRow = {Center.x - SearchWidth * 0.5f, Layout.m_Panel.y + Layout.m_Panel.h + 10.0f, SearchWidth, 40.0f};
			Layout.m_Status = {Layout.m_SearchRow.x, Layout.m_SearchRow.y + Layout.m_SearchRow.h + 10.0f, SearchWidth, 20.0f};
			// 背景、鼠标和触摸统一包含 CP 行与提示，不各自维护独立的底边。
			Layout.m_Panel.h = Layout.m_Status.y + Layout.m_Status.h + Padding - Layout.m_Panel.y;
		}
		Layout.m_Panel.Margin(Padding, &Layout.m_Mouse);
		return Layout;
	}

	inline vec2 ClampMouse(const SSelectorLayout &Layout, vec2 Center, vec2 Mouse)
	{
		return vec2(std::clamp(Mouse.x, Layout.m_Mouse.x - Center.x, Layout.m_Mouse.x + Layout.m_Mouse.w - Center.x),
			std::clamp(Mouse.y, Layout.m_Mouse.y - Center.y, Layout.m_Mouse.y + Layout.m_Mouse.h - Center.y));
	}
}

#endif
