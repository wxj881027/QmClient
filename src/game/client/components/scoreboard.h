/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <array>

class CScoreboard : public CComponent
{
	struct CScoreboardRenderState
	{
		float m_TeamStartX;
		float m_TeamStartY;
		int m_CurrentDDTeamSize;

		CScoreboardRenderState() :
			m_TeamStartX(0), m_TeamStartY(0), m_CurrentDDTeamSize(0) {}
	};

	void RenderTitle(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderGoals(CUIRect Goals);
	void RenderSpectators(CUIRect Spectators);
	void RenderMediaControls(CUIRect Controls);
	void RenderSoundMuteBar(CUIRect ScoreboardRect);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State);
	void RenderRecordingNotification(float x);

	static void ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData);

	const char *GetTeamName(int Team) const;

	bool m_Active;
	float m_ServerRecord;
	float m_Visibility;
	float m_OpenTime;
	float m_AnimContentAlpha;
	static constexpr int SOUND_MUTE_BUTTON_COUNT = 9;

	IGraphics::CTextureHandle m_DeadTeeTexture;

	std::optional<vec2> m_LastMousePos;
	bool m_MouseUnlocked = false;

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
	} m_ScoreboardPopupContext;

	static CUi::EPopupMenuFunctionResult PopupScoreboard(void *pContext, CUIRect View, bool Active);

public:
	CScoreboard();
	int Sizeof() const override { return sizeof(*this); }
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
