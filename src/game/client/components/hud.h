/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmLayout.h>
#include <game/client/component.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/system_media_controls.h>
#include <game/client/ui_rect.h>
#include <game/teamscore.h>

#include <array>
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
		m_RoundRectCorners = -1;
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
	int m_RoundRectCorners;
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
		float m_LossTargetX = 0.0f;
		float m_LossTargetY = 0.0f;
		float m_FpsTargetAlpha = 0.0f;
		float m_PredTargetAlpha = 0.0f;
		float m_LossTargetAlpha = 0.0f;
		float m_LastFpsWidth = 0.0f;
		float m_LastPredWidth = 0.0f;
		float m_LastLossWidth = 0.0f;
		char m_aLastFpsText[16] = {0};
		char m_aLastPredText[64] = {0};
		char m_aLastLossText[16] = {0};
		bool m_FpsPositionInitialized = false;
		bool m_PredPositionInitialized = false;
		bool m_LossPositionInitialized = false;
		bool m_AlphaInitialized = false;

		void Reset()
		{
			m_FpsTargetX = 0.0f;
			m_FpsTargetY = 0.0f;
			m_PredTargetX = 0.0f;
			m_PredTargetY = 0.0f;
			m_LossTargetX = 0.0f;
			m_LossTargetY = 0.0f;
			m_FpsTargetAlpha = 0.0f;
			m_PredTargetAlpha = 0.0f;
			m_LossTargetAlpha = 0.0f;
			m_LastFpsWidth = 0.0f;
			m_LastPredWidth = 0.0f;
			m_LastLossWidth = 0.0f;
			m_aLastFpsText[0] = '\0';
			m_aLastPredText[0] = '\0';
			m_aLastLossText[0] = '\0';
			m_FpsPositionInitialized = false;
			m_PredPositionInitialized = false;
			m_LossPositionInitialized = false;
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
		static constexpr int SATELLITE_MAX_ITEMS = IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS;
		static constexpr int SATELLITE_LIVE_MAX_ITEMS = SATELLITE_MAX_ITEMS;

		enum class EVisualState
		{
			MINIMIZED,
			EXPANDED,
		};

		EVisualState m_VisualState = EVisualState::MINIMIZED;
		int64_t m_ExpandUntilTick = 0;
		int64_t m_TrackDetailsUntilTick = 0;
		bool m_LyricsActive = false;
		bool m_LyricsMarqueeInitialized = false;
		int64_t m_LyricsMarqueeStartTick = 0;
		char m_aLyricsMarquee[256] = {};
		float m_TargetX = 0.0f;
		float m_TargetWidth = 0.0f;
		float m_TargetHeight = 0.0f;
		float m_TargetTitleAlpha = 0.0f;
		float m_TargetTitleOffset = 0.0f;
		float m_TargetSpectatorAlpha = 0.0f;
		float m_TargetBottomAlpha = 0.0f;
		float m_TargetCoverInAlpha = 1.0f;
		float m_TargetCoverOutAlpha = 0.0f;
		float m_TargetCoverInScale = 1.0f;
		float m_TargetCoverOutScale = 1.0f;
		float m_TargetTrackTitleInAlpha = 1.0f;
		float m_TargetTrackTitleOutAlpha = 0.0f;
		float m_TargetTrackTitleInOffset = 0.0f;
		float m_TargetTrackTitleOutOffset = 0.0f;
		float m_TargetTrackMetaInAlpha = 1.0f;
		float m_TargetTrackMetaOutAlpha = 0.0f;
		float m_TargetTrackMetaInOffset = 0.0f;
		float m_TargetTrackMetaOutOffset = 0.0f;
		bool m_WaveformWasPlaying = false;
		bool m_WaveformSettling = false;
		int64_t m_WaveformSettleStartTick = 0;
		float m_WaveformSettleSampleTime = 0.0f;
		bool m_LayoutInitialized = false;
		bool m_HasTrackIdentity = false;
		SHudMediaIslandTrackSnapshot m_CurrentTrack;
		SHudMediaIslandTrackSnapshot m_OutgoingTrack;
		bool m_TrackTransitionActive = false;
		bool m_TrackTransitionNeedsNodeReset = false;
		int64_t m_TrackTransitionStartTick = 0;
		float m_OldTrackExitProgress = 1.0f;
		float m_NewTrackEnterProgress = 1.0f;
		bool m_CapsuleMorphActive = false;
		bool m_CapsuleMorphNeedsCapture = false;
		float m_CapsuleMorphFromX = 0.0f;
		float m_CapsuleMorphFromWidth = 0.0f;
		float m_CapsuleMorphFromHeight = 0.0f;
		struct SSatelliteItem
		{
			bool m_Used = false;
			bool m_Active = false;
			bool m_Seen = false;
			bool m_NodeInitialized = false;
			bool m_Completed = false;
			bool m_SwapOutgoing = false;
			EHudMediaIslandCountdownType m_Type = EHudMediaIslandCountdownType::SWAP;
			int m_Id = 0;
			int64_t m_TriggerTick = 0;
			int64_t m_ExitStartTick = 0;
			int64_t m_LiquidLastTick = 0;
			float m_Progress = 0.0f;
			float m_LiquidProgress = 0.0f;
			float m_LiquidOriginCenterX = 0.0f;
			float m_LiquidOriginWidth = 0.0f;

			void Reset()
			{
				*this = {};
			}
		};
		std::array<SSatelliteItem, SATELLITE_MAX_ITEMS> m_aSatelliteItems{};
		bool m_SatelliteGroupInitialized = false;
		float m_TargetSatelliteX = 0.0f;
		float m_TargetSatelliteWidth = 0.0f;
		float m_TargetSatelliteAlpha = 0.0f;
		float m_SpectatorLiquidProgress = 0.0f;
		int64_t m_SpectatorLiquidLastTick = 0;
		int m_SpectatorDisplayCount = 0;
		float m_SpectatorIconProgress = 1.0f;
		int64_t m_SpectatorIconLastTick = 0;
		bool m_SpectatorHadWatchers = false;
		float m_SpectatorExitLiquidStart = 0.0f;
		float m_SpectatorExitIconStart = 1.0f;

		void StartCapsuleMorph()
		{
			m_CapsuleMorphActive = true;
			m_CapsuleMorphNeedsCapture = true;
		}

		bool HasVisibleSatellite() const
		{
			if(m_SpectatorLiquidProgress > 0.0f)
				return true;
			for(const SSatelliteItem &Item : m_aSatelliteItems)
			{
				if(Item.m_Used)
					return true;
			}
			return false;
		}

		void Reset()
		{
			m_VisualState = EVisualState::MINIMIZED;
			m_ExpandUntilTick = 0;
			m_TrackDetailsUntilTick = 0;
			m_LyricsActive = false;
			m_LyricsMarqueeInitialized = false;
			m_LyricsMarqueeStartTick = 0;
			m_aLyricsMarquee[0] = '\0';
			m_TargetX = 0.0f;
			m_TargetWidth = 0.0f;
			m_TargetHeight = 0.0f;
			m_TargetTitleAlpha = 0.0f;
			m_TargetTitleOffset = 0.0f;
			m_TargetSpectatorAlpha = 0.0f;
			m_TargetBottomAlpha = 0.0f;
			m_TargetCoverInAlpha = 1.0f;
			m_TargetCoverOutAlpha = 0.0f;
			m_TargetCoverInScale = 1.0f;
			m_TargetCoverOutScale = 1.0f;
			m_TargetTrackTitleInAlpha = 1.0f;
			m_TargetTrackTitleOutAlpha = 0.0f;
			m_TargetTrackTitleInOffset = 0.0f;
			m_TargetTrackTitleOutOffset = 0.0f;
			m_TargetTrackMetaInAlpha = 1.0f;
			m_TargetTrackMetaOutAlpha = 0.0f;
			m_TargetTrackMetaInOffset = 0.0f;
			m_TargetTrackMetaOutOffset = 0.0f;
			m_WaveformWasPlaying = false;
			m_WaveformSettling = false;
			m_WaveformSettleStartTick = 0;
			m_WaveformSettleSampleTime = 0.0f;
			m_LayoutInitialized = false;
			m_HasTrackIdentity = false;
			m_CurrentTrack.Reset();
			m_OutgoingTrack.Reset();
			m_TrackTransitionActive = false;
			m_TrackTransitionNeedsNodeReset = false;
			m_TrackTransitionStartTick = 0;
			m_OldTrackExitProgress = 1.0f;
			m_NewTrackEnterProgress = 1.0f;
			m_CapsuleMorphActive = false;
			m_CapsuleMorphNeedsCapture = false;
			m_CapsuleMorphFromX = 0.0f;
			m_CapsuleMorphFromWidth = 0.0f;
			m_CapsuleMorphFromHeight = 0.0f;
			for(SSatelliteItem &Item : m_aSatelliteItems)
				Item.Reset();
			m_SatelliteGroupInitialized = false;
			m_TargetSatelliteX = 0.0f;
			m_TargetSatelliteWidth = 0.0f;
			m_TargetSatelliteAlpha = 0.0f;
			m_SpectatorLiquidProgress = 0.0f;
			m_SpectatorLiquidLastTick = 0;
			m_SpectatorDisplayCount = 0;
			m_SpectatorIconProgress = 1.0f;
			m_SpectatorIconLastTick = 0;
			m_SpectatorHadWatchers = false;
			m_SpectatorExitLiquidStart = 0.0f;
			m_SpectatorExitIconStart = 1.0f;
		}
	};
	SHudMediaIslandAnimState m_MediaIslandAnimState;
	struct SHudMediaIslandFrameCache
	{
		uint64_t m_Frame = 0;
		bool m_Valid = false;
		bool m_HasMediaState = false;
		CSystemMediaControls::SState m_MediaState{};
		bool m_ShowLyrics = false;
		bool m_LyricsActive = false;
		char m_aLyrics[256] = {};
		ColorRGBA m_LyricsColor = ColorRGBA(0.97f, 0.98f, 1.0f, 0.90f);
		int m_SpectatorCount = 0;
		bool m_HasVisible = false;
		bool m_AvoidanceValid = false;
		float m_AvoidanceRight = 0.0f;

		void Reset()
		{
			*this = {};
			m_LyricsColor = ColorRGBA(0.97f, 0.98f, 1.0f, 0.90f);
		}
	};
	mutable SHudMediaIslandFrameCache m_MediaIslandFrameCache;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurSource;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurTemporary;
	IGraphics::CRenderTargetHandle m_MediaIslandBlurTarget;
	int m_MediaIslandBlurWidth = 0;
	int m_MediaIslandBlurHeight = 0;
	bool m_MediaIslandBlurReady = false;
	uint64_t m_MediaIslandBlurLastAttemptFrame = 0;
	bool m_MediaIslandBlurAttemptInitialized = false;
	IGraphics::CRenderTargetHandle m_DummyMiniViewRenderTarget;
	int m_DummyMiniViewRenderTargetWidth = 0;
	int m_DummyMiniViewRenderTargetHeight = 0;
	struct SHudWeaponPresentationState
	{
		bool m_aClientInitialized[MAX_CLIENTS] = {};
		float m_aaTargetX[MAX_CLIENTS][NUM_WEAPONS] = {};
		float m_aaTargetY[MAX_CLIENTS][NUM_WEAPONS] = {};
		float m_aaTargetAlpha[MAX_CLIENTS][NUM_WEAPONS] = {};
		float m_aaTargetScale[MAX_CLIENTS][NUM_WEAPONS] = {};

		void Reset()
		{
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				m_aClientInitialized[ClientId] = false;
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				{
					m_aaTargetX[ClientId][Weapon] = 0.0f;
					m_aaTargetY[ClientId][Weapon] = 0.0f;
					m_aaTargetAlpha[ClientId][Weapon] = 0.0f;
					m_aaTargetScale[ClientId][Weapon] = 1.0f;
				}
			}
		}
	};
	SHudWeaponPresentationState m_WeaponPresentationState;
	struct SHudRecordingStatusAnimState
	{
		float m_TargetWidth = 0.0f;
		float m_TargetAlpha = 0.0f;
		float m_TargetTextAlpha = 0.0f;
		bool m_Initialized = false;

		void Reset()
		{
			m_TargetWidth = 0.0f;
			m_TargetAlpha = 0.0f;
			m_TargetTextAlpha = 0.0f;
			m_Initialized = false;
		}
	};
	SHudRecordingStatusAnimState m_RecordingStatusAnimState;
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
	struct SHudSwitchCountdownRingState
	{
		int m_Team = -1;
		int m_Number = 0;
		int m_ClientId = -1;
		int m_Connection = 0;
		int m_TriggerTick = 0;
		int m_EndTick = 0;
		int m_LayoutSlot = 0;
		vec2 m_Position{};
		vec2 m_Velocity{};
		float m_Alpha = 0.0f;
		bool m_Seen = false;
		bool m_Initialized = false;

		void Reset()
		{
			*this = {};
			m_Team = -1;
			m_ClientId = -1;
		}
	};
	std::array<SHudSwitchCountdownRingState, SWITCH_COUNTDOWN_MAX_LINES> m_aSwitchCountdownRings{};
	struct SHudSwitchCountdownTracker
	{
		int m_aaEndTick[NUM_DDRACE_TEAMS][256] = {};
		int m_aaTouchTick[NUM_DDRACE_TEAMS][256] = {};
		int m_aaClientId[NUM_DDRACE_TEAMS][256] = {};
		int m_aaConnection[NUM_DDRACE_TEAMS][256] = {};

		void Reset()
		{
			for(int t = 0; t < NUM_DDRACE_TEAMS; ++t)
			{
				for(int i = 0; i < 256; ++i)
				{
					m_aaEndTick[t][i] = 0;
					m_aaTouchTick[t][i] = 0;
					m_aaClientId[t][i] = -1;
					m_aaConnection[t][i] = -1;
				}
			}
		}
	};
	SHudSwitchCountdownTracker m_SwitchCountdownTracker;
	struct SHudMediaIslandMuteState
	{
		bool m_Confirmed = false;
		int64_t m_TriggerTick = 0;
		int64_t m_EndTick = 0;
		int64_t m_DurationTicks = 0;

		void Reset()
		{
			*this = {};
		}
	};
	SHudMediaIslandMuteState m_MediaIslandMuteState;
	std::vector<SUiLayoutChild> m_vTextInfoLayoutChildrenScratch;
	std::vector<SUiLayoutChild> m_vLocalTimeLayoutChildrenScratch;

	void RenderCursor();

	void RenderTextInfo();
	void RenderSwapCountdown();
	void RenderSwitchCountdowns();
	void UpdateSwitchCountdownTracker();
	bool HasActiveSwitchCountdown() const;
	bool BuildSwitchCountdownSummary(char *pBuf, size_t BufSize) const;
	void ResetSwitchCountdownRings();
	void RenderFollowSwitchCountdowns();
	void RenderDummyMiniMap();
	void DestroyDummyMiniViewRenderTarget();
	bool GetDummyMiniMapRect(float &X, float &Y, float &W, float &H) const;
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();

	void PrepareAmmoHealthAndArmorQuads();
	void RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter);

	void PreparePlayerStateQuads();
	void RenderPlayerState(int ClientId);
	void EnsureMediaIslandFrameCache() const;
	bool HasVisibleMediaIsland() const;
	float GetTopIslandAvoidanceRight() const;
	void DestroyMediaIslandBlurTargets();
	bool PrepareMediaIslandBlur();
	void RenderMediaIsland();

	int m_LastSpectatorCountTick;
	void RenderSpectatorCount();
	void RenderDummyActions();
	void RenderKeyStatus();
	void RenderMovementInformation();
	void RenderJumpHint();
	void RenderMapProgressBar();

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

	void RenderSpeedrunTimer();
	void RenderGameTimer();
	void RenderPauseNotification();
	void RenderSuddenDeath();

	void RenderScoreHud();
	int m_LastLocalClientId = -1;

	void RenderSpectatorHud();
	void RenderWarmupTimer();
	void RenderLocalTime(float x);
	float RenderLegacyMediaInfoAt(float AnchorX, float CenterY);
	void RenderLegacyMediaInfo();
	bool GetLegacyMediaInfoAnchor(float &AnchorX, float &CenterY) const;

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
	void OnRelease() override;

	// DDRace

	void OnMessage(int MsgType, void *pRawMsg) override;
	void HandleSpamProtectionMessage(const char *pMessage);
	void RenderNinjaBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);

private:
	void RenderRecord();
	void RenderDDRaceEffects();
	float m_TimeCpDiff;
	float m_aPlayerRecord[NUM_DUMMIES];
	float m_FinishTimeDiff;
	int m_DDRaceTime;
	int m_FinishTimeLastReceivedTick;
	int m_TimeCpLastReceivedTick;
	bool m_ShowFinishTime;
	int m_SpeedrunTimerExpiredTick = 0;

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
	CUIRect m_MediaIslandLastVisibleRect{};
	bool m_MediaIslandLastVisibleRectValid = false;
	bool m_LegacyMediaInfoRendered = false;
	float m_aMapProgressDisplayed[NUM_DUMMIES] = {0.0f, 0.0f};
	bool m_aMapProgressInitialized[NUM_DUMMIES] = {false, false};
};

#endif
