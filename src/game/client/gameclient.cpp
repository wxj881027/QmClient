/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

// Test PR: This is a simple comment to verify the PR workflow

#include "gameclient.h"

#include "components/assets_resource_registry.h"
#include "components/background.h"
#include "components/binds.h"
#include "components/broadcast.h"
#include "components/camera.h"
#include "components/chat.h"
#include "components/console.h"
#include "components/controls.h"
#include "components/countryflags.h"
#include "components/damageind.h"
#include "components/debughud.h"
#include "components/effects.h"
#include "components/emoticon.h"
#include "components/freezebars.h"
#include "components/ghost.h"
#include "components/hud.h"
#include "components/infomessages.h"
#include "components/items.h"
#include "components/mapimages.h"
#include "components/maplayers.h"
#include "components/mapsounds.h"
#include "components/menu_background.h"
#include "components/menus.h"
#include "components/motd.h"
#include "components/nameplates.h"
#include "components/particles.h"
#include "components/players.h"
#include "components/qmclient/jelly_tee.h"
#include "components/qmclient/modes.h"
#include "components/qmclient/perf_logging.h"
#include "components/qmclient/qmclient_utils.h"
#include "components/qmclient/translate/translate_ui_settings.h"
#include "components/race_demo.h"
#include "components/scoreboard.h"
#include "components/settings_resource_jobs.h"
#include "components/settings_runtime_cache.h"
#include "components/skins.h"
#include "components/skins7.h"
#include "components/sounds.h"
#include "components/spectator.h"
#include "components/statboard.h"
#include "components/tclient/fast_practice.h"
#include "components/voting.h"
#include "lineinput.h"
#include "prediction/entities/character.h"
#include "prediction/entities/projectile.h"
#include "race.h"
#include "render.h"

#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client/checksum.h>
#include <engine/client/enums.h>
#include <engine/demo.h>
#include <engine/discord.h>
#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/csv.h>
#include <engine/shared/protocol_ex.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/client_data.h>
#include <generated/client_data7.h>

#include <cinttypes>

namespace
{
	constexpr int DUMMY_HAMMER_RESENDS = 2;

	void NormalizeSixupSkinName(char *pSkinName, int SkinNameSize)
	{
		if(!CSkin::IsValidName(pSkinName))
		{
			str_copy(pSkinName, "default", SkinNameSize);
		}
	}

	void LogSettingsLoadingPrewarmEvent(const IClient *pClient, const char *pEvent, int CompletedSteps, int MaxAttempts, int TeeWarmupEntries, int ConsecutiveNoProgressSteps, uint64_t UploadsCompleted, uint64_t LoadsCompleted)
	{
		if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
			return;
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=%s steps=%d max_attempts=%d tee_entries=%d stall_steps=%d uploads_completed=%" PRIu64 " loads_completed=%" PRIu64,
			pEvent != nullptr ? pEvent : "startup_prewarm",
			CompletedSteps,
			MaxAttempts,
			TeeWarmupEntries,
			ConsecutiveNoProgressSteps,
			UploadsCompleted,
			LoadsCompleted);
		QmPerfLogPayload("perf/settings-warmup", aPayload, pClient, "settings:tee");
	}

	void LogQmIconDiagnostics(const SQmIconDiagnostics &Diagnostics, const IClient *pClient)
	{
		if(!QmPerfEnabled())
			return;
		char aPayload[1024];
		str_format(aPayload, sizeof(aPayload), "event=icon_frame alpha_draws=%" PRIu64 " msdf_draws=%" PRIu64 " msdf_manager_call_run_max=%" PRIu64 " msdf_manager_call_run_1=%" PRIu64 " msdf_manager_call_run_2=%" PRIu64 " msdf_manager_call_run_3_4=%" PRIu64 " msdf_manager_call_run_5_8=%" PRIu64 " msdf_manager_call_run_9_16=%" PRIu64 " msdf_manager_call_run_17_32=%" PRIu64 " msdf_manager_call_run_33_64=%" PRIu64 " msdf_manager_call_run_65_plus=%" PRIu64 " reload_attempts=%" PRIu64 " reload_successes=%" PRIu64 " msdf_probe_attempts=%" PRIu64 " msdf_probe_successes=%" PRIu64 " atlas_swaps=%" PRIu64 " texture_load_successes=%" PRIu64 " texture_load_failures=%" PRIu64 " texture_unloads=%" PRIu64,
			Diagnostics.m_AlphaIconDraws,
			Diagnostics.m_MsdfIconDraws,
			Diagnostics.m_MaxMsdfManagerCallRun,
			Diagnostics.m_MsdfManagerCallRunBuckets[0],
			Diagnostics.m_MsdfManagerCallRunBuckets[1],
			Diagnostics.m_MsdfManagerCallRunBuckets[2],
			Diagnostics.m_MsdfManagerCallRunBuckets[3],
			Diagnostics.m_MsdfManagerCallRunBuckets[4],
			Diagnostics.m_MsdfManagerCallRunBuckets[5],
			Diagnostics.m_MsdfManagerCallRunBuckets[6],
			Diagnostics.m_MsdfManagerCallRunBuckets[7],
			Diagnostics.m_ReloadAttempts,
			Diagnostics.m_ReloadSuccesses,
			Diagnostics.m_MsdfProbes,
			Diagnostics.m_MsdfProbeSuccesses,
			Diagnostics.m_AtlasSwaps,
			Diagnostics.m_TextureLoads,
			Diagnostics.m_TextureLoadFailures,
			Diagnostics.m_TextureUnloads);
		QmPerfLogPayload("perf/icons", aPayload, pClient);
	}

} // namespace
#include <generated/protocol.h>
#include <generated/protocol7.h>
#include <generated/protocolglue.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/frame_scheduler.h>
#include <game/client/projectile_data.h>
#include <game/localization.h>
#include <game/mapitems.h>
#include <game/teamscore.h>
#include <game/version.h>

namespace
{
	constexpr int DEMO_INPUT_KEY_STATE_SIZE = KEY_LAST / 8;

	void LogPerfStage(const CGameClient *pGameClient, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/gameclient", pStage, DurationMs, Force, pGameClient != nullptr ? pGameClient->Client() : nullptr, nullptr, nullptr, pExtra);
	}

	bool IsQmStutterFeatureConfigName(const char *pName)
	{
		return str_startswith(pName, "qm_") != nullptr ||
		       str_startswith(pName, "tc_") != nullptr ||
		       str_startswith(pName, "cl_") != nullptr;
	}

	void CollectQmStutterFeatureConfig(const SConfigVariable *pVariable, void *pUserData)
	{
		if(pVariable == nullptr || pVariable->m_Type != SConfigVariable::VAR_INT || !IsQmStutterFeatureConfigName(pVariable->m_pScriptName))
			return;
		const SIntConfigVariable *pIntVariable = static_cast<const SIntConfigVariable *>(pVariable);
		if(pIntVariable->m_Max <= 0 || pIntVariable->m_Min != 0 || *pIntVariable->m_pVariable == 0)
			return;
		auto *pSnapshot = static_cast<std::vector<std::pair<std::string, int>> *>(pUserData);
		pSnapshot->emplace_back(pVariable->m_pScriptName, *pIntVariable->m_pVariable);
	}

	void ConDiscardLegacyHudIslandEdgeMargin(IConsole::IResult *, void *)
	{
	}

	int QmLocalReferenceClientId(const CGameClient *pGameClient)
	{
		const int LocalId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
		if(LocalId >= 0 && LocalId < MAX_CLIENTS)
			return LocalId;
		const int MainLocalId = pGameClient->m_aLocalIds[0];
		return MainLocalId >= 0 && MainLocalId < MAX_CLIENTS ? MainLocalId : -1;
	}

	bool QmCachedOtherTeam(const CGameClient *pGameClient, int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;

		const int LocalId = QmLocalReferenceClientId(pGameClient);
		if(LocalId < 0 || LocalId >= MAX_CLIENTS)
			return false;
		if(pGameClient->m_aClients[LocalId].m_Team == TEAM_SPECTATORS)
			return false;

		const bool Local = LocalId == ClientId;
		if((pGameClient->m_aClients[LocalId].m_Solo || pGameClient->m_aClients[ClientId].m_Solo) && !Local)
			return true;

		if(pGameClient->m_Teams.Team(ClientId) == TEAM_SUPER || pGameClient->m_Teams.Team(LocalId) == TEAM_SUPER)
			return false;

		return pGameClient->m_Teams.Team(ClientId) != pGameClient->m_Teams.Team(LocalId);
	}

	float QmKnownOwnerEventAlpha(CGameClient *pGameClient, int Owner)
	{
		if(Owner < 0 || Owner >= MAX_CLIENTS)
			return 1.0f;

		float Alpha = 1.0f;
		if(QmCachedOtherTeam(pGameClient, Owner))
			Alpha = g_Config.m_ClShowOthersAlpha / 100.0f;
		return Alpha;
	}

	void QmAddUniqueOwnerCandidate(int &Owner, bool &Ambiguous, int Candidate)
	{
		if(Candidate < 0 || Candidate >= MAX_CLIENTS || Ambiguous)
			return;

		if(Owner < 0)
			Owner = Candidate;
		else if(Owner != Candidate)
			Ambiguous = true;
	}

	float QmDistancePointSegment(vec2 Pos, vec2 From, vec2 To)
	{
		const vec2 Segment = To - From;
		const float SegmentLengthSquared = length_squared(Segment);
		if(SegmentLengthSquared <= 0.000001f)
			return distance(Pos, From);

		const float Progress = std::clamp(dot(Pos - From, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
		return distance(Pos, From + Segment * Progress);
	}

	bool QmProjectileMotion(const CProjectileData &Projectile, const CTuningParams *pTuning, float &Curvature, float &Speed)
	{
		if(Projectile.m_Type == WEAPON_GRENADE)
		{
			Curvature = pTuning->m_GrenadeCurvature;
			Speed = pTuning->m_GrenadeSpeed;
			return true;
		}
		if(Projectile.m_Type == WEAPON_SHOTGUN)
		{
			Curvature = pTuning->m_ShotgunCurvature;
			Speed = pTuning->m_ShotgunSpeed;
			return true;
		}
		if(Projectile.m_Type == WEAPON_GUN)
		{
			Curvature = pTuning->m_GunCurvature;
			Speed = pTuning->m_GunSpeed;
			return true;
		}
		return false;
	}

	void QmAddExplosionProjectileCandidate(CGameClient *pGameClient, int &Owner, bool &Ambiguous, const CProjectileData &Projectile, vec2 Pos)
	{
		if(!Projectile.m_ExtraInfo || !Projectile.m_Explosive || Projectile.m_Owner < 0 || Projectile.m_Owner >= MAX_CLIENTS)
			return;

		float Curvature = 0.0f;
		float Speed = 0.0f;
		const int TuneZone = std::clamp(Projectile.m_TuneZone, 0, NUM_TUNEZONES - 1);
		if(!QmProjectileMotion(Projectile, pGameClient->GetTuning(TuneZone), Curvature, Speed))
			return;

		const float FromTime = std::max(0.0f, (pGameClient->Client()->PrevGameTick(g_Config.m_ClDummy) - Projectile.m_StartTick) / (float)pGameClient->Client()->GameTickSpeed());
		const float ToTime = std::max(0.0f, (pGameClient->Client()->GameTick(g_Config.m_ClDummy) - Projectile.m_StartTick) / (float)pGameClient->Client()->GameTickSpeed());
		if(ToTime <= 0.0f && Projectile.m_StartTick > pGameClient->Client()->GameTick(g_Config.m_ClDummy))
			return;

		const vec2 From = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, FromTime);
		const vec2 To = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, ToTime);
		constexpr float MaxExplosionOwnerDistance = 96.0f;
		if(QmDistancePointSegment(Pos, From, To) <= MaxExplosionOwnerDistance)
			QmAddUniqueOwnerCandidate(Owner, Ambiguous, Projectile.m_Owner);
	}

	void QmAddExplosionWorldCandidates(CGameClient *pGameClient, CGameWorld &World, int &Owner, bool &Ambiguous, vec2 Pos)
	{
		for(CProjectile *pProj = static_cast<CProjectile *>(World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE)); pProj; pProj = static_cast<CProjectile *>(pProj->TypeNext()))
		{
			const CProjectileData Projectile = pProj->GetData();
			QmAddExplosionProjectileCandidate(pGameClient, Owner, Ambiguous, Projectile, Pos);
		}
	}

	bool QmIsProjectileSnapType(int Type)
	{
		return Type == NETOBJTYPE_PROJECTILE || Type == NETOBJTYPE_DDRACEPROJECTILE || Type == NETOBJTYPE_DDNETPROJECTILE;
	}

	int QmInferExplosionOwner(CGameClient *pGameClient, vec2 Pos)
	{
		int Owner = -1;
		bool Ambiguous = false;

		for(const CSnapEntities &Ent : pGameClient->SnapEntities())
		{
			if(!QmIsProjectileSnapType(Ent.m_Item.m_Type))
				continue;
			const CProjectileData Projectile = ExtractProjectileInfo(Ent.m_Item.m_Type, Ent.m_Item.m_pData, &pGameClient->m_GameWorld, Ent.m_pDataEx);
			QmAddExplosionProjectileCandidate(pGameClient, Owner, Ambiguous, Projectile, Pos);
		}

		QmAddExplosionWorldCandidates(pGameClient, pGameClient->m_GameWorld, Owner, Ambiguous, Pos);
		QmAddExplosionWorldCandidates(pGameClient, pGameClient->m_PredictedWorld, Owner, Ambiguous, Pos);
		QmAddExplosionWorldCandidates(pGameClient, pGameClient->m_PrevPredictedWorld, Owner, Ambiguous, Pos);

		return Ambiguous ? -1 : Owner;
	}

	void QmAddHammerAttackSample(
		SQmHammerAttackSample *pSamples,
		int &NumSamples,
		int MaxSamples,
		int ClientId,
		const CNetObj_Character *pCurrent,
		const CNetObj_Character *pPrevious,
		bool HammerHitEnabled,
		int DDTeam,
		bool Solo,
		bool Super)
	{
		if(pCurrent == nullptr || NumSamples >= MaxSamples)
			return;
		SQmHammerAttackSample &Sample = pSamples[NumSamples++];
		Sample.m_ClientId = ClientId;
		Sample.m_AttackTick = pCurrent->m_AttackTick;
		Sample.m_Weapon = pCurrent->m_Weapon;
		Sample.m_HammerHitEnabled = HammerHitEnabled;
		Sample.m_PrevPos = pPrevious != nullptr ? vec2(pPrevious->m_X, pPrevious->m_Y) : vec2(pCurrent->m_X, pCurrent->m_Y);
		Sample.m_CurPos = vec2(pCurrent->m_X, pCurrent->m_Y);
		Sample.m_Direction = direction(pCurrent->m_Angle / 256.0f);
		Sample.m_ProximityRadius = CCharacterCore::PhysicalSize();
		Sample.m_DDTeam = DDTeam;
		Sample.m_Solo = Solo;
		Sample.m_Super = Super;
	}

	SQmHammerHitMatch QmInferHammerHit(CGameClient *pGameClient, vec2 Pos, int EventTick)
	{
		SQmHammerAttackSample aAttackSamples[MAX_CLIENTS];
		SQmHammerTargetSample aTargetSamples[MAX_CLIENTS];
		int NumAttackSamples = 0;
		int NumTargetSamples = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto *pCurrent = static_cast<const CNetObj_Character *>(pGameClient->Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_CHARACTER, ClientId));
			const auto *pPrevious = static_cast<const CNetObj_Character *>(pGameClient->Client()->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_CHARACTER, ClientId));
			const CGameClient::CSnapState::CCharacterInfo &Character = pGameClient->m_Snap.m_aCharacters[ClientId];
			const int CharacterFlags = Character.m_HasExtendedData ? Character.m_ExtendedData.m_Flags : 0;
			const bool HammerHitEnabled = (CharacterFlags & CHARACTERFLAG_HAMMER_HIT_DISABLED) == 0;
			const int DDTeam = pGameClient->m_Teams.Team(ClientId);
			const bool Solo = (CharacterFlags & CHARACTERFLAG_SOLO) != 0;
			const bool Super = (CharacterFlags & CHARACTERFLAG_SUPER) != 0 || QmIsHammerSuperTeam(DDTeam, pGameClient->m_Teams.m_IsDDRace16);
			QmAddHammerAttackSample(aAttackSamples, NumAttackSamples, std::size(aAttackSamples), ClientId, pCurrent, pPrevious, HammerHitEnabled, DDTeam, Solo, Super);
			if((pCurrent == nullptr && pPrevious == nullptr) || NumTargetSamples >= MAX_CLIENTS)
				continue;
			SQmHammerTargetSample &Target = aTargetSamples[NumTargetSamples++];
			Target.m_ClientId = ClientId;
			Target.m_PrevPos = pPrevious != nullptr ? vec2(pPrevious->m_X, pPrevious->m_Y) : vec2(pCurrent->m_X, pCurrent->m_Y);
			Target.m_CurPos = pCurrent != nullptr ? vec2(pCurrent->m_X, pCurrent->m_Y) : Target.m_PrevPos;
			Target.m_ProximityRadius = CCharacterCore::PhysicalSize();
			Target.m_DDTeam = DDTeam;
			Target.m_Solo = Solo;
			Target.m_Super = Super;
		}

		return QmMatchHammerHitEvent(Pos, EventTick, aAttackSamples, NumAttackSamples, aTargetSamples, NumTargetSamples);
	}

	void SetDemoInputKeyState(unsigned char *pKeyStates, int Key, bool Pressed)
	{
		dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "invalid demo input key");
		const unsigned char Mask = 1U << (Key & 7);
		if(Pressed)
			pKeyStates[Key >> 3] |= Mask;
		else
			pKeyStates[Key >> 3] &= ~Mask;
	}

	void LoadNamedSingleFileImage(CGameClient *pGameClient, int ImageId, const char *pCategoryId, const char *pActiveName)
	{
		if(ImageId < 0 || ImageId >= g_pData->m_NumImages || pCategoryId == nullptr || pActiveName == nullptr)
			return;

		IGraphics::CTextureHandle NewTexture;

		for(const std::string &Candidate : BuildNamedSingleFileAssetCandidates(pCategoryId, pActiveName))
		{
			if(Candidate.empty())
				continue;

			NewTexture = pGameClient->Graphics()->LoadTexture(Candidate.c_str(), IStorage::TYPE_ALL);
			if(NewTexture.IsValid() && !NewTexture.IsNullTexture())
				break;
			NewTexture = IGraphics::CTextureHandle();
		}

		if(!NewTexture.IsValid() || NewTexture.IsNullTexture())
			return;

		pGameClient->Graphics()->UnloadTexture(&g_pData->m_aImages[ImageId].m_Id);
		g_pData->m_aImages[ImageId].m_Id = NewTexture;
	}
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

using namespace std::chrono_literals;

constexpr int QM_SKIN_CHANGE_TRANSITION_SCOPE_OWN = 0;
constexpr int QM_SKIN_CHANGE_TRANSITION_SCOPE_LOCAL = 1;
constexpr int QM_SKIN_CHANGE_TRANSITION_SCOPE_ALL = 2;

namespace
{
	float QmBestInputInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
	{
		if(!Enable)
			return Fraction;
		const float T = std::clamp(Fraction, 0.0f, 1.0f);
		const float T2 = T * T;
		const float CubicT = 3.0f * T2 - 2.0f * T2 * T;
		switch(std::clamp(g_Config.m_QmBestInputInterpolation, 1, 3))
		{
		case 2:
			return CubicT;
		case 3:
			return mix(T, CubicT, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
		default:
			return T;
		}
	}

	vec2 QmBestInputInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable)
	{
		return mix(PrevPos, CurPos, QmBestInputInterpolationAmount(Fraction, length(CurPos - PrevPos), Enable));
	}

	float EffectiveFastInputOffsetTicks(const CGameClient *pGameClient)
	{
		SQmFastInputSettings Settings;
		Settings.m_Enabled = pGameClient->TClientComponent().IsFastInputActive();
		Settings.m_Mode = g_Config.m_QmFastInputMode;
		Settings.m_FastAmountMs = g_Config.m_TcFastInputAmount;
		Settings.m_BestOffset = g_Config.m_QmBestInputOffset;
		Settings.m_BestSmoothing = g_Config.m_QmBestInputSmoothing;
		Settings.m_BestLatencyComp = g_Config.m_QmBestInputLatencyComp;
		Settings.m_SaikoPlusAmount = g_Config.m_QmSaikoPlusAmount;
		return QmEffectiveFastInputOffsetTicks(Settings);
	}

	int FastInputPredictionTicks(float OffsetTicks)
	{
		return QmFastInputPredictionTicks(OffsetTicks, g_Config.m_QmFastInputMode);
	}

	bool EffectiveFastInputOthers(const CGameClient *pGameClient)
	{
		return QmEffectiveFastInputOthers(pGameClient->TClientComponent().IsFastInputActive(), g_Config.m_QmFastInputMode, g_Config.m_TcFastInputOthers != 0, g_Config.m_QmBestInputOthers != 0, g_Config.m_QmSaikoPlusOthers != 0);
	}

} // namespace

const char *CGameClient::Version() const { return GAME_VERSION; }
const char *CGameClient::NetVersion() const { return GAME_NETVERSION; }
const char *CGameClient::NetVersion7() const { return GAME_NETVERSION7; }
int CGameClient::DDNetVersion() const { return DDNET_VERSION_NUMBER; }
const char *CGameClient::DDNetVersionStr() const { return m_aDDNetVersionStr; }
int CGameClient::ClientVersion7() const { return CLIENT_VERSION7; }
const char *CGameClient::GetItemName(int Type) const { return m_NetObjHandler.GetObjName(Type); }

bool CGameClient::CanRunRuntimeConfigConchainEffects() const
{
	return m_pClient != nullptr && m_pClient->GlobalTime();
}

bool CGameClient::ClientStateOnline() const
{
	return m_pClient != nullptr && m_pClient->State() == IClient::STATE_ONLINE;
}

bool CGameClient::ClientStateAtLeastOnline() const
{
	return m_pClient != nullptr && m_pClient->State() >= IClient::STATE_ONLINE;
}

void CGameClient::StopRaceRecordIfRecording() const
{
	if(m_pClient != nullptr && m_pClient->RaceRecord_IsRecording())
		m_pClient->RaceRecord_Stop();
}

IFrameScheduler *CGameClient::FrameScheduler() const
{
	return m_pFrameScheduler;
}

void CGameClient::OnConsoleInit()
{
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pTextRender = Kernel()->RequestInterface<ITextRender>();
	m_pSound = Kernel()->RequestInterface<ISound>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pConfig = m_pConfigManager->Values();
	m_pInput = Kernel()->RequestInterface<IInput>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pDemoPlayer = Kernel()->RequestInterface<IDemoPlayer>();
	m_pServerBrowser = Kernel()->RequestInterface<IServerBrowser>();
	m_pEditor = Kernel()->RequestInterface<IEditor>();
	m_pFavorites = Kernel()->RequestInterface<IFavorites>();
	m_pFriends = Kernel()->RequestInterface<IFriends>();
	m_pFoes = m_pClient->Foes();
	m_pDiscord = Kernel()->RequestInterface<IDiscord>();
	m_pFrameScheduler = Kernel()->RequestInterface<IFrameScheduler>();
#if defined(CONF_AUTOUPDATE)
	m_pUpdater = Kernel()->RequestInterface<IUpdater>();
#endif
	m_pHttp = Kernel()->RequestInterface<IHttp>();
	m_QmImeManager.Init(this);

	// Keep the stable profiler ID next to its component pointer so attribution cannot drift.
	const auto AddComponent = [&](CComponent *pComponent, const char *pPerfName) {
		m_vpAll.push_back(pComponent);
		m_vpAllPerfNames.push_back(pPerfName);
	};
	AddComponent(&m_Skins, "skins");
	AddComponent(&m_Skins7, "skins7");
	AddComponent(&m_CountryFlags, "country_flags");
	AddComponent(&m_MapImages, "map_images");
	AddComponent(&m_Effects, "effects");
	AddComponent(&m_SkinProfiles, "skin_profiles");
	AddComponent(&m_Binds, "binds");
	AddComponent(&m_Binds.m_SpecialBinds, "special_binds");
	AddComponent(&m_Controls, "controls");
	AddComponent(&m_Camera, "camera");
	AddComponent(&m_Sounds, "sounds");
	AddComponent(&m_Voting, "voting");
	AddComponent(&m_Particles, "particles");
	AddComponent(&m_RaceDemo, "race_demo");
	AddComponent(&m_Rainbow, "rainbow");
	AddComponent(&m_MapSounds, "map_sounds");
	AddComponent(&m_Censor, "censor");
	AddComponent(&m_Background, "background");
	AddComponent(&m_BackgroundParticles, "background_particles");
	AddComponent(&m_MapLayersBackground, "map_layers_background");
	AddComponent(&m_BgDraw, "background_draw");
	AddComponent(&m_Particles.m_RenderTrail, "particles_trail");
	AddComponent(&m_Particles.m_RenderTrailExtra, "particles_trail_extra");
	AddComponent(&m_Items, "items");
	AddComponent(&m_Trails, "trails");
	AddComponent(&m_Translate, "translate");
	AddComponent(&m_Ghost, "ghost");
	AddComponent(&m_QmClient, "qmclient");
	AddComponent(&m_QmAxiomAutoLogin, "axiom_auto_login");
	AddComponent(&m_QmAxiomScores, "axiom_scores");
	AddComponent(&m_QmChatEmoji, "chat_emoji");
	AddComponent(&m_QmMonitoring, "monitoring");
	AddComponent(&m_QmWeaponTrajectory, "weapon_trajectory");
	AddComponent(&m_TClient, "tclient");
	AddComponent(&m_FastPractice, "fast_practice");
	AddComponent(&m_Voice, "voice");
	AddComponent(&m_SystemMediaControls, "system_media_controls");
	AddComponent(&m_NeteaseIntegration, "netease_integration");
	AddComponent(&m_MusicLyricsIntegration, "music_lyrics_integration");
	AddComponent(&m_SpotifyIntegration, "spotify_integration");
	AddComponent(&m_MusicAppWatcher, "music_app_watcher");
	AddComponent(&m_Players, "players");
	AddComponent(&m_MovingTilesBackground, "moving_tiles_background");
	AddComponent(&m_MapLayersForeground, "map_layers_foreground");
	AddComponent(&m_MovingTilesForeground, "moving_tiles_foreground");
	AddComponent(&m_Outlines, "outlines");
	AddComponent(&m_CollisionHitbox, "collision_hitbox");
	AddComponent(&m_Pet, "pet");
	AddComponent(&m_Particles.m_RenderExplosions, "particles_explosions");
	AddComponent(&m_NamePlates, "nameplates");
	AddComponent(&m_Particles.m_RenderExtra, "particles_extra");
	AddComponent(&m_Particles.m_RenderGeneral, "particles_general");
	AddComponent(&m_FreezeBars, "freeze_bars");
	AddComponent(&m_DamageInd, "damage_indicators");
	AddComponent(&m_PlayerIndicator, "player_indicator");
	AddComponent(&m_Mod, "mod");
	AddComponent(&m_CustomCommunities, "custom_communities");
	AddComponent(&m_PlayerPoints, "player_points");
	AddComponent(&m_Hud, "hud");
	AddComponent(&m_Spectator, "spectator");
	AddComponent(&m_Emoticon, "emoticon");
	AddComponent(&m_BindChat, "bind_chat");
	AddComponent(&m_BindWheel, "bind_wheel");
	AddComponent(&m_WarList, "war_list");
	AddComponent(&m_StatusBar, "status_bar");
	AddComponent(&m_InfoMessages, "info_messages");
	AddComponent(&m_Chat, "chat");
	AddComponent(&m_QmHudNotifications, "hud_notifications");
	AddComponent(&m_QmBindStatusHud, "qm_bind_status_hud");
	AddComponent(&m_Broadcast, "broadcast");
	AddComponent(&m_ImportantAlert, "important_alert");
	AddComponent(&m_DebugHud, "debug_hud");
	AddComponent(&m_TouchControls, "touch_controls");
	AddComponent(&m_Scoreboard, "scoreboard");
	AddComponent(&m_Statboard, "statboard");
	AddComponent(&m_Motd, "motd");
	AddComponent(&m_Menus, "menus");
	AddComponent(&m_PieMenu, "pie_menu");
	AddComponent(&m_InputOverlay, "input_overlay");
	AddComponent(&m_HudEditor, "hud_editor");
	AddComponent(&m_Tooltips, "tooltips");
	AddComponent(&m_Scripting, "scripting");
	AddComponent(&m_KeyBinder, "key_binder");
	AddComponent(&m_GameConsole, "game_console");
	AddComponent(&m_MenuBackground, "menu_background");
	AddComponent(&m_UiEffects, "ui_effects");
	dbg_assert(m_vpAll.size() == m_vpAllPerfNames.size(), "component profiler IDs must stay aligned");
	m_vQmStutterPendingUpdateMs.resize(m_vpAll.size());
	m_vQmStutterPendingRenderMs.resize(m_vpAll.size());
	m_vQmStutterComponentSamples.resize(m_vpAll.size());

	// build the input stack
	m_vpInput.insert(m_vpInput.end(), {&m_KeyBinder, // this will take over all input when we want to bind a key
						  &m_Binds.m_SpecialBinds,
						  &m_GameConsole,
						  &m_Chat, // chat has higher prio, due to that you can quit it by pressing esc
						  &m_Scoreboard,
						  &m_Motd, // for pressing esc to remove it
						  &m_Spectator,
						  &m_BindWheel,
						  &m_Emoticon,
						  &m_ImportantAlert,
						  &m_Menus,
						  &m_PieMenu,
						  &m_HudEditor,
						  &m_TClient, // TClient repeat message
						  &m_Controls,
						  &m_TouchControls,
						  &m_Binds});

	// initialize client data
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CClientData &Client = m_aClients[ClientId];
		Client.m_pGameClient = this;
		Client.m_ClientId = ClientId;
	}

	// add basic console commands
	IConsole *pConsole = m_pConsole;
	pConsole->Register("qm_hud_island_edge_margin", "?i[value]", CFGFLAG_CLIENT, ConDiscardLegacyHudIslandEdgeMargin, nullptr, "Ignored legacy Dynamic Island edge margin");
	pConsole->Register("team", "i[team-id]", CFGFLAG_CLIENT, ConTeam, this, "Switch team");
	pConsole->Register("kill", "", CFGFLAG_CLIENT, ConKill, this, "Kill yourself to restart");
	pConsole->Register("ready_change", "", CFGFLAG_CLIENT, ConReadyChange7, this, "Change ready state (0.7 only)");

	// register game commands to allow the client prediction to load settings from the map
	pConsole->Register("tune", "s[tuning] ?f[value]", CFGFLAG_GAME, ConTuneParam, this, "Tune variable to value");
	pConsole->Register("tune_zone", "i[zone] s[tuning] f[value]", CFGFLAG_GAME, ConTuneZone, this, "Tune in zone a variable to value");
	pConsole->Register("mapbug", "s[mapbug]", CFGFLAG_GAME, ConMapbug, this, "Enable map compatibility mode using the specified bug (example: grenade-doubleexplosion@ddnet.tw)");

	for(auto &pComponent : m_vpAll)
		pComponent->OnInterfacesInit(this);

	m_LocalServer.OnInterfacesInit(this);

	// let all the other components register their console commands
	for(auto &pComponent : m_vpAll)
		pComponent->OnConsoleInit();

	pConsole->Chain("cl_languagefile", ConchainLanguageUpdate, this);

	pConsole->Chain("player_name", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_clan", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_country", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_use_custom_color", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_color_body", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_color_feet", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player_skin", ConchainSpecialInfoupdate, this);

	pConsole->Chain("player7_skin", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_body", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_marking", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_decoration", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_hands", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_feet", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_skin_eyes", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_body", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_marking", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_decoration", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_hands", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_feet", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_color_eyes", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_body", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_marking", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_decoration", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_hands", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_feet", ConchainSpecialInfoupdate, this);
	pConsole->Chain("player7_use_custom_color_eyes", ConchainSpecialInfoupdate, this);

	pConsole->Chain("dummy_name", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_clan", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_country", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_use_custom_color", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_color_body", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_color_feet", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy_skin", ConchainSpecialDummyInfoupdate, this);

	pConsole->Chain("dummy7_skin", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_body", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_marking", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_decoration", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_hands", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_feet", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_skin_eyes", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_body", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_marking", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_decoration", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_hands", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_feet", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_color_eyes", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_body", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_marking", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_decoration", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_hands", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_feet", ConchainSpecialDummyInfoupdate, this);
	pConsole->Chain("dummy7_use_custom_color_eyes", ConchainSpecialDummyInfoupdate, this);

	pConsole->Chain("cl_skin_download_url", ConchainRefreshSkins, this);
	pConsole->Chain("cl_skin_community_download_url", ConchainRefreshSkins, this);
	pConsole->Chain("cl_skin_prefix", ConchainRefreshSkins, this);
	pConsole->Chain("cl_download_skins", ConchainRefreshSkins, this);
	pConsole->Chain("cl_download_community_skins", ConchainRefreshSkins, this);
	pConsole->Chain("cl_vanilla_skins_only", ConchainRefreshSkins, this);
	pConsole->Chain("events", ConchainRefreshEventSkins, this);

	pConsole->Chain("cl_dummy", ConchainSpecialDummy, this);

	pConsole->Chain("cl_menu_map", ConchainMenuMap, this);
}

// One-shot migration of legacy tc_jump_hint* settings into qm_jump_hint*.
// Copied only when the new value is still the built-in default and the legacy
// value is non-default, mirroring the previous MigrateChatBubbleConfig pattern.
static void MigrateJumpHintConfig()
{
	auto MigrateInt = [](int &NewValue, int LegacyValue, int NewDefault, int LegacyDefault) {
		if(NewValue == NewDefault && LegacyValue != LegacyDefault)
			NewValue = LegacyValue;
	};
	auto MigrateCol = [](unsigned &NewValue, unsigned LegacyValue, unsigned NewDefault, unsigned LegacyDefault) {
		if(NewValue == NewDefault && LegacyValue != LegacyDefault)
			NewValue = LegacyValue;
	};
	auto MigrateStr = [](char *pNewValue, size_t NewSize, const char *pLegacyValue, const char *pNewDefault) {
		if(str_comp(pNewValue, pNewDefault) == 0 && pLegacyValue[0] != '\0' && str_comp(pLegacyValue, pNewDefault) != 0)
			str_copy(pNewValue, pLegacyValue, NewSize);
	};
	MigrateInt(g_Config.m_QmJumpHint, g_Config.m_TcJumpHintLegacy, DefaultConfig::QmJumpHint, DefaultConfig::TcJumpHintLegacy);
	MigrateStr(g_Config.m_QmJumpHintText, sizeof(g_Config.m_QmJumpHintText), g_Config.m_TcJumpHintTextLegacy, DefaultConfig::QmJumpHintText);
	MigrateCol(g_Config.m_QmJumpHintColor, g_Config.m_TcJumpHintColorLegacy, DefaultConfig::QmJumpHintColor, DefaultConfig::TcJumpHintColorLegacy);
	MigrateInt(g_Config.m_QmJumpHintX, g_Config.m_TcJumpHintXLegacy, DefaultConfig::QmJumpHintX, DefaultConfig::TcJumpHintXLegacy);
	MigrateInt(g_Config.m_QmJumpHintY, g_Config.m_TcJumpHintYLegacy, DefaultConfig::QmJumpHintY, DefaultConfig::TcJumpHintYLegacy);
	MigrateInt(g_Config.m_QmJumpHintSize, g_Config.m_TcJumpHintSizeLegacy, DefaultConfig::QmJumpHintSize, DefaultConfig::TcJumpHintSizeLegacy);
}

// CFGFLAG_COLALPHA 将这组设置从六位 RGB 改为八位 ARGB。
// 对已保存的 RGB 值仅迁移一次，恢复各自声明的默认 alpha。
static void MigrateTranslateUiColorAlphaConfig(const IConfigManager *pConfigManager)
{
	bool Migrated = g_Config.m_QmTranslateColorAlphaMigrated != 0;
	const auto InputAlphaMode = [pConfigManager](const char *pScriptName) {
		return pConfigManager != nullptr ? pConfigManager->ColorValueInputAlphaMode(pScriptName) : EColorInputAlphaMode::PACKED;
	};
	NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated,
		g_Config.m_QmTranslateBtnColorDisabled,
		g_Config.m_QmTranslateBtnColorEnabled,
		g_Config.m_QmTranslateMenuBgColor,
		g_Config.m_QmTranslateMenuOptionSelected,
		g_Config.m_QmTranslateMenuOptionNormal,
		DefaultConfig::QmTranslateBtnColorDisabled,
		DefaultConfig::QmTranslateBtnColorEnabled,
		DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected,
		DefaultConfig::QmTranslateMenuOptionNormal,
		InputAlphaMode("qm_translate_btn_color_disabled"),
		InputAlphaMode("qm_translate_btn_color_enabled"),
		InputAlphaMode("qm_translate_menu_bg_color"),
		InputAlphaMode("qm_translate_menu_option_selected"),
		InputAlphaMode("qm_translate_menu_option_normal"));
	g_Config.m_QmTranslateColorAlphaMigrated = Migrated ? 1 : 0;
}

static void GenerateTimeoutCode(char *pTimeoutCode)
{
	if(pTimeoutCode[0] == '\0' || str_comp(pTimeoutCode, "hGuEYnfxicsXGwFq") == 0)
	{
		for(unsigned int i = 0; i < 16; i++)
		{
			if(rand() % 2)
				pTimeoutCode[i] = (char)((rand() % ('z' - 'a' + 1)) + 'a');
			else
				pTimeoutCode[i] = (char)((rand() % ('Z' - 'A' + 1)) + 'A');
		}
	}
}

void CGameClient::InitializeLanguage()
{
	// set the language
	g_Localization.LoadIndexfile(Storage(), Console());
	if(g_Config.m_ClShowWelcome)
		g_Localization.SelectDefaultLanguage(Console(), g_Config.m_ClLanguagefile, sizeof(g_Config.m_ClLanguagefile));
	g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());
}

void CGameClient::ForceUpdateConsoleRemoteCompletionSuggestions()
{
	m_GameConsole.ForceUpdateRemoteCompletionSuggestions();
}

void CGameClient::OnInit()
{
	const int64_t OnInitStart = time_get();
	IClient *pClient = m_pClient;

	// Migrate legacy tc_jump_hint_text into qm_jump_hint_text before any HUD use.
	MigrateJumpHintConfig();
	MigrateTranslateUiColorAlphaConfig(ConfigManager());

	// Initialize config tags system
	InitConfigTags();

	pClient->SetLoadingCallback([this](IClient::ELoadingCallbackDetail Detail) {
		const char *pTitle;
		if(Detail == IClient::LOADING_CALLBACK_DETAIL_DEMO || DemoPlayer()->IsPlaying())
		{
			pTitle = Localize("Preparing demo playback");
		}
		else
		{
			pTitle = Localize("Connected");
		}

		const char *pMessage;
		switch(Detail)
		{
		case IClient::LOADING_CALLBACK_DETAIL_MAP:
			pMessage = Localize("Loading map file from storage");
			break;
		case IClient::LOADING_CALLBACK_DETAIL_DEMO:
			pMessage = Localize("Loading demo file from storage");
			break;
		default:
			dbg_assert_failed("Invalid callback loading detail");
		}
		m_Menus.RenderLoading(pTitle, pMessage, 0);
	});

	m_pGraphics = Kernel()->RequestInterface<IGraphics>();
	// 设备重建后引擎会广播「图形资源已重置」，这里负责把游戏侧资源重新建起来。
	// 注意必须早于任何资源加载：需要在 OnInit 的资产加载之前完成注册。
	Graphics()->AddGraphicsResourcesResetListener([this]() { OnGraphicsResourcesReset(); });

	// propagate pointers
	m_UI.Init(Kernel());
	m_UI.SetOnBackButtonPressedCallback([this]() {
		m_BackButtonHandledKeyBind = m_KeyBinder.HasPendingKeyReader();
		if(m_BackButtonHandledKeyBind)
			m_KeyBinder.AbortPendingKey();
	});
	m_UI.SetDispatchInputCallback([this](const IInput::CEvent &Event) {
		if(m_BackButtonHandledKeyBind)
		{
			if(Event.m_Flags & IInput::FLAG_RELEASE)
				m_BackButtonHandledKeyBind = false;
			return;
		}
		OnInput(Event);
	});
	m_UiRuntimeV2.Init(this);
	m_RenderTools.Init(Graphics(), TextRender(), this); // TClient
	m_RenderMap.Init(Graphics(), TextRender());
	m_QmIconManager.Init(Graphics(), Storage(), Console());
	m_AppliedQmUiIconWeight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);

	if(GIT_SHORTREV_HASH)
	{
		str_format(m_aDDNetVersionStr, sizeof(m_aDDNetVersionStr), "%s %s (%s)", CLIENT_NAME, CLIENT_RELEASE_VERSION, GIT_SHORTREV_HASH);
	}
	else
	{
		str_format(m_aDDNetVersionStr, sizeof(m_aDDNetVersionStr), "%s %s", CLIENT_NAME, CLIENT_RELEASE_VERSION);
	}

	// TODO: this should be different
	// setup item sizes
	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		pClient->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
	// HACK: only set static size for items, which were available in the first 0.7 release
	// so new items don't break the snapshot delta
	static const int OLD_NUM_NETOBJTYPES = 23;
	for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
		pClient->SnapSetStaticsize7(i, m_NetObjHandler7.GetObjSize(i));

	if(!TextRender()->LoadFonts())
	{
		pClient->AddWarning(SWarning(Localize("Some fonts could not be loaded. Check the local console for details.")));
	}
	TextRender()->SetFontLanguageVariant(g_Config.m_ClLanguagefile);

	// update and swap after font loading, they are quite huge
	pClient->UpdateAndSwap();

	const char *pLoadingDDNetCaption = Localize("Loading DDNet Client");
	const char *pLoadingMessageComponents = Localize("Initializing components");
	const char *pLoadingMessageComponentsSpecial = Localize("Why are you slowmo replaying to read this?");
	char aLoadingMessage[256];

	int LoadingTotal = g_pData->m_NumImages + ComponentCount();
	if(!g_Config.m_ClThreadsoundloading)
		LoadingTotal += g_pData->m_NumSounds;
	m_Menus.StartLoading(LoadingTotal);

	// init all components
	int SkippedComps = 1;
	int CompCounter = 1;
	const int NumComponents = ComponentCount();
	for(int i = NumComponents - 1; i >= 0; --i)
	{
		m_vpAll[i]->OnInit();
		// try to render a frame after each component, also flushes GPU uploads
		if(m_Menus.IsInit())
		{
			str_format(aLoadingMessage, std::size(aLoadingMessage), "%s [%d/%d]", CompCounter == NumComponents ? pLoadingMessageComponentsSpecial : pLoadingMessageComponents, CompCounter, NumComponents);
			m_Menus.RenderLoading(pLoadingDDNetCaption, aLoadingMessage, SkippedComps);
			SkippedComps = 1;
		}
		else
		{
			++SkippedComps;
		}
		++CompCounter;
	}

	m_GameSkinLoaded = false;
	m_ParticlesSkinLoaded = false;
	m_SpawnEventsProcessed = 0;
	m_SpawnEffectsDispatched = 0;
	m_SpawnEffectsFiltered = 0;
	m_SpawnParticleAddFailures = 0;
	m_EmoticonsSkinLoaded = false;
	m_HudSkinLoaded = false;

	// setup load amount, load textures
	const char *pLoadingMessageAssets = Localize("Initializing assets");
	LoadInitialGraphicsAssets();
	m_Menus.RenderLoading(pLoadingDDNetCaption, pLoadingMessageAssets, 1);

	m_GameWorld.Init(Collision(), m_aTuningList, &m_MapBugs);
	if(!m_pJellyTee)
		m_pJellyTee = std::make_unique<CQmJelly>(this);
	OnReset();

	// Set free binds to DDRace binds if it's active
	m_Binds.SetDDRaceBinds(true);

	GenerateTimeoutCode(g_Config.m_ClTimeoutCode);
	GenerateTimeoutCode(g_Config.m_ClDummyTimeoutCode);

	// Aggressively try to grab window again since some Windows users report
	// window not being focused after starting client.
	Graphics()->SetWindowGrab(true);

	PrewarmSettingsRuntimeCachesDuringLoading(pLoadingDDNetCaption, pLoadingMessageAssets);

	CChecksumData *pChecksum = Client()->ChecksumData();
	pChecksum->m_SizeofGameClient = sizeof(*this);
	pChecksum->m_NumComponents = m_vpAll.size();
	for(size_t i = 0; i < m_vpAll.size(); i++)
	{
		if(i >= std::size(pChecksum->m_aComponentsChecksum))
		{
			break;
		}
		int Size = m_vpAll[i]->Sizeof();
		pChecksum->m_aComponentsChecksum[i] = Size;
	}

	m_Menus.FinishLoading();
	log_trace("gameclient", "initialization finished after %.2fms", (time_get() - OnInitStart) * 1000.0f / (float)time_freq());
}

void CGameClient::PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage)
{
	if(g_Config.m_QmSettingsPrewarm == 0)
		return;

	m_Menus.PrewarmSettingsPages();

	constexpr int TEXT_PREWARM_BUDGET_PER_STEP = 8;
	// loading 是可阻塞阶段（有 loading 画面），循环 prebuild 直到 plan collection complete
	// + prebuild remaining=0。原实现只调一次 budget=8，导致运行时 (ESC 打开/切 tab) 首帧
	// 仍要现场创建文本容器 (text_new 爆发，实测 settings_page_content 单次 355ms)。
	// 用 WarmupReady (plan items + units 都 <= 0) 或连续无进展检测退出，避免死循环。
	constexpr int MAX_TEXT_PREWARM_STEPS = 96;
	constexpr int MAX_NO_PROGRESS_STEPS = 6;

	SSettingsLoadingPrewarmState State;
	State.m_LastBuiltTextContainers = m_Menus.SettingsTextContainerCount();
	State.m_LastMissingTextPlanItems = m_Menus.SettingsTextPrebuildRemaining();
	State.m_LastMissingTextPlanCollectionUnits = m_Menus.SettingsTextPlanCollectionRemaining();
	LogSettingsLoadingPrewarmEvent(Client(), "startup_text_prewarm_begin", State.m_CompletedSteps, 1, 0, State.m_ConsecutiveNoProgressSteps, 0, 0);

	for(int Step = 0; Step < MAX_TEXT_PREWARM_STEPS; ++Step)
	{
		m_Menus.RenderLoading(pLoadingCaption, pLoadingMessage, 0);
		m_Menus.PrewarmSettingsTextPoolForLoading(TEXT_PREWARM_BUDGET_PER_STEP);
		SettingsLoadingPrewarmAdvance(State, m_Menus.SettingsTextContainerCount(), m_Menus.SettingsTextPrebuildRemaining(), m_Menus.SettingsTextPlanCollectionRemaining());
		if(State.m_WarmupReady)
			break;
		if(State.m_ConsecutiveNoProgressSteps >= MAX_NO_PROGRESS_STEPS)
			break;
	}

	LogSettingsLoadingPrewarmEvent(Client(), "startup_text_prewarm_end", State.m_CompletedSteps, 1, 0, State.m_ConsecutiveNoProgressSteps, 0, 0);
}

void CGameClient::OnUpdate()
{
	// Vulkan swapchain 重建在渲染线程执行，能力状态可能晚于窗口 resize 回调变化。
	// 这里仅在 DPI、图标配置或 MSDF capability 变化时重载 atlas。
	SyncQmUiIconWeight();
	m_QmIconManager.RefreshForCurrentDpi();

	const bool TeeSettingsActive = m_Menus.IsSettingsPageActive() && g_Config.m_UiSettingsPage == CMenus::SETTINGS_TEE;
	const bool AssetsSettingsActive = m_Menus.IsSettingsPageActive() && g_Config.m_UiSettingsPage == CMenus::SETTINGS_ASSETS;
	m_Skins.PrepareSettingsThroughputForFrame();
	const int FrameGpuUploadLimit = m_Menus.SettingsGpuUploadLimitForFrame(TeeSettingsActive, AssetsSettingsActive, m_Skins.SettingsGpuUploadLimiterUnitsForFrame());
	const int FrameSkinUploadBudget = TeeSettingsActive ? m_Skins.SettingsGpuUploadFrameBudgetForFrame() : -1;
	m_GpuUploadLimiter.OnFrameStart(FrameGpuUploadLimit);
	m_Menus.ResetSettingsFrameBudgetForFrame(TeeSettingsActive, AssetsSettingsActive, FrameSkinUploadBudget);

	HandleLanguageChanged();
	RefreshStreamerSkinPrivacyAfterStateChange();

	CUIElementBase::Init(Ui()); // update static pointer because game and editor use separate UI

	// handle mouse movement
	float x = 0.0f, y = 0.0f;
	IInput::ECursorType CursorType = Input()->CursorRelative(&x, &y);
	if(CursorType != IInput::CURSOR_NONE)
	{
		for(auto &pComponent : m_vpInput)
		{
			if(pComponent->OnCursorMove(x, y, CursorType))
				break;
		}
	}

	// handle touch events
	const std::vector<IInput::CTouchFingerState> &vTouchFingerStates = Input()->TouchFingerStates();
	bool TouchHandled = false;
	for(auto &pComponent : m_vpInput)
	{
		if(TouchHandled)
		{
			// Also update inactive components so they can handle touch fingers being released.
			pComponent->OnTouchState({});
		}
		else if(pComponent->OnTouchState(vTouchFingerStates))
		{
			Input()->ClearTouchDeltas();
			TouchHandled = true;
		}
	}

	// handle key presses
	Input()->ConsumeEvents([&](const IInput::CEvent &Event) {
		OnInput(Event);
	});

	if(g_Config.m_ClSubTickAiming && m_Binds.m_MouseOnAction)
	{
		m_Controls.m_aMousePosOnAction[g_Config.m_ClDummy] = m_Controls.m_aMousePos[g_Config.m_ClDummy];
		m_Binds.m_MouseOnAction = false;
	}

	if(g_Config.m_QmPerfStutterDiagnostics)
	{
		for(size_t i = 0; i < m_vpAll.size(); ++i)
		{
			CPerfTimer ComponentTimer;
			m_vpAll[i]->OnUpdate();
			RecordComponentUpdate(i, ComponentTimer.ElapsedMs());
		}
	}
	else
	{
		for(auto &pComponent : m_vpAll)
			pComponent->OnUpdate();
	}

	RefreshPredictionAfterConfigChange();

	RecordDemoHudState(false);
	RecordDemoInputState(false);
	RecordDemoGamepadState(false);
	RecordDemoInputWheelEvent();
}

void CGameClient::SyncQmUiIconWeight()
{
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	if(m_AppliedQmUiIconWeight == Weight)
		return;

	m_AppliedQmUiIconWeight = Weight;
	TextRender()->SetIconFontWeight(QmIconWeightUsesBoldFontFallback(Weight));
	m_QmIconManager.RefreshForCurrentDpi();
	OnWindowResize();
}

void CGameClient::RefreshStreamerSkinPrivacyAfterStateChange()
{
	const uint64_t FriendsRevision = Friends() != nullptr ? Friends()->Revision() : 0;
	if(m_LastStreamerHideSkins == g_Config.m_QmStreamerHideSkins &&
		m_LastStreamerFriendsIgnoreClan == g_Config.m_ClFriendsIgnoreClan &&
		m_aLastStreamerLocalIds[0] == m_aLocalIds[0] &&
		m_aLastStreamerLocalIds[1] == m_aLocalIds[1] &&
		m_LastStreamerFriendsRevision == FriendsRevision)
		return;
	m_LastStreamerHideSkins = g_Config.m_QmStreamerHideSkins;
	m_LastStreamerFriendsIgnoreClan = g_Config.m_ClFriendsIgnoreClan;
	std::copy(std::begin(m_aLocalIds), std::end(m_aLocalIds), std::begin(m_aLastStreamerLocalIds));
	m_LastStreamerFriendsRevision = FriendsRevision;
	for(CClientData &ClientData : m_aClients)
	{
		if(!ClientData.m_Active)
			continue;
		ClientData.m_Friend = !IsLocalClientId(ClientData.m_ClientId) && Friends() != nullptr && Friends()->IsFriend(ClientData.m_aName, ClientData.m_aClan, true);
		if(ClientData.m_pSkinInfo != nullptr)
			ClientData.UpdateRenderInfo();
	}
}

int CGameClient::RenderThrottleRefreshRate() const
{
	return m_Menus.IdleRenderFrameRate();
}

void CGameClient::RequestPredictionRefreshAfterConfigChange()
{
	m_RequestPredictionRefreshAfterConfigChange = true;
}

void CGameClient::RefreshPredictionAfterConfigChange()
{
	if(!m_RequestPredictionRefreshAfterConfigChange)
		return;

	m_RequestPredictionRefreshAfterConfigChange = false;

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(m_Snap.m_LocalClientId < 0 || !m_Snap.m_pLocalCharacter)
		return;

	const int Dummy = g_Config.m_ClDummy;
	const int PredTick = Client()->PredGameTick(Dummy);
	const int CurGameTick = Client()->GameTick(Dummy);
	const int MaxLatencyTicks = Client()->GameTickSpeed() + round_to_int(Client()->PredictionMarginMs() * Client()->GameTickSpeed() / 1000.0f);
	if(PredTick <= CurGameTick || PredTick >= CurGameTick + MaxLatencyTicks)
		return;

	OnPredict();
}

int CGameClient::PackDemoHudState(int DummyResetOnSwitch, int DeepflyMode, bool DummyControl, bool DummyCopyMoves)
{
	const int ClampedDummyResetOnSwitch = std::clamp(DummyResetOnSwitch, 0, 3);
	const int ClampedDeepflyMode = std::clamp(DeepflyMode, 0, 3);
	return ClampedDummyResetOnSwitch |
	       (ClampedDeepflyMode << 2) |
	       ((DummyControl ? 1 : 0) << 4) |
	       ((DummyCopyMoves ? 1 : 0) << 5);
}

void CGameClient::UnpackDemoHudState(int PackedState)
{
	m_DemoHudPlaybackState.m_Valid = true;
	m_DemoHudPlaybackState.m_DummyResetOnSwitch = PackedState & 0x3;
	m_DemoHudPlaybackState.m_DeepflyMode = (PackedState >> 2) & 0x3;
	m_DemoHudPlaybackState.m_DummyControl = ((PackedState >> 4) & 0x1) != 0;
	m_DemoHudPlaybackState.m_DummyCopyMoves = ((PackedState >> 5) & 0x1) != 0;
}

void CGameClient::RecordDemoHudState(bool Force)
{
	bool ActiveRecording = Force;
	for(int i = 0; i < RECORDER_MAX && !ActiveRecording; ++i)
	{
		ActiveRecording = DemoRecorder(i)->IsRecording();
	}
	if(!ActiveRecording || Client()->State() != IClient::STATE_ONLINE)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(!Force && Tick == m_LastDemoHudRecordTick)
		return;
	m_LastDemoHudRecordTick = Tick;

	CMsgPacker Msg(NETMSG_QM_DEMO_HUD_STATE, false);
	Msg.AddInt(PackDemoHudState(g_Config.m_ClDummyResetOnSwitch, g_Config.m_QmDeepflyMode, g_Config.m_ClDummyControl != 0, g_Config.m_ClDummyCopyMoves != 0));
	Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
}

void CGameClient::RecordDemoInputState(bool Force)
{
	bool ActiveRecording = Force;
	for(int i = 0; i < RECORDER_MAX && !ActiveRecording; ++i)
	{
		ActiveRecording = DemoRecorder(i)->IsRecording();
	}
	if(!ActiveRecording || Client()->State() != IClient::STATE_ONLINE)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(!Force && Tick == m_LastDemoInputRecordTick)
		return;
	m_LastDemoInputRecordTick = Tick;

	unsigned char aKeyStates[DEMO_INPUT_KEY_STATE_SIZE];
	mem_zero(aKeyStates, sizeof(aKeyStates));
	for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
	{
		SetDemoInputKeyState(aKeyStates, Key, Input()->KeyIsPressed(Key));
	}
	for(int MouseButton = 1; MouseButton <= NUM_MOUSE_BUTTONS; ++MouseButton)
	{
		SetDemoInputKeyState(aKeyStates, KEY_MOUSE_1 + MouseButton - 1, Input()->NativeMousePressed(MouseButton));
	}

	const vec2 AimPos = m_Controls.m_aMousePos[g_Config.m_ClDummy];
	CMsgPacker Msg(NETMSG_QM_DEMO_INPUT_STATE, false);
	Msg.AddRaw(aKeyStates, sizeof(aKeyStates));
	Msg.AddInt(round_truncate(AimPos.x));
	Msg.AddInt(round_truncate(AimPos.y));
	Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
}

void CGameClient::RecordDemoGamepadState(bool Force)
{
	bool ActiveRecording = Force;
	for(int i = 0; i < RECORDER_MAX && !ActiveRecording; ++i)
		ActiveRecording = DemoRecorder(i)->IsRecording();
	if(!ActiveRecording || Client()->State() != IClient::STATE_ONLINE)
		return;

	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(!Force && Tick == m_LastDemoGamepadRecordTick)
		return;
	m_LastDemoGamepadRecordTick = Tick;

	IInput::IJoystick *pJoystick = Input()->GetActiveJoystick();
	const bool Valid = pJoystick != nullptr;
	uint32_t Buttons = 0;
	float aAxes[6] = {};
	int PlayerIndex = 0;
	if(Valid)
	{
		for(int Button = 0; Button < NUM_JOYSTICK_BUTTONS; ++Button)
			if(Input()->KeyIsPressed(KEY_JOYSTICK_BUTTON_0 + Button))
				Buttons |= 1U << Button;
		for(int Axis = 0; Axis < 6 && Axis < pJoystick->GetNumAxes(); ++Axis)
			aAxes[Axis] = std::clamp(pJoystick->GetAxisValue(Axis), -1.0f, 1.0f);
		PlayerIndex = std::clamp(pJoystick->GetIndex(), 0, 2);
	}

	CMsgPacker Msg(NETMSG_QM_DEMO_GAMEPAD_STATE, false);
	Msg.AddInt(Valid ? 1 : 0);
	Msg.AddInt(static_cast<int>(Buttons & 0xffffU));
	Msg.AddInt(static_cast<int>((Buttons >> 16) & 0xffffU));
	for(float Axis : aAxes)
		Msg.AddInt(round_to_int(Axis * 32767.0f));
	Msg.AddInt(PlayerIndex);
	Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
}

void CGameClient::RecordDemoInputWheelEvent() const
{
	bool ActiveRecording = false;
	for(int i = 0; i < RECORDER_MAX && !ActiveRecording; ++i)
	{
		ActiveRecording = DemoRecorder(i)->IsRecording();
	}
	if(!ActiveRecording || Client()->State() != IClient::STATE_ONLINE)
		return;

	int WheelMask = 0;
	if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
		WheelMask |= 1 << 0;
	if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
		WheelMask |= 1 << 1;
	if(Input()->KeyPress(KEY_MOUSE_WHEEL_LEFT))
		WheelMask |= 1 << 2;
	if(Input()->KeyPress(KEY_MOUSE_WHEEL_RIGHT))
		WheelMask |= 1 << 3;
	if(WheelMask == 0)
		return;

	CMsgPacker Msg(NETMSG_QM_DEMO_INPUT_WHEEL, false);
	Msg.AddInt(WheelMask);
	Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
}

void CGameClient::OnInput(const IInput::CEvent &Event)
{
	for(auto &pComponent : m_vpInput)
	{
		// Events with flag `FLAG_RELEASE` must always be forwarded to all components so keys being
		// released can be handled in all components also after some components have been disabled.
		if(pComponent->OnInput(Event) && (Event.m_Flags & ~IInput::FLAG_RELEASE) != 0)
			break;
	}
}

void CGameClient::OnDummySwap()
{
	if(g_Config.m_ClDummyResetOnSwitch)
	{
		int PlayerOrDummy = (g_Config.m_ClDummyResetOnSwitch == 2) ? g_Config.m_ClDummy : (!g_Config.m_ClDummy);
		m_Controls.ResetInput(PlayerOrDummy);
		m_Controls.m_aInputData[PlayerOrDummy].m_Hook = 0;
	}
	const int PrevDummyFire = m_DummyInput.m_Fire;
	m_DummyInput = m_Controls.m_aInputData[!g_Config.m_ClDummy];
	m_Controls.m_aInputData[g_Config.m_ClDummy].m_Fire = PrevDummyFire;
	m_IsDummySwapping = 1;
}

int CGameClient::OnSnapInput(int *pData, bool Dummy, bool Force)
{
	if(!Dummy)
	{
		const int Size = m_Controls.SnapInput(pData);
		return Size;
	}
	if(m_aLocalIds[!g_Config.m_ClDummy] < 0)
	{
		return 0;
	}

	if(!g_Config.m_ClDummyHammer)
	{
		m_DummyHammerResends = 0;
		if(m_DummyFire != 0)
		{
			m_DummyInput.m_Fire = (m_HammerInput.m_Fire + 1) & ~1;
			m_DummyFire = 0;
		}

		if(!Force && (!m_DummyInput.m_Direction && !m_DummyInput.m_Jump && !m_DummyInput.m_Hook))
		{
			return 0;
		}

		mem_copy(pData, &m_DummyInput, sizeof(m_DummyInput));
		return sizeof(m_DummyInput);
	}

	if(m_DummyFire % 25 != 0)
	{
		m_DummyFire++;
		// 重复发送最新的 Fire counter，避免单个非 Vital 输入包丢失导致吞锤。
		if(m_DummyHammerResends > 0)
		{
			m_DummyHammerResends--;
			mem_copy(pData, &m_HammerInput, sizeof(m_HammerInput));
			return sizeof(m_HammerInput);
		}
		return 0;
	}
	m_DummyFire++;

	m_HammerInput.m_Fire = (m_HammerInput.m_Fire + 1) | 1;
	m_DummyHammerResends = DUMMY_HAMMER_RESENDS;
	m_HammerInput.m_WantedWeapon = WEAPON_HAMMER + 1;
	if(!g_Config.m_ClDummyRestoreWeapon)
		m_DummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;

	const vec2 Dir = m_LocalCharacterPos - m_aClients[m_aLocalIds[!g_Config.m_ClDummy]].m_Predicted.m_Pos;
	m_HammerInput.m_TargetX = (int)Dir.x;
	m_HammerInput.m_TargetY = (int)Dir.y;

	mem_copy(pData, &m_HammerInput, sizeof(m_HammerInput));
	return sizeof(m_HammerInput);
}

bool CGameClient::GetDummyFastInput(CNetObj_PlayerInput &DummyFastInput, const CNetObj_PlayerInput *pDummyInputData, const CCharacter *pDummyChar, int LocalTee, int DummyTee) const
{
	if(!PredictDummy() || !pDummyChar)
		return false;

	if(g_Config.m_ClDummyHammer)
	{
		DummyFastInput = m_HammerInput;
		return true;
	}

	if(g_Config.m_ClDummyCopyMoves)
	{
		DummyFastInput = m_Controls.m_aFastInput[LocalTee];
		DummyFastInput.m_Fire = m_Controls.m_aFastInput[DummyTee].m_Fire;
		DummyFastInput.m_WantedWeapon = m_Controls.m_aFastInput[DummyTee].m_WantedWeapon;
		DummyFastInput.m_NextWeapon = m_Controls.m_aFastInput[DummyTee].m_NextWeapon;
		DummyFastInput.m_PrevWeapon = m_Controls.m_aFastInput[DummyTee].m_PrevWeapon;
		if(g_Config.m_ClDummyControl)
		{
			const CNetObj_PlayerInput BaseDummyInput = pDummyInputData ? *pDummyInputData : CNetObj_PlayerInput{};
			DummyFastInput.m_Jump = BaseDummyInput.m_Jump;
			DummyFastInput.m_Fire = BaseDummyInput.m_Fire;
			DummyFastInput.m_Hook = BaseDummyInput.m_Hook;
		}
		return true;
	}

	if(g_Config.m_ClDummyControl)
	{
		const CNetObj_PlayerInput BaseDummyInput = pDummyInputData ? *pDummyInputData : CNetObj_PlayerInput{};
		DummyFastInput = BaseDummyInput;
		DummyFastInput.m_Direction = m_Controls.m_aFastInput[DummyTee].m_Direction;
		DummyFastInput.m_PlayerFlags = m_Controls.m_aFastInput[DummyTee].m_PlayerFlags;
		DummyFastInput.m_TargetX = m_Controls.m_aFastInput[DummyTee].m_TargetX;
		DummyFastInput.m_TargetY = m_Controls.m_aFastInput[DummyTee].m_TargetY;
		DummyFastInput.m_WantedWeapon = m_Controls.m_aFastInput[DummyTee].m_WantedWeapon;
		DummyFastInput.m_NextWeapon = m_Controls.m_aFastInput[DummyTee].m_NextWeapon;
		DummyFastInput.m_PrevWeapon = m_Controls.m_aFastInput[DummyTee].m_PrevWeapon;
		return true;
	}

	return false;
}

void CGameClient::OnConnected()
{
	m_FastPractice.InvalidateBufferedInputState();
	const char *pConnectCaption = DemoPlayer()->IsPlaying() ? Localize("Preparing demo playback") : Localize("Connected");
	const char *pLoadMapContent = Localize("Initializing map logic");
	// render loading before skip is calculated
	m_Menus.RenderLoading(pConnectCaption, pLoadMapContent, 0);
	m_Layers.Init(Kernel()->RequestInterface<IMap>(), false);
	m_Collision.Init(Layers());
	m_GameWorld.m_Core.InitSwitchers(m_Collision.m_HighestSwitchNumber);
	m_GameWorld.m_PredictedEvents.clear();
	m_RaceHelper.Init(this);

	// render loading before going through all components
	m_Menus.RenderLoading(pConnectCaption, pLoadMapContent, 0);
	for(auto &pComponent : m_vpAll)
	{
		pComponent->OnMapLoad();
		pComponent->OnReset();
	}

	ConfigManager()->ResetGameSettings();
	LoadMapSettings();

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		Client()->SetLoadingStateDetail(IClient::LOADING_STATE_DETAIL_GETTING_READY);
		m_Menus.RenderLoading(pConnectCaption, Localize("Sending initial client info"), 0);

		// send the initial info
		SendInfo(true);
		// we should keep this in for now, because otherwise you can't spectate
		// people at start as the other info 64 packet is only sent after the first
		// snap
		Client()->Rcon("crashmeplx");

		m_LocalServer.RconAuthIfPossible();
	}
}

void CGameClient::OnReset()
{
	InvalidateSnapshot();
	ResetDemoPlaybackState();
	m_LastDemoHudRecordTick = -1;
	m_LastDemoInputRecordTick = -1;
	m_LastDemoGamepadRecordTick = -1;
	m_LastDemoPlaybackStateTick = -1;

	m_EditorMovementDelay = 5;

	m_PredictedTick = -1;
	std::fill(std::begin(m_aLastNewPredictedTick), std::end(m_aLastNewPredictedTick), -1);
	std::fill(std::begin(m_aLastPredictedAirJumpTick), std::end(m_aLastPredictedAirJumpTick), -1);
	std::fill(std::begin(m_aLastHammerSkinSwapHitTick), std::end(m_aLastHammerSkinSwapHitTick), -1);
	std::fill(std::begin(m_aLastRandomEmoteHammerHitTick), std::end(m_aLastRandomEmoteHammerHitTick), -1);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		m_aLastRandomEmoteDamageTick[Dummy] = -1;

	m_LastRoundStartTick = -1;
	m_LastRaceTick = -1;
	m_LastFlagCarrierRed = -4;
	m_LastFlagCarrierBlue = -4;

	std::fill(std::begin(m_aCheckInfo), std::end(m_aCheckInfo), -1);

	// m_aDDNetVersionStr is initialized once in OnInit

	std::fill(std::begin(m_aLastPos), std::end(m_aLastPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aLastActive), std::end(m_aLastActive), false);
	ClearClientBrands();

	m_GameOver = false;
	m_GamePaused = false;

	m_SuppressEvents = false;
	m_SpawnEventsProcessed = 0;
	m_SpawnEffectsDispatched = 0;
	m_SpawnEffectsFiltered = 0;
	m_SpawnParticleAddFailures = 0;
	m_NewTick = false;
	m_NewPredictedTick = false;
	m_HammerHitTracker.Reset();
	m_vPendingHammerHitEvents.clear();
	std::fill(std::begin(m_aConfirmedHammerHitEvent), std::end(m_aConfirmedHammerHitEvent), false);

	m_aFlagDropTick[TEAM_RED] = 0;
	m_aFlagDropTick[TEAM_BLUE] = 0;

	m_ServerMode = SERVERMODE_PURE;
	mem_zero(&m_GameInfo, sizeof(m_GameInfo));

	m_DemoSpecId = SPEC_FOLLOW;
	m_LocalCharacterPos = vec2(0.0f, 0.0f);

	m_PredictedPrevChar.Reset();
	m_PredictedChar.Reset();

	// m_Snap was cleared in InvalidateSnapshot

	std::fill(std::begin(m_aLocalTuneZone), std::end(m_aLocalTuneZone), -1);
	std::fill(std::begin(m_aReceivedTuning), std::end(m_aReceivedTuning), false);
	std::fill(std::begin(m_aExpectingTuningForZone), std::end(m_aExpectingTuningForZone), -1);
	std::fill(std::begin(m_aExpectingTuningSince), std::end(m_aExpectingTuningSince), 0);
	std::fill(std::begin(m_aTuning), std::end(m_aTuning), CTuningParams());

	m_ActiveRecordings.reset();

	for(auto &Client : m_aClients)
		Client.Reset();

	for(auto &Stats : m_aStats)
		Stats.Reset();

	std::fill(std::begin(m_aNextChangeInfo), std::end(m_aNextChangeInfo), -1);
	std::fill(std::begin(m_aLocalIds), std::end(m_aLocalIds), -1);
	m_DummyInput = {};
	m_HammerInput = {};
	m_DummyFire = 0;
	m_DummyHammerResends = 0;
	m_ReceivedDDNetPlayer = false;
	m_ReceivedDDNetPlayerFinishTimes = false;
	m_ReceivedDDNetPlayerFinishTimesMillis = false;

	m_Teams.Reset();
	m_GameWorld.Clear();
	m_GameWorld.m_WorldConfig.m_InfiniteAmmo = true;
	m_PredictedWorld.CopyWorld(&m_GameWorld);
	m_PrevPredictedWorld.CopyWorld(&m_PredictedWorld);
	m_RegularPredictedWorld.CopyWorldClean(&m_PredictedWorld);
	m_PrevRegularPredictedWorld.CopyWorldClean(&m_PredictedWorld);

	m_vSnapEntities.clear();

	std::fill(std::begin(m_aDDRaceMsgSent), std::end(m_aDDRaceMsgSent), false);
	std::fill(std::begin(m_aShowOthers), std::end(m_aShowOthers), SHOW_OTHERS_NOT_SET);
	std::fill(std::begin(m_aEnableSpectatorCount), std::end(m_aEnableSpectatorCount), -1);
	std::fill(std::begin(m_aLastUpdateTick), std::end(m_aLastUpdateTick), 0);
	std::fill(std::begin(m_aQ1menGSyncMarkUntil), std::end(m_aQ1menGSyncMarkUntil), 0);
	std::fill(std::begin(m_aQ1menGSyncFootParticlesEnabled), std::end(m_aQ1menGSyncFootParticlesEnabled), false);
	std::fill(std::begin(m_aQ1menGSyncRemoteParticlesEnabled), std::end(m_aQ1menGSyncRemoteParticlesEnabled), false);
	std::fill(std::begin(m_aQmVoiceSyncMarkUntil), std::end(m_aQmVoiceSyncMarkUntil), 0);
	std::fill(std::begin(m_aQmDeveloperMarkUntil), std::end(m_aQmDeveloperMarkUntil), 0);
	std::fill(std::begin(m_aQmDeveloperRainbow), std::end(m_aQmDeveloperRainbow), false);
	mem_zero(m_aaQmDeveloperMarkName, sizeof(m_aaQmDeveloperMarkName));

	m_PredictedDummyId = -1;
	m_IsDummySwapping = false;
	m_CharOrder.Reset();
	std::fill(std::begin(m_aSwitchStateTeam), std::end(m_aSwitchStateTeam), -1);

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
	}

	m_MapBestTimeSeconds = FinishTime::UNSET;
	m_MapBestTimeMillis = 0;
	m_aMapDescription[0] = '\0';

	// m_MapBugs and m_aTuningList are reset in LoadMapSettings

	m_LastShowDistanceZoom = 0.0f;
	m_LastZoom = 0.0f;
	m_LastScreenAspect = 0.0f;
	m_LastDeadzone = 0.0f;
	m_LastFollowFactor = 0.0f;
	m_LastDummyConnected = false;

	m_MultiViewPersonalZoom = 0.0f;
	m_MultiViewActivated = false;
	m_MultiViewTeam = 0;
	m_MultiView.m_Solo = false;
	m_MultiView.m_IsInit = false;
	m_MultiView.m_Teleported = false;
	m_MultiView.m_OldPos = vec2(0.0f, 0.0f);
	m_MultiView.m_OldPersonalZoom = 0;
	m_MultiView.m_SecondChance = 0.0f;
	m_MultiView.m_OldCameraDistance = 0.0f;
	std::fill(std::begin(m_MultiView.m_aLastFreeze), std::end(m_MultiView.m_aLastFreeze), 0.0f);
	std::fill(std::begin(m_MultiView.m_aVanish), std::end(m_MultiView.m_aVanish), false);

	m_CursorInfo.m_CursorOwnerId = -1;
	m_CursorInfo.m_NumSamples = 0;

	m_UiRuntimeV2.Reset();
	m_QmImeManager.Reset();
	if(m_pJellyTee)
		m_pJellyTee->Reset();

	for(auto &pComponent : m_vpAll)
		pComponent->OnReset();

	if(m_pEditor != nullptr)
	{
		m_pEditor->ResetMentions();
		m_pEditor->ResetIngameMoved();
	}

	Collision()->Unload();
	Layers()->Unload();
}

void CGameClient::UpdatePositions()
{
	// local character position
	if(g_Config.m_ClPredict && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(!AntiPingPlayers())
		{
			if(!m_Snap.m_pLocalCharacter || (m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
			{
				// don't use predicted
			}
			else
			{
				m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
			}
		}
		else
		{
			if(!(m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
			{
				if(m_Snap.m_pLocalCharacter)
					m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
			}
			//		else
			//			m_LocalCharacterPos = mix(m_PredictedPrevChar.m_Pos, m_PredictedChar.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
		}
	}
	else if(m_Snap.m_pLocalCharacter && m_Snap.m_pLocalPrevCharacter)
	{
		m_LocalCharacterPos = mix(
			vec2(m_Snap.m_pLocalPrevCharacter->m_X, m_Snap.m_pLocalPrevCharacter->m_Y),
			vec2(m_Snap.m_pLocalCharacter->m_X, m_Snap.m_pLocalCharacter->m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
	}

	// spectator position
	if(m_Snap.m_SpecInfo.m_Active)
	{
		if(m_MultiViewActivated)
		{
			HandleMultiView();
		}
		else if(Client()->State() == IClient::STATE_DEMOPLAYBACK && m_DemoSpecId != SPEC_FOLLOW && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			m_Snap.m_SpecInfo.m_Position = mix(
				vec2(m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Prev.m_X, m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Prev.m_Y),
				vec2(m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Cur.m_X, m_Snap.m_aCharacters[m_Snap.m_SpecInfo.m_SpectatorId].m_Cur.m_Y),
				Client()->IntraGameTick(g_Config.m_ClDummy));
			m_Snap.m_SpecInfo.m_UsePosition = true;
		}
		else if(m_Snap.m_pSpectatorInfo && ((Client()->State() == IClient::STATE_DEMOPLAYBACK && m_DemoSpecId == SPEC_FOLLOW) || (Client()->State() != IClient::STATE_DEMOPLAYBACK && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)))
		{
			if(m_Snap.m_pPrevSpectatorInfo && m_Snap.m_pPrevSpectatorInfo->m_SpectatorId == m_Snap.m_pSpectatorInfo->m_SpectatorId)
				m_Snap.m_SpecInfo.m_Position = mix(vec2(m_Snap.m_pPrevSpectatorInfo->m_X, m_Snap.m_pPrevSpectatorInfo->m_Y),
					vec2(m_Snap.m_pSpectatorInfo->m_X, m_Snap.m_pSpectatorInfo->m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
			else
				m_Snap.m_SpecInfo.m_Position = vec2(m_Snap.m_pSpectatorInfo->m_X, m_Snap.m_pSpectatorInfo->m_Y);
			m_Snap.m_SpecInfo.m_UsePosition = true;
		}
	}

	if(!m_MultiViewActivated && m_MultiView.m_IsInit)
		ResetMultiView();

	UpdateRenderedCharacters();
}

void CGameClient::OnRender()
{
	CPerfTimer FrameTimer;

	m_pFrameScheduler->BeginFrame(Client()->PerfFrame());
	if(m_TClient.IsPreparingUpdateForShutdown())
	{
		Graphics()->Clear(0.0f, 0.0f, 0.0f);
		Ui()->MapScreen();
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Ui()->DoLabel(Ui()->Screen(), m_TClient.UpdateShutdownMessage(), 16.0f, TEXTALIGN_MC);
		Input()->Clear();
		m_pFrameScheduler->EndFrame();
		return;
	}

	const ColorRGBA ClearColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClOverlayEntities ? g_Config.m_ClBackgroundEntitiesColor : g_Config.m_ClBackgroundColor));

#if defined(CONF_PLATFORM_MACOS)
	if(g_Config.m_QmMacosGraphicsDiagnostics != 0)
	{
		// 仅在诊断开启后的首次及相关配置变化时记录，避免污染每帧日志。
		static int s_LastOverlayEntities = -1;
		static unsigned s_LastBackgroundColor = 0;
		static unsigned s_LastBackgroundEntitiesColor = 0;
		static char s_aLastBackgroundEntities[IO_MAX_PATH_LENGTH] = "";
		if(s_LastOverlayEntities != g_Config.m_ClOverlayEntities ||
			s_LastBackgroundColor != g_Config.m_ClBackgroundColor ||
			s_LastBackgroundEntitiesColor != g_Config.m_ClBackgroundEntitiesColor ||
			str_comp(s_aLastBackgroundEntities, g_Config.m_ClBackgroundEntities) != 0)
		{
			log_info("gfx/entities", "clear: overlay=%d background=%06x entities_background=%06x clear_rgb=%.3f,%.3f,%.3f entities_map='%s'", g_Config.m_ClOverlayEntities, g_Config.m_ClBackgroundColor, g_Config.m_ClBackgroundEntitiesColor, ClearColor.r, ClearColor.g, ClearColor.b, g_Config.m_ClBackgroundEntities);
			s_LastOverlayEntities = g_Config.m_ClOverlayEntities;
			s_LastBackgroundColor = g_Config.m_ClBackgroundColor;
			s_LastBackgroundEntitiesColor = g_Config.m_ClBackgroundEntitiesColor;
			str_copy(s_aLastBackgroundEntities, g_Config.m_ClBackgroundEntities, sizeof(s_aLastBackgroundEntities));
		}
	}
#endif
	Graphics()->Clear(ClearColor.r, ClearColor.g, ClearColor.b);

	// check if multi view got activated
	if(!m_MultiView.m_IsInit && m_MultiViewActivated)
	{
		int TeamId = 0;
		if(m_Snap.m_SpecInfo.m_SpectatorId >= 0)
			TeamId = m_Teams.Team(m_Snap.m_SpecInfo.m_SpectatorId);

		if(TeamId > MAX_CLIENTS || TeamId < 0)
			TeamId = 0;

		if(!InitMultiView(TeamId))
		{
			dbg_msg("MultiView", "No players found to spectate");
			ResetMultiView();
		}
	}

	// update the local character and spectate position
	{
		CPerfTimer StageTimer;
		UpdatePositions();
		LogPerfStage(this, "update_positions", StageTimer.ElapsedMs());
	}

	// display warnings
	if(m_Menus.CanDisplayWarning())
	{
		std::optional<SWarning> Warning = Graphics()->CurrentWarning();
		if(!Warning.has_value())
		{
			Warning = Client()->CurrentWarning();
		}
		if(Warning.has_value())
		{
			const SWarning &TheWarning = Warning.value();
			m_Menus.PopupWarning(TheWarning.m_aWarningTitle[0] == '\0' ? Localize("Warning") : TheWarning.m_aWarningTitle, TheWarning.m_aWarningMsg, Localize("Ok"), TheWarning.m_AutoHide ? 10s : 0s);
		}
	}

	// update camera data prior to CControls::OnRender to allow CControls::m_aTargetPos to compensate using camera data
	{
		CPerfTimer StageTimer;
		m_Camera.UpdateCamera();
		UpdateSpectatorCursor();
		LogPerfStage(this, "camera_and_cursor", StageTimer.ElapsedMs());
	}

	{
		CPerfTimer StageTimer;
		const char *pPerfPage = m_Menus.CurrentQmUiPerfPage();
		if(pPerfPage != nullptr)
			m_UiRuntimeV2.SetPerfContext(pPerfPage, m_Menus.CurrentQmUiPerfOperation());
		else
			m_UiRuntimeV2.ClearPerfContext();
		m_UiRuntimeV2.OnRender();
		LogPerfStage(this, "ui_runtime_v2", StageTimer.ElapsedMs());
	}

	// render all systems
	CPerfTimer ComponentsTimer;
	const auto RenderComponent = [&](CComponent *pComponent) {
		if(pComponent == &m_Menus)
		{
			CPerfTimer StageTimer;
			pComponent->OnRender();
			LogPerfStage(this, "component_menus", StageTimer.ElapsedMs());
		}
		else
		{
			pComponent->OnRender();
		}
	};
	if(g_Config.m_QmPerfStutterDiagnostics)
	{
		for(size_t i = 0; i < m_vpAll.size(); ++i)
		{
			CPerfTimer ComponentTimer;
			RenderComponent(m_vpAll[i]);
			RecordComponentRender(i, ComponentTimer.ElapsedMs());
		}
	}
	else
	{
		for(auto &pComponent : m_vpAll)
			RenderComponent(pComponent);
	}
	LogPerfStage(this, "components_total", ComponentsTimer.ElapsedMs());

	// clear all events/input for this frame
	{
		CPerfTimer StageTimer;
		m_QmImeManager.RenderCandidatePopup();
		m_QmImeManager.OnFrame();
		Input()->Clear();
		LogPerfStage(this, "input_clear_and_candidates", StageTimer.ElapsedMs());
	}

	const bool WasNewTick = m_NewTick;

	// clear new tick flags
	m_NewTick = false;
	m_NewPredictedTick = false;
	std::fill(std::begin(m_aConfirmedHammerHitEvent), std::end(m_aConfirmedHammerHitEvent), false);

	if(g_Config.m_ClDummy && !Client()->DummyConnected())
		g_Config.m_ClDummy = 0;

	LogPerfStage(this, "gameclient_onrender_total", FrameTimer.ElapsedMs());

	m_pFrameScheduler->EndFrame();
	if(QmPerfEnabled())
		LogQmIconDiagnostics(m_QmIconManager.TakeDiagnostics(), Client());

	// resend player and dummy info if it was filtered by server
	if(m_aLocalIds[0] >= 0 && Client()->State() == IClient::STATE_ONLINE && !m_Menus.IsActive() && WasNewTick)
	{
		if(m_aCheckInfo[0] == 0 && !m_TClient.IsFinishRenamePending(0))
		{
			if(m_pClient->IsSixup())
			{
				if(!GotWantedSkin7(false))
					SendSkinChange7(false);
				else
					m_aCheckInfo[0] = -1;
			}
			else
			{
				if(
					str_comp(m_aClients[m_aLocalIds[0]].m_aName, Client()->PlayerName()) ||
					str_comp(m_aClients[m_aLocalIds[0]].m_aClan, g_Config.m_PlayerClan) ||
					m_aClients[m_aLocalIds[0]].m_Country != g_Config.m_PlayerCountry ||
					str_comp(m_aClients[m_aLocalIds[0]].m_aSkinName, g_Config.m_ClPlayerSkin) ||
					m_aClients[m_aLocalIds[0]].m_UseCustomColor != g_Config.m_ClPlayerUseCustomColor ||
					m_aClients[m_aLocalIds[0]].m_ColorBody != (int)g_Config.m_ClPlayerColorBody ||
					m_aClients[m_aLocalIds[0]].m_ColorFeet != (int)g_Config.m_ClPlayerColorFeet)
					SendInfo(false);
				else
					m_aCheckInfo[0] = -1;
			}
		}

		if(m_aCheckInfo[0] > 0)
		{
			m_aCheckInfo[0] -= minimum(Client()->GameTick(0) - Client()->PrevGameTick(0), m_aCheckInfo[0]);
		}

		if(m_aLocalIds[1] >= 0)
		{
			if(m_aCheckInfo[1] == 0 && !m_TClient.IsFinishRenamePending(1))
			{
				if(m_pClient->IsSixup())
				{
					if(!GotWantedSkin7(true))
						SendSkinChange7(true);
					else
						m_aCheckInfo[1] = -1;
				}
				else
				{
					if(
						str_comp(m_aClients[m_aLocalIds[1]].m_aName, Client()->DummyName()) ||
						str_comp(m_aClients[m_aLocalIds[1]].m_aClan, g_Config.m_ClDummyClan) ||
						m_aClients[m_aLocalIds[1]].m_Country != g_Config.m_ClDummyCountry ||
						str_comp(m_aClients[m_aLocalIds[1]].m_aSkinName, g_Config.m_ClDummySkin) ||
						m_aClients[m_aLocalIds[1]].m_UseCustomColor != g_Config.m_ClDummyUseCustomColor ||
						m_aClients[m_aLocalIds[1]].m_ColorBody != (int)g_Config.m_ClDummyColorBody ||
						m_aClients[m_aLocalIds[1]].m_ColorFeet != (int)g_Config.m_ClDummyColorFeet)
						SendDummyInfo(false);
					else
						m_aCheckInfo[1] = -1;
				}
			}

			if(m_aCheckInfo[1] > 0)
			{
				m_aCheckInfo[1] -= minimum(Client()->GameTick(1) - Client()->PrevGameTick(1), m_aCheckInfo[1]);
			}
		}
	}

	UpdateManagedTeeRenderInfos();
}

void CGameClient::RecordComponentUpdate(size_t ComponentIndex, double DurationMs)
{
	if(ComponentIndex < m_vQmStutterPendingUpdateMs.size())
		m_vQmStutterPendingUpdateMs[ComponentIndex] += DurationMs;
}

void CGameClient::RecordComponentRender(size_t ComponentIndex, double DurationMs)
{
	if(ComponentIndex < m_vQmStutterPendingRenderMs.size())
		m_vQmStutterPendingRenderMs[ComponentIndex] += DurationMs;
}

void CGameClient::CaptureQmStutterFeatureSnapshot()
{
	m_vQmStutterFeatureSnapshot.clear();
	ConfigManager()->PossibleConfigVariables("", CFGFLAG_CLIENT, CollectQmStutterFeatureConfig, &m_vQmStutterFeatureSnapshot);
	std::sort(m_vQmStutterFeatureSnapshot.begin(), m_vQmStutterFeatureSnapshot.end());

	const char *pPage = m_Menus.CurrentQmUiPerfPage();
	const char *pOperation = m_Menus.CurrentQmUiPerfOperation();
	m_QmStutterPage = pPage != nullptr && pPage[0] != '\0' ? pPage : "game";
	m_QmStutterOperation = pOperation != nullptr && pOperation[0] != '\0' ? pOperation : "none";

	int ConfiguredLimit = 0;
	if(g_Config.m_GfxRefreshRate > 0)
		ConfiguredLimit = g_Config.m_GfxRefreshRate;
	if(g_Config.m_ClRefreshRate > 0 && (ConfiguredLimit == 0 || g_Config.m_ClRefreshRate < ConfiguredLimit))
		ConfiguredLimit = g_Config.m_ClRefreshRate;
	const int IdleLimit = m_Menus.IdleRenderFrameRate();
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	m_QmStutterLimitCause = QmDetermineStutterLimitCause(
		g_Config.m_GfxVsync != 0,
		ConfiguredLimit,
		g_Config.m_ClRefreshRateInactive,
		WindowActive,
		IdleLimit);
}

void CGameClient::ResetQmStutterWindowSamples()
{
	m_QmStutterFrameSamples.Reset();
	for(SQmStutterComponentWindowSamples &Samples : m_vQmStutterComponentSamples)
	{
		Samples.m_Update.Reset();
		Samples.m_Render.Reset();
	}
	m_vQmStutterFeatureSnapshot.clear();
	m_QmStutterWindowStartFrame = 0;
	m_QmStutterWorstFrame = 0;
	m_QmStutterWorstFrameMs = 0.0;
	m_QmStutterLimitCause = EQmStutterLimitCause::NONE;
	m_QmStutterPage.clear();
	m_QmStutterOperation.clear();
}

void CGameClient::FlushQmStutterWindow(const SQmStutterFrameDecision &Decision, bool ForceLog)
{
	if(!Decision.m_FlushWindow || m_QmStutterFrameSamples.Empty())
		return;

	const double SampleSeconds = m_QmStutterFrameSamples.Total() / 1000.0;
	const double FrameMsP95 = m_QmStutterFrameSamples.Percentile(95.0);
	const double FrameMsP99 = m_QmStutterFrameSamples.Percentile(99.0);
	const double FpsAverage = SampleSeconds > 0.0 ? m_QmStutterFrameSamples.Count() / SampleSeconds : 0.0;
	const double FpsMinimum = m_QmStutterFrameSamples.Max() > 0.0 ? 1000.0 / m_QmStutterFrameSamples.Max() : 0.0;
	const double FpsOnePctLow = FrameMsP99 > 0.0 ? 1000.0 / FrameMsP99 : 0.0;
	const bool CapLimited = m_QmStutterLimitCause != EQmStutterLimitCause::NONE;
	const char *pClassification = SampleSeconds >= 1.0 || Decision.m_Reason == EQmStutterFlushReason::PERIODIC ? "sustained_low" : "frame_drop";

	char aPayload[1024];
	str_format(aPayload, sizeof(aPayload),
		"schema=1 event=stutter_event stutter_id=%" PRIu64 " segment=%" PRIu64 " classification=%s end_reason=%s window_start_frame=%" PRIu64 " window_end_frame=%" PRIu64 " worst_frame=%" PRIu64 " target_fps=300 target_ms=%.6f sample_frames=%d sample_seconds=%.3f below_target_frames=%d fps_avg=%.3f fps_min=%.3f fps_1pct_low=%.3f frame_ms_avg=%.3f frame_ms_p95=%.3f frame_ms_p99=%.3f frame_ms_max=%.3f cap_limited=%d cap_reason=%s context=%s page=%s tab=%s",
		Decision.m_StutterId,
		Decision.m_Segment,
		pClassification,
		QmStutterFlushReasonName(Decision.m_Reason),
		m_QmStutterWindowStartFrame,
		m_QmStutterEpisodeTracker.LastBelowTargetFrame(),
		m_QmStutterWorstFrame,
		QmStutterFrameBudgetMs(),
		(int)m_QmStutterFrameSamples.Count(),
		SampleSeconds,
		(int)m_QmStutterFrameSamples.Count(),
		FpsAverage,
		FpsMinimum,
		FpsOnePctLow,
		m_QmStutterFrameSamples.Average(),
		FrameMsP95,
		FrameMsP99,
		m_QmStutterFrameSamples.Max(),
		CapLimited ? 1 : 0,
		QmStutterLimitCauseName(m_QmStutterLimitCause),
		Client()->State() == IClient::STATE_ONLINE ? "online" : "offline",
		m_QmStutterPage.empty() ? "game" : m_QmStutterPage.c_str(),
		m_QmStutterOperation.empty() ? "none" : m_QmStutterOperation.c_str());
	if(ForceLog)
		QmPerfLogPayloadForce("perf/stutter", aPayload, Client());
	else
		QmPerfLogPayload("perf/stutter", aPayload, Client());

	for(size_t i = 0; i < m_vQmStutterComponentSamples.size(); ++i)
	{
		const auto LogSamples = [&](const char *pCallback, const CQmStutterSampleSeries &Samples) {
			if(Samples.Empty())
				return;
			char aComponentPayload[512];
			str_format(aComponentPayload, sizeof(aComponentPayload),
				"schema=1 event=component_sample stutter_id=%" PRIu64 " segment=%" PRIu64 " scope=component owner=client module=%s callback=%s sample_count=%d total_ms=%.3f avg_ms=%.3f p95_ms=%.3f max_ms=%.3f max_frame=%" PRIu64,
				Decision.m_StutterId,
				Decision.m_Segment,
				m_vpAllPerfNames[i],
				pCallback,
				(int)Samples.Count(),
				Samples.Total(),
				Samples.Average(),
				Samples.Percentile(95.0),
				Samples.Max(),
				Samples.MaxFrame());
			if(ForceLog)
				QmPerfLogPayloadForce("perf/stutter", aComponentPayload, Client());
			else
				QmPerfLogPayload("perf/stutter", aComponentPayload, Client());
		};
		LogSamples("on_update", m_vQmStutterComponentSamples[i].m_Update);
		LogSamples("on_render", m_vQmStutterComponentSamples[i].m_Render);
	}

	for(const auto &[Name, Value] : m_vQmStutterFeatureSnapshot)
	{
		const char *pOwner = str_startswith(Name.c_str(), "qm_") != nullptr ? "qmclient" :
				     str_startswith(Name.c_str(), "tc_") != nullptr ? "tclient" :
										      "ddnet";
		char aFeaturePayload[512];
		str_format(aFeaturePayload, sizeof(aFeaturePayload),
			"schema=1 event=feature_snapshot stutter_id=%" PRIu64 " segment=%" PRIu64 " frame=%" PRIu64 " owner=%s feature=%s module=unmapped config_enabled=1 config_value=%d hud_visible=-1 settings_visible=-1 executed_in_frame=-1 executed_in_window=-1 current_page=%s current_tab=%s",
			Decision.m_StutterId,
			Decision.m_Segment,
			m_QmStutterWorstFrame,
			pOwner,
			Name.c_str(),
			Value,
			m_QmStutterPage.empty() ? "game" : m_QmStutterPage.c_str(),
			m_QmStutterOperation.empty() ? "none" : m_QmStutterOperation.c_str());
		if(ForceLog)
			QmPerfLogPayloadForce("perf/stutter", aFeaturePayload, Client());
		else
			QmPerfLogPayload("perf/stutter", aFeaturePayload, Client());
	}
}

void CGameClient::ProcessQmStutterFrame()
{
	const bool Enabled = g_Config.m_QmPerfStutterDiagnostics != 0;
	if(!Enabled)
	{
		if(m_QmStutterDiagnosticsWasEnabled)
		{
			const SQmStutterFrameDecision Decision = m_QmStutterEpisodeTracker.Flush(EQmStutterFlushReason::DISABLED);
			FlushQmStutterWindow(Decision, true);
			ResetQmStutterWindowSamples();
		}
		m_QmStutterDiagnosticsWasEnabled = false;
		std::fill(m_vQmStutterPendingUpdateMs.begin(), m_vQmStutterPendingUpdateMs.end(), 0.0);
		std::fill(m_vQmStutterPendingRenderMs.begin(), m_vQmStutterPendingRenderMs.end(), 0.0);
		return;
	}

	if(!m_QmStutterDiagnosticsWasEnabled)
	{
		m_QmStutterEpisodeTracker = CQmStutterEpisodeTracker();
		ResetQmStutterWindowSamples();
		m_QmStutterDiagnosticsWasEnabled = true;
	}

	const uint64_t FrameId = Client()->PerfFrame();
	const double FrameMs = Client()->RenderFrameTime() * 1000.0;
	const SQmStutterFrameDecision Decision = m_QmStutterEpisodeTracker.RecordFrame(FrameId, FrameMs);
	if(Decision.m_Started)
	{
		ResetQmStutterWindowSamples();
		m_QmStutterWindowStartFrame = FrameId;
	}

	if(Decision.m_BelowTarget)
	{
		if(m_QmStutterFrameSamples.Empty())
			m_QmStutterWindowStartFrame = FrameId;
		m_QmStutterFrameSamples.Record(FrameMs, FrameId);
		for(size_t i = 0; i < m_vQmStutterComponentSamples.size(); ++i)
		{
			m_vQmStutterComponentSamples[i].m_Update.Record(m_vQmStutterPendingUpdateMs[i], FrameId);
			m_vQmStutterComponentSamples[i].m_Render.Record(m_vQmStutterPendingRenderMs[i], FrameId);
		}
		if(FrameMs > m_QmStutterWorstFrameMs)
		{
			m_QmStutterWorstFrameMs = FrameMs;
			m_QmStutterWorstFrame = FrameId;
			CaptureQmStutterFeatureSnapshot();
		}
	}

	std::fill(m_vQmStutterPendingUpdateMs.begin(), m_vQmStutterPendingUpdateMs.end(), 0.0);
	std::fill(m_vQmStutterPendingRenderMs.begin(), m_vQmStutterPendingRenderMs.end(), 0.0);
	if(Decision.m_FlushWindow)
	{
		FlushQmStutterWindow(Decision, false);
		ResetQmStutterWindowSamples();
	}
}

void CGameClient::OnDummyDisconnect()
{
	m_aLocalIds[1] = -1;
	m_aDDRaceMsgSent[1] = false;
	m_aShowOthers[1] = SHOW_OTHERS_NOT_SET;
	m_aEnableSpectatorCount[1] = -1;
	m_aLastNewPredictedTick[1] = -1;
	m_aLastPredictedAirJumpTick[1] = -1;
	m_PredictedDummyId = -1;
	m_HammerHitTracker.Reset();
	m_vPendingHammerHitEvents.clear();
	std::fill(std::begin(m_aConfirmedHammerHitEvent), std::end(m_aConfirmedHammerHitEvent), false);
	m_FastPractice.InvalidateBufferedInputState();
}

void CGameClient::OnDummyManualDisconnect()
{
	m_QmAxiomAutoLogin.DisableDummyReconnectForServer();
}

int CGameClient::LastRaceTick() const
{
	return m_LastRaceTick;
}

int CGameClient::CurrentRaceTime() const
{
	if(m_LastRaceTick < 0)
	{
		return 0;
	}
	return (Client()->GameTick(g_Config.m_ClDummy) - m_LastRaceTick) / Client()->GameTickSpeed();
}

bool CGameClient::IsTeamPlay() const
{
	return m_Snap.m_pGameInfoObj &&
	       (m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_TEAMS) != 0;
}

bool CGameClient::IsWorldPaused() const
{
	return m_Snap.m_pGameInfoObj &&
	       (m_Snap.m_pGameInfoObj->m_GameStateFlags & (GAMESTATEFLAG_GAMEOVER | GAMESTATEFLAG_PAUSED)) != 0;
}

bool CGameClient::IsDemoPlaybackPaused() const
{
	return Client()->State() == IClient::STATE_DEMOPLAYBACK &&
	       DemoPlayer()->BaseInfo()->m_Paused;
}

float CGameClient::GetAnimationPlaybackSpeed() const
{
	if(IsWorldPaused() || IsDemoPlaybackPaused())
	{
		return 0.0f;
	}
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return DemoPlayer()->BaseInfo()->m_Speed;
	}
	return 1.0f;
}

bool CGameClient::Predict() const
{
	if(!g_Config.m_ClPredict && !m_FastPractice.Enabled())
		return false;

	if(m_Snap.m_pGameInfoObj)
	{
		if(m_Snap.m_pGameInfoObj->m_GameStateFlags & (GAMESTATEFLAG_GAMEOVER | GAMESTATEFLAG_PAUSED))
		{
			return false;
		}
	}

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return false;

	return !m_Snap.m_SpecInfo.m_Active && m_Snap.m_pLocalCharacter;
}

ColorRGBA CGameClient::GetDDTeamColor(int DDTeam, float Lightness) const
{
	// TClient
	if(g_Config.m_TcOldTeamColors)
		return color_cast<ColorRGBA>(ColorHSLA(DDTeam / 64.0f, 1.0f, Lightness));

	// Use golden angle to generate unique colors with distinct adjacent colors.
	// The first DDTeam (team 1) gets angle 0°, i.e. red hue.
	const float Hue = std::fmod((DDTeam - 1) * normalized_golden_angle, 1.0f);
	return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, Lightness));
}

void CGameClient::FormatClientId(int ClientId, char (&aClientId)[16], EClientIdFormat Format) const
{
	if(Format == EClientIdFormat::NO_INDENT)
	{
		str_format(aClientId, sizeof(aClientId), "%d", ClientId);
	}
	else
	{
		const int HighestClientId = Format == EClientIdFormat::INDENT_AUTO ? m_Snap.m_HighestClientId : 64;
		const char *pFigureSpace = " ";
		char aNumber[8];
		str_format(aNumber, sizeof(aNumber), "%d", ClientId);
		aClientId[0] = '\0';
		if(ClientId < 100 && HighestClientId >= 100)
		{
			str_append(aClientId, pFigureSpace);
		}
		if(ClientId < 10 && HighestClientId >= 10)
		{
			str_append(aClientId, pFigureSpace);
		}
		str_append(aClientId, aNumber);
	}
	str_append(aClientId, ": ");
}

bool CGameClient::IsLocalClientId(int ClientId) const
{
	return ClientId >= 0 && (ClientId == m_aLocalIds[0] || ClientId == m_aLocalIds[1]);
}

bool CGameClient::ShouldRunSkinChangeTransition(int ClientId) const
{
	if(ClientId < 0)
		return false;
	if(!ShouldRunLiveSkinChangeTransition(Client()->State() == IClient::STATE_DEMOPLAYBACK))
		return false;

	switch(std::clamp(g_Config.m_QmSkinChangeTransitionScope, QM_SKIN_CHANGE_TRANSITION_SCOPE_OWN, QM_SKIN_CHANGE_TRANSITION_SCOPE_ALL))
	{
	case QM_SKIN_CHANGE_TRANSITION_SCOPE_OWN:
		return ClientId == m_Snap.m_LocalClientId;
	case QM_SKIN_CHANGE_TRANSITION_SCOPE_LOCAL:
		return IsLocalClientId(ClientId);
	case QM_SKIN_CHANGE_TRANSITION_SCOPE_ALL:
		return true;
	default:
		return false;
	}
}

bool CGameClient::ShouldHideStreamerIdentity(int ClientId) const
{
	return g_Config.m_QmStreamerHideNames && ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       !IsLocalClientId(ClientId) && !m_aClients[ClientId].m_Friend;
}

bool CGameClient::ShouldHideStreamerSkin(int ClientId) const
{
	return g_Config.m_QmStreamerHideSkins && ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       !IsLocalClientId(ClientId) && !m_aClients[ClientId].m_Friend;
}

void CGameClient::FormatStreamerName(int ClientId, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;

	if(ShouldHideStreamerIdentity(ClientId))
	{
		str_format(pBuf, BufSize, "%d", ClientId);
	}
	else if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		str_copy(pBuf, m_aClients[ClientId].m_aName, BufSize);
	}
	else
	{
		pBuf[0] = '\0';
	}
}

void CGameClient::RenderQmMonitoringHud(float GraphX, float GraphSpacing)
{
	if(!g_Config.m_DbgGraphs)
		return;

	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(
		Graphics()->ScreenWidth(),
		Graphics()->ScreenHeight(),
		GraphX,
		GraphSpacing);
	m_QmMonitoring.RenderHud(Layout.m_PanelRect);
}

void CGameClient::FormatStreamerClan(int ClientId, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;

	if(ShouldHideStreamerIdentity(ClientId))
	{
		pBuf[0] = '\0';
		return;
	}

	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		str_copy(pBuf, m_aClients[ClientId].m_aClan, BufSize);
	else
		pBuf[0] = '\0';
}

namespace
{
	bool IsAsciiDigit(char c)
	{
		return c >= '0' && c <= '9';
	}

	void AppendVoteText(char *pBuf, int BufSize, int &Length, const char *pText, int TextLen)
	{
		if(!pBuf || BufSize <= 0 || Length >= BufSize - 1 || !pText || TextLen <= 0)
			return;

		const int CopyLen = minimum(TextLen, BufSize - 1 - Length);
		mem_copy(pBuf + Length, pText, CopyLen);
		Length += CopyLen;
		pBuf[Length] = '\0';
	}

	void AppendVoteText(char *pBuf, int BufSize, int &Length, const char *pText)
	{
		if(!pText)
			return;

		AppendVoteText(pBuf, BufSize, Length, pText, str_length(pText));
	}

	struct SVoteClientLabel
	{
		int m_ClientId = -1;
		const char *m_pName = nullptr;
		int m_NameLen = 0;
		int m_Consumed = 0;
	};

	bool ParseQuotedVoteClientLabel(const char *pText, SVoteClientLabel &Out)
	{
		if(!pText || *pText != '\'')
			return false;

		const char *pCursor = pText + 1;
		while(*pCursor == ' ')
			++pCursor;

		if(!IsAsciiDigit(*pCursor))
			return false;

		int ClientId = 0;
		do
		{
			ClientId = ClientId * 10 + (*pCursor - '0');
			++pCursor;
		} while(IsAsciiDigit(*pCursor));

		if(*pCursor != ':' || pCursor[1] != ' ')
			return false;

		pCursor += 2;
		const char *pQuoteEnd = str_find(pCursor, "'");
		if(!pQuoteEnd || pQuoteEnd == pCursor)
			return false;

		Out.m_ClientId = ClientId;
		Out.m_pName = pCursor;
		Out.m_NameLen = (int)(pQuoteEnd - pCursor);
		Out.m_Consumed = (int)(pQuoteEnd - pText) + 1;
		return true;
	}

	bool ParseFullVoteClientLabel(const char *pText, SVoteClientLabel &Out)
	{
		if(!pText)
			return false;

		const char *pCursor = pText;
		while(*pCursor == ' ')
			++pCursor;

		if(!IsAsciiDigit(*pCursor))
			return false;

		int ClientId = 0;
		do
		{
			ClientId = ClientId * 10 + (*pCursor - '0');
			++pCursor;
		} while(IsAsciiDigit(*pCursor));

		if(*pCursor != ':' || pCursor[1] != ' ')
			return false;

		pCursor += 2;
		if(*pCursor == '\0')
			return false;

		Out.m_ClientId = ClientId;
		Out.m_pName = pCursor;
		Out.m_NameLen = str_length(pCursor);
		Out.m_Consumed = str_length(pText);
		return true;
	}
}

void CGameClient::FormatStreamerVoteText(const char *pText, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;

	pBuf[0] = '\0';
	if(!pText)
		return;

	if(!g_Config.m_QmStreamerHideNames)
	{
		str_copy(pBuf, pText, BufSize);
		return;
	}

	SVoteClientLabel Label;
	if(ParseFullVoteClientLabel(pText, Label) && Label.m_ClientId >= 0 && Label.m_ClientId < MAX_CLIENTS &&
		ShouldHideStreamerIdentity(Label.m_ClientId) &&
		str_comp_num(m_aClients[Label.m_ClientId].m_aName, Label.m_pName, Label.m_NameLen) == 0 &&
		m_aClients[Label.m_ClientId].m_aName[Label.m_NameLen] == '\0')
	{
		str_format(pBuf, BufSize, "%d", Label.m_ClientId);
		return;
	}

	int Length = 0;
	for(const char *pCursor = pText; *pCursor != '\0';)
	{
		bool Replaced = false;
		SVoteClientLabel QuotedLabel;
		if(ParseQuotedVoteClientLabel(pCursor, QuotedLabel) &&
			QuotedLabel.m_ClientId >= 0 && QuotedLabel.m_ClientId < MAX_CLIENTS &&
			ShouldHideStreamerIdentity(QuotedLabel.m_ClientId) &&
			str_comp_num(m_aClients[QuotedLabel.m_ClientId].m_aName, QuotedLabel.m_pName, QuotedLabel.m_NameLen) == 0 &&
			m_aClients[QuotedLabel.m_ClientId].m_aName[QuotedLabel.m_NameLen] == '\0')
		{
			char aClientId[16];
			str_format(aClientId, sizeof(aClientId), "%d", QuotedLabel.m_ClientId);
			AppendVoteText(pBuf, BufSize, Length, "'");
			AppendVoteText(pBuf, BufSize, Length, aClientId);
			AppendVoteText(pBuf, BufSize, Length, "'");
			pCursor += QuotedLabel.m_Consumed;
			Replaced = true;
		}

		if(!Replaced && *pCursor == '\'')
		{
			const char *pQuoteEnd = str_find(pCursor + 1, "'");
			if(pQuoteEnd)
			{
				const int NameLen = (int)(pQuoteEnd - (pCursor + 1));
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
				{
					if(!ShouldHideStreamerIdentity(ClientId))
						continue;

					const char *pClientName = m_aClients[ClientId].m_aName;
					if(str_comp_num(pClientName, pCursor + 1, NameLen) != 0 || pClientName[NameLen] != '\0')
						continue;

					char aClientId[16];
					str_format(aClientId, sizeof(aClientId), "%d", ClientId);
					AppendVoteText(pBuf, BufSize, Length, "'");
					AppendVoteText(pBuf, BufSize, Length, aClientId);
					AppendVoteText(pBuf, BufSize, Length, "'");
					pCursor = pQuoteEnd + 1;
					Replaced = true;
					break;
				}
			}
		}

		if(Replaced)
			continue;

		AppendVoteText(pBuf, BufSize, Length, pCursor, 1);
		++pCursor;
	}
}

void CGameClient::PrepareInputForSend(int *pData, int Size, bool Dummy)
{
	m_FastPractice.PrepareInputForSend(pData, Size, Dummy);
}

void CGameClient::OnRelease()
{
	// release all systems
	for(auto &pComponent : m_vpAll)
		pComponent->OnRelease();
}

void CGameClient::OnMessage(int MsgId, CUnpacker *pUnpacker, int Conn, bool Dummy)
{
	// special messages
	static_assert((int)NETMSGTYPE_SV_TUNEPARAMS == (int)protocol7::NETMSGTYPE_SV_TUNEPARAMS, "0.6 and 0.7 tune message id do not match");
	if(MsgId == NETMSGTYPE_SV_TUNEPARAMS)
	{
		// unpack the new tuning
		CTuningParams NewTuning;

		// No jetpack on DDNet incompatible servers,
		// jetpack strength will be received by tune params
		NewTuning.m_JetpackStrength = 0;

		int *pParams = NewTuning.NetworkArray();
		for(int i = 0; i < CTuningParams::Num(); i++)
		{
			static_assert(offsetof(CTuningParams, m_LaserDamage) / sizeof(CTuneParam) == 30);
			if(i == 30 && Client()->IsSixup()) // laser_damage was removed in 0.7
			{
				continue;
			}

			const int Value = pUnpacker->GetInt();

			// check for unpacking errors
			if(pUnpacker->Error())
				break;

			pParams[i] = Value;
		}

		m_ServerMode = SERVERMODE_PURE;

		m_aReceivedTuning[Conn] = true;
		// apply new tuning
		m_aTuning[Conn] = NewTuning;
		return;
	}

	void *pRawMsg = TranslateGameMsg(&MsgId, pUnpacker, Conn);

	if(!pRawMsg)
	{
		// the 0.7 version of this error message is printed on translation
		// in sixup/translate_game.cpp
		if(!Client()->IsSixup())
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgId), MsgId, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
		}
		return;
	}

	if(MsgId == NETMSGTYPE_SV_CHANGEINFOCOOLDOWN)
	{
		CNetMsg_Sv_ChangeInfoCooldown *pMsg = (CNetMsg_Sv_ChangeInfoCooldown *)pRawMsg;
		m_aNextChangeInfo[Conn] = pMsg->m_WaitUntil;
		return;
	}

	if(MsgId == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId < 0 && pMsg->m_pMessage != nullptr)
		{
			m_TClient.HandleSwapCountdownMessage(pMsg->m_pMessage, Conn);
			m_Hud.HandleSpamProtectionMessage(pMsg->m_pMessage);
		}
	}

	if(Dummy)
	{
		if(MsgId == NETMSGTYPE_SV_CHAT && m_aLocalIds[0] >= 0 && m_aLocalIds[1] >= 0)
		{
			CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

			if((pMsg->m_Team == 1 && (m_aClients[m_aLocalIds[0]].m_Team != m_aClients[m_aLocalIds[1]].m_Team || m_Teams.Team(m_aLocalIds[0]) != m_Teams.Team(m_aLocalIds[1]))) || pMsg->m_Team > 1)
			{
				m_Chat.OnMessage(MsgId, pRawMsg);
			}
		}
		return; // no need of all that stuff for the dummy
	}

	// TODO: this should be done smarter
	for(auto &pComponent : m_vpAll)
		pComponent->OnMessage(MsgId, pRawMsg);

	if(MsgId == NETMSGTYPE_SV_READYTOENTER)
	{
		Client()->EnterGame(Conn);
	}
	else if(MsgId == NETMSGTYPE_SV_EMOTICON)
	{
		CNetMsg_Sv_Emoticon *pMsg = (CNetMsg_Sv_Emoticon *)pRawMsg;

		// apply
		m_aClients[pMsg->m_ClientId].m_Emoticon = pMsg->m_Emoticon;
		m_aClients[pMsg->m_ClientId].m_EmoticonStartTick = Client()->GameTick(Conn);
		m_aClients[pMsg->m_ClientId].m_EmoticonStartFraction = Client()->IntraGameTickSincePrev(Conn);
	}
	else if(MsgId == NETMSGTYPE_SV_SOUNDGLOBAL)
	{
		if(m_SuppressEvents)
			return;

		// don't enqueue pseudo-global sounds from demos (created by PlayAndRecord)
		CNetMsg_Sv_SoundGlobal *pMsg = (CNetMsg_Sv_SoundGlobal *)pRawMsg;
		if(pMsg->m_SoundId == SOUND_CTF_DROP || pMsg->m_SoundId == SOUND_CTF_RETURN ||
			pMsg->m_SoundId == SOUND_CTF_CAPTURE || pMsg->m_SoundId == SOUND_CTF_GRAB_EN ||
			pMsg->m_SoundId == SOUND_CTF_GRAB_PL)
		{
			if(g_Config.m_SndGame)
				m_Sounds.Enqueue(CSounds::CHN_GLOBAL, pMsg->m_SoundId);
		}
		else
		{
			if(g_Config.m_SndGame)
				m_Sounds.Play(CSounds::CHN_GLOBAL, pMsg->m_SoundId, 1.0f);
		}
	}
	else if(MsgId == NETMSGTYPE_SV_TEAMSSTATE || MsgId == NETMSGTYPE_SV_TEAMSSTATELEGACY)
	{
		unsigned int i;

		for(i = 0; i < MAX_CLIENTS; i++)
		{
			const int Team = pUnpacker->GetInt();
			if(!pUnpacker->Error() && Team >= TEAM_FLOCK && Team < NUM_DDRACE_TEAMS)
			{
				m_Teams.Team(i, Team);
			}
			else
			{
				m_Teams.Team(i, 0);
				break;
			}
		}

		if(i <= 16)
			m_Teams.m_IsDDRace16 = true;

		m_Ghost.m_AllowRestart = true;
		m_RaceDemo.m_AllowRestart = true;
	}
	else if(MsgId == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim < 0 || pMsg->m_Victim >= MAX_CLIENTS)
			return;
		QmHudMarkTeeDead(m_aClients[pMsg->m_Victim].m_HudFrozenTeeState, Client()->GameTick(Conn), Client()->PredGameTick(Conn));

		// reset character prediction
		if(!(m_GameWorld.m_WorldConfig.m_IsFNG && pMsg->m_Weapon == WEAPON_LASER))
		{
			m_CharOrder.GiveWeak(pMsg->m_Victim);
			if(CCharacter *pChar = m_GameWorld.GetCharacterById(pMsg->m_Victim))
				pChar->ResetPrediction();
			m_GameWorld.ReleaseHooked(pMsg->m_Victim);
		}

		// if we are spectating a static id set (team 0) and somebody killed, and its not a guy in solo, we remove them from the list
		// never remove players from the list if it is a pvp server
		if(IsMultiViewIdSet() && m_MultiViewTeam == 0 && m_aMultiViewId[pMsg->m_Victim] && !m_aClients[pMsg->m_Victim].m_Spec && !m_MultiView.m_Solo && !m_GameInfo.m_Pvp)
		{
			m_aMultiViewId[pMsg->m_Victim] = false;

			// if everyone of a team killed, we have no ids to spectate anymore, so we disable multi view
			if(!IsMultiViewIdSet())
			{
				ResetMultiView();
			}
			else
			{
				// the "main" tee killed, search a new one
				if(m_Snap.m_SpecInfo.m_SpectatorId == pMsg->m_Victim)
				{
					int NewClientId = FindFirstMultiViewId();
					if(NewClientId < MAX_CLIENTS && NewClientId >= 0)
					{
						CleanMultiViewId(NewClientId);
						m_aMultiViewId[NewClientId] = true;
						m_Spectator.Spectate(NewClientId);
					}
				}
			}
		}
	}
	else if(MsgId == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		const int GameTick = Client()->GameTick(Conn);
		const int PredictedGameTick = Client()->PredGameTick(Conn);

		// reset prediction
		std::vector<std::pair<int, int>> vStrongWeakSorted;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_Teams.Team(i) == pMsg->m_Team)
			{
				if(m_aClients[i].m_Active)
					QmHudMarkTeeDead(m_aClients[i].m_HudFrozenTeeState, GameTick, PredictedGameTick);
				if(CCharacter *pChar = m_GameWorld.GetCharacterById(i))
				{
					pChar->ResetPrediction();
					vStrongWeakSorted.emplace_back(i, pMsg->m_First == i ? MAX_CLIENTS : (pChar ? pChar->GetStrongWeakId() : 0));
				}
				m_GameWorld.ReleaseHooked(i);
			}
		}
		std::stable_sort(vStrongWeakSorted.begin(), vStrongWeakSorted.end(), [](auto &Left, auto &Right) { return Left.second > Right.second; });
		for(auto Id : vStrongWeakSorted)
		{
			m_CharOrder.GiveWeak(Id.first);
		}
	}
	else if(MsgId == NETMSGTYPE_SV_MAPSOUNDGLOBAL)
	{
		if(m_SuppressEvents)
			return;

		if(!g_Config.m_SndGame)
			return;

		CNetMsg_Sv_MapSoundGlobal *pMsg = (CNetMsg_Sv_MapSoundGlobal *)pRawMsg;
		m_MapSounds.Play(CSounds::CHN_GLOBAL, pMsg->m_SoundId);
	}
	else if(MsgId == NETMSGTYPE_SV_PREINPUT)
	{
		CNetMsg_Sv_PreInput *pMsg = (CNetMsg_Sv_PreInput *)pRawMsg;
		m_aClients[pMsg->m_Owner].m_aPreInputs[pMsg->m_IntendedTick % 200] = *pMsg;
	}
	else if(MsgId == NETMSGTYPE_SV_SAVECODE)
	{
		const CNetMsg_Sv_SaveCode *pMsg = (CNetMsg_Sv_SaveCode *)pRawMsg;
		OnSaveCodeNetMessage(pMsg);
	}
	else if(MsgId == NETMSGTYPE_SV_RECORD || MsgId == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = static_cast<CNetMsg_Sv_Record *>(pRawMsg);
		if(pMsg->m_ServerTimeBest > 0)
		{
			m_MapBestTimeSeconds = pMsg->m_ServerTimeBest / 100;
			m_MapBestTimeMillis = (pMsg->m_ServerTimeBest % 100) * 10;
		}
		else if(m_MapBestTimeSeconds == FinishTime::UNSET)
		{
			// some PvP mods based on DDNet accidentally send a best time of 0, despite having no finished races
		}
	}
	else if(MsgId == NETMSGTYPE_SV_MAPINFO)
	{
		CNetMsg_Sv_MapInfo *pMsg = static_cast<CNetMsg_Sv_MapInfo *>(pRawMsg);
		str_copy(m_aMapDescription, pMsg->m_pDescription);
	}
}

void CGameClient::OnClientBrandsMessage(CUnpacker *pUnpacker)
{
	const int Version = pUnpacker->GetInt();
	const int NumEntries = pUnpacker->GetInt();
	if(pUnpacker->Error() || Version != CLIENT_BRANDS_PROTOCOL_VERSION || NumEntries < 0 || NumEntries > MAX_CLIENTS)
		return;

	char aaClientBrandNames[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	EClientBrand aClientBrands[MAX_CLIENTS] = {};
	int NumValidEntries = 0;
	for(int Entry = 0; Entry < NumEntries; ++Entry)
	{
		const char *pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
		const EClientBrand Brand = ClientBrandFromInt(pUnpacker->GetInt());
		if(pUnpacker->Error())
			return;
		if(pName[0] == '\0' || Brand == EClientBrand::NONE)
			continue;

		bool Found = false;
		for(int Existing = 0; Existing < NumValidEntries; ++Existing)
		{
			if(str_comp(aaClientBrandNames[Existing], pName) == 0)
			{
				aClientBrands[Existing] = Brand;
				Found = true;
				break;
			}
		}
		if(Found || NumValidEntries >= MAX_CLIENTS)
			continue;

		str_copy(aaClientBrandNames[NumValidEntries], pName);
		aClientBrands[NumValidEntries] = Brand;
		++NumValidEntries;
	}

	mem_copy(m_aaClientBrandNames, aaClientBrandNames, sizeof(m_aaClientBrandNames));
	mem_copy(m_aClientBrands, aClientBrands, sizeof(m_aClientBrands));
}

bool CGameClient::OnDemoPlaybackMessage(int MsgId, CUnpacker *pUnpacker)
{
	if(MsgId == NETMSG_QM_DEMO_HUD_STATE)
	{
		const int PackedState = pUnpacker->GetInt();
		if(!pUnpacker->Error())
			UnpackDemoHudState(PackedState);
		return true;
	}

	if(MsgId == NETMSG_QM_DEMO_INPUT_STATE)
	{
		const void *pKeyStates = pUnpacker->GetRaw(DEMO_INPUT_KEY_STATE_SIZE);
		const int TargetX = pUnpacker->GetInt();
		const int TargetY = pUnpacker->GetInt();
		if(pKeyStates != nullptr && !pUnpacker->Error())
		{
			mem_copy(m_DemoInputPlaybackState.m_aKeyStates, pKeyStates, sizeof(m_DemoInputPlaybackState.m_aKeyStates));
			m_DemoInputPlaybackState.m_TargetX = TargetX;
			m_DemoInputPlaybackState.m_TargetY = TargetY;
			m_DemoInputPlaybackState.m_Valid = true;
		}
		return true;
	}

	if(MsgId == NETMSG_QM_DEMO_INPUT_WHEEL)
	{
		const int WheelMask = pUnpacker->GetInt();
		if(!pUnpacker->Error())
		{
			m_DemoInputPlaybackState.m_WheelMask = WheelMask & 0xf;
			++m_DemoInputPlaybackState.m_WheelSequence;
		}
		return true;
	}

	if(MsgId == NETMSG_QM_DEMO_GAMEPAD_STATE)
	{
		const int Valid = pUnpacker->GetInt();
		const int ButtonsLow = pUnpacker->GetInt();
		const int ButtonsHigh = pUnpacker->GetInt();
		float aAxes[6];
		for(float &Axis : aAxes)
			Axis = pUnpacker->GetInt() / 32767.0f;
		const int PlayerIndex = pUnpacker->GetInt();
		if(!pUnpacker->Error())
		{
			const bool ValidGamepad = Valid != 0;
			m_DemoInputPlaybackState.m_GamepadValid = ValidGamepad;
			// Valid == 0 时归一化为全释放、轴居中、玩家 0，避免伪造或异常数据带进回放。
			m_DemoInputPlaybackState.m_GamepadButtons = ValidGamepad ? (static_cast<uint32_t>(ButtonsLow & 0xffff) | (static_cast<uint32_t>(ButtonsHigh & 0xffff) << 16)) : 0;
			for(int i = 0; i < 6; ++i)
				m_DemoInputPlaybackState.m_aGamepadAxes[i] = ValidGamepad ? std::clamp(aAxes[i], -1.0f, 1.0f) : 0.0f;
			m_DemoInputPlaybackState.m_GamepadPlayerIndex = ValidGamepad ? std::clamp(PlayerIndex, 0, 2) : 0;
			m_DemoInputPlaybackState.m_Valid = true;
		}
		return true;
	}

	return false;
}

void CGameClient::ResetDemoPlaybackState()
{
	m_DemoHudPlaybackState = {};
	m_DemoInputPlaybackState = {};
	m_HammerHitTracker.Reset();
	m_vPendingHammerHitEvents.clear();
	for(auto &Client : m_aClients)
		Client.m_HudFrozenTeeState = {};
}

void CGameClient::OnStateChange(int NewState, int OldState)
{
	// reset everything when not already connected (to keep gathered stuff)
	if(NewState < IClient::STATE_ONLINE)
		OnReset();

	// then change the state
	for(auto &pComponent : m_vpAll)
		pComponent->OnStateChange(NewState, OldState);
}

void CGameClient::OnScreenshotTaken(CImageInfo &&Image)
{
	m_UiEffects.StartScreenshotAnimation(std::move(Image));
}

void CGameClient::OnShutdown()
{
	for(auto &pComponent : m_vpAll)
		pComponent->OnShutdown();

	m_UI.OnShutdown();
	m_QmIconManager.Shutdown();
	m_LocalServer.KillServer();
}

void CGameClient::OnEnterGame()
{
}

void CGameClient::OnGameOver()
{
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && g_Config.m_ClEditor == 0)
		Client()->AutoScreenshot_Start();
}

void CGameClient::OnStartGame()
{
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && !g_Config.m_ClAutoDemoOnConnect)
		Client()->DemoRecorder_HandleAutoStart();
	m_Statboard.OnReset();
}

void CGameClient::OnStartRound()
{
	// In GamePaused or GameOver state RoundStartTick is updated on each tick
	// hence no need to reset stats until player leaves GameOver
	// and it would be a mistake to reset stats after or during the pause
	m_Statboard.OnReset();

	// Restart automatic race demo recording
	m_RaceDemo.OnReset();
}

void CGameClient::OnFlagGrab(int TeamId)
{
	if(TeamId == TEAM_RED)
		m_aStats[m_Snap.m_pGameDataObj->m_FlagCarrierRed].m_FlagGrabs++;
	else
		m_aStats[m_Snap.m_pGameDataObj->m_FlagCarrierBlue].m_FlagGrabs++;
}

void CGameClient::OnWindowResize()
{
	for(auto &pComponent : m_vpAll)
		pComponent->OnWindowResize();

	Ui()->OnWindowResize();
	m_QmIconManager.RefreshForCurrentDpi();
}

void CGameClient::OnLanguageChange()
{
	// The actual language change is delayed because it
	// might require clearing the text render font atlas,
	// which would invalidate text that is currently drawn.
	m_LanguageChanged = true;
}

void CGameClient::HandleLanguageChanged()
{
	if(!m_LanguageChanged)
		return;
	if(g_Config.m_ClEditor)
		return;
	m_LanguageChanged = false;

	g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());

	TextRender()->SetFontLanguageVariant(g_Config.m_ClLanguagefile);
	m_Menus.InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::LANGUAGE_CHANGED);

	// Clear all text containers
	Client()->OnWindowResize();
}

void CGameClient::RenderShutdownMessage()
{
	const char *pMessage = nullptr;
	if(Client()->State() == IClient::STATE_QUITTING)
		pMessage = Localize("Quitting. Please wait…");
	else if(Client()->State() == IClient::STATE_RESTARTING)
		pMessage = Localize("Restarting. Please wait…");
	else
		dbg_assert_failed("Invalid client state for quitting message");

	// This function only gets called after the render loop has already terminated, so we have to call Swap manually.
	Graphics()->Clear(0.0f, 0.0f, 0.0f);
	Ui()->MapScreen();
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Ui()->DoLabel(Ui()->Screen(), pMessage, 16.0f, TEXTALIGN_MC);
	Graphics()->Swap();
	Graphics()->Clear(0.0f, 0.0f, 0.0f);
}

bool CGameClient::PrepareForShutdown(bool Force)
{
	return m_TClient.PrepareForShutdown(Force);
}

void CGameClient::ProcessDemoSnapshot(CSnapshot *pSnap)
{
	for(int Index = 0; Index < pSnap->NumItems(); Index++)
	{
		const CSnapshotItem *pItem = pSnap->GetItem(Index);
		int ItemType = pSnap->GetItemType(Index);

		if(ItemType == NETOBJTYPE_PROJECTILE)
		{
			// for antiping: if the projectile netobjects from the server contains extra data, this is removed and the original content restored before recording demo
			CNetObj_Projectile *pProj = (CNetObj_Projectile *)((void *)pItem->Data());
			DemoObjectRemoveExtraProjectileInfo(pProj);
		}
		else if(ItemType == NETOBJTYPE_DDNETSPECTATORINFO)
		{
			// always record local camera info as follow mode
			CNetObj_DDNetSpectatorInfo *pDDNetSpectatorInfo = (CNetObj_DDNetSpectatorInfo *)((void *)pItem->Data());
			pDDNetSpectatorInfo->m_HasCameraInfo = true;
			pDDNetSpectatorInfo->m_Zoom = (m_Camera.m_Zooming ? m_Camera.m_ZoomSmoothingTarget : m_Camera.BaseZoom()) * 1000.0f;
			pDDNetSpectatorInfo->m_Deadzone = m_Camera.Deadzone();
			pDDNetSpectatorInfo->m_FollowFactor = m_Camera.FollowFactor();
		}
	}
}

void CGameClient::OnRconType(bool UsernameReq)
{
	m_GameConsole.RequireUsername(UsernameReq);
}

void CGameClient::OnRconLine(const char *pLine)
{
	m_GameConsole.PrintLine(CGameConsole::CONSOLETYPE_REMOTE, pLine);
}

void CGameClient::ProcessEvents()
{
	m_vPendingHammerHitEvents.clear();
	std::fill(std::begin(m_aConfirmedHammerHitEvent), std::end(m_aConfirmedHammerHitEvent), false);
	if(m_SuppressEvents)
		return;

	int SnapType = IClient::SNAP_CURRENT;
	int Num = Client()->SnapNumItems(SnapType);
	for(int Index = 0; Index < Num; Index++)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(SnapType, Index);

		// TODO: We don't have enough info about us, others, to know a correct alpha or volume value.
		const float Alpha = 1.0f;
		const float Volume = 1.0f;

		if(Item.m_Type == NETEVENTTYPE_DAMAGEIND)
		{
			const CNetEvent_DamageInd *pEvent = (const CNetEvent_DamageInd *)Item.m_pData;

			vec2 DamageIndPos = vec2(pEvent->m_X, pEvent->m_Y);
			if(!m_PredictedWorld.CheckPredictedEventHandled(CGameWorld::CPredictedEvent(Item.m_Type, DamageIndPos, -1, Client()->GameTick(g_Config.m_ClDummy), pEvent->m_Angle)))
			{
				m_Effects.DamageIndicator(vec2(pEvent->m_X, pEvent->m_Y), direction(pEvent->m_Angle / 256.0f), Alpha);
			}
		}
		else if(Item.m_Type == NETEVENTTYPE_EXPLOSION)
		{
			const CNetEvent_Explosion *pEvent = (const CNetEvent_Explosion *)Item.m_pData;

			vec2 ExplosionPos = vec2(pEvent->m_X, pEvent->m_Y);
			if(!m_PredictedWorld.CheckPredictedEventHandled(CGameWorld::CPredictedEvent(Item.m_Type, ExplosionPos, -1, Client()->GameTick(g_Config.m_ClDummy))))
			{
				const float ExplosionAlpha = QmKnownOwnerEventAlpha(this, QmInferExplosionOwner(this, ExplosionPos));
				m_Effects.Explosion(ExplosionPos, ExplosionAlpha);
			}
		}
		else if(Item.m_Type == NETEVENTTYPE_HAMMERHIT)
		{
			const CNetEvent_HammerHit *pEvent = (const CNetEvent_HammerHit *)Item.m_pData;
			const vec2 HammerHitPos(pEvent->m_X, pEvent->m_Y);
			bool RenderEffect = true;
			m_vPendingHammerHitEvents.push_back({HammerHitPos,
				Client()->GameTick(g_Config.m_ClDummy),
				g_Config.m_ClDummy,
				Index,
				RenderEffect});
		}
		else if(Item.m_Type == NETEVENTTYPE_BIRTHDAY)
		{
			const CNetEvent_Birthday *pEvent = (const CNetEvent_Birthday *)Item.m_pData;
			m_Effects.Confetti(vec2(pEvent->m_X, pEvent->m_Y), Alpha);
		}
		else if(Item.m_Type == NETEVENTTYPE_FINISH)
		{
			const CNetEvent_Finish *pEvent = (const CNetEvent_Finish *)Item.m_pData;
			m_Effects.Confetti(vec2(pEvent->m_X, pEvent->m_Y), Alpha);
		}
		else if(Item.m_Type == NETEVENTTYPE_SPAWN)
		{
			m_SpawnEventsProcessed++;
			const CNetEvent_Spawn *pEvent = (const CNetEvent_Spawn *)Item.m_pData;
			m_SpawnEffectsDispatched += m_Effects.PlayerSpawn(vec2(pEvent->m_X, pEvent->m_Y), Alpha, Volume);
		}
		else if(Item.m_Type == NETEVENTTYPE_DEATH)
		{
			const CNetEvent_Death *pEvent = (const CNetEvent_Death *)Item.m_pData;
			m_Effects.PlayerDeath(vec2(pEvent->m_X, pEvent->m_Y), pEvent->m_ClientId, Alpha);
		}
		else if(Item.m_Type == NETEVENTTYPE_SOUNDWORLD)
		{
			const CNetEvent_SoundWorld *pEvent = (const CNetEvent_SoundWorld *)Item.m_pData;
			if(Client()->IsSixup() && pEvent->m_SoundId == SOUND_PLAYER_AIRJUMP)
			{
				m_Effects.AirJump(vec2(pEvent->m_X, pEvent->m_Y), Alpha, Volume);
				continue;
			}

			if(!Config()->m_SndGame)
				continue;

			const bool FocusMode = g_Config.m_QmFocusMode != 0;
			if(pEvent->m_SoundId == SOUND_PLAYER_JUMP && !ShouldPlayFocusJumpSound(FocusMode, g_Config.m_QmFocusModeMuteJumpSounds != 0, Config()->m_SndGame))
				continue;
			if(pEvent->m_SoundId == SOUND_PLAYER_DIE && !ShouldPlayFocusDeathOrSpawnSound(FocusMode, g_Config.m_QmFocusModeMuteDeathSounds != 0, Config()->m_SndGame))
				continue;

			if(m_GameInfo.m_RaceSounds && ((pEvent->m_SoundId == SOUND_GUN_FIRE && !g_Config.m_SndGun) || (pEvent->m_SoundId == SOUND_PLAYER_PAIN_LONG && !g_Config.m_SndLongPain)))
				continue;

			vec2 SoundPos = vec2(pEvent->m_X, pEvent->m_Y);
			if(!m_PredictedWorld.CheckPredictedEventHandled(CGameWorld::CPredictedEvent(Item.m_Type, SoundPos, -1, Client()->GameTick(g_Config.m_ClDummy), pEvent->m_SoundId)))
			{
				m_Sounds.PlayAt(CSounds::CHN_WORLD, pEvent->m_SoundId, 1.0f, SoundPos);
			}
		}
		else if(Item.m_Type == NETEVENTTYPE_MAPSOUNDWORLD)
		{
			CNetEvent_MapSoundWorld *pEvent = (CNetEvent_MapSoundWorld *)Item.m_pData;
			if(!Config()->m_SndGame)
				continue;

			m_MapSounds.PlayAt(CSounds::CHN_WORLD, pEvent->m_SoundId, vec2(pEvent->m_X, pEvent->m_Y));
		}
	}
}

void CGameClient::FinalizeHammerHitEvents()
{
	for(const SPendingHammerHitEvent &Event : m_vPendingHammerHitEvents)
	{
		const SQmHammerHitMatch Match = QmInferHammerHit(this, Event.m_Pos, Event.m_SnapshotTick);
		bool TargetWoke = false;
		if(Match.m_TargetId >= 0 && Match.m_TargetId < MAX_CLIENTS)
		{
			const CSnapState::CCharacterInfo &Character = m_Snap.m_aCharacters[Match.m_TargetId];
			TargetWoke = Character.m_HasExtendedData && Character.m_pPrevExtendedData != nullptr &&
				     QmIsHammerWakeupTransition(Character.m_pPrevExtendedData->m_FreezeEnd, Character.m_ExtendedData.m_FreezeEnd, Event.m_SnapshotTick);
		}

		const SQmHammerHitRecord Hit = {
			Match.m_AttackerId,
			Match.m_TargetId,
			Event.m_SnapshotTick,
			Event.m_EventOrdinal,
			Event.m_Pos,
			Event.m_Connection,
			TargetWoke};
		if((IsLocalClientId(Hit.m_AttackerId) || IsLocalClientId(Hit.m_TargetId)) && m_HammerHitTracker.Record(Hit))
			HandleConfirmedHammerHit(Hit);

		const bool PredictedHandled = Match.m_AttackerId >= 0 && Match.m_TargetId >= 0 && m_PredictedWorld.CheckPredictedHammerHitHandled(CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, Event.m_Pos, Match.m_AttackerId, Event.m_SnapshotTick, Match.m_TargetId));
		if(Event.m_RenderEffect && !PredictedHandled)
		{
			const float HammerHitAlpha = QmKnownOwnerEventAlpha(this, Match.m_AttackerId);
			m_Effects.HammerHit(Event.m_Pos, HammerHitAlpha, 1.0f);
		}
	}
	m_vPendingHammerHitEvents.clear();
}

static CGameInfo GetGameInfo(const CNetObj_GameInfoEx *pInfoEx, int InfoExSize, const CServerInfo *pFallbackServerInfo)
{
	int Version = -1;
	if(InfoExSize >= 12)
	{
		Version = pInfoEx->m_Version;
	}
	else if(InfoExSize >= 8)
	{
		Version = minimum(pInfoEx->m_Version, 4);
	}
	else if(InfoExSize >= 4)
	{
		Version = 0;
	}
	int Flags = 0;
	if(Version >= 0)
	{
		Flags = pInfoEx->m_Flags;
	}
	int Flags2 = 0;
	if(Version >= 5)
	{
		Flags2 = pInfoEx->m_Flags2;
	}
	bool Race;
	bool FastCap;
	bool FNG;
	bool DDRace;
	bool DDNet;
	bool BlockWorlds;
	bool City;
	bool Vanilla;
	bool Plus;
	bool FDDrace;
	if(Version < 1)
	{
		// The game type is intentionally only available inside this
		// `if`. Game type sniffing should be avoided and ideally not
		// extended. Mods should set the relevant game flags instead.
		const char *pGameType = pFallbackServerInfo->m_aGameType;
		Race = str_find_nocase(pGameType, "race") || str_find_nocase(pGameType, "fastcap");
		FastCap = str_find_nocase(pGameType, "fastcap");
		FNG = str_find_nocase(pGameType, "fng");
		DDRace = str_find_nocase(pGameType, "ddrace") || str_find_nocase(pGameType, "mkrace");
		DDNet = str_find_nocase(pGameType, "ddracenet") || str_find_nocase(pGameType, "ddnet");
		BlockWorlds = str_startswith(pGameType, "bw  ") || str_comp_nocase(pGameType, "bw") == 0;
		City = str_find_nocase(pGameType, "city");
		Vanilla = str_comp(pGameType, "DM") == 0 || str_comp(pGameType, "TDM") == 0 || str_comp(pGameType, "CTF") == 0;
		Plus = str_find(pGameType, "+");
		FDDrace = false;
	}
	else
	{
		Race = Flags & GAMEINFOFLAG_GAMETYPE_RACE;
		FastCap = Flags & GAMEINFOFLAG_GAMETYPE_FASTCAP;
		FNG = Flags & GAMEINFOFLAG_GAMETYPE_FNG;
		DDRace = Flags & GAMEINFOFLAG_GAMETYPE_DDRACE;
		DDNet = Flags & GAMEINFOFLAG_GAMETYPE_DDNET;
		BlockWorlds = Flags & GAMEINFOFLAG_GAMETYPE_BLOCK_WORLDS;
		Vanilla = Flags & GAMEINFOFLAG_GAMETYPE_VANILLA;
		Plus = Flags & GAMEINFOFLAG_GAMETYPE_PLUS;
		City = Version >= 5 && Flags2 & GAMEINFOFLAG2_GAMETYPE_CITY;
		FDDrace = Version >= 6 && Flags2 & GAMEINFOFLAG2_GAMETYPE_FDDRACE;

		// Ensure invariants upheld by the server info parsing business.
		DDRace = DDRace || DDNet || FDDrace;
		Race = Race || FastCap || DDRace;
	}

	CGameInfo Info;
	Info.m_FlagStartsRace = FastCap;
	Info.m_TimeScore = Race;
	Info.m_UnlimitedAmmo = Race;
	Info.m_DDRaceRecordMessage = DDRace && !DDNet;
	Info.m_RaceRecordMessage = DDNet || (Race && !DDRace);
	Info.m_RaceSounds = DDRace || FNG || BlockWorlds;
	Info.m_AllowEyeWheel = DDRace || BlockWorlds || City || Plus;
	Info.m_AllowHookColl = DDRace;
	Info.m_AllowZoom = Race || BlockWorlds || City;
	Info.m_BugDDRaceGhost = DDRace;
	Info.m_BugDDRaceInput = DDRace;
	Info.m_BugFNGLaserRange = FNG;
	Info.m_BugVanillaBounce = Vanilla;
	Info.m_PredictFNG = FNG;
	Info.m_PredictDDRace = DDRace;
	Info.m_PredictDDRaceTiles = DDRace && !BlockWorlds;
	Info.m_PredictVanilla = Vanilla || FastCap;
	Info.m_EntitiesDDNet = DDNet;
	Info.m_EntitiesDDRace = DDRace;
	Info.m_EntitiesRace = Race;
	Info.m_EntitiesFNG = FNG;
	Info.m_EntitiesVanilla = Vanilla;
	Info.m_EntitiesBW = BlockWorlds;
	Info.m_Race = Race;
	Info.m_Pvp = !Race;
	Info.m_DontMaskEntities = !DDNet;
	Info.m_AllowXSkins = false;
	Info.m_EntitiesFDDrace = FDDrace;
	Info.m_HudHealthArmor = true;
	Info.m_HudAmmo = true;
	Info.m_HudDDRace = false;
	Info.m_NoWeakHookAndBounce = false;
	Info.m_NoSkinChangeForFrozen = false;
	Info.m_DDRaceTeam = false;
	Info.m_PredictEvents = Vanilla;

	if(Version >= 0)
	{
		Info.m_TimeScore = Flags & GAMEINFOFLAG_TIMESCORE;
	}
	if(Version >= 2)
	{
		Info.m_FlagStartsRace = Flags & GAMEINFOFLAG_FLAG_STARTS_RACE;
		Info.m_UnlimitedAmmo = Flags & GAMEINFOFLAG_UNLIMITED_AMMO;
		Info.m_DDRaceRecordMessage = Flags & GAMEINFOFLAG_DDRACE_RECORD_MESSAGE;
		Info.m_RaceRecordMessage = Flags & GAMEINFOFLAG_RACE_RECORD_MESSAGE;
		Info.m_AllowEyeWheel = Flags & GAMEINFOFLAG_ALLOW_EYE_WHEEL;
		Info.m_AllowHookColl = Flags & GAMEINFOFLAG_ALLOW_HOOK_COLL;
		Info.m_AllowZoom = Flags & GAMEINFOFLAG_ALLOW_ZOOM;
		Info.m_BugDDRaceGhost = Flags & GAMEINFOFLAG_BUG_DDRACE_GHOST;
		Info.m_BugDDRaceInput = Flags & GAMEINFOFLAG_BUG_DDRACE_INPUT;
		Info.m_BugFNGLaserRange = Flags & GAMEINFOFLAG_BUG_FNG_LASER_RANGE;
		Info.m_BugVanillaBounce = Flags & GAMEINFOFLAG_BUG_VANILLA_BOUNCE;
		Info.m_PredictFNG = Flags & GAMEINFOFLAG_PREDICT_FNG;
		Info.m_PredictDDRace = Flags & GAMEINFOFLAG_PREDICT_DDRACE;
		Info.m_PredictDDRaceTiles = Flags & GAMEINFOFLAG_PREDICT_DDRACE_TILES;
		Info.m_PredictVanilla = Flags & GAMEINFOFLAG_PREDICT_VANILLA;
		Info.m_EntitiesDDNet = Flags & GAMEINFOFLAG_ENTITIES_DDNET;
		Info.m_EntitiesDDRace = Flags & GAMEINFOFLAG_ENTITIES_DDRACE;
		Info.m_EntitiesRace = Flags & GAMEINFOFLAG_ENTITIES_RACE;
		Info.m_EntitiesFNG = Flags & GAMEINFOFLAG_ENTITIES_FNG;
		Info.m_EntitiesVanilla = Flags & GAMEINFOFLAG_ENTITIES_VANILLA;
	}
	if(Version >= 3)
	{
		Info.m_Race = Flags & GAMEINFOFLAG_RACE;
		Info.m_DontMaskEntities = Flags & GAMEINFOFLAG_DONT_MASK_ENTITIES;
	}
	if(Version >= 4)
	{
		Info.m_EntitiesBW = Flags & GAMEINFOFLAG_ENTITIES_BW;
	}
	if(Version >= 5)
	{
		Info.m_AllowXSkins = Flags2 & GAMEINFOFLAG2_ALLOW_X_SKINS;
	}
	if(Version >= 6)
	{
		Info.m_EntitiesFDDrace = Flags2 & GAMEINFOFLAG2_ENTITIES_FDDRACE;
	}
	if(Version >= 7)
	{
		Info.m_HudHealthArmor = Flags2 & GAMEINFOFLAG2_HUD_HEALTH_ARMOR;
		Info.m_HudAmmo = Flags2 & GAMEINFOFLAG2_HUD_AMMO;
		Info.m_HudDDRace = Flags2 & GAMEINFOFLAG2_HUD_DDRACE;
	}
	if(Version >= 8)
	{
		Info.m_NoWeakHookAndBounce = Flags2 & GAMEINFOFLAG2_NO_WEAK_HOOK;
	}
	if(Version >= 9)
	{
		Info.m_NoSkinChangeForFrozen = Flags2 & GAMEINFOFLAG2_NO_SKIN_CHANGE_FOR_FROZEN;
	}
	if(Version >= 10)
	{
		Info.m_DDRaceTeam = Flags2 & GAMEINFOFLAG2_DDRACE_TEAM;
	}
	if(Version >= 11)
	{
		Info.m_PredictEvents = Flags2 & GAMEINFOFLAG2_PREDICT_EVENTS;
	}
	// TClient
	str_copy(Info.m_aGameType, pFallbackServerInfo->m_aGameType);

	return Info;
}

void CGameClient::InvalidateSnapshot()
{
	// clear all pointers
	mem_zero(&m_Snap, sizeof(m_Snap));
	m_Snap.m_SpecInfo.m_Zoom = 1.0f;
	m_Snap.m_LocalClientId = -1;
	m_vSnapEntities.clear();
	if(m_pClient != nullptr && (m_pClient->State() == IClient::STATE_ONLINE || m_pClient->State() == IClient::STATE_DEMOPLAYBACK))
		SnapCollectEntities();
}

void CGameClient::OnNewSnapshot()
{
	auto &&Evolve = [this](CNetObj_Character *pCharacter, int Tick) {
		CWorldCore TempWorld;
		CCharacterCore TempCore = CCharacterCore();
		CTeamsCore TempTeams = CTeamsCore();
		TempCore.Init(&TempWorld, Collision(), &TempTeams);
		TempCore.Read(pCharacter);
		TempCore.m_ActiveWeapon = pCharacter->m_Weapon;

		while(pCharacter->m_Tick < Tick)
		{
			pCharacter->m_Tick++;
			TempCore.Tick(false);
			TempCore.Move();
			TempCore.Quantize();
		}

		TempCore.Write(pCharacter);
	};

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		const int DemoTick = Client()->GameTick(g_Config.m_ClDummy);
		if(m_LastDemoPlaybackStateTick != -1 && DemoTick <= m_LastDemoPlaybackStateTick)
			ResetDemoPlaybackState();
		m_LastDemoPlaybackStateTick = DemoTick;
	}
	else
	{
		m_LastDemoPlaybackStateTick = -1;
	}

	InvalidateSnapshot();

	m_NewTick = true;

	ProcessEvents();

	if(g_Config.m_DbgStress)
	{
		if((Client()->GameTick(g_Config.m_ClDummy) % 100) == 0)
		{
			char aMessage[64];
			int MsgLen = rand() % (sizeof(aMessage) - 1);
			for(int i = 0; i < MsgLen; i++)
				aMessage[i] = (char)('a' + (rand() % ('z' - 'a')));
			aMessage[MsgLen] = 0;

			m_Chat.SendChat(rand() & 1, aMessage);
		}
	}

	CServerInfo ServerInfo;
	Client()->GetServerInfo(&ServerInfo);

	bool FoundGameInfoEx = false;
	bool GotSwitchStateTeam = false;
	bool HasUnsetDDNetFinishTimes = false;
	bool HasTrueMillisecondFinishTimes = false;
	m_aSwitchStateTeam[g_Config.m_ClDummy] = -1;

	for(auto &Client : m_aClients)
	{
		Client.m_SpecCharPresent = false;
	}

	// go through all the items in the snapshot and gather the info we want
	{
		m_Snap.m_aTeamSize[TEAM_RED] = m_Snap.m_aTeamSize[TEAM_BLUE] = 0;

		int Num = Client()->SnapNumItems(IClient::SNAP_CURRENT);
		for(int i = 0; i < Num; i++)
		{
			const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, i);

			if(Item.m_Type == NETOBJTYPE_CLIENTINFO)
			{
				const CNetObj_ClientInfo *pInfo = (const CNetObj_ClientInfo *)Item.m_pData;
				int ClientId = Item.m_Id;
				if(ClientId < MAX_CLIENTS)
				{
					CClientData *pClient = &m_aClients[ClientId];

					if(!IntsToStr(pInfo->m_aName, std::size(pInfo->m_aName), pClient->m_aName, std::size(pClient->m_aName)))
					{
						str_copy(pClient->m_aName, "nameless tee");
					}
					IntsToStr(pInfo->m_aClan, std::size(pInfo->m_aClan), pClient->m_aClan, std::size(pClient->m_aClan));
					pClient->m_Country = pInfo->m_Country;

					IntsToStr(pInfo->m_aSkin, std::size(pInfo->m_aSkin), pClient->m_aSkinName, std::size(pClient->m_aSkinName));
					if(!CSkin::IsValidName(pClient->m_aSkinName) ||
						(!m_GameInfo.m_AllowXSkins && CSkins::IsSpecialSkin(pClient->m_aSkinName)))
					{
						str_copy(pClient->m_aSkinName, "default");
					}

					pClient->m_UseCustomColor = pInfo->m_UseCustomColor;
					pClient->m_ColorBody = pInfo->m_ColorBody;
					pClient->m_ColorFeet = pInfo->m_ColorFeet;
				}
			}
			else if(Item.m_Type == NETOBJTYPE_PLAYERINFO)
			{
				const CNetObj_PlayerInfo *pInfo = (const CNetObj_PlayerInfo *)Item.m_pData;

				if(pInfo->m_ClientId < MAX_CLIENTS && pInfo->m_ClientId == Item.m_Id)
				{
					m_aClients[pInfo->m_ClientId].m_Team = pInfo->m_Team;
					m_aClients[pInfo->m_ClientId].m_Active = true;
					m_Snap.m_apPlayerInfos[pInfo->m_ClientId] = pInfo;
					m_Snap.m_apPrevPlayerInfos[pInfo->m_ClientId] = static_cast<const CNetObj_PlayerInfo *>(Client()->SnapFindItem(IClient::SNAP_PREV, Item.m_Type, pInfo->m_ClientId));
					m_Snap.m_NumPlayers++;

					if(pInfo->m_Local)
					{
						m_Snap.m_LocalClientId = pInfo->m_ClientId;
						m_Snap.m_pLocalInfo = pInfo;

						if(pInfo->m_Team == TEAM_SPECTATORS)
						{
							m_Snap.m_SpecInfo.m_Active = true;
						}
					}

					m_Snap.m_HighestClientId = maximum(m_Snap.m_HighestClientId, pInfo->m_ClientId);

					// calculate team-balance
					if(pInfo->m_Team != TEAM_SPECTATORS)
					{
						m_Snap.m_aTeamSize[pInfo->m_Team]++;
						if(!m_aStats[pInfo->m_ClientId].IsActive())
							m_aStats[pInfo->m_ClientId].JoinGame(Client()->GameTick(g_Config.m_ClDummy));
					}
					else if(m_aStats[pInfo->m_ClientId].IsActive())
					{
						m_aStats[pInfo->m_ClientId].JoinSpec(Client()->GameTick(g_Config.m_ClDummy));
					}
				}
			}
			else if(Item.m_Type == NETOBJTYPE_DDNETPLAYER)
			{
				m_ReceivedDDNetPlayer = true;
				const CNetObj_DDNetPlayer *pInfo = (const CNetObj_DDNetPlayer *)Item.m_pData;
				if(Item.m_Id < MAX_CLIENTS)
				{
					m_aClients[Item.m_Id].m_AuthLevel = pInfo->m_AuthLevel;
					m_aClients[Item.m_Id].m_Afk = pInfo->m_Flags & EXPLAYERFLAG_AFK;
					m_aClients[Item.m_Id].m_Paused = pInfo->m_Flags & EXPLAYERFLAG_PAUSED;
					m_aClients[Item.m_Id].m_Spec = pInfo->m_Flags & EXPLAYERFLAG_SPEC;
					m_aClients[Item.m_Id].m_FinishTimeSeconds = pInfo->m_FinishTimeSeconds;
					m_aClients[Item.m_Id].m_FinishTimeMillis = pInfo->m_FinishTimeMillis;

					if(m_aClients[Item.m_Id].m_FinishTimeSeconds == FinishTime::UNSET)
						HasUnsetDDNetFinishTimes = true;
					else if(m_aClients[Item.m_Id].m_FinishTimeMillis % 10 != 0)
						HasTrueMillisecondFinishTimes = true;

					if(Item.m_Id == m_Snap.m_LocalClientId && (m_aClients[Item.m_Id].m_Paused || m_aClients[Item.m_Id].m_Spec))
					{
						m_Snap.m_SpecInfo.m_Active = true;
					}
				}
			}
			else if(Item.m_Type == NETOBJTYPE_CHARACTER)
			{
				if(Item.m_Id < MAX_CLIENTS)
				{
					const void *pOld = Client()->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_CHARACTER, Item.m_Id);
					m_Snap.m_aCharacters[Item.m_Id].m_Cur = *((const CNetObj_Character *)Item.m_pData);
					if(pOld)
					{
						m_Snap.m_aCharacters[Item.m_Id].m_Active = true;
						m_Snap.m_aCharacters[Item.m_Id].m_Prev = *((const CNetObj_Character *)pOld);

						// limit evolving to 3 seconds
						bool EvolvePrev = Client()->PrevGameTick(g_Config.m_ClDummy) - m_Snap.m_aCharacters[Item.m_Id].m_Prev.m_Tick <= 3 * Client()->GameTickSpeed();
						bool EvolveCur = Client()->GameTick(g_Config.m_ClDummy) - m_Snap.m_aCharacters[Item.m_Id].m_Cur.m_Tick <= 3 * Client()->GameTickSpeed();

						// reuse the result from the previous evolve if the snapped character didn't change since the previous snapshot
						if(EvolveCur && m_aClients[Item.m_Id].m_Evolved.m_Tick == Client()->PrevGameTick(g_Config.m_ClDummy))
						{
							if(mem_comp(&m_Snap.m_aCharacters[Item.m_Id].m_Prev, &m_aClients[Item.m_Id].m_Snapped, sizeof(CNetObj_Character)) == 0)
								m_Snap.m_aCharacters[Item.m_Id].m_Prev = m_aClients[Item.m_Id].m_Evolved;
							if(mem_comp(&m_Snap.m_aCharacters[Item.m_Id].m_Cur, &m_aClients[Item.m_Id].m_Snapped, sizeof(CNetObj_Character)) == 0)
								m_Snap.m_aCharacters[Item.m_Id].m_Cur = m_aClients[Item.m_Id].m_Evolved;
						}

						if(EvolvePrev && m_Snap.m_aCharacters[Item.m_Id].m_Prev.m_Tick)
							Evolve(&m_Snap.m_aCharacters[Item.m_Id].m_Prev, Client()->PrevGameTick(g_Config.m_ClDummy));
						if(EvolveCur && m_Snap.m_aCharacters[Item.m_Id].m_Cur.m_Tick)
							Evolve(&m_Snap.m_aCharacters[Item.m_Id].m_Cur, Client()->GameTick(g_Config.m_ClDummy));

						m_aClients[Item.m_Id].m_Snapped = *((const CNetObj_Character *)Item.m_pData);
						m_aClients[Item.m_Id].m_Evolved = m_Snap.m_aCharacters[Item.m_Id].m_Cur;
					}
					else
					{
						m_aClients[Item.m_Id].m_Evolved.m_Tick = -1;
					}
				}
			}
			else if(Item.m_Type == NETOBJTYPE_DDNETCHARACTER)
			{
				const CNetObj_DDNetCharacter *pCharacterData = (const CNetObj_DDNetCharacter *)Item.m_pData;

				if(Item.m_Id < MAX_CLIENTS)
				{
					m_Snap.m_aCharacters[Item.m_Id].m_ExtendedData = *pCharacterData;
					m_Snap.m_aCharacters[Item.m_Id].m_pPrevExtendedData = (const CNetObj_DDNetCharacter *)Client()->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_DDNETCHARACTER, Item.m_Id);
					m_Snap.m_aCharacters[Item.m_Id].m_HasExtendedData = true;
					m_Snap.m_aCharacters[Item.m_Id].m_HasExtendedDisplayInfo = false;
					if(pCharacterData->m_JumpedTotal != -1)
					{
						m_Snap.m_aCharacters[Item.m_Id].m_HasExtendedDisplayInfo = true;
					}
					CClientData *pClient = &m_aClients[Item.m_Id];
					// Collision
					pClient->m_Solo = pCharacterData->m_Flags & CHARACTERFLAG_SOLO;
					pClient->m_Jetpack = pCharacterData->m_Flags & CHARACTERFLAG_JETPACK;
					pClient->m_CollisionDisabled = pCharacterData->m_Flags & CHARACTERFLAG_COLLISION_DISABLED;
					pClient->m_HammerHitDisabled = pCharacterData->m_Flags & CHARACTERFLAG_HAMMER_HIT_DISABLED;
					pClient->m_GrenadeHitDisabled = pCharacterData->m_Flags & CHARACTERFLAG_GRENADE_HIT_DISABLED;
					pClient->m_LaserHitDisabled = pCharacterData->m_Flags & CHARACTERFLAG_LASER_HIT_DISABLED;
					pClient->m_ShotgunHitDisabled = pCharacterData->m_Flags & CHARACTERFLAG_SHOTGUN_HIT_DISABLED;
					pClient->m_HookHitDisabled = pCharacterData->m_Flags & CHARACTERFLAG_HOOK_HIT_DISABLED;
					pClient->m_Super = pCharacterData->m_Flags & CHARACTERFLAG_SUPER;
					pClient->m_Invincible = pCharacterData->m_Flags & CHARACTERFLAG_INVINCIBLE;

					// Endless
					pClient->m_EndlessHook = pCharacterData->m_Flags & CHARACTERFLAG_ENDLESS_HOOK;
					pClient->m_EndlessJump = pCharacterData->m_Flags & CHARACTERFLAG_ENDLESS_JUMP;

					// Freeze
					pClient->m_FreezeEnd = pCharacterData->m_FreezeEnd;
					pClient->m_DeepFrozen = pCharacterData->m_FreezeEnd == -1;
					pClient->m_LiveFrozen = (pCharacterData->m_Flags & CHARACTERFLAG_MOVEMENTS_DISABLED) != 0;
					pClient->m_IsInFreeze = (pCharacterData->m_Flags & CHARACTERFLAG_IN_FREEZE) != 0;
					QmHudObserveTeeCharacterSnapshot(pClient->m_HudFrozenTeeState, true, Client()->GameTick(g_Config.m_ClDummy));

					// Telegun
					pClient->m_HasTelegunGrenade = pCharacterData->m_Flags & CHARACTERFLAG_TELEGUN_GRENADE;
					pClient->m_HasTelegunGun = pCharacterData->m_Flags & CHARACTERFLAG_TELEGUN_GUN;
					pClient->m_HasTelegunLaser = pCharacterData->m_Flags & CHARACTERFLAG_TELEGUN_LASER;

					pClient->m_Predicted.ReadDDNet(pCharacterData);

					m_Teams.SetSolo(Item.m_Id, pClient->m_Solo);
				}
			}
			else if(Item.m_Type == NETOBJTYPE_SPECCHAR)
			{
				const CNetObj_SpecChar *pSpecCharData = (const CNetObj_SpecChar *)Item.m_pData;

				if(Item.m_Id < MAX_CLIENTS)
				{
					CClientData *pClient = &m_aClients[Item.m_Id];
					pClient->m_SpecCharPresent = true;
					pClient->m_SpecChar.x = pSpecCharData->m_X;
					pClient->m_SpecChar.y = pSpecCharData->m_Y;
				}
			}
			else if(Item.m_Type == NETOBJTYPE_SPECTATORINFO)
			{
				m_Snap.m_pSpectatorInfo = (const CNetObj_SpectatorInfo *)Item.m_pData;
				m_Snap.m_pPrevSpectatorInfo = (const CNetObj_SpectatorInfo *)Client()->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_SPECTATORINFO, Item.m_Id);

				// needed for 0.7 survival
				// to auto spec players when dead
				if(Client()->IsSixup())
					m_Snap.m_SpecInfo.m_Active = true;
				m_Snap.m_SpecInfo.m_SpectatorId = m_Snap.m_pSpectatorInfo->m_SpectatorId;
			}
			else if(Item.m_Type == NETOBJTYPE_DDNETSPECTATORINFO)
			{
				const CNetObj_DDNetSpectatorInfo *pDDNetSpecInfo = (const CNetObj_DDNetSpectatorInfo *)Item.m_pData;
				m_Snap.m_SpecInfo.m_HasCameraInfo = pDDNetSpecInfo->m_HasCameraInfo;
				m_Snap.m_SpecInfo.m_Zoom = pDDNetSpecInfo->m_Zoom / 1000.0f;
				m_Snap.m_SpecInfo.m_Deadzone = pDDNetSpecInfo->m_Deadzone;
				m_Snap.m_SpecInfo.m_FollowFactor = pDDNetSpecInfo->m_FollowFactor;
			}
			else if(Item.m_Type == NETOBJTYPE_SPECTATORCOUNT)
			{
				m_Snap.m_pSpectatorCount = (const CNetObj_SpectatorCount *)Item.m_pData;
			}
			else if(Item.m_Type == NETOBJTYPE_GAMEINFO)
			{
				m_Snap.m_pGameInfoObj = (const CNetObj_GameInfo *)Item.m_pData;
				const bool CurrentTickGameOver = (m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) != 0;
				const bool CurrentTickGamePaused = (m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED) != 0;
				if(!m_GameOver && CurrentTickGameOver)
					OnGameOver();
				else if(m_GameOver && !CurrentTickGameOver)
					OnStartGame();
				// Handle case that a new round is started (RoundStartTick changed)
				// New round is usually started after `restart` on server
				if(m_Snap.m_pGameInfoObj->m_RoundStartTick != m_LastRoundStartTick && !(CurrentTickGameOver || CurrentTickGamePaused || m_GamePaused))
					OnStartRound();
				m_LastRoundStartTick = m_Snap.m_pGameInfoObj->m_RoundStartTick;
				m_GameOver = CurrentTickGameOver;
				m_GamePaused = CurrentTickGamePaused;
			}
			else if(Item.m_Type == NETOBJTYPE_GAMEINFOEX)
			{
				if(FoundGameInfoEx)
				{
					continue;
				}
				FoundGameInfoEx = true;
				m_GameInfo = GetGameInfo((const CNetObj_GameInfoEx *)Item.m_pData, Item.m_DataSize, &ServerInfo);
			}
			else if(Item.m_Type == NETOBJTYPE_GAMEDATA)
			{
				m_Snap.m_pGameDataObj = static_cast<const CNetObj_GameData *>(Item.m_pData);
				m_Snap.m_pPrevGameDataObj = static_cast<const CNetObj_GameData *>(Client()->SnapFindItem(IClient::SNAP_PREV, Item.m_Type, Item.m_Id));
				if(m_Snap.m_pGameDataObj->m_FlagCarrierRed == FLAG_TAKEN)
				{
					if(m_aFlagDropTick[TEAM_RED] == 0)
						m_aFlagDropTick[TEAM_RED] = Client()->GameTick(g_Config.m_ClDummy);
				}
				else
				{
					m_aFlagDropTick[TEAM_RED] = 0;
				}
				if(m_Snap.m_pGameDataObj->m_FlagCarrierBlue == FLAG_TAKEN)
				{
					if(m_aFlagDropTick[TEAM_BLUE] == 0)
						m_aFlagDropTick[TEAM_BLUE] = Client()->GameTick(g_Config.m_ClDummy);
				}
				else
				{
					m_aFlagDropTick[TEAM_BLUE] = 0;
				}
				if(m_LastFlagCarrierRed == FLAG_ATSTAND && m_Snap.m_pGameDataObj->m_FlagCarrierRed >= 0)
					OnFlagGrab(TEAM_RED);
				else if(m_LastFlagCarrierBlue == FLAG_ATSTAND && m_Snap.m_pGameDataObj->m_FlagCarrierBlue >= 0)
					OnFlagGrab(TEAM_BLUE);

				m_LastFlagCarrierRed = m_Snap.m_pGameDataObj->m_FlagCarrierRed;
				m_LastFlagCarrierBlue = m_Snap.m_pGameDataObj->m_FlagCarrierBlue;
			}
			else if(Item.m_Type == NETOBJTYPE_FLAG)
			{
				const CNetObj_Flag *pPrevFlag = static_cast<const CNetObj_Flag *>(Client()->SnapFindItem(IClient::SNAP_PREV, Item.m_Type, Item.m_Id));
				if(pPrevFlag == nullptr)
				{
					continue;
				}
				m_Snap.m_apFlags[m_Snap.m_NumFlags] = static_cast<const CNetObj_Flag *>(Item.m_pData);
				m_Snap.m_apPrevFlags[m_Snap.m_NumFlags] = pPrevFlag;
				++m_Snap.m_NumFlags;
			}
			else if(Item.m_Type == NETOBJTYPE_SWITCHSTATE)
			{
				if(Item.m_DataSize < 36)
				{
					continue;
				}
				const CNetObj_SwitchState *pSwitchStateData = (const CNetObj_SwitchState *)Item.m_pData;
				// TODO: use NUM_DDRACE_TEAMS-1 instead of hardcoding 63
				//       once https://github.com/ddnet/ddnet/pull/11232 is resolved
				int Team = std::clamp(Item.m_Id, (int)TEAM_FLOCK, 63);

				int HighestSwitchNumber = std::clamp(pSwitchStateData->m_HighestSwitchNumber, 0, 255);
				if(HighestSwitchNumber != maximum(0, (int)Switchers().size() - 1))
				{
					m_GameWorld.m_Core.InitSwitchers(HighestSwitchNumber);
					Collision()->m_HighestSwitchNumber = HighestSwitchNumber;
				}

				for(int j = 0; j < (int)Switchers().size(); j++)
				{
					Switchers()[j].m_aStatus[Team] = (pSwitchStateData->m_aStatus[j / 32] >> (j % 32)) & 1;
				}

				if(Item.m_DataSize >= 68)
				{
					// update the endtick of up to four timed switchers
					for(int j = 0; j < (int)std::size(pSwitchStateData->m_aEndTicks); j++)
					{
						int SwitchNumber = pSwitchStateData->m_aSwitchNumbers[j];
						int EndTick = pSwitchStateData->m_aEndTicks[j];
						if(EndTick > 0 && in_range(SwitchNumber, 0, (int)Switchers().size()))
						{
							Switchers()[SwitchNumber].m_aEndTick[Team] = EndTick;
						}
					}
				}

				// update switch types
				for(auto &Switcher : Switchers())
				{
					if(Switcher.m_aStatus[Team])
						Switcher.m_aType[Team] = Switcher.m_aEndTick[Team] ? TILE_SWITCHTIMEDOPEN : TILE_SWITCHOPEN;
					else
						Switcher.m_aType[Team] = Switcher.m_aEndTick[Team] ? TILE_SWITCHTIMEDCLOSE : TILE_SWITCHCLOSE;
				}

				if(!GotSwitchStateTeam)
					m_aSwitchStateTeam[g_Config.m_ClDummy] = Team;
				else
					m_aSwitchStateTeam[g_Config.m_ClDummy] = -1;
				GotSwitchStateTeam = true;
			}
			else if(Item.m_Type == NETOBJTYPE_MAPBESTTIME)
			{
				const CNetObj_MapBestTime *pMapBestTimeData = static_cast<const CNetObj_MapBestTime *>(Item.m_pData);
				m_MapBestTimeSeconds = pMapBestTimeData->m_MapBestTimeSeconds;
				m_MapBestTimeMillis = pMapBestTimeData->m_MapBestTimeMillis;
			}
		}
	}

	if(!FoundGameInfoEx)
	{
		m_GameInfo = GetGameInfo(nullptr, 0, &ServerInfo);
	}

	// setup local pointers
	if(m_Snap.m_LocalClientId >= 0)
	{
		m_aLocalIds[g_Config.m_ClDummy] = m_Snap.m_LocalClientId;

		CSnapState::CCharacterInfo *pChr = &m_Snap.m_aCharacters[m_Snap.m_LocalClientId];
		if(pChr->m_Active)
		{
			if(!m_Snap.m_SpecInfo.m_Active)
			{
				m_Snap.m_pLocalCharacter = &pChr->m_Cur;
				m_Snap.m_pLocalPrevCharacter = &pChr->m_Prev;
				m_LocalCharacterPos = vec2(m_Snap.m_pLocalCharacter->m_X, m_Snap.m_pLocalCharacter->m_Y);
			}
		}
		else if(Client()->SnapFindItem(IClient::SNAP_PREV, NETOBJTYPE_CHARACTER, m_Snap.m_LocalClientId))
		{
			// player died
			m_Controls.OnPlayerDeath();
		}
	}

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		// Streamer skin privacy depends on local/friend classification, so publish it before skin render info.
		m_aClients[i].m_Friend = !(i == m_Snap.m_LocalClientId || !m_Snap.m_apPlayerInfos[i] || !Friends()->IsFriend(m_aClients[i].m_aName, m_aClients[i].m_aClan, true));
		m_aClients[i].m_Foe = !(i == m_Snap.m_LocalClientId || !m_Snap.m_apPlayerInfos[i] || !Foes()->IsFriend(m_aClients[i].m_aName, m_aClients[i].m_aClan, true));
	}

	for(CClientData &Client : m_aClients)
		Client.UpdateSkinInfo();

	if(m_FastPractice.Enabled())
		m_PredictedDummyId = m_FastPractice.CurrentPracticeDummyId();
	else if(Client()->DummyConnected() && m_aLocalIds[!g_Config.m_ClDummy] >= 0)
		m_PredictedDummyId = m_aLocalIds[!g_Config.m_ClDummy];
	else
		m_PredictedDummyId = -1;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(m_Snap.m_LocalClientId == -1 && m_DemoSpecId == SPEC_FOLLOW)
		{
			// TODO: can this be done in the translation layer?
			if(!Client()->IsSixup())
				m_DemoSpecId = SPEC_FREEVIEW;
		}
		if(m_DemoSpecId != SPEC_FOLLOW)
		{
			m_Snap.m_SpecInfo.m_Active = true;
			if(m_DemoSpecId > SPEC_FREEVIEW && m_Snap.m_aCharacters[m_DemoSpecId].m_Active)
				m_Snap.m_SpecInfo.m_SpectatorId = m_DemoSpecId;
			else
				m_Snap.m_SpecInfo.m_SpectatorId = SPEC_FREEVIEW;
		}
	}

	// clear out unneeded client data
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!m_Snap.m_apPlayerInfos[i] && m_aClients[i].m_Active)
		{
			m_aClients[i].Reset();
			m_aStats[i].Reset();
		}
	}

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		m_pDiscord->UpdatePlayerCount(m_Snap.m_NumPlayers);
	}

	// check if we received all finish times
	m_ReceivedDDNetPlayerFinishTimes = m_ReceivedDDNetPlayer && !HasUnsetDDNetFinishTimes;
	m_ReceivedDDNetPlayerFinishTimesMillis = m_ReceivedDDNetPlayer && HasTrueMillisecondFinishTimes;

	// sort player infos by name
	mem_copy(m_Snap.m_apInfoByName, m_Snap.m_apPlayerInfos, sizeof(m_Snap.m_apInfoByName));
	std::stable_sort(m_Snap.m_apInfoByName, m_Snap.m_apInfoByName + MAX_CLIENTS,
		[this](const CNetObj_PlayerInfo *pPlayer1, const CNetObj_PlayerInfo *pPlayer2) -> bool {
			if(!pPlayer2)
				return static_cast<bool>(pPlayer1);
			if(!pPlayer1)
				return false;
			return str_comp_nocase(m_aClients[pPlayer1->m_ClientId].m_aName, m_aClients[pPlayer2->m_ClientId].m_aName) < 0;
		});

	bool TimeScore = m_GameInfo.m_TimeScore;
	bool Race7 = Client()->IsSixup() && m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	const bool UsePointsSort = g_Config.m_QmScoreboardSortMode != 0;

	// sort player infos by score
	mem_copy(m_Snap.m_apInfoByScore, m_Snap.m_apInfoByName, sizeof(m_Snap.m_apInfoByScore));
	if(Race7)
		std::stable_sort(m_Snap.m_apInfoByScore, m_Snap.m_apInfoByScore + MAX_CLIENTS,
			[this, UsePointsSort](const CNetObj_PlayerInfo *pPlayer1, const CNetObj_PlayerInfo *pPlayer2) -> bool {
				if(!pPlayer2)
					return static_cast<bool>(pPlayer1);
				if(!pPlayer1)
					return false;
				if(UsePointsSort)
				{
					const SPlayerPointsResult Points1 = m_PlayerPoints.GetPoints(m_aClients[pPlayer1->m_ClientId].m_aName);
					const SPlayerPointsResult Points2 = m_PlayerPoints.GetPoints(m_aClients[pPlayer2->m_ClientId].m_aName);
					const bool HasPoints1 = Points1.m_Status == EPointsStatus::READY;
					const bool HasPoints2 = Points2.m_Status == EPointsStatus::READY;
					if(HasPoints1 != HasPoints2)
						return HasPoints1;
					if(HasPoints1 && Points1.m_Points != Points2.m_Points)
						return Points1.m_Points > Points2.m_Points;
				}
				return (((pPlayer1->m_Score == -1) ? std::numeric_limits<int>::max() : pPlayer1->m_Score) <
					((pPlayer2->m_Score == -1) ? std::numeric_limits<int>::max() : pPlayer2->m_Score));
			});
	else
		std::stable_sort(m_Snap.m_apInfoByScore, m_Snap.m_apInfoByScore + MAX_CLIENTS,
			[this, TimeScore, UsePointsSort](const CNetObj_PlayerInfo *pPlayer1, const CNetObj_PlayerInfo *pPlayer2) -> bool {
				if(!pPlayer2)
					return static_cast<bool>(pPlayer1);
				if(!pPlayer1)
					return false;
				if(UsePointsSort)
				{
					const SPlayerPointsResult Points1 = m_PlayerPoints.GetPoints(m_aClients[pPlayer1->m_ClientId].m_aName);
					const SPlayerPointsResult Points2 = m_PlayerPoints.GetPoints(m_aClients[pPlayer2->m_ClientId].m_aName);
					const bool HasPoints1 = Points1.m_Status == EPointsStatus::READY;
					const bool HasPoints2 = Points2.m_Status == EPointsStatus::READY;
					if(HasPoints1 != HasPoints2)
						return HasPoints1;
					if(HasPoints1 && Points1.m_Points != Points2.m_Points)
						return Points1.m_Points > Points2.m_Points;
				}
				if(m_ReceivedDDNetPlayerFinishTimes)
				{
					int TimeSeconds1 = m_aClients[pPlayer1->m_ClientId].m_FinishTimeSeconds;
					int TimeSeconds2 = m_aClients[pPlayer2->m_ClientId].m_FinishTimeSeconds;
					TimeSeconds1 = TimeSeconds1 == FinishTime::NOT_FINISHED_MILLIS ? std::numeric_limits<int>::max() : TimeSeconds1;
					TimeSeconds2 = TimeSeconds2 == FinishTime::NOT_FINISHED_MILLIS ? std::numeric_limits<int>::max() : TimeSeconds2;
					if(TimeSeconds1 == TimeSeconds2)
						return m_aClients[pPlayer1->m_ClientId].m_FinishTimeMillis < m_aClients[pPlayer2->m_ClientId].m_FinishTimeMillis;
					return TimeSeconds1 < TimeSeconds2;
				}
				return (((TimeScore && pPlayer1->m_Score == FinishTime::NOT_FINISHED_TIMESCORE) ? std::numeric_limits<int>::min() : pPlayer1->m_Score) >
					((TimeScore && pPlayer2->m_Score == FinishTime::NOT_FINISHED_TIMESCORE) ? std::numeric_limits<int>::min() : pPlayer2->m_Score));
			});

	// sort player infos by DDRace Team (and score between)
	int Index = 0;
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		for(int i = 0; i < MAX_CLIENTS && Index < MAX_CLIENTS; ++i)
		{
			if(m_Snap.m_apInfoByScore[i] && m_Teams.Team(m_Snap.m_apInfoByScore[i]->m_ClientId) == Team)
				m_Snap.m_apInfoByDDTeamScore[Index++] = m_Snap.m_apInfoByScore[i];
		}
	}

	// sort player infos by DDRace Team (and name between)
	Index = 0;
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		for(int i = 0; i < MAX_CLIENTS && Index < MAX_CLIENTS; ++i)
		{
			if(m_Snap.m_apInfoByName[i] && m_Teams.Team(m_Snap.m_apInfoByName[i]->m_ClientId) == Team)
				m_Snap.m_apInfoByDDTeamName[Index++] = m_Snap.m_apInfoByName[i];
		}
	}

	if(ServerInfo.m_aGameType[0] != '0')
	{
		if(str_comp(ServerInfo.m_aGameType, "DM") != 0 && str_comp(ServerInfo.m_aGameType, "TDM") != 0 && str_comp(ServerInfo.m_aGameType, "CTF") != 0)
			m_ServerMode = SERVERMODE_MOD;
		else if(mem_comp(&CTuningParams::DEFAULT, &m_aTuning[g_Config.m_ClDummy], 33) == 0)
			m_ServerMode = SERVERMODE_PURE;
		else
			m_ServerMode = SERVERMODE_PUREMOD;
	}

	{
		// add tuning to demo when new recording was started, because server tune message was already received before
		std::bitset<RECORDER_MAX> CurrentRecordings;
		for(int i = 0; i < RECORDER_MAX; i++)
		{
			if(DemoRecorder(i)->IsRecording())
			{
				CurrentRecordings.set(i);
			}
		}
		const bool HasNewRecordings = (CurrentRecordings & ~m_ActiveRecordings).any();
		m_ActiveRecordings = CurrentRecordings;
		if(HasNewRecordings)
		{
			CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
			int *pParams = (int *)&m_aTuning[g_Config.m_ClDummy];
			for(unsigned i = 0; i < sizeof(m_aTuning[0]) / sizeof(int); i++)
				Msg.AddInt(pParams[i]);
			Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
			RecordDemoHudState(true);
			RecordDemoInputState(true);
			RecordDemoGamepadState(true);
		}

		for(int i = 0; i < 2; i++)
		{
			if(m_aDDRaceMsgSent[i] || !m_Snap.m_pLocalInfo)
			{
				continue;
			}
			if(i == IClient::CONN_DUMMY && !Client()->DummyConnected())
			{
				continue;
			}
			CMsgPacker Msg(NETMSGTYPE_CL_ISDDNETLEGACY, false);
			Msg.AddInt(DDNetVersion());
			Client()->SendMsg(i, &Msg, MSGFLAG_VITAL);
			m_aDDRaceMsgSent[i] = true;
		}

		if(m_Snap.m_SpecInfo.m_Active && m_MultiViewActivated)
		{
			// dont show other teams while spectating in multi view
			CNetMsg_Cl_ShowOthers Msg;
			Msg.m_Show = SHOW_OTHERS_ONLY_TEAM;
			Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);

			// update state
			m_aShowOthers[g_Config.m_ClDummy] = SHOW_OTHERS_ONLY_TEAM;
		}
		else if(m_aShowOthers[g_Config.m_ClDummy] == SHOW_OTHERS_NOT_SET || m_aShowOthers[g_Config.m_ClDummy] != g_Config.m_ClShowOthers)
		{
			{
				CNetMsg_Cl_ShowOthers Msg;
				Msg.m_Show = g_Config.m_ClShowOthers;
				Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
			}

			// update state
			m_aShowOthers[g_Config.m_ClDummy] = g_Config.m_ClShowOthers;
		}

		if(m_aEnableSpectatorCount[0] == -1 || m_aEnableSpectatorCount[0] != g_Config.m_ClShowhudSpectatorCount)
		{
			CNetMsg_Cl_EnableSpectatorCount Msg;
			Msg.m_Enable = g_Config.m_ClShowhudSpectatorCount;
			Client()->SendPackMsg(0, &Msg, MSGFLAG_VITAL);
			m_aEnableSpectatorCount[0] = g_Config.m_ClShowhudSpectatorCount;
		}
		if(Client()->DummyConnected() && (m_aEnableSpectatorCount[1] == -1 || m_aEnableSpectatorCount[1] != g_Config.m_ClShowhudSpectatorCount))
		{
			CNetMsg_Cl_EnableSpectatorCount Msg;
			Msg.m_Enable = g_Config.m_ClShowhudSpectatorCount;
			Client()->SendPackMsg(1, &Msg, MSGFLAG_VITAL);
			m_aEnableSpectatorCount[1] = g_Config.m_ClShowhudSpectatorCount;
		}

		const float BaseZoom = m_Camera.BaseZoom();
		float ShowDistanceZoom = BaseZoom;
		float Zoom = BaseZoom;
		if(m_Camera.m_Zooming)
		{
			if(m_Camera.m_ZoomSmoothingTarget > BaseZoom) // Zooming out
				ShowDistanceZoom = m_Camera.m_ZoomSmoothingTarget;
			else if(m_Camera.m_ZoomSmoothingTarget < BaseZoom && m_LastShowDistanceZoom > 0) // Zooming in
				ShowDistanceZoom = m_LastShowDistanceZoom;

			Zoom = m_Camera.m_ZoomSmoothingTarget;
		}

		float Deadzone = m_Camera.Deadzone();
		float FollowFactor = m_Camera.FollowFactor();

		if(m_Snap.m_SpecInfo.m_Active)
		{
			// don't send camera information when spectating
			Zoom = m_LastZoom;
			Deadzone = m_LastDeadzone;
			FollowFactor = m_LastFollowFactor;
		}

		// initialize dummy vital when first connected
		if(Client()->DummyConnected() && !m_LastDummyConnected)
		{
			{
				CNetMsg_Cl_ShowDistance Msg;
				float x, y;
				Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), ShowDistanceZoom, &x, &y);
				Msg.m_X = x;
				Msg.m_Y = y;
				CMsgPacker Packer(&Msg);
				Msg.Pack(&Packer);
				Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
			}
			{
				CNetMsg_Cl_CameraInfo Msg;
				Msg.m_Zoom = round_truncate(Zoom * 1000.f);
				Msg.m_Deadzone = Deadzone;
				Msg.m_FollowFactor = FollowFactor;
				CMsgPacker Packer(&Msg);
				Msg.Pack(&Packer);
				Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
			}
		}

		// send show distance
		if(ShowDistanceZoom != m_LastShowDistanceZoom || Graphics()->GameScreenAspect() != m_LastScreenAspect)
		{
			CNetMsg_Cl_ShowDistance Msg;
			float x, y;
			Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), ShowDistanceZoom, &x, &y);
			Msg.m_X = x;
			Msg.m_Y = y;
			Client()->ChecksumData()->m_Zoom = ShowDistanceZoom;
			CMsgPacker Packer(&Msg);
			Msg.Pack(&Packer);

			Client()->SendMsg(IClient::CONN_MAIN, &Packer, MSGFLAG_VITAL);
			if(Client()->DummyConnected() && m_LastDummyConnected)
				Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
		}

		// send camera info
		if(Zoom != m_LastZoom || Deadzone != m_LastDeadzone || FollowFactor != m_LastFollowFactor)
		{
			CNetMsg_Cl_CameraInfo Msg;
			Msg.m_Zoom = round_truncate(Zoom * 1000.f);
			Msg.m_Deadzone = Deadzone;
			Msg.m_FollowFactor = FollowFactor;
			CMsgPacker Packer(&Msg);
			Msg.Pack(&Packer);

			Client()->SendMsg(IClient::CONN_MAIN, &Packer, MSGFLAG_VITAL);
			if(Client()->DummyConnected() && m_LastDummyConnected)
				Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
		}

		m_LastShowDistanceZoom = ShowDistanceZoom;
		m_LastZoom = Zoom;
		m_LastScreenAspect = Graphics()->GameScreenAspect();
		m_LastDeadzone = Deadzone;
		m_LastFollowFactor = FollowFactor;
		m_LastDummyConnected = Client()->DummyConnected();
	}

	FinalizeHammerHitEvents();

	for(auto &pComponent : m_vpAll)
		pComponent->OnNewSnapshot();

	// notify editor when local character moved
	UpdateEditorIngameMoved();

	// detect air jump for other players
	const int PrevGameTick = Client()->PrevGameTick(g_Config.m_ClDummy);
	const int CurGameTick = Client()->GameTick(g_Config.m_ClDummy);
	const int PredictedLocalDummy = g_Config.m_ClDummy ^ m_IsDummySwapping;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const auto &Character = m_Snap.m_aCharacters[i];
		if(!Character.m_Active)
			continue;

		const bool AirJumpByJumpedState = (Character.m_Cur.m_Jumped & 2) && !(Character.m_Prev.m_Jumped & 2);
		bool AirJumpByJumpCount = false;
		if(Character.m_HasExtendedDisplayInfo && Character.m_pPrevExtendedData != nullptr && Character.m_pPrevExtendedData->m_JumpedTotal != -1)
		{
			// Players with extra or endless jumps can reset the "dark feet" jumped bit
			// in the same tick, so use the monotonically increasing air-jump counter as
			// a fallback and verify it matches an upward jump impulse.
			const bool JumpedTotalIncreased = Character.m_ExtendedData.m_JumpedTotal > Character.m_pPrevExtendedData->m_JumpedTotal;
			const bool VelocityLooksLikeAirJump = Character.m_Cur.m_VelY < Character.m_Prev.m_VelY;
			AirJumpByJumpCount = JumpedTotalIncreased && VelocityLooksLikeAirJump;
		}

		if(AirJumpByJumpedState || AirJumpByJumpCount)
		{
			bool IsDummy = Client()->DummyConnected() && i == m_aLocalIds[!g_Config.m_ClDummy];
			bool IsLocalPlayer = i == m_Snap.m_LocalClientId;
			if(Client()->IsSixup() && !IsLocalPlayer)
			{
				// Sixup servers provide explicit AIR_JUMP events, handled in ProcessEvents.
				// Skip heuristic detection for non-local players to avoid duplicate triggers.
				continue;
			}

			bool UseSnapshotAirJump = false;
			if(IsLocalPlayer)
			{
				// Local air-jump effects are normally predicted. Fall back to snapshot
				// when prediction is unavailable, predicted events are disabled,
				// or the predicted AIR_JUMP event was missed in the current snap window.
				if(!Predict() || !g_Config.m_ClPredictEvents)
				{
					UseSnapshotAirJump = true;
				}
				else
				{
					const int LastPredictedAirJumpTick = m_aLastPredictedAirJumpTick[PredictedLocalDummy];
					UseSnapshotAirJump = LastPredictedAirJumpTick <= PrevGameTick || LastPredictedAirJumpTick > CurGameTick;
				}
			}
			else
			{
				UseSnapshotAirJump = !Predict() || !AntiPingPlayers() || !IsDummy;
			}

			if(UseSnapshotAirJump)
			{
				vec2 Pos = mix(vec2(Character.m_Prev.m_X, Character.m_Prev.m_Y),
					vec2(Character.m_Cur.m_X, Character.m_Cur.m_Y),
					Client()->IntraGameTick(g_Config.m_ClDummy));
				float Alpha = 1.0f;
				if(IsOtherTeam(i))
					Alpha = g_Config.m_ClShowOthersAlpha / 100.0f;
				const float Volume = 1.0f; // TODO snd_game_volume_others

				const bool Grounded = Collision()->IsOnGround(vec2(m_Snap.m_aCharacters[i].m_Prev.m_X, m_Snap.m_aCharacters[i].m_Prev.m_Y), CCharacterCore::PhysicalSize());
				if(!Grounded)
				{
					m_Effects.AirJump(Pos, Alpha, Volume);
				}
			}
		}
	}

	if(g_Config.m_ClFreezeStars && !m_SuppressEvents)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			auto &Character = m_Snap.m_aCharacters[ClientId];
			if(Character.m_Active && Character.m_HasExtendedData && Character.m_pPrevExtendedData)
			{
				int FreezeTimeNow = Character.m_ExtendedData.m_FreezeEnd - Client()->GameTick(g_Config.m_ClDummy);
				int FreezeTimePrev = Character.m_pPrevExtendedData->m_FreezeEnd - Client()->PrevGameTick(g_Config.m_ClDummy);
				vec2 Pos = vec2(Character.m_Cur.m_X, Character.m_Cur.m_Y);
				int StarsNow = (FreezeTimeNow + 1) / Client()->GameTickSpeed();
				int StarsPrev = (FreezeTimePrev + 1) / Client()->GameTickSpeed();
				if(StarsNow < StarsPrev || (StarsPrev == 0 && StarsNow > 0))
				{
					int Amount = StarsNow + 1;
					float Mid = 3 * pi / 2;
					float Min = Mid - pi / 3;
					float Max = Mid + pi / 3;
					for(int j = 0; j < Amount; j++)
					{
						float Angle = mix(Min, Max, (j + 1) / (float)(Amount + 2));
						m_Effects.DamageIndicator(Pos, direction(Angle), 1.0f);
					}
				}
			}
		}
	}

	// Record m_LastRaceTick for g_Config.m_ClConfirmDisconnect/QuitTime
	if(m_GameInfo.m_Race &&
		Client()->State() == IClient::STATE_ONLINE &&
		m_Snap.m_pGameInfoObj &&
		!m_Snap.m_SpecInfo.m_Active &&
		m_Snap.m_pLocalCharacter &&
		m_Snap.m_pLocalPrevCharacter)
	{
		const bool RaceFlag = m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME;
		m_LastRaceTick = RaceFlag ? -m_Snap.m_pGameInfoObj->m_WarmupTimer : -1;
	}

	SnapCollectEntities(); // creates a collection that associates EntityEx snap items with the entities they belong to

	UpdateLocalTuning();
	m_IsDummySwapping = 0;
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
		UpdatePrediction();
	UpdateAutoTeamLock();
}

void CGameClient::UpdateAutoTeamLock()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
			m_aAutoTeamLockDeadlineTick[Dummy] = 0;
			m_aAutoTeamLockPending[Dummy] = false;
		}
		return;
	}

	const int Dummy = g_Config.m_ClDummy;
	const int ClientId = m_aLocalIds[Dummy];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		m_aAutoTeamLockLastTeam[Dummy] = TEAM_FLOCK;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	const int Team = m_Teams.Team(ClientId);
	const bool TeamCanBeLocked = Team > TEAM_FLOCK && Team < TEAM_SUPER;
	const bool LastTeamCanBeLocked = m_aAutoTeamLockLastTeam[Dummy] > TEAM_FLOCK && m_aAutoTeamLockLastTeam[Dummy] < TEAM_SUPER;

	if(!g_Config.m_QmAutoTeamLock)
	{
		m_aAutoTeamLockLastTeam[Dummy] = Team;
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
		return;
	}

	if(TeamCanBeLocked && (!LastTeamCanBeLocked || Team != m_aAutoTeamLockLastTeam[Dummy]))
	{
		const int DelayTicks = g_Config.m_QmAutoTeamLockDelay * Client()->GameTickSpeed();
		m_aAutoTeamLockDeadlineTick[Dummy] = (int64_t)Client()->GameTick(Dummy) + DelayTicks;
		m_aAutoTeamLockPending[Dummy] = true;
	}
	else if(!TeamCanBeLocked)
	{
		m_aAutoTeamLockDeadlineTick[Dummy] = 0;
		m_aAutoTeamLockPending[Dummy] = false;
	}

	if(m_aAutoTeamLockPending[Dummy] && TeamCanBeLocked && Client()->GameTick(Dummy) >= m_aAutoTeamLockDeadlineTick[Dummy])
	{
		m_Chat.SendChat(0, "/lock 1");
		m_aAutoTeamLockPending[Dummy] = false;
	}

	m_aAutoTeamLockLastTeam[Dummy] = Team;
}

void CGameClient::UpdateEditorIngameMoved()
{
	const bool LocalCharacterMoved = m_Snap.m_pLocalCharacter && m_Snap.m_pLocalPrevCharacter && (m_Snap.m_pLocalCharacter->m_X != m_Snap.m_pLocalPrevCharacter->m_X || m_Snap.m_pLocalCharacter->m_Y != m_Snap.m_pLocalPrevCharacter->m_Y);
	if(!g_Config.m_ClEditor)
	{
		m_EditorMovementDelay = 5;
	}
	else if(m_EditorMovementDelay > 0 && !LocalCharacterMoved)
	{
		--m_EditorMovementDelay;
	}
	if(m_EditorMovementDelay == 0 && LocalCharacterMoved)
	{
		Editor()->OnIngameMoved();
	}
}

bool CGameClient::GetPotentialHammerHitArea(CCharacter *pChar, vec2 &HitPos, float &HitRadius)
{
	if(!pChar || pChar->GetActiveWeapon() != WEAPON_HAMMER || pChar->HammerHitDisabled())
		return false;

	const CNetObj_PlayerInput *pInput = pChar->LatestInput();
	if(!pInput)
		return false;

	vec2 Direction = normalize(vec2(pInput->m_TargetX, pInput->m_TargetY));
	if(Direction.x == 0.0f && Direction.y == 0.0f)
		Direction = vec2(0.0f, -1.0f);

	const float ProximityRadius = pChar->GetProximityRadius();
	HitPos = pChar->GetPos() + Direction * ProximityRadius * 0.75f;
	HitRadius = ProximityRadius * 0.5f;
	return true;
}

int CGameClient::FindPotentialHammerHitTargets(CCharacter *pChar, vec2 HitPos, float HitRadius, int *pTargetIds, int MaxTargetIds)
{
	if(!pChar || !pTargetIds || MaxTargetIds <= 0)
		return 0;

	CEntity *apEnts[MAX_CLIENTS];
	const int Num = pChar->GameWorld()->FindEntities(HitPos, HitRadius, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	int NumTargets = 0;
	for(int i = 0; i < Num && NumTargets < MaxTargetIds; ++i)
	{
		CCharacter *pTarget = static_cast<CCharacter *>(apEnts[i]);
		if(!pTarget || pTarget == pChar)
			continue;

		const int TargetId = pTarget->GetCid();
		if(TargetId < 0 || TargetId >= MAX_CLIENTS || !pChar->CanCollide(TargetId))
			continue;

		pTargetIds[NumTargets++] = TargetId;
	}

	return NumTargets;
}

int CGameClient::HammerHitConnectionFilter() const
{
	const int ActiveConnection = g_Config.m_ClDummy;
	if(!Client()->DummyConnected())
		return ActiveConnection;
	const int ActiveClientId = m_aLocalIds[ActiveConnection];
	const int OtherClientId = m_aLocalIds[ActiveConnection ^ 1];
	const bool SharedObservationTeam = ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS &&
					   OtherClientId >= 0 && OtherClientId < MAX_CLIENTS && m_Teams.CanCollide(ActiveClientId, OtherClientId);
	return SharedObservationTeam ? CQmHammerHitTracker::ANY_CONNECTION : ActiveConnection;
}

void CGameClient::HandleConfirmedHammerHit(const SQmHammerHitRecord &Hit)
{
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	if(Online)
		HandleHammerSkinSwap(Hit);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(Hit.m_AttackerId == m_aLocalIds[Dummy])
			m_aConfirmedHammerHitEvent[Dummy] = true;
		if(!Online || !g_Config.m_QmRandomEmoteOnHit || Hit.m_TargetId != m_aLocalIds[Dummy] || Hit.m_AttackerId < 0 || Hit.m_AttackerId == Hit.m_TargetId)
			continue;
		if(Dummy == 1 && !Client()->DummyConnected())
			continue;
		if(Hit.m_SnapshotTick == m_aLastRandomEmoteHammerHitTick[Dummy])
			continue;
		m_aLastRandomEmoteHammerHitTick[Dummy] = Hit.m_SnapshotTick;
		CNetMsg_Cl_Emoticon Msg;
		Msg.m_Emoticon = rand() % NUM_EMOTICONS;
		Client()->SendPackMsg(Dummy == 0 ? IClient::CONN_MAIN : IClient::CONN_DUMMY, &Msg, MSGFLAG_VITAL);
	}
}

void CGameClient::HandleHammerSkinSwap(const SQmHammerHitRecord &Hit)
{
	if(!g_Config.m_QmHammerSwapSkin || Hit.m_AttackerId < 0 || Hit.m_TargetId < 0 || Hit.m_TargetId >= MAX_CLIENTS)
		return;

	int TeeIndex = -1;
	if(Hit.m_AttackerId == m_aLocalIds[0])
		TeeIndex = 0;
	else if(Hit.m_AttackerId == m_aLocalIds[1])
		TeeIndex = 1;
	if(TeeIndex < 0 || Hit.m_SnapshotTick == m_aLastHammerSkinSwapHitTick[TeeIndex])
		return;

	const CClientData &TargetClient = m_aClients[Hit.m_TargetId];
	if(!TargetClient.m_Active)
		return;
	m_aLastHammerSkinSwapHitTick[TeeIndex] = Hit.m_SnapshotTick;

	bool Changed = false;
	if(TeeIndex == 1)
	{
		if(str_comp(g_Config.m_ClDummySkin, TargetClient.m_aSkinName) != 0)
		{
			str_copy(g_Config.m_ClDummySkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClDummySkin));
			Changed = true;
		}
		if(TargetClient.m_UseCustomColor)
		{
			if(g_Config.m_ClDummyUseCustomColor != 1)
			{
				g_Config.m_ClDummyUseCustomColor = 1;
				Changed = true;
			}
			if(static_cast<int>(g_Config.m_ClDummyColorBody) != TargetClient.m_ColorBody)
			{
				g_Config.m_ClDummyColorBody = TargetClient.m_ColorBody;
				Changed = true;
			}
			if(static_cast<int>(g_Config.m_ClDummyColorFeet) != TargetClient.m_ColorFeet)
			{
				g_Config.m_ClDummyColorFeet = TargetClient.m_ColorFeet;
				Changed = true;
			}
		}
		else if(g_Config.m_ClDummyUseCustomColor != 0)
		{
			g_Config.m_ClDummyUseCustomColor = 0;
			Changed = true;
		}

		if(Changed)
			SendDummyInfo(false);
	}
	else
	{
		if(str_comp(g_Config.m_ClPlayerSkin, TargetClient.m_aSkinName) != 0)
		{
			str_copy(g_Config.m_ClPlayerSkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
			Changed = true;
		}
		if(TargetClient.m_UseCustomColor)
		{
			if(g_Config.m_ClPlayerUseCustomColor != 1)
			{
				g_Config.m_ClPlayerUseCustomColor = 1;
				Changed = true;
			}
			if(static_cast<int>(g_Config.m_ClPlayerColorBody) != TargetClient.m_ColorBody)
			{
				g_Config.m_ClPlayerColorBody = TargetClient.m_ColorBody;
				Changed = true;
			}
			if(static_cast<int>(g_Config.m_ClPlayerColorFeet) != TargetClient.m_ColorFeet)
			{
				g_Config.m_ClPlayerColorFeet = TargetClient.m_ColorFeet;
				Changed = true;
			}
		}
		else if(g_Config.m_ClPlayerUseCustomColor != 0)
		{
			g_Config.m_ClPlayerUseCustomColor = 0;
			Changed = true;
		}

		if(Changed)
			SendInfo(false);
	}
}

void CGameClient::HandleRandomGrenadeEmoteOnHit(CCharacter *pLocalChar, int DummyIndex)
{
	if(!g_Config.m_QmRandomEmoteOnHit || !pLocalChar)
		return;
	if(DummyIndex != 0 && !Client()->DummyConnected())
		return;

	const int LocalId = pLocalChar->GetCid();
	int Conn = DummyIndex == 0 ? IClient::CONN_MAIN : IClient::CONN_DUMMY;
	if(LocalId == m_Snap.m_LocalClientId)
		Conn = g_Config.m_ClDummy;
	else if(LocalId == m_PredictedDummyId)
		Conn = !g_Config.m_ClDummy;

	bool GrenadeTriggered = false;
	const int DamageTick = pLocalChar->GetLastDamageTick();
	if(DamageTick > 0 && DamageTick != m_aLastRandomEmoteDamageTick[DummyIndex])
	{
		m_aLastRandomEmoteDamageTick[DummyIndex] = DamageTick;
		const int From = pLocalChar->GetLastDamageFrom();
		if(pLocalChar->GetLastDamageWeapon() == WEAPON_GRENADE && From >= 0 && From != LocalId)
			GrenadeTriggered = true;
	}

	if(GrenadeTriggered)
	{
		const int Emote = rand() % NUM_EMOTICONS;
		CNetMsg_Cl_Emoticon Msg;
		Msg.m_Emoticon = Emote;
		Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
	}
}

void CGameClient::ApplyPreInputs(int Tick, bool Direct, CGameWorld &GameWorld)
{
	if(!g_Config.m_ClAntiPingPreInput)
		return;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(CCharacter *pChar = GameWorld.GetCharacterById(ClientId))
		{
			if(ClientId == m_aLocalIds[0] || (Client()->DummyConnected() && ClientId == m_aLocalIds[1]))
				continue;

			const CNetMsg_Sv_PreInput PreInput = m_aClients[ClientId].m_aPreInputs[Tick % 200];
			if(PreInput.m_IntendedTick != Tick)
				continue;

			//convert preinput to input
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = PreInput.m_Direction;
			Input.m_TargetX = PreInput.m_TargetX;
			Input.m_TargetY = PreInput.m_TargetY;
			Input.m_Jump = PreInput.m_Jump;
			Input.m_Fire = PreInput.m_Fire;
			Input.m_Hook = PreInput.m_Hook;
			Input.m_WantedWeapon = PreInput.m_WantedWeapon;
			Input.m_NextWeapon = PreInput.m_NextWeapon;
			Input.m_PrevWeapon = PreInput.m_PrevWeapon;

			if(Direct)
			{
				pChar->OnDirectInput(&Input);
			}
			else
			{
				pChar->OnPredictedInput(&Input);
			}
		}
	}
}

void CGameClient::OnPredict()
{
	// store the previous values so we can detect prediction errors
	CCharacterCore BeforePrevChar = m_PredictedPrevChar;
	CCharacterCore BeforeChar = m_PredictedChar;

	// we can't predict without our own id or own character
	if(m_Snap.m_LocalClientId == -1 || !m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_Active)
		return;

	// don't predict anything if we are paused
	if(m_Snap.m_pGameInfoObj && m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED)
	{
		if(m_Snap.m_pLocalCharacter)
		{
			m_PredictedChar.Read(m_Snap.m_pLocalCharacter);
			m_PredictedChar.m_ActiveWeapon = m_Snap.m_pLocalCharacter->m_Weapon;
		}
		if(m_Snap.m_pLocalPrevCharacter)
		{
			m_PredictedPrevChar.Read(m_Snap.m_pLocalPrevCharacter);
			m_PredictedPrevChar.m_ActiveWeapon = m_Snap.m_pLocalPrevCharacter->m_Weapon;
		}
		return;
	}

	if(m_FastPractice.Enabled() && m_FastPractice.OverridePredict())
		return;

	vec2 aBeforeRender[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
		aBeforeRender[i] = GetSmoothPos(i);

	// init
	bool Dummy = g_Config.m_ClDummy ^ m_IsDummySwapping;

	// PredictedEvents are only handled in predicted world, so update them here
	m_GameWorld.m_PredictedEvents = m_PredictedWorld.m_PredictedEvents;
	m_PredictedWorld.CopyWorld(&m_GameWorld);

	// don't predict inactive players, or entities from other teams
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(CCharacter *pChar = m_PredictedWorld.GetCharacterById(i))
			if((!m_Snap.m_aCharacters[i].m_Active && pChar->m_SnapTicks > 10) || IsOtherTeam(i))
				pChar->Destroy();

	CProjectile *pProjNext = nullptr;
	for(CProjectile *pProj = (CProjectile *)m_PredictedWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = pProjNext)
	{
		pProjNext = (CProjectile *)pProj->TypeNext();
		if(IsOtherTeam(pProj->GetOwner()))
		{
			pProj->Destroy();
		}
	}

	CCharacter *pLocalChar = m_PredictedWorld.GetCharacterById(m_Snap.m_LocalClientId);
	if(!pLocalChar)
		return;
	CCharacter *pDummyChar = nullptr;
	if(PredictDummy())
		pDummyChar = m_PredictedWorld.GetCharacterById(m_aLocalIds[!g_Config.m_ClDummy]);

	bool RealPredTick = false;
	// predict
	// prediction actually happens here

	const float FastInputOffsetTicks = EffectiveFastInputOffsetTicks(this);
	const int FastInputTicks = FastInputPredictionTicks(FastInputOffsetTicks);
	const bool FastInputOthers = EffectiveFastInputOthers(this);
	const int FastInputTicksOthers = FastInputOthers ? QmFastInputPredictionTicksOthers(FastInputOffsetTicks, g_Config.m_QmFastInputMode) : 0;

	int FinalTickRegular = Client()->PredGameTick(g_Config.m_ClDummy); // The vanilla final tick disregarding fast input
	int FinalTickSelf = FinalTickRegular + FastInputTicks; // the final tick for just our local tee
	int FinalTickOthers = FinalTickRegular + FastInputTicksOthers; // the final tick for all other tees

	int LocalTee = g_Config.m_ClDummy ^ m_IsDummySwapping;
	int DummyTee = LocalTee ^ 1;

	for(int Tick = Client()->GameTick(g_Config.m_ClDummy) + 1; Tick <= FinalTickSelf; Tick++)
	{
		// fetch the previous characters
		if(Tick == FinalTickSelf)
		{
			m_PrevPredictedWorld.CopyWorld(&m_PredictedWorld);
			m_PredictedPrevChar = pLocalChar->GetCore();
			m_aClients[m_Snap.m_LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = m_PredictedWorld.GetCharacterById(i))
					m_aClients[i].m_PrevPredicted = pChar->GetCore();
		}

		if(Tick == Client()->PredGameTick(g_Config.m_ClDummy))
		{
			m_PredictedPrevChar = pLocalChar->GetCore();
			m_aClients[m_Snap.m_LocalClientId].m_PrevPredicted = pLocalChar->GetCore();

			if(pDummyChar)
				m_aClients[m_aLocalIds[!g_Config.m_ClDummy]].m_PrevPredicted = pDummyChar->GetCore();
		}

		if(Tick == FinalTickRegular)
			m_PrevRegularPredictedWorld.CopyWorldClean(&m_PredictedWorld);

		// optionally allow some movement in freeze by not predicting freeze the last one to two ticks
		if(g_Config.m_ClPredictFreeze == 2 && Client()->PredGameTick(g_Config.m_ClDummy) - 1 - Client()->PredGameTick(g_Config.m_ClDummy) % 2 <= Tick)
			pLocalChar->m_CanMoveInFreeze = true;

		// apply inputs and tick
		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, m_IsDummySwapping);
		CNetObj_PlayerInput *pDummyInputData = !pDummyChar ? nullptr : (CNetObj_PlayerInput *)Client()->GetInput(Tick, m_IsDummySwapping ^ 1);
		CNetObj_PlayerInput DummyFastInput = {};
		bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		if(m_TClient.IsFastInputActive() && Tick > FinalTickRegular)
		{
			pInputData = &m_Controls.m_aFastInput[LocalTee];
			if(GetDummyFastInput(DummyFastInput, pDummyInputData, pDummyChar, LocalTee, DummyTee))
				pDummyInputData = &DummyFastInput;
		}

		// Disable predicted events during fast input overrun ticks because these ticks are not real.
		bool TempPredEventState = m_PredictedWorld.m_WorldConfig.m_PredictEvents;
		if(Tick > FinalTickRegular)
			m_PredictedWorld.m_WorldConfig.m_PredictEvents = false;
		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		ApplyPreInputs(Tick, true, m_PredictedWorld);

		m_PredictedWorld.m_GameTick = Tick;
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		ApplyPreInputs(Tick, false, m_PredictedWorld);

		m_PredictedWorld.Tick();
		m_PredictedWorld.m_WorldConfig.m_PredictEvents = TempPredEventState;
		if(Tick <= FinalTickRegular)
		{
			HandleRandomGrenadeEmoteOnHit(pLocalChar, 0);
			if(pDummyChar)
				HandleRandomGrenadeEmoteOnHit(pDummyChar, 1);
		}

		// fetch the current characters
		if(Tick == FinalTickSelf)
		{
			m_PredictedChar = pLocalChar->GetCore();
			m_aClients[m_Snap.m_LocalClientId].m_Predicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = m_PredictedWorld.GetCharacterById(i))
					m_aClients[i].m_Predicted = pChar->GetCore();
		}
		if(Tick == FinalTickRegular)
		{
			m_RegularPredictedWorld.CopyWorldClean(&m_PredictedWorld);
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = m_PredictedWorld.GetCharacterById(i))
					m_aClients[i].m_RegularPredicted = pChar->GetCore();
		}

		if(Tick == Client()->PredGameTick(g_Config.m_ClDummy))
		{
			m_PredictedChar = pLocalChar->GetCore();
			m_aClients[m_Snap.m_LocalClientId].m_Predicted = pLocalChar->GetCore();

			if(pDummyChar)
				m_aClients[m_aLocalIds[!g_Config.m_ClDummy]].m_Predicted = pDummyChar->GetCore();
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = m_PredictedWorld.GetCharacterById(i))
			{
				m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
				m_aClients[i].m_aPredTick[Tick % 200] = Tick;
			}

		// check if we want to trigger effects
		if(Tick > m_aLastNewPredictedTick[Dummy] && (Tick <= FinalTickRegular))
		{
			m_aLastNewPredictedTick[Dummy] = Tick;
			m_NewPredictedTick = true;
			RealPredTick = true;
			vec2 Pos = pLocalChar->Core()->m_Pos;
			int Events = pLocalChar->Core()->m_TriggeredEvents;

			if(g_Config.m_ClPredict && m_PredictedWorld.m_WorldConfig.m_PredictEvents && !m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
				{
					m_aLastPredictedAirJumpTick[Dummy] = Tick;
					m_Effects.AirJump(Pos, 1.0f, 1.0f);
				}
			if(g_Config.m_SndGame && !m_SuppressEvents)
			{
				if(Events & COREEVENT_GROUND_JUMP)
					if(ShouldPlayFocusJumpSound(g_Config.m_QmFocusMode != 0, g_Config.m_QmFocusModeMuteJumpSounds != 0, g_Config.m_SndGame))
						m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_GROUND)
					m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_HIT_NOHOOK)
					m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
				if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
				{
					m_PredictedWorld.CreatePredictedSound(Pos, SOUND_HOOK_ATTACH_PLAYER, pLocalChar->GetCid());
				}
			}
		}

		// check if we want to trigger predicted airjump for dummy
		if(AntiPingPlayers() && pDummyChar && Tick > m_aLastNewPredictedTick[!Dummy] && Tick <= FinalTickRegular)
		{
			m_aLastNewPredictedTick[!Dummy] = Tick;
			vec2 Pos = pDummyChar->Core()->m_Pos;
			int Events = pDummyChar->Core()->m_TriggeredEvents;
			if(g_Config.m_ClPredict && m_PredictedWorld.m_WorldConfig.m_PredictEvents && !m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
				{
					m_aLastPredictedAirJumpTick[!Dummy] = Tick;
					m_Effects.AirJump(Pos, 1.0f, 1.0f);
				}
		}

		HandlePredictedEvents(Tick);
	}

	if(FastInputTicks > 0)
		m_PredictedWorld.CopyWorld(&m_RegularPredictedWorld);

	if(g_Config.m_TcRemoveAnti)
	{
		m_ExtraPredictedWorld.CopyWorldClean(&m_PredictedWorld);

		// Remove other tees to reduce lag and because they aren't really important in this case
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(i != m_Snap.m_LocalClientId)
				if(CCharacter *pDelChar = m_ExtraPredictedWorld.GetCharacterById(i))
					pDelChar->Destroy();

		CCharacter *pExtraChar = m_ExtraPredictedWorld.GetCharacterById(m_Snap.m_LocalClientId);
		if(pExtraChar)
		{
			bool Unfrozen = false;
			bool Frozen = false;
			for(int i = 0; i < g_Config.m_TcUnfreezeLagDelayTicks; i++)
			{
				if(!pExtraChar)
					continue;

				if(Frozen && pExtraChar->m_AliveAccumulation > 0)
					Unfrozen = true;

				if(pExtraChar->m_AliveAccumulation < 0)
					Frozen = true;

				if(!Unfrozen)
				{
					m_ExtraPredictedWorld.m_GameTick++;
					m_ExtraPredictedWorld.Tick();
				}
				else
				{
					pExtraChar->m_AliveAccumulation = std::max(pExtraChar->m_AliveAccumulation, 1);
					pExtraChar->m_AliveAccumulation = std::min(pExtraChar->m_AliveAccumulation + 1, g_Config.m_TcUnfreezeLagDelayTicks);
				}
			}
		}

		HandlePredictedEvents(m_ExtraPredictedWorld.m_GameTick);
	}

	// detect mispredictions of other players and make corrections smoother when possible
	if(g_Config.m_ClAntiPingSmooth &&
		Predict() && AntiPingPlayers() &&
		m_NewTick && m_PredictedTick >= MIN_TICK &&
		absolute(m_PredictedTick - Client()->PredGameTick(g_Config.m_ClDummy)) <= 1 &&
		absolute(Client()->GameTick(g_Config.m_ClDummy) - Client()->PrevGameTick(g_Config.m_ClDummy)) <= 2)
	{
		int PredTime = std::clamp(Client()->GetPredictionTime(), 0, 800);
		float SmoothPace = 4 - 1.5f * PredTime / 800.f; // smoothing pace (a lower value will make the smoothing quicker)
		int64_t Len = 1000 * PredTime * SmoothPace;

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_Snap.m_aCharacters[i].m_Active || i == m_Snap.m_LocalClientId || !m_aLastActive[i])
				continue;
			vec2 NewPos = m_aClients[i].m_Predicted.m_Pos;
			vec2 PredErr = (m_aLastPos[i] - NewPos) / (float)minimum(Client()->GetPredictionTime(), 200);
			if(in_range(length(PredErr), 0.05f, 5.f))
			{
				vec2 PredPos = mix(m_aClients[i].m_PrevPredicted.m_Pos, m_aClients[i].m_Predicted.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
				vec2 CurPos = mix(
					vec2(m_Snap.m_aCharacters[i].m_Prev.m_X, m_Snap.m_aCharacters[i].m_Prev.m_Y),
					vec2(m_Snap.m_aCharacters[i].m_Cur.m_X, m_Snap.m_aCharacters[i].m_Cur.m_Y),
					Client()->IntraGameTick(g_Config.m_ClDummy));
				vec2 RenderDiff = PredPos - aBeforeRender[i];
				vec2 PredDiff = PredPos - CurPos;

				float aMixAmount[2];
				for(int j = 0; j < 2; j++)
				{
					aMixAmount[j] = 1.0f;
					if(absolute(PredErr[j]) > 0.05f)
					{
						aMixAmount[j] = 0.0f;
						if(absolute(RenderDiff[j]) > 0.01f)
						{
							aMixAmount[j] = 1.f - std::clamp(RenderDiff[j] / PredDiff[j], 0.f, 1.f);
							aMixAmount[j] = 1.f - std::pow(1.f - aMixAmount[j], 1 / 1.2f);
						}
					}
					int64_t TimePassed = time_get() - m_aClients[i].m_aSmoothStart[j];
					if(in_range(TimePassed, (int64_t)0, Len - 1))
						aMixAmount[j] = minimum(aMixAmount[j], (float)(TimePassed / (double)Len));
				}
				for(int j = 0; j < 2; j++)
					if(absolute(RenderDiff[j]) < 0.01f && absolute(PredDiff[j]) < 0.01f && absolute(m_aClients[i].m_PrevPredicted.m_Pos[j] - m_aClients[i].m_Predicted.m_Pos[j]) < 0.01f && aMixAmount[j] > aMixAmount[j ^ 1])
						aMixAmount[j] = aMixAmount[j ^ 1];
				for(int j = 0; j < 2; j++)
				{
					int64_t Remaining = minimum((1.f - aMixAmount[j]) * Len, minimum(time_freq() * 0.700f, (1.f - aMixAmount[j ^ 1]) * Len + time_freq() * 0.300f)); // don't smooth for longer than 700ms, or more than 300ms longer along one axis than the other axis
					int64_t Start = time_get() - (Len - Remaining);
					if(!in_range(Start + Len, m_aClients[i].m_aSmoothStart[j], m_aClients[i].m_aSmoothStart[j] + Len))
					{
						m_aClients[i].m_aSmoothStart[j] = Start;
						m_aClients[i].m_aSmoothLen[j] = Len;
					}
				}
			}
		}
	}

	// TClient
	// New antiping smoothing
	CCharacter *pSmoothLocalChar = m_PredSmoothingWorld.GetCharacterById(m_Snap.m_LocalClientId);
	if(g_Config.m_TcAntiPingImproved &&
		Predict() && AntiPingPlayers() &&
		pSmoothLocalChar &&
		RealPredTick && m_PredictedTick >= MIN_TICK)
	{
		int PredTime = std::clamp(Client()->GetPredictionTime(), 0, 8000); // Milliseconds for some reason?? TODO: Use more precision
		const int PredEndTick = FinalTickRegular;
		const int SmoothTick = PredEndTick;

		// Nightmare: in order to get 100% accurate comparison to detect mispredictions we must
		// tick the PREVIOUS predicted world with our CURRENT predicted inputs
		CCharacter *pSmoothDummyChar = 0;
		CCharacter *pPredDummyChar = 0;
		if(PredictDummy())
		{
			pSmoothDummyChar = m_PredSmoothingWorld.GetCharacterById(m_PredictedDummyId);
			pPredDummyChar = m_PredictedWorld.GetCharacterById(m_PredictedDummyId);
		}
		CNetObj_PlayerInput *pInputData = m_PredictedWorld.GetCharacterById(m_Snap.m_LocalClientId)->LatestInput();
		CNetObj_PlayerInput *pDummyInputData = !pPredDummyChar ? 0 : m_PredictedWorld.GetCharacterById(m_PredictedDummyId)->LatestInput();
		bool DummyFirst = pSmoothLocalChar && pSmoothDummyChar && pSmoothDummyChar->GetCid() < pSmoothLocalChar->GetCid();
		if(DummyFirst && pSmoothDummyChar && pDummyInputData)
			pSmoothDummyChar->OnDirectInput(pDummyInputData);

		if(pInputData && pSmoothLocalChar)
			pSmoothLocalChar->OnDirectInput(pInputData);

		if(!DummyFirst && pSmoothDummyChar && pDummyInputData)
			pSmoothDummyChar->OnDirectInput(pDummyInputData);

		ApplyPreInputs(SmoothTick, true, m_PredSmoothingWorld);
		m_PredSmoothingWorld.m_GameTick = SmoothTick;

		if(pInputData && pSmoothLocalChar)
			pSmoothLocalChar->OnPredictedInput(pInputData);

		if(pDummyInputData && pSmoothDummyChar)
			pSmoothDummyChar->OnPredictedInput(pDummyInputData);
		ApplyPreInputs(SmoothTick, false, m_PredSmoothingWorld);
		m_PredSmoothingWorld.Tick();

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_Snap.m_aCharacters[i].m_Active || !m_aLastActive[i])
			{
				m_aClients[i].m_ValidAntipingSmooth = false;
				continue;
			}

			if(i == m_PredictedDummyId || i == m_Snap.m_LocalClientId)
			{
				m_aClients[i].m_PrevImprovedPredPos = m_aClients[i].m_PrevPredicted.m_Pos;
				m_aClients[i].m_ImprovedPredPos = m_aClients[i].m_Predicted.m_Pos;
				continue;
			}

			CCharacter *pChar = m_PredSmoothingWorld.GetCharacterById(i);
			if(!pChar)
				continue;

			vec2 PredPos = m_aClients[i].m_RegularPredicted.m_Pos;

			vec2 PrevPredPos = pChar->GetCore().m_Pos;

			// Cursed hack to get the game tick consistently
			int GameTick = Client()->GameTick(g_Config.m_ClDummy) + (int)Client()->IntraGameTick(g_Config.m_ClDummy);
			static int s_PrevGameTick = 0;
			if(s_PrevGameTick == GameTick)
				GameTick++;
			s_PrevGameTick = Client()->GameTick(g_Config.m_ClDummy) + (int)Client()->IntraGameTick(g_Config.m_ClDummy);

			vec2 ServerPos = m_aClients[i].m_aPredPos[GameTick % 200];
			vec2 PrevServerPos = m_aClients[i].m_aPredPos[(GameTick - 1) % 200];

			vec2 PredDir = normalize(PredPos - ServerPos);
			vec2 LastDir = normalize(PrevPredPos - ServerPos);

			vec2 MaxPos = vec2(0, 0);
			vec2 MinPos = vec2(0, 0);
			bool FoundBoundingBox = false;
			// Get a bounding box for our final prediction position to minimize going through walls
			for(int Tick = GameTick - 1; Tick <= PredEndTick; Tick++)
			{
				if(m_aClients[i].m_aPredTick[Tick % 200] != Tick)
					continue;
				vec2 Pos = m_aClients[i].m_aPredPos[Tick % 200];
				if(!FoundBoundingBox)
				{
					MaxPos = Pos;
					MinPos = Pos;
					FoundBoundingBox = true;
				}
				else
				{
					MaxPos.x = std::max(Pos.x, MaxPos.x);
					MaxPos.y = std::max(Pos.y, MaxPos.y);
					MinPos.x = std::min(Pos.x, MinPos.x);
					MinPos.y = std::min(Pos.y, MinPos.y);
				}
			}
			int PredStartTick = GameTick;
			int HistoryStartTick = PredStartTick - (PredEndTick - PredStartTick);
			HistoryStartTick = std::max(1, HistoryStartTick);
			vec2 HistoryVector = vec2(0, 0);
			float HistoryDistance = 0.0f;
			int HistoryCount = 0;
			// Find the average history vector
			for(int Tick = HistoryStartTick; Tick <= PredStartTick; Tick++)
			{
				if(m_aClients[i].m_aPredTick[Tick % 200] != Tick || m_aClients[i].m_aPredTick[(Tick - 1) % 200] != Tick - 1)
					continue;
				vec2 DirVector = m_aClients[i].m_aPredPos[Tick % 200] - m_aClients[i].m_aPredPos[(Tick - 1) % 200];
				HistoryVector += DirVector;
				HistoryDistance += length(DirVector);
				HistoryCount++;
			}

			bool ValidRecentPositions = m_aClients[i].m_aPredTick[GameTick % 200] == GameTick && m_aClients[i].m_aPredTick[(GameTick - 1) % 200] == GameTick - 1;
			// Not enough history data.
			if(!ValidRecentPositions || !FoundBoundingBox || HistoryCount == 0 || HistoryDistance <= 0.0f || GameTick <= 0)
			{
				m_aClients[i].m_PrevImprovedPredPos = PrevPredPos;
				m_aClients[i].m_ImprovedPredPos = PredPos;
				m_aClients[i].m_ValidAntipingSmooth = false;
				continue;
			}

			HistoryVector = HistoryVector / HistoryCount;
			HistoryVector = normalize(HistoryVector);
			float Variance = 0.0f;
			// Find the variance over the history window
			if(length(HistoryVector) > 0.0f)
			{
				for(int Tick = HistoryStartTick; Tick <= PredStartTick; Tick++)
				{
					if(m_aClients[i].m_aPredTick[Tick % 200] != Tick || m_aClients[i].m_aPredTick[(Tick - 1) % 200] != Tick - 1)
						continue;
					vec2 DirVector = m_aClients[i].m_aPredPos[Tick % 200] - m_aClients[i].m_aPredPos[(Tick - 1) % 200];
					vec2 Diff = normalize(DirVector) - HistoryVector;
					Variance += dot(Diff, Diff);
				}
				Variance /= HistoryCount;
			}
			else
			{
				Variance = 0.0f;
			}
			float Sigma = 1.5f; // Can be adjusted
			float SigmaScale = length(PredPos - ServerPos) / HistoryDistance;
			if(SigmaScale > 0)
				Sigma /= SigmaScale;
			float TrustFactor = std::max(0.0f, 1.0f - (std::sqrt(Variance) / Sigma));
			vec2 TrustedVector = HistoryVector;

			// Detect mispredictions
			float Confidence = 1.0f;
			if(PredDir == vec2(0, 0))
			{
				Confidence = 1.0f;
			}
			else
			{
				Confidence = std::max(0.0f, dot(LastDir, PredDir));
				Confidence = std::pow(Confidence, 4.0f); // Can be adjusted
			}
			float Uncertainty = 1.0f - Confidence;
			float TickDuration = (float)1000 / (float)Client()->GameTickSpeed();

			// Manage uncertainty value
			float PredTimeScale = (float)g_Config.m_TcAntiPingUncertaintyScale / 100.0f;
			float TickSize = TickDuration / ((float)PredTime * PredTimeScale); // 20ms / PredTime
			float PrevConfidence = 1.0f - m_aClients[i].m_Uncertainty;
			float NewConfidence = PrevConfidence - Uncertainty + TickSize;
			float MinConfidence = g_Config.m_TcAntiPingNegativeBuffer ? -1.0f : 0.0f;
			NewConfidence = std::clamp(NewConfidence, MinConfidence, 1.0f); // A certain about of "negative buffer" is allowed
			m_aClients[i].m_Uncertainty = 1.0f - NewConfidence;
			NewConfidence = std::max(0.0f, NewConfidence);

			// Decompose prediction vector into 2 components based on the trusted vector
			vec2 PredVector = PredPos - ServerPos;
			vec2 Forward = normalize(TrustedVector);
			float DotPf = std::max(0.0f, dot(normalize(PredVector), Forward));
			vec2 ConfidenceParallel = Forward * DotPf * length(PredVector);
			if(DotPf == 0.0f)
				ConfidenceParallel = vec2(0, 0);
			vec2 ConfidencePerp = PredVector - ConfidenceParallel;

			if(!g_Config.m_TcAntiPingStableDirection)
				TrustFactor = 0.0f;

			vec2 ConfidenceVector = ConfidenceParallel * std::max(TrustFactor, NewConfidence) + ConfidencePerp * NewConfidence;

			// Minor safe guard against insane predictions
			if(length(ConfidenceVector) > HistoryDistance)
				ConfidenceVector = mix(normalize(ConfidenceVector) * HistoryDistance, ConfidenceVector, NewConfidence);

			vec2 ConfidencePos = ServerPos + ConfidenceVector;

			// Clamp final position to bounding box
			ConfidencePos.x = std::clamp(ConfidencePos.x, MinPos.x, MaxPos.x);
			ConfidencePos.y = std::clamp(ConfidencePos.y, MinPos.y, MaxPos.y);

			m_aClients[i].m_PrevImprovedPredPos = m_aClients[i].m_ImprovedPredPos;
			m_aClients[i].m_ImprovedPredPos = ConfidencePos;
			if(distance(ServerPos, PrevServerPos) > 600.0f || distance(m_aClients[i].m_PrevImprovedPredPos, m_aClients[i].m_ImprovedPredPos) > 600.0f)
			{
				m_aClients[i].m_PrevImprovedPredPos = m_aClients[i].m_ImprovedPredPos;
			}
			m_aClients[i].m_ValidAntipingSmooth = true;
		}
	}
	// Copy the current pred world so on the next tick we have the "previous" pred world to advance and test against
	if(m_NewPredictedTick && g_Config.m_TcAntiPingImproved)
		m_PredSmoothingWorld.CopyWorldClean(&m_RegularPredictedWorld);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_Snap.m_aCharacters[i].m_Active)
		{
			if(m_NewPredictedTick)
			{
				m_aLastPos[i] = m_aClients[i].m_Predicted.m_Pos;
				m_aLastActive[i] = true;
			}
		}
		else
		{
			m_aLastActive[i] = false;
		}
	}

	if(g_Config.m_Debug && g_Config.m_ClPredict && FastInputTicks == 0 && m_PredictedTick == Client()->PredGameTick(g_Config.m_ClDummy))
	{
		CNetObj_CharacterCore Before = {0}, Now = {0}, BeforePrev = {0}, NowPrev = {0};
		BeforeChar.Write(&Before);
		BeforePrevChar.Write(&BeforePrev);
		m_PredictedChar.Write(&Now);
		m_PredictedPrevChar.Write(&NowPrev);

		if(mem_comp(&Before, &Now, sizeof(CNetObj_CharacterCore)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", "prediction error");
			for(unsigned i = 0; i < sizeof(CNetObj_CharacterCore) / sizeof(int); i++)
				if(((int *)&Before)[i] != ((int *)&Now)[i])
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "	%d %d %d (%d %d)", i, ((int *)&Before)[i], ((int *)&Now)[i], ((int *)&BeforePrev)[i], ((int *)&NowPrev)[i]);
					Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
				}
		}
	}

	m_PredictedTick = FinalTickRegular;

	if(m_NewPredictedTick)
		m_Ghost.OnNewPredictedSnapshot();
}

void CGameClient::OnActivateEditor()
{
	OnRelease();
}

CGameClient::CClientStats::CClientStats()
{
	Reset();
}

bool CGameClient::ShouldUseServerControlledLocalSkin() const
{
	CServerInfo ServerInfo = {};
	Client()->GetServerInfo(&ServerInfo);

	const char *pServerInfoGameType = ServerInfo.m_aGameType;
	const char *pCommunityId = ServerInfo.m_aCommunityId;
	IServerBrowser *pServerBrowser = ServerBrowser();
	const NETADDR *pServerAddress = Client()->ServerAddress();
	const IServerBrowser::CServerEntry *pEntry = pServerBrowser != nullptr && pServerAddress != nullptr ? pServerBrowser->Find(*pServerAddress) : nullptr;
	if(pEntry != nullptr)
	{
		if(pServerInfoGameType[0] == '\0')
			pServerInfoGameType = pEntry->m_Info.m_aGameType;
		if(pEntry->m_Info.m_aCommunityId[0] != '\0')
			pCommunityId = pEntry->m_Info.m_aCommunityId;
	}
	if(m_ConnectServerInfo.has_value())
	{
		if(pServerInfoGameType[0] == '\0')
			pServerInfoGameType = m_ConnectServerInfo->m_aGameType;
		if(m_ConnectServerInfo->m_aCommunityId[0] != '\0' &&
			(pCommunityId[0] == '\0' || str_comp(pCommunityId, IServerBrowser::COMMUNITY_NONE) == 0))
			pCommunityId = m_ConnectServerInfo->m_aCommunityId;
	}

	const CCommunity *pCommunity = pServerBrowser != nullptr && pCommunityId[0] != '\0' ? pServerBrowser->Community(pCommunityId) : nullptr;
	return ::ShouldUseServerControlledLocalSkin(
		m_GameInfo.m_aGameType,
		pServerInfoGameType,
		pCommunityId,
		pCommunity != nullptr ? pCommunity->Name() : nullptr);
}

void CGameClient::CClientStats::Reset()
{
	m_JoinTick = 0;
	m_IngameTicks = 0;
	m_Active = false;

	std::fill(std::begin(m_aFragsWith), std::end(m_aFragsWith), 0);
	std::fill(std::begin(m_aDeathsFrom), std::end(m_aDeathsFrom), 0);
	m_Frags = 0;
	m_Deaths = 0;
	m_Suicides = 0;
	m_BestSpree = 0;
	m_CurrentSpree = 0;

	m_FlagGrabs = 0;
	m_FlagCaptures = 0;
}

int CGameClient::CClientData::LocalSkinConfigIndex() const
{
	if(m_pGameClient == nullptr)
	{
		return -1;
	}
	return ResolveLocalSkinConfigIndex(
		m_pGameClient->m_pClient->State() == IClient::STATE_DEMOPLAYBACK,
		m_ClientId,
		m_pGameClient->m_aLocalIds[0],
		m_pGameClient->m_aLocalIds[1]);
}

void CGameClient::CClientData::BuildLocalSkinDescriptor(CSkinDescriptor &SkinDescriptor, int Dummy) const
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES)
	{
		return;
	}

	CTranslationContext::CClientData &TranslatedClient = m_pGameClient->m_pClient->m_TranslationContext.m_aClients[ClientId()];
	if(m_Active && !TranslatedClient.m_Active)
	{
		SkinDescriptor.m_Flags |= CSkinDescriptor::FLAG_SIX;
		str_copy(SkinDescriptor.m_aSkinName, Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin);
		NormalizeSixupSkinName(SkinDescriptor.m_aSkinName, sizeof(SkinDescriptor.m_aSkinName));
	}
	else if(TranslatedClient.m_Active)
	{
		SkinDescriptor.m_Flags |= CSkinDescriptor::FLAG_SEVEN;
		for(int SkinDummy = 0; SkinDummy < NUM_DUMMIES; ++SkinDummy)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				str_copy(SkinDescriptor.m_aSixup[SkinDummy].m_aaSkinPartNames[Part], CSkins7::ms_apSkinVariables[Dummy][Part]);
			}
			SkinDescriptor.m_aSixup[SkinDummy].m_XmasHat = time_season() == ETimeSeason::XMAS;
			SkinDescriptor.m_aSixup[SkinDummy].m_BotDecoration = (TranslatedClient.m_PlayerFlags7 & protocol7::PLAYERFLAG_BOT) != 0;
		}
	}
}

void CGameClient::CClientData::UpdateSkinInfo()
{
	const CSkinDescriptor SkinDescriptor = ToSkinDescriptor();
	if(SkinDescriptor.m_Flags == 0)
	{
		return;
	}

	const auto &&ApplySkinProperties = [&]() {
		const int LocalDummy = LocalSkinConfigIndex();
		const bool UseServerControlledSkin = LocalDummy >= 0 && m_pGameClient->ShouldUseServerControlledLocalSkin();
		if(SkinDescriptor.m_Flags & CSkinDescriptor::FLAG_SIX)
		{
			if(UseServerControlledSkin)
			{
				m_pSkinInfo->TeeRenderInfo().ApplyColors(m_UseCustomColor, m_ColorBody, m_ColorFeet);
			}
			else if(LocalDummy >= 0)
			{
				m_pSkinInfo->TeeRenderInfo().ApplyColors(
					LocalDummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor,
					LocalDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody,
					LocalDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
			}
			else
			{
				m_pSkinInfo->TeeRenderInfo().ApplyColors(m_UseCustomColor, m_ColorBody, m_ColorFeet);
			}
		}
		if(SkinDescriptor.m_Flags & CSkinDescriptor::FLAG_SEVEN)
		{
			for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
			{
				const CClientData::CSixup &SixupData = m_aSixup[Dummy];
				CTeeRenderInfo::CSixup &SixupSkinInfo = m_pSkinInfo->TeeRenderInfo().m_aSixup[Dummy];
				for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
				{
					if(UseServerControlledSkin)
					{
						m_pGameClient->m_Skins7.ApplyColorTo(SixupSkinInfo, m_aSixup[LocalDummy].m_aUseCustomColors[Part], m_aSixup[LocalDummy].m_aSkinPartColors[Part], Part);
					}
					else if(LocalDummy >= 0)
					{
						m_pGameClient->m_Skins7.ApplyColorTo(SixupSkinInfo, *CSkins7::ms_apUCCVariables[LocalDummy][Part], *CSkins7::ms_apColorVariables[LocalDummy][Part], Part);
					}
					else
					{
						m_pGameClient->m_Skins7.ApplyColorTo(SixupSkinInfo, SixupData.m_aUseCustomColors[Part], SixupData.m_aSkinPartColors[Part], Part);
					}
				}
				UpdateSkin7HatSprite(Dummy);
				UpdateSkin7BotDecoration(Dummy);
			}
		}
		m_pSkinInfo->TeeRenderInfo().m_Size = 64.0f;
	};

	if(m_pSkinInfo == nullptr)
	{
		CTeeRenderInfo TeeRenderInfo;
		m_pSkinInfo = m_pGameClient->CreateManagedTeeRenderInfo(TeeRenderInfo, SkinDescriptor);
		m_pSkinInfo->SetRefreshCallback([&]() { UpdateRenderInfo(); });
		ApplySkinProperties();
		m_pSkinInfo->m_RefreshCallback();
	}
	else if(m_pSkinInfo->SkinDescriptor() != SkinDescriptor)
	{
		m_pSkinInfo->m_SkinDescriptor = SkinDescriptor;
		m_pGameClient->RefreshSkin(m_pSkinInfo);
		ApplySkinProperties();
		m_pSkinInfo->m_RefreshCallback();
	}
	else
	{
		ApplySkinProperties();
		m_pSkinInfo->m_RefreshCallback();
	}
}

namespace
{
	void BuildDefaultSkinDescriptor(CSkinDescriptor &Out)
	{
		Out.Reset();
		Out.m_Flags = CSkinDescriptor::FLAG_SIX | CSkinDescriptor::FLAG_SEVEN;
		str_copy(Out.m_aSkinName, "default");
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				const char *pPartName = (Part == protocol7::SKINPART_MARKING || Part == protocol7::SKINPART_DECORATION) ? "" : "standard";
				str_copy(Out.m_aSixup[Dummy].m_aaSkinPartNames[Part], pPartName);
			}
			Out.m_aSixup[Dummy].m_BotDecoration = false;
			Out.m_aSixup[Dummy].m_XmasHat = false;
		}
	}

	bool ApplyDefaultSkin(CGameClient *pGameClient, CTeeRenderInfo &Info)
	{
		if(!pGameClient)
			return false;

		Info.Reset();
		if(const CSkin *pSkin = pGameClient->m_Skins.FindOrNullptr("default"))
		{
			Info.Apply(pSkin);
		}

		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				const CSkins7::CSkinPart *pPart = pGameClient->m_Skins7.FindDefaultSkinPart(Part);
				if(pPart)
					pPart->ApplyTo(Info.m_aSixup[Dummy]);
				pGameClient->m_Skins7.ApplyColorTo(Info.m_aSixup[Dummy], false, 0, Part);
			}
			Info.m_aSixup[Dummy].m_HatTexture.Invalidate();
			Info.m_aSixup[Dummy].m_BotTexture.Invalidate();
		}
		return Info.SixDescriptorReady() || Info.SevenDescriptorReady();
	}

	void CopySkinColorsOnly(CTeeRenderInfo &Target, const CTeeRenderInfo &Source)
	{
		Target.m_CustomColoredSkin = Source.m_CustomColoredSkin;
		Target.m_ColorBody = Source.m_ColorBody;
		Target.m_ColorFeet = Source.m_ColorFeet;
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				Target.m_aSixup[Dummy].m_aUseCustomColors[Part] = Source.m_aSixup[Dummy].m_aUseCustomColors[Part];
				Target.m_aSixup[Dummy].m_aColors[Part] = Source.m_aSixup[Dummy].m_aColors[Part];
			}
		}
	}
}

void CGameClient::CClientData::UpdateRenderInfo()
{
	const CSkinDescriptor SkinDescriptor = ToSkinDescriptor();
	CSkinDescriptor RenderSkinDescriptor = SkinDescriptor;
	CTeeRenderInfo NewRenderInfo = m_pSkinInfo->TeeRenderInfo();
	const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();
	if(m_RenderInfoSkinDescriptor != SkinDescriptor && m_RenderInfoFallbackResidencyDescriptor != SkinDescriptor)
		m_RenderInfoFallbackResidencyRequested = false;
	if(!DescriptorRenderInfoReady && m_RenderInfo.Valid())
	{
		if(!m_RenderInfoFallbackResidencyRequested && m_RenderInfoSkinDescriptor.m_aSkinName[0] != '\0')
		{
			m_pGameClient->m_Skins.FindContainerOrNullptr(m_RenderInfoSkinDescriptor.m_aSkinName);
			m_RenderInfoFallbackResidencyRequested = true;
			m_RenderInfoFallbackResidencyDescriptor = SkinDescriptor;
		}
		const CTeeRenderInfo SkinProperties = NewRenderInfo;
		NewRenderInfo = m_RenderInfo;
		CopySkinColorsOnly(NewRenderInfo, SkinProperties);
	}
	else if(!DescriptorRenderInfoReady)
	{
		const float OriginalSize = NewRenderInfo.m_Size;
		BuildDefaultSkinDescriptor(RenderSkinDescriptor);
		if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo))
			NewRenderInfo.Reset();
		NewRenderInfo.m_Size = OriginalSize;
	}

	// force team colors
	if(m_pGameClient->IsTeamPlay())
	{
		NewRenderInfo.m_CustomColoredSkin = true;
		for(auto &Sixup : NewRenderInfo.m_aSixup)
		{
			std::fill(std::begin(Sixup.m_aUseCustomColors), std::end(Sixup.m_aUseCustomColors), true);
		}

		if(m_Team >= TEAM_RED && m_Team <= TEAM_BLUE)
		{
			const int aTeamColors[2] = {65461, 10223541};
			NewRenderInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(aTeamColors[m_Team]));
			NewRenderInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(aTeamColors[m_Team]));

			// 0.7
			for(auto &Sixup : NewRenderInfo.m_aSixup)
			{
				const ColorRGBA aTeamColorsSixup[2] = {
					ColorRGBA(0.753f, 0.318f, 0.318f, 1.0f),
					ColorRGBA(0.318f, 0.471f, 0.753f, 1.0f)};
				const ColorRGBA aMarkingColorsSixup[2] = {
					ColorRGBA(0.824f, 0.345f, 0.345f, 1.0f),
					ColorRGBA(0.345f, 0.514f, 0.824f, 1.0f)};
				float MarkingAlpha = Sixup.m_aColors[protocol7::SKINPART_MARKING].a;
				for(auto &Color : Sixup.m_aColors)
				{
					Color = aTeamColorsSixup[m_Team];
				}
				if(MarkingAlpha > 0.1f)
				{
					Sixup.m_aColors[protocol7::SKINPART_MARKING] = aMarkingColorsSixup[m_Team];
				}
			}
		}
		else
		{
			NewRenderInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(12829350));
			NewRenderInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(12829350));
			for(auto &Sixup : NewRenderInfo.m_aSixup)
			{
				for(auto &Color : Sixup.m_aColors)
				{
					Color = color_cast<ColorRGBA>(ColorHSLA(12829350));
				}
			}
		}
	}

	if(m_pGameClient != nullptr && m_pGameClient->ShouldHideStreamerSkin(m_ClientId))
	{
		CTeeRenderInfo OriginalInfo = NewRenderInfo;
		const float OriginalSize = NewRenderInfo.m_Size;
		const bool KeepTeamColors = m_pGameClient->IsTeamPlay();

		if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo))
			NewRenderInfo.Reset();
		NewRenderInfo.m_Size = OriginalSize;

		if(KeepTeamColors)
		{
			NewRenderInfo.m_CustomColoredSkin = true;
			NewRenderInfo.m_ColorBody = OriginalInfo.m_ColorBody;
			NewRenderInfo.m_ColorFeet = OriginalInfo.m_ColorFeet;
			for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
			{
				for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
				{
					NewRenderInfo.m_aSixup[Dummy].m_aUseCustomColors[Part] = OriginalInfo.m_aSixup[Dummy].m_aUseCustomColors[Part];
					NewRenderInfo.m_aSixup[Dummy].m_aColors[Part] = OriginalInfo.m_aSixup[Dummy].m_aColors[Part];
				}
			}
		}
	}

	// 异步资源尚未提交时，继续使用上一份资源身份；否则会在空白窗口期间提前播放切换动画。
	if(!DescriptorRenderInfoReady)
		RenderSkinDescriptor = m_RenderInfoSkinDescriptor;
	const auto SameTexture = [](const IGraphics::CTextureHandle &Left, const IGraphics::CTextureHandle &Right) {
		return Left.Id() == Right.Id() && Left.Generation() == Right.Generation();
	};
	bool ResourceChanged = !SameTexture(m_RenderInfo.m_OriginalRenderSkin.m_Body, NewRenderInfo.m_OriginalRenderSkin.m_Body) ||
			       !SameTexture(m_RenderInfo.m_ColorableRenderSkin.m_Body, NewRenderInfo.m_ColorableRenderSkin.m_Body);
	const auto SameSkinTextures = [&SameTexture](const CSkin::CSkinTextures &Left, const CSkin::CSkinTextures &Right) {
		return SameTexture(Left.m_Body, Right.m_Body) && SameTexture(Left.m_BodyOutline, Right.m_BodyOutline) &&
		       SameTexture(Left.m_Feet, Right.m_Feet) && SameTexture(Left.m_FeetOutline, Right.m_FeetOutline) &&
		       SameTexture(Left.m_Hands, Right.m_Hands) && SameTexture(Left.m_HandsOutline, Right.m_HandsOutline) &&
		       std::equal(std::begin(Left.m_aEyes), std::end(Left.m_aEyes), std::begin(Right.m_aEyes), [&SameTexture](const auto &A, const auto &B) { return SameTexture(A, B); });
	};
	ResourceChanged = !SameSkinTextures(m_RenderInfo.m_OriginalRenderSkin, NewRenderInfo.m_OriginalRenderSkin) ||
			  !SameSkinTextures(m_RenderInfo.m_ColorableRenderSkin, NewRenderInfo.m_ColorableRenderSkin);
	for(int Dummy = 0; Dummy < NUM_DUMMIES && !ResourceChanged; ++Dummy)
	{
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS && !ResourceChanged; ++Part)
		{
			ResourceChanged = !SameTexture(m_RenderInfo.m_aSixup[Dummy].m_aOriginalTextures[Part], NewRenderInfo.m_aSixup[Dummy].m_aOriginalTextures[Part]) ||
					  !SameTexture(m_RenderInfo.m_aSixup[Dummy].m_aColorableTextures[Part], NewRenderInfo.m_aSixup[Dummy].m_aColorableTextures[Part]);
		}
	}
	if(DescriptorRenderInfoReady && (m_RenderInfoSkinDescriptor != SkinDescriptor || ResourceChanged))
		++m_RenderInfoSkinGeneration;
	UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);
	m_RenderInfo = NewRenderInfo;
	m_RenderInfoSkinDescriptor = DescriptorRenderInfoReady ? SkinDescriptor : m_RenderInfoSkinDescriptor;
	if(DescriptorRenderInfoReady)
	{
		m_RenderInfoFallbackResidencyRequested = false;
		m_RenderInfoFallbackResidencyDescriptor = {};
	}
}

void CGameClient::CClientData::UpdateSkinChangeTransition(const CTeeRenderInfo &NewRenderInfo, const CSkinDescriptor &SkinDescriptor)
{
	CSkinTransitionKey Key;
	Key.m_SkinDescriptor = SkinDescriptor;
	Key.m_SkinGeneration = m_RenderInfoSkinGeneration;
	const int LocalDummy = LocalSkinConfigIndex();
	const bool UseServerControlledSkin = LocalDummy >= 0 && m_pGameClient->ShouldUseServerControlledLocalSkin();
	Key.m_UseCustomColor = UseServerControlledSkin ? m_UseCustomColor : (LocalDummy >= 0 ? (LocalDummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor) : m_UseCustomColor);
	Key.m_ColorBody = UseServerControlledSkin ? m_ColorBody : (LocalDummy >= 0 ? (LocalDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : m_ColorBody);
	Key.m_ColorFeet = UseServerControlledSkin ? m_ColorFeet : (LocalDummy >= 0 ? (LocalDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : m_ColorFeet);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
		{
			if(UseServerControlledSkin)
			{
				Key.m_aaSixupUseCustomColors[Dummy][Part] = m_aSixup[LocalDummy].m_aUseCustomColors[Part];
				Key.m_aaSixupSkinPartColors[Dummy][Part] = m_aSixup[LocalDummy].m_aSkinPartColors[Part];
			}
			else if(LocalDummy >= 0)
			{
				Key.m_aaSixupUseCustomColors[Dummy][Part] = *CSkins7::ms_apUCCVariables[LocalDummy][Part];
				Key.m_aaSixupSkinPartColors[Dummy][Part] = *CSkins7::ms_apColorVariables[LocalDummy][Part];
			}
			else
			{
				Key.m_aaSixupUseCustomColors[Dummy][Part] = m_aSixup[Dummy].m_aUseCustomColors[Part];
				Key.m_aaSixupSkinPartColors[Dummy][Part] = m_aSixup[Dummy].m_aSkinPartColors[Part];
			}
		}
	}
	if(m_pGameClient != nullptr && m_pGameClient->ShouldHideStreamerSkin(m_ClientId))
	{
		m_LastSkinTransitionKey = Key;
		m_HasSkinTransitionKey = true;
		m_SkinTransitionPreviousRenderInfo.Reset();
		m_SkinTransitionStart.reset();
		return;
	}

	const bool IsTransitionClient = m_pGameClient != nullptr && m_pGameClient->ShouldRunSkinChangeTransition(m_ClientId);
	if(!IsTransitionClient)
	{
		m_LastSkinTransitionKey = Key;
		m_HasSkinTransitionKey = true;
		m_SkinTransitionPreviousRenderInfo.Reset();
		m_SkinTransitionStart.reset();
		return;
	}

	if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0)
	{
		m_LastSkinTransitionKey = Key;
		m_HasSkinTransitionKey = true;
		m_SkinTransitionPreviousRenderInfo.Reset();
		m_SkinTransitionStart.reset();
		return;
	}

	const ESkinChangeTransitionAction Action = ResolveSkinChangeTransitionAction(
		m_HasSkinTransitionKey,
		!(m_LastSkinTransitionKey.m_SkinDescriptor == Key.m_SkinDescriptor),
		!(m_LastSkinTransitionKey == Key));
	if(Action == ESkinChangeTransitionAction::START && m_RenderInfo.Valid() && NewRenderInfo.Valid())
	{
		const std::chrono::nanoseconds Now = time_get_nanoseconds();
		const bool TransitionActive = m_SkinTransitionStart.has_value() &&
					      m_SkinTransitionPreviousRenderInfo.Valid() &&
					      SkinChangeTransitionProgress(Now) < 1.0f;
		m_SkinTransitionPreviousRenderInfo = TransitionActive ? m_SkinTransitionPreviousRenderInfo : m_RenderInfo;
		m_SkinTransitionStart = Now;
	}
	else if(Action != ESkinChangeTransitionAction::KEEP)
	{
		m_SkinTransitionPreviousRenderInfo.Reset();
		m_SkinTransitionStart.reset();
	}

	m_LastSkinTransitionKey = Key;
	m_HasSkinTransitionKey = true;
}

float CGameClient::CClientData::SkinChangeTransitionProgress(std::chrono::nanoseconds Now) const
{
	if(!m_SkinTransitionStart.has_value())
	{
		return 1.0f;
	}

	const float ElapsedSeconds = std::chrono::duration<float>(Now - m_SkinTransitionStart.value()).count();
	if(!g_Config.m_QmSkinChangeTransition)
	{
		return 1.0f;
	}
	return ResolveSkinChangeTransitionProgress(ElapsedSeconds, g_Config.m_QmSkinChangeTransitionMs);
}

const CTeeRenderInfo *CGameClient::CClientData::SkinChangePreviousRenderInfo(std::chrono::nanoseconds Now) const
{
	if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0 || !m_SkinTransitionStart.has_value() || SkinChangeTransitionProgress(Now) >= 1.0f || !m_SkinTransitionPreviousRenderInfo.Valid())
	{
		return nullptr;
	}

	return &m_SkinTransitionPreviousRenderInfo;
}

void CGameClient::CClientData::Reset()
{
	m_RenderInfoSkinDescriptor = {};
	m_RenderInfoFallbackResidencyDescriptor = {};
	m_RenderInfoSkinGeneration = 0;
	m_RenderInfoFallbackResidencyRequested = false;
	m_UseCustomColor = 0;
	m_ColorBody = 0;
	m_ColorFeet = 0;

	m_aName[0] = '\0';
	m_aClan[0] = '\0';
	m_Country = -1;
	str_copy(m_aSkinName, "default");

	m_Team = 0;
	m_Emoticon = 0;
	m_EmoticonStartFraction = 0;
	m_EmoticonStartTick = -1;

	m_Solo = false;
	m_Jetpack = false;
	m_CollisionDisabled = false;
	m_EndlessHook = false;
	m_EndlessJump = false;
	m_HammerHitDisabled = false;
	m_GrenadeHitDisabled = false;
	m_LaserHitDisabled = false;
	m_ShotgunHitDisabled = false;
	m_HookHitDisabled = false;
	m_Super = false;
	m_Invincible = false;
	m_HasTelegunGun = false;
	m_HasTelegunGrenade = false;
	m_HasTelegunLaser = false;
	m_FreezeEnd = 0;
	m_DeepFrozen = false;
	m_LiveFrozen = false;
	m_IsInFreeze = false;
	m_HudFrozenTeeState = {};

	m_Predicted.Reset();
	m_PrevPredicted.Reset();

	// TClient
	m_RegularPredicted.Reset();
	m_ValidAntipingSmooth = false;

	if(m_pSkinInfo != nullptr)
	{
		// Make sure other `shared_ptr`s to this skin info will not use the refresh callback that refers to this reset client data
		m_pSkinInfo->SetRefreshCallback(nullptr);
		m_pSkinInfo = nullptr;
	}
	m_RenderInfo.Reset();
	m_SkinTransitionPreviousRenderInfo.Reset();
	m_SkinTransitionStart.reset();
	m_HasSkinTransitionKey = false;

	m_Angle = 0.0f;
	m_Active = false;
	m_ChatIgnore = false;
	m_EmoticonIgnore = false;
	m_Friend = false;
	m_Foe = false;

	m_AuthLevel = AUTHED_NO;
	m_Afk = false;
	m_Paused = false;
	m_Spec = false;

	std::fill(std::begin(m_aSwitchStates), std::end(m_aSwitchStates), 0);

	m_Snapped.m_Tick = -1;
	m_Evolved.m_Tick = -1;

	for(auto &PreInput : m_aPreInputs)
	{
		PreInput.m_IntendedTick = -1;
	}

	m_RenderCur.m_Tick = -1;
	m_RenderPrev.m_Tick = -1;
	m_RenderPos = vec2(0.0f, 0.0f);
	m_IsPredicted = false;
	m_IsPredictedLocal = false;
	std::fill(std::begin(m_aSmoothStart), std::end(m_aSmoothStart), 0);
	std::fill(std::begin(m_aSmoothLen), std::end(m_aSmoothLen), 0);
	std::fill(std::begin(m_aPredPos), std::end(m_aPredPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aPredTick), std::end(m_aPredTick), 0);
	m_SpecCharPresent = false;
	m_SpecChar = vec2(0.0f, 0.0f);

	// Chat bubble
	m_aChatBubbleText[0] = '\0';
	m_ChatBubbleStartTick = 0;
	m_ChatBubbleExpireTick = 0;

	for(auto &Info : m_aSixup)
		Info.Reset();
}

CSkinDescriptor CGameClient::CClientData::ToSkinDescriptor() const
{
	CSkinDescriptor SkinDescriptor;

	const int ClientId = this->ClientId();
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return SkinDescriptor;

	CTranslationContext::CClientData &TranslatedClient = m_pGameClient->m_pClient->m_TranslationContext.m_aClients[ClientId];
	const int LocalDummy = LocalSkinConfigIndex();
	if(LocalDummy >= 0)
	{
		BuildLocalSkinDescriptor(SkinDescriptor, LocalDummy);
		return SkinDescriptor;
	}

	if(m_Active && !TranslatedClient.m_Active)
	{
		SkinDescriptor.m_Flags |= CSkinDescriptor::FLAG_SIX;
		str_copy(SkinDescriptor.m_aSkinName, m_aSkinName);
		NormalizeSixupSkinName(SkinDescriptor.m_aSkinName, sizeof(SkinDescriptor.m_aSkinName));
	}
	else if(TranslatedClient.m_Active)
	{
		SkinDescriptor.m_Flags |= CSkinDescriptor::FLAG_SEVEN;
		for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				str_copy(SkinDescriptor.m_aSixup[Dummy].m_aaSkinPartNames[Part], m_aSixup[Dummy].m_aaSkinPartNames[Part]);
			}
			SkinDescriptor.m_aSixup[Dummy].m_XmasHat = time_season() == ETimeSeason::XMAS;
			SkinDescriptor.m_aSixup[Dummy].m_BotDecoration = (TranslatedClient.m_PlayerFlags7 & protocol7::PLAYERFLAG_BOT) != 0;
		}
	}

	return SkinDescriptor;
}

void CGameClient::UpdateLocalSkinInfo(int Dummy)
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES)
	{
		return;
	}

	const int ClientId = m_aLocalIds[Dummy];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		return;
	}

	m_aClients[ClientId].UpdateSkinInfo();
}

void CGameClient::CClientData::CSixup::Reset()
{
	for(int i = 0; i < protocol7::NUM_SKINPARTS; ++i)
	{
		m_aaSkinPartNames[i][0] = '\0';
		m_aUseCustomColors[i] = 0;
		m_aSkinPartColors[i] = 0;
	}
}

void CGameClient::SendSwitchTeam(int Team)
{
	if(Team == TEAM_SPECTATORS && m_FastPractice.Enabled())
	{
		m_FastPractice.ConsumeSpectatorCommand();
		return;
	}

	CNetMsg_Cl_SetTeam Msg;
	Msg.m_Team = Team;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CGameClient::SendStartInfo7(bool Dummy)
{
	const char *pClanToSend = Dummy ? Config()->m_ClDummyClan : Config()->m_PlayerClan;

	protocol7::CNetMsg_Cl_StartInfo Msg;
	Msg.m_pName = Dummy ? Client()->DummyName() : Client()->PlayerName();
	Msg.m_pClan = pClanToSend;
	Msg.m_Country = Dummy ? Config()->m_ClDummyCountry : Config()->m_PlayerCountry;
	for(int p = 0; p < protocol7::NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = CSkins7::ms_apSkinVariables[(int)Dummy][p];
		Msg.m_aUseCustomColors[p] = *CSkins7::ms_apUCCVariables[(int)Dummy][p];
		Msg.m_aSkinPartColors[p] = *CSkins7::ms_apColorVariables[(int)Dummy][p];
	}
	CMsgPacker Packer(&Msg, false, true);
	if(Msg.Pack(&Packer))
		return;
	Client()->SendMsg((int)Dummy, &Packer, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	m_aCheckInfo[(int)Dummy] = -1;
}

void CGameClient::SendSkinChange7(bool Dummy)
{
	protocol7::CNetMsg_Cl_SkinChange Msg;
	for(int p = 0; p < protocol7::NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = CSkins7::ms_apSkinVariables[(int)Dummy][p];
		Msg.m_aUseCustomColors[p] = *CSkins7::ms_apUCCVariables[(int)Dummy][p];
		Msg.m_aSkinPartColors[p] = *CSkins7::ms_apColorVariables[(int)Dummy][p];
	}
	CMsgPacker Packer(&Msg, false, true);
	if(Msg.Pack(&Packer))
		return;
	Client()->SendMsg((int)Dummy, &Packer, MSGFLAG_VITAL | MSGFLAG_FLUSH);
	m_aCheckInfo[(int)Dummy] = Client()->GameTickSpeed();
}

bool CGameClient::GotWantedSkin7(bool Dummy)
{
	// validate the wanted skinparts before comparison
	// because the skin parts we compare against are also validated
	// otherwise it tries to resend the skin info when the eyes are set to "negative"
	// in team based modes
	char aSkinParts[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_ARRAY_SIZE];
	char *apSkinPartsPtr[protocol7::NUM_SKINPARTS];
	int aUCCVars[protocol7::NUM_SKINPARTS];
	int aColorVars[protocol7::NUM_SKINPARTS];
	for(int SkinPart = 0; SkinPart < protocol7::NUM_SKINPARTS; SkinPart++)
	{
		str_copy(aSkinParts[SkinPart], CSkins7::ms_apSkinVariables[(int)Dummy][SkinPart], protocol7::MAX_SKIN_ARRAY_SIZE);
		apSkinPartsPtr[SkinPart] = aSkinParts[SkinPart];
		aUCCVars[SkinPart] = *CSkins7::ms_apUCCVariables[(int)Dummy][SkinPart];
		aColorVars[SkinPart] = *CSkins7::ms_apColorVariables[(int)Dummy][SkinPart];
	}
	m_Skins7.ValidateSkinParts(apSkinPartsPtr, aUCCVars, aColorVars, m_pClient->m_TranslationContext.m_GameFlags);

	for(int SkinPart = 0; SkinPart < protocol7::NUM_SKINPARTS; SkinPart++)
	{
		if(str_comp(m_aClients[m_aLocalIds[(int)Dummy]].m_aSixup[(int)Dummy].m_aaSkinPartNames[SkinPart], apSkinPartsPtr[SkinPart]))
			return false;
		if(m_aClients[m_aLocalIds[(int)Dummy]].m_aSixup[(int)Dummy].m_aUseCustomColors[SkinPart] != aUCCVars[SkinPart])
			return false;
		if(m_aClients[m_aLocalIds[(int)Dummy]].m_aSixup[(int)Dummy].m_aSkinPartColors[SkinPart] != aColorVars[SkinPart])
			return false;
	}

	// TODO: add name change ddnet extension to 0.7 protocol
	// if(str_comp(m_aClients[m_aLocalIds[(int)Dummy]].m_aName, Dummy ? Client()->DummyName() : Client()->PlayerName()))
	// 	return false;
	// if(str_comp(m_aClients[m_aLocalIds[(int)Dummy]].m_aClan, Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan))
	// 	return false;
	// if(m_aClients[m_aLocalIds[(int)Dummy]].m_Country != (Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry))
	// 	return false;

	return true;
}

void CGameClient::SendInfo(bool Start)
{
	if(!Start && m_TClient.IsFinishRenamePending(0))
		return;

	UpdateLocalSkinInfo(0);

	if(m_pClient->IsSixup())
	{
		if(Start)
			SendStartInfo7(false);
		else
			SendSkinChange7(false);
		return;
	}
	if(Start)
	{
		CNetMsg_Cl_StartInfo Msg;
		Msg.m_pName = Client()->PlayerName();
		Msg.m_pClan = g_Config.m_PlayerClan;
		Msg.m_Country = g_Config.m_PlayerCountry;
		Msg.m_pSkin = g_Config.m_ClPlayerSkin;
		Msg.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor;
		Msg.m_ColorBody = g_Config.m_ClPlayerColorBody;
		Msg.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(IClient::CONN_MAIN, &Packer, MSGFLAG_VITAL | MSGFLAG_FLUSH);
		m_aCheckInfo[0] = -1;
	}
	else
	{
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = Client()->PlayerName();
		Msg.m_pClan = g_Config.m_PlayerClan;
		Msg.m_Country = g_Config.m_PlayerCountry;
		Msg.m_pSkin = g_Config.m_ClPlayerSkin;
		Msg.m_UseCustomColor = g_Config.m_ClPlayerUseCustomColor;
		Msg.m_ColorBody = g_Config.m_ClPlayerColorBody;
		Msg.m_ColorFeet = g_Config.m_ClPlayerColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(IClient::CONN_MAIN, &Packer, MSGFLAG_VITAL);
		m_aCheckInfo[0] = Client()->GameTickSpeed();
	}
}

void CGameClient::SendDummyInfo(bool Start)
{
	if(!Start && m_TClient.IsFinishRenamePending(1))
		return;
	UpdateLocalSkinInfo(1);

	if(m_pClient->IsSixup())
	{
		if(Start)
			SendStartInfo7(true);
		else
			SendSkinChange7(true);
		return;
	}
	if(Start)
	{
		CNetMsg_Cl_StartInfo Msg;
		Msg.m_pName = Client()->DummyName();
		Msg.m_pClan = g_Config.m_ClDummyClan;
		Msg.m_Country = g_Config.m_ClDummyCountry;
		Msg.m_pSkin = g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
		m_aCheckInfo[1] = -1;
	}
	else
	{
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = Client()->DummyName();
		Msg.m_pClan = g_Config.m_ClDummyClan;
		Msg.m_Country = g_Config.m_ClDummyCountry;
		Msg.m_pSkin = g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(IClient::CONN_DUMMY, &Packer, MSGFLAG_VITAL);
		m_aCheckInfo[1] = Client()->GameTickSpeed();
	}
}

void CGameClient::SendKill()
{
	if(m_FastPractice.ConsumeKillCommand())
		return;

	CNetMsg_Cl_Kill Msg;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);

	if(g_Config.m_ClDummyCopyMoves)
	{
		CMsgPacker MsgP(NETMSGTYPE_CL_KILL, false);
		Client()->SendMsg(!g_Config.m_ClDummy, &MsgP, MSGFLAG_VITAL);
	}
}

void CGameClient::SendKill() const
{
	const_cast<CGameClient *>(this)->SendKill();
}

void CGameClient::SendReadyChange7()
{
	if(!Client()->IsSixup())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "Error you have to be connected to a 0.7 server to use ready_change");
		return;
	}
	protocol7::CNetMsg_Cl_ReadyChange Msg;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL, true);
}

void CGameClient::ConTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CGameClient *)pUserData)->SendSwitchTeam(pResult->GetInteger(0));
}

void CGameClient::ConKill(IConsole::IResult *pResult, void *pUserData)
{
	((CGameClient *)pUserData)->SendKill();
}

void CGameClient::ConReadyChange7(IConsole::IResult *pResult, void *pUserData)
{
	CGameClient *pClient = static_cast<CGameClient *>(pUserData);
	if(pClient->Client()->State() == IClient::STATE_ONLINE)
		pClient->SendReadyChange7();
}

void CGameClient::ConchainLanguageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	const bool HasArgument = pResult->NumArguments() != 0;
	char aRequestedLanguage[IO_MAX_PATH_LENGTH];
	char aPreviousLanguage[IO_MAX_PATH_LENGTH];
	str_copy(aPreviousLanguage, g_Config.m_ClLanguagefile, sizeof(aPreviousLanguage));
	if(HasArgument)
		str_copy(aRequestedLanguage, pResult->GetString(0), sizeof(aRequestedLanguage));
	pfnCallback(pResult, pCallbackUserData);
	const bool Changed = pThis->CanRunRuntimeConfigConchainEffects() && HasArgument && str_comp(aRequestedLanguage, aPreviousLanguage) != 0;
	if(Changed)
	{
		pThis->OnLanguageChange();
	}
}

void CGameClient::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pThis->CanRunRuntimeConfigConchainEffects())
		pThis->SendInfo(false);
}

void CGameClient::ConchainSpecialDummyInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pThis->CanRunRuntimeConfigConchainEffects())
		pThis->SendDummyInfo(false);
}

void CGameClient::ConchainSpecialDummy(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pThis->CanRunRuntimeConfigConchainEffects())
	{
		if(g_Config.m_ClDummy && !pThis->m_pClient->DummyConnected())
			g_Config.m_ClDummy = 0;
	}
}

IGameClient *CreateGameClient()
{
	return new CGameClient();
}

int CGameClient::IntersectCharacter(vec2 HookPos, vec2 NewPos, vec2 &NewPos2, int OwnId, vec2 *pPlayerPosition)
{
	float Distance = 0.0f;
	int ClosestId = -1;

	const CClientData &OwnClientData = m_aClients[OwnId];

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == OwnId)
			continue;

		const CClientData &Data = m_aClients[i];

		if(!Data.m_Active || !m_Snap.m_aCharacters[i].m_Active)
			continue;

		CNetObj_Character Prev = m_Snap.m_aCharacters[i].m_Prev;
		CNetObj_Character Player = m_Snap.m_aCharacters[i].m_Cur;

		vec2 Position = mix(vec2(Prev.m_X, Prev.m_Y), vec2(Player.m_X, Player.m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));

		bool IsOneSuper = Data.m_Super || OwnClientData.m_Super;
		bool IsOneSolo = Data.m_Solo || OwnClientData.m_Solo;

		if(!IsOneSuper && (!m_Teams.SameTeam(i, OwnId) || IsOneSolo || OwnClientData.m_HookHitDisabled))
			continue;

		vec2 ClosestPoint;
		if(closest_point_on_line(HookPos, NewPos, Position, ClosestPoint))
		{
			if(distance(Position, ClosestPoint) < CCharacterCore::PhysicalSize() + 2.0f)
			{
				if(ClosestId == -1 || distance(HookPos, Position) < Distance)
				{
					NewPos2 = ClosestPoint;
					ClosestId = i;
					Distance = distance(HookPos, Position);
					if(pPlayerPosition)
						*pPlayerPosition = Position;
				}
			}
		}
	}

	return ClosestId;
}

ColorRGBA CalculateNameColor(ColorHSLA TextColorHSL)
{
	return color_cast<ColorRGBA>(ColorHSLA(TextColorHSL.h, TextColorHSL.s * 0.68f, TextColorHSL.l * 0.81f));
}

void CGameClient::UpdateLocalTuning()
{
	m_GameWorld.m_WorldConfig.m_UseTuneZones = m_GameInfo.m_PredictDDRaceTiles;

	// always update default tune zone, even without character
	if(!m_GameWorld.m_WorldConfig.m_UseTuneZones)
		m_GameWorld.TuningList()[0] = m_aTuning[g_Config.m_ClDummy];

	if(!m_Snap.m_pLocalCharacter && !m_Snap.m_pSpectatorInfo)
		return;

	vec2 LocalPos = m_Snap.m_pLocalCharacter ? vec2(m_Snap.m_pLocalCharacter->m_X, m_Snap.m_pLocalCharacter->m_Y) : vec2(m_Snap.m_pSpectatorInfo->m_X, m_Snap.m_pSpectatorInfo->m_Y);

	// update the tuning at the local position with the latest tunings received before the new snapshot
	if(m_GameWorld.m_WorldConfig.m_UseTuneZones)
	{
		int TuneZone =
			m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_HasExtendedData &&
					m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_ExtendedData.m_TuneZoneOverride != TuneZone::OVERRIDE_NONE ?
				m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_ExtendedData.m_TuneZoneOverride :
				Collision()->IsTune(Collision()->GetMapIndex(LocalPos));

		if(TuneZone != m_aLocalTuneZone[g_Config.m_ClDummy])
		{
			// our tunezone changed, expecting tuning message
			m_aLocalTuneZone[g_Config.m_ClDummy] = m_aExpectingTuningForZone[g_Config.m_ClDummy] = TuneZone;
			m_aExpectingTuningSince[g_Config.m_ClDummy] = 0;
		}

		// tunezone could have changed, send dummy tuning to demo
		if(m_ActiveRecordings.any() && m_IsDummySwapping && m_aLocalTuneZone[0] != m_aLocalTuneZone[1])
		{
			CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
			int *pParams = (int *)&m_aTuning[g_Config.m_ClDummy];
			for(unsigned i = 0; i < sizeof(m_aTuning[0]) / sizeof(int); i++)
				Msg.AddInt(pParams[i]);
			Client()->SendMsgActive(&Msg, MSGFLAG_RECORD | MSGFLAG_NOSEND);
		}

		if(m_aExpectingTuningForZone[g_Config.m_ClDummy] >= 0)
		{
			if(m_aReceivedTuning[g_Config.m_ClDummy])
			{
				TuningList()[m_aExpectingTuningForZone[g_Config.m_ClDummy]] = m_aTuning[g_Config.m_ClDummy];
				m_GameWorld.TuningList()[m_aExpectingTuningForZone[g_Config.m_ClDummy]] = m_aTuning[g_Config.m_ClDummy];
				m_aReceivedTuning[g_Config.m_ClDummy] = false;
				m_aExpectingTuningForZone[g_Config.m_ClDummy] = -1;
			}
			else if(m_aExpectingTuningSince[g_Config.m_ClDummy] >= 5)
			{
				// if we are expecting tuning for more than 10 snaps (less than a quarter of a second)
				// it is probably dropped or it was received out of order
				// or applied to another tunezone.
				// we need to fallback to current tuning to fix ourselves.
				m_aExpectingTuningForZone[g_Config.m_ClDummy] = -1;
				m_aExpectingTuningSince[g_Config.m_ClDummy] = 0;
				m_aReceivedTuning[g_Config.m_ClDummy] = false;
				log_debug("tunezone", "the tuning was missed");
			}
			else
			{
				// if we are expecting tuning and have not received one yet.
				// do not update any tuning, so we don't apply it to the wrong tunezone.
				log_debug("tunezone", "waiting for tuning for zone %d", m_aExpectingTuningForZone[g_Config.m_ClDummy]);
				m_aExpectingTuningSince[g_Config.m_ClDummy]++;
			}
		}
		else
		{
			// if we have processed what we need, and the tuning is still wrong due to out of order message
			// fix our tuning by using the current one
			m_GameWorld.TuningList()[TuneZone] = m_aTuning[g_Config.m_ClDummy];
			m_aExpectingTuningSince[g_Config.m_ClDummy] = 0;
			m_aReceivedTuning[g_Config.m_ClDummy] = false;
		}
	}
}

void CGameClient::UpdatePrediction()
{
	m_GameWorld.m_WorldConfig.m_IsVanilla = m_GameInfo.m_PredictVanilla;
	m_GameWorld.m_WorldConfig.m_IsDDRace = m_GameInfo.m_PredictDDRace;
	m_GameWorld.m_WorldConfig.m_IsFNG = m_GameInfo.m_PredictFNG;
	m_GameWorld.m_WorldConfig.m_PredictDDRace = m_GameInfo.m_PredictDDRace;
	m_GameWorld.m_WorldConfig.m_PredictTiles = m_GameInfo.m_PredictDDRace && m_GameInfo.m_PredictDDRaceTiles;
	m_GameWorld.m_WorldConfig.m_PredictFreeze = g_Config.m_ClPredictFreeze;
	m_GameWorld.m_WorldConfig.m_PredictWeapons = AntiPingWeapons();
	m_GameWorld.m_WorldConfig.m_PredictEvents = g_Config.m_ClPredictEvents && m_GameInfo.m_PredictEvents;
	m_GameWorld.m_WorldConfig.m_PredictTeleport = false;
	m_GameWorld.m_WorldConfig.m_BugDDRaceInput = m_GameInfo.m_BugDDRaceInput;
	m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce = m_GameInfo.m_NoWeakHookAndBounce;

	if(!m_Snap.m_pLocalCharacter)
	{
		if(CCharacter *pLocalChar = m_GameWorld.GetCharacterById(m_Snap.m_LocalClientId))
			pLocalChar->Destroy();
		return;
	}

	if(m_Snap.m_pLocalCharacter->m_AmmoCount > 0 && m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_NINJA)
		m_GameWorld.m_WorldConfig.m_InfiniteAmmo = false;
	m_GameWorld.m_WorldConfig.m_IsSolo = !m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_HasExtendedData && !m_aTuning[g_Config.m_ClDummy].m_PlayerCollision && !m_aTuning[g_Config.m_ClDummy].m_PlayerHooking;

	CCharacter *pLocalChar = m_GameWorld.GetCharacterById(m_Snap.m_LocalClientId);
	CCharacter *pDummyChar = nullptr;
	if(PredictDummy())
		pDummyChar = m_GameWorld.GetCharacterById(m_aLocalIds[!g_Config.m_ClDummy]);

	// update strong and weak hook
	if(pLocalChar && !m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK && (m_aTuning[g_Config.m_ClDummy].m_PlayerCollision || m_aTuning[g_Config.m_ClDummy].m_PlayerHooking))
	{
		if(m_Snap.m_aCharacters[m_Snap.m_LocalClientId].m_HasExtendedData)
		{
			int aIds[MAX_CLIENTS];
			for(int &Id : aIds)
				Id = -1;
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = m_GameWorld.GetCharacterById(i))
					aIds[pChar->GetStrongWeakId()] = i;
			for(int Id : aIds)
				if(Id >= 0)
					m_CharOrder.GiveStrong(Id);
		}
		else
		{
			// manual detection
			DetectStrongHook();
		}
		for(int i : m_CharOrder.m_Ids)
		{
			if(CCharacter *pChar = m_GameWorld.GetCharacterById(i))
			{
				m_GameWorld.RemoveEntity(pChar);
				m_GameWorld.InsertEntity(pChar);
			}
		}
	}

	// advance the gameworld to the current gametick
	if(pLocalChar && absolute(m_GameWorld.GameTick() - Client()->GameTick(g_Config.m_ClDummy)) < Client()->GameTickSpeed())
	{
		for(int Tick = m_GameWorld.GameTick() + 1; Tick <= Client()->GameTick(g_Config.m_ClDummy); Tick++)
		{
			CNetObj_PlayerInput *pInput = (CNetObj_PlayerInput *)Client()->GetInput(Tick);
			CNetObj_PlayerInput *pDummyInput = nullptr;
			if(pDummyChar)
				pDummyInput = (CNetObj_PlayerInput *)Client()->GetInput(Tick, 1);
			if(pInput)
				pLocalChar->OnDirectInput(pInput);
			if(pDummyInput)
				pDummyChar->OnDirectInput(pDummyInput);

			ApplyPreInputs(Tick, true, m_GameWorld);

			m_GameWorld.m_GameTick = Tick;
			if(pInput)
				pLocalChar->OnPredictedInput(pInput);
			if(pDummyInput)
				pDummyChar->OnPredictedInput(pDummyInput);

			ApplyPreInputs(Tick, false, m_GameWorld);

			m_GameWorld.Tick();

			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = m_GameWorld.GetCharacterById(i))
				{
					m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
					m_aClients[i].m_aPredTick[Tick % 200] = Tick;
				}
		}
	}
	else
	{
		// skip to current gametick
		m_GameWorld.m_GameTick = Client()->GameTick(g_Config.m_ClDummy);
		if(pLocalChar)
			if(CNetObj_PlayerInput *pInput = (CNetObj_PlayerInput *)Client()->GetInput(Client()->GameTick(g_Config.m_ClDummy)))
				pLocalChar->SetInput(pInput);
		if(pDummyChar)
			if(CNetObj_PlayerInput *pInput = (CNetObj_PlayerInput *)Client()->GetInput(Client()->GameTick(g_Config.m_ClDummy), 1))
				pDummyChar->SetInput(pInput);
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
		if(CCharacter *pChar = m_GameWorld.GetCharacterById(i))
		{
			m_aClients[i].m_aPredPos[Client()->GameTick(g_Config.m_ClDummy) % 200] = pChar->Core()->m_Pos;
			m_aClients[i].m_aPredTick[Client()->GameTick(g_Config.m_ClDummy) % 200] = Client()->GameTick(g_Config.m_ClDummy);
		}

	// update the local gameworld with the new snapshot
	m_GameWorld.NetObjBegin(m_Teams, m_Snap.m_LocalClientId);

	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_Snap.m_aCharacters[i].m_Active)
		{
			bool IsLocal = (i == m_Snap.m_LocalClientId || (PredictDummy() && i == m_aLocalIds[!g_Config.m_ClDummy]));
			int GameTeam = IsTeamPlay() ? m_aClients[i].m_Team : i;
			m_GameWorld.NetCharAdd(i, &m_Snap.m_aCharacters[i].m_Cur,
				m_Snap.m_aCharacters[i].m_HasExtendedData ? &m_Snap.m_aCharacters[i].m_ExtendedData : nullptr,
				GameTeam, IsLocal);
		}

	for(const CSnapEntities &EntData : SnapEntities())
		m_GameWorld.NetObjAdd(EntData.m_Item.m_Id, EntData.m_Item.m_Type, EntData.m_Item.m_pData, EntData.m_pDataEx);

	m_GameWorld.NetObjEnd();
}

void CGameClient::UpdateSpectatorCursor()
{
	int CursorOwnerId = m_Snap.m_LocalClientId;
	if(m_Snap.m_SpecInfo.m_Active)
	{
		CursorOwnerId = m_Snap.m_SpecInfo.m_SpectatorId;
	}

	if(CursorOwnerId != m_CursorInfo.m_CursorOwnerId)
	{
		// reset cursor sample count upon changing spectating character
		m_CursorInfo.m_NumSamples = 0;
		m_CursorInfo.m_CursorOwnerId = CursorOwnerId;
	}

	if(m_MultiViewActivated || CursorOwnerId < 0 || CursorOwnerId >= MAX_CLIENTS)
	{
		// do not show spec cursor in multi-view
		m_CursorInfo.m_Available = false;
		m_CursorInfo.m_NumSamples = 0;
		return;
	}

	const CSnapState::CCharacterInfo &CharInfo = m_Snap.m_aCharacters[CursorOwnerId];
	const CClientData &CursorOwnerClient = m_aClients[CursorOwnerId];
	if(!CharInfo.m_HasExtendedDisplayInfo || !CursorOwnerClient.m_Active || (!g_Config.m_Debug && CursorOwnerClient.m_Paused))
	{
		// hide cursor when the spectating player is paused
		m_CursorInfo.m_Available = false;
		m_CursorInfo.m_NumSamples = 0;
		return;
	}

	m_CursorInfo.m_Available = true;
	m_CursorInfo.m_Position = CursorOwnerClient.m_RenderPos;
	m_CursorInfo.m_Weapon = CharInfo.m_Cur.m_Weapon;

	const vec2 Target = vec2(CharInfo.m_ExtendedData.m_TargetX, CharInfo.m_ExtendedData.m_TargetY);

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && DemoPlayer()->BaseInfo()->m_Paused)
	{
		m_CursorInfo.m_CursorOwnerId = -1;
		m_CursorInfo.m_NumSamples = 0;
		const vec2 TargetNew = vec2(CharInfo.m_ExtendedData.m_TargetX, CharInfo.m_ExtendedData.m_TargetY);
		if(CharInfo.m_pPrevExtendedData)
		{
			const vec2 TargetOld = vec2(CharInfo.m_pPrevExtendedData->m_TargetX, CharInfo.m_pPrevExtendedData->m_TargetY);
			m_CursorInfo.m_Target = mix(TargetOld, TargetNew, Client()->IntraGameTick(g_Config.m_ClDummy));
		}
		else
		{
			m_CursorInfo.m_Target = TargetNew;
		}
	}
	else
	{
		// interpolate cursor positions
		const double Tick = Client()->GameTick(g_Config.m_ClDummy);
		m_CursorInfo.m_NumSamples = std::clamp(m_CursorInfo.m_NumSamples, 0, CCursorInfo::CURSOR_SAMPLES);

		const bool HasSample = m_CursorInfo.m_NumSamples > 0;
		const vec2 LastInput = HasSample ? m_CursorInfo.m_aTargetSamplesData[m_CursorInfo.m_NumSamples - 1] : vec2(0.0f, 0.0f);
		const double LastTime = HasSample ? m_CursorInfo.m_aTargetSamplesTime[m_CursorInfo.m_NumSamples - 1] : 0.0;
		bool NewSample = LastInput != Target || LastTime + CCursorInfo::REST_THRESHOLD < Tick;

		if(LastTime > Tick)
		{
			// clear samples when time flows backwards
			m_CursorInfo.m_NumSamples = 0;
			NewSample = true;
		}

		if(m_CursorInfo.m_NumSamples == 0)
		{
			m_CursorInfo.m_aTargetSamplesTime[0] = Tick - CCursorInfo::INTERP_DELAY;
			m_CursorInfo.m_aTargetSamplesData[0] = Target;
			m_CursorInfo.m_NumSamples = 1;
		}

		if(NewSample && (m_CursorInfo.m_aTargetSamplesTime[m_CursorInfo.m_NumSamples - 1] != Tick ||
					m_CursorInfo.m_aTargetSamplesData[m_CursorInfo.m_NumSamples - 1] != Target))
		{
			if(m_CursorInfo.m_NumSamples == CCursorInfo::CURSOR_SAMPLES)
			{
				m_CursorInfo.m_NumSamples--;
				mem_move(m_CursorInfo.m_aTargetSamplesTime, m_CursorInfo.m_aTargetSamplesTime + 1, m_CursorInfo.m_NumSamples * sizeof(double));
				mem_move(m_CursorInfo.m_aTargetSamplesData, m_CursorInfo.m_aTargetSamplesData + 1, m_CursorInfo.m_NumSamples * sizeof(vec2));
			}
			m_CursorInfo.m_aTargetSamplesTime[m_CursorInfo.m_NumSamples] = Tick;
			m_CursorInfo.m_aTargetSamplesData[m_CursorInfo.m_NumSamples] = Target;
			m_CursorInfo.m_NumSamples++;
		}

		// using double to avoid precision loss when converting int tick to decimal type
		const double DisplayTime = Tick - CCursorInfo::INTERP_DELAY + double(Client()->IntraGameTickSincePrev(g_Config.m_ClDummy));
		double aTime[CCursorInfo::SAMPLE_FRAME_WINDOW];
		vec2 aData[CCursorInfo::SAMPLE_FRAME_WINDOW];

		// find the available sample timing
		int Index = m_CursorInfo.m_NumSamples;
		for(int i = 0; i < m_CursorInfo.m_NumSamples; i++)
		{
			if(m_CursorInfo.m_aTargetSamplesTime[i] > DisplayTime)
			{
				Index = i;
				break;
			}
		}

		for(int i = 0; i < CCursorInfo::SAMPLE_FRAME_WINDOW; i++)
		{
			const int Offset = i - CCursorInfo::SAMPLE_FRAME_OFFSET;
			const int SampleIndex = Index + Offset;
			if(SampleIndex < 0)
			{
				aTime[i] = m_CursorInfo.m_aTargetSamplesTime[0] + CCursorInfo::REST_THRESHOLD * Offset;
				aData[i] = m_CursorInfo.m_aTargetSamplesData[0];
			}
			else if(SampleIndex >= m_CursorInfo.m_NumSamples)
			{
				aTime[i] = m_CursorInfo.m_aTargetSamplesTime[m_CursorInfo.m_NumSamples - 1] + CCursorInfo::REST_THRESHOLD * (Offset + 1);
				aData[i] = m_CursorInfo.m_aTargetSamplesData[m_CursorInfo.m_NumSamples - 1];
			}
			else
			{
				aTime[i] = m_CursorInfo.m_aTargetSamplesTime[SampleIndex];
				aData[i] = m_CursorInfo.m_aTargetSamplesData[SampleIndex];
			}
		}

		m_CursorInfo.m_Target = mix_polynomial(aTime, aData, CCursorInfo::SAMPLE_FRAME_WINDOW, DisplayTime, vec2(0.0f, 0.0f));
	}

	vec2 TargetCameraOffset(0, 0);
	float l = length(m_CursorInfo.m_Target);

	if(l > 0.0001f) // make sure that this isn't 0
	{
		float OffsetAmount = maximum(l - m_Snap.m_SpecInfo.m_Deadzone, 0.0f) * (m_Snap.m_SpecInfo.m_FollowFactor / 100.0f);
		TargetCameraOffset = normalize(m_CursorInfo.m_Target) * OffsetAmount;
	}

	// if we are in auto spec mode, use camera zoom to smooth out cursor transitions
	const float Zoom = (m_Camera.m_Zooming && m_Camera.m_AutoSpecCameraZooming) ? m_Camera.m_Zoom : m_Snap.m_SpecInfo.m_Zoom;
	m_CursorInfo.m_WorldTarget = m_CursorInfo.m_Position + (m_CursorInfo.m_Target - TargetCameraOffset) * Zoom + TargetCameraOffset;
}

void CGameClient::UpdateRenderedCharacters()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_Snap.m_aCharacters[i].m_Active)
			continue;
		m_aClients[i].m_RenderCur = m_Snap.m_aCharacters[i].m_Cur;
		m_aClients[i].m_RenderPrev = m_Snap.m_aCharacters[i].m_Prev;
		m_aClients[i].m_IsPredicted = false;
		m_aClients[i].m_IsPredictedLocal = false;
		vec2 UnpredPos = mix(
			vec2(m_Snap.m_aCharacters[i].m_Prev.m_X, m_Snap.m_aCharacters[i].m_Prev.m_Y),
			vec2(m_Snap.m_aCharacters[i].m_Cur.m_X, m_Snap.m_aCharacters[i].m_Cur.m_Y),
			Client()->IntraGameTick(g_Config.m_ClDummy));
		vec2 Pos = UnpredPos;
		CCharacter *pChar = m_PredictedWorld.GetCharacterById(i);

		// TClient
		if(i == m_Snap.m_LocalClientId)
			Client()->m_IsLocalFrozen = pChar && pChar->m_FreezeTime > 0;

		const bool IsPracticeParticipant = m_FastPractice.Enabled() && m_FastPractice.IsPracticeParticipant(i);
		if(Predict() && (i == m_Snap.m_LocalClientId || IsPracticeParticipant || (AntiPingPlayers() && !IsOtherTeam(i))) && pChar)
		{
			m_aClients[i].m_Predicted.Write(&m_aClients[i].m_RenderCur);
			m_aClients[i].m_PrevPredicted.Write(&m_aClients[i].m_RenderPrev);

			m_aClients[i].m_IsPredicted = true;

			Pos = mix(
				vec2(m_aClients[i].m_RenderPrev.m_X, m_aClients[i].m_RenderPrev.m_Y),
				vec2(m_aClients[i].m_RenderCur.m_X, m_aClients[i].m_RenderCur.m_Y),
				m_aClients[i].m_IsPredicted ? Client()->PredIntraGameTick(g_Config.m_ClDummy) : Client()->IntraGameTick(g_Config.m_ClDummy));

			if(IsPracticeParticipant)
			{
				if(m_TClient.IsFastInputActive() && (i == m_Snap.m_LocalClientId || EffectiveFastInputOthers(this)))
					Pos = GetFastInputPos(i);
			}
			else if(g_Config.m_TcRemoveAnti)
			{
				Pos = GetFreezePos(i);
			}
			else if(m_TClient.IsFastInputActive() && (i == m_Snap.m_LocalClientId || (PredictDummy() && i == m_aLocalIds[!g_Config.m_ClDummy])))
			{
				Pos = GetFastInputPos(i);
			}

			if(i == m_Snap.m_LocalClientId || IsPracticeParticipant || (PredictDummy() && i == m_aLocalIds[!g_Config.m_ClDummy]))
			{
				m_aClients[i].m_IsPredictedLocal = true;
				if(AntiPingGunfire() && ((pChar->m_NinjaJetpack && pChar->m_FreezeTime == 0) || m_Snap.m_aCharacters[i].m_Cur.m_Weapon != WEAPON_NINJA || m_Snap.m_aCharacters[i].m_Cur.m_Weapon == m_aClients[i].m_Predicted.m_ActiveWeapon))
				{
					m_aClients[i].m_RenderCur.m_AttackTick = pChar->GetAttackTick();
					if(m_Snap.m_aCharacters[i].m_Cur.m_Weapon != WEAPON_NINJA && !(pChar->m_NinjaJetpack && pChar->Core()->m_ActiveWeapon == WEAPON_GUN))
						m_aClients[i].m_RenderCur.m_Weapon = m_aClients[i].m_Predicted.m_ActiveWeapon;
				}
			}
			else
			{
				// use unpredicted values for other players
				m_aClients[i].m_RenderPrev.m_Angle = m_Snap.m_aCharacters[i].m_Prev.m_Angle;
				m_aClients[i].m_RenderCur.m_Angle = m_Snap.m_aCharacters[i].m_Cur.m_Angle;

				if(g_Config.m_ClAntiPingSmooth)
					Pos = GetSmoothPos(i);

				if(g_Config.m_TcAntiPingImproved && m_aClients[i].m_ValidAntipingSmooth)
					Pos = mix(m_aClients[i].m_PrevImprovedPredPos, m_aClients[i].m_ImprovedPredPos, Client()->PredIntraGameTick(g_Config.m_ClDummy));

				if(g_Config.m_TcRemoveAnti && m_pClient->m_IsLocalFrozen)
					Pos = GetFreezePos(i);
				else if(m_TClient.IsFastInputActive() && EffectiveFastInputOthers(this) && !g_Config.m_TcAntiPingImproved)
					Pos = GetFastInputPos(i);

				if(g_Config.m_TcShowOthersGhosts && g_Config.m_TcSwapGhosts && !(m_aClients[i].m_FreezeEnd > 0 && g_Config.m_TcHideFrozenGhosts))
					Pos = UnpredPos;

				if(g_Config.m_TcUnpredOthersInFreeze && Client()->m_IsLocalFrozen)
					Pos = UnpredPos;
			}
		}
		m_aClients[i].m_RenderPos = Pos;
		if(Predict() && i == m_Snap.m_LocalClientId)
			m_LocalCharacterPos = Pos;
	}
}

void CGameClient::HandlePredictedEvents(const int Tick)
{
	const float Alpha = 1.0f;
	const float Volume = 1.0f;

	auto EventsIterator = m_PredictedWorld.m_PredictedEvents.begin();
	while(EventsIterator != m_PredictedWorld.m_PredictedEvents.end())
	{
		if(!EventsIterator->m_Handled && EventsIterator->m_Tick <= Tick)
		{
			if(EventsIterator->m_EventId == NETEVENTTYPE_SOUNDWORLD)
			{
				if(m_GameInfo.m_RaceSounds && ((EventsIterator->m_ExtraInfo == SOUND_GUN_FIRE && !g_Config.m_SndGun) || (EventsIterator->m_ExtraInfo == SOUND_PLAYER_PAIN_LONG && !g_Config.m_SndLongPain)))
				{
					EventsIterator = m_PredictedWorld.m_PredictedEvents.erase(EventsIterator);
					continue;
				}
				m_Sounds.PlayAt(CSounds::CHN_WORLD, EventsIterator->m_ExtraInfo, 1.0f, EventsIterator->m_Pos);
			}
			else if(EventsIterator->m_EventId == NETEVENTTYPE_EXPLOSION)
			{
				m_Effects.Explosion(EventsIterator->m_Pos, Alpha);
			}
			else if(EventsIterator->m_EventId == NETEVENTTYPE_HAMMERHIT)
			{
				m_Effects.HammerHit(EventsIterator->m_Pos, Alpha, Volume);
			}
			else if(EventsIterator->m_EventId == NETEVENTTYPE_DAMAGEIND)
			{
				m_Effects.DamageIndicator(EventsIterator->m_Pos, direction(EventsIterator->m_ExtraInfo / 256.0f), Alpha);
			}

			EventsIterator->m_Handled = true;
			++EventsIterator;
			continue;
		}
		else if(Tick - EventsIterator->m_Tick > 3 * Client()->GameTickSpeed()) // 3 seconds
		{
			// remove too old events
			EventsIterator = m_PredictedWorld.m_PredictedEvents.erase(EventsIterator);
		}
		else
		{
			++EventsIterator;
		}
	}
}

void CGameClient::DetectStrongHook()
{
	// attempt to detect strong/weak between players
	for(int FromPlayer = 0; FromPlayer < MAX_CLIENTS; FromPlayer++)
	{
		if(!m_Snap.m_aCharacters[FromPlayer].m_Active)
			continue;
		int ToPlayer = m_Snap.m_aCharacters[FromPlayer].m_Prev.m_HookedPlayer;
		if(ToPlayer < 0 || ToPlayer >= MAX_CLIENTS || !m_Snap.m_aCharacters[ToPlayer].m_Active || ToPlayer != m_Snap.m_aCharacters[FromPlayer].m_Cur.m_HookedPlayer)
			continue;
		if(absolute(minimum(m_aLastUpdateTick[ToPlayer], m_aLastUpdateTick[FromPlayer]) - Client()->GameTick(g_Config.m_ClDummy)) < Client()->GameTickSpeed() / 4)
			continue;
		if(m_Snap.m_aCharacters[FromPlayer].m_Prev.m_Direction != m_Snap.m_aCharacters[FromPlayer].m_Cur.m_Direction || m_Snap.m_aCharacters[ToPlayer].m_Prev.m_Direction != m_Snap.m_aCharacters[ToPlayer].m_Cur.m_Direction)
			continue;

		CCharacter *pFromCharWorld = m_GameWorld.GetCharacterById(FromPlayer);
		CCharacter *pToCharWorld = m_GameWorld.GetCharacterById(ToPlayer);
		if(!pFromCharWorld || !pToCharWorld)
			continue;

		m_aLastUpdateTick[ToPlayer] = m_aLastUpdateTick[FromPlayer] = Client()->GameTick(g_Config.m_ClDummy);

		float aPredictErr[2];
		CCharacterCore ToCharCur;
		ToCharCur.Read(&m_Snap.m_aCharacters[ToPlayer].m_Cur);

		CWorldCore World;

		for(int Direction = 0; Direction < 2; Direction++)
		{
			CCharacterCore ToChar = pFromCharWorld->GetCore();
			ToChar.Init(&World, Collision(), &m_Teams);
			World.m_apCharacters[ToPlayer] = &ToChar;
			ToChar.Read(&m_Snap.m_aCharacters[ToPlayer].m_Prev);

			CCharacterCore FromChar = pFromCharWorld->GetCore();
			FromChar.Init(&World, Collision(), &m_Teams);
			World.m_apCharacters[FromPlayer] = &FromChar;
			FromChar.Read(&m_Snap.m_aCharacters[FromPlayer].m_Prev);

			for(int Tick = Client()->PrevGameTick(g_Config.m_ClDummy); Tick < Client()->GameTick(g_Config.m_ClDummy); Tick++)
			{
				if(Direction == 0)
				{
					FromChar.Tick(false);
					ToChar.Tick(false);
				}
				else
				{
					ToChar.Tick(false);
					FromChar.Tick(false);
				}
				FromChar.Move();
				FromChar.Quantize();
				ToChar.Move();
				ToChar.Quantize();
			}
			aPredictErr[Direction] = distance(ToChar.m_Vel, ToCharCur.m_Vel);
		}
		const float LOW = 0.0001f;
		const float HIGH = 0.07f;
		if(aPredictErr[1] < LOW && aPredictErr[0] > HIGH)
		{
			if(m_CharOrder.HasStrongAgainst(ToPlayer, FromPlayer))
			{
				if(ToPlayer != m_Snap.m_LocalClientId)
					m_CharOrder.GiveWeak(ToPlayer);
				else
					m_CharOrder.GiveStrong(FromPlayer);
			}
		}
		else if(aPredictErr[0] < LOW && aPredictErr[1] > HIGH)
		{
			if(m_CharOrder.HasStrongAgainst(FromPlayer, ToPlayer))
			{
				if(ToPlayer != m_Snap.m_LocalClientId)
					m_CharOrder.GiveStrong(ToPlayer);
				else
					m_CharOrder.GiveWeak(FromPlayer);
			}
		}
	}
}

vec2 CGameClient::GetSmoothPos(int ClientId)
{
	SQmFastInputSettings Settings;
	Settings.m_Enabled = m_TClient.IsFastInputActive();
	Settings.m_Mode = g_Config.m_QmFastInputMode;
	Settings.m_FastAmountMs = g_Config.m_TcFastInputAmount;
	Settings.m_BestOffset = g_Config.m_QmBestInputOffset;
	Settings.m_BestSmoothing = g_Config.m_QmBestInputSmoothing;
	Settings.m_BestLatencyComp = g_Config.m_QmBestInputLatencyComp;
	Settings.m_SaikoPlusAmount = g_Config.m_QmSaikoPlusAmount;
	const float FastInputOffsetTicks = QmEffectiveFastInputOffsetTicks(Settings);
	const int FastInputTicks = QmFastInputPredictionTicks(FastInputOffsetTicks, g_Config.m_QmFastInputMode);
	const bool FastInputOthers = EffectiveFastInputOthers(this);
	const bool IsLocal = ClientId == m_Snap.m_LocalClientId || (PredictDummy() && ClientId == m_aLocalIds[!g_Config.m_ClDummy]);
	const int FastInputTicksClient = IsLocal ? FastInputTicks : (FastInputOthers ? QmFastInputPredictionTicksOthers(FastInputOffsetTicks, g_Config.m_QmFastInputMode) : 0);
	const bool BestInputInterpolationEnabled = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode) == 3 && FastInputTicksClient > 0;
	vec2 Pos = mix(m_aClients[ClientId].m_PrevPredicted.m_Pos, m_aClients[ClientId].m_Predicted.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
	int64_t Now = time_get();
	for(int i = 0; i < 2; i++)
	{
		int64_t Len = std::clamp(m_aClients[ClientId].m_aSmoothLen[i], (int64_t)1, time_freq());
		int64_t TimePassed = Now - m_aClients[ClientId].m_aSmoothStart[i];
		if(in_range(TimePassed, (int64_t)0, Len - 1))
		{
			float MixAmount = 1.f - std::pow(1.f - TimePassed / (float)Len, 1.2f);
			int SmoothTick;
			float SmoothIntra;
			Client()->GetSmoothTick(&SmoothTick, &SmoothIntra, MixAmount);

			if(ClientId != m_Snap.m_LocalClientId && FastInputOthers && FastInputTicksClient > 0)
				QmApplyFastInputOffset(FastInputOffsetTicks, SmoothTick, SmoothIntra);

			if(SmoothTick > 0 &&
				m_aClients[ClientId].m_aPredTick[(SmoothTick - 1) % 200] >= Client()->PrevGameTick(g_Config.m_ClDummy) &&
				m_aClients[ClientId].m_aPredTick[SmoothTick % 200] <= Client()->PredGameTick(g_Config.m_ClDummy) + FastInputTicksClient)
				Pos[i] = QmBestInputInterpolate(m_aClients[ClientId].m_aPredPos[(SmoothTick - 1) % 200], m_aClients[ClientId].m_aPredPos[SmoothTick % 200], SmoothIntra, BestInputInterpolationEnabled)[i];
		}
	}
	return Pos;
}

int CGameClient::GetFastInputPredictionAmountMs()
{
	if(!m_TClient.IsFastInputActive())
		return 0;
	const int Mode = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode);
	if(Mode == 0)
		return std::max(0, g_Config.m_TcFastInputAmount);
	if(Mode == 4)
		return std::max(0, g_Config.m_QmSaikoPlusAmount / 5);
	return std::max(0, g_Config.m_QmBestInputOffset / 5);
}

int CGameClient::GetFastInputPredictionTicks()
{
	return FastInputPredictionTicks(EffectiveFastInputOffsetTicks(this));
}

int CGameClient::GetFastInputRenderAmountMs()
{
	return round_to_int(EffectiveFastInputOffsetTicks(this) * 20.0f);
}

vec2 CGameClient::GetFastInputPos(int ClientId)
{
	float PredIntraTick = Client()->PredIntraGameTick(g_Config.m_ClDummy);
	int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);

	vec2 Pos = mix(m_aClients[ClientId].m_PrevPredicted.m_Pos, m_aClients[ClientId].m_Predicted.m_Pos, PredIntraTick);

	SQmFastInputSettings Settings;
	Settings.m_Enabled = m_TClient.IsFastInputActive();
	Settings.m_Mode = g_Config.m_QmFastInputMode;
	Settings.m_FastAmountMs = g_Config.m_TcFastInputAmount;
	Settings.m_BestOffset = g_Config.m_QmBestInputOffset;
	Settings.m_BestSmoothing = g_Config.m_QmBestInputSmoothing;
	Settings.m_BestLatencyComp = g_Config.m_QmBestInputLatencyComp;
	Settings.m_SaikoPlusAmount = g_Config.m_QmSaikoPlusAmount;
	const float FastInputOffsetTicks = QmEffectiveFastInputOffsetTicks(Settings);
	const int FastInputTicks = QmFastInputPredictionTicks(FastInputOffsetTicks, g_Config.m_QmFastInputMode);
	const bool FastInputOthers = EffectiveFastInputOthers(this);
	const int FastInputTicksClient = ClientId == m_Snap.m_LocalClientId ? FastInputTicks : (FastInputOthers ? QmFastInputPredictionTicksOthers(FastInputOffsetTicks, g_Config.m_QmFastInputMode) : 0);
	const bool BestInputInterpolationEnabled = QmFastInputNormalizedMode(g_Config.m_QmFastInputMode) == 3 && FastInputTicksClient > 0;
	QmApplyFastInputOffset(FastInputOffsetTicks, PredTick, PredIntraTick);

	if(PredTick > 0 &&
		m_aClients[ClientId].m_aPredTick[(PredTick - 1) % 200] >= Client()->PrevGameTick(g_Config.m_ClDummy) &&
		m_aClients[ClientId].m_aPredTick[PredTick % 200] <= Client()->PredGameTick(g_Config.m_ClDummy) + FastInputTicksClient)
	{
		Pos = QmBestInputInterpolate(m_aClients[ClientId].m_aPredPos[(PredTick - 1) % 200], m_aClients[ClientId].m_aPredPos[PredTick % 200], PredIntraTick, BestInputInterpolationEnabled);
	}

	return Pos;
}

vec2 CGameClient::GetFreezePos(int ClientId)
{
	vec2 Pos = mix(m_aClients[ClientId].m_PrevPredicted.m_Pos, m_aClients[ClientId].m_Predicted.m_Pos, Client()->PredIntraGameTick(g_Config.m_ClDummy));
	// int64_t Now = time_get();
	CCharacter *pChar = m_PredictedWorld.GetCharacterById(m_Snap.m_LocalClientId);
	CCharacter *pExtraChar = m_ExtraPredictedWorld.GetCharacterById(m_Snap.m_LocalClientId);

	// int64_t Len = clamp(m_aClients[ClientId].m_aSmoothLen[i], (int64_t)1, time_freq());
	// int64_t TimePassed = Now - m_aClients[ClientId].m_aSmoothStart[i];
	float MixAmount = 0.0f;
	int SmoothTick;
	float SmoothIntra;

	int AdjustTicks = 0;
	int DelayTicks = g_Config.m_TcUnfreezeLagDelayTicks;
	int FreezeTime = 0;
	if(pExtraChar && pChar)
	{
		AdjustTicks = pChar->m_FreezeAccumulation;
		if(pExtraChar->m_AliveAccumulation > 0)
			AdjustTicks -= pExtraChar->m_AliveAccumulation;

		AdjustTicks = std::max(AdjustTicks, 0);
		FreezeTime = pChar->m_FreezeTime;

		AdjustTicks = std::min(FreezeTime, AdjustTicks);
	}
	if(g_Config.m_TcRemoveAnti && pChar && AdjustTicks > 0 && FreezeTime > 0)
		MixAmount = mix(0.0f, 1.0f, 1.0f - AdjustTicks / (float)DelayTicks);
	// else if(AdjustTicks == 0 && ClientId != m_Snap.m_LocalClientId)
	//	MixAmount = 1.f - std::pow(1.f - TimePassed / (float)Len, 1.2f);
	else // our tee when not frozen
		MixAmount = 1.f;

	Client()->GetSmoothFreezeTick(&SmoothTick, &SmoothIntra, MixAmount);

	m_SmoothTick = SmoothTick;
	m_SmoothIntraTick = SmoothIntra;

	SQmFastInputSettings Settings;
	Settings.m_Enabled = m_TClient.IsFastInputActive();
	Settings.m_Mode = g_Config.m_QmFastInputMode;
	Settings.m_FastAmountMs = g_Config.m_TcFastInputAmount;
	Settings.m_BestOffset = g_Config.m_QmBestInputOffset;
	Settings.m_BestSmoothing = g_Config.m_QmBestInputSmoothing;
	Settings.m_BestLatencyComp = g_Config.m_QmBestInputLatencyComp;
	Settings.m_SaikoPlusAmount = g_Config.m_QmSaikoPlusAmount;
	const float FastInputOffsetTicks = QmEffectiveFastInputOffsetTicks(Settings);
	const int FastInputTicks = QmFastInputPredictionTicks(FastInputOffsetTicks, g_Config.m_QmFastInputMode);
	const bool FastInputOthers = EffectiveFastInputOthers(this);
	const int FastInputTicksOthers = FastInputOthers ? QmFastInputPredictionTicksOthers(FastInputOffsetTicks, g_Config.m_QmFastInputMode) : 0;

	const bool IsLocal = ClientId == m_Snap.m_LocalClientId || (PredictDummy() && ClientId == m_aLocalIds[!g_Config.m_ClDummy]);
	if(IsLocal && m_TClient.IsFastInputActive())
	{
		QmApplyFastInputOffset(FastInputOffsetTicks, SmoothTick, SmoothIntra);
	}
	else if(!IsLocal && FastInputOthers)
	{
		QmApplyFastInputOffset(FastInputOffsetTicks, SmoothTick, SmoothIntra);
	}

	if(SmoothTick > 0 &&
		m_aClients[ClientId].m_aPredTick[(SmoothTick - 1) % 200] >= Client()->PrevGameTick(g_Config.m_ClDummy) &&
		m_aClients[ClientId].m_aPredTick[SmoothTick % 200] <= Client()->PredGameTick(g_Config.m_ClDummy) + (IsLocal ? FastInputTicks : FastInputTicksOthers))
	{
		Pos = mix(m_aClients[ClientId].m_aPredPos[(SmoothTick - 1) % 200], m_aClients[ClientId].m_aPredPos[SmoothTick % 200], SmoothIntra);
	}

	return Pos;
}

void CGameClient::Echo(const char *pString)
{
	m_Chat.Echo(pString);
}

void CGameClient::Echo(const char *pString, bool ForceVisible)
{
	m_Chat.Echo(pString, ForceVisible);
}

bool CGameClient::IsOtherTeam(int ClientId) const
{
	bool Local = m_Snap.m_LocalClientId == ClientId;

	if(m_Snap.m_LocalClientId < 0)
	{
		return false;
	}
	else if((m_Snap.m_SpecInfo.m_Active && m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW) || ClientId < 0)
	{
		return false;
	}
	else if(m_Snap.m_SpecInfo.m_Active && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
	{
		if(m_Teams.Team(ClientId) == TEAM_SUPER || m_Teams.Team(m_Snap.m_SpecInfo.m_SpectatorId) == TEAM_SUPER)
			return false;
		return m_Teams.Team(ClientId) != m_Teams.Team(m_Snap.m_SpecInfo.m_SpectatorId);
	}
	else if((m_aClients[m_Snap.m_LocalClientId].m_Solo || m_aClients[ClientId].m_Solo) && !Local)
	{
		return true;
	}

	if(m_Teams.Team(ClientId) == TEAM_SUPER || m_Teams.Team(m_Snap.m_LocalClientId) == TEAM_SUPER)
		return false;

	return m_Teams.Team(ClientId) != m_Teams.Team(m_Snap.m_LocalClientId);
}

int CGameClient::SwitchStateTeam() const
{
	if(m_aSwitchStateTeam[g_Config.m_ClDummy] >= 0)
		return m_aSwitchStateTeam[g_Config.m_ClDummy];
	else if(m_Snap.m_LocalClientId < 0)
		return 0;
	else if(m_Snap.m_SpecInfo.m_Active && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		return m_Teams.Team(m_Snap.m_SpecInfo.m_SpectatorId);
	return m_Teams.Team(m_Snap.m_LocalClientId);
}

bool CGameClient::IsLocalCharSuper() const
{
	if(m_Snap.m_LocalClientId < 0)
		return false;
	return m_aClients[m_Snap.m_LocalClientId].m_Super;
}

void CGameClient::ReloadNamedSingleFileAssetImage(int ImageId, const char *pCategoryId, const char *pActiveName)
{
	LoadNamedSingleFileImage(this, ImageId, pCategoryId, pActiveName);
	if(ImageId == IMAGE_ARROW || ImageId == IMAGE_STRONGWEAK)
		m_NamePlates.ResetNamePlates();
}

void CGameClient::LoadInitialGraphicsAssets()
{
	// 按 g_pData 的图片表加载全部初始资源。启动与「图形资源重置后重建」共用这一条路径，
	// 保证两条路径不会各自漂移。
	for(int i = 0; i < g_pData->m_NumImages; i++)
	{
		if(i == IMAGE_GAME)
			LoadGameSkin(g_Config.m_ClAssetGame);
		else if(i == IMAGE_CURSOR)
			LoadNamedSingleFileImage(this, i, "gui_cursor", g_Config.m_ClAssetGuiCursor);
		else if(i == IMAGE_ARROW)
			LoadNamedSingleFileImage(this, i, "arrow", g_Config.m_ClAssetArrow);
		else if(i == IMAGE_EMOTICONS)
			LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
		else if(i == IMAGE_PARTICLES)
			LoadParticlesSkin(g_Config.m_ClAssetParticles);
		else if(i == IMAGE_HUD)
			LoadHudSkin(g_Config.m_ClAssetHud);
		else if(i == IMAGE_EXTRAS)
			LoadExtrasSkin(g_Config.m_ClAssetExtras);
		else if(i == IMAGE_STRONGWEAK)
			LoadNamedSingleFileImage(this, i, "strong_weak", g_Config.m_ClAssetStrongWeak);
		else if(g_pData->m_aImages[i].m_pFilename[0] == '\0') // handle special null image without filename
			g_pData->m_aImages[i].m_Id = IGraphics::CTextureHandle();
		else
			g_pData->m_aImages[i].m_Id = Graphics()->LoadTexture(g_pData->m_aImages[i].m_pFilename, IStorage::TYPE_ALL);
	}
}

void CGameClient::OnGraphicsResourcesReset()
{
	// 设备重建后所有 GPU 资源都已随设备消失。引擎侧的句柄已经通过纪元自增全部失效，
	// 这里负责把游戏侧的资源重新建起来。
	log_info("gfx", "graphics resources were reset, reloading game assets (generation %u)", Graphics()->GraphicsResourcesResetVersion());

	// 先让所有旧句柄显式作废，避免任何一次误用旧句柄的绘制落到新设备上。
	for(int i = 0; i < g_pData->m_NumImages; i++)
		g_pData->m_aImages[i].m_Id.Invalidate();

	// 标记为未加载，让各个 Load*Skin 走完整的加载分支（而不是因为“已加载”直接返回）。
	m_GameSkinLoaded = false;
	m_EmoticonsSkinLoaded = false;
	m_ParticlesSkinLoaded = false;
	m_HudSkinLoaded = false;
	m_ExtrasSkinLoaded = false;

	m_QmIconManager.OnGraphicsResourcesReset();

	LoadInitialGraphicsAssets();

	// 文本渲染器缓存了字体纹理，必须同样重建。
	TextRender()->OnGraphicsResourcesReset();

	log_info("gfx", "game assets reloaded after graphics resources reset");
}

void CGameClient::LoadGameSkin(const char *pPath, bool AsDir)
{
	if(m_GameSkinLoaded)
	{
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteHealthFull);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteArmorFull);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteArmorEmpty);

		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponHammerCursor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGunCursor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponShotgunCursor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGrenadeCursor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponNinjaCursor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponLaserCursor);

		for(auto &SpriteWeaponCursor : m_GameSkin.m_aSpriteWeaponCursors)
		{
			SpriteWeaponCursor = IGraphics::CTextureHandle();
		}

		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteHookChain);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteHookHead);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponHammer);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGun);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponShotgun);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGrenade);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponNinja);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponLaser);

		for(auto &SpriteWeapon : m_GameSkin.m_aSpriteWeapons)
		{
			SpriteWeapon = IGraphics::CTextureHandle();
		}

		for(auto &SpriteParticle : m_GameSkin.m_aSpriteParticles)
		{
			Graphics()->UnloadTexture(&SpriteParticle);
		}

		for(auto &SpriteStar : m_GameSkin.m_aSpriteStars)
		{
			Graphics()->UnloadTexture(&SpriteStar);
		}

		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGunProjectile);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponShotgunProjectile);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponGrenadeProjectile);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponHammerProjectile);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponNinjaProjectile);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteWeaponLaserProjectile);

		for(auto &SpriteWeaponProjectile : m_GameSkin.m_aSpriteWeaponProjectiles)
		{
			SpriteWeaponProjectile = IGraphics::CTextureHandle();
		}

		for(int i = 0; i < 3; ++i)
		{
			Graphics()->UnloadTexture(&m_GameSkin.m_aSpriteWeaponGunMuzzles[i]);
			Graphics()->UnloadTexture(&m_GameSkin.m_aSpriteWeaponShotgunMuzzles[i]);
			Graphics()->UnloadTexture(&m_GameSkin.m_aaSpriteWeaponNinjaMuzzles[i]);

			for(auto &SpriteWeaponsMuzzle : m_GameSkin.m_aaSpriteWeaponsMuzzles)
			{
				SpriteWeaponsMuzzle[i] = IGraphics::CTextureHandle();
			}
		}

		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupHealth);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupArmor);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupArmorShotgun);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupArmorGrenade);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupArmorLaser);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupArmorNinja);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupGrenade);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupShotgun);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupLaser);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupNinja);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupGun);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpritePickupHammer);

		for(auto &SpritePickupWeapon : m_GameSkin.m_aSpritePickupWeapons)
		{
			SpritePickupWeapon = IGraphics::CTextureHandle();
		}

		for(auto &SpritePickupWeaponArmor : m_GameSkin.m_aSpritePickupWeaponArmor)
		{
			SpritePickupWeaponArmor = IGraphics::CTextureHandle();
		}

		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteFlagBlue);
		Graphics()->UnloadTexture(&m_GameSkin.m_SpriteFlagRed);

		if(m_GameSkin.IsSixup())
		{
			Graphics()->UnloadTexture(&m_GameSkin.m_SpriteNinjaBarFullLeft);
			Graphics()->UnloadTexture(&m_GameSkin.m_SpriteNinjaBarFull);
			Graphics()->UnloadTexture(&m_GameSkin.m_SpriteNinjaBarEmpty);
			Graphics()->UnloadTexture(&m_GameSkin.m_SpriteNinjaBarEmptyRight);
		}

		m_GameSkinLoaded = false;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	bool IsDefault = false;
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(aPath, g_pData->m_aImages[IMAGE_GAME].m_pFilename);
		IsDefault = true;
	}
	else
	{
		if(AsDir)
			str_format(aPath, sizeof(aPath), "assets/game/%s/%s", pPath, g_pData->m_aImages[IMAGE_GAME].m_pFilename);
		else
			str_format(aPath, sizeof(aPath), "assets/game/%s.png", pPath);
	}

	CImageInfo ImgInfo;
	bool PngLoaded = Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
	const bool ImageValid = PngLoaded &&
				Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_HEALTH_FULL].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_HEALTH_FULL].m_pSet->m_Gridy, true) &&
				Graphics()->IsImageFormatRgba(aPath, ImgInfo);
	if(!PngLoaded && !IsDefault)
	{
		if(AsDir)
			LoadGameSkin("default");
		else
			LoadGameSkin(pPath, true);
	}
	else if(!ImageValid && !IsDefault)
	{
		// Avoid leaving the game skin in an unloaded state when a custom file is invalid.
		LoadGameSkin("default");
	}
	else if(ImageValid)
	{
		m_GameSkin.m_SpriteHealthFull = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HEALTH_FULL]);
		m_GameSkin.m_SpriteHealthEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HEALTH_EMPTY]);
		m_GameSkin.m_SpriteArmorFull = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_ARMOR_FULL]);
		m_GameSkin.m_SpriteArmorEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_ARMOR_EMPTY]);

		m_GameSkin.m_SpriteWeaponHammerCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_HAMMER_CURSOR]);
		m_GameSkin.m_SpriteWeaponGunCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_CURSOR]);
		m_GameSkin.m_SpriteWeaponShotgunCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_CURSOR]);
		m_GameSkin.m_SpriteWeaponGrenadeCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GRENADE_CURSOR]);
		m_GameSkin.m_SpriteWeaponNinjaCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_CURSOR]);
		m_GameSkin.m_SpriteWeaponLaserCursor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_LASER_CURSOR]);

		m_GameSkin.m_aSpriteWeaponCursors[0] = m_GameSkin.m_SpriteWeaponHammerCursor;
		m_GameSkin.m_aSpriteWeaponCursors[1] = m_GameSkin.m_SpriteWeaponGunCursor;
		m_GameSkin.m_aSpriteWeaponCursors[2] = m_GameSkin.m_SpriteWeaponShotgunCursor;
		m_GameSkin.m_aSpriteWeaponCursors[3] = m_GameSkin.m_SpriteWeaponGrenadeCursor;
		m_GameSkin.m_aSpriteWeaponCursors[4] = m_GameSkin.m_SpriteWeaponLaserCursor;
		m_GameSkin.m_aSpriteWeaponCursors[5] = m_GameSkin.m_SpriteWeaponNinjaCursor;

		// weapons and hook
		m_GameSkin.m_SpriteHookChain = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HOOK_CHAIN]);
		m_GameSkin.m_SpriteHookHead = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HOOK_HEAD]);
		m_GameSkin.m_SpriteWeaponHammer = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_HAMMER_BODY]);
		m_GameSkin.m_SpriteWeaponGun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_BODY]);
		m_GameSkin.m_SpriteWeaponShotgun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_BODY]);
		m_GameSkin.m_SpriteWeaponGrenade = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GRENADE_BODY]);
		m_GameSkin.m_SpriteWeaponNinja = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_BODY]);
		m_GameSkin.m_SpriteWeaponLaser = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_LASER_BODY]);

		m_GameSkin.m_aSpriteWeapons[0] = m_GameSkin.m_SpriteWeaponHammer;
		m_GameSkin.m_aSpriteWeapons[1] = m_GameSkin.m_SpriteWeaponGun;
		m_GameSkin.m_aSpriteWeapons[2] = m_GameSkin.m_SpriteWeaponShotgun;
		m_GameSkin.m_aSpriteWeapons[3] = m_GameSkin.m_SpriteWeaponGrenade;
		m_GameSkin.m_aSpriteWeapons[4] = m_GameSkin.m_SpriteWeaponLaser;
		m_GameSkin.m_aSpriteWeapons[5] = m_GameSkin.m_SpriteWeaponNinja;

		// particles
		for(int i = 0; i < 9; ++i)
		{
			m_GameSkin.m_aSpriteParticles[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART1 + i]);
		}

		// stars
		for(int i = 0; i < 3; ++i)
		{
			m_GameSkin.m_aSpriteStars[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_STAR1 + i]);
		}

		// projectiles
		m_GameSkin.m_SpriteWeaponGunProjectile = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_PROJ]);
		m_GameSkin.m_SpriteWeaponShotgunProjectile = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_PROJ]);
		m_GameSkin.m_SpriteWeaponGrenadeProjectile = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GRENADE_PROJ]);

		// these weapons have no projectiles
		m_GameSkin.m_SpriteWeaponHammerProjectile = IGraphics::CTextureHandle();
		m_GameSkin.m_SpriteWeaponNinjaProjectile = IGraphics::CTextureHandle();

		m_GameSkin.m_SpriteWeaponLaserProjectile = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_LASER_PROJ]);

		m_GameSkin.m_aSpriteWeaponProjectiles[0] = m_GameSkin.m_SpriteWeaponHammerProjectile;
		m_GameSkin.m_aSpriteWeaponProjectiles[1] = m_GameSkin.m_SpriteWeaponGunProjectile;
		m_GameSkin.m_aSpriteWeaponProjectiles[2] = m_GameSkin.m_SpriteWeaponShotgunProjectile;
		m_GameSkin.m_aSpriteWeaponProjectiles[3] = m_GameSkin.m_SpriteWeaponGrenadeProjectile;
		m_GameSkin.m_aSpriteWeaponProjectiles[4] = m_GameSkin.m_SpriteWeaponLaserProjectile;
		m_GameSkin.m_aSpriteWeaponProjectiles[5] = m_GameSkin.m_SpriteWeaponNinjaProjectile;

		// muzzles
		for(int i = 0; i < 3; ++i)
		{
			m_GameSkin.m_aSpriteWeaponGunMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_MUZZLE1 + i]);
			m_GameSkin.m_aSpriteWeaponShotgunMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_MUZZLE1 + i]);
			m_GameSkin.m_aaSpriteWeaponNinjaMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_MUZZLE1 + i]);

			m_GameSkin.m_aaSpriteWeaponsMuzzles[1][i] = m_GameSkin.m_aSpriteWeaponGunMuzzles[i];
			m_GameSkin.m_aaSpriteWeaponsMuzzles[2][i] = m_GameSkin.m_aSpriteWeaponShotgunMuzzles[i];
			m_GameSkin.m_aaSpriteWeaponsMuzzles[5][i] = m_GameSkin.m_aaSpriteWeaponNinjaMuzzles[i];
		}

		// pickups
		m_GameSkin.m_SpritePickupHealth = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_HEALTH]);
		m_GameSkin.m_SpritePickupArmor = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_ARMOR]);
		m_GameSkin.m_SpritePickupHammer = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_HAMMER]);
		m_GameSkin.m_SpritePickupGun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_GUN]);
		m_GameSkin.m_SpritePickupShotgun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_SHOTGUN]);
		m_GameSkin.m_SpritePickupGrenade = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_GRENADE]);
		m_GameSkin.m_SpritePickupLaser = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_LASER]);
		m_GameSkin.m_SpritePickupNinja = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_NINJA]);
		m_GameSkin.m_SpritePickupArmorShotgun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_ARMOR_SHOTGUN]);
		m_GameSkin.m_SpritePickupArmorGrenade = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_ARMOR_GRENADE]);
		m_GameSkin.m_SpritePickupArmorNinja = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_ARMOR_NINJA]);
		m_GameSkin.m_SpritePickupArmorLaser = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PICKUP_ARMOR_LASER]);

		m_GameSkin.m_aSpritePickupWeapons[0] = m_GameSkin.m_SpritePickupHammer;
		m_GameSkin.m_aSpritePickupWeapons[1] = m_GameSkin.m_SpritePickupGun;
		m_GameSkin.m_aSpritePickupWeapons[2] = m_GameSkin.m_SpritePickupShotgun;
		m_GameSkin.m_aSpritePickupWeapons[3] = m_GameSkin.m_SpritePickupGrenade;
		m_GameSkin.m_aSpritePickupWeapons[4] = m_GameSkin.m_SpritePickupLaser;
		m_GameSkin.m_aSpritePickupWeapons[5] = m_GameSkin.m_SpritePickupNinja;

		m_GameSkin.m_aSpritePickupWeaponArmor[0] = m_GameSkin.m_SpritePickupArmorShotgun;
		m_GameSkin.m_aSpritePickupWeaponArmor[1] = m_GameSkin.m_SpritePickupArmorGrenade;
		m_GameSkin.m_aSpritePickupWeaponArmor[2] = m_GameSkin.m_SpritePickupArmorNinja;
		m_GameSkin.m_aSpritePickupWeaponArmor[3] = m_GameSkin.m_SpritePickupArmorLaser;

		// flags
		m_GameSkin.m_SpriteFlagBlue = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_FLAG_BLUE]);
		m_GameSkin.m_SpriteFlagRed = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_FLAG_RED]);

		// ninja bar (0.7)
		if(!Graphics()->IsSpriteTextureFullyTransparent(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_FULL_LEFT]) ||
			!Graphics()->IsSpriteTextureFullyTransparent(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_FULL]) ||
			!Graphics()->IsSpriteTextureFullyTransparent(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_EMPTY]) ||
			!Graphics()->IsSpriteTextureFullyTransparent(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_EMPTY_RIGHT]))
		{
			m_GameSkin.m_SpriteNinjaBarFullLeft = Graphics()->LoadSpriteTexture(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_FULL_LEFT]);
			m_GameSkin.m_SpriteNinjaBarFull = Graphics()->LoadSpriteTexture(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_FULL]);
			m_GameSkin.m_SpriteNinjaBarEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_EMPTY]);
			m_GameSkin.m_SpriteNinjaBarEmptyRight = Graphics()->LoadSpriteTexture(ImgInfo, &client_data7::g_pData->m_aSprites[client_data7::SPRITE_NINJA_BAR_EMPTY_RIGHT]);
		}

		m_GameSkinLoaded = true;
	}
	ImgInfo.Free();
}

void CGameClient::LoadEmoticonsSkin(const char *pPath, bool AsDir)
{
	if(m_EmoticonsSkinLoaded)
	{
		for(auto &SpriteEmoticon : m_EmoticonsSkin.m_aSpriteEmoticons)
			Graphics()->UnloadTexture(&SpriteEmoticon);

		m_EmoticonsSkinLoaded = false;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	bool IsDefault = false;
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(aPath, g_pData->m_aImages[IMAGE_EMOTICONS].m_pFilename);
		IsDefault = true;
	}
	else
	{
		if(AsDir)
			str_format(aPath, sizeof(aPath), "assets/emoticons/%s/%s", pPath, g_pData->m_aImages[IMAGE_EMOTICONS].m_pFilename);
		else
			str_format(aPath, sizeof(aPath), "assets/emoticons/%s.png", pPath);
	}

	CImageInfo ImgInfo;
	bool PngLoaded = Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
	if(!PngLoaded && !IsDefault)
	{
		if(AsDir)
			LoadEmoticonsSkin("default");
		else
			LoadEmoticonsSkin(pPath, true);
	}
	else if(PngLoaded && Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_OOP].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_OOP].m_pSet->m_Gridy, true) && Graphics()->IsImageFormatRgba(aPath, ImgInfo))
	{
		for(int i = 0; i < 16; ++i)
			m_EmoticonsSkin.m_aSpriteEmoticons[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_OOP + i]);

		m_EmoticonsSkinLoaded = true;
	}
	ImgInfo.Free();
}

void CGameClient::LoadParticlesSkin(const char *pPath, bool AsDir)
{
	char aPath[IO_MAX_PATH_LENGTH];
	bool IsDefault = false;
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(aPath, g_pData->m_aImages[IMAGE_PARTICLES].m_pFilename);
		IsDefault = true;
	}
	else
	{
		if(AsDir)
			str_format(aPath, sizeof(aPath), "assets/particles/%s/%s", pPath, g_pData->m_aImages[IMAGE_PARTICLES].m_pFilename);
		else
			str_format(aPath, sizeof(aPath), "assets/particles/%s.png", pPath);
	}

	CImageInfo ImgInfo;
	bool PngLoaded = Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
	const bool ValidImage = PngLoaded && Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_PART_SLICE].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_PART_SLICE].m_pSet->m_Gridy, true) && Graphics()->IsImageFormatRgba(aPath, ImgInfo);
	if(!ValidImage && !IsDefault)
	{
		ImgInfo.Free();
		if(AsDir)
			LoadParticlesSkin("default");
		else
			LoadParticlesSkin(pPath, true);
	}
	else if(ValidImage)
	{
		auto UnloadParticleSkin = [this](SClientParticlesSkin &Skin) {
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleSlice);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleBall);
			for(auto &Texture : Skin.m_aSpriteParticleSplat)
				Graphics()->UnloadTexture(&Texture);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleSmoke);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleShell);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleExpl);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleAirJump);
			Graphics()->UnloadTexture(&Skin.m_SpriteParticleHit);
		};
		SClientParticlesSkin Candidate;
		Candidate.m_SpriteParticleSlice = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SLICE]);
		Candidate.m_SpriteParticleBall = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_BALL]);
		for(int i = 0; i < 3; ++i)
			Candidate.m_aSpriteParticleSplat[i] = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SPLAT01 + i]);
		Candidate.m_SpriteParticleSmoke = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SMOKE]);
		Candidate.m_SpriteParticleShell = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SHELL]);
		Candidate.m_SpriteParticleExpl = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_EXPL01]);
		Candidate.m_SpriteParticleAirJump = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_AIRJUMP]);
		Candidate.m_SpriteParticleHit = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_HIT01]);

		Candidate.m_aSpriteParticles[0] = Candidate.m_SpriteParticleSlice;
		Candidate.m_aSpriteParticles[1] = Candidate.m_SpriteParticleBall;
		for(int i = 0; i < 3; ++i)
			Candidate.m_aSpriteParticles[2 + i] = Candidate.m_aSpriteParticleSplat[i];
		Candidate.m_aSpriteParticles[5] = Candidate.m_SpriteParticleSmoke;
		Candidate.m_aSpriteParticles[6] = Candidate.m_SpriteParticleShell;
		Candidate.m_aSpriteParticles[7] = Candidate.m_SpriteParticleExpl;
		Candidate.m_aSpriteParticles[8] = Candidate.m_SpriteParticleAirJump;
		Candidate.m_aSpriteParticles[9] = Candidate.m_SpriteParticleHit;

		bool CandidateValid = true;
		for(const auto &Texture : Candidate.m_aSpriteParticles)
			CandidateValid &= Texture.IsValid();
		if(CandidateValid)
		{
			if(m_ParticlesSkinLoaded)
				UnloadParticleSkin(m_ParticlesSkin);
			m_ParticlesSkin = Candidate;
			m_ParticlesSkinLoaded = true;
		}
		else
		{
			UnloadParticleSkin(Candidate);
			if(!IsDefault)
				LoadParticlesSkin("default");
		}
	}
	ImgInfo.Free();
}

void CGameClient::LoadHudSkin(const char *pPath, bool AsDir)
{
	if(m_HudSkinLoaded)
	{
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudAirjump);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudAirjumpEmpty);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudSolo);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudJetpack);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudFreezeBarFullLeft);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudFreezeBarFull);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudFreezeBarEmpty);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudFreezeBarEmptyRight);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudNinjaBarFull);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudLockMode);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudTeam0Mode);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudDummyHammer);
		Graphics()->UnloadTexture(&m_HudSkin.m_SpriteHudDummyCopy);
		m_HudSkinLoaded = false;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	bool IsDefault = false;
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(aPath, g_pData->m_aImages[IMAGE_HUD].m_pFilename);
		IsDefault = true;
	}
	else
	{
		if(AsDir)
			str_format(aPath, sizeof(aPath), "assets/hud/%s/%s", pPath, g_pData->m_aImages[IMAGE_HUD].m_pFilename);
		else
			str_format(aPath, sizeof(aPath), "assets/hud/%s.png", pPath);
	}

	CImageInfo ImgInfo;
	bool PngLoaded = Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
	if(!PngLoaded && !IsDefault)
	{
		if(AsDir)
			LoadHudSkin("default");
		else
			LoadHudSkin(pPath, true);
	}
	else if(PngLoaded && Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_HUD_AIRJUMP].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_HUD_AIRJUMP].m_pSet->m_Gridy, true) && Graphics()->IsImageFormatRgba(aPath, ImgInfo))
	{
		m_HudSkin.m_SpriteHudAirjump = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_AIRJUMP]);
		m_HudSkin.m_SpriteHudAirjumpEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_AIRJUMP_EMPTY]);
		m_HudSkin.m_SpriteHudSolo = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_SOLO]);
		m_HudSkin.m_SpriteHudCollisionDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_COLLISION_DISABLED]);
		m_HudSkin.m_SpriteHudEndlessJump = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_ENDLESS_JUMP]);
		m_HudSkin.m_SpriteHudEndlessHook = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_ENDLESS_HOOK]);
		m_HudSkin.m_SpriteHudJetpack = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_JETPACK]);
		m_HudSkin.m_SpriteHudFreezeBarFullLeft = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_FREEZE_BAR_FULL_LEFT]);
		m_HudSkin.m_SpriteHudFreezeBarFull = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_FREEZE_BAR_FULL]);
		m_HudSkin.m_SpriteHudFreezeBarEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_FREEZE_BAR_EMPTY]);
		m_HudSkin.m_SpriteHudFreezeBarEmptyRight = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_FREEZE_BAR_EMPTY_RIGHT]);
		m_HudSkin.m_SpriteHudNinjaBarFullLeft = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_NINJA_BAR_FULL_LEFT]);
		m_HudSkin.m_SpriteHudNinjaBarFull = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_NINJA_BAR_FULL]);
		m_HudSkin.m_SpriteHudNinjaBarEmpty = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_NINJA_BAR_EMPTY]);
		m_HudSkin.m_SpriteHudNinjaBarEmptyRight = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_NINJA_BAR_EMPTY_RIGHT]);
		m_HudSkin.m_SpriteHudHookHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_HOOK_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudHammerHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_HAMMER_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudShotgunHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_SHOTGUN_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudGrenadeHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_GRENADE_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudLaserHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_LASER_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudGunHitDisabled = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_GUN_HIT_DISABLED]);
		m_HudSkin.m_SpriteHudDeepFrozen = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_DEEP_FROZEN]);
		m_HudSkin.m_SpriteHudLiveFrozen = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_LIVE_FROZEN]);
		m_HudSkin.m_SpriteHudTeleportGrenade = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_TELEPORT_GRENADE]);
		m_HudSkin.m_SpriteHudTeleportGun = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_TELEPORT_GUN]);
		m_HudSkin.m_SpriteHudTeleportLaser = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_TELEPORT_LASER]);
		m_HudSkin.m_SpriteHudPracticeMode = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_PRACTICE_MODE]);
		m_HudSkin.m_SpriteHudLockMode = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_LOCK_MODE]);
		m_HudSkin.m_SpriteHudTeam0Mode = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_TEAM0_MODE]);
		m_HudSkin.m_SpriteHudDummyHammer = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_DUMMY_HAMMER]);
		m_HudSkin.m_SpriteHudDummyCopy = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_HUD_DUMMY_COPY]);

		m_HudSkinLoaded = true;
	}
	ImgInfo.Free();
}

void CGameClient::LoadExtrasSkin(const char *pPath, bool AsDir)
{
	auto UnloadExtrasSkin = [this](SClientExtrasSkin &Skin) {
		Graphics()->UnloadTexture(&Skin.m_SpriteParticleSnowflake);
		Graphics()->UnloadTexture(&Skin.m_SpriteParticleSparkle);
		Graphics()->UnloadTexture(&Skin.m_SpritePulley);
		Graphics()->UnloadTexture(&Skin.m_SpriteHectagon);
	};

	char aPath[IO_MAX_PATH_LENGTH];
	bool IsDefault = false;
	if(str_comp(pPath, "default") == 0)
	{
		str_copy(aPath, g_pData->m_aImages[IMAGE_EXTRAS].m_pFilename);
		IsDefault = true;
	}
	else
	{
		if(AsDir)
			str_format(aPath, sizeof(aPath), "assets/extras/%s/%s", pPath, g_pData->m_aImages[IMAGE_EXTRAS].m_pFilename);
		else
			str_format(aPath, sizeof(aPath), "assets/extras/%s.png", pPath);
	}

	CImageInfo ImgInfo;
	bool PngLoaded = Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL);
	const bool ValidImage = PngLoaded && Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_PART_SNOWFLAKE].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_PART_SNOWFLAKE].m_pSet->m_Gridy, true) && Graphics()->IsImageFormatRgba(aPath, ImgInfo);
	if(!ValidImage && !IsDefault)
	{
		ImgInfo.Free();
		if(AsDir)
			LoadExtrasSkin("default");
		else
			LoadExtrasSkin(pPath, true);
		return;
	}
	else if(ValidImage)
	{
		SClientExtrasSkin Candidate;
		Candidate.m_SpriteParticleSnowflake = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SNOWFLAKE]);
		Candidate.m_SpriteParticleSparkle = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_SPARKLE]);
		Candidate.m_SpritePulley = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_PULLEY]);
		Candidate.m_SpriteHectagon = Graphics()->LoadSpriteTexture(ImgInfo, &g_pData->m_aSprites[SPRITE_PART_HECTAGON]);
		Candidate.m_aSpriteParticles[0] = Candidate.m_SpriteParticleSnowflake;
		Candidate.m_aSpriteParticles[1] = Candidate.m_SpriteParticleSparkle;
		Candidate.m_aSpriteParticles[2] = Candidate.m_SpritePulley;
		Candidate.m_aSpriteParticles[3] = Candidate.m_SpriteHectagon;
		bool CandidateValid = true;
		for(const auto &Texture : Candidate.m_aSpriteParticles)
			CandidateValid &= Texture.IsValid();
		if(CandidateValid)
		{
			if(m_ExtrasSkinLoaded)
				UnloadExtrasSkin(m_ExtrasSkin);
			m_ExtrasSkin = Candidate;
			m_ExtrasSkinLoaded = true;
		}
		else
		{
			UnloadExtrasSkin(Candidate);
			if(IsDefault)
			{
				if(m_ExtrasSkinLoaded)
					UnloadExtrasSkin(m_ExtrasSkin);
				m_ExtrasSkin = {};
				m_ExtrasSkinLoaded = false;
			}
			else
			{
				ImgInfo.Free();
				if(AsDir)
					LoadExtrasSkin("default");
				else
					LoadExtrasSkin(pPath, true);
				return;
			}
		}
	}
	else if(!IsDefault)
	{
		ImgInfo.Free();
		if(AsDir)
			LoadExtrasSkin("default");
		else
			LoadExtrasSkin(pPath, true);
		return;
	}
	else if(IsDefault)
	{
		if(m_ExtrasSkinLoaded)
			UnloadExtrasSkin(m_ExtrasSkin);
		m_ExtrasSkin = {};
		m_ExtrasSkinLoaded = false;
	}
	ImgInfo.Free();
}

void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)
{
	CTeeRenderInfo &TeeInfo = pManagedTeeRenderInfo->TeeRenderInfo();
	const CSkinDescriptor &SkinDescriptor = pManagedTeeRenderInfo->SkinDescriptor();
	pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(false);
	TeeInfo.ResetMissingDescriptorBranches(SkinDescriptor.m_Flags);
	bool SixReady = false;
	bool SevenReady = false;

	if(SkinDescriptor.m_Flags & CSkinDescriptor::FLAG_SIX)
	{
		const CSkin *pSkin = m_Skins.FindOrNullptr(CSkin::IsValidName(SkinDescriptor.m_aSkinName) ? SkinDescriptor.m_aSkinName : "default");
		if(pSkin != nullptr)
		{
			TeeInfo.Apply(pSkin);
			SixReady = TeeInfo.SixDescriptorReady();
		}
	}

	if(SkinDescriptor.m_Flags & CSkinDescriptor::FLAG_SEVEN)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				m_Skins7.FindSkinPart(Part, SkinDescriptor.m_aSixup[Dummy].m_aaSkinPartNames[Part], true)->ApplyTo(TeeInfo.m_aSixup[Dummy]);

				if(SkinDescriptor.m_aSixup[Dummy].m_XmasHat)
				{
					TeeInfo.m_aSixup[Dummy].m_HatTexture = m_Skins7.XmasHatTexture();
				}
				else
				{
					TeeInfo.m_aSixup[Dummy].m_HatTexture.Invalidate();
				}

				if(SkinDescriptor.m_aSixup[Dummy].m_BotDecoration)
				{
					TeeInfo.m_aSixup[Dummy].m_BotTexture = m_Skins7.BotDecorationTexture();
				}
				else
				{
					TeeInfo.m_aSixup[Dummy].m_BotTexture.Invalidate();
				}
			}
		}
		SevenReady = TeeInfo.SevenDescriptorReady();
	}
	bool DescriptorRenderInfoReady = SixReady || SevenReady;
	pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(DescriptorRenderInfoReady);

	if(SkinDescriptor.m_Flags != 0 && pManagedTeeRenderInfo->m_RefreshCallback)
	{
		pManagedTeeRenderInfo->m_RefreshCallback();
	}
}

void CGameClient::RefreshSkins(int SkinDescriptorFlags)
{
	dbg_assert(SkinDescriptorFlags != 0, "SkinDescriptorFlags invalid");

	const auto SkinStartLoadTime = time_get_nanoseconds();
	const auto ProgressCallback = [this, SkinStartLoadTime]() {
		// if skin refreshing takes to long, swap to a loading screen
		if(time_get_nanoseconds() - SkinStartLoadTime > 500ms)
		{
			m_Menus.RenderLoading(Localize("Loading skin files"), "", 0);
		}
	};
	if(SkinDescriptorFlags & CSkinDescriptor::FLAG_SIX)
	{
		m_Skins.Refresh(ProgressCallback);
	}
	if(SkinDescriptorFlags & CSkinDescriptor::FLAG_SEVEN)
	{
		m_Skins7.Refresh([this, ProgressCallback]() {
			ProgressCallback();
			for(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo : m_vpManagedTeeRenderInfos)
			{
				if(pManagedTeeRenderInfo->SkinDescriptor().m_Flags & CSkinDescriptor::FLAG_SEVEN)
					RefreshSkin(pManagedTeeRenderInfo);
			}
		});
	}

	for(std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo : m_vpManagedTeeRenderInfos)
	{
		if(!(pManagedTeeRenderInfo->SkinDescriptor().m_Flags & SkinDescriptorFlags))
		{
			continue;
		}
		RefreshSkin(pManagedTeeRenderInfo);
	}
}

void CGameClient::OnSkinUpdate(const char *pSkinName)
{
	// If the refreshed skin's name starts with the current skin prefix, we also have to
	// refresh skins matching the unprefixed skin name, e.g. if "santa_cammo" is refreshed
	// with prefix "santa" we need to refresh both "santa_cammo" and "cammo".
	const char *pSkinPrefix = m_Skins.SkinPrefix();
	const int SkinPrefixLength = str_length(pSkinPrefix);
	char aSkinNameWithoutPrefix[MAX_SKIN_LENGTH];
	if(SkinPrefixLength > 0 &&
		str_comp_num(pSkinName, pSkinPrefix, SkinPrefixLength) == 0 &&
		pSkinName[SkinPrefixLength] == '_' &&
		pSkinName[SkinPrefixLength + 1] != '\0')
	{
		str_copy(aSkinNameWithoutPrefix, &pSkinName[SkinPrefixLength + 1]);
	}
	else
	{
		aSkinNameWithoutPrefix[0] = '\0';
	}
	const auto &&NameMatches = [&](const char *pCheckName) {
		if(str_comp(pCheckName, pSkinName) == 0)
		{
			return true;
		}
		if(aSkinNameWithoutPrefix[0] != '\0' &&
			str_comp(pCheckName, aSkinNameWithoutPrefix) == 0)
		{
			return true;
		}
		return false;
	};

	for(std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo : m_vpManagedTeeRenderInfos)
	{
		if(!(pManagedTeeRenderInfo->SkinDescriptor().m_Flags & CSkinDescriptor::FLAG_SIX) ||
			!NameMatches(pManagedTeeRenderInfo->SkinDescriptor().m_aSkinName))
		{
			continue;
		}
		RefreshSkin(pManagedTeeRenderInfo);
	}
}

std::shared_ptr<CManagedTeeRenderInfo> CGameClient::CreateManagedTeeRenderInfo(const CTeeRenderInfo &TeeRenderInfo, const CSkinDescriptor &SkinDescriptor)
{
	std::shared_ptr<CManagedTeeRenderInfo> pManagedTeeRenderInfo = std::make_shared<CManagedTeeRenderInfo>(TeeRenderInfo, SkinDescriptor);
	RefreshSkin(pManagedTeeRenderInfo);
	m_vpManagedTeeRenderInfos.emplace_back(pManagedTeeRenderInfo);
	return pManagedTeeRenderInfo;
}

std::shared_ptr<CManagedTeeRenderInfo> CGameClient::CreateManagedTeeRenderInfo(const CClientData &Client)
{
	if(ShouldHideStreamerSkin(Client.ClientId()))
	{
		CTeeRenderInfo TeeRenderInfo;
		TeeRenderInfo.m_Size = Client.m_RenderInfo.m_Size;
		CSkinDescriptor SkinDescriptor;
		BuildDefaultSkinDescriptor(SkinDescriptor);
		return CreateManagedTeeRenderInfo(TeeRenderInfo, SkinDescriptor);
	}

	return CreateManagedTeeRenderInfo(Client.m_RenderInfo, Client.ToSkinDescriptor());
}

void CGameClient::UpdateManagedTeeRenderInfos()
{
	while(!m_vpManagedTeeRenderInfos.empty())
	{
		auto UnusedInfo = std::find_if(m_vpManagedTeeRenderInfos.begin(), m_vpManagedTeeRenderInfos.end(), [&](const auto &pItem) {
			return pItem.use_count() <= 1;
		});
		if(UnusedInfo == m_vpManagedTeeRenderInfos.end())
		{
			break;
		}
		m_vpManagedTeeRenderInfos.erase(UnusedInfo);
	}
}

void CGameClient::CollectManagedTeeRenderInfos(const std::function<void(const char *pSkinName)> &ActiveSkinAcceptor)
{
	for(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo : m_vpManagedTeeRenderInfos)
	{
		if((pManagedTeeRenderInfo->m_SkinDescriptor.m_Flags & CSkinDescriptor::FLAG_SIX) &&
			CSkin::IsValidName(pManagedTeeRenderInfo->m_SkinDescriptor.m_aSkinName))
		{
			ActiveSkinAcceptor(pManagedTeeRenderInfo->m_SkinDescriptor.m_aSkinName);
		}
	}
	for(const CClientData &ClientData : m_aClients)
	{
		if(CSkin::IsValidName(ClientData.m_RenderInfoSkinDescriptor.m_aSkinName))
			ActiveSkinAcceptor(ClientData.m_RenderInfoSkinDescriptor.m_aSkinName);
	}
}

void CGameClient::ConchainRefreshSkins(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pThis->m_Menus.IsInit())
	{
		pThis->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}

void CGameClient::ConchainRefreshEventSkins(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pThis = static_cast<CGameClient *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pThis->m_Menus.IsInit())
	{
		pThis->m_Skins.RefreshEventSkins();
		pThis->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}

static bool UnknownMapSettingCallback(const char *pCommand, void *pUser)
{
	return true;
}

void CGameClient::LoadMapSettings()
{
	IEngineMap *pMap = Kernel()->RequestInterface<IEngineMap>();
	m_MapBugs = CMapBugs::Create(Client()->GetCurrentMap(), 0, SHA256_DIGEST{});

	// Reset Tunezones
	for(int TuneZone = 0; TuneZone < TuneZone::NUM; TuneZone++)
	{
		TuningList()[TuneZone] = CTuningParams::DEFAULT;
		TuningList()[TuneZone].Set("gun_curvature", 0);
		TuningList()[TuneZone].Set("gun_speed", 1400);
		TuningList()[TuneZone].Set("shotgun_curvature", 0);
		TuningList()[TuneZone].Set("shotgun_speed", 500);
		TuningList()[TuneZone].Set("shotgun_speeddiff", 0);
	}

	if(!pMap || !pMap->IsLoaded())
	{
		return;
	}

	m_MapBugs = CMapBugs::Create(Client()->GetCurrentMap(), pMap->Size(), pMap->Sha256());

	// Load map tunings
	int Start, Num;
	pMap->GetType(MAPITEMTYPE_INFO, &Start, &Num);
	for(int i = Start; i < Start + Num; i++)
	{
		int ItemId;
		CMapItemInfoSettings *pItem = (CMapItemInfoSettings *)pMap->GetItem(i, nullptr, &ItemId);
		int ItemSize = pMap->GetItemSize(i);
		if(!pItem || ItemId != 0)
			continue;

		if(ItemSize < (int)sizeof(CMapItemInfoSettings))
			break;
		if(!(pItem->m_Settings > -1))
			break;

		int Size = pMap->GetDataSize(pItem->m_Settings);
		char *pSettings = (char *)pMap->GetData(pItem->m_Settings);
		char *pNext = pSettings;
		Console()->SetUnknownCommandCallback(UnknownMapSettingCallback, nullptr);
		while(pNext < pSettings + Size)
		{
			int StrSize = str_length(pNext) + 1;
			Console()->ExecuteLine(pNext, IConsole::CLIENT_ID_GAME);
			pNext += StrSize;
		}
		Console()->SetUnknownCommandCallback(IConsole::EmptyUnknownCommandCallback, nullptr);
		pMap->UnloadData(pItem->m_Settings);
		break;
	}
}

void CGameClient::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameClient *pSelf = (CGameClient *)pUserData;
	const char *pParamName = pResult->GetString(0);
	if(pResult->NumArguments() == 2)
	{
		float NewValue = pResult->GetFloat(1);
		pSelf->TuningList()[0].Set(pParamName, NewValue);
	}
}

void CGameClient::ConTuneZone(IConsole::IResult *pResult, void *pUserData)
{
	CGameClient *pSelf = (CGameClient *)pUserData;
	int List = pResult->GetInteger(0);
	const char *pParamName = pResult->GetString(1);
	float NewValue = pResult->GetFloat(2);

	if(List >= 0 && List < TuneZone::NUM)
		pSelf->TuningList()[List].Set(pParamName, NewValue);
}

void CGameClient::ConMapbug(IConsole::IResult *pResult, void *pUserData)
{
	CGameClient *pSelf = (CGameClient *)pUserData;
	const char *pMapBugName = pResult->GetString(0);

	switch(pSelf->m_MapBugs.Update(pMapBugName))
	{
	case EMapBugUpdate::OK:
		break;
	case EMapBugUpdate::OVERRIDDEN:
		log_debug("mapbugs", "map-internal setting overridden by database");
		break;
	case EMapBugUpdate::NOTFOUND:
		log_debug("mapbugs", "unknown map bug '%s', ignoring", pMapBugName);
		break;
	default:
		dbg_assert_failed("unreachable");
	}
}

void CGameClient::ConchainMenuMap(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameClient *pSelf = (CGameClient *)pUserData;
	if(pResult->NumArguments())
	{
		if(str_comp(g_Config.m_ClMenuMap, pResult->GetString(0)) != 0)
		{
			str_copy(g_Config.m_ClMenuMap, pResult->GetString(0));
			pSelf->m_MenuBackground.LoadMenuBackground();
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CGameClient::DummyResetInput()
{
	if(!Client()->DummyConnected())
		return;

	if((m_DummyInput.m_Fire & 1) != 0)
		m_DummyInput.m_Fire++;
	m_DummyInput.m_Fire &= INPUT_STATE_MASK;

	m_Controls.ResetInput(!g_Config.m_ClDummy);
	m_Controls.m_aInputData[!g_Config.m_ClDummy].m_Hook = 0;
	m_Controls.m_aInputData[!g_Config.m_ClDummy].m_Fire = m_DummyInput.m_Fire;

	m_DummyInput = m_Controls.m_aInputData[!g_Config.m_ClDummy];
}

bool CGameClient::CanDisplayWarning() const
{
	return m_Menus.CanDisplayWarning();
}

CNetObjHandler *CGameClient::GetNetObjHandler()
{
	return &m_NetObjHandler;
}

protocol7::CNetObjHandler *CGameClient::GetNetObjHandler7()
{
	return &m_NetObjHandler7;
}

void CGameClient::SnapCollectEntities()
{
	int NumSnapItems = Client()->SnapNumItems(IClient::SNAP_CURRENT);

	std::vector<CSnapEntities> vItemData;
	std::vector<CSnapEntities> vItemEx;

	for(int Index = 0; Index < NumSnapItems; Index++)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, Index);
		if(Item.m_Type == NETOBJTYPE_ENTITYEX)
			vItemEx.push_back({Item, nullptr});
		else if(Item.m_Type == NETOBJTYPE_PICKUP || Item.m_Type == NETOBJTYPE_DDNETPICKUP || Item.m_Type == NETOBJTYPE_LASER || Item.m_Type == NETOBJTYPE_DDNETLASER || Item.m_Type == NETOBJTYPE_PROJECTILE || Item.m_Type == NETOBJTYPE_DDRACEPROJECTILE || Item.m_Type == NETOBJTYPE_DDNETPROJECTILE)
			vItemData.push_back({Item, nullptr});
	}

	// sort by id
	class CEntComparer
	{
	public:
		bool operator()(const CSnapEntities &Lhs, const CSnapEntities &Rhs) const
		{
			return Lhs.m_Item.m_Id < Rhs.m_Item.m_Id;
		}
	};

	std::sort(vItemData.begin(), vItemData.end(), CEntComparer());
	std::sort(vItemEx.begin(), vItemEx.end(), CEntComparer());

	// merge extended items with items they belong to
	m_vSnapEntities.clear();

	size_t IndexEx = 0;
	for(const CSnapEntities &Ent : vItemData)
	{
		while(IndexEx < vItemEx.size() && vItemEx[IndexEx].m_Item.m_Id < Ent.m_Item.m_Id)
			IndexEx++;

		const CNetObj_EntityEx *pDataEx = nullptr;
		if(IndexEx < vItemEx.size() && vItemEx[IndexEx].m_Item.m_Id == Ent.m_Item.m_Id)
			pDataEx = (const CNetObj_EntityEx *)vItemEx[IndexEx].m_Item.m_pData;

		m_vSnapEntities.push_back({Ent.m_Item, pDataEx});
	}
}

void CGameClient::HandleMultiView()
{
	bool IsTeamZero = IsMultiViewIdSet();
	bool Init = false;
	vec2 MinPos, MaxPos;
	float SumVel = 0.0f;
	int AmountPlayers = 0;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		// look at players who are vanished
		if(m_MultiView.m_aVanish[ClientId])
		{
			// not in freeze anymore and the delay is over
			if(m_MultiView.m_aLastFreeze[ClientId] + 6.0f <= Client()->LocalTime() && m_aClients[ClientId].m_FreezeEnd == 0)
			{
				m_MultiView.m_aVanish[ClientId] = false;
				m_MultiView.m_aLastFreeze[ClientId] = 0.0f;
			}
		}

		// we look at team 0 and the player is not in the spec list
		if(IsTeamZero && !m_aMultiViewId[ClientId])
			continue;

		// player is vanished
		if(m_MultiView.m_aVanish[ClientId])
			continue;

		// the player is not in the team we are spectating
		if(m_Teams.Team(ClientId) != m_MultiViewTeam)
			continue;

		vec2 PlayerPos;
		if(m_Snap.m_aCharacters[ClientId].m_Active)
			PlayerPos = m_aClients[ClientId].m_RenderPos;
		else if(m_aClients[ClientId].m_Spec) // tee is in spec
			PlayerPos = m_aClients[ClientId].m_SpecChar;
		else
			continue;

		// player is far away and frozen
		if(distance(m_MultiView.m_OldPos, PlayerPos) > 1100 && m_aClients[ClientId].m_FreezeEnd != 0)
		{
			// check if the player is frozen for more than 3 seconds, if so vanish them
			if(m_MultiView.m_aLastFreeze[ClientId] == 0.0f)
			{
				m_MultiView.m_aLastFreeze[ClientId] = Client()->LocalTime();
			}
			else if(m_MultiView.m_aLastFreeze[ClientId] + 3.0f <= Client()->LocalTime())
			{
				m_MultiView.m_aVanish[ClientId] = true;
				// player we want to be vanished is our "main" tee, so lets switch the tee
				if(ClientId == m_Snap.m_SpecInfo.m_SpectatorId)
					m_Spectator.Spectate(FindFirstMultiViewId());
			}
		}
		else if(m_MultiView.m_aLastFreeze[ClientId] != 0)
		{
			m_MultiView.m_aLastFreeze[ClientId] = 0;
		}

		// set the minimum and maximum position
		if(!Init)
		{
			MinPos = PlayerPos;
			MaxPos = PlayerPos;
			Init = true;
		}
		else
		{
			MinPos.x = std::min(MinPos.x, PlayerPos.x);
			MaxPos.x = std::max(MaxPos.x, PlayerPos.x);
			MinPos.y = std::min(MinPos.y, PlayerPos.y);
			MaxPos.y = std::max(MaxPos.y, PlayerPos.y);
		}

		// sum up the velocity of all players we are spectating
		const CNetObj_Character &CurrentCharacter = m_Snap.m_aCharacters[ClientId].m_Cur;
		SumVel += length(vec2(CurrentCharacter.m_VelX / 256.0f, CurrentCharacter.m_VelY / 256.0f)) * 50.0f / 32.0f;
		AmountPlayers++;
	}

	// if we have found no players, we disable multi view
	if(AmountPlayers == 0)
	{
		if(m_MultiView.m_SecondChance == 0.0f)
		{
			m_MultiView.m_SecondChance = Client()->LocalTime() + 0.3f;
		}
		else if(m_MultiView.m_SecondChance < Client()->LocalTime())
		{
			ResetMultiView();
		}
		return;
	}
	else if(m_MultiView.m_SecondChance != 0.0f)
	{
		m_MultiView.m_SecondChance = 0.0f;
	}

	// if we only have one tee that's in the list, we activate solo-mode
	m_MultiView.m_Solo = std::count(std::begin(m_aMultiViewId), std::end(m_aMultiViewId), true) == 1;

	vec2 TargetPos = vec2((MinPos.x + MaxPos.x) / 2.0f, (MinPos.y + MaxPos.y) / 2.0f);
	// dont hide the position hud if its only one player
	m_MultiViewShowHud = AmountPlayers == 1;
	// get the average velocity
	float AvgVel = std::clamp(SumVel / AmountPlayers, 0.0f, 1000.0f);

	if(m_MultiView.m_OldPersonalZoom == m_MultiViewPersonalZoom)
		m_Camera.SetZoom(CalculateMultiViewZoom(MinPos, MaxPos, AvgVel), g_Config.m_ClMultiViewZoomSmoothness, false);
	else
		m_Camera.SetZoom(CalculateMultiViewZoom(MinPos, MaxPos, AvgVel), 50, false);

	m_Snap.m_SpecInfo.m_Position = m_MultiView.m_OldPos + ((TargetPos - m_MultiView.m_OldPos) * CalculateMultiViewMultiplier(TargetPos));
	m_MultiView.m_OldPos = m_Snap.m_SpecInfo.m_Position;
	m_Snap.m_SpecInfo.m_UsePosition = true;
}

bool CGameClient::InitMultiView(int Team)
{
	float Width, Height;
	CleanMultiViewIds();
	m_MultiView.m_IsInit = true;
	m_MultiView.m_SecondChance = 0.0f;
	m_MultiView.m_OldCameraDistance = 0.0f;
	m_MultiView.m_OldPos = m_Camera.m_Center;
	m_MultiView.m_OldPersonalZoom = m_MultiViewPersonalZoom;

	// get the current view coordinates
	Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), m_Camera.m_Zoom, &Width, &Height);
	vec2 AxisX = vec2(m_Camera.m_Center.x - (Width / 2.0f), m_Camera.m_Center.x + (Width / 2.0f));
	vec2 AxisY = vec2(m_Camera.m_Center.y - (Height / 2.0f), m_Camera.m_Center.y + (Height / 2.0f));

	if(Team > 0)
	{
		m_MultiViewTeam = Team;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			m_aMultiViewId[ClientId] = m_Teams.Team(ClientId) == Team;
	}
	else
	{
		// we want to allow spectating players in teams directly if there is no other team on screen
		// to do that, -1 is used temporarily for "we don't know which team to spectate yet"
		m_MultiViewTeam = -1;

		int Count = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			vec2 PlayerPos;

			// get the position of the player
			if(m_Snap.m_aCharacters[ClientId].m_Active)
				PlayerPos = vec2(m_Snap.m_aCharacters[ClientId].m_Cur.m_X, m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
			else if(m_aClients[ClientId].m_Spec)
				PlayerPos = m_aClients[ClientId].m_SpecChar;
			else
				continue;

			if(PlayerPos.x == 0 || PlayerPos.y == 0)
				continue;

			// skip players that aren't in view
			if(PlayerPos.x <= AxisX.x || PlayerPos.x >= AxisX.y || PlayerPos.y <= AxisY.x || PlayerPos.y >= AxisY.y)
				continue;

			if(m_MultiViewTeam == -1)
			{
				// use the current player's team for now, but it might switch to team 0 if any other team is found
				m_MultiViewTeam = m_Teams.Team(ClientId);
			}
			else if(m_MultiViewTeam != 0 && m_Teams.Team(ClientId) != m_MultiViewTeam)
			{
				// mismatched teams; remove all previously added players again and switch to team 0 instead
				std::fill_n(m_aMultiViewId, ClientId, false);
				m_MultiViewTeam = 0;
			}

			m_aMultiViewId[ClientId] = true;
			Count++;
		}

		// might still be -1 if not a single player was in view; fallback to team 0 in that case
		if(m_MultiViewTeam == -1)
			m_MultiViewTeam = 0;

		// we are spectating only one player
		m_MultiView.m_Solo = Count == 1;
	}

	if(IsMultiViewIdSet())
	{
		int SpectatorId = m_Snap.m_SpecInfo.m_SpectatorId;
		int NewSpectatorId = -1;

		vec2 CurPosition(m_Camera.m_Center);
		if(SpectatorId != SPEC_FREEVIEW)
		{
			const CNetObj_Character &CurCharacter = m_Snap.m_aCharacters[SpectatorId].m_Cur;
			CurPosition.x = CurCharacter.m_X;
			CurPosition.y = CurCharacter.m_Y;
		}

		int ClosestDistance = std::numeric_limits<int>::max();
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(!m_Snap.m_apPlayerInfos[ClientId] || m_Snap.m_apPlayerInfos[ClientId]->m_Team == TEAM_SPECTATORS || m_Teams.Team(ClientId) != m_MultiViewTeam)
				continue;

			vec2 PlayerPos;
			if(m_Snap.m_aCharacters[ClientId].m_Active)
				PlayerPos = vec2(m_aClients[ClientId].m_RenderPos.x, m_aClients[ClientId].m_RenderPos.y);
			else if(m_aClients[ClientId].m_Spec) // tee is in spec
				PlayerPos = m_aClients[ClientId].m_SpecChar;
			else
				continue;

			int Distance = distance(CurPosition, PlayerPos);
			if(NewSpectatorId == -1 || Distance < ClosestDistance)
			{
				NewSpectatorId = ClientId;
				ClosestDistance = Distance;
			}
		}

		if(NewSpectatorId > -1)
			m_Spectator.Spectate(NewSpectatorId);
	}

	return IsMultiViewIdSet();
}

float CGameClient::CalculateMultiViewMultiplier(vec2 TargetPos)
{
	float MaxCameraDist = 200.0f;
	float MinCameraDist = 20.0f;
	float MaxVel = g_Config.m_ClMultiViewSensitivity / 150.0f;
	float MinVel = 0.007f;
	float CurrentCameraDistance = distance(m_MultiView.m_OldPos, TargetPos);
	float UpperLimit = 1.0f;

	if(m_MultiView.m_Teleported && CurrentCameraDistance <= 100.0f)
		m_MultiView.m_Teleported = false;

	// somebody got teleported very likely
	if((m_MultiView.m_Teleported || CurrentCameraDistance - m_MultiView.m_OldCameraDistance > 100.0f) && m_MultiView.m_OldCameraDistance != 0.0f)
	{
		UpperLimit = 0.1f; // dont try to compensate it by flickering
		m_MultiView.m_Teleported = true;
	}
	m_MultiView.m_OldCameraDistance = CurrentCameraDistance;

	return std::clamp(MapValue(MaxCameraDist, MinCameraDist, MaxVel, MinVel, CurrentCameraDistance), MinVel, UpperLimit);
}

float CGameClient::CalculateMultiViewZoom(vec2 MinPos, vec2 MaxPos, float Vel)
{
	float Ratio = Graphics()->GameScreenAspect();
	float ZoomX = 0.0f, ZoomY;

	// only calc two axis if the aspect ratio is not 1:1
	if(Ratio != 1.0f)
		ZoomX = (0.001309f - 0.000328f * Ratio) * (MaxPos.x - MinPos.x) + (0.741413f - 0.032959f * Ratio);

	// calculate the according zoom with linear function
	ZoomY = 0.001309f * (MaxPos.y - MinPos.y) + 0.741413f;
	// choose the highest zoom
	float Zoom = std::max(ZoomX, ZoomY);
	// zoom out to maximum 10 percent of the current zoom for 70 velocity
	float Diff = std::clamp(MapValue(70.0f, 15.0f, Zoom * 0.10f, 0.0f, Vel), 0.0f, Zoom * 0.10f);
	// zoom should stay between 1.1 and 20.0
	Zoom = std::clamp(Zoom + Diff, 1.1f, 20.0f);
	// dont go below default zoom
	Zoom = std::max(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), Zoom);
	// add the user preference
	Zoom -= Zoom * 0.1f * m_MultiViewPersonalZoom;
	m_MultiView.m_OldPersonalZoom = m_MultiViewPersonalZoom;

	return Zoom;
}

float CGameClient::MapValue(float MaxValue, float MinValue, float MaxRange, float MinRange, float Value)
{
	return (MaxRange - MinRange) / (MaxValue - MinValue) * (Value - MinValue) + MinRange;
}

void CGameClient::ResetMultiView()
{
	m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);
	m_MultiViewPersonalZoom = 0.0f;
	m_MultiViewActivated = false;
	m_MultiView.m_Solo = false;
	m_MultiView.m_IsInit = false;
	m_MultiView.m_Teleported = false;
	m_MultiView.m_OldCameraDistance = 0.0f;
}

void CGameClient::CleanMultiViewIds()
{
	std::fill(std::begin(m_aMultiViewId), std::end(m_aMultiViewId), false);
	std::fill(std::begin(m_MultiView.m_aLastFreeze), std::end(m_MultiView.m_aLastFreeze), 0.0f);
	std::fill(std::begin(m_MultiView.m_aVanish), std::end(m_MultiView.m_aVanish), false);
}

void CGameClient::CleanMultiViewId(int ClientId)
{
	if(ClientId >= MAX_CLIENTS || ClientId < 0)
		return;

	m_aMultiViewId[ClientId] = false;
	m_MultiView.m_aLastFreeze[ClientId] = 0.0f;
	m_MultiView.m_aVanish[ClientId] = false;
}

bool CGameClient::IsMultiViewIdSet()
{
	return std::any_of(std::begin(m_aMultiViewId), std::end(m_aMultiViewId), [](bool IsSet) { return IsSet; });
}

int CGameClient::FindFirstMultiViewId()
{
	int ClientId = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aMultiViewId[i] && !m_MultiView.m_aVanish[i])
			return i;
	}
	return ClientId;
}

void CGameClient::OnSaveCodeNetMessage(const CNetMsg_Sv_SaveCode *pMsg)
{
	char aBuf[512];
	if(pMsg->m_pError[0] != '\0')
		m_Chat.AddLine(-1, TEAM_ALL, pMsg->m_pError);

	int State = pMsg->m_State;
	if(State == SAVESTATE_PENDING)
	{
		if(pMsg->m_pCode[0] == '\0')
		{
			str_format(aBuf, sizeof(aBuf),
				Localize("Team save in progress. You'll be able to load with '/load %s'"),
				pMsg->m_pGeneratedCode);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf),
				Localize("Team save in progress. You'll be able to load with '/load %s' if save is successful or with '/load %s' if it fails"),
				pMsg->m_pCode,
				pMsg->m_pGeneratedCode);
		}
		m_Chat.AddLine(-1, TEAM_ALL, aBuf);
	}
	else if(State == SAVESTATE_DONE)
	{
		if(pMsg->m_pServerName[0] == '\0')
		{
			str_format(aBuf, sizeof(aBuf),
				"Team successfully saved by %s. Use '/load %s' to continue",
				pMsg->m_pSaveRequester,
				pMsg->m_pCode[0] ? pMsg->m_pCode : pMsg->m_pGeneratedCode);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf),
				"Team successfully saved by %s. Use '/load %s' on %s to continue",
				pMsg->m_pSaveRequester,
				pMsg->m_pCode[0] ? pMsg->m_pCode : pMsg->m_pGeneratedCode,
				pMsg->m_pServerName);
		}
		m_Chat.AddLine(-1, TEAM_ALL, aBuf);
	}
	else if(State == SAVESTATE_FALLBACKFILE)
	{
		if(pMsg->m_pServerName[0] == '\0')
		{
			str_format(aBuf, sizeof(aBuf),
				Localize("Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' to continue"),
				pMsg->m_pSaveRequester,
				pMsg->m_pGeneratedCode);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf),
				Localize("Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' on %s to continue"),
				pMsg->m_pSaveRequester,
				pMsg->m_pGeneratedCode,
				pMsg->m_pServerName);
		}
		m_Chat.AddLine(-1, TEAM_ALL, aBuf);
	}
	else if(State == SAVESTATE_ERROR)
	{
		m_Chat.AddLine(-1, TEAM_ALL, Localize("Save failed!"));
	}

	if(State != SAVESTATE_PENDING && State != SAVESTATE_ERROR && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		StoreSave(pMsg->m_pTeamMembers, pMsg->m_pCode[0] ? pMsg->m_pCode : pMsg->m_pGeneratedCode);
	}
}

void CGameClient::StoreSave(const char *pTeamMembers, const char *pGeneratedCode) const
{
	static constexpr const char *SAVES_HEADER[] = {
		"Time",
		"Players",
		"Map",
		"Code",
	};

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("saves", "Failed to open the saves file '%s'", SAVES_FILE);
		return;
	}

	const char *apColumns[std::size(SAVES_HEADER)] = {
		aTimestamp,
		pTeamMembers,
		Client()->GetCurrentMap(),
		pGeneratedCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, std::size(SAVES_HEADER), SAVES_HEADER);
	}
	CsvWrite(File, std::size(SAVES_HEADER), apColumns);
	io_close(File);
}

// TClient

bool CGameClient::CheckNewInput()
{
	return m_Controls.CheckNewInput();
}

bool CGameClient::IsFastInputActive() const
{
	return m_TClient.IsFastInputActive();
}

void CGameClient::SetConnectInfo(const NETADDR *pAddress)
{
	m_ConnectServerInfo = std::nullopt;
	if(!pAddress)
		return;
	const auto *pEntry = ServerBrowser()->Find(*pAddress);
	if(!pEntry)
		return;
	m_ConnectServerInfo = pEntry->m_Info;
	const CNetObj_GameInfoEx GameInfoEx = {.m_Version = 0};
	m_GameInfo = GetGameInfo(&GameInfoEx, 0, &*m_ConnectServerInfo);
}

// Q1menG Client Recognition Functions
void CGameClient::ClearQ1menGSyncMarks()
{
	std::fill(std::begin(m_aQ1menGSyncMarkUntil), std::end(m_aQ1menGSyncMarkUntil), 0);
	std::fill(std::begin(m_aQ1menGSyncFootParticlesEnabled), std::end(m_aQ1menGSyncFootParticlesEnabled), false);
	std::fill(std::begin(m_aQ1menGSyncRemoteParticlesEnabled), std::end(m_aQ1menGSyncRemoteParticlesEnabled), false);
	std::fill(std::begin(m_aQ1menGSyncClientBrands), std::end(m_aQ1menGSyncClientBrands), EClientBrand::NONE);
	for(auto &aQid : m_aaQ1menGSyncQid)
		aQid[0] = '\0';
}

void CGameClient::MarkQ1menGSyncClient(int ClientId, int64_t ExpireTick, bool FootParticlesEnabled, bool RemoteParticlesEnabled, const char *pQid, EClientBrand ClientBrand)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(ExpireTick <= 0)
		return;
	m_aQ1menGSyncMarkUntil[ClientId] = maximum(m_aQ1menGSyncMarkUntil[ClientId], ExpireTick);
	m_aQ1menGSyncFootParticlesEnabled[ClientId] = FootParticlesEnabled;
	m_aQ1menGSyncRemoteParticlesEnabled[ClientId] = RemoteParticlesEnabled;
	m_aQ1menGSyncClientBrands[ClientId] = ClientBrand == EClientBrand::NONE ? EClientBrand::QM : ClientBrand;
	if(pQid && pQid[0] != '\0')
		str_copy(m_aaQ1menGSyncQid[ClientId], pQid, sizeof(m_aaQ1menGSyncQid[ClientId]));
}

bool CGameClient::IsQ1menGClientRecognized(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const int64_t Now = time_get();
	if(m_aQ1menGSyncMarkUntil[ClientId] > Now)
		return true;
	return false;
}

const char *CGameClient::GetQ1menGClientQid(int ClientId) const
{
	if(!IsQ1menGClientRecognized(ClientId))
		return "";
	return m_aaQ1menGSyncQid[ClientId];
}

bool CGameClient::ShouldRenderQ1menGRemoteFootParticles(int ClientId) const
{
	if(!IsQ1menGClientRecognized(ClientId))
		return false;

	return m_aQ1menGSyncRemoteParticlesEnabled[ClientId] && m_aQ1menGSyncFootParticlesEnabled[ClientId];
}

void CGameClient::ClearQmVoiceSyncMarks()
{
	std::fill(std::begin(m_aQmVoiceSyncMarkUntil), std::end(m_aQmVoiceSyncMarkUntil), 0);
}

void CGameClient::MarkQmVoiceSupportedClient(int ClientId, int64_t ExpireTick)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(ExpireTick <= 0)
		return;
	m_aQmVoiceSyncMarkUntil[ClientId] = maximum(m_aQmVoiceSyncMarkUntil[ClientId], ExpireTick);
}

bool CGameClient::IsQmVoiceSupportedClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	return m_aQmVoiceSyncMarkUntil[ClientId] > time_get();
}

void CGameClient::ClearQmDeveloperMarks()
{
	std::fill(std::begin(m_aQmDeveloperMarkUntil), std::end(m_aQmDeveloperMarkUntil), 0);
	std::fill(std::begin(m_aQmDeveloperRainbow), std::end(m_aQmDeveloperRainbow), false);
	mem_zero(m_aaQmDeveloperMarkName, sizeof(m_aaQmDeveloperMarkName));
}

void CGameClient::MarkQmDeveloperClient(int ClientId, const char *pPlayerName, int64_t ExpireTick, bool Rainbow)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pPlayerName || pPlayerName[0] == '\0' || ExpireTick <= 0)
		return;
	m_aQmDeveloperMarkUntil[ClientId] = maximum(m_aQmDeveloperMarkUntil[ClientId], ExpireTick);
	m_aQmDeveloperRainbow[ClientId] = Rainbow;
	str_copy(m_aaQmDeveloperMarkName[ClientId], pPlayerName);
}

bool CGameClient::IsQmDeveloperAuthenticated(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       IsQmDeveloperMarkCurrent(
		       m_aClients[ClientId].m_Active,
		       m_aaQmDeveloperMarkName[ClientId],
		       m_aClients[ClientId].m_aName,
		       m_aQmDeveloperMarkUntil[ClientId],
		       time_get());
}

bool CGameClient::IsQmDeveloperRainbow(int ClientId) const
{
	return IsQmDeveloperAuthenticated(ClientId) && m_aQmDeveloperRainbow[ClientId];
}

void CGameClient::ClearClientBrands()
{
	for(auto &aName : m_aaClientBrandNames)
		aName[0] = '\0';
	std::fill(std::begin(m_aClientBrands), std::end(m_aClientBrands), EClientBrand::NONE);
}

EClientBrand CGameClient::ClientBrand(const char *pName) const
{
	if(!pName || pName[0] == '\0')
		return EClientBrand::NONE;
	for(int Entry = 0; Entry < MAX_CLIENTS; ++Entry)
	{
		if(m_aClientBrands[Entry] != EClientBrand::NONE && str_comp(m_aaClientBrandNames[Entry], pName) == 0)
			return m_aClientBrands[Entry];
	}
	const int64_t Now = time_get();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(m_aQ1menGSyncMarkUntil[ClientId] > Now && m_aQ1menGSyncClientBrands[ClientId] != EClientBrand::NONE && m_aClients[ClientId].m_Active && str_comp(m_aClients[ClientId].m_aName, pName) == 0)
			return m_aQ1menGSyncClientBrands[ClientId];
	}
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		const int ClientId = m_aLocalIds[Dummy];
		if(ClientId >= 0 && ClientId < MAX_CLIENTS && m_aClients[ClientId].m_Active && str_comp(m_aClients[ClientId].m_aName, pName) == 0)
			return EClientBrand::QM;
	}
	return EClientBrand::NONE;
}
