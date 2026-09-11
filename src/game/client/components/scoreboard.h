/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/axiom_scores_data.h>
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

constexpr float ScoreboardRowsVerticalScale(float AvailableHeight, int NumRows, int NumTeamLabels, int NumTeamModeLabels, float LineHeight, float Spacing, float TeamFontSize, float TeamModeIconSize)
{
	if(AvailableHeight <= 0.0f || NumRows <= 0)
		return 1.0f;
	const int ClampedTeamLabels = NumTeamLabels < 0 ? 0 : (NumTeamLabels > NumRows ? NumRows : NumTeamLabels);
	const int ClampedTeamModeLabels = NumTeamModeLabels < 0 ? 0 : (NumTeamModeLabels > ClampedTeamLabels ? ClampedTeamLabels : NumTeamModeLabels);
	const int TextOnlyTeamLabels = ClampedTeamLabels - ClampedTeamModeLabels;
	const int RowsWithoutTeamLabels = NumRows - ClampedTeamLabels;
	const float TeamTextSpacing = TeamFontSize > Spacing ? TeamFontSize : Spacing;
	const float ScalableHeight = NumRows * LineHeight + RowsWithoutTeamLabels * Spacing + TextOnlyTeamLabels * TeamTextSpacing;
	const float FixedHeight = ClampedTeamModeLabels * TeamModeIconSize;
	const float RequiredHeight = ScalableHeight + FixedHeight;
	if(RequiredHeight <= AvailableHeight || ScalableHeight <= 0.0f)
		return 1.0f;
	return AvailableHeight > FixedHeight ? (AvailableHeight - FixedHeight) / ScalableHeight : 0.0f;
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
		std::array<bool, NUM_DDRACE_TEAMS> m_aTeamHasSpecPlayer{};
		int m_Count = 0;
	};

	void RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize);
	void RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize);
	void RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderGoals(CUIRect Goals);
	void RenderSpectators(CUIRect Spectators);
	void RenderMediaControls(CUIRect Controls);
	void RenderSoundMuteBar(CUIRect ScoreboardRect);
	void RenderTeamModeIcons(float x, float y, float IconSize, const SQmScoreboardTeamModeState &State, float Alpha);
	void UpdateTeamModeCache();
	void BuildPlayerRowPlan(int Team, CScoreboardPlayerRowPlan &Plan);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, const CScoreboardPlayerRowPlan &Plan, CScoreboardRenderState &State);
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
	static constexpr int SOUND_MUTE_BUTTON_COUNT = 9;

	IGraphics::CTextureHandle m_DeadTeeTexture;
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> m_aCachedTeamModes{};

	std::optional<vec2> m_LastMousePos;
	bool m_MouseUnlocked = false;
	bool m_RenderInteractions = false;

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
