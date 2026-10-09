#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_STATUS_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_STATUS_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>

struct SQmBrowserStatusLayout
{
	CUIRect m_ServerList;
	CUIRect m_Panel;
	CUIRect m_Controls;
	CUIRect m_RefreshBar;
	CUIRect m_Notice;
};

// 过期/失败提示在状态面板内独占一行，额外高度由列表让出，不压缩原有控件。
inline SQmBrowserStatusLayout QmBrowserStatusLayout(const CUIRect &Stack, bool ShowNotice)
{
	constexpr float PanelHeight = 84.0f;
	constexpr float NoticeHeight = 12.0f;
	constexpr float PanelMargin = 10.0f;
	constexpr float ListGap = 8.0f;
	SQmBrowserStatusLayout Layout{};
	const float Height = std::min(std::max(0.0f, Stack.h), PanelHeight + (ShowNotice ? NoticeHeight : 0.0f));
	Layout.m_Panel = {Stack.x, Stack.y + Stack.h - Height, std::max(0.0f, Stack.w), Height};
	Layout.m_ServerList = {Stack.x, Stack.y, std::max(0.0f, Stack.w), std::max(0.0f, Stack.h - Height - ListGap)};
	const float Margin = std::min(PanelMargin, std::min(Layout.m_Panel.w, Layout.m_Panel.h) * 0.5f);
	Layout.m_Panel.Margin(Margin, &Layout.m_Controls);
	Layout.m_Controls.HSplitTop(std::min(5.0f, Layout.m_Controls.h), &Layout.m_RefreshBar, &Layout.m_Controls);
	if(ShowNotice)
		Layout.m_Controls.HSplitBottom(std::min(NoticeHeight, Layout.m_Controls.h), &Layout.m_Controls, &Layout.m_Notice);
	return Layout;
}

#endif
