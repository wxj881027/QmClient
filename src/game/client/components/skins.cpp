/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "skins.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/qm_skin_outline.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/components/settings_runtime_cache.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

static constexpr int SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 1000;
static constexpr const char *OFFICIAL_SKIN_INDEX_URL = "https://ddnet.org/skins/skin/skins.json";
static constexpr const char *OFFICIAL_SKIN_INDEX_CACHE_PATH = "downloadedskins/official_skins.json";

static int &SkinQueueEnabledVar(int Dummy)
{
	return Dummy ? g_Config.m_QmDummySkinQueueEnabled : g_Config.m_QmSkinQueueEnabled;
}

static int SettingsSkinDecodeJobWorkerBudget()
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	return 4;
#else
	return maximum(1, std::max(4, (int)std::thread::hardware_concurrency()) - 2);
#endif
}

static int &SkinQueueIntervalVar(int Dummy)
{
	return Dummy ? g_Config.m_QmDummySkinQueueInterval : g_Config.m_QmSkinQueueInterval;
}

static void LogSkinSettingsResourcePerf(const char *pJob, int Count, int Budget, int Remaining, ESettingsWarmupMissReason Reason, double DurationMs)
{
	LogSettingsResourcePerf(CMenus::SETTINGS_PLAYER, pJob, Count, Budget, Remaining, Reason, DurationMs);
	LogSettingsResourcePerf(CMenus::SETTINGS_TEE, pJob, Count, Budget, Remaining, Reason, DurationMs);
}

static void LogSettingsSkinSourceEvictEvent(const char *pSkinName, const char *pReason)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=evict skin=%s artifact=source reason=%s",
		pSkinName != nullptr ? pSkinName : "",
		pReason != nullptr ? pReason : "none");
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static void LogSettingsSkinSourceStageEvent(const char *pEvent, const char *pSkinName, int Width, int Height, int ByteCount, double DurationMs, int Uploads = -1)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	if(Uploads > 0)
	{
		str_format(aPayload, sizeof(aPayload), "event=%s skin=%s artifact=source width=%d height=%d bytes=%d dur_ms=%.3f uploads=%d",
			pEvent != nullptr ? pEvent : "source-stage",
			pSkinName != nullptr ? pSkinName : "",
			Width,
			Height,
			ByteCount,
			DurationMs,
			Uploads);
	}
	else
	{
		str_format(aPayload, sizeof(aPayload), "event=%s skin=%s artifact=source width=%d height=%d bytes=%d dur_ms=%.3f",
			pEvent != nullptr ? pEvent : "source-stage",
			pSkinName != nullptr ? pSkinName : "",
			Width,
			Height,
			ByteCount,
			DurationMs);
	}
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static const char *SettingsResourcePriorityName(ESettingsResourcePriority Priority)
{
	switch(Priority)
	{
	case ESettingsResourcePriority::BACKGROUND: return "background";
	case ESettingsResourcePriority::PREFETCH: return "prefetch";
	case ESettingsResourcePriority::VISIBLE: return "visible";
	}
	return "unknown";
}

static const char *SkinStateName(CSkins::CSkinContainer::EState State)
{
	switch(State)
	{
	case CSkins::CSkinContainer::EState::UNLOADED: return "unloaded";
	case CSkins::CSkinContainer::EState::BACKGROUND_REQUESTED: return "background_requested";
	case CSkins::CSkinContainer::EState::PENDING: return "pending";
	case CSkins::CSkinContainer::EState::LOADING: return "loading";
	case CSkins::CSkinContainer::EState::LOADED: return "loaded";
	case CSkins::CSkinContainer::EState::ERROR: return "error";
	case CSkins::CSkinContainer::EState::NOT_FOUND: return "not_found";
	}
	return "unknown";
}

static void LogSettingsSkinSourceRequestEvent(const char *pSkinName, ESettingsResourcePriority Priority, CSkins::CSkinContainer::EState State)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=source_request skin=%s priority=%s state=%s",
		pSkinName != nullptr ? pSkinName : "",
		SettingsResourcePriorityName(Priority),
		SkinStateName(State));
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static void LogSettingsSkinSourceWaitEvent(const char *pSkinName, const char *pReason, int RemainingUploads, int MaxUploads)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=source_wait skin=%s artifact=source reason=%s remaining_uploads=%d max_uploads=%d",
		pSkinName != nullptr ? pSkinName : "",
		pReason != nullptr ? pReason : "none",
		RemainingUploads,
		MaxUploads);
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static void LogSettingsSkinStartLoadingFallbackSweepEvent(int ItemsTotal, int ItemsScanned, int ItemsStarted, int ItemsSkipped, bool Invoked, double DurationMs, const char *pReason)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=skin_start_loading_fallback_sweep items_total=%d items_scanned=%d items_started=%d items_skipped=%d invoked=%d dur_ms=%.3f reason=%s",
		ItemsTotal,
		ItemsScanned,
		ItemsStarted,
		ItemsSkipped,
		Invoked ? 1 : 0,
		DurationMs,
		pReason != nullptr ? pReason : "none");
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static void LogSettingsSkinSourceWarmupEvent(const char *pEvent, const char *pExtra = nullptr)
{
	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;
	char aPayload[256];
	if(pExtra != nullptr && pExtra[0] != '\0')
		str_format(aPayload, sizeof(aPayload), "event=%s %s", pEvent != nullptr ? pEvent : "warmup", pExtra);
	else
		str_format(aPayload, sizeof(aPayload), "event=%s", pEvent != nullptr ? pEvent : "warmup");
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
}

static ESettingsWarmupMissReason SettingsResourceMissReason(ESettingsWarmupStopReason StopReason)
{
	switch(StopReason)
	{
	case ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET: return ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
	case ESettingsWarmupStopReason::MERGE_BUDGET: return ESettingsWarmupMissReason::JOB_RESULT_PENDING;
	case ESettingsWarmupStopReason::TEXT_BUDGET: return ESettingsWarmupMissReason::TEXT_BUDGET;
	case ESettingsWarmupStopReason::ACTIVE_ITEM: return ESettingsWarmupMissReason::ACTIVE_ITEM;
	case ESettingsWarmupStopReason::NONE: return ESettingsWarmupMissReason::NONE;
	}
	return ESettingsWarmupMissReason::NONE;
}

static SSettingsWarmupFrameBudget *SettingsFrameBudgetOrNull(CGameClient *pGameClient)
{
	if(pGameClient == nullptr || !pGameClient->m_Menus.IsSettingsPageActive())
		return nullptr;
	return pGameClient->m_Menus.SettingsFrameBudget();
}

static SSettingsResourceFrameContext SettingsFrameContextOrDefault(const CGameClient *pGameClient)
{
	if(pGameClient == nullptr || !pGameClient->m_Menus.IsSettingsPageActive())
		return {};
	const SSettingsResourceFrameContext PersistentContext = pGameClient->m_Menus.SettingsResourceFrameContext();
	CUi *pUi = const_cast<CGameClient *>(pGameClient)->Ui();
	const bool ImmediateScrollInput =
		pGameClient->Input()->KeyPress(KEY_MOUSE_WHEEL_UP) ||
		pGameClient->Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) ||
		pGameClient->Input()->KeyPress(KEY_MOUSE_WHEEL_LEFT) ||
		pGameClient->Input()->KeyPress(KEY_MOUSE_WHEEL_RIGHT) ||
		(pGameClient->Input()->KeyPress(KEY_MOUSE_1) && pUi->HotScrollRegion() != nullptr);
	return SettingsBuildFrameContext(PersistentContext.m_ScrollActive, ImmediateScrollInput, PersistentContext.m_PostScrollRecoveryFrames);
}

static bool ActiveSettingsTeePage(const CGameClient *pGameClient)
{
	return pGameClient != nullptr &&
	       pGameClient->m_Menus.IsSettingsPageActive() &&
	       g_Config.m_UiSettingsPage == CMenus::SETTINGS_TEE;
}

static void LogSettingsSkinFrameCapEvent(const CGameClient *pGameClient)
{
	static bool s_LastTeeSettingsActive = false;
	static int s_LastGpuCap = -1;
	static int s_LastFinalizeCap = -1;
	static int s_LastVisibleLoadCap = -1;
	static int s_LastNormalLoadCap = -1;

	if(g_Config.m_QmPerfDebug == 0 && g_Config.m_QmPerfLogfile == 0)
		return;

	const bool TeeSettingsActive = ActiveSettingsTeePage(pGameClient);
	if(!TeeSettingsActive)
	{
		s_LastTeeSettingsActive = false;
		s_LastGpuCap = -1;
		s_LastFinalizeCap = -1;
		s_LastVisibleLoadCap = -1;
		s_LastNormalLoadCap = -1;
		return;
	}

	const int GpuCap = pGameClient != nullptr ? pGameClient->GpuUploadLimiter()->MaxUploadsPerFrame() : -1;
	const int FinalizeCap = pGameClient != nullptr ? pGameClient->m_Skins.SettingsFinalizeBudgetForFrame() : -1;
	const int VisibleLoadCap = pGameClient != nullptr ? pGameClient->m_Skins.SettingsVisibleLoadingWindowForFrame() : -1;
	const int NormalLoadCap = pGameClient != nullptr ? pGameClient->m_Skins.SettingsNormalLoadingWindowForFrame() : -1;
	if(s_LastTeeSettingsActive && s_LastGpuCap == GpuCap && s_LastFinalizeCap == FinalizeCap &&
		s_LastVisibleLoadCap == VisibleLoadCap && s_LastNormalLoadCap == NormalLoadCap)
		return;

	s_LastTeeSettingsActive = true;
	s_LastGpuCap = GpuCap;
	s_LastFinalizeCap = FinalizeCap;
	s_LastVisibleLoadCap = VisibleLoadCap;
	s_LastNormalLoadCap = NormalLoadCap;
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=frame_cap gpu_cap=%d finalize_cap=%d loading_visible_cap=%d loading_other_cap=%d",
		GpuCap,
		FinalizeCap,
		VisibleLoadCap,
		NormalLoadCap);
	QmPerfLogPayload("perf/settings-skin-source", aPayload, pGameClient != nullptr ? pGameClient->Client() : nullptr, "settings:tee");
}

static int SettingsSkinMaxPerFrame(const CGameClient *pGameClient)
{
	if(ActiveSettingsTeePage(pGameClient))
		return pGameClient->m_Skins.SettingsFinalizeBudgetForFrame();
	return SettingsSkinFinalizeFrameBudget(SettingsFrameContextOrDefault(pGameClient), false);
}

[[maybe_unused]] static int SettingsSkinGpuUploadUnits(const CGameClient *pGameClient)
{
	if(ActiveSettingsTeePage(pGameClient))
		return pGameClient->m_Skins.SettingsGpuUploadFrameBudgetForFrame();
	return SettingsSkinGpuUploadFrameUnits(SettingsFrameContextOrDefault(pGameClient), false);
}

static constexpr int SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS = 24;
static constexpr int SKIN_QUEUE_HARD_LIMIT = 1024;
static constexpr int SKIN_QUEUE_PRESET_HARD_LIMIT = 256;

static int &SkinQueueLengthVar(int Dummy)
{
	return Dummy ? g_Config.m_QmDummySkinQueueLength : g_Config.m_QmSkinQueueLength;
}

static int &SkinQueueIndexVar(int Dummy)
{
	return Dummy ? g_Config.m_QmDummySkinQueueIndex : g_Config.m_QmSkinQueueIndex;
}

static int &SkinQueueRotateMapVar(int Dummy)
{
	return Dummy ? g_Config.m_QmDummySkinQueueRotateMap : g_Config.m_QmSkinQueueRotateMap;
}

static char *SkinNameVar(int Dummy)
{
	return Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
}

static size_t SkinNameVarSize(int Dummy)
{
	return Dummy ? sizeof(g_Config.m_ClDummySkin) : sizeof(g_Config.m_ClPlayerSkin);
}

static int &SkinUseCustomColorVar(int Dummy)
{
	return Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor;
}

static unsigned &SkinBodyColorVar(int Dummy)
{
	return Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody;
}

static unsigned &SkinFeetColorVar(int Dummy)
{
	return Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet;
}

static CSkins::CSkinQueueEntry MakeSkinQueueEntry(const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet)
{
	CSkins::CSkinQueueEntry Entry;
	Entry.m_SkinName = pSkinName;
	Entry.m_UseCustomColor = UseCustomColor;
	if(UseCustomColor)
	{
		Entry.m_ColorBody = ColorBody;
		Entry.m_ColorFeet = ColorFeet;
	}
	return Entry;
}

static CSkins::CSkinListEntry::SColorKey MakeSkinListColorKey(bool UseCustomColor, int ColorBody, int ColorFeet)
{
	CSkins::CSkinListEntry::SColorKey ColorKey;
	ColorKey.m_UseCustomColor = UseCustomColor;
	if(UseCustomColor)
	{
		ColorKey.m_ColorBody = ColorBody;
		ColorKey.m_ColorFeet = ColorFeet;
	}
	return ColorKey;
}

static CSkins::CSkinListEntry::SColorKey MakeSkinListColorKey(int Dummy)
{
	return MakeSkinListColorKey(SkinUseCustomColorVar(Dummy) != 0, SkinBodyColorVar(Dummy), SkinFeetColorVar(Dummy));
}

static SSettingsSkinListColorKey MakeSettingsSkinListColorKey(const CSkins::CSkinListEntry::SColorKey &ColorKey)
{
	SSettingsSkinListColorKey SettingsColorKey;
	SettingsColorKey.m_UseCustomColor = ColorKey.m_UseCustomColor;
	SettingsColorKey.m_ColorBody = ColorKey.m_ColorBody;
	SettingsColorKey.m_ColorFeet = ColorKey.m_ColorFeet;
	return SettingsColorKey;
}

static CSkins::CSkinListEntry::SColorKey MakeSkinListColorKey(const SSettingsSkinListColorKey &ColorKey)
{
	return MakeSkinListColorKey(ColorKey.m_UseCustomColor, ColorKey.m_ColorBody, ColorKey.m_ColorFeet);
}

static bool SkinListColorKeyEquals(const CSkins::CSkinListEntry::SColorKey &Lhs, const CSkins::CSkinListEntry::SColorKey &Rhs)
{
	if(Lhs.m_UseCustomColor != Rhs.m_UseCustomColor)
		return false;
	if(!Lhs.m_UseCustomColor)
		return true;
	return Lhs.m_ColorBody == Rhs.m_ColorBody && Lhs.m_ColorFeet == Rhs.m_ColorFeet;
}

static bool SkinListColorKeyLess(const CSkins::CSkinListEntry::SColorKey &Lhs, const CSkins::CSkinListEntry::SColorKey &Rhs)
{
	if(Lhs.m_UseCustomColor != Rhs.m_UseCustomColor)
		return !Lhs.m_UseCustomColor;
	if(!Lhs.m_UseCustomColor)
		return false;
	if(Lhs.m_ColorBody != Rhs.m_ColorBody)
		return Lhs.m_ColorBody < Rhs.m_ColorBody;
	return Lhs.m_ColorFeet < Rhs.m_ColorFeet;
}

static CSkins::CSkinQueueEntry MakeSkinQueueEntry(const CGameClient::CClientData &ClientData, int Conn)
{
	CSkins::CSkinQueueEntry Entry = MakeSkinQueueEntry(ClientData.m_aSkinName, ClientData.m_UseCustomColor != 0, ClientData.m_ColorBody, ClientData.m_ColorFeet);
	const CGameClient::CClientData::CSixup &SixupData = ClientData.m_aSixup[Conn];
	Entry.m_HasSixup = true;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		str_copy(Entry.m_aaSixupSkinPartNames[Part], SixupData.m_aaSkinPartNames[Part], sizeof(Entry.m_aaSixupSkinPartNames[Part]));
		Entry.m_aSixupUseCustomColors[Part] = SixupData.m_aUseCustomColors[Part];
		Entry.m_aSixupSkinPartColors[Part] = SixupData.m_aSkinPartColors[Part];
	}
	return Entry;
}

static bool SyncSkinQueueEntriesInPlace(std::vector<CSkins::CSkinQueueEntry> &Queue, const CSkins::CSkinQueueEntry *pDesiredEntries, size_t DesiredCount, bool RemoveExtraEntries = true)
{
	bool Changed = false;
	for(size_t DesiredIndex = 0; DesiredIndex < DesiredCount; ++DesiredIndex)
	{
		auto It = std::find(Queue.begin() + minimum(DesiredIndex, Queue.size()), Queue.end(), pDesiredEntries[DesiredIndex]);
		if(It == Queue.end())
		{
			Queue.insert(Queue.begin() + DesiredIndex, pDesiredEntries[DesiredIndex]);
			Changed = true;
		}
		else if((size_t)(It - Queue.begin()) != DesiredIndex)
		{
			std::rotate(Queue.begin() + DesiredIndex, It, It + 1);
			Changed = true;
		}
	}
	if(RemoveExtraEntries && Queue.size() > DesiredCount)
	{
		Queue.erase(Queue.begin() + DesiredCount, Queue.end());
		Changed = true;
	}
	return Changed;
}

CSkins::CAbstractSkinLoadJob::CAbstractSkinLoadJob(CSkins *pSkins, const char *pName) :
	m_pSkins(pSkins)
{
	str_copy(m_aName, pName);
	Abortable(true);
}

CSkins::CAbstractSkinLoadJob::~CAbstractSkinLoadJob()
{
	m_Data.m_Info.Free();
	m_Data.m_InfoGrayscale.Free();
}

CSkins::CSkinLoadJob::CSkinLoadJob(CSkins *pSkins, const char *pName, int StorageType) :
	CAbstractSkinLoadJob(pSkins, pName),
	m_StorageType(StorageType)
{
}

CSkins::CSkinListPlanJob::CSkinListPlanJob(std::vector<SSkinListSnapshotEntry> vEntries, std::string Filter, int Generation, int SortMode) :
	m_vEntries(std::move(vEntries)),
	m_Filter(std::move(Filter)),
	m_SortMode(SortMode)
{
	m_Result.m_Generation = Generation;
}

CSkins::CSkinDirectoryScanJob::CSkinDirectoryScanJob(IStorage *pStorage) :
	m_pStorage(pStorage)
{
}

int CSkins::CSkinDirectoryScanJob::ScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	auto *pSelf = static_cast<CSkinDirectoryScanJob *>(pUser);
	if(IsDir)
		return 0;

	const char *pName = pInfo->m_pName;
	const char *pSuffix = str_endswith(pName, ".png");
	if(pSuffix == nullptr)
		return 0;

	char aSkinName[IO_MAX_PATH_LENGTH];
	str_truncate(aSkinName, sizeof(aSkinName), pName, pSuffix - pName);
	if(!CSkin::IsValidName(aSkinName))
		return 0;

	pSelf->m_Result.m_vEntries.push_back({aSkinName, pSelf->m_CurrentScanType, StorageType, pInfo->m_TimeModified});
	return 0;
}

void CSkins::CSkinDirectoryScanJob::ScanDirectory(const char *pDirectory, CSkinContainer::EType Type)
{
	m_CurrentScanType = Type;
	m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, pDirectory, ScanCallback, this);
}

void CSkins::CSkinDirectoryScanJob::Run()
{
	ScanDirectory("skins", CSkinContainer::EType::LOCAL);
	ScanDirectory("downloadedskins", CSkinContainer::EType::DOWNLOAD);
}

void CSkins::CSkinListPlanJob::Run()
{
	std::vector<SSettingsSkinListEntry> vPlanEntries;
	vPlanEntries.reserve(m_vEntries.size());

	for(const SSkinListSnapshotEntry &Entry : m_vEntries)
	{
		if(Entry.m_Special)
			continue;

		if(Entry.m_NotFound && !Entry.m_SelectedMain && !Entry.m_SelectedDummy && !Entry.m_Favorite && !Entry.m_ForceShowNotFound)
			continue;

		++m_Result.m_UnfilteredCount;
		if(!m_Filter.empty())
		{
			const char *pNameMatchEnd = nullptr;
			if(str_utf8_find_nocase(Entry.m_Name.c_str(), m_Filter.c_str(), &pNameMatchEnd) == nullptr)
				continue;
		}

		SSettingsSkinListEntry PlanEntry;
		PlanEntry.m_Name = Entry.m_Name;
		PlanEntry.m_Selected = Entry.m_SelectedMain || Entry.m_SelectedDummy;
		PlanEntry.m_Favorite = Entry.m_Favorite;
		PlanEntry.m_ColorKey = Entry.m_ColorKey.has_value() ? std::make_optional(MakeSettingsSkinListColorKey(Entry.m_ColorKey.value())) : std::nullopt;
		PlanEntry.m_OfficialReleaseDate = Entry.m_OfficialReleaseDate;
		PlanEntry.m_LastModified = Entry.m_LastModified;
		vPlanEntries.push_back(std::move(PlanEntry));
	}

	m_Result.m_Filter = m_Filter;
	m_Result.m_Plan = BuildSettingsSkinListPlan(std::move(vPlanEntries), m_SortMode);
}

CSkins::CSkinContainer::CSkinContainer(CSkins *pSkins, const char *pName, EType Type, int StorageType) :
	m_pSkins(pSkins),
	m_Type(Type),
	m_StorageType(StorageType)
{
	str_copy(m_aName, pName);
	m_Vanilla = IsVanillaSkin(m_aName);
	m_Special = IsSpecialSkin(m_aName);
	m_AlwaysLoaded = m_Vanilla; // Vanilla skins are loaded immediately and not unloaded
}

CSkins::CSkinContainer::~CSkinContainer()
{
	dbg_assert(m_pLoadJob == nullptr, "Skin container load job was not cleared");
	m_SettingsPendingUploadData.m_Info.Free();
	m_SettingsPendingUploadData.m_InfoGrayscale.Free();
}

bool CSkins::CSkinContainer::operator<(const CSkinContainer &Other) const
{
	return str_comp(m_aName, Other.m_aName) < 0;
}

static constexpr std::chrono::nanoseconds MIN_REQUESTED_TIME_FOR_PENDING = 100ms;
static constexpr std::chrono::nanoseconds MAX_REQUESTED_TIME_FOR_PENDING = 220ms;
static constexpr std::chrono::nanoseconds MIN_UNLOAD_TIME_PENDING = 1s;
static constexpr std::chrono::nanoseconds MIN_UNLOAD_TIME_LOADED = 2s;
static_assert(MIN_REQUESTED_TIME_FOR_PENDING < MAX_REQUESTED_TIME_FOR_PENDING);
static_assert(MIN_REQUESTED_TIME_FOR_PENDING < MIN_UNLOAD_TIME_PENDING, "Unloading pending skins must take longer than adding more pending skins");

void CSkins::CSkinContainer::RequestLoad(bool Immediate)
{
	if(Immediate)
	{
		RequestLoad(ESettingsResourcePriority::VISIBLE);
		return;
	}

	if(m_AlwaysLoaded)
	{
		return;
	}

	// Delay non-priority requests a bit after the load has been requested to avoid loading a lot of skins
	// when quickly scrolling through lists or if a player with a new skin quickly joins and leaves.
	if(m_State == EState::UNLOADED)
	{
		const std::chrono::nanoseconds Now = time_get_nanoseconds();
		if(!m_FirstLoadRequest.has_value() ||
			!m_LastLoadRequest.has_value() ||
			Now - m_LastLoadRequest.value() > MAX_REQUESTED_TIME_FOR_PENDING)
		{
			m_FirstLoadRequest = Now;
			m_LastLoadRequest = m_FirstLoadRequest;
		}
		else if(Now - m_FirstLoadRequest.value() > MIN_REQUESTED_TIME_FOR_PENDING)
		{
			m_pSkins->ReclaimBackgroundSkinForPriorityRequest(Name(), g_Config.m_ClSkinsLoadedMax);
			SetState(EState::PENDING, ESettingsResourcePriority::PREFETCH);
		}
	}
	else if(m_State == EState::PENDING ||
		m_State == EState::LOADING ||
		m_State == EState::LOADED)
	{
		m_LastLoadRequest = time_get_nanoseconds();
		TouchUsage();
	}
}

void CSkins::CSkinContainer::RequestLoad(ESettingsResourcePriority Priority)
{
	if(m_AlwaysLoaded)
	{
		return;
	}
	const bool TeeSettingsActive = ActiveSettingsTeePage(m_pSkins->GameClient());

	if(Priority == ESettingsResourcePriority::BACKGROUND)
	{
		if(m_State == EState::UNLOADED)
			SetState(EState::BACKGROUND_REQUESTED, ESettingsResourcePriority::BACKGROUND);
		return;
	}

	if(Priority == ESettingsResourcePriority::PREFETCH)
	{
		m_pSkins->ReclaimBackgroundSkinForPriorityRequest(Name(), g_Config.m_ClSkinsLoadedMax);
	}

	// Delay loading skins a bit after the load has been requested to avoid loading a lot of skins
	// when quickly scrolling through lists or if a player with a new skin quickly joins and leaves.
	if(m_State == EState::UNLOADED || m_State == EState::BACKGROUND_REQUESTED)
	{
		if(TeeSettingsActive)
		{
			SetState(EState::BACKGROUND_REQUESTED, Priority);
			TouchUsage();
		}
		else if(Priority == ESettingsResourcePriority::VISIBLE || Priority == ESettingsResourcePriority::PREFETCH)
		{
			const ESettingsResourcePriority DirectPriority = Priority;
			SetState(EState::PENDING, DirectPriority);
		}
	}
	else if(m_State == EState::PENDING ||
		m_State == EState::LOADING ||
		m_State == EState::LOADED)
	{
		if(SettingsResourcePriorityCanUpgrade(Priority, m_LoadPriority))
			m_LoadPriority = Priority;
		m_LastLoadRequest = time_get_nanoseconds();
	}

	if(m_State == EState::PENDING ||
		m_State == EState::LOADING ||
		m_State == EState::LOADED)
	{
		TouchUsage();
	}
}

CSkins::CSkinContainer::EState CSkins::CSkinContainer::DetermineInitialState() const
{
	if(m_AlwaysLoaded)
	{
		// Load immediately if it should always be loaded
		return EState::PENDING;
	}
	else if((g_Config.m_ClVanillaSkinsOnly && !m_Vanilla) ||
		(m_Type == EType::DOWNLOAD && !g_Config.m_ClDownloadSkins))
	{
		// Fail immediately if it shouldn't be loaded
		return EState::NOT_FOUND;
	}
	else
	{
		return EState::UNLOADED;
	}
}

void CSkins::CSkinContainer::SetState(EState State, ESettingsResourcePriority Priority)
{
	const EState OldState = m_State;
	if(OldState != EState::LOADING && State == EState::LOADING)
		++m_pSkins->m_NumLoadingSkins;
	else if(OldState == EState::LOADING && State != EState::LOADING)
	{
		--m_pSkins->m_NumLoadingSkins;
		if(m_pSkins->m_pSkinPreviewUpload == this)
			m_pSkins->m_pSkinPreviewUpload = nullptr;
	}
	if(ShouldDiscardPendingUpload(OldState, State))
		m_pSkins->DiscardSkinPreviewUpload(this);
	m_State = State;
	m_pSkins->m_UnresolvedSkinScanState.OnStateChange(OldState, State);
	if(OldState != State)
		m_UnresolvedNotified = false;

	if(m_State == EState::BACKGROUND_REQUESTED ||
		m_State == EState::PENDING ||
		m_State == EState::LOADING ||
		m_State == EState::LOADED)
	{
		m_LoadPriority = Priority;
		const auto Now = time_get_nanoseconds();
		if(!m_FirstLoadRequest.has_value())
		{
			m_FirstLoadRequest = Now;
		}
		if(Priority != ESettingsResourcePriority::BACKGROUND || !m_LastLoadRequest.has_value())
			m_LastLoadRequest = Now;
		if(UsageTrackingUpdate(m_State, m_AlwaysLoaded, m_UsageEntryIterator.has_value(), Priority).m_ShouldTouch)
		{
			TouchUsage();
		}
		else if(Priority == ESettingsResourcePriority::BACKGROUND && !m_UsageEntryIterator.has_value())
		{
			TouchBackgroundUsage();
		}
	}
	else
	{
		m_FirstLoadRequest = std::nullopt;
		m_LastLoadRequest = std::nullopt;
		m_LoadPriority = ESettingsResourcePriority::BACKGROUND;
	}

	if(UsageTrackingUpdate(m_State, m_AlwaysLoaded, m_UsageEntryIterator.has_value(), Priority).m_ShouldErase)
	{
		ClearUsage();
	}
	if((m_State == EState::UNLOADED || m_State == EState::ERROR || m_State == EState::NOT_FOUND || Priority != ESettingsResourcePriority::BACKGROUND) && m_BackgroundEntryIterator.has_value())
		ClearBackgroundUsage();

	if(StateChangeRequiresListRefresh(OldState, m_State))
		m_pSkins->m_SkinList.ForceRefresh();
}

void CSkins::CSkinContainer::TouchUsage()
{
	ClearBackgroundUsage();
	ClearUsage();
	m_pSkins->m_SkinsUsageList.emplace_front(Name());
	m_UsageEntryIterator = m_pSkins->m_SkinsUsageList.begin();
}

void CSkins::CSkinContainer::ClearUsage()
{
	if(!m_UsageEntryIterator.has_value())
		return;
	m_pSkins->m_SkinsUsageList.erase(m_UsageEntryIterator.value());
	m_UsageEntryIterator = std::nullopt;
}

void CSkins::CSkinContainer::TouchBackgroundUsage()
{
	if(m_BackgroundEntryIterator.has_value())
		return;
	m_pSkins->m_SkinsBackgroundList.emplace_back(Name());
	m_BackgroundEntryIterator = std::prev(m_pSkins->m_SkinsBackgroundList.end());
}

void CSkins::CSkinContainer::ClearBackgroundUsage()
{
	if(!m_BackgroundEntryIterator.has_value())
		return;
	m_pSkins->m_SkinsBackgroundList.erase(m_BackgroundEntryIterator.value());
	m_BackgroundEntryIterator = std::nullopt;
}

bool CSkins::CSkinListEntry::operator<(const CSkins::CSkinListEntry &Other) const
{
	if(m_Favorite && !Other.m_Favorite)
	{
		return true;
	}
	if(!m_Favorite && Other.m_Favorite)
	{
		return false;
	}
	if(g_Config.m_QmSkinSortMode == 1)
	{
		const int OfficialReleaseDate = m_pSkinContainer->OfficialReleaseDate();
		const int OtherOfficialReleaseDate = Other.m_pSkinContainer->OfficialReleaseDate();
		if(OfficialReleaseDate != OtherOfficialReleaseDate)
		{
			return OfficialReleaseDate > OtherOfficialReleaseDate;
		}
		if(m_pSkinContainer->LastModified() != Other.m_pSkinContainer->LastModified())
		{
			return m_pSkinContainer->LastModified() > Other.m_pSkinContainer->LastModified();
		}
	}
	const int NameCompare = str_comp(m_pSkinContainer->Name(), Other.m_pSkinContainer->Name());
	if(NameCompare != 0)
	{
		return NameCompare < 0;
	}
	if(!m_ColorKey.has_value() && Other.m_ColorKey.has_value())
	{
		return true;
	}
	if(m_ColorKey.has_value() && !Other.m_ColorKey.has_value())
	{
		return false;
	}
	if(m_ColorKey.has_value() && Other.m_ColorKey.has_value())
	{
		return SkinListColorKeyLess(m_ColorKey.value(), Other.m_ColorKey.value());
	}
	return false;
}

void CSkins::CSkinListEntry::RequestLoad(bool Immediate)
{
	m_pSkinContainer->RequestLoad(Immediate);
}

void CSkins::CSkinListEntry::RequestLoad(ESettingsResourcePriority Priority)
{
	m_pSkinContainer->RequestLoad(Priority);
}

CSkins::CSkins() :
	m_PlaceholderSkin("dummy")
{
	std::fill(m_aAppliedSkinQueuePresetIndex.begin(), m_aAppliedSkinQueuePresetIndex.end(), -1);
	m_vSkinQueuePresets.push_back({"Default preset", {}, CSkinQueuePreset::EKind::USER});
	m_vSkinQueuePresets.push_back({"Server preset", {}, CSkinQueuePreset::EKind::SERVER});
	m_PlaceholderSkin.m_OriginalSkin.Reset();
	m_PlaceholderSkin.m_ColorableSkin.Reset();
	m_PlaceholderSkin.m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	m_PlaceholderSkin.m_Metrics.m_Body.m_Width = 64;
	m_PlaceholderSkin.m_Metrics.m_Body.m_Height = 64;
	m_PlaceholderSkin.m_Metrics.m_Body.m_OffsetX = 16;
	m_PlaceholderSkin.m_Metrics.m_Body.m_OffsetY = 16;
	m_PlaceholderSkin.m_Metrics.m_Body.m_MaxWidth = 96;
	m_PlaceholderSkin.m_Metrics.m_Body.m_MaxHeight = 96;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_Width = 32;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_Height = 16;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_OffsetX = 16;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_OffsetY = 8;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_MaxWidth = 64;
	m_PlaceholderSkin.m_Metrics.m_Feet.m_MaxHeight = 32;
}

bool CSkins::IsSpecialSkin(const char *pName)
{
	return str_utf8_comp_nocase_num(pName, "x_", 2) == 0;
}

bool CSkins::IsVanillaSkin(const char *pName)
{
	return std::any_of(std::begin(VANILLA_SKINS), std::end(VANILLA_SKINS), [pName](const char *pVanillaSkin) {
		return str_comp(pName, pVanillaSkin) == 0;
	});
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CSkinScanUser
{
public:
	CSkins *m_pThis;
	CSkins::TSkinLoadedCallback m_SkinLoadedCallback;
};

int CSkins::SkinScan(const char *pName, int IsDir, int StorageType, void *pUser)
{
	auto *pUserReal = static_cast<CSkinScanUser *>(pUser);
	CSkins *pSelf = pUserReal->m_pThis;

	if(IsDir)
	{
		return 0;
	}

	const char *pSuffix = str_endswith(pName, ".png");
	if(pSuffix == nullptr)
	{
		return 0;
	}

	char aSkinName[IO_MAX_PATH_LENGTH];
	str_truncate(aSkinName, sizeof(aSkinName), pName, pSuffix - pName);
	if(!CSkin::IsValidName(aSkinName))
	{
		log_error("skins", "Skin name is not valid: %s", aSkinName);
		log_error("skins", "%s", CSkin::m_aSkinNameRestrictions);
		return 0;
	}

	CSkinContainer SkinContainer(pSelf, aSkinName, CSkinContainer::EType::LOCAL, StorageType);
	auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s", pName);
	time_t Created = 0;
	time_t Modified = 0;
	if(pSelf->Storage()->RetrieveTimes(aPath, StorageType, &Created, &Modified))
		pSkinContainer->SetLastModified(Modified);
	pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
	pSelf->m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)});
	pUserReal->m_SkinLoadedCallback();
	return 0;
}

bool CSkins::PrepareSkinData(const char *pName, CSkinLoadData &Data)
{
	Data.m_pPreparedTextures.reset();
	const SSkinSpriteSpec Body{
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridx,
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridy,
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_X,
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_Y,
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_W,
		g_pData->m_aSprites[SPRITE_TEE_BODY].m_H};
	const SSkinSpriteSpec BodyOutline{
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_pSet->m_Gridx,
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_pSet->m_Gridy,
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_X,
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_Y,
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_W,
		g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_H};
	const SSkinSpriteSpec Feet{
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_pSet->m_Gridx,
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_pSet->m_Gridy,
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_X,
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_Y,
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_W,
		g_pData->m_aSprites[SPRITE_TEE_FOOT].m_H};
	const SSkinSpriteSpec FeetOutline{
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_pSet->m_Gridx,
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_pSet->m_Gridy,
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_X,
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_Y,
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_W,
		g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_H};
	SSkinDataPlan Plan;
	if(!BuildSkinDataPlan(Data.m_Info, Body, BodyOutline, Feet, FeetOutline, Plan))
	{
		log_error("skins", "Skin data is invalid (w=%" PRIzu ", h=%" PRIzu ", format=%s): %s", Data.m_Info.m_Width, Data.m_Info.m_Height, Data.m_Info.FormatName(), pName);
		Data.m_Info.Free();
		return false;
	}

	const size_t BodyWidth = Body.m_W * (Data.m_Info.m_Width / Body.m_GridX);
	const size_t BodyHeight = Body.m_H * (Data.m_Info.m_Height / Body.m_GridY);

	const size_t PixelStep = Data.m_Info.PixelSize();
	const size_t Pitch = Data.m_Info.m_Width * PixelStep;

	// dig out blood color
	{
		int64_t aColors[3] = {0};
		for(size_t y = 0; y < BodyHeight; y++)
		{
			for(size_t x = 0; x < BodyWidth; x++)
			{
				const size_t Offset = y * Pitch + x * PixelStep;
				if(Data.m_Info.m_pData[Offset + 3] > 128)
				{
					for(size_t c = 0; c < 3; c++)
					{
						aColors[c] += Data.m_Info.m_pData[Offset + c];
					}
				}
			}
		}
		const vec3 NormalizedColor = normalize(vec3(aColors[0], aColors[1], aColors[2]));
		Data.m_BloodColor = ColorRGBA(NormalizedColor.x, NormalizedColor.y, NormalizedColor.z);
	}

	Data.m_Metrics.Reset();
	Data.m_Metrics.m_Body.m_Width = Plan.m_Body.m_Width;
	Data.m_Metrics.m_Body.m_Height = Plan.m_Body.m_Height;
	Data.m_Metrics.m_Body.m_OffsetX = Plan.m_Body.m_OffsetX;
	Data.m_Metrics.m_Body.m_OffsetY = Plan.m_Body.m_OffsetY;
	Data.m_Metrics.m_Body.m_MaxWidth = Plan.m_Body.m_MaxWidth;
	Data.m_Metrics.m_Body.m_MaxHeight = Plan.m_Body.m_MaxHeight;
	Data.m_Metrics.m_Feet.m_Width = Plan.m_Feet.m_Width;
	Data.m_Metrics.m_Feet.m_Height = Plan.m_Feet.m_Height;
	Data.m_Metrics.m_Feet.m_OffsetX = Plan.m_Feet.m_OffsetX;
	Data.m_Metrics.m_Feet.m_OffsetY = Plan.m_Feet.m_OffsetY;
	Data.m_Metrics.m_Feet.m_MaxWidth = Plan.m_Feet.m_MaxWidth;
	Data.m_Metrics.m_Feet.m_MaxHeight = Plan.m_Feet.m_MaxHeight;

	Data.m_InfoGrayscale = Data.m_Info.DeepCopy();
	ConvertToGrayscale(Data.m_InfoGrayscale);

	int aFreq[256] = {0};
	uint8_t OrgWeight = 1;
	uint8_t NewWeight = 192;

	// find most common non-zero frequency
	for(size_t y = 0; y < BodyHeight; y++)
	{
		for(size_t x = 0; x < BodyWidth; x++)
		{
			const size_t Offset = y * Pitch + x * PixelStep;
			if(Data.m_InfoGrayscale.m_pData[Offset + 3] > 128)
			{
				aFreq[Data.m_InfoGrayscale.m_pData[Offset]]++;
			}
		}
	}

	for(int i = 1; i < 256; i++)
	{
		if(aFreq[OrgWeight] < aFreq[i])
		{
			OrgWeight = i;
		}
	}

	// reorder
	for(size_t y = 0; y < BodyHeight; y++)
	{
		for(size_t x = 0; x < BodyWidth; x++)
		{
			const size_t Offset = y * Pitch + x * PixelStep;
			uint8_t v = Data.m_InfoGrayscale.m_pData[Offset];
			if(v <= OrgWeight)
			{
				v = (uint8_t)((v / (float)OrgWeight) * NewWeight);
			}
			else
			{
				v = (uint8_t)(((v - OrgWeight) / (float)(255 - OrgWeight)) * (255 - NewWeight) + NewWeight);
			}
			Data.m_InfoGrayscale.m_pData[Offset] = v;
			Data.m_InfoGrayscale.m_pData[Offset + 1] = v;
			Data.m_InfoGrayscale.m_pData[Offset + 2] = v;
		}
	}

	// 在作业线程备好 CPU 侧素材（轮廓、聊天头像、逐精灵像素），
	// 主线程 LoadSkinFinish 只做纹理上传与共享指针接管，不再重复提取精灵。
	Data.m_PreparedVisuals = QmPrepareSkinVisuals(Data.m_Info, Data.m_InfoGrayscale, g_pData->m_aSprites);
	Data.m_pPreparedTextures = QmPrepareSkinTextures(Data.m_Info, Data.m_InfoGrayscale, g_pData->m_aSprites);
	Data.m_SourceWidth = Data.m_Info.m_Width;
	Data.m_SourceHeight = Data.m_Info.m_Height;
	// 缺失精灵仍需原图供 LoadSpriteTexture 执行本地空白素材回退。
	bool NeedsSourceFallback = false;
	for(size_t Variant = 0; Variant < 2; ++Variant)
		for(size_t Index = 0; Index < SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS / 2; ++Index)
			NeedsSourceFallback |= !Data.m_pPreparedTextures->Available(Variant, Index);
	if(!NeedsSourceFallback)
	{
		Data.m_Info.Free();
		Data.m_InfoGrayscale.Free();
	}

	return true;
}

void CSkins::LoadSkinFinish(CSkinContainer *pSkinContainer, CSkinLoadData &Data)
{
	const std::chrono::nanoseconds UploadStart = time_get_nanoseconds();
	CSkin Skin{pSkinContainer->Name()};

	// 12 张精灵已在作业线程按 SpriteId 的顺序提取好，这里只做上传。
	// 但自定义图集可能比默认布局小，越界精灵在作业侧就被记为「不可用」——
	// 那些精灵必须回落到 LoadSpriteTexture，才能保住 qm_blank_asset_fallback
	// 的空白素材回退（回退默认资源或保持不可见），否则整包皮肤会加载失败。
	const auto UploadVariant = [&](CSkin::CSkinTextures &Textures, const CImageInfo &Source, size_t Variant) {
		size_t Index = 0;
		const auto Upload = [&]() {
			const int SpriteId = CQmPreparedSkinTextures::SpriteId(Index);
			const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
			if(Data.m_pPreparedTextures != nullptr && Data.m_pPreparedTextures->Available(Variant, Index))
			{
				CImageInfo &Image = Data.m_pPreparedTextures->Image(Variant, Index++);
				return Graphics()->LoadTextureRawMove(Image, 0, Sprite.m_pName);
			}
			++Index;
			return Graphics()->LoadSpriteTexture(Source, std::nullopt, &g_pData->m_aSprites[SpriteId]);
		};
		Textures.m_Body = Upload();
		Textures.m_BodyOutline = Upload();
		Textures.m_Feet = Upload();
		Textures.m_FeetOutline = Upload();
		Textures.m_Hands = Upload();
		Textures.m_HandsOutline = Upload();
		for(auto &Eye : Textures.m_aEyes)
			Eye = Upload();
	};
	UploadVariant(Skin.m_OriginalSkin, Data.m_Info, 0);
	UploadVariant(Skin.m_ColorableSkin, Data.m_InfoGrayscale, 1);

	// 轮廓与聊天头像同样在作业线程备好，这里只接管共享指针。
	Data.m_PreparedVisuals.Apply(Skin);

	Skin.m_Metrics = Data.m_Metrics;
	Skin.m_BloodColor = Data.m_BloodColor;

	if(g_Config.m_Debug)
	{
		log_trace("skins", "Loaded skin '%s'", Skin.GetName());
	}

	auto SkinIt = m_Skins.find(pSkinContainer->Name());
	dbg_assert(SkinIt != m_Skins.end(), "LoadSkinFinish on skin '%s' which is not in m_Skins", pSkinContainer->Name());
	const bool BackgroundTracked = SkinIt->second->IsBackgroundTracked();
	SkinIt->second->m_SettingsSourceApproxBytes = SettingsSkinSourceBytesEstimate((int)Data.m_SourceWidth, (int)Data.m_SourceHeight, 2);
	SkinIt->second->m_pSkin = std::make_unique<CSkin>(std::move(Skin));
	pSkinContainer->SetState(CSkinContainer::EState::LOADED, BackgroundTracked ? ESettingsResourcePriority::BACKGROUND : ESettingsResourcePriority::VISIBLE);
	LogSettingsSkinSourceStageEvent("upload_done", pSkinContainer->Name(), Data.m_SourceWidth, Data.m_SourceHeight, (int)SkinIt->second->m_SettingsSourceApproxBytes, std::chrono::duration<double, std::milli>(time_get_nanoseconds() - UploadStart).count(), SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS);
}

bool CSkins::BeginSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadData &&Data)
{
	if(pSkinContainer == nullptr)
		return false;
	pSkinContainer->m_SettingsPendingUploadData = std::move(Data);
	pSkinContainer->m_SettingsPendingUploadSprite = 0;
	pSkinContainer->m_SettingsPendingUploadStart = time_get_nanoseconds();
	m_pSkinPreviewUpload = pSkinContainer;
	pSkinContainer->m_pPendingSkin = std::make_unique<CSkin>(pSkinContainer->Name());
	return true;
}

void CSkins::DiscardSkinPreviewUpload(CSkinContainer *pSkinContainer)
{
	if(pSkinContainer->m_pPendingSkin)
	{
		pSkinContainer->m_pPendingSkin->m_OriginalSkin.Unload(Graphics());
		pSkinContainer->m_pPendingSkin->m_ColorableSkin.Unload(Graphics());
		pSkinContainer->m_pPendingSkin.reset();
	}
	pSkinContainer->m_SettingsPendingUploadData.m_Info.Free();
	pSkinContainer->m_SettingsPendingUploadData.m_InfoGrayscale.Free();
	pSkinContainer->m_SettingsPendingUploadData = {};
	pSkinContainer->m_SettingsPendingUploadSprite = 0;
	pSkinContainer->m_SettingsPendingUploadStart = {};
}

static bool LoadSkinSpriteTexture(IGraphics *pGraphics, IGraphics::CTextureHandle *pTargetTexture, CImageInfo &PreparedImage, const CImageInfo &SourceImage, bool PreparedAvailable, const CDataSprite *pSprite)
{
	if(pGraphics == nullptr || pTargetTexture == nullptr)
		return false;
	IGraphics::CTextureHandle Texture = PreparedAvailable ?
						    pGraphics->LoadTextureRawMove(PreparedImage, 0, pSprite->m_pName) :
						    pGraphics->LoadSpriteTexture(SourceImage, std::nullopt, pSprite);
	if(!Texture.IsValid())
		return false;
	if(pTargetTexture->IsValid())
		pGraphics->UnloadTexture(pTargetTexture);
	*pTargetTexture = Texture;
	return true;
}

bool CSkins::UploadNextSkinPreviewSprite(CSkinContainer *pSkinContainer, SResourcePreviewUploadBudget &Budget)
{
	if(pSkinContainer == nullptr || pSkinContainer->m_pPendingSkin == nullptr)
		return false;
	(void)Budget;

	CSkinLoadData &Data = pSkinContainer->m_SettingsPendingUploadData;
	CSkin &Skin = *pSkinContainer->m_pPendingSkin;
	const auto Upload = [&](IGraphics::CTextureHandle &Target, size_t Variant, size_t Index) {
		const int SpriteId = CQmPreparedSkinTextures::SpriteId(Index);
		const CImageInfo &Source = Variant == 0 ? Data.m_Info : Data.m_InfoGrayscale;
		return LoadSkinSpriteTexture(Graphics(), &Target, Data.m_pPreparedTextures->Image(Variant, Index),
			Source, Data.m_pPreparedTextures->Available(Variant, Index), &g_pData->m_aSprites[SpriteId]);
	};
	switch(pSkinContainer->m_SettingsPendingUploadSprite)
	{
	case 0:
		if(Upload(Skin.m_OriginalSkin.m_Body, 0, 0))
			break;
		return false;
	case 1:
		if(Upload(Skin.m_OriginalSkin.m_BodyOutline, 0, 1))
			break;
		return false;
	case 2:
		if(Upload(Skin.m_OriginalSkin.m_Feet, 0, 2))
			break;
		return false;
	case 3:
		if(Upload(Skin.m_OriginalSkin.m_FeetOutline, 0, 3))
			break;
		return false;
	case 4:
		if(Upload(Skin.m_OriginalSkin.m_Hands, 0, 4))
			break;
		return false;
	case 5:
		if(Upload(Skin.m_OriginalSkin.m_HandsOutline, 0, 5))
			break;
		return false;
	case 6:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[0], 0, 6))
			break;
		return false;
	case 7:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[1], 0, 7))
			break;
		return false;
	case 8:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[2], 0, 8))
			break;
		return false;
	case 9:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[3], 0, 9))
			break;
		return false;
	case 10:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[4], 0, 10))
			break;
		return false;
	case 11:
		if(Upload(Skin.m_OriginalSkin.m_aEyes[5], 0, 11))
			break;
		return false;
	case 12:
		if(Upload(Skin.m_ColorableSkin.m_Body, 1, 0))
			break;
		return false;
	case 13:
		if(Upload(Skin.m_ColorableSkin.m_BodyOutline, 1, 1))
			break;
		return false;
	case 14:
		if(Upload(Skin.m_ColorableSkin.m_Feet, 1, 2))
			break;
		return false;
	case 15:
		if(Upload(Skin.m_ColorableSkin.m_FeetOutline, 1, 3))
			break;
		return false;
	case 16:
		if(Upload(Skin.m_ColorableSkin.m_Hands, 1, 4))
			break;
		return false;
	case 17:
		if(Upload(Skin.m_ColorableSkin.m_HandsOutline, 1, 5))
			break;
		return false;
	case 18:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[0], 1, 6))
			break;
		return false;
	case 19:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[1], 1, 7))
			break;
		return false;
	case 20:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[2], 1, 8))
			break;
		return false;
	case 21:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[3], 1, 9))
			break;
		return false;
	case 22:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[4], 1, 10))
			break;
		return false;
	case 23:
		if(Upload(Skin.m_ColorableSkin.m_aEyes[5], 1, 11))
			break;
		return false;
	default:
		return false;
	}
	++pSkinContainer->m_SettingsPendingUploadSprite;
	return true;
}

void CSkins::FinishSkinPreviewUpload(CSkinContainer *pSkinContainer)
{
	if(pSkinContainer == nullptr)
		return;
	auto SkinIt = m_Skins.find(pSkinContainer->Name());
	dbg_assert(SkinIt != m_Skins.end(), "FinishSkinPreviewUpload on skin '%s' which is not in m_Skins", pSkinContainer->Name());
	const bool BackgroundTracked = SkinIt->second->IsBackgroundTracked();
	pSkinContainer->m_pPendingSkin->m_Metrics = pSkinContainer->m_SettingsPendingUploadData.m_Metrics;
	pSkinContainer->m_SettingsPendingUploadData.m_PreparedVisuals.Apply(*pSkinContainer->m_pPendingSkin);
	pSkinContainer->m_pPendingSkin->m_BloodColor = pSkinContainer->m_SettingsPendingUploadData.m_BloodColor;
	SkinIt->second->m_SettingsSourceApproxBytes = SettingsSkinSourceBytesEstimate((int)pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth, (int)pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, 2);
	if(pSkinContainer->m_pSkin)
	{
		pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());
		pSkinContainer->m_pSkin->m_ColorableSkin.Unload(Graphics());
	}
	pSkinContainer->m_pSkin = std::move(pSkinContainer->m_pPendingSkin);
	pSkinContainer->SetState(CSkinContainer::EState::LOADED, BackgroundTracked ? ESettingsResourcePriority::BACKGROUND : ESettingsResourcePriority::VISIBLE);
	LogSettingsSkinSourceStageEvent("upload_done", pSkinContainer->Name(), pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth, pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, (int)SkinIt->second->m_SettingsSourceApproxBytes, std::chrono::duration<double, std::milli>(time_get_nanoseconds() - pSkinContainer->m_SettingsPendingUploadStart).count(), SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS);
	pSkinContainer->m_SettingsPendingUploadData.m_Info.Free();
	pSkinContainer->m_SettingsPendingUploadData.m_InfoGrayscale.Free();
	pSkinContainer->m_SettingsPendingUploadData.m_pPreparedTextures.reset();
	pSkinContainer->m_SettingsPendingUploadSprite = 0;
	++m_SettingsSourceUploadsCompleted;
}

void CSkins::LoadSkinDirect(const char *pName)
{
	if(m_Skins.contains(pName))
	{
		return;
	}
	CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::LOCAL, IStorage::TYPE_ALL);
	auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
	pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
	const auto &[SkinIt, _] = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)});

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", pName);
	CSkinLoadData DefaultSkinData;
	SkinIt->second->SetState(CSkinContainer::EState::LOADING);
	if(!Graphics()->LoadPng(DefaultSkinData.m_Info, aPath, SkinIt->second->StorageType()))
	{
		log_error("skins", "Failed to load PNG of skin '%s' from '%s'", pName, aPath);
		SkinIt->second->SetState(CSkinContainer::EState::ERROR);
	}
	else if(PrepareSkinData(pName, DefaultSkinData))
	{
		LoadSkinFinish(SkinIt->second.get(), DefaultSkinData);
	}
	else
	{
		SkinIt->second->SetState(CSkinContainer::EState::ERROR);
	}
	DefaultSkinData.m_Info.Free();
	DefaultSkinData.m_InfoGrayscale.Free();
}

void CSkins::OnConsoleInit()
{
	ConfigManager()->RegisterCallback(CSkins::ConfigSaveCallback, this);
	ConfigManager()->RegisterCallback(CSkins::ConfigSaveQueueCallback, this, ConfigDomain::QMCLIENT);
	Console()->Register("add_favorite_skin", "s[skin_name]", CFGFLAG_CLIENT, ConAddFavoriteSkin, this, "Add a skin as a favorite");
	Console()->Register("remove_favorite_skin", "s[skin_name]", CFGFLAG_CLIENT, ConRemFavoriteSkin, this, "Remove a skin from the favorites");
	Console()->Register("add_skin_queue", "s[skin_name]", CFGFLAG_CLIENT, ConAddSkinQueue, this, "Add a skin to the queue");
	Console()->Register("add_dummy_skin_queue", "s[skin_name]", CFGFLAG_CLIENT, ConAddDummySkinQueue, this, "Add a skin to the dummy queue");
	Console()->Register("add_skin_queue_ex", "s[skin_name] i[use_custom_color] i[color_body] i[color_feet]", CFGFLAG_CLIENT, ConAddSkinQueueEx, this, "Add a colored skin to the queue");
	Console()->Register("add_dummy_skin_queue_ex", "s[skin_name] i[use_custom_color] i[color_body] i[color_feet]", CFGFLAG_CLIENT, ConAddDummySkinQueueEx, this, "Add a colored skin to the dummy queue");
	Console()->Register("add_skin_queue_preset", "s[preset_name]", CFGFLAG_CLIENT, ConAddSkinQueuePreset, this, "Add a queue preset");
	Console()->Register("add_dummy_skin_queue_preset", "s[preset_name]", CFGFLAG_CLIENT, ConAddDummySkinQueuePreset, this, "Add a dummy queue preset");
	Console()->Register("add_skin_queue_preset_item", "i[preset_index] s[skin_name]", CFGFLAG_CLIENT, ConAddSkinQueuePresetItem, this, "Add a skin to a queue preset");
	Console()->Register("add_dummy_skin_queue_preset_item", "i[preset_index] s[skin_name]", CFGFLAG_CLIENT, ConAddDummySkinQueuePresetItem, this, "Add a skin to a dummy queue preset");
	Console()->Register("add_skin_queue_preset_item_ex", "i[preset_index] s[skin_name] i[use_custom_color] i[color_body] i[color_feet]", CFGFLAG_CLIENT, ConAddSkinQueuePresetItemEx, this, "Add a colored skin to a queue preset");
	Console()->Register("add_dummy_skin_queue_preset_item_ex", "i[preset_index] s[skin_name] i[use_custom_color] i[color_body] i[color_feet]", CFGFLAG_CLIENT, ConAddDummySkinQueuePresetItemEx, this, "Add a colored skin to a dummy queue preset");
	Console()->Register("random_skin_queue", "", CFGFLAG_CLIENT, ConRandomSkinQueue, this, "Apply a random skin from the queue");
	Console()->Register("random_dummy_skin_queue", "", CFGFLAG_CLIENT, ConRandomDummySkinQueue, this, "Apply a random skin from the dummy queue");

	Console()->Chain("player_skin", ConchainRefreshSkinList, this);
	Console()->Chain("dummy_skin", ConchainRefreshSkinList, this);
}

void CSkins::OnInit()
{
	RefreshEventSkins();

	// load skins
	Refresh([]() {});
}

void CSkins::OnShutdown()
{
	if(m_pSkinDirectoryScanJob)
	{
		m_pSkinDirectoryScanJob->Abort();
		m_pSkinDirectoryScanJob = nullptr;
	}
	if(m_pSkinListPlanJob)
	{
		m_pSkinListPlanJob->Abort();
		m_pSkinListPlanJob = nullptr;
	}
	if(m_pOfficialSkinIndexRequest)
	{
		m_pOfficialSkinIndexRequest->Abort();
		m_pOfficialSkinIndexRequest = nullptr;
	}
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if(pSkinContainer->m_pLoadJob)
		{
			pSkinContainer->m_pLoadJob->Abort();
			pSkinContainer->m_pLoadJob = nullptr;
		}
		if(pSkinContainer->m_pPendingSkin)
			DiscardSkinPreviewUpload(pSkinContainer.get());
	}
	m_pSkinPreviewUpload = nullptr;
	m_NumLoadingSkins = 0;
	m_Skins.clear();
	m_UnresolvedSkinScanState = {};
	m_vSkinsUnresolvedThisFrame.clear();
	m_vSkinsTexturesUnloadedThisFrame.clear();
}

void CSkins::OnUpdate()
{
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		UpdateSkinQueue(Now, Dummy);
	}

	// 续传不受容器扫描间隔限制，也不每帧扫描全部皮肤。
	if(m_pSkinPreviewUpload != nullptr)
	{
		CSkinLoadingStats UploadStats;
		UploadStats.m_NumLoading = m_NumLoadingSkins;
		int Processed = 0;
		DrainSettingsSkinPreviewUpload(m_pSkinPreviewUpload, UploadStats, Processed, Now, 1ms);
	}

	// Only update skins periodically to reduce FPS impact
	const std::chrono::nanoseconds MaxTime = std::chrono::milliseconds(std::clamp(round_to_int(Client()->RenderFrameTime() * 50000.0f), 25, 500));
	if(m_ContainerUpdateTime.has_value() && Now - m_ContainerUpdateTime.value() < MaxTime)
	{
		return;
	}
	m_ContainerUpdateTime = Now;

	// Update loaded state of managed skins which are not retrieved with the FindOrNullptr function
	GameClient()->CollectManagedTeeRenderInfos([&](const char *pSkinName) {
		// This will update the loaded state of the container
		dbg_assert(FindContainerOrNullptr(pSkinName) != nullptr, "No skin container found for managed tee render info: %s", pSkinName);
	});
	// Keep player and dummy skin loaded
	FindContainerOrNullptr(g_Config.m_ClPlayerSkin);
	FindContainerOrNullptr(g_Config.m_ClDummySkin);

	CSkinLoadingStats Stats = LoadingStats();
	ProcessOfficialSkinIndexRequest();
	ProcessSkinDirectoryScanJob();
	UpdateUnloadSkins(Stats);
	UpdateStartLoading(Stats);
	// 检查间隔不是主线程预算：终局上传使用独立的 1 ms 预算，首个皮肤始终取得进展。
	UpdateFinishLoading(Stats, Now, std::chrono::milliseconds(1));
	ProcessSkinListPlanJob();
	CollectUnresolvedSkins();
	for(const std::string &SkinName : m_vSkinsUnresolvedThisFrame)
		GameClient()->OnSkinUpdate(SkinName.c_str());
	m_vSkinsUnresolvedThisFrame.clear();
	for(const std::string &SkinName : m_vSkinsTexturesUnloadedThisFrame)
		GameClient()->OnSkinUpdate(SkinName.c_str());
	m_vSkinsTexturesUnloadedThisFrame.clear();
}

void CSkins::OnRender()
{
	m_SkinUploadFrameBudget.Reset();
}

void CSkins::UpdateForSettingsWarmup()
{
	// Startup warmup pumps skin jobs in a tight loop; bypass the normal frame-rate
	// throttle so the blocking preload actually advances before the menu appears.
	m_ContainerUpdateTime.reset();
	m_SkinUploadFrameBudget.Reset();
	OnUpdate();
}

size_t CSkins::LoadedSkinLimit() const
{
	return (size_t)g_Config.m_ClSkinsLoadedMax;
}

void CSkins::PrepareSettingsThroughputForFrame()
{
	const bool TeeSettingsActive = ActiveSettingsTeePage(GameClient());
	const SSettingsResourceFrameContext FrameContext = SettingsFrameContextOrDefault(GameClient());
	if(!TeeSettingsActive)
	{
		m_SettingsThroughputControllerState = {};
		m_SettingsThroughputControllerOutput = {};
		m_SettingsTeeVisibleSnapshot = {};
		return;
	}

	CSkinLoadingStats Stats;
	int LoadingJobsAwaitingResult = 0;
	int LoadingJobsReadyForMainThread = 0;
	for(const auto &[_, pSkinContainer] : m_Skins)
	{
		Stats.AddState(pSkinContainer->m_State);
		if(pSkinContainer->m_State != CSkinContainer::EState::LOADING || pSkinContainer->m_pLoadJob == nullptr)
			continue;
		if(!pSkinContainer->m_pLoadJob->Done())
			++LoadingJobsAwaitingResult;
		else
			++LoadingJobsReadyForMainThread;
	}

	int UploadsDoneDelta = 0;
	int LoadedDelta = 0;
	if(m_SettingsThroughputControllerState.m_Initialized)
	{
		UploadsDoneDelta = (int)(m_SettingsSourceUploadsCompleted - m_SettingsSourceUploadsAtLastControllerFrame);
		LoadedDelta = (int)(m_SettingsSourceLoadsCompleted - m_SettingsSourceLoadsAtLastControllerFrame);
	}
	m_SettingsSourceUploadsAtLastControllerFrame = m_SettingsSourceUploadsCompleted;
	m_SettingsSourceLoadsAtLastControllerFrame = m_SettingsSourceLoadsCompleted;

	const int CurrentNormalWindow = m_SettingsThroughputControllerState.m_Initialized && m_SettingsThroughputControllerOutput.m_NormalLoadingWindow > 0 ?
						m_SettingsThroughputControllerOutput.m_NormalLoadingWindow :
						SettingsSkinSourceLoadNormalWindow(FrameContext, true, g_Config.m_ClSkinsLoadedMax);
	const bool DecodeJobsSaturated =
		LoadedDelta <= 0 &&
		LoadingJobsReadyForMainThread == 0 &&
		LoadingJobsAwaitingResult >= minimum(maximum(1, CurrentNormalWindow), SettingsSkinDecodeJobWorkerBudget());

	m_SettingsThroughputControllerOutput = SettingsSkinThroughputControllerStep({
											    FrameContext,
											    true,
											    Client()->FrameTimeAverage() * 1000.0f,
											    Client()->RenderFrameTime() * 1000.0f,
											    g_Config.m_ClSkinsLoadedMax,
											    m_SettingsTeeVisibleSnapshot.m_VisibleTotal,
											    m_SettingsTeeVisibleSnapshot.m_VisibleReady,
											    m_SettingsTeeVisibleSnapshot.m_VisibleWaiting,
											    m_SettingsTeeVisibleSnapshot.m_VisibleBackgroundRequested,
											    m_SettingsTeeVisibleSnapshot.m_VisibleNonterminalWaiting,
											    (int)Stats.m_NumBackgroundRequested,
											    (int)Stats.m_NumPending,
											    (int)Stats.m_NumLoading,
											    (int)Stats.m_NumLoaded,
											    (int)Stats.RealInflight(),
											    UploadsDoneDelta,
											    LoadedDelta,
											    m_SettingsSourceAdmissionTelemetry.m_AdmittedDelta,
											    m_SettingsSourceAdmissionTelemetry.m_StartedDelta,
											    GameClient()->GpuUploadLimiter()->RemainingUploads(),
											    DecodeJobsSaturated,
											    m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason,
											    m_SettingsTeeVisibleSnapshot.m_aRequestBudgetBlockReason,
										    },
		m_SettingsThroughputControllerState);
}

void CSkins::ClampSkinQueueIndex(int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	int &QueueIndex = SkinQueueIndexVar(Dummy);
	if(Queue.empty())
	{
		QueueIndex = 0;
		return;
	}
	if(QueueIndex < 0 || QueueIndex >= (int)Queue.size())
	{
		QueueIndex = 0;
	}
}

void CSkins::ApplySkinQueueCurrent(int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	if(Queue.empty())
	{
		return;
	}

	ClampSkinQueueIndex(Dummy);
	const CSkinQueueEntry &TargetEntry = Queue[SkinQueueIndexVar(Dummy)];
	char *pSkinName = SkinNameVar(Dummy);
	int &UseCustomColor = SkinUseCustomColorVar(Dummy);
	unsigned &ColorBody = SkinBodyColorVar(Dummy);
	unsigned &ColorFeet = SkinFeetColorVar(Dummy);

	bool Changed = false;
	if(str_comp(pSkinName, TargetEntry.m_SkinName.c_str()) != 0)
	{
		str_copy(pSkinName, TargetEntry.m_SkinName.c_str(), SkinNameVarSize(Dummy));
		Changed = true;
	}
	if(UseCustomColor != (int)TargetEntry.m_UseCustomColor)
	{
		UseCustomColor = TargetEntry.m_UseCustomColor ? 1 : 0;
		Changed = true;
	}
	if(TargetEntry.m_UseCustomColor)
	{
		if(ColorBody != (unsigned)TargetEntry.m_ColorBody)
		{
			ColorBody = TargetEntry.m_ColorBody;
			Changed = true;
		}
		if(ColorFeet != (unsigned)TargetEntry.m_ColorFeet)
		{
			ColorFeet = TargetEntry.m_ColorFeet;
			Changed = true;
		}
	}

	if(Client()->IsSixup() && TargetEntry.m_HasSixup)
	{
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
		{
			if(str_comp(CSkins7::ms_apSkinVariables[Dummy][Part], TargetEntry.m_aaSixupSkinPartNames[Part]) != 0)
			{
				str_copy(CSkins7::ms_apSkinVariables[Dummy][Part], TargetEntry.m_aaSixupSkinPartNames[Part], protocol7::MAX_SKIN_ARRAY_SIZE);
				Changed = true;
			}
			if(*CSkins7::ms_apUCCVariables[Dummy][Part] != TargetEntry.m_aSixupUseCustomColors[Part])
			{
				*CSkins7::ms_apUCCVariables[Dummy][Part] = TargetEntry.m_aSixupUseCustomColors[Part];
				Changed = true;
			}
			if((int)*CSkins7::ms_apColorVariables[Dummy][Part] != TargetEntry.m_aSixupSkinPartColors[Part])
			{
				*CSkins7::ms_apColorVariables[Dummy][Part] = TargetEntry.m_aSixupSkinPartColors[Part];
				Changed = true;
			}
		}
		CSkins7::ms_apSkinNameVariables[Dummy][0] = '\0';
	}

	if(Changed)
	{
		m_SkinList.ForceRefresh();
		if(Dummy == 0)
		{
			if(Client()->State() == IClient::STATE_ONLINE)
			{
				GameClient()->SendInfo(false);
			}
		}
		else if(Client()->DummyConnected())
		{
			GameClient()->SendDummyInfo(false);
		}
	}
}

void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)
{
	SyncSkinQueueFromMapPlayers(Dummy);
	auto &Queue = m_aSkinQueue[Dummy];
	const int QueueInterval = SkinQueueIntervalVar(Dummy);
	const int QueueActiveCount = (int)Queue.size();
	if(!SkinQueueEnabledVar(Dummy) || Queue.empty() || QueueActiveCount <= 0)
	{
		m_aSkinQueueLastUpdate[Dummy].reset();
		m_aSkinQueueElapsed[Dummy] = 0ns;
		return;
	}

	const bool Online = Dummy == 0 ? Client()->State() == IClient::STATE_ONLINE : Client()->DummyConnected();
	if(!Online)
	{
		m_aSkinQueueLastUpdate[Dummy].reset();
		return;
	}

	if(!m_aSkinQueueLastUpdate[Dummy].has_value())
	{
		m_aSkinQueueLastUpdate[Dummy] = Now;
		// 轮换（重新）启动时按配置从随机位置开始：进图上线、分身单独连接、重新启用队列都会走到这里。
		const int RandomJoin = Dummy ? g_Config.m_QmDummySkinQueueRandomJoin : g_Config.m_QmSkinQueueRandomJoin;
		if(RandomJoin)
		{
			SkinQueueIndexVar(Dummy) = rand() % QueueActiveCount;
			m_aSkinQueueElapsed[Dummy] = 0ns;
		}
		ApplySkinQueueCurrent(Dummy);
		return;
	}

	m_aSkinQueueElapsed[Dummy] += Now - m_aSkinQueueLastUpdate[Dummy].value_or(Now);
	m_aSkinQueueLastUpdate[Dummy] = Now;

	const auto Interval = std::chrono::milliseconds(QueueInterval);
	if(Interval <= 0ns)
	{
		return;
	}

	int &QueueIndex = SkinQueueIndexVar(Dummy);
	if(QueueIndex >= QueueActiveCount)
	{
		QueueIndex = 0;
	}
	const int64_t StepsElapsed = m_aSkinQueueElapsed[Dummy] / Interval;
	if(StepsElapsed <= 0)
	{
		return;
	}
	m_aSkinQueueElapsed[Dummy] -= Interval * StepsElapsed;
	QueueIndex = (QueueIndex + (int)(StepsElapsed % QueueActiveCount)) % QueueActiveCount;
	ApplySkinQueueCurrent(Dummy);
}

void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)
{
	if(!SkinQueueRotateMapVar(Dummy))
	{
		return;
	}

	const bool Online = Dummy == 0 ? Client()->State() == IClient::STATE_ONLINE : Client()->DummyConnected();
	if(!Online)
	{
		return;
	}

	std::array<CSkinQueueEntry, MAX_CLIENTS> aMapSkins;
	size_t DesiredCount = 0;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(!pPlayerInfo || !GameClient()->m_aClients[ClientId].m_Active || pPlayerInfo->m_Team == TEAM_SPECTATORS || GameClient()->IsLocalClientId(ClientId))
		{
			continue;
		}

		const auto &ClientData = GameClient()->m_aClients[ClientId];
		const char *pSkinName = ClientData.m_aSkinName;
		if(!CSkin::IsValidName(pSkinName))
		{
			continue;
		}

		const CSkinQueueEntry Entry = Client()->IsSixup() ? MakeSkinQueueEntry(ClientData, g_Config.m_ClDummy) : MakeSkinQueueEntry(pSkinName, ClientData.m_UseCustomColor != 0, ClientData.m_ColorBody, ClientData.m_ColorFeet);
		if(std::find(aMapSkins.begin(), aMapSkins.begin() + DesiredCount, Entry) != aMapSkins.begin() + DesiredCount)
		{
			continue;
		}

		aMapSkins[DesiredCount++] = Entry;
	}

	auto &Queue = m_aSkinQueue[Dummy];
	// Sync only runs in server-rotation mode (caller opens RotateMap then calls
	// this), so the playing queue is always attributed to the Server preset.
	if(Queue.size() == DesiredCount && std::equal(Queue.begin(), Queue.end(), aMapSkins.begin()))
	{
		// Map-player skins belong to the Server preset (index 1), not the
		// Default preset (index 0), so the Default preset stays user-owned.
		// Only re-assign the Server template when it actually differs: this is a
		// per-frame, per-dummy hot path and the steady state (no player-skin
		// change) hits this branch every frame.
		auto &ServerTemplate = m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue;
		if(ServerTemplate.size() != DesiredCount || !std::equal(ServerTemplate.begin(), ServerTemplate.end(), aMapSkins.begin()))
		{
			m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);
		}
		m_aAppliedSkinQueuePresetIndex[Dummy] = (int)SKIN_QUEUE_SERVER_PRESET;
		return;
	}

	std::optional<CSkinQueueEntry> CurrentSkin;
	if(!Queue.empty())
	{
		ClampSkinQueueIndex(Dummy);
		CurrentSkin = Queue[SkinQueueIndexVar(Dummy)];
	}

	const bool QueueChanged = Queue.size() != DesiredCount || !std::equal(Queue.begin(), Queue.end(), aMapSkins.begin());
	Queue.assign(aMapSkins.begin(), aMapSkins.begin() + DesiredCount);
	m_vSkinQueuePresets[SKIN_QUEUE_SERVER_PRESET].m_Queue = Queue;
	if(QueueChanged)
	{
		m_SkinList.ForceRefresh();
	}

	int &QueueIndex = SkinQueueIndexVar(Dummy);
	if(Queue.empty())
	{
		QueueIndex = 0;
	}
	else if(CurrentSkin.has_value())
	{
		const auto It = std::find(Queue.begin(), Queue.end(), CurrentSkin.value());
		QueueIndex = It != Queue.end() ? (int)(It - Queue.begin()) : 0;
	}
	ClampSkinQueueIndex(Dummy);
	m_aAppliedSkinQueuePresetIndex[Dummy] = (int)SKIN_QUEUE_SERVER_PRESET;

	m_aSkinQueueElapsed[Dummy] = 0ns;
	m_aSkinQueueLastUpdate[Dummy].reset();
	if(SkinQueueEnabledVar(Dummy))
	{
		ApplySkinQueueCurrent(Dummy);
	}
}

void CSkins::QueueSkinTexturesUnloaded(const char *pSkinName)
{
	if(pSkinName != nullptr && pSkinName[0] != '\0' &&
		std::find(m_vSkinsTexturesUnloadedThisFrame.begin(), m_vSkinsTexturesUnloadedThisFrame.end(), pSkinName) == m_vSkinsTexturesUnloadedThisFrame.end())
		m_vSkinsTexturesUnloadedThisFrame.emplace_back(pSkinName);
}

void CSkins::UnloadLoadedSkinTextures(CSkinContainer *pSkinContainer)
{
	if(pSkinContainer == nullptr || pSkinContainer->m_pSkin == nullptr)
		return;
	pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());
	pSkinContainer->m_pSkin->m_ColorableSkin.Unload(Graphics());
	pSkinContainer->m_pSkin.reset();
	pSkinContainer->m_SettingsSourceApproxBytes = 0;
	QueueSkinTexturesUnloaded(pSkinContainer->Name());
}

void CSkins::UpdateUnloadSkins(CSkinLoadingStats &Stats)
{
	size_t SourceBytesInUse = 0;
	for(const auto &[_, pSkinContainer] : m_Skins)
	{
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADED)
			SourceBytesInUse += pSkinContainer->SettingsSourceApproxBytes();
	}
	const size_t SourceBytesBudget = SettingsSkinSourceBytesEstimate(256, 128, 2) * (size_t)maximum(0, g_Config.m_ClSkinsLoadedMax);
	const bool TeeSettingsActive = ActiveSettingsTeePage(GameClient());
	const SSettingsResourceFrameContext FrameContext = SettingsFrameContextOrDefault(GameClient());
	const int CountFuseLimit = SettingsSkinSourceCountFuseLimit(FrameContext, TeeSettingsActive, g_Config.m_ClSkinsLoadedMax);
	const bool CountFuseExceeded = CountFuseLimit > 0 && Stats.m_NumPending + Stats.m_NumLoading > (size_t)CountFuseLimit;
	const bool BytesBudgetExceeded = SourceBytesBudget > 0 && SourceBytesInUse > SourceBytesBudget;
	const bool ReclaimLoadedSources = BytesBudgetExceeded;
	if(!SettingsSkinResidencyShouldReclaim(BytesBudgetExceeded, CountFuseExceeded))
	{
		return;
	}

	const std::chrono::nanoseconds UnloadStart = time_get_nanoseconds();
	size_t NumToUnload = CountFuseExceeded ?
				     std::min<size_t>(Stats.m_NumPending + Stats.m_NumLoading - (size_t)CountFuseLimit, 16) :
				     16;
	auto TryUnloadContainer = [&](CSkinContainer *pSkinContainer) {
		if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&
			pSkinContainer->m_State != CSkinContainer::EState::LOADED)
		{
			return false;
		}
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADED && !ReclaimLoadedSources)
		{
			return false;
		}
		const std::chrono::nanoseconds TimeUnused = UnloadStart - pSkinContainer->m_LastLoadRequest.value();
		if(TimeUnused < (pSkinContainer->m_State == CSkinContainer::EState::LOADED ? MIN_UNLOAD_TIME_LOADED : MIN_UNLOAD_TIME_PENDING))
		{
			return false;
		}
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADED)
		{
			LogSettingsSkinSourceEvictEvent(pSkinContainer->Name(), BytesBudgetExceeded ? "bytes_budget" : "queue_count");
			UnloadLoadedSkinTextures(pSkinContainer);
			Stats.m_NumLoaded--;
		}
		else
		{
			LogSettingsSkinSourceEvictEvent(pSkinContainer->Name(), "queue_count");
			Stats.m_NumPending--;
		}
		Stats.m_NumUnloaded++;
		pSkinContainer->SetState(CSkinContainer::EState::UNLOADED);
		return true;
	};
	std::vector<std::string> vBackgroundSnapshot;
	vBackgroundSnapshot.reserve(m_SkinsBackgroundList.size());
	for(const std::string &SkinName : m_SkinsBackgroundList)
	{
		vBackgroundSnapshot.push_back(SkinName);
	}
	for(const std::string &SkinName : vBackgroundSnapshot)
	{
		if(NumToUnload == 0)
		{
			return;
		}
		auto SkinIt = m_Skins.find(SkinName);
		if(SkinIt == m_Skins.end())
		{
			m_SkinsBackgroundList.remove(SkinName);
			continue;
		}
		if(CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(true, SkinIt->second->m_State, SkinIt->second->m_AlwaysLoaded))
		{
			SkinIt->second->ClearBackgroundUsage();
			continue;
		}
		if(TryUnloadContainer(SkinIt->second.get()))
		{
			NumToUnload--;
		}
	}
	const size_t MaxSkipped = m_SkinsUsageList.size() / 8;
	size_t NumSkipped = 0;
	std::vector<std::string> vUsageSnapshot;
	vUsageSnapshot.reserve(m_SkinsUsageList.size());
	for(auto It = m_SkinsUsageList.rbegin(); It != m_SkinsUsageList.rend(); ++It)
	{
		vUsageSnapshot.push_back(*It);
	}
	for(const std::string &SkinName : vUsageSnapshot)
	{
		if(NumToUnload == 0 || NumSkipped >= MaxSkipped)
		{
			break;
		}

		auto SkinIt = m_Skins.find(SkinName);
		if(CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(SkinIt != m_Skins.end(),
			   SkinIt != m_Skins.end() ? SkinIt->second->m_State : CSkinContainer::EState::UNLOADED,
			   SkinIt != m_Skins.end() && SkinIt->second->m_AlwaysLoaded))
		{
			if(SkinIt != m_Skins.end() && SkinIt->second->m_UsageEntryIterator.has_value())
			{
				SkinIt->second->ClearUsage();
			}
			else
				m_SkinsUsageList.remove(SkinName);
			continue;
		}
		auto &pSkinContainer = SkinIt->second;
		if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&
			pSkinContainer->m_State != CSkinContainer::EState::LOADED)
		{
			dbg_assert(pSkinContainer->m_State == CSkinContainer::EState::LOADING ||
					   pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED,
				"m_SkinsUsageList contains skin which is not BACKGROUND_REQUESTED, PENDING, LOADING or LOADED");
			NumSkipped++;
			continue;
		}
		if(!TryUnloadContainer(pSkinContainer.get()))
		{
			NumSkipped++;
			continue;
		}
		NumToUnload--;
	}
}

bool CSkins::ReclaimBackgroundSkinForPriorityRequest(const char *pRequesterName, int CountFuseLimit)
{
	if(CountFuseLimit <= 0 || m_SkinsBackgroundList.empty())
		return false;

	size_t NumPendingLoading = 0;
	for(const auto &[_, pSkinContainer] : m_Skins)
	{
		if(pSkinContainer->m_State == CSkinContainer::EState::PENDING ||
			pSkinContainer->m_State == CSkinContainer::EState::LOADING)
		{
			if(++NumPendingLoading >= (size_t)CountFuseLimit)
				break;
		}
	}
	if(NumPendingLoading < (size_t)CountFuseLimit)
		return false;

	std::vector<std::string> vBackgroundSnapshot;
	vBackgroundSnapshot.reserve(m_SkinsBackgroundList.size());
	for(const std::string &SkinName : m_SkinsBackgroundList)
		vBackgroundSnapshot.push_back(SkinName);

	bool ReclaimedBackgroundRequested = false;
	for(const std::string &SkinName : vBackgroundSnapshot)
	{
		if(pRequesterName != nullptr && SkinName == pRequesterName)
			continue;
		auto SkinIt = m_Skins.find(SkinName);
		if(SkinIt == m_Skins.end())
		{
			m_SkinsBackgroundList.remove(SkinName);
			continue;
		}
		CSkinContainer *pSkinContainer = SkinIt->second.get();
		if(!pSkinContainer->m_BackgroundEntryIterator.has_value() || pSkinContainer->m_UsageEntryIterator.has_value())
			continue;
		if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)
		{
			pSkinContainer->SetState(CSkinContainer::EState::UNLOADED);
			ReclaimedBackgroundRequested = true;
			continue;
		}
		if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&
			pSkinContainer->m_State != CSkinContainer::EState::LOADING &&
			pSkinContainer->m_State != CSkinContainer::EState::LOADED)
			continue;
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADING && pSkinContainer->m_pLoadJob != nullptr)
		{
			pSkinContainer->m_pLoadJob->Abort();
			pSkinContainer->m_pLoadJob = nullptr;
		}
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADED)
		{
			continue;
		}
		pSkinContainer->SetState(CSkinContainer::EState::UNLOADED);
		return true;
	}
	return ReclaimedBackgroundRequested;
}

void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)
{
	const bool TeeSettingsActive = ActiveSettingsTeePage(GameClient());
	const SSettingsResourceFrameContext FrameContext = SettingsFrameContextOrDefault(GameClient());
	const bool BackgroundDrainActive = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_BackgroundDrainActive : SettingsSkinBackgroundDrainActive(FrameContext, TeeSettingsActive);
	const int CountFuseLimit = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_CountFuseLimit : SettingsSkinSourceCountFuseLimit(FrameContext, TeeSettingsActive, g_Config.m_ClSkinsLoadedMax);
	const int NormalLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_NormalLoadingWindow : SettingsSkinSourceLoadNormalWindow(FrameContext, TeeSettingsActive, g_Config.m_ClSkinsLoadedMax);
	const int VisibleLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleLoadingWindow : SettingsSkinSourceLoadVisibleWindow(FrameContext, TeeSettingsActive, g_Config.m_ClSkinsLoadedMax);
	m_SettingsSourceAdmissionTelemetry = {};
	m_SettingsSourceAdmissionTelemetry.m_CountFuseLimit = CountFuseLimit;
	m_SettingsSourceAdmissionTelemetry.m_VisibleReserve = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleReserve : 8;
	m_SettingsSourceAdmissionTelemetry.m_RealInflight = (int)Stats.RealInflight();
	m_SettingsSourceAdmissionTelemetry.m_LoadingWindowUsed = (int)Stats.m_NumLoading;
	m_SettingsSourceAdmissionTelemetry.m_GpuUploadLimitUnits = GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame();
	m_SettingsSourceAdmissionTelemetry.m_GpuUploadRemainingUnits = GameClient()->GpuUploadLimiter()->RemainingUploads();
	m_SettingsSourceAdmissionTelemetry.m_FinalizeBudgetLimit = SettingsSkinMaxPerFrame(GameClient());
	m_SettingsSourceAdmissionTelemetry.m_VisibleBackgroundRequested = m_SettingsTeeVisibleSnapshot.m_VisibleBackgroundRequested;
	m_SettingsSourceAdmissionTelemetry.m_VisibleNonterminalWaiting = m_SettingsTeeVisibleSnapshot.m_VisibleNonterminalWaiting;
	m_SettingsSourceAdmissionTelemetry.m_UnderfedStreak = m_SettingsThroughputControllerOutput.m_UnderfedStreak;
	m_SettingsSourceAdmissionTelemetry.m_FrameTimeAverageMs = Client()->FrameTimeAverage() * 1000.0f;
	m_SettingsSourceAdmissionTelemetry.m_RenderFrameTimeMs = Client()->RenderFrameTime() * 1000.0f;
	m_SettingsSourceAdmissionTelemetry.m_AdmissionUnderfed = m_SettingsThroughputControllerOutput.m_AdmissionUnderfed;
	str_copy(m_SettingsSourceAdmissionTelemetry.m_aDynamicDecision,
		SettingsSkinThroughputControllerReasonName(m_SettingsThroughputControllerOutput.m_Reason),
		sizeof(m_SettingsSourceAdmissionTelemetry.m_aDynamicDecision));
	str_copy(m_SettingsSourceAdmissionTelemetry.m_aControllerMode,
		SettingsSkinThroughputControllerModeName(m_SettingsThroughputControllerOutput.m_Mode),
		sizeof(m_SettingsSourceAdmissionTelemetry.m_aControllerMode));
	str_copy(m_SettingsSourceAdmissionTelemetry.m_aControllerReason,
		SettingsSkinThroughputControllerReasonName(m_SettingsThroughputControllerOutput.m_Reason),
		sizeof(m_SettingsSourceAdmissionTelemetry.m_aControllerReason));
	const int EffectiveNormalLoadingWindow = NormalLoadingWindow;
	m_SettingsSourceAdmissionTelemetry.m_LoadingWindowLimit = EffectiveNormalLoadingWindow;
	m_SettingsSourceAdmissionTelemetry.m_AdmissionInvariantViolated = Stats.AdmissionInvariantViolated(CountFuseLimit);
	struct SSettingsSourceAdmissionDecision
	{
		bool m_PromoteAllowed = true;
		ESettingsResourcePriority m_PromotePriority = ESettingsResourcePriority::BACKGROUND;
		const char *m_pBlockReason = "none";
		bool m_CountFuseApplies = true;
	};
	auto DetermineAdmission = [&](CSkinContainer *pSkinContainer, ESettingsResourcePriority Priority) {
		SSettingsSourceAdmissionDecision Admission;
		Admission.m_PromotePriority = pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED ? pSkinContainer->m_LoadPriority : Priority;
		if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)
		{
			const auto SourceAdmission = SettingsSkinSourceAdmissionDecision({
				true,
				Admission.m_PromotePriority,
				BackgroundDrainActive,
				(int)Stats.m_NumLoading,
				EffectiveNormalLoadingWindow,
				VisibleLoadingWindow,
			});
			Admission.m_PromoteAllowed = SourceAdmission.m_PromoteAllowed;
			Admission.m_PromotePriority = SourceAdmission.m_PromotePriority;
			Admission.m_pBlockReason = SettingsSkinSourceAdmissionBlockReasonName(SourceAdmission.m_BlockReason);
			Admission.m_CountFuseApplies = SourceAdmission.m_CountFuseApplies;
			return Admission;
		}

		if(pSkinContainer->m_State == CSkinContainer::EState::PENDING && TeeSettingsActive)
		{
			const auto SourceAdmission = SettingsSkinSourceAdmissionDecision({
				false,
				Priority,
				BackgroundDrainActive,
				(int)Stats.m_NumLoading,
				EffectiveNormalLoadingWindow,
				VisibleLoadingWindow,
			});
			Admission.m_PromoteAllowed = SourceAdmission.m_PromoteAllowed;
			Admission.m_PromotePriority = SourceAdmission.m_PromotePriority;
			Admission.m_pBlockReason = SettingsSkinSourceAdmissionBlockReasonName(SourceAdmission.m_BlockReason);
			Admission.m_CountFuseApplies = SourceAdmission.m_CountFuseApplies;
			return Admission;
		}
		return Admission;
	};
	auto StartLoadJob = [&](CSkinContainer *pSkinContainer, ESettingsResourcePriority Priority) {
		if(Stats.m_NumPending == 0 && pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)
		{
			return true;
		}
		const auto Admission = DetermineAdmission(pSkinContainer, Priority);
		if(!Admission.m_PromoteAllowed)
		{
			str_copy(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason, Admission.m_pBlockReason, sizeof(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason));
			LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), Admission.m_pBlockReason,
				GameClient()->GpuUploadLimiter()->RemainingUploads(),
				GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
			return false;
		}
		const bool CountFuseApplies = Admission.m_CountFuseApplies;
		if(CountFuseApplies && CountFuseLimit > 0 && Stats.m_NumPending + Stats.m_NumLoading >= (size_t)CountFuseLimit)
		{
			if(ReclaimBackgroundSkinForPriorityRequest(pSkinContainer->Name(), CountFuseLimit))
			{
				Stats = LoadingStats();
			}
			if(Stats.m_NumPending + Stats.m_NumLoading >= (size_t)CountFuseLimit)
			{
				str_copy(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason, "queue_fuse", sizeof(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason));
				LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), "queue_fuse",
					GameClient()->GpuUploadLimiter()->RemainingUploads(),
					GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
				return false;
			}
		}
		if(pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED)
		{
			pSkinContainer->SetState(CSkinContainer::EState::PENDING, Admission.m_PromotePriority);
			Stats.m_NumBackgroundRequested--;
			Stats.m_NumPending++;
			m_SettingsSourceAdmissionTelemetry.m_AdmittedDelta++;
		}
		Priority = Admission.m_PromotePriority;
		if(pSkinContainer->m_State != CSkinContainer::EState::PENDING)
		{
			return true;
		}

		switch(pSkinContainer->Type())
		{
		case CSkinContainer::EType::LOCAL:
			pSkinContainer->m_pLoadJob = std::make_shared<CSkinLoadJob>(this, pSkinContainer->Name(), pSkinContainer->StorageType());
			break;
		case CSkinContainer::EType::DOWNLOAD:
			pSkinContainer->m_pLoadJob = std::make_shared<CSkinDownloadJob>(this, pSkinContainer->Name());
			break;
		default:
			dbg_assert_failed("pSkinContainer->Type() invalid");
		}
		Engine()->AddJob(pSkinContainer->m_pLoadJob);
		LogSettingsSkinSourceRequestEvent(pSkinContainer->Name(), Priority, pSkinContainer->m_State);
		pSkinContainer->SetState(CSkinContainer::EState::LOADING, Priority);
		Stats.m_NumPending--;
		Stats.m_NumLoading++;
		m_SettingsSourceAdmissionTelemetry.m_StartedDelta++;
		return true;
	};

	std::vector<std::string> vPrioritizedSkinNames;
	vPrioritizedSkinNames.reserve(m_SkinsUsageList.size());
	for(const std::string &SkinName : m_SkinsUsageList)
	{
		vPrioritizedSkinNames.push_back(SkinName);
	}
	for(const std::string &SkinName : vPrioritizedSkinNames)
	{
		auto It = m_Skins.find(SkinName);
		if(CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(It != m_Skins.end(),
			   It != m_Skins.end() ? It->second->m_State : CSkinContainer::EState::UNLOADED,
			   It != m_Skins.end() && It->second->m_AlwaysLoaded))
		{
			if(It != m_Skins.end() && It->second->m_UsageEntryIterator.has_value())
			{
				It->second->ClearUsage();
			}
			else
			{
				m_SkinsUsageList.remove(SkinName);
			}
			continue;
		}
		if(It == m_Skins.end())
		{
			continue;
		}
		if(!StartLoadJob(It->second.get(), It->second->m_LoadPriority))
		{
			return;
		}
	}

	std::vector<std::string> vBackgroundSkinNames;
	vBackgroundSkinNames.reserve(m_SkinsBackgroundList.size());
	for(const std::string &SkinName : m_SkinsBackgroundList)
	{
		vBackgroundSkinNames.push_back(SkinName);
	}
	for(const std::string &SkinName : vBackgroundSkinNames)
	{
		auto It = m_Skins.find(SkinName);
		if(It == m_Skins.end())
		{
			continue;
		}
		if(!StartLoadJob(It->second.get(), ESettingsResourcePriority::BACKGROUND))
		{
			return;
		}
	}

	int FallbackScanned = 0;
	int FallbackStarted = 0;
	int FallbackSkipped = 0;
	const auto FallbackStartTime = time_get_nanoseconds();
	const char *pFallbackReason = m_SkinsUsageList.empty() && m_SkinsBackgroundList.empty() ? "disabled_explicit_queues" : "fallback_sweep";
	static constexpr int MaxFallbackSweepItems = 64;
	std::vector<std::string> vFallbackSkinNames;
	vFallbackSkinNames.reserve(minimum(MaxFallbackSweepItems, (int)m_Skins.size()));
	for(const auto &[SkinName, pSkinContainer] : m_Skins)
	{
		(void)pSkinContainer;
		if((int)vFallbackSkinNames.size() >= MaxFallbackSweepItems)
			break;
		vFallbackSkinNames.push_back(SkinName);
	}
	for(const std::string &SkinName : vFallbackSkinNames)
	{
		auto It = m_Skins.find(SkinName);
		if(It == m_Skins.end())
			continue;
		CSkinContainer *pSkinContainer = It->second.get();
		++FallbackScanned;
		if(pSkinContainer->m_UsageEntryIterator.has_value() || pSkinContainer->m_BackgroundEntryIterator.has_value())
		{
			++FallbackSkipped;
			continue;
		}
		if(!StartLoadJob(pSkinContainer, ESettingsResourcePriority::BACKGROUND))
		{
			break;
		}
		++FallbackStarted;
	}
	m_SettingsSourceAdmissionTelemetry.m_FallbackSweepScanned = FallbackScanned;
	m_SettingsSourceAdmissionTelemetry.m_FallbackSweepStarted = FallbackStarted;
	if(FallbackScanned > 0)
	{
		const double FallbackDurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - FallbackStartTime).count();
		LogSettingsSkinStartLoadingFallbackSweepEvent((int)m_Skins.size(), FallbackScanned, FallbackStarted, FallbackSkipped, true, FallbackDurationMs, pFallbackReason);
	}
	m_SettingsSourceAdmissionTelemetry.m_RealInflight = (int)Stats.RealInflight();
	m_SettingsSourceAdmissionTelemetry.m_LoadingWindowUsed = (int)Stats.m_NumLoading;
	m_SettingsSourceAdmissionTelemetry.m_AdmissionInvariantViolated = Stats.AdmissionInvariantViolated(CountFuseLimit);
	m_SettingsSourceLoadsAtLastStartLoading = m_SettingsSourceLoadsCompleted;
}

CSkins::ESkinProcessResult CSkins::ProcessSkinContainer(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,
	int &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,
	std::chrono::nanoseconds MaxTime)
{
	if(pSkinContainer->m_State != CSkinContainer::EState::LOADING)
	{
		return ESkinProcessResult::CONTINUE;
	}

	dbg_assert(pSkinContainer->m_pLoadJob != nullptr, "Skin container in loading state must have a load job");
	if(!pSkinContainer->m_pLoadJob->Done())
	{
		return ESkinProcessResult::CONTINUE;
	}

	// 首个已就绪皮肤始终取得进展，后续皮肤超过时间预算则留到下一轮。
	if(!QmSkinCanFinalize(SkinsProcessedThisFrame, time_get_nanoseconds() - StartTime, MaxTime))
	{
		return ESkinProcessResult::BREAK_TIME_EXCEEDED;
	}

	if(pSkinContainer->m_pLoadJob->State() == IJob::STATE_DONE &&
		(pSkinContainer->m_pLoadJob->m_Data.m_pPreparedTextures || pSkinContainer->m_SettingsPendingUploadData.m_pPreparedTextures))
		return DrainSettingsSkinPreviewUpload(pSkinContainer, Stats, SkinsProcessedThisFrame, StartTime, MaxTime);
	else
	{
		Stats.m_NumLoading--;
		SkinsProcessedThisFrame++;

		if(pSkinContainer->m_pLoadJob->State() == IJob::STATE_DONE && pSkinContainer->m_pLoadJob->m_NotFound)
		{
			pSkinContainer->SetState(CSkinContainer::EState::NOT_FOUND);
			Stats.m_NumNotFound++;
		}
		else
		{
			pSkinContainer->SetState(CSkinContainer::EState::ERROR);
			Stats.m_NumError++;
		}
		pSkinContainer->m_pLoadJob = nullptr;
	}

	if(time_get_nanoseconds() - StartTime >= MaxTime)
	{
		return ESkinProcessResult::BREAK_TIME_EXCEEDED;
	}

	return ESkinProcessResult::CONTINUE;
}

CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,
	int &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,
	std::chrono::nanoseconds MaxTime)
{
	if(!m_SkinUploadFrameBudget.TryConsume())
		return ESkinProcessResult::BREAK_UPLOAD;
	const int MaxSkinsPerFrame = SettingsSkinMaxPerFrame(GameClient());
	if(!GameClient()->GpuUploadLimiter()->CanUpload(1))
	{
		LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), "gpu_upload_budget",
			GameClient()->GpuUploadLimiter()->RemainingUploads(),
			GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
		LogSkinSettingsResourcePerf("upload", 0, MaxSkinsPerFrame, Stats.m_NumLoading, ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET, 0.0);
		return ESkinProcessResult::BREAK_GPU_LIMIT;
	}
	SSettingsResourceMergeBudget UploadBudget;
	UploadBudget.m_MaxGpuUploads = 1;
	if(!SettingsResourceConsumeGpuUpload(UploadBudget, SettingsFrameBudgetOrNull(GameClient())))
	{
		LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), "max_per_frame",
			GameClient()->GpuUploadLimiter()->RemainingUploads(),
			GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
		LogSkinSettingsResourcePerf("upload", 0, MaxSkinsPerFrame, Stats.m_NumLoading, SettingsResourceMissReason(UploadBudget.m_StopReason), 0.0);
		return ESkinProcessResult::BREAK_GPU_LIMIT;
	}
	if(!pSkinContainer->m_SettingsPendingUploadData.m_pPreparedTextures)
	{
		if(!BeginSkinPreviewUpload(pSkinContainer, std::move(pSkinContainer->m_pLoadJob->m_Data)))
			return ESkinProcessResult::BREAK_UPLOAD;
	}
	SResourcePreviewUploadBudget SkinPreviewUploadBudget;
	SkinPreviewUploadBudget.m_MaxUploads = 1;
	SkinPreviewUploadBudget.m_pGpuUploadLimiter = GameClient()->GpuUploadLimiter();
	if(!SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget))
	{
		LogSettingsSkinSourceStageEvent("preview_uploads", pSkinContainer->Name(), 0, 0, 0, 0.0, 0);
		return ESkinProcessResult::BREAK_GPU_LIMIT;
	}
	if(!UploadNextSkinPreviewSprite(pSkinContainer, SkinPreviewUploadBudget))
	{
		pSkinContainer->m_pLoadJob = nullptr;
		pSkinContainer->SetState(CSkinContainer::EState::ERROR);
		Stats.m_NumLoading--;
		SkinsProcessedThisFrame++;
		Stats.m_NumError++;
		return ESkinProcessResult::BREAK_UPLOAD;
	}
	SettingsResourcePreviewCommitUploadBudget(SkinPreviewUploadBudget);
	LogSettingsSkinSourceStageEvent("preview_uploads", pSkinContainer->Name(),
		pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth,
		pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, 0, 0.0, 1);
	if(pSkinContainer->m_SettingsPendingUploadSprite < SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)
		return ESkinProcessResult::BREAK_UPLOAD;

	Stats.m_NumLoading--;
	SkinsProcessedThisFrame++;
	FinishSkinPreviewUpload(pSkinContainer);
	GameClient()->OnSkinUpdate(pSkinContainer->Name());
	pSkinContainer->m_pLoadJob = nullptr;
	Stats.m_NumLoaded++;
	++m_SettingsSourceLoadsCompleted;
	LogSkinSettingsResourcePerf("upload", 1, MaxSkinsPerFrame, (int)Stats.m_NumLoading, ESettingsWarmupMissReason::NONE, 0.0);

	if(time_get_nanoseconds() - StartTime >= MaxTime)
		return ESkinProcessResult::BREAK_TIME_EXCEEDED;
	return ESkinProcessResult::CONTINUE;
}

void CSkins::UpdateFinishLoading(CSkinLoadingStats &Stats, std::chrono::nanoseconds StartTime, std::chrono::nanoseconds MaxTime)
{
	int SkinsProcessedThisFrame = 0;
	const int MaxSkinsPerFrame = SettingsSkinMaxPerFrame(GameClient());
	LogSettingsSkinFrameCapEvent(GameClient());
	bool ProcessedHighPrioritySkin = false;
	std::vector<std::string> vUsageSnapshot;
	vUsageSnapshot.reserve(m_SkinsUsageList.size());
	for(const std::string &SkinName : m_SkinsUsageList)
	{
		vUsageSnapshot.push_back(SkinName);
	}

	// First, try to process skins from the usage list (most recently used first)
	// This prioritizes visible/commonly used skins for better perceived performance
	for(const std::string &SkinName : vUsageSnapshot)
	{
		if(Stats.m_NumLoading == 0)
		{
			break;
		}
		if(SkinsProcessedThisFrame >= MaxSkinsPerFrame)
		{
			LogSettingsSkinSourceWaitEvent(SkinName.c_str(), "max_per_frame",
				GameClient()->GpuUploadLimiter()->RemainingUploads(),
				GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
			break;
		}

		auto It = m_Skins.find(SkinName);
		if(CSkinContainer::ShouldDiscardUsageEntryBeforeUnload(It != m_Skins.end(),
			   It != m_Skins.end() ? It->second->m_State : CSkinContainer::EState::UNLOADED,
			   It != m_Skins.end() && It->second->m_AlwaysLoaded))
		{
			if(It != m_Skins.end() && It->second->m_UsageEntryIterator.has_value())
			{
				It->second->ClearUsage();
			}
			else
			{
				m_SkinsUsageList.remove(SkinName);
			}
			continue;
		}
		if(It == m_Skins.end())
		{
			continue;
		}

		ESkinProcessResult Result = ProcessSkinContainer(It->second.get(), Stats, SkinsProcessedThisFrame, StartTime, MaxTime);
		if(Result == ESkinProcessResult::BREAK_GPU_LIMIT || Result == ESkinProcessResult::BREAK_TIME_EXCEEDED || Result == ESkinProcessResult::BREAK_UPLOAD)
		{
			return;
		}
		if(Result == ESkinProcessResult::CONTINUE && It->second->m_State == CSkinContainer::EState::LOADED)
		{
			ProcessedHighPrioritySkin = true;
		}
	}

	if(SettingsSkinFinalizeShouldDeferBackgroundSweep(ProcessedHighPrioritySkin, SkinsProcessedThisFrame, MaxSkinsPerFrame))
	{
		return;
	}

	std::vector<std::string> vBackgroundSnapshot;
	vBackgroundSnapshot.reserve(m_SkinsBackgroundList.size());
	for(const std::string &SkinName : m_SkinsBackgroundList)
	{
		vBackgroundSnapshot.push_back(SkinName);
	}
	for(const std::string &SkinName : vBackgroundSnapshot)
	{
		if(Stats.m_NumLoading == 0)
		{
			break;
		}
		if(SkinsProcessedThisFrame >= MaxSkinsPerFrame)
		{
			LogSettingsSkinSourceWaitEvent(SkinName.c_str(), "max_per_frame",
				GameClient()->GpuUploadLimiter()->RemainingUploads(),
				GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
			break;
		}

		auto It = m_Skins.find(SkinName);
		if(It == m_Skins.end())
		{
			continue;
		}
		if(It->second->m_UsageEntryIterator.has_value())
		{
			continue;
		}

		ESkinProcessResult Result = ProcessSkinContainer(It->second.get(), Stats, SkinsProcessedThisFrame, StartTime, MaxTime);
		if(Result == ESkinProcessResult::BREAK_GPU_LIMIT || Result == ESkinProcessResult::BREAK_TIME_EXCEEDED || Result == ESkinProcessResult::BREAK_UPLOAD)
		{
			return;
		}
	}

	// Process remaining loading skins that are not tracked by either priority queue.
	// This ensures legacy and direct-load paths can still finish.
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if(Stats.m_NumLoading == 0 || SkinsProcessedThisFrame >= MaxSkinsPerFrame)
		{
			break;
		}

		// Skip skins that were already processed (those in usage list)
		if(pSkinContainer->m_UsageEntryIterator.has_value())
		{
			continue;
		}
		if(pSkinContainer->m_BackgroundEntryIterator.has_value())
		{
			continue;
		}

		ESkinProcessResult Result = ProcessSkinContainer(pSkinContainer.get(), Stats, SkinsProcessedThisFrame, StartTime, MaxTime);
		if(Result == ESkinProcessResult::BREAK_GPU_LIMIT || Result == ESkinProcessResult::BREAK_TIME_EXCEEDED || Result == ESkinProcessResult::BREAK_UPLOAD)
		{
			break;
		}
	}
}

void CSkins::RefreshEventSkins()
{
	m_aEventSkinPrefix[0] = '\0';

	if(g_Config.m_Events)
	{
		if(time_season() == ETimeSeason::XMAS)
		{
			str_copy(m_aEventSkinPrefix, "santa");
		}
	}
}

void CSkins::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)
{
	if(m_pSkinDirectoryScanJob)
	{
		m_pSkinDirectoryScanJob->Abort();
		m_pSkinDirectoryScanJob.reset();
	}
	if(m_pSkinListPlanJob)
	{
		m_pSkinListPlanJob->Abort();
		m_pSkinListPlanJob.reset();
	}
	m_vPendingSkinListMergeEntries.clear();
	m_vPendingSkinListEntries.clear();
	m_HasPendingSkinListMergePlan = false;
	m_SkinListMergeCursor = 0;
	m_PendingSkinListUnfilteredCount = 0;
	m_vPendingSkinDirectoryEntries.clear();
	m_SkinDirectoryMergeCursor = 0;

	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if(str_comp(pSkinContainer->Name(), "default") == 0)
		{
			continue;
		}
		if(pSkinContainer->m_pLoadJob)
		{
			pSkinContainer->m_pLoadJob->Abort();
			pSkinContainer->m_pLoadJob = nullptr;
		}
		if(pSkinContainer->m_State != CSkinContainer::EState::LOADED)
			pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
	}
	m_SkinList.m_NeedsUpdate = true;

	const auto LoadSpecialSkinDirect = [&](const char *pName) {
		LoadSkinDirect(pName);
		const auto Skin = m_Skins.find(pName);
		if(Skin != m_Skins.end() && Skin->second->State() == CSkinContainer::EState::LOADED)
		{
			GameClient()->OnSkinUpdate(pName);
		}
		SkinLoadedCallback();
	};

	LoadSkinDirect("default");
	LoadOfficialSkinIndexCache();
	SkinLoadedCallback();
	QueueOfficialSkinIndexRequest();
	LoadSpecialSkinDirect("x_ninja");
	LoadSpecialSkinDirect("x_spec");
	QueueSkinDirectoryScanJob();
}

void CSkins::CollectUnresolvedSkins()
{
	if(!m_UnresolvedSkinScanState.Consume())
		return;
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if(!CSkinContainer::IsUnresolved(pSkinContainer->m_State) || pSkinContainer->m_UnresolvedNotified)
			continue;
		pSkinContainer->m_UnresolvedNotified = true;
		m_vSkinsUnresolvedThisFrame.emplace_back(pSkinContainer->Name());
	}
}

CSkins::CSkinLoadingStats CSkins::LoadingStats() const
{
	CSkinLoadingStats Stats;
	for(const auto &[_, pSkinContainer] : m_Skins)
		Stats.AddState(pSkinContainer->m_State);
	return Stats;
}

CSkins::CSkinList &CSkins::SkinList(int Dummy)
{
	const CSkinListEntry::SColorKey MainColorKey = MakeSkinListColorKey(0);
	const CSkinListEntry::SColorKey DummyColorKey = MakeSkinListColorKey(1);
	if(!m_SkinList.m_NeedsUpdate &&
		m_SkinList.m_Dummy != Dummy)
	{
		m_SkinList.ForceRefresh();
	}
	if(!m_SkinList.m_NeedsUpdate &&
		(!SkinListColorKeyEquals(m_SkinList.m_MainColorKey, MainColorKey) ||
			!SkinListColorKeyEquals(m_SkinList.m_DummyColorKey, DummyColorKey)))
	{
		m_SkinList.ForceRefresh();
	}

	ProcessSkinListPlanJob();
	if(m_SkinList.m_NeedsUpdate && m_pSkinListPlanJob == nullptr && m_vPendingSkinListMergeEntries.empty())
	{
		QueueSkinListPlanJob(Dummy);
		m_SkinList.m_NeedsUpdate = false;
	}

	if(!m_SkinList.m_NeedsUpdate && m_pSkinListPlanJob == nullptr && m_vPendingSkinListMergeEntries.empty())
	{
		return m_SkinList;
	}
	return m_SkinList;
}

void CSkins::RebuildSkinListPlan()
{
	m_SkinList.ForceRefresh();
}

bool CSkins::SkinListReady() const
{
	return !m_SkinList.m_NeedsUpdate &&
	       m_pSkinDirectoryScanJob == nullptr &&
	       m_pSkinListPlanJob == nullptr &&
	       m_vPendingSkinDirectoryEntries.empty() &&
	       !m_HasPendingSkinListMergePlan &&
	       m_vPendingSkinListMergeEntries.empty() &&
	       m_vPendingSkinListEntries.empty();
}

bool CSkins::SkinListSkeletonReady() const
{
	const SSkinListPlanState State{
		m_pSkinDirectoryScanJob != nullptr || !m_vPendingSkinDirectoryEntries.empty(),
		!m_SkinList.m_NeedsUpdate &&
			m_pSkinListPlanJob == nullptr &&
			!m_HasPendingSkinListMergePlan &&
			m_vPendingSkinListMergeEntries.empty() &&
			m_vPendingSkinListEntries.empty(),
		(int)m_SkinList.m_vSkins.size(),
		-1,
		-1,
	};
	return SettingsSkinListSkeletonReady(State);
}

void CSkins::PrewarmByNames(const std::vector<std::string> &vNames, bool Immediate)
{
	const ESettingsResourcePriority Priority = Immediate ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::PREFETCH;
	for(const std::string &Name : vNames)
	{
		if(Name.empty())
			continue;

		const CSkinContainer *pContainer = FindContainerOrNullptr(Name.c_str());
		if(pContainer == nullptr)
			continue;

		const_cast<CSkinContainer *>(pContainer)->RequestLoad(Priority);
	}
}

bool CSkins::PrewarmPlayerPreviewReady(int Dummy, int MaxEntries, bool ProgressiveListReady)
{
	CSkinList &List = SkinList(Dummy);

	std::vector<std::string> vNames;
	const int VisibleTotal = minimum((int)List.Skins().size(), MaxEntries);
	vNames.reserve(VisibleTotal + 1);
	const char *pSelectedSkin = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	vNames.push_back(pSelectedSkin != nullptr && pSelectedSkin[0] != '\0' ? pSelectedSkin : "default");
	for(int i = 0; i < VisibleTotal; ++i)
	{
		const CSkinContainer *pContainer = List.Skins()[i].SkinContainer();
		if(pContainer != nullptr)
			vNames.emplace_back(pContainer->Name());
	}

	char aWarmupRequest[128];
	str_format(aWarmupRequest, sizeof(aWarmupRequest), "dummy=%d selected=%s visible_total=%d", Dummy, vNames.front().c_str(), VisibleTotal);
	LogSettingsSkinSourceWarmupEvent("warmup_request", aWarmupRequest);

	PrewarmByNames(vNames, true);

	const bool ProgressiveEntriesReady = SettingsSkinListHasProgressiveWarmEntries((int)m_SkinList.m_vSkins.size(), MaxEntries, (int)m_vPendingSkinListMergeEntries.size());
	const bool ListReady = !m_SkinList.m_NeedsUpdate &&
			       m_pSkinDirectoryScanJob == nullptr &&
			       m_pSkinListPlanJob == nullptr &&
			       m_vPendingSkinDirectoryEntries.empty() &&
			       (ProgressiveListReady ? ProgressiveEntriesReady : m_vPendingSkinListMergeEntries.empty());
	if(!ListReady)
	{
		LogSettingsSkinSourceWarmupEvent("warmup_miss", "reason=list_pending");
		return false;
	}

	const CSkinContainer *pDefaultContainer = FindContainerOrNullptr("default");
	const bool DefaultReady = pDefaultContainer != nullptr && pDefaultContainer->State() == CSkinContainer::EState::LOADED;
	if(!DefaultReady)
	{
		LogSettingsSkinSourceWarmupEvent("warmup_miss", "reason=default_loading");
		return false;
	}

	const CSkinContainer *pSelectedContainer = FindContainerOrNullptr(vNames.front().c_str());
	const bool SelectedReady = pSelectedContainer == nullptr ||
				   pSelectedContainer->State() == CSkinContainer::EState::LOADED ||
				   pSelectedContainer->State() == CSkinContainer::EState::ERROR ||
				   pSelectedContainer->State() == CSkinContainer::EState::NOT_FOUND;

	int VisibleReadyCount = 0;
	int SourceLoadedCount = 0;
	const char *pMissReason = SelectedReady ? "visible_source_loading" : "selected_source_loading";
	for(int i = 0; i < VisibleTotal; ++i)
	{
		const CSkinContainer *pContainer = List.Skins()[i].SkinContainer();
		if(pContainer == nullptr)
			continue;

		const CSkinContainer::EState State = pContainer->State();
		if(State == CSkinContainer::EState::LOADED)
		{
			++VisibleReadyCount;
			++SourceLoadedCount;
			continue;
		}
		if(State == CSkinContainer::EState::ERROR || State == CSkinContainer::EState::NOT_FOUND)
			++VisibleReadyCount;
		else
			pMissReason = "visible_source_loading";
	}

	char aWarmupGate[192];
	str_format(aWarmupGate, sizeof(aWarmupGate),
		"dummy=%d selected_ready=%d visible_ready_count=%d visible_total=%d source_loaded_count=%d preview_ready_count=0 restore_queue_count=0",
		Dummy, SelectedReady ? 1 : 0, VisibleReadyCount, VisibleTotal, SourceLoadedCount);
	LogSettingsSkinSourceWarmupEvent("warmup_gate", aWarmupGate);
	if(!SelectedReady || VisibleReadyCount < VisibleTotal)
	{
		char aWarmupMiss[96];
		str_format(aWarmupMiss, sizeof(aWarmupMiss), "dummy=%d reason=%s", Dummy, pMissReason);
		LogSettingsSkinSourceWarmupEvent("warmup_miss", aWarmupMiss);
		return false;
	}
	return true;
}

void CSkins::QueueSkinListPlanJob(int Dummy)
{
	for(const auto &FavoriteSkin : m_Favorites)
		FindContainerOrNullptr(FavoriteSkin.c_str());

	const CSkinListEntry::SColorKey CurrentColorKey = MakeSkinListColorKey(Dummy);
	const CSkinListEntry::SColorKey MainColorKey = MakeSkinListColorKey(0);
	const CSkinListEntry::SColorKey DummyColorKey = MakeSkinListColorKey(1);

	std::vector<SSkinListSnapshotEntry> vEntries;
	vEntries.reserve(m_Skins.size() + m_aSkinQueue[Dummy].size());
	const auto EntryColorKey = [&](const SSkinListSnapshotEntry &Entry) {
		return Entry.m_ColorKey.value_or(CurrentColorKey);
	};
	const auto HasListEntry = [&](const CSkinContainer *pSkinContainer, const CSkinListEntry::SColorKey &ColorKey) {
		return std::any_of(vEntries.begin(), vEntries.end(), [&](const SSkinListSnapshotEntry &Entry) {
			return Entry.m_Name == pSkinContainer->Name() && SkinListColorKeyEquals(EntryColorKey(Entry), ColorKey);
		});
	};
	const auto AddSkinListSnapshotEntry = [&](CSkinContainer *pSkinContainer, std::optional<CSkinListEntry::SColorKey> ColorKey, bool ForceShowNotFound) {
		if(pSkinContainer->IsSpecial())
			return;

		const CSkinListEntry::SColorKey EffectiveColorKey = ColorKey.value_or(CurrentColorKey);
		const bool SelectedMain = str_comp(pSkinContainer->Name(), g_Config.m_ClPlayerSkin) == 0;
		const bool SelectedDummy = str_comp(pSkinContainer->Name(), g_Config.m_ClDummySkin) == 0;
		const bool SelectedMainColor = SelectedMain && SkinListColorKeyEquals(EffectiveColorKey, MainColorKey);
		const bool SelectedDummyColor = SelectedDummy && SkinListColorKeyEquals(EffectiveColorKey, DummyColorKey);
		const bool Favorite = IsFavorite(pSkinContainer->Name());
		if(pSkinContainer->m_State == CSkinContainer::EState::NOT_FOUND &&
			!SelectedMainColor &&
			!SelectedDummyColor &&
			!Favorite &&
			!ForceShowNotFound)
		{
			return;
		}

		if(HasListEntry(pSkinContainer, EffectiveColorKey))
			return;

		SSkinListSnapshotEntry Entry;
		Entry.m_Name = pSkinContainer->Name();
		Entry.m_ColorKey = ColorKey;
		Entry.m_SelectedMain = SelectedMainColor;
		Entry.m_SelectedDummy = SelectedDummyColor;
		Entry.m_Favorite = Favorite;
		Entry.m_NotFound = pSkinContainer->m_State == CSkinContainer::EState::NOT_FOUND;
		Entry.m_Special = pSkinContainer->IsSpecial();
		Entry.m_ForceShowNotFound = ForceShowNotFound;
		Entry.m_OfficialReleaseDate = pSkinContainer->OfficialReleaseDate();
		Entry.m_LastModified = pSkinContainer->LastModified();
		vEntries.push_back(std::move(Entry));
	};

	for(const auto &[Name, pSkinContainer] : m_Skins)
	{
		AddSkinListSnapshotEntry(pSkinContainer.get(), std::nullopt, false);
	}
	for(const CSkinQueueEntry &QueueEntry : m_aSkinQueue[Dummy])
	{
		if(!CSkin::IsValidName(QueueEntry.m_SkinName.c_str()))
			continue;
		auto SkinIt = m_Skins.find(QueueEntry.m_SkinName);
		if(SkinIt == m_Skins.end())
		{
			CSkinContainer SkinContainer(this, QueueEntry.m_SkinName.c_str(), CSkinContainer::EType::DOWNLOAD, IStorage::TYPE_SAVE);
			auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
			pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
			SkinIt = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
		}
		const CSkinListEntry::SColorKey QueueColorKey = MakeSkinListColorKey(QueueEntry.m_UseCustomColor, QueueEntry.m_ColorBody, QueueEntry.m_ColorFeet);
		AddSkinListSnapshotEntry(SkinIt->second.get(), QueueColorKey, true);
	}

	m_pSkinListPlanJob = std::make_shared<CSkinListPlanJob>(std::move(vEntries), g_Config.m_ClSkinFilterString, ++m_SkinListPlanGeneration, std::clamp(g_Config.m_QmSkinSortMode, 0, 1));
	m_SkinList.m_Dummy = Dummy;
	m_SkinList.m_MainColorKey = MainColorKey;
	m_SkinList.m_DummyColorKey = DummyColorKey;
	Engine()->AddJob(m_pSkinListPlanJob);
	LogSkinSettingsResourcePerf("queued", 1, 1, 0, ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING, 0.0);
}

void CSkins::QueueSkinDirectoryScanJob()
{
	m_pSkinDirectoryScanJob = std::make_shared<CSkinDirectoryScanJob>(Storage());
	Engine()->AddJob(m_pSkinDirectoryScanJob);
}

void CSkins::ProcessSkinDirectoryScanJob()
{
	if(m_pSkinDirectoryScanJob && m_pSkinDirectoryScanJob->State() == IJob::STATE_DONE)
	{
		m_vPendingSkinDirectoryEntries = m_pSkinDirectoryScanJob->TakeResult().m_vEntries;
		m_SkinDirectoryMergeCursor = 0;
		m_pSkinDirectoryScanJob.reset();
	}

	if(m_vPendingSkinDirectoryEntries.empty())
		return;

	bool DirectoryScanDirty = false;
	SSettingsResourceMergeBudget MergeBudget;
	MergeBudget.m_MaxListEntries = 64;
	while(m_SkinDirectoryMergeCursor < m_vPendingSkinDirectoryEntries.size() && SettingsResourceConsumeMergeEntry(MergeBudget, SettingsFrameBudgetOrNull(GameClient())))
	{
		const auto &Entry = m_vPendingSkinDirectoryEntries[m_SkinDirectoryMergeCursor++];
		auto ExistingSkin = m_Skins.find(Entry.m_Name);
		if(ExistingSkin != m_Skins.end())
		{
			CSkinContainer *pSkinContainer = ExistingSkin->second.get();
			const bool KeepExistingLocalSkin =
				pSkinContainer->Type() == CSkinContainer::EType::LOCAL &&
				Entry.m_Type == CSkinContainer::EType::DOWNLOAD;
			if(KeepExistingLocalSkin)
			{
				continue;
			}
			if(pSkinContainer->LastModified() != Entry.m_LastModified)
			{
				pSkinContainer->SetLastModified(Entry.m_LastModified);
				DirectoryScanDirty = true;
			}
			if(pSkinContainer->Type() != Entry.m_Type)
			{
				const CSkinContainer::EState OldState = pSkinContainer->m_State;
				const ESettingsResourcePriority OldPriority = pSkinContainer->m_LoadPriority;
				const bool KeepRequestedState =
					OldState == CSkinContainer::EState::PENDING ||
					OldState == CSkinContainer::EState::LOADING ||
					OldState == CSkinContainer::EState::LOADED;
				if(OldState == CSkinContainer::EState::LOADING && pSkinContainer->m_pLoadJob != nullptr)
				{
					pSkinContainer->m_pLoadJob->Abort();
					pSkinContainer->m_pLoadJob = nullptr;
				}
				pSkinContainer->m_Type = Entry.m_Type;
				pSkinContainer->m_StorageType = Entry.m_StorageType;
				if(OldState == CSkinContainer::EState::LOADED && pSkinContainer->m_pSkin)
				{
					UnloadLoadedSkinTextures(pSkinContainer);
				}
				if(KeepRequestedState)
				{
					pSkinContainer->SetState(CSkinContainer::EState::PENDING, OldPriority);
				}
				else
				{
					pSkinContainer->SetState(pSkinContainer->DetermineInitialState(), ESettingsResourcePriority::VISIBLE);
				}
				DirectoryScanDirty = true;
			}
			continue;
		}

		CSkinContainer SkinContainer(this, Entry.m_Name.c_str(), Entry.m_Type, Entry.m_StorageType);
		auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
		pSkinContainer->SetLastModified(Entry.m_LastModified);
		pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
		m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)});
		DirectoryScanDirty = true;
	}
	if(DirectoryScanDirty)
		m_SkinList.m_NeedsUpdate = true;

	if(m_SkinDirectoryMergeCursor >= m_vPendingSkinDirectoryEntries.size())
	{
		m_vPendingSkinDirectoryEntries.clear();
		m_SkinDirectoryMergeCursor = 0;
	}
}

void CSkins::QueueOfficialSkinIndexRequest()
{
	if(m_pOfficialSkinIndexRequest && !m_pOfficialSkinIndexRequest->Done())
		return;
	m_pOfficialSkinIndexRequest = HttpGetFile(OFFICIAL_SKIN_INDEX_URL, Storage(), OFFICIAL_SKIN_INDEX_CACHE_PATH, IStorage::TYPE_SAVE);
	m_pOfficialSkinIndexRequest->Timeout(CTimeout{10000, 0, 8192, 10});
	m_pOfficialSkinIndexRequest->SkipByFileTime(true);
	m_pOfficialSkinIndexRequest->LogProgress(HTTPLOG::NONE);
	m_pOfficialSkinIndexRequest->FailOnErrorStatus(false);
	Http()->Run(m_pOfficialSkinIndexRequest);
}

void CSkins::ProcessOfficialSkinIndexRequest()
{
	if(m_pOfficialSkinIndexRequest == nullptr || !m_pOfficialSkinIndexRequest->Done())
		return;
	const bool Success = m_pOfficialSkinIndexRequest->State() == EHttpState::DONE && m_pOfficialSkinIndexRequest->StatusCode() < 400;
	m_pOfficialSkinIndexRequest.reset();
	if(Success)
	{
		LoadOfficialSkinIndexCache();
	}
}

void CSkins::LoadOfficialSkinIndexCache()
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(OFFICIAL_SKIN_INDEX_CACHE_PATH, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;
	const bool Dirty = ApplyOfficialSkinIndexJson(static_cast<const char *>(pFileData), FileSize);
	free(pFileData);
	if(Dirty)
	{
		m_SkinList.ForceRefresh();
	}
}

bool CSkins::ApplyOfficialSkinIndexJson(const char *pJson, size_t JsonSize)
{
	json_value *pRoot = JsonParse(static_cast<const json_char *>(pJson), JsonSize);
	if(pRoot == nullptr)
		return false;

	bool Dirty = false;
	const json_value &Skins = (*pRoot)["skins"];
	if(Skins.type == json_array)
	{
		for(int i = 0; i < json_array_length(&Skins); ++i)
		{
			const json_value *pEntry = json_array_get(&Skins, i);
			if(pEntry == nullptr || pEntry->type != json_object)
				continue;
			const char *pName = json_string_get(json_object_get(pEntry, "name"));
			const char *pDate = json_string_get(json_object_get(pEntry, "date"));
			const char *pCreator = json_string_get(json_object_get(pEntry, "creator"));
			const int ReleaseDate = ParseOfficialSkinReleaseDateKey(pDate);
			if(pName == nullptr || ReleaseDate == 0 || !CSkin::IsValidName(pName))
				continue;
			auto ExistingSkin = m_Skins.find(pName);
			if(ExistingSkin == m_Skins.end())
			{
				CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::DOWNLOAD, IStorage::TYPE_SAVE);
				auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
				pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
				ExistingSkin = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
				Dirty = true;
			}
			CSkinContainer *pSkinContainer = ExistingSkin->second.get();
			if(pSkinContainer->OfficialReleaseDate() != ReleaseDate)
			{
				pSkinContainer->SetOfficialReleaseDate(ReleaseDate);
				Dirty = true;
			}
			if(pCreator != nullptr && str_comp(pSkinContainer->OfficialCreator(), pCreator) != 0)
			{
				pSkinContainer->SetOfficialCreator(pCreator);
				Dirty = true;
			}
		}
	}
	json_value_free(pRoot);
	return Dirty;
}

void CSkins::ProcessSkinListPlanJob()
{
	if(m_pSkinListPlanJob && m_pSkinListPlanJob->State() == IJob::STATE_DONE)
	{
		auto Result = m_pSkinListPlanJob->TakeResult();
		LogSkinSettingsResourcePerf("complete", (int)Result.m_Plan.m_vNames.size(), (int)Result.m_UnfilteredCount, 0, ESettingsWarmupMissReason::NONE, 0.0);
		if(SettingsSkinListPlanGenerationMatches({Result.m_Generation, Result.m_Plan}, m_SkinListPlanGeneration))
		{
			m_vPendingSkinListMergeEntries = std::move(Result.m_Plan.m_vEntries);
			m_vPendingSkinListEntries.clear();
			m_vPendingSkinListEntries.reserve(m_vPendingSkinListMergeEntries.size());
			m_SkinListMergeCursor = 0;
			m_PendingSkinListUnfilteredCount = Result.m_UnfilteredCount;
			m_HasPendingSkinListMergePlan = true;
			// 不清除 m_NeedsUpdate：让它在合并完成后，如果目录扫描还有新数据，
			// 下一帧能 queue 新 plan job 捕获新皮肤。合并期间 entries 非空不会打断。
		}
		m_pSkinListPlanJob.reset();
	}

	if(m_SkinList.m_NeedsUpdate && m_pSkinListPlanJob == nullptr && m_vPendingSkinListMergeEntries.empty())
	{
		m_vPendingSkinListEntries.clear();
		m_HasPendingSkinListMergePlan = false;
		m_SkinListMergeCursor = 0;
		QueueSkinListPlanJob(m_SkinList.m_Dummy >= 0 ? m_SkinList.m_Dummy : 0);
		m_SkinList.m_NeedsUpdate = false;
		return;
	}

	if(!SettingsSkinListHasPendingMergeWork(m_HasPendingSkinListMergePlan, m_vPendingSkinListMergeEntries.size(), m_vPendingSkinListEntries.size(), m_SkinListMergeCursor))
		return;

	SSettingsResourceMergeBudget MergeBudget;
	MergeBudget.m_MaxListEntries = 64;
	const size_t MergeStartCursor = m_SkinListMergeCursor;
	while(m_SkinListMergeCursor < m_vPendingSkinListMergeEntries.size() && SettingsResourceConsumeMergeEntry(MergeBudget, SettingsFrameBudgetOrNull(GameClient())))
	{
		const SSettingsSkinListEntry &Entry = m_vPendingSkinListMergeEntries[m_SkinListMergeCursor++];
		const auto SkinIt = m_Skins.find(Entry.m_Name);
		if(SkinIt == m_Skins.end())
			continue;

		const std::optional<CSkinListEntry::SColorKey> ColorKey = Entry.m_ColorKey.has_value() ? std::make_optional(MakeSkinListColorKey(Entry.m_ColorKey.value())) : std::nullopt;
		m_vPendingSkinListEntries.push_back(MakeSkinListEntry(SkinIt->second.get(), ColorKey));
	}
	LogSkinSettingsResourcePerf("merge", (int)(m_SkinListMergeCursor - MergeStartCursor), 64, (int)(m_vPendingSkinListMergeEntries.size() - m_SkinListMergeCursor), m_SkinListMergeCursor < m_vPendingSkinListMergeEntries.size() ? ESettingsWarmupMissReason::JOB_RESULT_PENDING : ESettingsWarmupMissReason::NONE, 0.0);

	if(SettingsSkinListShouldPublishMergedList(m_SkinListMergeCursor, m_vPendingSkinListMergeEntries.size()))
	{
		const bool MergeComplete = m_SkinListMergeCursor >= m_vPendingSkinListMergeEntries.size();
		const bool DirectoryScanPending = m_pSkinDirectoryScanJob != nullptr || !m_vPendingSkinDirectoryEntries.empty();
		if(SettingsSkinListShouldReplacePublishedEntries((int)m_SkinList.m_vSkins.size(), (int)m_vPendingSkinListEntries.size(), DirectoryScanPending, MergeComplete))
		{
			m_SkinList.m_vSkins = std::move(m_vPendingSkinListEntries);
			m_SkinList.m_UnfilteredCount = m_PendingSkinListUnfilteredCount;
			++m_SkinList.m_Revision;
		}
		if(MergeComplete)
		{
			m_vPendingSkinListMergeEntries.clear();
			m_vPendingSkinListEntries.clear();
			m_HasPendingSkinListMergePlan = false;
			m_SkinListMergeCursor = 0;
		}
	}
}

CSkins::CSkinListEntry CSkins::MakeSkinListEntry(const CSkinContainer *pSkinContainer, std::optional<CSkinListEntry::SColorKey> ColorKey) const
{
	const CSkinListEntry::SColorKey EffectiveColorKey = ColorKey.value_or(MakeSkinListColorKey(m_SkinList.m_Dummy >= 0 ? m_SkinList.m_Dummy : 0));
	const CSkinListEntry::SColorKey MainColorKey = MakeSkinListColorKey(0);
	const CSkinListEntry::SColorKey DummyColorKey = MakeSkinListColorKey(1);
	const bool Favorite = IsFavorite(pSkinContainer->Name());
	const bool SelectedMain = str_comp(pSkinContainer->Name(), g_Config.m_ClPlayerSkin) == 0 && SkinListColorKeyEquals(EffectiveColorKey, MainColorKey);
	const bool SelectedDummy = str_comp(pSkinContainer->Name(), g_Config.m_ClDummySkin) == 0 && SkinListColorKeyEquals(EffectiveColorKey, DummyColorKey);

	std::optional<std::pair<int, int>> NameMatch;
	if(g_Config.m_ClSkinFilterString[0] != '\0')
	{
		const char *pNameMatchEnd = nullptr;
		const char *pNameMatchStart = str_utf8_find_nocase(pSkinContainer->Name(), g_Config.m_ClSkinFilterString, &pNameMatchEnd);
		if(pNameMatchStart != nullptr)
			NameMatch = std::make_pair<int, int>(pNameMatchStart - pSkinContainer->Name(), pNameMatchEnd - pNameMatchStart);
	}

	return CSkinListEntry(const_cast<CSkinContainer *>(pSkinContainer), Favorite, SelectedMain, SelectedDummy, ColorKey, NameMatch);
}

const CSkin *CSkins::Find(const char *pName)
{
	const auto *pSkin = FindOrNullptr(pName);
	if(pSkin == nullptr)
	{
		pSkin = FindOrNullptr("default");
	}
	if(pSkin == nullptr)
	{
		pSkin = &m_PlaceholderSkin;
	}
	return pSkin;
}

const CSkins::CSkinContainer *CSkins::FindContainerOrNullptr(const char *pName)
{
	const char *pSkinPrefix = SkinPrefix();
	if(pSkinPrefix[0] != '\0')
	{
		char aNameWithPrefix[2 * MAX_SKIN_LENGTH + 2]; // Larger than skin name length to allow IsValidName to check if it's too long
		str_format(aNameWithPrefix, sizeof(aNameWithPrefix), "%s_%s", pSkinPrefix, pName);
		// If we find something, use it, otherwise fall back to normal skins.
		const CSkinContainer *pSkinContainer = FindContainerImpl(aNameWithPrefix);
		if(pSkinContainer != nullptr && pSkinContainer->State() == CSkinContainer::EState::LOADED)
		{
			return pSkinContainer;
		}
	}
	return FindContainerImpl(pName);
}

const CSkins::CSkinContainer *CSkins::FindContainerImpl(const char *pName)
{
	if(!CSkin::IsValidName(pName))
	{
		return nullptr;
	}

	auto ExistingSkin = m_Skins.find(pName);
	if(ExistingSkin == m_Skins.end() ||
		(ExistingSkin->second->State() == CSkinContainer::EState::ERROR || ExistingSkin->second->State() == CSkinContainer::EState::NOT_FOUND))
	{
		const auto CaseInsensitiveSkin = std::find_if(
			m_Skins.begin(),
			m_Skins.end(),
			[this, pName, &ExistingSkin](const auto &Entry) {
				return (ExistingSkin == m_Skins.end() || Entry.second.get() != ExistingSkin->second.get()) &&
				       str_comp_nocase(Entry.first.c_str(), pName) == 0 &&
				       Entry.second->State() != CSkinContainer::EState::ERROR && Entry.second->State() != CSkinContainer::EState::NOT_FOUND;
			});
		if(CaseInsensitiveSkin != m_Skins.end())
			ExistingSkin = CaseInsensitiveSkin;
	}
	if(ExistingSkin == m_Skins.end())
	{
		CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::DOWNLOAD, IStorage::TYPE_SAVE);
		auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
		pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
		ExistingSkin = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
	}
	ExistingSkin->second->RequestLoad(true);
	return ExistingSkin->second.get();
}

const CSkin *CSkins::FindOrNullptr(const char *pName)
{
	const CSkinContainer *pSkinContainer = FindContainerOrNullptr(pName);
	if(pSkinContainer == nullptr || pSkinContainer->m_State != CSkinContainer::EState::LOADED)
	{
		return nullptr;
	}
	return pSkinContainer->m_pSkin.get();
}

void CSkins::AddFavorite(const char *pName)
{
	if(!CSkin::IsValidName(pName))
	{
		log_error("skins", "Favorite skin name '%s' is not valid", pName);
		log_error("skins", "%s", CSkin::m_aSkinNameRestrictions);
		return;
	}

	const auto &[_, Inserted] = m_Favorites.emplace(pName);
	if(Inserted)
	{
		m_SkinList.ForceRefresh();
	}
}

void CSkins::RemoveFavorite(const char *pName)
{
	const auto FavoriteIt = m_Favorites.find(pName);
	if(FavoriteIt != m_Favorites.end())
	{
		m_Favorites.erase(FavoriteIt);
		m_SkinList.ForceRefresh();
	}
}

bool CSkins::IsFavorite(const char *pName) const
{
	return m_Favorites.contains(pName);
}

bool CSkins::IsInSkinQueue(const char *pName, int Dummy) const
{
	return IsInSkinQueue(pName, false, 0, 0, Dummy);
}

bool CSkins::AddSkinQueue(const char *pName, int Dummy)
{
	return AddSkinQueue(pName, false, 0, 0, Dummy);
}

bool CSkins::IsInSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy) const
{
	const auto &Queue = m_aSkinQueue[Dummy];
	const CSkinQueueEntry Entry = MakeSkinQueueEntry(pName, UseCustomColor, ColorBody, ColorFeet);
	return std::find(Queue.begin(), Queue.end(), Entry) != Queue.end();
}

bool CSkins::AddSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)
{
	if(!CSkin::IsValidName(pName))
	{
		log_error("skins", "Queue skin name '%s' is not valid", pName);
		log_error("skins", "%s", CSkin::m_aSkinNameRestrictions);
		return false;
	}

	if(IsInSkinQueue(pName, UseCustomColor, ColorBody, ColorFeet, Dummy))
	{
		return false;
	}

	auto &Queue = m_aSkinQueue[Dummy];
	const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));
	if((int)Queue.size() >= Limit)
	{
		return false;
	}

	Queue.push_back(MakeSkinQueueEntry(pName, UseCustomColor, ColorBody, ColorFeet));
	m_aSkinQueueDirty[Dummy] = true;
	ClampSkinQueueIndex(Dummy);
	m_SkinList.ForceRefresh();
	return true;
}

bool CSkins::AddActiveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)
{
	// The playing queue (m_aSkinQueue) is the only editable workspace now;
	// presets are read-only templates until Save / Save-As.
	return AddSkinQueue(pName, UseCustomColor, ColorBody, ColorFeet, Dummy);
}

bool CSkins::RemoveSkinQueue(const char *pName, int Dummy)
{
	return RemoveSkinQueue(pName, false, 0, 0, Dummy);
}

bool CSkins::RemoveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)
{
	return RemoveSkinQueue(MakeSkinQueueEntry(pName, UseCustomColor, ColorBody, ColorFeet), Dummy);
}

bool CSkins::RemoveSkinQueue(const CSkinQueueEntry &Entry, int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	auto It = std::find(Queue.begin(), Queue.end(), Entry);
	if(It == Queue.end())
	{
		return false;
	}

	const int RemovedIndex = (int)(It - Queue.begin());
	Queue.erase(It);
	int &QueueIndex = SkinQueueIndexVar(Dummy);
	m_aSkinQueueDirty[Dummy] = true;
	if(RemovedIndex < QueueIndex)
	{
		QueueIndex--;
	}
	ClampSkinQueueIndex(Dummy);
	m_SkinList.ForceRefresh();
	return true;
}

bool CSkins::RemoveActiveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)
{
	return RemoveActiveSkinQueue(MakeSkinQueueEntry(pName, UseCustomColor, ColorBody, ColorFeet), Dummy);
}

bool CSkins::RemoveActiveSkinQueue(const CSkinQueueEntry &Entry, int Dummy)
{
	// Editing always targets the playing queue now.
	return RemoveSkinQueue(Entry, Dummy);
}

void CSkins::MoveSkinQueueItem(size_t FromIndex, size_t ToIndex, int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	if(FromIndex >= Queue.size() || ToIndex >= Queue.size() || FromIndex == ToIndex)
	{
		return;
	}

	CSkinQueueEntry Moving = std::move(Queue[FromIndex]);
	Queue.erase(Queue.begin() + FromIndex);
	Queue.insert(Queue.begin() + ToIndex, std::move(Moving));

	int CurrentIndex = SkinQueueIndexVar(Dummy);
	if(CurrentIndex == (int)FromIndex)
	{
		CurrentIndex = (int)ToIndex;
	}
	else if(FromIndex < ToIndex && CurrentIndex > (int)FromIndex && CurrentIndex <= (int)ToIndex)
	{
		CurrentIndex--;
	}
	else if(FromIndex > ToIndex && CurrentIndex >= (int)ToIndex && CurrentIndex < (int)FromIndex)
	{
		CurrentIndex++;
	}
	SkinQueueIndexVar(Dummy) = CurrentIndex;
	m_aSkinQueueDirty[Dummy] = true;
	ClampSkinQueueIndex(Dummy);
	m_SkinList.ForceRefresh();
}

void CSkins::MoveActiveSkinQueueItem(size_t FromIndex, size_t ToIndex, int Dummy)
{
	// Editing always targets the playing queue now.
	MoveSkinQueueItem(FromIndex, ToIndex, Dummy);
}

bool CSkins::ApplySkinQueueIndex(size_t QueueIndex, int Dummy)
{
	if(QueueIndex >= m_aSkinQueue[Dummy].size())
	{
		return false;
	}
	SkinQueueIndexVar(Dummy) = (int)QueueIndex;
	ApplySkinQueueCurrent(Dummy);
	return true;
}

bool CSkins::RandomSkinQueueIndex(int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	if(Queue.empty())
	{
		return false;
	}
	return ApplySkinQueueIndex(rand() % Queue.size(), Dummy);
}

void CSkins::TrimSkinQueueToLimit(int Dummy)
{
	auto &Queue = m_aSkinQueue[Dummy];
	const int Limit = maximum(0, SkinQueueLengthVar(Dummy));
	if((int)Queue.size() > Limit)
	{
		Queue.resize(Limit);
		m_SkinList.ForceRefresh();
	}
	ClampSkinQueueIndex(Dummy);
}

void CSkins::TrimActiveSkinQueueToLimit(int Dummy)
{
	// Editing always targets the playing queue now.
	TrimSkinQueueToLimit(Dummy);
}

bool CSkins::AddSkinQueuePreset(const char *pName, int Dummy)
{
	auto &Presets = m_vSkinQueuePresets;
	if(Presets.size() >= SKIN_QUEUE_PRESET_HARD_LIMIT)
	{
		return false;
	}

	char aPresetName[MAX_SKIN_LENGTH];
	if(pName == nullptr || pName[0] == '\0')
	{
		str_format(aPresetName, sizeof(aPresetName), Localize("Preset %d"), maximum(1, (int)Presets.size() - 1));
		pName = aPresetName;
	}
	str_copy(aPresetName, pName, sizeof(aPresetName));

	const auto ExistingPreset = std::find_if(Presets.begin(), Presets.end(), [aPresetName](const CSkinQueuePreset &Preset) {
		return Preset.Kind() == CSkinQueuePreset::EKind::USER && str_comp(Preset.m_Name.c_str(), aPresetName) == 0;
	});
	if(ExistingPreset != Presets.end())
	{
		return true;
	}

	Presets.push_back({});
	Presets.back().m_Name = aPresetName;
	Presets.back().m_Kind = CSkinQueuePreset::EKind::USER;
	return true;
}

bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, int Dummy)
{
	return AddSkinQueuePresetItem(PresetIndex, pSkinName, false, 0, 0, Dummy);
}

bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy)
{
	auto &Presets = m_vSkinQueuePresets;
	if(PresetIndex < 0 || PresetIndex >= (int)Presets.size())
	{
		return false;
	}
	if(!CSkin::IsValidName(pSkinName))
	{
		return false;
	}

	auto &Queue = Presets[PresetIndex].m_Queue;
	const CSkinQueueEntry Entry = MakeSkinQueueEntry(pSkinName, UseCustomColor, ColorBody, ColorFeet);
	const int Limit = minimum(SKIN_QUEUE_HARD_LIMIT, maximum(0, SkinQueueLengthVar(Dummy)));
	if((int)Queue.size() >= Limit)
	{
		return false;
	}
	if(std::find(Queue.begin(), Queue.end(), Entry) == Queue.end())
	{
		Queue.push_back(Entry);
	}
	return true;
}

bool CSkins::AddSkinQueuePresetFromCurrent(int Dummy)
{
	const auto &Queue = m_aSkinQueue[Dummy];
	if(Queue.empty())
	{
		return false;
	}

	char aPresetName[MAX_SKIN_LENGTH];
	str_format(aPresetName, sizeof(aPresetName), Localize("Preset %d"), maximum(1, (int)m_vSkinQueuePresets.size() - 1));
	AddSkinQueuePreset(aPresetName, Dummy);
	m_vSkinQueuePresets.back().m_Queue = Queue;
	m_aAppliedSkinQueuePresetIndex[Dummy] = (int)m_vSkinQueuePresets.size() - 1;
	m_aSkinQueueDirty[Dummy] = false;
	return true;
}

bool CSkins::RenameSkinQueuePreset(size_t PresetIndex, const char *pName, int Dummy)
{
	auto &Presets = m_vSkinQueuePresets;
	if(PresetIndex >= Presets.size() || pName == nullptr)
	{
		return false;
	}
	if(Presets[PresetIndex].Kind() != CSkinQueuePreset::EKind::USER)
	{
		return false;
	}

	char aTrimmedName[MAX_SKIN_LENGTH];
	str_copy(aTrimmedName, str_utf8_skip_whitespaces(pName), sizeof(aTrimmedName));
	str_utf8_trim_right(aTrimmedName);
	if(aTrimmedName[0] == '\0')
	{
		return false;
	}

	Presets[PresetIndex].m_Name = aTrimmedName;
	return true;
}

void CSkins::ClearSkinQueue(int Dummy)
{
	// Clear the playing queue only. AppliedPresetIndex is kept so clear-then-save
	// still writes back to the preset the queue came from; the cleared state is
	// just marked dirty until saved or another preset is applied.
	m_aSkinQueue[Dummy].clear();
	SkinQueueIndexVar(Dummy) = 0;
	// Disable server rotation so the queue does not immediately refill from
	// map players after being cleared.
	SkinQueueRotateMapVar(Dummy) = 0;
	m_aSkinQueueDirty[Dummy] = true;
	m_aSkinQueueElapsed[Dummy] = 0ns;
	m_aSkinQueueLastUpdate[Dummy].reset();
	m_SkinList.ForceRefresh();
}

bool CSkins::SaveSkinQueueToAppliedPreset(int Dummy)
{
	const int PresetIndex = m_aAppliedSkinQueuePresetIndex[Dummy];
	if(!IsSkinQueuePresetWritable(PresetIndex, m_vSkinQueuePresets.size()))
	{
		// The UI turns a failed save (Server preset or nothing applied) into "Save As".
		return false;
	}
	m_vSkinQueuePresets[PresetIndex].m_Queue = m_aSkinQueue[Dummy];
	m_aSkinQueueDirty[Dummy] = false;
	m_SkinList.ForceRefresh();
	return true;
}

bool CSkins::ApplySkinQueuePreset(size_t PresetIndex, int Dummy)
{
	const auto &Presets = m_vSkinQueuePresets;
	if(PresetIndex >= Presets.size())
	{
		return false;
	}
	// Clicking a preset applies it to the playing queue immediately (the list
	// doubles as the apply control; there is no separate Apply button).
	if(PresetIndex == SKIN_QUEUE_SERVER_PRESET)
	{
		SkinQueueRotateMapVar(Dummy) = 1;
		SyncSkinQueueFromMapPlayers(Dummy);
	}
	else
	{
		m_aSkinQueue[Dummy] = Presets[PresetIndex].m_Queue;
		SkinQueueIndexVar(Dummy) = 0;
		SkinQueueRotateMapVar(Dummy) = Presets[PresetIndex].Kind() == CSkinQueuePreset::EKind::SERVER ? 1 : 0;
	}
	m_aAppliedSkinQueuePresetIndex[Dummy] = (int)PresetIndex;
	m_aSkinQueueDirty[Dummy] = false;
	m_aSkinQueueElapsed[Dummy] = 0ns;
	m_aSkinQueueLastUpdate[Dummy].reset();
	ApplySkinQueueCurrent(Dummy);
	m_SkinList.ForceRefresh();
	return true;
}

bool CSkins::RemoveSkinQueuePreset(size_t PresetIndex, int Dummy)
{
	auto &Presets = m_vSkinQueuePresets;
	if(PresetIndex >= Presets.size())
	{
		return false;
	}
	if(PresetIndex < 2)
	{
		return false;
	}
	Presets.erase(Presets.begin() + PresetIndex);
	for(int PresetDummy = 0; PresetDummy < NUM_DUMMIES; ++PresetDummy)
	{
		const int OldApplied = m_aAppliedSkinQueuePresetIndex[PresetDummy];
		m_aAppliedSkinQueuePresetIndex[PresetDummy] = NextAppliedPresetIndexAfterRemove(OldApplied, (int)PresetIndex);
		if(OldApplied == (int)PresetIndex)
		{
			// The playing queue no longer maps to any preset; flag it so the UI
			// shows the unsaved dot instead of a misleading "clean" state.
			m_aSkinQueueDirty[PresetDummy] = true;
		}
	}
	return true;
}

void CSkins::RandomizeSkin(int Dummy)
{
	static const float s_aSchemes[] = {1.0f / 2.0f, 1.0f / 3.0f, 1.0f / -3.0f, 1.0f / 12.0f, 1.0f / -12.0f}; // complementary, triadic, analogous
	const bool UseCustomColor = Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor;
	if(UseCustomColor)
	{
		float GoalSat = random_float(0.3f, 1.0f);
		float MaxBodyLht = 1.0f - GoalSat * GoalSat; // max allowed lightness before we start losing saturation

		ColorHSLA Body;
		Body.h = random_float();
		Body.l = random_float(0.0f, MaxBodyLht);
		Body.s = std::clamp(GoalSat * GoalSat / (1.0f - Body.l), 0.0f, 1.0f);

		ColorHSLA Feet;
		Feet.h = std::fmod(Body.h + s_aSchemes[rand() % std::size(s_aSchemes)], 1.0f);
		Feet.l = random_float();
		Feet.s = std::clamp(GoalSat * GoalSat / (1.0f - Feet.l), 0.0f, 1.0f);

		unsigned *pColorBody = Dummy ? &g_Config.m_ClDummyColorBody : &g_Config.m_ClPlayerColorBody;
		unsigned *pColorFeet = Dummy ? &g_Config.m_ClDummyColorFeet : &g_Config.m_ClPlayerColorFeet;

		*pColorBody = Body.Pack(false);
		*pColorFeet = Feet.Pack(false);
	}

	std::vector<const CSkinContainer *> vpConsideredSkins;
	for(const auto &[_, pSkinContainer] : m_Skins)
	{
		if(pSkinContainer->m_State == CSkinContainer::EState::ERROR ||
			pSkinContainer->m_State == CSkinContainer::EState::NOT_FOUND ||
			pSkinContainer->IsSpecial())
		{
			continue;
		}
		vpConsideredSkins.push_back(pSkinContainer.get());
	}
	const char *pRandomSkin;
	if(vpConsideredSkins.empty())
	{
		pRandomSkin = "default";
	}
	else
	{
		pRandomSkin = vpConsideredSkins[rand() % vpConsideredSkins.size()]->Name();
	}

	char *pSkinName = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const size_t SkinNameSize = Dummy ? sizeof(g_Config.m_ClDummySkin) : sizeof(g_Config.m_ClPlayerSkin);
	str_copy(pSkinName, pRandomSkin, SkinNameSize);
	m_SkinList.ForceRefresh();
}

const char *CSkins::SkinPrefix() const
{
	if(g_Config.m_ClVanillaSkinsOnly)
	{
		return "";
	}
	if(m_aEventSkinPrefix[0] != '\0')
	{
		return m_aEventSkinPrefix;
	}
	return g_Config.m_ClSkinPrefix;
}

void CSkins::CSkinLoadJob::Run()
{
	const std::chrono::nanoseconds DecodeStart = time_get_nanoseconds();
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", m_aName);

	if(State() == IJob::STATE_ABORTED)
	{
		return;
	}

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pSkins->Storage()->ReadFile(aPath, m_StorageType, &pFileData, &FileSize))
	{
		log_error("skins", "Failed to read skin file '%s'", aPath);
		return;
	}

	if(State() == IJob::STATE_ABORTED)
	{
		free(pFileData);
		return;
	}

	const bool LoadSuccess = CImageLoader::LoadPng(pFileData, FileSize, aPath, m_Data.m_Info);
	free(pFileData);

	if(!LoadSuccess)
	{
		log_error("skins", "Failed to decode skin PNG '%s'", aPath);
		return;
	}

	if(State() == IJob::STATE_ABORTED)
	{
		return;
	}
	if(PrepareSkinData(m_aName, m_Data))
	{
		LogSettingsSkinSourceStageEvent("decode_done", m_aName, m_Data.m_SourceWidth, m_Data.m_SourceHeight, (int)FileSize, std::chrono::duration<double, std::milli>(time_get_nanoseconds() - DecodeStart).count());
	}
}

CSkins::CSkinDownloadJob::CSkinDownloadJob(CSkins *pSkins, const char *pName) :
	CAbstractSkinLoadJob(pSkins, pName)
{
}

bool CSkins::CSkinDownloadJob::Abort()
{
	if(!CAbstractSkinLoadJob::Abort())
	{
		return false;
	}

	const CLockScope LockScope(m_Lock);
	if(m_pGetRequest)
	{
		m_pGetRequest->Abort();
		m_pGetRequest = nullptr;
	}
	return true;
}

void CSkins::CSkinDownloadJob::Run()
{
	const char *pBaseUrl = g_Config.m_ClDownloadCommunitySkins != 0 ? g_Config.m_ClSkinCommunityDownloadUrl : g_Config.m_ClSkinDownloadUrl;

	char aEscapedName[256];
	EscapeUrl(aEscapedName, m_aName);

	char aUrl[IO_MAX_PATH_LENGTH];
	str_format(aUrl, sizeof(aUrl), "%s%s.png", pBaseUrl, aEscapedName);

	char aPathReal[IO_MAX_PATH_LENGTH];
	str_format(aPathReal, sizeof(aPathReal), "downloadedskins/%s.png", m_aName);

	const CTimeout Timeout{10000, 0, 8192, 10};
	const size_t MaxResponseSize = 10 * 1024 * 1024; // 10 MiB

	std::shared_ptr<IHttpRequest> pGet = HttpGetBoth(aUrl, m_pSkins->Storage(), aPathReal, IStorage::TYPE_SAVE);
	pGet->Timeout(Timeout);
	pGet->MaxResponseSize(MaxResponseSize);
	pGet->ValidateBeforeOverwrite(true);
	pGet->LogProgress(HTTPLOG::NONE);
	pGet->FailOnErrorStatus(false);
	{
		const CLockScope LockScope(m_Lock);
		m_pGetRequest = pGet;
	}
	m_pSkins->Http()->Run(pGet);

	// Load existing file while waiting for the HTTP request
	{
		void *pPngData;
		unsigned PngSize;
		if(m_pSkins->Storage()->ReadFile(aPathReal, IStorage::TYPE_SAVE, &pPngData, &PngSize))
		{
			if(CImageLoader::LoadPng(pPngData, PngSize, aPathReal, m_Data.m_Info))
			{
				if(State() == IJob::STATE_ABORTED)
				{
					return;
				}
				PrepareSkinData(m_aName, m_Data);
			}
			free(pPngData);
		}
	}

	pGet->Wait();
	{
		const CLockScope LockScope(m_Lock);
		m_pGetRequest = nullptr;
	}
	if(pGet->State() != EHttpState::DONE || State() == IJob::STATE_ABORTED || pGet->StatusCode() >= 400)
	{
		m_NotFound = pGet->State() == EHttpState::DONE && pGet->StatusCode() == 404; // 404 Not Found
		return;
	}
	if(pGet->StatusCode() == 304) // 304 Not Modified
	{
		bool Success = m_Data.m_pPreparedTextures != nullptr;
		pGet->OnValidation(Success);
		if(Success)
		{
			return; // Local skin is up-to-date and was loaded successfully
		}

		log_error("skins", "Failed to load PNG of existing downloaded skin '%s' from '%s', downloading it again", m_aName, aPathReal);
		pGet = HttpGetBoth(aUrl, m_pSkins->Storage(), aPathReal, IStorage::TYPE_SAVE);
		pGet->Timeout(Timeout);
		pGet->MaxResponseSize(MaxResponseSize);
		pGet->ValidateBeforeOverwrite(true);
		pGet->SkipByFileTime(false);
		pGet->LogProgress(HTTPLOG::NONE);
		pGet->FailOnErrorStatus(false);
		{
			const CLockScope LockScope(m_Lock);
			m_pGetRequest = pGet;
		}
		m_pSkins->Http()->Run(pGet);
		pGet->Wait();
		{
			const CLockScope LockScope(m_Lock);
			m_pGetRequest = nullptr;
		}
		if(pGet->State() != EHttpState::DONE || State() == IJob::STATE_ABORTED || pGet->StatusCode() >= 400)
		{
			m_NotFound = pGet->State() == EHttpState::DONE && pGet->StatusCode() == 404; // 404 Not Found
			return;
		}
	}

	unsigned char *pResult;
	size_t ResultSize;
	pGet->Result(&pResult, &ResultSize);

	m_Data.m_Info.Free();
	m_Data.m_InfoGrayscale.Free();
	m_Data.m_pPreparedTextures.reset();
	const bool Success = CImageLoader::LoadPng(pResult, ResultSize, aUrl, m_Data.m_Info);
	if(Success)
	{
		if(State() == IJob::STATE_ABORTED)
		{
			return;
		}
		PrepareSkinData(m_aName, m_Data);
	}
	else
	{
		log_error("skins", "Failed to load PNG of skin '%s' downloaded from '%s' (size %" PRIzu ")", m_aName, aUrl, ResultSize);
	}
	pGet->OnValidation(Success);
}

void CSkins::ConAddFavoriteSkin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddFavorite(pResult->GetString(0));
}

void CSkins::ConRemFavoriteSkin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->RemoveFavorite(pResult->GetString(0));
}

void CSkins::ConAddSkinQueue(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueue(pResult->GetString(0), 0);
}

void CSkins::ConAddDummySkinQueue(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueue(pResult->GetString(0), 1);
}

void CSkins::ConAddSkinQueueEx(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueue(pResult->GetString(0), pResult->GetInteger(1) != 0, pResult->GetInteger(2), pResult->GetInteger(3), 0);
}

void CSkins::ConAddDummySkinQueueEx(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueue(pResult->GetString(0), pResult->GetInteger(1) != 0, pResult->GetInteger(2), pResult->GetInteger(3), 1);
}

void CSkins::ConRandomSkinQueue(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->RandomSkinQueueIndex(0);
}

void CSkins::ConRandomDummySkinQueue(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->RandomSkinQueueIndex(1);
}

void CSkins::ConAddSkinQueuePreset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePreset(pResult->GetString(0), 0);
}

void CSkins::ConAddDummySkinQueuePreset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePreset(pResult->GetString(0), 0);
}

void CSkins::ConAddSkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePresetItem(pResult->GetInteger(0), pResult->GetString(1), 0);
}

void CSkins::ConAddDummySkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePresetItem(pResult->GetInteger(0), pResult->GetString(1), 0);
}

void CSkins::ConAddSkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePresetItem(pResult->GetInteger(0), pResult->GetString(1), pResult->GetInteger(2) != 0, pResult->GetInteger(3), pResult->GetInteger(4), 0);
}

void CSkins::ConAddDummySkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->AddSkinQueuePresetItem(pResult->GetInteger(0), pResult->GetString(1), pResult->GetInteger(2) != 0, pResult->GetInteger(3), pResult->GetInteger(4), 0);
}

void CSkins::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->OnConfigSave(pConfigManager);
}

void CSkins::OnConfigSave(IConfigManager *pConfigManager)
{
	for(const auto &Favorite : m_Favorites)
	{
		char aBuffer[32 + MAX_SKIN_LENGTH];
		str_format(aBuffer, sizeof(aBuffer), "add_favorite_skin \"%s\"", Favorite.c_str());
		pConfigManager->WriteLine(aBuffer);
	}
}

void CSkins::ConfigSaveQueueCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CSkins *>(pUserData);
	pSelf->OnQueueConfigSave(pConfigManager);
}

void CSkins::OnQueueConfigSave(IConfigManager *pConfigManager)
{
	const auto WriteQueueEntry = [pConfigManager](const CSkinQueueEntry &Entry, bool Dummy, int PresetIndex) {
		char aBuffer[160 + MAX_SKIN_LENGTH];
		if(PresetIndex < 0)
		{
			if(Entry.m_UseCustomColor)
			{
				str_format(aBuffer, sizeof(aBuffer), "%s \"%s\" %d %d %d",
					Dummy ? "add_dummy_skin_queue_ex" : "add_skin_queue_ex",
					Entry.m_SkinName.c_str(),
					Entry.m_UseCustomColor ? 1 : 0,
					Entry.m_ColorBody,
					Entry.m_ColorFeet);
			}
			else
			{
				str_format(aBuffer, sizeof(aBuffer), "%s \"%s\"",
					Dummy ? "add_dummy_skin_queue" : "add_skin_queue",
					Entry.m_SkinName.c_str());
			}
		}
		else if(Entry.m_UseCustomColor)
		{
			str_format(aBuffer, sizeof(aBuffer), "%s %d \"%s\" %d %d %d",
				"add_skin_queue_preset_item_ex",
				PresetIndex,
				Entry.m_SkinName.c_str(),
				Entry.m_UseCustomColor ? 1 : 0,
				Entry.m_ColorBody,
				Entry.m_ColorFeet);
		}
		else
		{
			str_format(aBuffer, sizeof(aBuffer), "%s %d \"%s\"",
				"add_skin_queue_preset_item",
				PresetIndex,
				Entry.m_SkinName.c_str());
		}
		pConfigManager->WriteLine(aBuffer, ConfigDomain::QMCLIENT);
	};

	for(const auto &QueueSkin : m_aSkinQueue[0])
	{
		WriteQueueEntry(QueueSkin, false, -1);
	}
	for(const auto &QueueSkin : m_aSkinQueue[1])
	{
		WriteQueueEntry(QueueSkin, true, -1);
	}

	for(size_t QueuePresetIndex = 0; QueuePresetIndex < m_vSkinQueuePresets.size(); ++QueuePresetIndex)
	{
		const auto &Preset = m_vSkinQueuePresets[QueuePresetIndex];
		if(Preset.Kind() != CSkinQueuePreset::EKind::USER)
		{
			continue;
		}
		if(QueuePresetIndex != SKIN_QUEUE_DEFAULT_PRESET)
		{
			char aBuffer[64 + MAX_SKIN_LENGTH];
			str_format(aBuffer, sizeof(aBuffer), "add_skin_queue_preset \"%s\"", Preset.m_Name.c_str());
			pConfigManager->WriteLine(aBuffer, ConfigDomain::QMCLIENT);
		}

		for(const auto &QueueSkin : Preset.m_Queue)
			WriteQueueEntry(QueueSkin, false, (int)QueuePresetIndex);
	}
}

void CSkins::ConchainRefreshSkinList(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CSkins *pThis = static_cast<CSkins *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		pThis->m_SkinList.ForceRefresh();
	}
}
