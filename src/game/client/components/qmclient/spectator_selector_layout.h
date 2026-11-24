#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_SELECTOR_LAYOUT_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
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

	struct SGroupTitleLayout
	{
		CUIRect m_Label;
		CUIRect m_Divider;
		float m_Height;
	};

	inline SGroupTitleLayout GroupTitle(vec2 Position, float Width, float TitleHeight, float Scale)
	{
		const float Padding = ui_token::spacing::SM * Scale;
		const float DividerGap = ui_token::spacing::XS * Scale;
		SGroupTitleLayout Layout{};
		Layout.m_Label = {Position.x, Position.y + Padding, Width, TitleHeight};
		Layout.m_Divider = {Position.x, Layout.m_Label.y + TitleHeight + DividerGap, Width, Scale};
		// 测量与绘制共用包含上下留白的高度，底栏跟随最后一行下移。
		Layout.m_Height = Padding + TitleHeight + DividerGap + Layout.m_Divider.h + Padding;
		return Layout;
	}

	inline SRoundedSurfaceParams TeamRowSurface(float Radius, bool StartsTeam, bool EndsTeam)
	{
		SRoundedSurfaceParams Surface;
		Surface.m_Corners = IGraphics::CORNER_NONE;
		if(StartsTeam)
			Surface.m_Corners |= IGraphics::CORNER_T;
		if(EndsTeam)
			Surface.m_Corners |= IGraphics::CORNER_B;
		// 中间行必须给零半径，否则公共表面会把 CORNER_NONE 解析为四角圆角。
		Surface.m_Radius = Surface.m_Corners == IGraphics::CORNER_NONE ? 0.0f : Radius;
		return Surface;
	}

	// HalfHeight 默认 300（桌面 600 高的面板）；手机放大整块时传入按比例加高的半高。
	inline SSelectorLayout Build(vec2 Center, float HalfWidth, bool HasTeleSearch, float PlayersBottom = 0.0f, float HalfHeight = 300.0f)
	{
		constexpr float Padding = 20.0f;
		SSelectorLayout Layout{};
		Layout.m_Panel = {Center.x - HalfWidth, Center.y - HalfHeight, HalfWidth * 2.0f, HalfHeight * 2.0f};
		if(HasTeleSearch)
		{
			const float SearchWidth = std::max(0.0f, std::min(560.0f, Layout.m_Panel.w - Padding * 2.0f));
			const float SearchTop = std::max(310.0f, PlayersBottom + Padding);
			Layout.m_SearchRow = {Center.x - SearchWidth * 0.5f, Center.y + SearchTop, SearchWidth, 40.0f};
			Layout.m_Status = {Layout.m_SearchRow.x, Layout.m_SearchRow.y + Layout.m_SearchRow.h + 10.0f, SearchWidth, 20.0f};
			// 背景、鼠标和触摸统一包含 CP 行与提示，不各自维护独立的底边。
			Layout.m_Panel.h = Layout.m_Status.y + Layout.m_Status.h + Padding - Layout.m_Panel.y;
		}
		else
		{
			Layout.m_Panel.h = std::max(Layout.m_Panel.h, Center.y + PlayersBottom + Padding - Layout.m_Panel.y);
		}
		Layout.m_Panel.Margin(Padding, &Layout.m_Mouse);
		return Layout;
	}

	inline vec2 ClampMouse(const SSelectorLayout &Layout, vec2 Center, vec2 Mouse)
	{
		return vec2(std::clamp(Mouse.x, Layout.m_Mouse.x - Center.x, Layout.m_Mouse.x + Layout.m_Mouse.w - Center.x),
			std::clamp(Mouse.y, Layout.m_Mouse.y - Center.y, Layout.m_Mouse.y + Layout.m_Mouse.h - Center.y));
	}

	inline vec2 TouchPosition(vec2 Position, vec2 ScreenSize, vec2 Center)
	{
		// 面板动画会改变中心；触点仍对应原始屏幕位置。
		return Position * ScreenSize - Center;
	}
}

#endif
