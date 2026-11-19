#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTING_HUD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTING_HUD_H

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <algorithm>

enum class EVoteHudMode
{
	NONE,
	MINI,
	FULL,
};

// 非计分板 HUD 的唯一可见性入口。小 HUD 只改变面板密度，不改变投票生命周期：
// 投票中且尚未选择时显示；投完票后默认消失，只有 ShowAfterVoting 才继续显示。
inline EVoteHudMode QmVoteHudMode(bool IsVoting, int TakenChoice, bool ScoreboardActive, bool ShowAfterVoting, bool EditorPreview, bool Compact)
{
	if(EditorPreview)
		return Compact ? EVoteHudMode::MINI : EVoteHudMode::FULL;
	if(!IsVoting || ScoreboardActive)
		return EVoteHudMode::NONE;
	if(TakenChoice != 0 && !ShowAfterVoting)
		return EVoteHudMode::NONE;
	return Compact ? EVoteHudMode::MINI : EVoteHudMode::FULL;
}

inline bool QmScoreboardVoteCanSubmit(bool IsVoting, bool Interactive, int TakenChoice, int RequestedChoice)
{
	return IsVoting && Interactive && (RequestedChoice == -1 || RequestedChoice == 1) && TakenChoice != RequestedChoice;
}

struct SQmVoteHudLayout
{
	CUIRect m_Panel{};
	CUIRect m_Header{};
	CUIRect m_Description{};
	CUIRect m_Time{};
	CUIRect m_Reason{};
	CUIRect m_Bars{};
	CUIRect m_Yes{};
	CUIRect m_No{};
	float m_Scale = 0.0f;
};

struct SQmMiniVoteHudLayout
{
	CUIRect m_Panel{};
	CUIRect m_Description{};
	CUIRect m_Reason{};
	CUIRect m_Time{};
	CUIRect m_Spinner{};
	CUIRect m_Bars{};
	CUIRect m_Yes{};
	CUIRect m_No{};
};

// 游戏 HUD 使用世界屏幕坐标，计分板使用 UI 坐标。两者宽度不同，不能直接共用
// 同一个 320 单位宽度；按坐标系宽度折算后，非计分板面板才会和计分板同物理大小。
inline float QmVoteHudCoordScale(float GraphicsWidth, float UiWidth)
{
	if(GraphicsWidth <= 0.0f || UiWidth <= 0.0f)
		return 1.0f;
	return GraphicsWidth / UiWidth;
}

// 所有完整投票 HUD 的唯一版式入口：面板矩形与缩放固定后，四行内容只从这里切分。
inline SQmVoteHudLayout QmVoteHudLayoutFromRect(const CUIRect &Panel, float Scale)
{
	SQmVoteHudLayout Layout;
	if(Panel.w <= 0.0f || Panel.h <= 0.0f || Scale <= 0.0f)
		return Layout;
	Layout.m_Panel = Panel;
	Layout.m_Scale = Scale;

	namespace Token = ui_token::vote_hud;

	CUIRect Content;
	Layout.m_Panel.Margin(Token::PADDING * Scale, &Content);
	Content.HSplitTop(Token::HEADER_HEIGHT * Scale, &Layout.m_Header, &Content);
	Layout.m_Header.VSplitRight(Layout.m_Header.w * Token::TIME_WIDTH_RATIO, &Layout.m_Description, &Layout.m_Time);
	Layout.m_Description.VSplitRight(std::min(Token::TIME_GAP * Scale, Layout.m_Description.w), &Layout.m_Description, nullptr);
	Content.HSplitTop(Token::HEADER_GAP * Scale, nullptr, &Content);
	Content.HSplitTop(Token::REASON_HEIGHT * Scale, &Layout.m_Reason, &Content);
	Content.HSplitTop(Token::REASON_GAP * Scale, nullptr, &Content);
	Content.HSplitTop(Token::BARS_HEIGHT * Scale, &Layout.m_Bars, &Content);
	Content.HSplitTop(Token::BARS_GAP * Scale, nullptr, &Content);
	Content.VSplitMid(&Layout.m_Yes, &Layout.m_No, Token::BUTTON_GAP * Scale);
	return Layout;
}

// 恢复旧小投票 HUD 的紧凑行序：描述、理由、时间/进度、票数条、F3/F4。
inline SQmMiniVoteHudLayout QmMiniVoteHudLayoutFromRect(const CUIRect &Panel)
{
	SQmMiniVoteHudLayout Layout;
	if(Panel.w <= 0.0f || Panel.h <= 0.0f)
		return Layout;
	Layout.m_Panel = Panel;

	namespace Token = ui_token::vote_hud;

	CUIRect Content;
	Layout.m_Panel.Margin(Token::MINI_PADDING, &Content);
	Content.HSplitTop(Token::MINI_DESCRIPTION_HEIGHT, &Layout.m_Description, &Content);
	Content.HSplitTop(Token::MINI_DESCRIPTION_GAP, nullptr, &Content);
	Content.HSplitTop(Token::MINI_REASON_HEIGHT, &Layout.m_Reason, &Content);
	Content.HSplitTop(Token::MINI_REASON_GAP, nullptr, &Content);
	Content.HSplitTop(Token::MINI_TIME_HEIGHT, &Layout.m_Time, &Content);
	Layout.m_Time.VSplitLeft(Token::MINI_SPINNER_SIZE, &Layout.m_Spinner, &Layout.m_Time);
	Layout.m_Time.VSplitLeft(Token::MINI_SPINNER_GAP, nullptr, &Layout.m_Time);
	Content.HSplitTop(Token::MINI_TIME_GAP, nullptr, &Content);
	Content.HSplitTop(Token::MINI_BARS_HEIGHT, &Layout.m_Bars, &Content);
	Content.HSplitTop(Token::MINI_BARS_GAP, nullptr, &Content);
	Content.VSplitMid(&Layout.m_Yes, &Layout.m_No, Token::MINI_ACTION_GAP);
	return Layout;
}

// HUD 编辑器里的默认矩形。完整 HUD 按坐标系比例折算，和计分板保持同物理尺寸；
// 小 HUD 只缩小面板与信息密度，颜色、圆角和表面来源仍与大 HUD 相同。
inline CUIRect QmVoteHudDefaultRect(EVoteHudMode Mode, float CoordScale = 1.0f)
{
	namespace Token = ui_token::vote_hud;
	if(Mode == EVoteHudMode::MINI)
		return {0.0f, 60.0f, Token::MINI_PANEL_WIDTH, Token::MINI_PANEL_HEIGHT};
	if(Mode == EVoteHudMode::FULL)
		return {0.0f, 60.0f, Token::PANEL_WIDTH * CoordScale, Token::PANEL_HEIGHT * CoordScale};
	return {};
}

// 计分板上方的临时区域：不写入玩家保存的 HUD 布局，只借用同一套版式。
inline SQmVoteHudLayout QmScoreboardVoteLayout(const CUIRect &Screen, const CUIRect &Scoreboard)
{
	namespace Token = ui_token::vote_hud;
	// 计分板内投票不属于小 HUD：qm_mini_vote_hud 只影响非计分板场景。
	const float PanelWidth = Token::PANEL_WIDTH;
	const float AvailableWidth = std::max(0.0f, std::min(Scoreboard.w, Screen.w - 2.0f * Token::SCREEN_MARGIN));
	const float AvailableHeight = std::max(0.0f, std::min(Scoreboard.y - Token::PANEL_GAP, Screen.y + Screen.h - Token::SCREEN_MARGIN) - (Screen.y + Token::SCREEN_MARGIN));
	const float Scale = std::min({1.0f, AvailableWidth / PanelWidth, AvailableHeight / Token::PANEL_HEIGHT});
	if(Scale <= 0.0f)
		return {};

	const float Width = PanelWidth * Scale;
	const float Height = Token::PANEL_HEIGHT * Scale;
	const float Left = std::clamp(Scoreboard.x, Screen.x + Token::SCREEN_MARGIN, Screen.x + Screen.w - Token::SCREEN_MARGIN - Width);
	const float Bottom = std::min(Scoreboard.y - Token::PANEL_GAP, Screen.y + Screen.h - Token::SCREEN_MARGIN);
	return QmVoteHudLayoutFromRect({Left, Bottom - Height, Width, Height}, Scale);
}

#endif
