/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <engine/client.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/teamscore.h>

#include <game/client/component.h>
#include <game/client/QmUi/QmLayout.h>

#include <cstdint>
#include <vector>

struct SScoreInfo
{
	SScoreInfo()
	{
		Reset();
	}

	void Reset()
	{
		m_TextRankContainerIndex.Reset();
		m_TextScoreContainerIndex.Reset();
		m_RoundRectQuadContainerIndex = -1;
		m_OptionalNameTextContainerIndex.Reset();
		m_aScoreText[0] = 0;
		m_aRankText[0] = 0;
		m_aPlayerNameText[0] = 0;
		m_ScoreTextWidth = 0.f;
		m_Initialized = false;
	}

	STextContainerIndex m_TextRankContainerIndex;
	STextContainerIndex m_TextScoreContainerIndex;
	float m_ScoreTextWidth;
	char m_aScoreText[16];
	char m_aRankText[16];
	char m_aPlayerNameText[MAX_NAME_LENGTH];
	int m_RoundRectQuadContainerIndex;
	STextContainerIndex m_OptionalNameTextContainerIndex;

	bool m_Initialized;
};

class CHud : public CComponent
{
	static constexpr int SWITCH_COUNTDOWN_MAX_LINES = 3;
	float m_Width, m_Height;

	int m_HudQuadContainerIndex;
	SScoreInfo m_aScoreInfo[2];
	STextContainerIndex m_FPSTextContainerIndex;
	STextContainerIndex m_DDRaceEffectsTextContainerIndex;
	STextContainerIndex m_PlayerAngleTextContainerIndex;
	float m_PlayerPrevAngle;
	STextContainerIndex m_aPlayerSpeedTextContainers[2];
	float m_aPlayerPrevSpeed[2];
	int m_aPlayerSpeed[2];
	enum class ESpeedChange
	{
		NONE,
		INCREASE,
		DECREASE
	};
	ESpeedChange m_aLastPlayerSpeedChange[2];
	STextContainerIndex m_aPlayerPositionContainers[2];
	float m_aPlayerPrevPosition[2];
	struct SHudTextInfoV2AnimState
	{
		float m_FpsTargetX = 0.0f;
		float m_FpsTargetY = 0.0f;
		float m_PredTargetX = 0.0f;
		float m_PredTargetY = 0.0f;
		float m_FpsTargetAlpha = 0.0f;
		float m_PredTargetAlpha = 0.0f;
		float m_LastFpsWidth = 0.0f;
		float m_LastPredWidth = 0.0f;
		char m_aLastFpsText[16] = {0};
		char m_aLastPredText[64] = {0};
		bool m_FpsPositionInitialized = false;
		bool m_PredPositionInitialized = false;
		bool m_AlphaInitialized = false;

		void Reset()
		{
			m_FpsTargetX = 0.0f;
			m_FpsTargetY = 0.0f;
			m_PredTargetX = 0.0f;
			m_PredTargetY = 0.0f;
			m_FpsTargetAlpha = 0.0f;
			m_PredTargetAlpha = 0.0f;
			m_LastFpsWidth = 0.0f;
			m_LastPredWidth = 0.0f;
			m_aLastFpsText[0] = '\0';
			m_aLastPredText[0] = '\0';
			m_FpsPositionInitialized = false;
			m_PredPositionInitialized = false;
			m_AlphaInitialized = false;
		}
	};
	SHudTextInfoV2AnimState m_TextInfoV2AnimState;
	struct SHudLocalTimeV2AnimState
	{
		float m_TargetBoxX = 0.0f;
		float m_TargetBoxW = 0.0f;
		float m_TargetTextX = 0.0f;
		bool m_Initialized = false;

		void Reset()
		{
			m_TargetBoxX = 0.0f;
			m_TargetBoxW = 0.0f;
			m_TargetTextX = 0.0f;
			m_Initialized = false;
		}
	};
	SHudLocalTimeV2AnimState m_LocalTimeV2AnimState;
	struct SHudMediaIslandAnimState
	{
		enum class EVisualState
		{
			MINIMIZED,
			EXPANDED,
		};

		EVisualState m_VisualState = EVisualState::MINIMIZED;
		int64_t m_ExpandUntilTick = 0;
		int64_t m_LastTrackDurationMs = 0;
		float m_TargetX = 0.0f;
		float m_TargetWidth = 0.0f;
		float m_TargetTitleAlpha = 0.0f;
		float m_TargetTitleOffset = 0.0f;
		float m_TargetSpectatorAlpha = 0.0f;
		float m_CoverRotation = 0.0f;
		int64_t m_LastCoverRotationTick = 0;
		bool m_LayoutInitialized = false;
		bool m_HasTrackIdentity = false;
		char m_aLastTrackTitle[128] = {};
		char m_aLastTrackArtist[128] = {};
		char m_aLastTrackAlbum[128] = {};

		void Reset()
		{
			m_VisualState = EVisualState::MINIMIZED;
			m_ExpandUntilTick = 0;
			m_LastTrackDurationMs = 0;
			m_TargetX = 0.0f;
			m_TargetWidth = 0.0f;
			m_TargetTitleAlpha = 0.0f;
			m_TargetTitleOffset = 0.0f;
			m_TargetSpectatorAlpha = 0.0f;
			m_CoverRotation = 0.0f;
			m_LastCoverRotationTick = 0;
			m_LayoutInitialized = false;
			m_HasTrackIdentity = false;
			m_aLastTrackTitle[0] = '\0';
			m_aLastTrackArtist[0] = '\0';
			m_aLastTrackAlbum[0] = '\0';
		}
	};
	SHudMediaIslandAnimState m_MediaIslandAnimState;
	struct SHudSwitchCountdownAnimState
	{
		float m_aTargetX[SWITCH_COUNTDOWN_MAX_LINES] = {0.0f, 0.0f, 0.0f};
		float m_aTargetY[SWITCH_COUNTDOWN_MAX_LINES] = {0.0f, 0.0f, 0.0f};
		float m_aTargetAlpha[SWITCH_COUNTDOWN_MAX_LINES] = {0.0f, 0.0f, 0.0f};
		float m_aLastWidth[SWITCH_COUNTDOWN_MAX_LINES] = {0.0f, 0.0f, 0.0f};
		int m_aLastSwitchNumber[SWITCH_COUNTDOWN_MAX_LINES] = {-1, -1, -1};
		bool m_aPositionInitialized[SWITCH_COUNTDOWN_MAX_LINES] = {false, false, false};
		bool m_aAlphaInitialized[SWITCH_COUNTDOWN_MAX_LINES] = {false, false, false};
		bool m_aWasVisible[SWITCH_COUNTDOWN_MAX_LINES] = {false, false, false};
		char m_aaLastText[SWITCH_COUNTDOWN_MAX_LINES][64] = {};

		void Reset()
		{
			for(int i = 0; i < SWITCH_COUNTDOWN_MAX_LINES; ++i)
			{
				m_aTargetX[i] = 0.0f;
				m_aTargetY[i] = 0.0f;
				m_aTargetAlpha[i] = 0.0f;
				m_aLastWidth[i] = 0.0f;
				m_aLastSwitchNumber[i] = -1;
				m_aPositionInitialized[i] = false;
				m_aAlphaInitialized[i] = false;
				m_aWasVisible[i] = false;
				m_aaLastText[i][0] = '\0';
			}
		}
	};
	SHudSwitchCountdownAnimState m_SwitchCountdownAnimState;
	struct SHudSwitchCountdownTracker
	{
		int m_aaEndTick[NUM_DDRACE_TEAMS][256] = {};
		int m_aaTouchTick[NUM_DDRACE_TEAMS][256] = {};

		void Reset()
		{
			for(int t = 0; t < NUM_DDRACE_TEAMS; ++t)
			{
				for(int i = 0; i < 256; ++i)
				{
					m_aaEndTick[t][i] = 0;
					m_aaTouchTick[t][i] = 0;
				}
			}
		}
	};
	SHudSwitchCountdownTracker m_SwitchCountdownTracker;
	std::vector<SUiLayoutChild> m_vTextInfoLayoutChildrenScratch;
	std::vector<SUiLayoutChild> m_vLocalTimeLayoutChildrenScratch;

	void RenderCursor();

	void RenderTextInfo();
	void RenderSwapCountdown();
	void RenderSwitchCountdowns();
	void RenderDummyMiniMap();
	bool GetDummyMiniMapRect(float &X, float &Y, float &W, float &H) const;
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();

	void PrepareAmmoHealthAndArmorQuads();
	void RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter);

	void PreparePlayerStateQuads();
	void RenderPlayerState(int ClientId);
	bool HasVisibleMediaIsland() const;
	void RenderMediaIsland();

	int m_LastSpectatorCountTick;
	void RenderSpectatorCount();
	void RenderDummyActions();
	void RenderKeyStatus();
	void RenderMovementInformation();

	void UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue);
	void RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y);

	class CMovementInformation
	{
	public:
		vec2 m_Pos;
		vec2 m_Speed;
		float m_Angle = 0.0f;
	};
	class CMovementInformation GetMovementInformation(int ClientId, int Conn) const;

	void RenderGameTimer();
	void RenderPauseNotification();
	void RenderSuddenDeath();

	void RenderScoreHud();
	int m_LastLocalClientId = -1;

	void RenderSpectatorHud();
	void RenderWarmupTimer();
	void RenderLocalTime(float x);

	static constexpr float MOVEMENT_INFORMATION_LINE_HEIGHT = 8.0f;

public:
	CHud();
	int Sizeof() const override { return sizeof(*this); }

	void ResetHudContainers();
	void OnWindowResize() override;
	void OnReset() override;
	void OnRender() override;
	void OnInit() override;
	void OnNewSnapshot() override;

	// DDRace

	void OnMessage(int MsgType, void *pRawMsg) override;
	void RenderNinjaBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);

private:
	void RenderRecord();
	void RenderDDRaceEffects();
	float m_TimeCpDiff;
	float m_ServerRecord;
	float m_aPlayerRecord[NUM_DUMMIES];
	float m_FinishTimeDiff;
	int m_DDRaceTime;
	int m_FinishTimeLastReceivedTick;
	int m_TimeCpLastReceivedTick;
	bool m_ShowFinishTime;

	inline float GetMovementInformationBoxHeight();
	inline int GetDigitsIndex(int Value, int Max);

	// Quad Offsets
	int m_aAmmoOffset[NUM_WEAPONS];
	int m_HealthOffset;
	int m_EmptyHealthOffset;
	int m_ArmorOffset;
	int m_EmptyArmorOffset;
	int m_aCursorOffset[NUM_WEAPONS];
	int m_FlagOffset;
	int m_AirjumpOffset;
	int m_AirjumpEmptyOffset;
	int m_aWeaponOffset[NUM_WEAPONS];
	int m_EndlessJumpOffset;
	int m_EndlessHookOffset;
	int m_JetpackOffset;
	int m_TeleportGrenadeOffset;
	int m_TeleportGunOffset;
	int m_TeleportLaserOffset;
	int m_SoloOffset;
	int m_CollisionDisabledOffset;
	int m_HookHitDisabledOffset;
	int m_HammerHitDisabledOffset;
	int m_GunHitDisabledOffset;
	int m_ShotgunHitDisabledOffset;
	int m_GrenadeHitDisabledOffset;
	int m_LaserHitDisabledOffset;
	int m_DeepFrozenOffset;
	int m_LiveFrozenOffset;
	int m_DummyHammerOffset;
	int m_DummyCopyOffset;
	int m_PracticeModeOffset;
	int m_Team0ModeOffset;
	int m_LockModeOffset;

	bool m_MovementInfoBoxValid = false;
	float m_MovementInfoBoxX = 0.0f;
	float m_MovementInfoBoxY = 0.0f;
	float m_MovementInfoBoxW = 0.0f;
	float m_MovementInfoBoxH = 0.0f;
};

#endif
