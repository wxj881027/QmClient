#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_VOTE_BAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_VOTE_BAR_H

#include <game/client/ui_rect.h>

#include <algorithm>

// 手机端顶部投票条：一条横幅 + 两个大按钮。
// 观战触控层直接按手指命中（游戏内 CUi 不逐帧更新），所以布局必须能单独算出来，
// 绘制在 CVoting 里、命中在 CQmMobileSpectator 里，共用这一份几何。
struct SQmMobileVoteBarLayout
{
	CUIRect m_Panel;
	CUIRect m_Description;
	CUIRect m_Reason;
	CUIRect m_Bars;
	CUIRect m_Yes;
	CUIRect m_No;
	float m_FontSize = 9.0f;
	float m_SmallFontSize = 7.5f;
	float m_ButtonFontSize = 11.0f;
};

inline SQmMobileVoteBarLayout QmMobileVoteBarLayout(vec2 Screen)
{
	SQmMobileVoteBarLayout Layout{};
	const float Width = std::min(Screen.x * 0.72f, 520.0f);
	const float Height = 86.0f;
	Layout.m_Panel = {(Screen.x - Width) * 0.5f, 10.0f, Width, Height};

	CUIRect Body = Layout.m_Panel;
	Body.Margin(8.0f, &Body);
	Body.HSplitTop(11.0f, &Layout.m_Description, &Body);
	Body.HSplitTop(4.0f, nullptr, &Body);
	Body.HSplitTop(9.0f, &Layout.m_Reason, &Body);
	Body.HSplitTop(4.0f, nullptr, &Body);
	Body.HSplitTop(5.0f, &Layout.m_Bars, &Body);
	Body.HSplitTop(5.0f, nullptr, &Body);
	Body.VSplitMid(&Layout.m_Yes, &Layout.m_No, 8.0f);
	return Layout;
}

// 手机端只在这几个条件下接管投票 HUD：在线、正在投票、没有全屏菜单遮挡。
inline bool QmMobileVoteBarVisible(bool MobilePlatform, bool Online, bool Voting, bool MenuActive, bool ConsoleActive, bool HudEditorActive)
{
	return MobilePlatform && Online && Voting && !MenuActive && !ConsoleActive && !HudEditorActive;
}

#endif
