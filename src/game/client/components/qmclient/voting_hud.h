#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTING_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTING_HUD_H

#include <game/client/ui_rect.h>

#include <algorithm>

inline bool QmVoteHudVisible(bool IsVoting, int TakenChoice, bool ScoreboardActive, bool ShowAfterVoting, bool EditorPreview)
{
	if(EditorPreview)
		return true;
	return IsVoting && !ScoreboardActive && (TakenChoice == 0 || ShowAfterVoting);
}

inline bool QmScoreboardVoteCanSubmit(bool IsVoting, bool Interactive, int TakenChoice, int RequestedChoice)
{
	return IsVoting && Interactive && (RequestedChoice == -1 || RequestedChoice == 1) && TakenChoice != RequestedChoice;
}

struct SQmScoreboardVoteLayout
{
	CUIRect m_Panel{};
	CUIRect m_Header{};
	CUIRect m_Reason{};
	CUIRect m_Bars{};
	CUIRect m_Yes{};
	CUIRect m_No{};
	float m_Scale = 0.0f;
};

inline SQmScoreboardVoteLayout QmScoreboardVoteLayout(const CUIRect &Screen, const CUIRect &Scoreboard, bool Mini)
{
	SQmScoreboardVoteLayout Layout;
	constexpr float ScreenMargin = 6.0f;
	constexpr float PanelGap = 6.0f;
	constexpr float PanelHeight = 60.0f;
	const float PanelWidth = Mini ? 220.0f : 320.0f;
	const float AvailableWidth = std::max(0.0f, std::min(Scoreboard.w, Screen.w - 2.0f * ScreenMargin));
	const float AvailableHeight = std::max(0.0f, std::min(Scoreboard.y - PanelGap, Screen.y + Screen.h - ScreenMargin) - (Screen.y + ScreenMargin));
	Layout.m_Scale = std::min({1.0f, AvailableWidth / PanelWidth, AvailableHeight / PanelHeight});
	if(Layout.m_Scale <= 0.0f)
		return Layout;

	// 只使用计分板上方的临时区域，不写入玩家保存的 HUD 编辑器布局。
	const float Width = PanelWidth * Layout.m_Scale;
	const float Height = PanelHeight * Layout.m_Scale;
	const float Left = std::clamp(Scoreboard.x, Screen.x + ScreenMargin, Screen.x + Screen.w - ScreenMargin - Width);
	const float Bottom = std::min(Scoreboard.y - PanelGap, Screen.y + Screen.h - ScreenMargin);
	Layout.m_Panel = {Left, Bottom - Height, Width, Height};

	CUIRect Content;
	Layout.m_Panel.Margin(5.0f * Layout.m_Scale, &Content);
	Content.HSplitTop(10.0f * Layout.m_Scale, &Layout.m_Header, &Content);
	Content.HSplitTop(2.0f * Layout.m_Scale, nullptr, &Content);
	Content.HSplitTop(9.0f * Layout.m_Scale, &Layout.m_Reason, &Content);
	Content.HSplitTop(3.0f * Layout.m_Scale, nullptr, &Content);
	Content.HSplitTop(4.0f * Layout.m_Scale, &Layout.m_Bars, &Content);
	Content.HSplitTop(3.0f * Layout.m_Scale, nullptr, &Content);
	Content.VSplitMid(&Layout.m_Yes, &Layout.m_No, 5.0f * Layout.m_Scale);
	return Layout;
}

#endif
