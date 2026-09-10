/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_GAMECLIENT_H
#define GAME_CLIENT_GAMECLIENT_H

#include "qm_icon_manager.h"
#include "qm_ime_manager.h"
#include "render.h"

#include <base/color.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/client/gpu_upload_limiter.h>
#include <engine/console.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/shared/client_brand.h>
#include <engine/shared/config.h>
#include <engine/shared/snapshot.h>

#include <generated/protocol7.h>
#include <generated/protocolglue.h>

#include <game/client/prediction/gameworld.h>
#include <game/client/race.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/map/render_map.h>
#include <game/mapbugs.h>
#include <game/teamscore.h>

// components
#include "QmUi/QmRt.h"
#include "components/background.h"
#include "components/binds.h"
#include "components/broadcast.h"
#include "components/camera.h"
#include "components/censor.h"
#include "components/chat.h"
#include "components/console.h"
#include "components/controls.h"
#include "components/countryflags.h"
#include "components/damageind.h"
#include "components/debughud.h"
#include "components/effects.h"
#include "components/emoticon.h"
#include "components/flow.h"
#include "components/freezebars.h"
#include "components/ghost.h"
#include "components/hud.h"
#include "components/hud_editor.h"
#include "components/hud_frozen_tee_state.h"
#include "components/important_alert.h"
#include "components/infomessages.h"
#include "components/items.h"
#include "components/key_binder.h"
#include "components/local_server.h"
#include "components/mapimages.h"
#include "components/maplayers.h"
#include "components/mapsounds.h"
#include "components/menu_background.h"
#include "components/menus.h"
#include "components/motd.h"
#include "components/nameplates.h"
#include "components/particles.h"
#include "components/pie_menu.h"
#include "components/player_points.h"
#include "components/players.h"
#include "components/qmclient/axiom_auto_login.h"
#include "components/qmclient/axiom_scores.h"
#include "components/qmclient/chat_emoji.h"
#include "components/qmclient/collision_hitbox.h"
#include "components/qmclient/data_version.h"
#include "components/qmclient/hammer_hit_detection.h"
#include "components/qmclient/hud_notifications/hud_notifications.h"
#include "components/qmclient/input_overlay.h"
#include "components/qmclient/monitoring/monitoring.h"
#include "components/qmclient/music_app_watcher.h"
#include "components/qmclient/music_lyrics/music_lyrics_integration.h"
#include "components/qmclient/music_lyrics/qm_spotify_integration.h"
#include "components/qmclient/netease/netease_integration.h"
#include "components/qmclient/qm_bind_status_hud.h"
#include "components/qmclient/qmclient.h"
#include "components/qmclient/scripting.h"
#include "components/qmclient/stutter_diagnostics.h"
#include "components/qmclient/translate/translate.h"
#include "components/qmclient/voice/voice_component.h"
#include "components/qmclient/weapon_trajectory.h"
#include "components/race_demo.h"
#include "components/scoreboard.h"
#include "components/section_loader.h"
#include "components/skins.h"
#include "components/skins7.h"
#include "components/sounds.h"
#include "components/spectator.h"
#include "components/statboard.h"
#include "components/system_media_controls.h"
#include "components/tclient/background_particles.h"
#include "components/tclient/bg_draw.h"
#include "components/tclient/bindchat.h"
#include "components/tclient/bindwheel.h"
#include "components/tclient/custom_communities.h"
#include "components/tclient/fast_practice.h"
#include "components/tclient/mod.h"
#include "components/tclient/moving_tiles.h"
#include "components/tclient/outlines.h"
#include "components/tclient/pet.h"
#include "components/tclient/player_indicator.h"
#include "components/tclient/rainbow.h"
#include "components/tclient/skinprofiles.h"
#include "components/tclient/statusbar.h"
#include "components/tclient/tclient.h"
#include "components/tclient/trails.h"
#include "components/tclient/warlist.h"
#include "components/tooltips.h"
#include "components/touch_controls.h"
#include "components/ui_effects.h"
#include "components/voting.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CQmJelly;
class CCollisionHitbox;

class CGameInfo
{
public:
	bool m_FlagStartsRace;
	bool m_TimeScore;
	bool m_UnlimitedAmmo;
	bool m_DDRaceRecordMessage;
	bool m_RaceRecordMessage;
	bool m_RaceSounds;

	bool m_AllowEyeWheel;
	bool m_AllowHookColl;
	bool m_AllowZoom;

	bool m_BugDDRaceGhost;
	bool m_BugDDRaceInput;
	bool m_BugFNGLaserRange;
	bool m_BugVanillaBounce;

	bool m_PredictFNG;
	bool m_PredictDDRace;
	bool m_PredictDDRaceTiles;
	bool m_PredictVanilla;

	bool m_EntitiesDDNet;
	bool m_EntitiesDDRace;
	bool m_EntitiesRace;
	bool m_EntitiesFNG;
	bool m_EntitiesVanilla;
	bool m_EntitiesBW;
	bool m_EntitiesFDDrace;

	bool m_Race;
	bool m_Pvp;

	bool m_DontMaskEntities;
	bool m_AllowXSkins;

	bool m_HudHealthArmor;
	bool m_HudAmmo;
	bool m_HudDDRace;

	bool m_NoWeakHookAndBounce;
	bool m_NoSkinChangeForFrozen;

	bool m_DDRaceTeam;
	bool m_PredictEvents;
	char m_aGameType[16];
};

class CSnapEntities
{
public:
	IClient::CSnapItem m_Item;
	const CNetObj_EntityEx *m_pDataEx;
};

enum class EClientIdFormat
{
	NO_INDENT,
	INDENT_AUTO,
	INDENT_FORCE, // for rendering settings preview
};

class CGameClient : public IGameClient
{
public:
	struct SDemoHudPlaybackState
	{
		bool m_Valid = false;
		int m_DummyResetOnSwitch = 0;
		int m_DeepflyMode = 0;
		bool m_DummyControl = false;
		bool m_DummyCopyMoves = false;
	};

	struct SDemoInputPlaybackState
	{
		bool m_Valid = false;
		unsigned char m_aKeyStates[KEY_LAST / 8] = {};
		int m_TargetX = 0;
		int m_TargetY = 0;
		unsigned m_WheelMask = 0;
		uint64_t m_WheelSequence = 0;
		bool m_GamepadValid = false;
		uint32_t m_GamepadButtons = 0;
		float m_aGamepadAxes[6] = {};
		int m_GamepadPlayerIndex = 0;
	};

	friend class CTClient;
	friend class CFastPractice;
	friend class CCollisionHitbox;

	// all components
	CInfoMessages m_InfoMessages;
	CCamera m_Camera;
	CChat m_Chat;
	CCensor m_Censor;
	CMotd m_Motd;
	CBroadcast m_Broadcast;
	CGameConsole m_GameConsole;
	CBinds m_Binds;
	CKeyBinder m_KeyBinder;
	CParticles m_Particles;
	CMenus m_Menus;
	CSkins m_Skins;
	CSkins7 m_Skins7;
	CCountryFlags m_CountryFlags;
	CFlow m_Flow;
	CHud m_Hud;
	CImportantAlert m_ImportantAlert;
	CDebugHud m_DebugHud;
	CControls m_Controls;
	CEffects m_Effects;
	CScoreboard m_Scoreboard;
	CStatboard m_Statboard;
	CSounds m_Sounds;
	CEmoticon m_Emoticon;
	CSystemMediaControls m_SystemMediaControls;
	CNeteaseIntegration m_NeteaseIntegration;
	CMusicLyricsIntegration m_MusicLyricsIntegration;
	CQmMusicAppWatcher m_MusicAppWatcher;
	CSpotifyIntegration m_SpotifyIntegration;

	CDamageInd m_DamageInd;
	CTouchControls m_TouchControls;
	CVoting m_Voting;
	CSpectator m_Spectator;

	CPlayers m_Players;
	CNamePlates m_NamePlates;
	CFreezeBars m_FreezeBars;
	CItems m_Items;
	CMapImages m_MapImages;

	CMapLayers m_MapLayersBackground = CMapLayers{ERenderType::RENDERTYPE_BACKGROUND};
	CMapLayers m_MapLayersForeground = CMapLayers{ERenderType::RENDERTYPE_FOREGROUND};
	CBackground m_Background;
	CBackgroundParticles m_BackgroundParticles;
	CMenuBackground m_MenuBackground;
	CUiEffects m_UiEffects;

	CMapSounds m_MapSounds;

	CRaceDemo m_RaceDemo;
	CGhost m_Ghost;
	CHudEditor m_HudEditor;

	CTooltips m_Tooltips;

	CLocalServer m_LocalServer;

	// TClient Components
	CSkinProfiles m_SkinProfiles;
	CStatusBar m_StatusBar;
	CBindChat m_BindChat;
	CBindWheel m_BindWheel;
	CBgDraw m_BgDraw;
	CQmClient m_QmClient;
	CQmAxiomAutoLogin m_QmAxiomAutoLogin;
	CQmAxiomScores m_QmAxiomScores;
	CQmChatEmoji m_QmChatEmoji;
	CQmMonitoring m_QmMonitoring;
	CQmHudNotifications m_QmHudNotifications;
	CQmBindStatusHud m_QmBindStatusHud;
	CQmWeaponTrajectory m_QmWeaponTrajectory;
	CTClient m_TClient;
	CFastPractice m_FastPractice;
	CVoiceComponent m_Voice;
	CTrails m_Trails;
	CTranslate m_Translate;
	CPet m_Pet;
	CPlayerIndicator m_PlayerIndicator;
	COutlines m_Outlines;
	CRainbow m_Rainbow;
	CWarList m_WarList;
	CScripting m_Scripting;
	CMod m_Mod;
	CCustomCommunities m_CustomCommunities;
	CPlayerPoints m_PlayerPoints;
	CCollisionHitbox m_CollisionHitbox;
	CPieMenu m_PieMenu;
	CInputOverlay m_InputOverlay;
	CMovingTiles m_MovingTilesBackground = CMovingTiles{false};
	CMovingTiles m_MovingTilesForeground = CMovingTiles{true};

private:
	struct SQmStutterComponentWindowSamples
	{
		CQmStutterSampleSeries m_Update;
		CQmStutterSampleSeries m_Render;
	};

	void ProcessQmStutterFrame();
	void RecordComponentUpdate(size_t ComponentIndex, double DurationMs);
	void RecordComponentRender(size_t ComponentIndex, double DurationMs);
	void CaptureQmStutterFeatureSnapshot();
	void FlushQmStutterWindow(const SQmStutterFrameDecision &Decision, bool ForceLog);
	void ResetQmStutterWindowSamples();

	std::vector<class CComponent *> m_vpAll;
	std::vector<const char *> m_vpAllPerfNames;
	std::vector<class CComponent *> m_vpInput;
	std::vector<double> m_vQmStutterPendingUpdateMs;
	std::vector<double> m_vQmStutterPendingRenderMs;
	std::vector<SQmStutterComponentWindowSamples> m_vQmStutterComponentSamples;
	std::vector<std::pair<std::string, int>> m_vQmStutterFeatureSnapshot;
	CQmStutterEpisodeTracker m_QmStutterEpisodeTracker;
	CQmStutterSampleSeries m_QmStutterFrameSamples;
	bool m_QmStutterDiagnosticsWasEnabled = false;
	uint64_t m_QmStutterWindowStartFrame = 0;
	uint64_t m_QmStutterWorstFrame = 0;
	double m_QmStutterWorstFrameMs = 0.0;
	EQmStutterLimitCause m_QmStutterLimitCause = EQmStutterLimitCause::NONE;
	std::string m_QmStutterPage;
	std::string m_QmStutterOperation;
	std::unique_ptr<CQmJelly> m_pJellyTee;

	CNetObjHandler m_NetObjHandler;
	protocol7::CNetObjHandler m_NetObjHandler7;

	// Global GPU texture upload limiter
	CGpuUploadLimiter m_GpuUploadLimiter;

	class IEngine *m_pEngine = nullptr;
	class IInput *m_pInput = nullptr;
	class IGraphics *m_pGraphics = nullptr;
	class ITextRender *m_pTextRender = nullptr;
	class IClient *m_pClient = nullptr;
	class ISound *m_pSound = nullptr;
	class IConfigManager *m_pConfigManager = nullptr;
	class CConfig *m_pConfig = nullptr;
	class IConsole *m_pConsole = nullptr;
	class IStorage *m_pStorage = nullptr;
	class IDemoPlayer *m_pDemoPlayer = nullptr;
	class IFavorites *m_pFavorites = nullptr;
	class IServerBrowser *m_pServerBrowser = nullptr;
	class IEditor *m_pEditor = nullptr;
	class IFriends *m_pFriends = nullptr;
	class IFriends *m_pFoes = nullptr;
	class IDiscord *m_pDiscord = nullptr;
	class IFrameScheduler *m_pFrameScheduler = nullptr;
#if defined(CONF_AUTOUPDATE)
	class IUpdater *m_pUpdater = nullptr;
#endif
	class IHttp *m_pHttp = nullptr;

	CLayers m_Layers;
	CCollision m_Collision;
	CUi m_UI;
	CUiRuntimeV2 m_UiRuntimeV2;
	CQmIconManager m_QmIconManager;
	int m_AppliedQmUiIconWeight = -1;
	CQmImeManager m_QmImeManager;
	CRaceHelper m_RaceHelper;
	CQmHammerHitTracker m_HammerHitTracker;
	struct SPendingHammerHitEvent
	{
		vec2 m_Pos;
		int m_SnapshotTick;
		int m_Connection;
		int m_EventOrdinal;
		bool m_RenderEffect;
	};
	std::vector<SPendingHammerHitEvent> m_vPendingHammerHitEvents;

	void ProcessEvents();
	void FinalizeHammerHitEvents();
	void UpdatePositions();
	void RecordDemoHudState(bool Force);
	void RecordDemoInputState(bool Force);
	void RecordDemoInputWheelEvent() const;
	void RecordDemoGamepadState(bool Force);
	static int PackDemoHudState(int DummyResetOnSwitch, int DeepflyMode, bool DummyControl, bool DummyCopyMoves);
	void UnpackDemoHudState(int PackedState);

	int m_EditorMovementDelay = 5;
	void UpdateEditorIngameMoved();
	void HandleHammerSkinSwap(const SQmHammerHitRecord &Hit);
	void HandleRandomGrenadeEmoteOnHit(CCharacter *pLocalChar, int DummyIndex);
	void HandleConfirmedHammerHit(const SQmHammerHitRecord &Hit);
	bool CanRunRuntimeConfigConchainEffects() const;

	int m_PredictedTick;
	int m_aLastNewPredictedTick[NUM_DUMMIES];
	int m_aLastPredictedAirJumpTick[NUM_DUMMIES];
	int m_aLastHammerSkinSwapHitTick[NUM_DUMMIES];
	int m_aLastRandomEmoteHammerHitTick[NUM_DUMMIES];
	int m_aLastRandomEmoteDamageTick[NUM_DUMMIES];

	int m_LastRoundStartTick;
	int m_LastRaceTick;

	int m_LastFlagCarrierRed;
	int m_LastFlagCarrierBlue;

	int m_aCheckInfo[NUM_DUMMIES];

	char m_aDDNetVersionStr[64];
	static void ConTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConKill(IConsole::IResult *pResult, void *pUserData);
	static void ConReadyChange7(IConsole::IResult *pResult, void *pUserData);

	static void ConchainLanguageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSpecialDummyInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRefreshSkins(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRefreshEventSkins(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSpecialDummy(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneZone(IConsole::IResult *pResult, void *pUserData);
	static void ConMapbug(IConsole::IResult *pResult, void *pUserData);

	static void ConchainMenuMap(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	// only used in OnPredict
	vec2 m_aLastPos[MAX_CLIENTS];
	bool m_aLastActive[MAX_CLIENTS];

	// only used in OnNewSnapshot
	bool m_GameOver = false;
	bool m_GamePaused = false;
	SDemoHudPlaybackState m_DemoHudPlaybackState;
	SDemoInputPlaybackState m_DemoInputPlaybackState;
	int m_LastDemoHudRecordTick = -1;
	int m_LastDemoInputRecordTick = -1;
	int m_LastDemoGamepadRecordTick = -1;
	int m_LastDemoPlaybackStateTick = -1;

	void PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage);
	bool ShouldUseServerControlledLocalSkin() const;

public:
	// 将 IInterface 的 protected Kernel() 暴露给客户端组件的既有访问模式。
	// NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
	IKernel *Kernel() { return IInterface::Kernel(); }
	IEngine *Engine() const { return m_pEngine; }
	class IFrameScheduler *FrameScheduler() const;
	class IGraphics *Graphics() const { return m_pGraphics; }
	class IClient *Client() const { return m_pClient; }
	class CUi *Ui() { return &m_UI; }
	class CUiRuntimeV2 *UiRuntimeV2() { return &m_UiRuntimeV2; }
	const class CUiRuntimeV2 *UiRuntimeV2() const { return &m_UiRuntimeV2; }
	class CQmIconManager *QmIconManager() { return &m_QmIconManager; }
	const class CQmIconManager *QmIconManager() const { return &m_QmIconManager; }
	void SyncQmUiIconWeight();
	class ISound *Sound() const { return m_pSound; }
	class IInput *Input() const { return m_pInput; }
	class IStorage *Storage() const { return m_pStorage; }
	class IConfigManager *ConfigManager() const { return m_pConfigManager; }
	class CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class ITextRender *TextRender() const { return m_pTextRender; }
	class IDemoPlayer *DemoPlayer() const { return m_pDemoPlayer; }
	class IDemoRecorder *DemoRecorder(int Recorder) const { return Client()->DemoRecorder(Recorder); }
	class IFavorites *Favorites() const { return m_pFavorites; }
	class IServerBrowser *ServerBrowser() const { return m_pServerBrowser; }
	class CRenderTools *RenderTools() { return &m_RenderTools; }
	class CRenderMap *RenderMap() { return &m_RenderMap; }
	class CLayers *Layers() { return &m_Layers; }
	class IEngineMap *Map() { return Kernel()->RequestInterface<IEngineMap>(); }
	CCollision *Collision() { return &m_Collision; }
	const CCollision *Collision() const { return &m_Collision; }
	const CRaceHelper *RaceHelper() const { return &m_RaceHelper; }
	class IEditor *Editor() { return m_pEditor; }
	class IFriends *Friends() { return m_pFriends; }
	class IFriends *Foes() { return m_pFoes; }
	float GlobalTimeOrZero() const { return m_pClient != nullptr ? m_pClient->GlobalTime() : 0.0f; }
	uint64_t PerfFrameOrOne() const { return m_pClient != nullptr && m_pClient->PerfFrame() > 0 ? m_pClient->PerfFrame() : 1; }
	bool ClientStateOnline() const;
	bool ClientStateAtLeastOnline() const;
	void StopRaceRecordIfRecording() const;
	void UpdateAndSwapClient() const
	{
		if(m_pClient != nullptr)
			m_pClient->UpdateAndSwap();
	}
	CQmJelly *JellyTee() const { return m_pJellyTee.get(); }
#if defined(CONF_AUTOUPDATE)
	class IUpdater *Updater()
	{
		return m_pUpdater;
	}
#endif
	class IHttp *Http()
	{
		return m_pHttp;
	}
	CTClient &TClientComponent() { return m_TClient; }
	const CTClient &TClientComponent() const { return m_TClient; }
	bool HasFreezeWakeupPopups() const { return m_TClient.HasFreezeWakeupPopups(); }
	void RenderFreezeWakeupPopups() { m_TClient.RenderFreezeWakeupPopups(); }

	/**
	 * Get the global GPU upload limiter for texture upload throttling.
	 */
	CGpuUploadLimiter *GpuUploadLimiter() { return &m_GpuUploadLimiter; }
	const CGpuUploadLimiter *GpuUploadLimiter() const { return &m_GpuUploadLimiter; }

	int NetobjNumCorrections()
	{
		return m_NetObjHandler.NumObjCorrections();
	}
	const char *NetobjCorrectedOn() { return m_NetObjHandler.CorrectedObjOn(); }

	bool m_SuppressEvents;
	bool m_RenderingDummyMiniMap = false;
	bool m_NewTick;
	bool m_NewPredictedTick;
	bool m_aConfirmedHammerHitEvent[NUM_DUMMIES];
	int m_aFlagDropTick[2];

	enum
	{
		SERVERMODE_PURE = 0,
		SERVERMODE_MOD,
		SERVERMODE_PUREMOD,
	};
	int m_ServerMode;
	CGameInfo m_GameInfo;

	int m_DemoSpecId;

	vec2 m_LocalCharacterPos;

	// predicted players
	CCharacterCore m_PredictedPrevChar;
	CCharacterCore m_PredictedChar;

	// snap pointers
	class CSnapState
	{
	public:
		const CNetObj_Character *m_pLocalCharacter;
		const CNetObj_Character *m_pLocalPrevCharacter;
		const CNetObj_PlayerInfo *m_pLocalInfo;
		const CNetObj_SpectatorInfo *m_pSpectatorInfo;
		const CNetObj_SpectatorInfo *m_pPrevSpectatorInfo;
		const CNetObj_SpectatorCount *m_pSpectatorCount;
		int m_NumFlags;
		const CNetObj_Flag *m_apFlags[CSnapshot::MAX_ITEMS];
		const CNetObj_Flag *m_apPrevFlags[CSnapshot::MAX_ITEMS];
		const CNetObj_GameInfo *m_pGameInfoObj;
		const CNetObj_GameData *m_pGameDataObj;
		const CNetObj_GameData *m_pPrevGameDataObj;

		const CNetObj_PlayerInfo *m_apPlayerInfos[MAX_CLIENTS];
		const CNetObj_PlayerInfo *m_apPrevPlayerInfos[MAX_CLIENTS];

		const CNetObj_PlayerInfo *m_apInfoByScore[MAX_CLIENTS];
		const CNetObj_PlayerInfo *m_apInfoByName[MAX_CLIENTS];
		const CNetObj_PlayerInfo *m_apInfoByDDTeamScore[MAX_CLIENTS];
		const CNetObj_PlayerInfo *m_apInfoByDDTeamName[MAX_CLIENTS];

		int m_LocalClientId;
		int m_NumPlayers;
		int m_aTeamSize[2];
		int m_HighestClientId;

		class CSpectateInfo
		{
		public:
			bool m_Active;
			int m_SpectatorId;
			bool m_UsePosition;
			vec2 m_Position;

			bool m_HasCameraInfo;
			float m_Zoom;
			int m_Deadzone;
			int m_FollowFactor;
		};
		CSpectateInfo m_SpecInfo;

		class CCharacterInfo
		{
		public:
			bool m_Active;

			// snapshots
			CNetObj_Character m_Prev;
			CNetObj_Character m_Cur;

			CNetObj_DDNetCharacter m_ExtendedData;
			const CNetObj_DDNetCharacter *m_pPrevExtendedData;
			bool m_HasExtendedData;
			bool m_HasExtendedDisplayInfo;
		};
		CCharacterInfo m_aCharacters[MAX_CLIENTS];
	};

	CSnapState m_Snap;
	int m_aLocalTuneZone[NUM_DUMMIES]; // current tunezone (0-255)
	bool m_aReceivedTuning[NUM_DUMMIES]; // was tuning message received after zone change
	int m_aExpectingTuningForZone[NUM_DUMMIES]; // tunezone changed, waiting for tuning for that zone
	int m_aExpectingTuningSince[NUM_DUMMIES]; // how many snaps received since tunezone changed
	CTuningParams m_aTuning[NUM_DUMMIES]; // current local player tuning, only what the player/dummy has

	std::bitset<RECORDER_MAX> m_ActiveRecordings;

	// spectate cursor data
	class CCursorInfo
	{
		friend class CGameClient;
		static constexpr int CURSOR_SAMPLES = 8; // how many samples to keep
		static constexpr int SAMPLE_FRAME_WINDOW = 3; // how many samples should be used for polynomial interpolation
		static constexpr int SAMPLE_FRAME_OFFSET = 2; // how many samples in the past should be included
		static constexpr double INTERP_DELAY = 4.25; // how many ticks in the past to show, enables extrapolation with smaller value (<= SAMPLE_FRAME_WINDOW - SAMPLE_FRAME_OFFSET + 3)
		static constexpr double REST_THRESHOLD = 3.0; // how many ticks of the same samples are considered to be resting

		int m_CursorOwnerId;
		double m_aTargetSamplesTime[CURSOR_SAMPLES];
		vec2 m_aTargetSamplesData[CURSOR_SAMPLES];
		int m_NumSamples;

		bool m_Available;
		int m_Weapon;
		vec2 m_Target;
		vec2 m_WorldTarget;
		vec2 m_Position;

	public:
		bool IsAvailable() const { return m_Available; }
		int Weapon() const { return m_Weapon; }
		vec2 Target() const { return m_Target; }
		vec2 WorldTarget() const { return m_WorldTarget; }
		vec2 Position() const { return m_Position; }
	} m_CursorInfo;

	// client data
	class CClientData
	{
		friend class CGameClient;
		CGameClient *m_pGameClient;
		int m_ClientId;
		int LocalSkinConfigIndex() const;
		void BuildLocalSkinDescriptor(CSkinDescriptor &SkinDescriptor, int Dummy) const;

	public:
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		char m_aSkinName[MAX_SKIN_LENGTH];
		int m_Team;
		int m_Emoticon;
		float m_EmoticonStartFraction;
		int m_EmoticonStartTick;

		bool m_Solo;
		bool m_Jetpack;
		bool m_CollisionDisabled;
		bool m_EndlessHook;
		bool m_EndlessJump;
		bool m_HammerHitDisabled;
		bool m_GrenadeHitDisabled;
		bool m_LaserHitDisabled;
		bool m_ShotgunHitDisabled;
		bool m_HookHitDisabled;
		bool m_Super;
		bool m_Invincible;
		bool m_HasTelegunGun;
		bool m_HasTelegunGrenade;
		bool m_HasTelegunLaser;
		int m_FreezeEnd;
		bool m_DeepFrozen;
		bool m_LiveFrozen;
		bool m_IsInFreeze;
		SHudFrozenTeeState m_HudFrozenTeeState;

		CCharacterCore m_Predicted;
		CCharacterCore m_PrevPredicted;

		// TClient
		CCharacterCore m_RegularPredicted;

		// TClient
		vec2 m_ImprovedPredPos = vec2(0, 0);
		vec2 m_PrevImprovedPredPos = vec2(0, 0);
		bool m_ValidAntipingSmooth = false;
		//vec2 m_DebugVector = vec2(0, 0);
		//vec2 m_DebugVector2 = vec2(0, 0);
		//vec2 m_DebugVector3 = vec2(0, 0);
		float m_Uncertainty = 0.0f;
		float m_VolleyBallAngle = 0.0f;
		bool m_IsVolleyBall = false;

		// Chat bubble above player's head
		char m_aChatBubbleText[256] = "";
		int64_t m_ChatBubbleStartTick = 0;
		int64_t m_ChatBubbleExpireTick = 0;

		std::shared_ptr<CManagedTeeRenderInfo> m_pSkinInfo = nullptr; // this is what the server reports
		CTeeRenderInfo m_RenderInfo; // this is what we use
		CSkinDescriptor m_RenderInfoSkinDescriptor;
		CSkinDescriptor m_RenderInfoFallbackResidencyDescriptor;
		uint64_t m_RenderInfoSkinGeneration = 0;
		bool m_RenderInfoFallbackResidencyRequested = false;
		CTeeRenderInfo m_SkinTransitionPreviousRenderInfo;

		class CSkinTransitionKey
		{
		public:
			CSkinDescriptor m_SkinDescriptor;
			uint64_t m_SkinGeneration = 0;
			int m_UseCustomColor = 0;
			int m_ColorBody = 0;
			int m_ColorFeet = 0;
			int m_aaSixupUseCustomColors[NUM_DUMMIES][protocol7::NUM_SKINPARTS] = {};
			int m_aaSixupSkinPartColors[NUM_DUMMIES][protocol7::NUM_SKINPARTS] = {};

			bool operator==(const CSkinTransitionKey &Other) const
			{
				// 颜色变化只更新渲染颜色，不应触发皮肤切换动画。
				return m_SkinDescriptor == Other.m_SkinDescriptor && m_SkinGeneration == Other.m_SkinGeneration;
			}
		} m_LastSkinTransitionKey;
		bool m_HasSkinTransitionKey = false;
		std::optional<std::chrono::nanoseconds> m_SkinTransitionStart;

		float m_Angle;
		bool m_Active;
		bool m_ChatIgnore;
		bool m_EmoticonIgnore;
		bool m_Friend;
		bool m_Foe;

		int m_AuthLevel;
		bool m_Afk;
		bool m_Paused;
		bool m_Spec;
		int m_FinishTimeSeconds;
		int m_FinishTimeMillis;

		// Editor allows 256 switches for now.
		bool m_aSwitchStates[256];

		CNetObj_Character m_Snapped;
		CNetObj_Character m_Evolved;

		CNetMsg_Sv_PreInput m_aPreInputs[200];

		// rendered characters
		CNetObj_Character m_RenderCur;
		CNetObj_Character m_RenderPrev;
		vec2 m_RenderPos;
		bool m_IsPredicted;
		bool m_IsPredictedLocal;
		int64_t m_aSmoothStart[2];
		int64_t m_aSmoothLen[2];
		vec2 m_aPredPos[200];
		int m_aPredTick[200];
		bool m_SpecCharPresent;
		vec2 m_SpecChar;

		void UpdateSkinInfo();
		void UpdateSkin7HatSprite(int Dummy);
		void UpdateSkin7BotDecoration(int Dummy);
		void UpdateRenderInfo();
		void UpdateSkinChangeTransition(const CTeeRenderInfo &NewRenderInfo, const CSkinDescriptor &SkinDescriptor);
		float SkinChangeTransitionProgress(std::chrono::nanoseconds Now) const;
		const CTeeRenderInfo *SkinChangePreviousRenderInfo(std::chrono::nanoseconds Now) const;
		void Reset();
		CSkinDescriptor ToSkinDescriptor() const;

		int ClientId() const { return m_ClientId; }

		class CSixup
		{
		public:
			void Reset();

			char m_aaSkinPartNames[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH];
			int m_aUseCustomColors[protocol7::NUM_SKINPARTS];
			int m_aSkinPartColors[protocol7::NUM_SKINPARTS];
		};

		// 0.7 Skin
		CSixup m_aSixup[NUM_DUMMIES];
	};

	CClientData m_aClients[MAX_CLIENTS];

	class CClientStats
	{
		int m_IngameTicks;
		int m_JoinTick;
		bool m_Active;

	public:
		CClientStats();

		int m_aFragsWith[NUM_WEAPONS];
		int m_aDeathsFrom[NUM_WEAPONS];
		int m_Frags;
		int m_Deaths;
		int m_Suicides;
		int m_BestSpree;
		int m_CurrentSpree;

		int m_FlagGrabs;
		int m_FlagCaptures;

		void Reset();

		bool IsActive() const { return m_Active; }
		void JoinGame(int Tick)
		{
			m_Active = true;
			m_JoinTick = Tick;
		}
		void JoinSpec(int Tick)
		{
			m_Active = false;
			m_IngameTicks += Tick - m_JoinTick;
		}
		int GetIngameTicks(int Tick) const { return m_IngameTicks + Tick - m_JoinTick; }
		float GetFPM(int Tick, int TickSpeed) const { return (float)(m_Frags * TickSpeed * 60) / GetIngameTicks(Tick); }
	};

	CClientStats m_aStats[MAX_CLIENTS];

	CRenderTools m_RenderTools;
	CRenderMap m_RenderMap;

	bool m_BackButtonHandledKeyBind = false;

	void OnReset();

	size_t ComponentCount() const { return m_vpAll.size(); }

	// hooks
	void OnConnected() override;
	void OnRender() override;
	void OnUpdate() override;
	int RenderThrottleRefreshRate() const override;
	void OnScreenshotTaken(class CImageInfo &&Image) override;
	void OnDummyDisconnect() override;
	void OnDummyManualDisconnect() override;
	virtual void OnRelease();
	void OnInit() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	template<typename T>
	void ApplySkin7InfoFromGameMsg(const T *pMsg, int ClientId, int Conn);
	void ApplySkin7InfoFromSnapObj(const protocol7::CNetObj_De_ClientInfo *pObj, int ClientId) override;
	int OnDemoRecSnap7(CSnapshot *pFrom, CSnapshotBuffer *pTo, int Conn) override;
	void *TranslateGameMsg(int *pMsgId, CUnpacker *pUnpacker, int Conn);
	int TranslateSnap(CSnapshotBuffer *pSnapDstSix, CSnapshot *pSnapSrcSeven, int Conn, bool Dummy) override;
	void OnMessage(int MsgId, CUnpacker *pUnpacker, int Conn, bool Dummy) override;
	void OnClientBrandsMessage(CUnpacker *pUnpacker) override;
	bool OnDemoPlaybackMessage(int MsgId, CUnpacker *pUnpacker) override;
	void ResetDemoPlaybackState() override;
	void InvalidateSnapshot() override;
	void OnNewSnapshot() override;
	void OnPredict() override;
	void OnActivateEditor() override;
	void OnDummySwap() override;
	int OnSnapInput(int *pData, bool Dummy, bool Force) override;
	void PrepareInputForSend(int *pData, int Size, bool Dummy) override;
	void OnShutdown() override;
	void OnEnterGame() override;
	void OnRconType(bool UsernameReq) override;
	void OnRconLine(const char *pLine) override;
	virtual void OnGameOver();
	virtual void OnStartGame();
	virtual void OnStartRound();
	virtual void OnFlagGrab(int TeamId);
	void OnWindowResize() override;
	// 图形设备重建（例如 Vulkan 设备丢失后重建）后由 IGraphics 广播触发：
	// 丢弃并重新加载所有 GPU 资源。
	void OnGraphicsResourcesReset();
	// 按 g_pData 图片表加载全部初始资源；启动与资源重置后重建共用这一条路径。
	void LoadInitialGraphicsAssets();

	void InitializeLanguage() override;
	bool m_LanguageChanged = false;
	void OnLanguageChange();
	void HandleLanguageChanged();

	void ForceUpdateConsoleRemoteCompletionSuggestions() override;
	void RenderQmMonitoringHud(float GraphX, float GraphSpacing) override;

	void RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo);
	void RefreshSkins(int SkinDescriptorFlags);
	void OnSkinUpdate(const char *pSkinName);
	std::shared_ptr<CManagedTeeRenderInfo> CreateManagedTeeRenderInfo(const CTeeRenderInfo &TeeRenderInfo, const CSkinDescriptor &SkinDescriptor);
	std::shared_ptr<CManagedTeeRenderInfo> CreateManagedTeeRenderInfo(const CClientData &Client);
	void CollectManagedTeeRenderInfos(const std::function<void(const char *pSkinName)> &ActiveSkinAcceptor);

	bool PrepareForShutdown(bool Force) override;
	void RenderShutdownMessage() override;
	void ProcessDemoSnapshot(CSnapshot *pSnap) override;

	const char *GetItemName(int Type) const override;
	const char *Version() const override;
	const char *NetVersion() const override;
	const char *NetVersion7() const override;
	int DDNetVersion() const override;
	const char *DDNetVersionStr() const override;
	int ClientVersion7() const override;
	const SDemoHudPlaybackState *DemoHudPlaybackState() const { return m_DemoHudPlaybackState.m_Valid ? &m_DemoHudPlaybackState : nullptr; }
	const SDemoInputPlaybackState *DemoInputPlaybackState() const { return m_DemoInputPlaybackState.m_Valid ? &m_DemoInputPlaybackState : nullptr; }
	void DoTeamChangeMessage7(const char *pName, int ClientId, int Team, const char *pPrefix = "");

	// actions
	// TODO: move these
	void SendSwitchTeam(int Team);
	void SendStartInfo7(bool Dummy);
	void SendSkinChange7(bool Dummy);
	// Returns true if the requested skin change got applied by the server
	bool GotWantedSkin7(bool Dummy);
	void SendInfo(bool Start);
	void SendDummyInfo(bool Start) override;
	void UpdateLocalSkinInfo(int Dummy);
	void SendKill();
	void SendKill() const;
	void SendReadyChange7();

	void ApplyPreInputs(int Tick, bool Direct, CGameWorld &GameWorld);
	bool GetDummyFastInput(CNetObj_PlayerInput &DummyFastInput, const CNetObj_PlayerInput *pDummyInputData, const class CCharacter *pDummyChar, int LocalTee, int DummyTee) const;

	int m_aNextChangeInfo[NUM_DUMMIES];

	// DDRace

	int m_aLocalIds[NUM_DUMMIES];
	CNetObj_PlayerInput m_DummyInput;
	CNetObj_PlayerInput m_HammerInput;
	unsigned int m_DummyFire;
	int m_DummyHammerResends;
	bool m_ReceivedDDNetPlayer;
	bool m_ReceivedDDNetPlayerFinishTimes;
	bool m_ReceivedDDNetPlayerFinishTimesMillis;

	CTeamsCore m_Teams;

	int IntersectCharacter(vec2 HookPos, vec2 NewPos, vec2 &NewPos2, int OwnId, vec2 *pPlayerPosition = nullptr);

	int LastRaceTick() const;
	int CurrentRaceTime() const;

	bool IsTeamPlay() const;
	bool IsWorldPaused() const;
	bool IsDemoPlaybackPaused() const;
	float GetAnimationPlaybackSpeed() const;

	bool AntiPingPlayers() const { return m_FastPractice.ForcePredictPlayers() || (g_Config.m_ClAntiPing && g_Config.m_ClAntiPingPlayers && !m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK); }
	bool AntiPingGrenade() const { return m_FastPractice.ForcePredictGrenade() || (g_Config.m_ClAntiPing && g_Config.m_ClAntiPingGrenade && !m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK); }
	bool AntiPingWeapons() const { return m_FastPractice.ForcePredictWeapons() || (g_Config.m_ClAntiPing && g_Config.m_ClAntiPingWeapons && !m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK); }
	bool AntiPingGunfire() const { return m_FastPractice.ForcePredictGunfire() || (AntiPingGrenade() && AntiPingWeapons() && g_Config.m_ClAntiPingGunfire); }
	bool Predict() const;
	bool PredictDummy() const
	{
		if(m_FastPractice.Enabled())
		{
			const int FastPracticeDummyId = m_FastPractice.CurrentPracticeDummyId();
			return FastPracticeDummyId >= 0 && m_Snap.m_LocalClientId >= 0 && !m_aClients[FastPracticeDummyId].m_Paused;
		}
		return g_Config.m_ClPredictDummy && Client()->DummyConnected() && m_Snap.m_LocalClientId >= 0 && m_aLocalIds[!g_Config.m_ClDummy] >= 0 && !m_aClients[m_aLocalIds[!g_Config.m_ClDummy]].m_Paused;
	}
	bool IsRenderingDummyMiniMap() const { return m_RenderingDummyMiniMap; }
	void SetRenderingDummyMiniMap(bool Rendering) { m_RenderingDummyMiniMap = Rendering; }
	const CTuningParams *GetTuning(int i) const { return &m_aTuningList[i]; }
	bool GetPotentialHammerHitArea(CCharacter *pChar, vec2 &HitPos, float &HitRadius);
	int FindPotentialHammerHitTargets(CCharacter *pChar, vec2 HitPos, float HitRadius, int *pTargetIds, int MaxTargetIds);
	int HammerHitConnectionFilter() const;
	const CQmHammerHitTracker &HammerHitTracker() const { return m_HammerHitTracker; }
	ColorRGBA GetDDTeamColor(int DDTeam, float Lightness = 0.5f) const;
	void FormatClientId(int ClientId, char (&aClientId)[16], EClientIdFormat Format) const;
	bool IsLocalClientId(int ClientId) const;
	bool ShouldRunSkinChangeTransition(int ClientId) const;
	bool ShouldHideStreamerIdentity(int ClientId) const;
	bool ShouldHideStreamerSkin(int ClientId) const;
	void FormatStreamerName(int ClientId, char *pBuf, int BufSize) const;
	void FormatStreamerClan(int ClientId, char *pBuf, int BufSize) const;
	void FormatStreamerVoteText(const char *pText, char *pBuf, int BufSize) const;

	CGameWorld m_GameWorld;
	CGameWorld m_PredictedWorld;
	CGameWorld m_PrevPredictedWorld;

	// TClient
	CGameWorld m_RegularPredictedWorld;
	CGameWorld m_PrevRegularPredictedWorld;

	// TClient
	CGameWorld m_ExtraPredictedWorld;
	CGameWorld m_PredSmoothingWorld;

	std::vector<SSwitchers> &Switchers() { return m_GameWorld.m_Core.m_vSwitchers; }
	std::vector<SSwitchers> &PredSwitchers() { return m_PredictedWorld.m_Core.m_vSwitchers; }

	void DummyResetInput() override;
	void Echo(const char *pString) override;
	void Echo(const char *pString, bool ForceVisible);
	bool IsOtherTeam(int ClientId) const;
	int SwitchStateTeam() const;
	bool IsLocalCharSuper() const;
	bool CanDisplayWarning() const override;
	CNetObjHandler *GetNetObjHandler() override;
	protocol7::CNetObjHandler *GetNetObjHandler7() override;

	void LoadGameSkin(const char *pPath, bool AsDir = false);
	void LoadEmoticonsSkin(const char *pPath, bool AsDir = false);
	void LoadParticlesSkin(const char *pPath, bool AsDir = false);
	void LoadHudSkin(const char *pPath, bool AsDir = false);
	void LoadExtrasSkin(const char *pPath, bool AsDir = false);
	void ReloadNamedSingleFileAssetImage(int ImageId, const char *pCategoryId, const char *pActiveName);

	struct SClientGameSkin
	{
		// health armor hud
		IGraphics::CTextureHandle m_SpriteHealthFull;
		IGraphics::CTextureHandle m_SpriteHealthEmpty;
		IGraphics::CTextureHandle m_SpriteArmorFull;
		IGraphics::CTextureHandle m_SpriteArmorEmpty;

		// cursors
		IGraphics::CTextureHandle m_SpriteWeaponHammerCursor;
		IGraphics::CTextureHandle m_SpriteWeaponGunCursor;
		IGraphics::CTextureHandle m_SpriteWeaponShotgunCursor;
		IGraphics::CTextureHandle m_SpriteWeaponGrenadeCursor;
		IGraphics::CTextureHandle m_SpriteWeaponNinjaCursor;
		IGraphics::CTextureHandle m_SpriteWeaponLaserCursor;

		IGraphics::CTextureHandle m_aSpriteWeaponCursors[6];

		// weapons and hook
		IGraphics::CTextureHandle m_SpriteHookChain;
		IGraphics::CTextureHandle m_SpriteHookHead;
		IGraphics::CTextureHandle m_SpriteWeaponHammer;
		IGraphics::CTextureHandle m_SpriteWeaponGun;
		IGraphics::CTextureHandle m_SpriteWeaponShotgun;
		IGraphics::CTextureHandle m_SpriteWeaponGrenade;
		IGraphics::CTextureHandle m_SpriteWeaponNinja;
		IGraphics::CTextureHandle m_SpriteWeaponLaser;

		IGraphics::CTextureHandle m_aSpriteWeapons[6];

		// particles
		IGraphics::CTextureHandle m_aSpriteParticles[9];

		// stars
		IGraphics::CTextureHandle m_aSpriteStars[3];

		// projectiles
		IGraphics::CTextureHandle m_SpriteWeaponGunProjectile;
		IGraphics::CTextureHandle m_SpriteWeaponShotgunProjectile;
		IGraphics::CTextureHandle m_SpriteWeaponGrenadeProjectile;
		IGraphics::CTextureHandle m_SpriteWeaponHammerProjectile;
		IGraphics::CTextureHandle m_SpriteWeaponNinjaProjectile;
		IGraphics::CTextureHandle m_SpriteWeaponLaserProjectile;

		IGraphics::CTextureHandle m_aSpriteWeaponProjectiles[6];

		// muzzles
		IGraphics::CTextureHandle m_aSpriteWeaponGunMuzzles[3];
		IGraphics::CTextureHandle m_aSpriteWeaponShotgunMuzzles[3];
		IGraphics::CTextureHandle m_aaSpriteWeaponNinjaMuzzles[3];

		IGraphics::CTextureHandle m_aaSpriteWeaponsMuzzles[6][3];

		// pickups
		IGraphics::CTextureHandle m_SpritePickupHealth;
		IGraphics::CTextureHandle m_SpritePickupArmor;
		IGraphics::CTextureHandle m_SpritePickupArmorShotgun;
		IGraphics::CTextureHandle m_SpritePickupArmorGrenade;
		IGraphics::CTextureHandle m_SpritePickupArmorNinja;
		IGraphics::CTextureHandle m_SpritePickupArmorLaser;
		IGraphics::CTextureHandle m_SpritePickupGrenade;
		IGraphics::CTextureHandle m_SpritePickupShotgun;
		IGraphics::CTextureHandle m_SpritePickupLaser;
		IGraphics::CTextureHandle m_SpritePickupNinja;
		IGraphics::CTextureHandle m_SpritePickupGun;
		IGraphics::CTextureHandle m_SpritePickupHammer;

		IGraphics::CTextureHandle m_aSpritePickupWeapons[6];
		IGraphics::CTextureHandle m_aSpritePickupWeaponArmor[4];

		// flags
		IGraphics::CTextureHandle m_SpriteFlagBlue;
		IGraphics::CTextureHandle m_SpriteFlagRed;

		// ninja bar (0.7)
		IGraphics::CTextureHandle m_SpriteNinjaBarFullLeft;
		IGraphics::CTextureHandle m_SpriteNinjaBarFull;
		IGraphics::CTextureHandle m_SpriteNinjaBarEmpty;
		IGraphics::CTextureHandle m_SpriteNinjaBarEmptyRight;

		bool IsSixup() const
		{
			return m_SpriteNinjaBarFullLeft.IsValid();
		}
	};

	SClientGameSkin m_GameSkin;
	bool m_GameSkinLoaded = false;

	struct SClientParticlesSkin
	{
		IGraphics::CTextureHandle m_SpriteParticleSlice;
		IGraphics::CTextureHandle m_SpriteParticleBall;
		IGraphics::CTextureHandle m_aSpriteParticleSplat[3];
		IGraphics::CTextureHandle m_SpriteParticleSmoke;
		IGraphics::CTextureHandle m_SpriteParticleShell;
		IGraphics::CTextureHandle m_SpriteParticleExpl;
		IGraphics::CTextureHandle m_SpriteParticleAirJump;
		IGraphics::CTextureHandle m_SpriteParticleHit;
		IGraphics::CTextureHandle m_aSpriteParticles[10];
	};

	SClientParticlesSkin m_ParticlesSkin;
	bool m_ParticlesSkinLoaded = false;
	int m_SpawnEventsProcessed = 0;
	int m_SpawnEffectsDispatched = 0;
	int m_SpawnEffectsFiltered = 0;
	int m_SpawnParticleAddFailures = 0;

	struct SClientEmoticonsSkin
	{
		IGraphics::CTextureHandle m_aSpriteEmoticons[16];
	};

	SClientEmoticonsSkin m_EmoticonsSkin;
	bool m_EmoticonsSkinLoaded = false;

	struct SClientHudSkin
	{
		IGraphics::CTextureHandle m_SpriteHudAirjump;
		IGraphics::CTextureHandle m_SpriteHudAirjumpEmpty;
		IGraphics::CTextureHandle m_SpriteHudSolo;
		IGraphics::CTextureHandle m_SpriteHudCollisionDisabled;
		IGraphics::CTextureHandle m_SpriteHudEndlessJump;
		IGraphics::CTextureHandle m_SpriteHudEndlessHook;
		IGraphics::CTextureHandle m_SpriteHudJetpack;
		IGraphics::CTextureHandle m_SpriteHudFreezeBarFullLeft;
		IGraphics::CTextureHandle m_SpriteHudFreezeBarFull;
		IGraphics::CTextureHandle m_SpriteHudFreezeBarEmpty;
		IGraphics::CTextureHandle m_SpriteHudFreezeBarEmptyRight;
		IGraphics::CTextureHandle m_SpriteHudNinjaBarFullLeft;
		IGraphics::CTextureHandle m_SpriteHudNinjaBarFull;
		IGraphics::CTextureHandle m_SpriteHudNinjaBarEmpty;
		IGraphics::CTextureHandle m_SpriteHudNinjaBarEmptyRight;
		IGraphics::CTextureHandle m_SpriteHudHookHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudHammerHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudShotgunHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudGrenadeHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudLaserHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudGunHitDisabled;
		IGraphics::CTextureHandle m_SpriteHudDeepFrozen;
		IGraphics::CTextureHandle m_SpriteHudLiveFrozen;
		IGraphics::CTextureHandle m_SpriteHudTeleportGrenade;
		IGraphics::CTextureHandle m_SpriteHudTeleportGun;
		IGraphics::CTextureHandle m_SpriteHudTeleportLaser;
		IGraphics::CTextureHandle m_SpriteHudPracticeMode;
		IGraphics::CTextureHandle m_SpriteHudLockMode;
		IGraphics::CTextureHandle m_SpriteHudTeam0Mode;
		IGraphics::CTextureHandle m_SpriteHudDummyHammer;
		IGraphics::CTextureHandle m_SpriteHudDummyCopy;
	};

	SClientHudSkin m_HudSkin;
	bool m_HudSkinLoaded = false;

	struct SClientExtrasSkin
	{
		IGraphics::CTextureHandle m_SpriteParticleSnowflake;
		IGraphics::CTextureHandle m_SpriteParticleSparkle;
		IGraphics::CTextureHandle m_SpritePulley;
		IGraphics::CTextureHandle m_SpriteHectagon;
		IGraphics::CTextureHandle m_aSpriteParticles[4];
	};

	SClientExtrasSkin m_ExtrasSkin;
	bool m_ExtrasSkinLoaded = false;

	const std::vector<CSnapEntities> &SnapEntities() { return m_vSnapEntities; }

	vec2 GetSmoothPos(int ClientId);
	vec2 GetFreezePos(int ClientId);
	vec2 GetFastInputPos(int ClientId);
	int m_MultiViewTeam;
	float m_MultiViewPersonalZoom;
	bool m_MultiViewShowHud;
	bool m_MultiViewActivated;
	bool m_aMultiViewId[MAX_CLIENTS];

	void ResetMultiView();
	int FindFirstMultiViewId();
	void CleanMultiViewId(int ClientId);
	int m_MapBestTimeSeconds;
	int m_MapBestTimeMillis;
	char m_aMapDescription[512];

	// Q1menG Client Recognition
	void ClearQ1menGSyncMarks();
	void MarkQ1menGSyncClient(int ClientId, int64_t ExpireTick, bool FootParticlesEnabled, bool RemoteParticlesEnabled, const char *pQid = nullptr, EClientBrand ClientBrand = EClientBrand::QM);
	bool IsQ1menGClientRecognized(int ClientId) const;
	const char *GetQ1menGClientQid(int ClientId) const;
	bool ShouldRenderQ1menGRemoteFootParticles(int ClientId) const;
	void ClearQmVoiceSyncMarks();
	void MarkQmVoiceSupportedClient(int ClientId, int64_t ExpireTick);
	bool IsQmVoiceSupportedClient(int ClientId) const;
	void ClearQmDeveloperMarks();
	void MarkQmDeveloperClient(int ClientId, const char *pPlayerName, int64_t ExpireTick, bool Rainbow);
	bool IsQmDeveloperAuthenticated(int ClientId) const;
	bool IsQmDeveloperRainbow(int ClientId) const;
	void ClearClientBrands();
	EClientBrand ClientBrand(const char *pName) const;

private:
	std::vector<CSnapEntities> m_vSnapEntities;
	void SnapCollectEntities();
	int GetFastInputPredictionAmountMs();
	int GetFastInputPredictionTicks();
	int GetFastInputRenderAmountMs();

	bool m_aDDRaceMsgSent[NUM_DUMMIES];
	int m_aShowOthers[NUM_DUMMIES];
	int m_aEnableSpectatorCount[NUM_DUMMIES]; // current setting as sent to the server, -1 if not yet sent

	std::vector<std::shared_ptr<CManagedTeeRenderInfo>> m_vpManagedTeeRenderInfos;
	void UpdateManagedTeeRenderInfos();

	void UpdateAutoTeamLock();
	void UpdateLocalTuning();
	void UpdatePrediction();
	void UpdateSpectatorCursor();
	void UpdateRenderedCharacters();
	void RefreshStreamerSkinPrivacyAfterStateChange();
	void RefreshPredictionAfterConfigChange();
	void RequestPredictionRefreshAfterConfigChange();
	void HandlePredictedEvents(int Tick);

	void OnInput(const IInput::CEvent &Event);

	int m_aLastUpdateTick[MAX_CLIENTS] = {0};
	void DetectStrongHook();

	int m_PredictedDummyId;

	int m_IsDummySwapping;
	bool m_RequestPredictionRefreshAfterConfigChange = false;
	int m_LastStreamerHideSkins = -1;
	int m_LastStreamerFriendsIgnoreClan = -1;
	int m_aLastStreamerLocalIds[NUM_DUMMIES] = {-2, -2};
	uint64_t m_LastStreamerFriendsRevision = std::numeric_limits<uint64_t>::max();
	CCharOrder m_CharOrder;
	int m_aSwitchStateTeam[NUM_DUMMIES];
	int m_aAutoTeamLockLastTeam[NUM_DUMMIES] = {TEAM_FLOCK, TEAM_FLOCK};
	int64_t m_aAutoTeamLockDeadlineTick[NUM_DUMMIES] = {0, 0};
	bool m_aAutoTeamLockPending[NUM_DUMMIES] = {false, false};
	int64_t m_aQ1menGSyncMarkUntil[MAX_CLIENTS] = {0};
	bool m_aQ1menGSyncFootParticlesEnabled[MAX_CLIENTS] = {false};
	bool m_aQ1menGSyncRemoteParticlesEnabled[MAX_CLIENTS] = {false};
	EClientBrand m_aQ1menGSyncClientBrands[MAX_CLIENTS] = {};
	char m_aaQ1menGSyncQid[MAX_CLIENTS][33] = {{0}};
	int64_t m_aQmVoiceSyncMarkUntil[MAX_CLIENTS] = {0};
	int64_t m_aQmDeveloperMarkUntil[MAX_CLIENTS] = {0};
	bool m_aQmDeveloperRainbow[MAX_CLIENTS] = {false};
	char m_aaQmDeveloperMarkName[MAX_CLIENTS][MAX_NAME_LENGTH] = {{0}};
	char m_aaClientBrandNames[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	EClientBrand m_aClientBrands[MAX_CLIENTS] = {};

	void LoadMapSettings();
	CMapBugs m_MapBugs;

	// tunings for every zone on the map, 0 is a global tune
	CTuningParams m_aTuningList[TuneZone::NUM];
	CTuningParams *TuningList() { return m_aTuningList; }

	float m_LastShowDistanceZoom;
	float m_LastZoom;
	float m_LastScreenAspect;
	float m_LastDeadzone;
	float m_LastFollowFactor;
	bool m_LastDummyConnected;

	void HandleMultiView();
	bool IsMultiViewIdSet();
	void CleanMultiViewIds();
	bool InitMultiView(int Team);
	float CalculateMultiViewMultiplier(vec2 TargetPos);
	float CalculateMultiViewZoom(vec2 MinPos, vec2 MaxPos, float Vel);
	float MapValue(float MaxValue, float MinValue, float MaxRange, float MinRange, float Value);

	struct SMultiView
	{
		bool m_Solo;
		bool m_IsInit;
		bool m_Teleported;
		bool m_aVanish[MAX_CLIENTS];
		vec2 m_OldPos;
		int m_OldPersonalZoom;
		float m_SecondChance;
		float m_OldCameraDistance;
		float m_aLastFreeze[MAX_CLIENTS];
	};

	SMultiView m_MultiView;

	void OnSaveCodeNetMessage(const CNetMsg_Sv_SaveCode *pMsg);
	void StoreSave(const char *pTeamMembers, const char *pGeneratedCode) const;

public:
	// TClient
	int m_SmoothTick = 0;
	float m_SmoothIntraTick = 0;
	bool CheckNewInput() override;
	bool IsFastInputActive() const override;
	void RequestPredictionRefresh() { RequestPredictionRefreshAfterConfigChange(); }
	std::optional<CServerInfo> m_ConnectServerInfo = std::nullopt;
	void SetConnectInfo(const NETADDR *pAddress) override;
};

ColorRGBA CalculateNameColor(ColorHSLA TextColorHSL);

#endif
