/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/axiom_scores_data.h>
#include <game/client/components/qmclient/scoreboard_media_controls.h>
#include <game/client/components/qmclient/scoreboard_team_modes.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>
#include <game/teamscore.h>

#include <array>

struct CNetObj_PlayerInfo;

struct SScoreboardRowRenderDetail
{
	bool m_FullTee = true;
	bool m_ShowClientBrand = true;
	bool m_ShowClan = true;
	bool m_ShowCountry = true;
};

struct SScoreboardTeamLabelLayout
{
	float m_X;
	float m_Y;
	float m_IconY;
	float m_RowSpacing;
};

constexpr float SCOREBOARD_TEAM_MODE_ICON_SIZE = 12.0f;

constexpr SScoreboardRowRenderDetail ResolveScoreboardRowRenderDetail()
{
	return {};
}

constexpr SScoreboardTeamLabelLayout ResolveScoreboardTeamLabelLayout(float RowX, float RowY, float RowHeight, float Spacing, float TeamFontSize, float TeamIconSize, bool EndsDDTeam)
{
	const float ContentHeight = TeamIconSize > TeamFontSize ? TeamIconSize : TeamFontSize;
	const float RowSpacing = EndsDDTeam && ContentHeight > Spacing ? ContentHeight : Spacing;
	const float ContentY = RowY + RowHeight;
	return {
		RowX + 5.0f,
		ContentY + (RowSpacing - TeamFontSize) / 2.0f,
		ContentY + (RowSpacing - TeamIconSize) / 2.0f,
		RowSpacing,
	};
}

constexpr float ScoreboardRowsHeight(int NumRows, int NumTeamLabels, int NumTeamModeLabels, float LineHeight, float Spacing, float TeamFontSize, float TeamModeIconSize)
{
	if(NumRows <= 0)
		return 0.0f;
	const int ClampedTeamLabels = NumTeamLabels < 0 ? 0 : (NumTeamLabels > NumRows ? NumRows : NumTeamLabels);
	const int ClampedTeamModeLabels = NumTeamModeLabels < 0 ? 0 : (NumTeamModeLabels > ClampedTeamLabels ? ClampedTeamLabels : NumTeamModeLabels);
	const float TextSpacing = ResolveScoreboardTeamLabelLayout(0.0f, 0.0f, LineHeight, Spacing, TeamFontSize, 0.0f, true).m_RowSpacing;
	const float IconSpacing = ResolveScoreboardTeamLabelLayout(0.0f, 0.0f, LineHeight, Spacing, TeamFontSize, TeamModeIconSize, true).m_RowSpacing;
	return NumRows * LineHeight + (NumRows - ClampedTeamLabels) * Spacing +
	       (ClampedTeamLabels - ClampedTeamModeLabels) * TextSpacing + ClampedTeamModeLabels * IconSpacing;
}

constexpr float ScoreboardRowsVerticalScale(float AvailableHeight, int NumRows, int NumTeamLabels, int NumTeamModeLabels, float LineHeight, float Spacing, float TeamFontSize, float TeamModeIconSize)
{
	if(NumRows <= 0)
		return 1.0f;
	if(AvailableHeight <= 0.0f)
		return 0.0f;
	const int ClampedTeamLabels = NumTeamLabels < 0 ? 0 : (NumTeamLabels > NumRows ? NumRows : NumTeamLabels);
	const int ClampedTeamModeLabels = NumTeamModeLabels < 0 ? 0 : (NumTeamModeLabels > ClampedTeamLabels ? ClampedTeamLabels : NumTeamModeLabels);
	const int TextOnlyTeamLabels = ClampedTeamLabels - ClampedTeamModeLabels;
	const int RowsWithoutTeamLabels = NumRows - ClampedTeamLabels;
	const float TeamTextSpacing = TeamFontSize > Spacing ? TeamFontSize : Spacing;
	const float TeamModeSpacing = TeamModeIconSize > TeamTextSpacing ? TeamModeIconSize : TeamTextSpacing;
	// 图标与文字随玩家行一起缩放，高度预算必须使用同一比例。
	const float RequiredHeight = NumRows * LineHeight + RowsWithoutTeamLabels * Spacing + TextOnlyTeamLabels * TeamTextSpacing + ClampedTeamModeLabels * TeamModeSpacing;
	if(RequiredHeight <= AvailableHeight)
		return 1.0f;
	return AvailableHeight / RequiredHeight;
}

constexpr CUIRect ScoreboardPlayerRowsRect(CUIRect Column, float HeadlineHeight)
{
	const float HeaderHeight = maximum(0.0f, minimum(HeadlineHeight, Column.h));
	return {Column.x, Column.y + HeaderHeight, Column.w, maximum(0.0f, Column.h - HeaderHeight - 5.0f)};
}

constexpr CUIRect ScoreboardDeadTeeRect(CUIRect Row, float TeeX, float TeeWidth, float PreferredSize)
{
	const float Size = maximum(0.0f, minimum(PreferredSize, minimum(TeeWidth, Row.h)));
	return {TeeX + (TeeWidth - Size) / 2.0f, Row.y + (Row.h - Size) / 2.0f, Size, Size};
}

// 滚动模式只在非队伍玩法且人数超过固定可视行数时生效。
constexpr bool ScoreboardScrollModeEnabled(bool IsTeamPlay, bool ConfigEnabled, int NumPlayers)
{
	return !IsTeamPlay && ConfigEnabled && NumPlayers > 16;
}

// 滚动条只在当前帧仍可交互且确实存在可滚动内容时绘制。
constexpr bool ScoreboardScrollbarVisible(bool ScrollMode, bool Interactive, bool ConfigEnabled, int ScrollMaxStart)
{
	return ScrollMode && Interactive && ConfigEnabled && ScrollMaxStart > 0;
}

// 标题计分时间沿用计分板内容的统一淡出透明度。
constexpr ColorRGBA ScoreboardTitleTimeColor(float ContentAlpha)
{
	return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f).WithMultipliedAlpha(ContentAlpha);
}

class CScoreboard : public CComponent
{
	struct CScoreboardRenderState
	{
		int m_CurrentDDTeamSize;

		CScoreboardRenderState() :
			m_CurrentDDTeamSize(0) {}
	};
	struct CScoreboardPlayerRow
	{
		const CNetObj_PlayerInfo *m_pInfo = nullptr;
		int m_DDTeam = 0;
		int m_PreviousSourceDDTeam = -1;
		int m_NextSourceDDTeam = 0;
		bool m_Dead = false;
	};
	struct CScoreboardPlayerRowPlan
	{
		std::array<CScoreboardPlayerRow, MAX_CLIENTS> m_aRows{};
		std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> m_aTeamModes{};
		std::array<bool, NUM_DDRACE_TEAMS> m_aTeamHasPlayer{};
		int m_Count = 0;
	};

	void RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize);
	void RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize);
	void RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderServerPlayerCount(CUIRect Rect, const char *pText);
	void RenderGoals(CUIRect Goals);
	void RenderFooter(CUIRect Footer);
	void RenderSpectators(CUIRect Spectators);
	void RenderSoundMuteBar(CUIRect ScoreboardRect);
	void RenderTeamModeIcons(float x, float y, float IconSize, const SQmScoreboardTeamModeState &State, float Alpha);
	void UpdateTeamModeCache();
	void BuildPlayerRowPlan(int Team, CScoreboardPlayerRowPlan &Plan);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, const CScoreboardPlayerRowPlan &Plan, CScoreboardRenderState &State, bool Scroll = false);
	void RenderRecordingNotification(float x);
	static CUi::EPopupMenuFunctionResult PopupScoreboard(void *pContext, CUIRect View, bool Active);

	static void ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData);

	const char *GetTeamName(int Team) const;

	bool m_Active;
	float m_ServerRecord;
	float m_Visibility;
	float m_OpenTime;
	float m_AnimContentAlpha;
	bool m_PresentationInitialized;
	// 国旗入场重放锚点：记分板由隐藏转为显示的瞬间记录时刻，行内国旗以它作为
	// 入场动画起点，规避名牌国旗持续渲染对 CCountryFlags 重现检测的遮蔽。
	int64_t m_ActiveStartTime = 0;
	bool m_WasActive = false;
	static constexpr int SOUND_MUTE_BUTTON_COUNT = 9;

	IGraphics::CTextureHandle m_DeadTeeTexture;
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> m_aCachedTeamModes{};

	// 滚动状态由组件持有，离开滚动模式或释放客户端时复位。
	CScrollRegion m_ScrollRegion;

	std::optional<vec2> m_LastMousePos;
	bool m_MouseUnlocked = false;
	bool m_RenderInteractions = false;
	CQmScoreboardMediaControls m_MediaControls;

	struct SSoundMuteButtonAnimState
	{
		std::array<float, SOUND_MUTE_BUTTON_COUNT> m_aTargetAlpha{};
		std::array<float, SOUND_MUTE_BUTTON_COUNT> m_aTargetScale{};
		std::array<float, SOUND_MUTE_BUTTON_COUNT> m_aTargetOffsetX{};
		std::array<float, SOUND_MUTE_BUTTON_COUNT> m_aTargetReveal{};
		bool m_Initialized = false;

		void Reset()
		{
			m_aTargetAlpha.fill(0.0f);
			m_aTargetScale.fill(1.0f);
			m_aTargetOffsetX.fill(18.0f);
			m_aTargetReveal.fill(0.0f);
			m_Initialized = false;
		}
	} m_SoundMuteButtonAnimState;

	struct SSoundMuteInfoAnimState
	{
		float m_TargetAlpha = 0.0f;
		float m_TargetOffsetX = 14.0f;
		bool m_Initialized = false;
		int m_HoveredButton = -1;

		void Reset()
		{
			m_TargetAlpha = 0.0f;
			m_TargetOffsetX = 14.0f;
			m_Initialized = false;
			m_HoveredButton = -1;
		}
	} m_SoundMuteInfoAnimState;

	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	class CScoreboardPopupContext : public SPopupMenuId
	{
	public:
		CScoreboard *m_pScoreboard = nullptr;
		CButtonContainer m_FriendAction;
		CButtonContainer m_MuteAction;
		CButtonContainer m_EmoticonAction;
		CButtonContainer m_CopySkinAction;

		CButtonContainer m_SpectateButton;

		int m_ClientId;
		bool m_IsLocal;
		bool m_IsSpectating;

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_ScoreboardPopupContext;

	class CMapTitlePopupContext : public SPopupMenuId
	{
	public:
		CScoreboard *m_pScoreboard = nullptr;

		float m_FontSize;

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_MapTitlePopupContext;
	char m_MapTitleButtonId;

	class CPlayerElement
	{
	public:
		char m_PlayerButtonId;
		char m_SpectatorSecondLineButtonId;
	};
	CPlayerElement m_aPlayers[MAX_CLIENTS];

	// 本帧解析出的 Axiom 积分模式（NONE 表示当前服务器不是 Axiom 积分服）。
	// 每帧在 OnRender 开头刷新一次，渲染期由 HasQmAxiomScoreMode / QmAxiomScorePoints 复用。
	EQmAxiomMode m_QmAxiomScoreModeFrame = EQmAxiomMode::NONE;
	bool m_QmAxiomScoreModeFrameValid = false;

	void UpdateQmAxiomScoreMode();
	bool HasQmAxiomScoreMode();
	void QmAxiomScorePoints(const char *pPlayerName, bool Visible, char *pBuffer, int BufferSize) const;

public:
	CScoreboard();
	int Sizeof() const override { return sizeof(*this); }
	static bool ShouldAutoShowOnDeath(bool ConfigEnabled, bool HasLocalInfo, bool Spectating, bool GamePaused, bool HasLocalCharacter)
	{
		return ConfigEnabled && HasLocalInfo && !Spectating && !GamePaused && !HasLocalCharacter;
	}
	void OnConsoleInit() override;
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const;
};

#endif
