/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "skins.h"

#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/http_response_validation.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/bounded_file_reader.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/qm_skin_outline.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/components/qmclient/skin_usage_tracking.h>
#include <game/client/components/settings_runtime_cache.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <new>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

static constexpr int SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 1000;
static constexpr const char *OFFICIAL_SKIN_INDEX_URL = "https://ddnet.org/skins/skin/skins.json";
static constexpr const char *OFFICIAL_SKIN_INDEX_CACHE_PATH = "downloadedskins/official_skins.json";
static constexpr unsigned SKIN_CATALOG_MAX_BYTES = 16 * 1024 * 1024;
static constexpr const char *DEFAULT_OFFICIAL_SKIN_URL = "https://skins.ddnet.org/skin/";
static constexpr const char *DEFAULT_COMMUNITY_SKIN_URL = "https://skins.ddnet.org/skin/community/";
static constexpr const char *DEFAULT_DDSTATS_SKIN_URL = "https://skins.ddstats.tw/";
static constexpr int SKIN_CATALOG_MERGE_BUDGET = 256;
// 来源登记可能在一次下载批次里连续到达；等一小段静默再重建计划，
// 避免把一次目录加载拆成多次 generation 重置。
static constexpr std::chrono::milliseconds SKIN_LIST_SOURCE_REFRESH_SETTLE_TIME{250};
static constexpr int MAX_ACTIVE_SKIN_DOWNLOAD_SESSIONS = 48;
static constexpr int MAX_BACKGROUND_SKIN_DOWNLOAD_SESSIONS = 16;
static constexpr int MAX_WAIT_LOG_THROTTLES = 8;
static constexpr int64_t SKIN_SOURCE_WAIT_LOG_INTERVAL_NS = 1'000'000'000;
static constexpr int64_t SKIN_SOURCES_SAVE_INTERVAL_NS = 1'000'000'000;

static std::string SkinUrlIdentity(const char *pUrl)
{
	if(pUrl == nullptr)
		return {};
	std::string Url(pUrl);
	while(!Url.empty() && (Url.back() == ' ' || Url.back() == '\t' || Url.back() == '\r' || Url.back() == '\n'))
		Url.pop_back();
	while(!Url.empty() && Url.front() == ' ')
		Url.erase(Url.begin());
	while(Url.size() > 1 && Url.back() == '/')
		Url.pop_back();
	return Url;
}

static bool SameSkinUrl(const char *pLeft, const char *pRight)
{
	return str_comp_nocase(SkinUrlIdentity(pLeft).c_str(), SkinUrlIdentity(pRight).c_str()) == 0;
}

static std::string SkinCatalogCachePath(const char *pKind, const char *pUrl)
{
	const std::string Identity = SkinUrlIdentity(pUrl);
	const SHA256_DIGEST Digest = sha256(Identity.data(), Identity.size());
	char aDigest[SHA256_MAXSTRSIZE];
	sha256_str(Digest, aDigest, sizeof(aDigest));
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "downloadedskins/%s_%s.html", pKind, aDigest);
	return aPath;
}

static qm_skin_catalog::EFormat DetectDirectoryCatalogFormat(const char *pUrl)
{
	if(pUrl != nullptr && str_startswith_nocase(pUrl, DEFAULT_DDSTATS_SKIN_URL) != nullptr)
		return qm_skin_catalog::EFormat::DDSTATS_HTML;
	return qm_skin_catalog::EFormat::DIRECTORY_HTML;
}

static ESkinSource ClassifyDownloadSource(ESkinSource Tier, const char *pUrl)
{
	if(pUrl == nullptr || pUrl[0] == '\0')
		return Tier;
	if(str_startswith_nocase(pUrl, DEFAULT_DDSTATS_SKIN_URL) != nullptr)
		return ESkinSource::DDSTATS;
	if(str_comp_nocase(pUrl, DEFAULT_OFFICIAL_SKIN_URL) == 0)
		return ESkinSource::OFFICIAL;
	if(str_comp_nocase(pUrl, DEFAULT_COMMUNITY_SKIN_URL) == 0)
		return ESkinSource::COMMUNITY;
	return Tier == ESkinSource::OFFICIAL ? ESkinSource::CUSTOM_OFFICIAL : ESkinSource::CUSTOM_COMMUNITY;
}

static ESkinSource SkinSourceForScanEntry(const CSkinSourceRegistry &Registry, bool Local, const char *pName)
{
	if(Local)
		return ESkinSource::LOCAL;
	return Registry.Find(pName).value_or(ESkinSource::UNKNOWN);
}

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
	struct SWaitLogThrottle
	{
		int64_t m_LastNs = 0;
		char m_aReason[32] = {};
		int m_Suppressed = 0;
	};
	// 每个等待原因独立限流：原因交替出现时，单槽实现会每次都换原因而绕过限制。
	static SWaitLogThrottle s_aThrottles[MAX_WAIT_LOG_THROTTLES];
	const char *pThrottleReason = pReason != nullptr ? pReason : "none";
	SWaitLogThrottle *pThrottle = nullptr;
	for(SWaitLogThrottle &Throttle : s_aThrottles)
	{
		if(str_comp(Throttle.m_aReason, pThrottleReason) == 0)
		{
			pThrottle = &Throttle;
			break;
		}
	}
	if(pThrottle == nullptr)
	{
		for(SWaitLogThrottle &Throttle : s_aThrottles)
		{
			if(Throttle.m_aReason[0] == '\0')
			{
				pThrottle = &Throttle;
				break;
			}
		}
	}
	if(pThrottle == nullptr)
	{
		int OldestIndex = 0;
		for(int Index = 1; Index < MAX_WAIT_LOG_THROTTLES; ++Index)
		{
			if(s_aThrottles[Index].m_LastNs < s_aThrottles[OldestIndex].m_LastNs)
				OldestIndex = Index;
		}
		pThrottle = &s_aThrottles[OldestIndex];
		*pThrottle = {};
	}
	const int64_t NowNs = time_get_nanoseconds().count();
	if(!SettingsSkinWaitLogShouldEmit(NowNs, pThrottle->m_LastNs, SKIN_SOURCE_WAIT_LOG_INTERVAL_NS))
	{
		++pThrottle->m_Suppressed;
		return;
	}
	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "event=source_wait skin=%s artifact=source reason=%s remaining_uploads=%d max_uploads=%d suppressed=%d",
		pSkinName != nullptr ? pSkinName : "",
		pReason != nullptr ? pReason : "none",
		RemainingUploads,
		MaxUploads,
		pThrottle->m_Suppressed);
	QmPerfLogPayload("perf/settings-skin-source", aPayload);
	pThrottle->m_LastNs = NowNs;
	str_copy(pThrottle->m_aReason, pThrottleReason, sizeof(pThrottle->m_aReason));
	pThrottle->m_Suppressed = 0;
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

static float SettingsSkinTargetFrameMs()
{
	// 没有显式渲染上限时按 144Hz 计：对 60/75/120Hz 更宽松但不会越帧，
	// 对 144Hz 正好守住一次刷新间隔。
	const int RefreshRate = g_Config.m_GfxRefreshRate > 0 ? g_Config.m_GfxRefreshRate : 144;
	return 1000.0f / (float)maximum(RefreshRate, 30);
}

// 准入与回收必须共用同一个驻留上限，否则会出现“按 512 准入、按 128 回收”。
static int SettingsSkinEffectiveCountFuseLimit(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	const int ConfiguredLimit = maximum(0, g_Config.m_ClSkinsLoadedMax);
	const size_t ResidencyLimit = TeeSettingsActive ?
					      (size_t)ConfiguredLimit :
					      SettingsSkinInactiveResidencyLimit((size_t)ConfiguredLimit);
	return SettingsSkinSourceCountFuseLimit(Context, TeeSettingsActive, (int)ResidencyLimit);
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

CSkins::CSkinLoadJob::CSkinLoadJob(CSkins *pSkins, const char *pName, int StorageType) :
	CAbstractSkinLoadJob(pName),
	m_pSkins(pSkins),
	m_StorageType(StorageType)
{
}

CSkins::CSkinListPlanJob::CSkinListPlanJob(std::vector<SSkinListSnapshotEntry> vEntries, std::string Filter, int Generation, int SortMode) :
	m_vEntries(std::move(vEntries)),
	m_Filter(std::move(Filter)),
	m_SortMode(SortMode)
{
	m_Result.m_Generation = Generation;
	Abortable(true);
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

	const std::string NameKey = CSkinNameIndex::Key(aSkinName);
	const int Priority = pSelf->m_CurrentScanType == CSkinContainer::EType::LOCAL ? 1 : 0;
	const auto Existing = pSelf->m_EntryByKey.find(NameKey);
	if(Existing != pSelf->m_EntryByKey.end())
	{
		CSkinDirectoryScanJob::SResult::SEntry &Entry = pSelf->m_Result.m_vEntries[Existing->second];
		const int ExistingPriority = Entry.m_Type == CSkinContainer::EType::LOCAL ? 1 : 0;
		if(PreferScannedSkin(Entry.m_Name.c_str(), Entry.m_StorageType, ExistingPriority, aSkinName, StorageType, Priority))
			Entry = {aSkinName, pSelf->m_CurrentScanType, StorageType, pInfo->m_TimeModified};
		return 0;
	}
	pSelf->m_EntryByKey.emplace(NameKey, pSelf->m_Result.m_vEntries.size());
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
		const char *pNameMatchStart = nullptr;
		const char *pNameMatchEnd = nullptr;
		if(!m_Filter.empty())
		{
			pNameMatchStart = str_utf8_find_nocase(Entry.m_Name.c_str(), m_Filter.c_str(), &pNameMatchEnd);
			if(pNameMatchStart == nullptr)
				continue;
		}

		SSettingsSkinListEntry PlanEntry;
		PlanEntry.m_Name = Entry.m_Name;
		PlanEntry.m_Selected = Entry.m_SelectedMain || Entry.m_SelectedDummy;
		PlanEntry.m_SelectedMain = Entry.m_SelectedMain;
		PlanEntry.m_SelectedDummy = Entry.m_SelectedDummy;
		PlanEntry.m_Favorite = Entry.m_Favorite;
		PlanEntry.m_ColorKey = Entry.m_ColorKey.has_value() ? std::make_optional(MakeSettingsSkinListColorKey(Entry.m_ColorKey.value())) : std::nullopt;
		PlanEntry.m_OfficialReleaseDate = Entry.m_OfficialReleaseDate;
		PlanEntry.m_LastModified = Entry.m_LastModified;
		if(pNameMatchStart != nullptr)
		{
			PlanEntry.m_NameMatchStart = (int)(pNameMatchStart - Entry.m_Name.c_str());
			PlanEntry.m_NameMatchLength = (int)(pNameMatchEnd - pNameMatchStart);
		}
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
		EnsureAlwaysLoaded();
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
		EnsureAlwaysLoaded();
		return;
	}
	// 目录条目可能有数千个；后台预热只处理实际下载过和明确引用的皮肤，
	// 不能让一次设置页预热把整个远端目录全部抓下来。
	if(Priority == ESettingsResourcePriority::BACKGROUND && m_CatalogOnly)
		return;
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
		// 等待中的可见需求不能被稍后到达的预取请求降级。
		if(m_State == EState::BACKGROUND_REQUESTED && !SettingsResourcePriorityCanUpgrade(Priority, m_LoadPriority))
			Priority = m_LoadPriority;
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

void CSkins::CSkinContainer::EnsureAlwaysLoaded()
{
	if(!AlwaysLoadedSkinNeedsImmediateLoad(m_State))
	{
		return;
	}
	// Built-in skins can only come from the local skins/ directory. The container may
	// already have been created by Find/queue handling before CSkins::OnInit, and
	// RequestLoad used to be a no-op for always-loaded skins: once such a skin stopped in
	// a transient state it had no load entry left, so the settings page spun forever and
	// render infos fell back to the untextured placeholder. Reuse the container and load
	// it synchronously from the local source.
	m_pSkins->LoadSkinDirect(Name());
}

CSkins::CSkinContainer::EState CSkins::CSkinContainer::DetermineInitialState() const
{
	const bool VanillaSkinsOnly = VanillaSkinsOnlyActive(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0);
	return InitialStateForSource(m_Type, m_AlwaysLoaded, !VanillaSkinsOnly || m_Vanilla,
		SourceDownloadAllowed(EffectiveRequestSources()), m_LastModified != 0, m_CatalogOnly);
}

ESkinSourceMask CSkins::CSkinContainer::EffectiveSkinSources() const
{
	ESkinSourceMask Sources = m_CatalogSources;
	if(m_Type == EType::LOCAL)
	{
		if(m_Vanilla)
			Sources |= ESkinSourceMask::OFFICIAL | ESkinSourceMask::VANILLA;
	}
	Sources |= SkinSourceMaskFromSource(m_SkinSource);
	return Sources;
}

ESkinSourceMask CSkins::CSkinContainer::EffectiveRequestSources() const
{
	ESkinSourceMask Sources = m_CatalogRequestSources;
	if(m_Type == EType::LOCAL && m_Vanilla)
		Sources |= ESkinSourceMask::OFFICIAL;
	Sources |= SkinSourceMaskFromSource(m_SkinSource);
	return Sources;
}

void CSkins::CSkinContainer::SetState(EState State, ESettingsResourcePriority Priority)
{
	const EState OldState = m_State;
	if(State == EState::UNLOADED || State == EState::ERROR || State == EState::NOT_FOUND)
	{
		m_pDownloadSession.reset();
		if(m_pLoadJob)
		{
			m_pLoadJob->Abort();
			m_pLoadJob.reset();
		}
	}
	if(!m_StateCounted || OldState != State)
	{
		if(m_StateCounted)
			--m_pSkins->m_LoadingStats.StateCount(OldState);
		m_pSkins->m_LoadingStats.AddState(State);
		m_StateCounted = true;
	}
	if(State == EState::UNLOADED && m_pSkin != nullptr)
		m_pSkins->UnloadLoadedSkinTextures(this);
	if(ShouldDiscardPendingUpload(OldState, State) || (m_pPendingSkin && State != EState::LOADED && State != EState::LOADING))
		m_pSkins->DiscardSkinPreviewUpload(this);
	m_State = State;
	if(State == EState::LOADED)
		m_CatalogOnly = false;
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
	QmTouchSkinUsageEntry(m_pSkins->m_SkinsUsageList, m_UsageEntryIterator,
		m_pSkins->m_SkinsBackgroundList, m_BackgroundEntryIterator, Name());
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
	const ESkinSortOrder Order = SkinListValueOrder(
		m_Favorite, Other.m_Favorite,
		m_pSkinContainer->OfficialReleaseDate(), Other.m_pSkinContainer->OfficialReleaseDate(),
		m_pSkinContainer->LastModified(), Other.m_pSkinContainer->LastModified(),
		m_pSkinContainer->Type() == CSkinContainer::EType::LOCAL,
		Other.m_pSkinContainer->Type() == CSkinContainer::EType::LOCAL,
		g_Config.m_QmSkinSortMode == 1);
	if(Order != ESkinSortOrder::EQUAL_KEYS)
		return Order == ESkinSortOrder::LESS;
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
	return PreferredContainerType(pName) == CSkinContainer::EType::LOCAL;
}

static int ContainerStorageTypeFor(CSkins::CSkinContainer::EType Type)
{
	return Type == CSkins::CSkinContainer::EType::LOCAL ? IStorage::TYPE_ALL : IStorage::TYPE_SAVE;
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
	if(Data.m_InfoGrayscale.m_pData == nullptr)
	{
		log_error("skins", "Failed to allocate grayscale skin: %s", pName);
		return false;
	}
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
	if(BeginSkinPreviewUpload(pSkinContainer, std::move(Data)))
	{
		while(pSkinContainer->m_SettingsPendingUploadSprite < SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS &&
			UploadNextSkinPreviewSprite(pSkinContainer))
		{
		}
		if(pSkinContainer->m_SettingsPendingUploadSprite == SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)
		{
			const bool ReplacingSkin = pSkinContainer->m_pSkin != nullptr;
			FinishSkinPreviewUpload(pSkinContainer);
			if(ReplacingSkin)
				QueueSkinTexturesUnloaded(pSkinContainer->Name());
			if(g_Config.m_Debug)
				log_trace("skins", "Loaded skin '%s'", pSkinContainer->Name());
			return;
		}
	}

	DiscardSkinPreviewUpload(pSkinContainer);
	pSkinContainer->SetState(pSkinContainer->m_pSkin ? CSkinContainer::EState::LOADED : CSkinContainer::EState::ERROR);
	log_error("skins", "Failed to upload skin '%s'", pSkinContainer->Name());
}

bool CSkins::BeginSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadData &&Data)
{
	if(pSkinContainer == nullptr)
		return false;
	std::unique_ptr<CSkin> pSkin(new(std::nothrow) CSkin(pSkinContainer->Name()));
	if(!pSkin)
		return false;
	pSkinContainer->m_SettingsPendingUploadData = std::move(Data);
	pSkinContainer->m_SettingsPendingUploadSprite = 0;
	pSkinContainer->m_SettingsPendingUploadStart = time_get_nanoseconds();
	pSkinContainer->m_pPendingSkin = std::move(pSkin);
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
	pSkinContainer->m_SettingsPendingUploadData = {};
	pSkinContainer->m_SettingsPendingUploadSprite = 0;
	pSkinContainer->m_SettingsPendingUploadStart = {};
}

static bool LoadSkinSpriteTexture(IGraphics *pGraphics, IGraphics::CTextureHandle *pTargetTexture, CImageInfo &PreparedImage, const CImageInfo &SourceImage, bool PreparedAvailable, const CDataSprite *pSprite)
{
	if(pGraphics == nullptr || pTargetTexture == nullptr)
		return false;
	IGraphics::CTextureHandle Texture;
	try
	{
		Texture = PreparedAvailable ?
				  pGraphics->LoadTextureRawMove(PreparedImage, 0, pSprite->m_pName) :
				  pGraphics->LoadSpriteTexture(SourceImage, std::nullopt, pSprite);
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	if(!Texture.IsValid() || Texture.IsNullTexture())
		return false;
	if(pTargetTexture->IsValid())
		pGraphics->UnloadTexture(pTargetTexture);
	*pTargetTexture = Texture;
	return true;
}

bool CSkins::UploadNextSkinPreviewSprite(CSkinContainer *pSkinContainer)
{
	if(pSkinContainer == nullptr || pSkinContainer->m_pPendingSkin == nullptr)
		return false;

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
	if(pSkinContainer->m_SettingsPendingUploadData.m_LastModified.has_value())
		pSkinContainer->SetLastModified(pSkinContainer->m_SettingsPendingUploadData.m_LastModified.value());
	SkinIt->second->m_SettingsSourceApproxBytes = SettingsSkinSourceBytesEstimate((int)pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth, (int)pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, 2);
	if(pSkinContainer->m_pSkin)
	{
		pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());
		pSkinContainer->m_pSkin->m_ColorableSkin.Unload(Graphics());
	}
	pSkinContainer->m_pSkin = std::move(pSkinContainer->m_pPendingSkin);
	pSkinContainer->m_LoadedSource = SQmSkinSourceIdentity{pSkinContainer->Type() == CSkinContainer::EType::DOWNLOAD, pSkinContainer->StorageType(), pSkinContainer->LastModified()};
	pSkinContainer->SetState(CSkinContainer::EState::LOADED, BackgroundTracked ? ESettingsResourcePriority::BACKGROUND : ESettingsResourcePriority::VISIBLE);
	LogSettingsSkinSourceStageEvent("upload_done", pSkinContainer->Name(), pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth, pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, (int)SkinIt->second->m_SettingsSourceApproxBytes, std::chrono::duration<double, std::milli>(time_get_nanoseconds() - pSkinContainer->m_SettingsPendingUploadStart).count(), SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS);
	// 已发布皮肤持有头像和描边，清除临时副本才能在回收皮肤时释放素材。
	DiscardSkinPreviewUpload(pSkinContainer);
}

void CSkins::LoadSkinDirect(const char *pName)
{
	auto SkinIt = m_Skins.find(pName);
	if(SkinIt != m_Skins.end())
	{
		CSkinContainer *pExisting = SkinIt->second.get();
		if(pExisting->m_State == CSkinContainer::EState::LOADED ||
			pExisting->m_State == CSkinContainer::EState::LOADING)
		{
			return;
		}
		// The container may already exist because Find/queue handling created it before
		// CSkins::OnInit. Built-in skins must be reloaded from the local source, otherwise
		// they stay PENDING and RequestLoad no longer accepts them, so the settings page
		// spins forever.
		pExisting->m_pDownloadSession.reset();
		if(pExisting->m_pLoadJob)
		{
			pExisting->m_pLoadJob->Abort();
			pExisting->m_pLoadJob = nullptr;
		}
		pExisting->m_Type = CSkinContainer::EType::LOCAL;
		pExisting->m_StorageType = IStorage::TYPE_ALL;
	}
	else
	{
		CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::LOCAL, IStorage::TYPE_ALL);
		auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
		pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
		SkinIt = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
	}
	CSkinContainer *pSkinContainer = SkinIt->second.get();

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", pName);
	CSkinLoadData DefaultSkinData;
	pSkinContainer->SetState(CSkinContainer::EState::LOADING);
	bool Prepared = false;
	try
	{
		DefaultSkinData.ReadLastModified(Storage(), aPath, pSkinContainer->StorageType());
		if(Graphics()->LoadPng(DefaultSkinData.m_Info, aPath, pSkinContainer->StorageType()))
			Prepared = PrepareSkinData(pName, DefaultSkinData);
		else
			log_error("skins", "Failed to load PNG of skin '%s' from '%s'", pName, aPath);
	}
	catch(const std::bad_alloc &)
	{
		log_error("skins", "Failed to prepare skin '%s': out of memory", pName);
	}
	if(Prepared)
		LoadSkinFinish(pSkinContainer, DefaultSkinData);
	else
		pSkinContainer->SetState(pSkinContainer->m_pSkin ? CSkinContainer::EState::LOADED : CSkinContainer::EState::ERROR);
}

void CSkins::OnConsoleInit()
{
	ConfigManager()->RegisterCallback(CSkins::ConfigSaveCallback, this);
	ConfigManager()->RegisterCallback(CSkins::ConfigSaveQueueCallback, this, ConfigDomain::QMCLIENT);
	ConfigManager()->RegisterCallback(CSkins::ConfigSaveRecentSkinsCallback, this, ConfigDomain::QMCLIENT);
	Console()->Register("qm_recent_tee_skin", "s[skin_name] i[use_custom_color] i[color_body] i[color_feet]", CFGFLAG_CLIENT, ConRecentSkin, this, "Restore a recently used Tee skin");
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
	m_SkinSources.Load(Storage());

	// load skins
	Refresh([]() {});
}

void CSkins::OnShutdown()
{
	m_SkinSources.Save(Storage());
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
	for(SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
	{
		if(Catalog.m_pRequest)
			Catalog.m_pRequest->Abort();
		Catalog.m_pRequest.reset();
	}
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		pSkinContainer->m_pDownloadSession.reset();
		if(pSkinContainer->m_pLoadJob)
		{
			pSkinContainer->m_pLoadJob->Abort();
			pSkinContainer->m_pLoadJob = nullptr;
		}
		if(pSkinContainer->m_pPendingSkin)
			DiscardSkinPreviewUpload(pSkinContainer.get());
	}
	m_vSkinDownloadNames.clear();
	m_LoadingStats = {};
	m_VerifiedSkinCacheUrls.clear();
	m_Skins.clear();
	m_UnresolvedSkinScanState = {};
	m_vSkinsUnresolvedThisFrame.clear();
	m_vSkinsTexturesUnloadedThisFrame.clear();
	m_SkinNameIndex.Clear();
	m_SkinNotFoundKeys.clear();
	m_HasInitialSkinDirectoryScan = false;
}

void CSkins::OnUpdate()
{
	const bool PerfEnabled = QmPerfEnabled();
	CPerfTimer StageTimer(PerfEnabled);
	auto LogStage = [&](const char *pStage) {
		if(!PerfEnabled)
			return;
		// 只记录超出帧预算的阶段，区分同步文件操作、回收和终局上传。
		QmPerfLogStage("perf/skins", pStage, StageTimer.ElapsedMs(), false, Client());
		StageTimer.Reset();
	};
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		UpdateSkinQueue(Now, Dummy);
	}
	LogStage("skin_queue");

	// 目录/缓存合并是列表的输入，不属于低频皮肤纹理更新。把它们留在下面的
	// 25–500ms 节流之后，会把 9k 条目录的合并拖到十几秒，并让数量标签长期停在
	// Loading；这里仍由各自的条目预算限制每帧工作量。
	ProcessSkinCatalogRequests();
	LogStage("skin_catalogs");
	ProcessPendingSkinCatalogEntries();
	LogStage("skin_catalog_entries");
	ProcessSkinDirectoryScanJob();
	LogStage("directory_results");

	const bool SourceRefreshSettled = !m_SkinListRefreshDirtyAt.has_value() ||
					  Now - m_SkinListRefreshDirtyAt.value() >= SKIN_LIST_SOURCE_REFRESH_SETTLE_TIME;
	if(m_SkinListRefreshPending && SourceRefreshSettled)
	{
		m_SkinList.ForceRefresh();
		m_SkinListRefreshPending = false;
		m_SkinListRefreshDirtyAt.reset();
	}
	// 列表计划不在 25–500ms 的纹理更新节流之后：它只做容器查找与轻量构造，
	// 每帧有独立的 0.5ms 预算；首屏物化在计划结果到达的同一帧完成。
	ProcessSkinListPlanJob();
	LogStage("list_plan_results");

	// Only update skins periodically to reduce FPS impact
	const std::chrono::nanoseconds MaxTime = std::chrono::milliseconds(std::clamp(round_to_int(Client()->RenderFrameTime() * 50000.0f), 25, 500));
	if(m_ContainerUpdateTime.has_value() && Now - m_ContainerUpdateTime.value() < MaxTime)
	{
		CSkinLoadingStats Stats = LoadingStats();
		UpdateFinishLoading(Stats, Now, m_SettingsFinalizeTimeBudget);
		LogStage("finish_loading");
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
	LogStage("managed_skins");

	CSkinLoadingStats Stats = LoadingStats();
	UpdateUnloadSkins(Stats);
	LogStage("unload_skins");
	UpdateStartLoading(Stats);
	LogStage("start_loading");
	UpdateSkinDownloads(Stats);
	LogStage("download_results");
	FlushSkinSourcesSave(time_get_nanoseconds());
	LogStage("skin_sources_save");
	// 检查间隔不是主线程预算：终局上传使用独立的 1 ms 预算，首个皮肤始终取得进展。
	UpdateFinishLoading(Stats, time_get_nanoseconds(), m_SettingsFinalizeTimeBudget);
	LogStage("finish_loading");
	CollectUnresolvedSkins();
	for(const std::string &SkinName : m_vSkinsUnresolvedThisFrame)
		GameClient()->OnSkinUpdate(SkinName.c_str());
	m_vSkinsUnresolvedThisFrame.clear();
	for(const std::string &SkinName : m_vSkinsTexturesUnloadedThisFrame)
		GameClient()->OnSkinUpdate(SkinName.c_str());
	m_vSkinsTexturesUnloadedThisFrame.clear();
	LogStage("skin_notifications");
}

void CSkins::OnRender()
{
	// 未被本帧真实列表重新发布的视窗失效；折叠和离页不能保留最高优先级。
	m_SettingsLoadViewport.BeginFrame();
	const bool TeeSettingsActive = ActiveSettingsTeePage(GameClient());
	const int UploadBudgetMs = SettingsSkinUploadTimeBudgetMs(
		SettingsFrameContextOrDefault(GameClient()), TeeSettingsActive,
		Client()->RenderFrameTime() * 1000.0f, SettingsSkinTargetFrameMs());
	m_SkinUploadFrameBudget.Reset(std::chrono::milliseconds(UploadBudgetMs));
	// 终局接管与元数据收尾和上传共用同一份帧余量预算。
	m_SettingsFinalizeTimeBudget = std::chrono::milliseconds(UploadBudgetMs);
}

void CSkins::UpdateForSettingsWarmup()
{
	// Startup warmup pumps skin jobs in a tight loop; bypass the normal frame-rate
	// throttle so the blocking preload actually advances before the menu appears.
	m_ContainerUpdateTime.reset();
	m_SkinUploadFrameBudget.Reset();
	m_SettingsFinalizeTimeBudget = std::chrono::milliseconds(1);
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
		if(m_SettingsTeePageActiveLastUpdate)
			ReleaseSettingsSkinLoadRequests();
		m_SettingsTeePageActiveLastUpdate = false;
		m_SettingsThroughputControllerState = {};
		m_SettingsThroughputControllerOutput = {};
		m_SettingsTeeVisibleSnapshot = {};
		return;
	}
	m_SettingsTeePageActiveLastUpdate = true;

	const CSkinLoadingStats Stats = LoadingStats();
	int LoadingJobsAwaitingResult = 0;
	int LoadingJobsReadyForMainThread = 0;
	for(const auto &Name : m_SkinsUsageList)
	{
		const auto It = m_Skins.find(Name);
		if(It == m_Skins.end())
			continue;
		const auto &pSkinContainer = It->second;
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

bool CSkins::SkinNameInActiveUse(const char *pSkinName) const
{
	if(pSkinName == nullptr || pSkinName[0] == '\0')
		return false;
	if(g_Config.m_ClPlayerSkin[0] != '\0' && str_comp_nocase(g_Config.m_ClPlayerSkin, pSkinName) == 0)
		return true;
	if(g_Config.m_ClDummySkin[0] != '\0' && str_comp_nocase(g_Config.m_ClDummySkin, pSkinName) == 0)
		return true;
	if(GameClient() == nullptr)
		return false;
	for(const auto &Client : GameClient()->m_aClients)
	{
		if(Client.m_Active && Client.m_aSkinName[0] != '\0' &&
			str_comp_nocase(Client.m_aSkinName, pSkinName) == 0)
			return true;
	}
	return false;
}

void CSkins::ReleaseSettingsSkinRequest(const char *pSkinName)
{
	if(pSkinName == nullptr || pSkinName[0] == '\0')
		return;
	const auto SkinIt = m_Skins.find(CanonicalSkinName(pSkinName));
	if(SkinIt == m_Skins.end())
		return;
	CSkinContainer *pSkinContainer = SkinIt->second.get();
	const bool PendingOrLoading =
		pSkinContainer->m_State == CSkinContainer::EState::PENDING ||
		pSkinContainer->m_State == CSkinContainer::EState::LOADING ||
		pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED;
	if(!SettingsSkinShouldReleaseViewportRequest(false, SkinNameInActiveUse(pSkinContainer->Name()),
		   pSkinContainer->IsAlwaysLoaded(), pSkinContainer->m_LoadPriority, PendingOrLoading))
		return;
	pSkinContainer->SetState(CSkinContainer::EState::UNLOADED);
}

void CSkins::ReleaseSettingsSkinLoadRequests()
{
	for(const std::string &SkinName : m_SettingsLoadViewport.PublishedNames())
		ReleaseSettingsSkinRequest(SkinName.c_str());
}

void CSkins::SetSettingsVisibleSkinLoads(std::vector<std::string> Names, bool MeasurementOnly)
{
	const std::vector<std::string> vPreviousNames = m_SettingsLoadViewport.PublishedNames();
	if(!m_SettingsLoadViewport.Publish(std::move(Names), MeasurementOnly))
		return;
	for(const std::string &SkinName : vPreviousNames)
	{
		if(!m_SettingsLoadViewport.Contains(SkinName))
			ReleaseSettingsSkinRequest(SkinName.c_str());
	}
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
	pSkinContainer->m_LoadedSource.reset();
	pSkinContainer->m_SettingsSourceApproxBytes = 0;
	QueueSkinTexturesUnloaded(pSkinContainer->Name());
}

void CSkins::UpdateUnloadSkins(CSkinLoadingStats &Stats)
{
	size_t SourceBytesInUse = 0;
	for(const auto &[_, pSkinContainer] : m_Skins)
	{
		if(pSkinContainer->m_pSkin != nullptr)
			SourceBytesInUse += pSkinContainer->SettingsSourceApproxBytes();
	}
	const bool TeeSettingsActive = ActiveSettingsTeePage(GameClient());
	const size_t ConfiguredLoadedLimit = (size_t)maximum(0, g_Config.m_ClSkinsLoadedMax);
	const size_t ResidencyLimit = TeeSettingsActive ? ConfiguredLoadedLimit : SettingsSkinInactiveResidencyLimit(ConfiguredLoadedLimit);
	const size_t SourceBytesBudget = SettingsSkinSourceBytesEstimate(256, 128, 2) * ResidencyLimit;
	const SSettingsResourceFrameContext FrameContext = SettingsFrameContextOrDefault(GameClient());
	const int CountFuseLimit = SettingsSkinEffectiveCountFuseLimit(FrameContext, TeeSettingsActive);
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
	const std::vector<std::string> &vBackgroundSnapshot = SettingsSkinNameSnapshot(m_SkinsBackgroundList);
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
	const size_t MaxSkipped = maximum<size_t>(1, m_SkinsUsageList.size() / 8);
	size_t NumSkipped = 0;
	const std::vector<std::string> &vUsageSnapshot = SettingsSkinNameSnapshot(m_SkinsUsageList, true);
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
	const int CountFuseLimit = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_CountFuseLimit : SettingsSkinEffectiveCountFuseLimit(FrameContext, false);
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
				m_SettingsTeeVisibleSnapshot.m_VisibleNonterminalWaiting,
				CountFuseLimit,
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
				m_SettingsTeeVisibleSnapshot.m_VisibleNonterminalWaiting,
				CountFuseLimit,
			});
			Admission.m_PromoteAllowed = SourceAdmission.m_PromoteAllowed;
			Admission.m_PromotePriority = SourceAdmission.m_PromotePriority;
			Admission.m_pBlockReason = SettingsSkinSourceAdmissionBlockReasonName(SourceAdmission.m_BlockReason);
			Admission.m_CountFuseApplies = SourceAdmission.m_CountFuseApplies;
			return Admission;
		}
		return Admission;
	};
	// 后台预热最多占一部分下载槽：可见项被用户拉到视窗内时，
	// 不应排在一整批后台预取之后。计数在每次 OnUpdate 开始时重算。
	int BackgroundDownloadSessions = 0;
	if(TeeSettingsActive)
	{
		for(const std::string &Name : m_vSkinDownloadNames)
		{
			const auto It = m_Skins.find(Name);
			if(It != m_Skins.end() && It->second->m_LoadPriority != ESettingsResourcePriority::VISIBLE)
				++BackgroundDownloadSessions;
		}
	}
	auto StartLoadJob = [&](CSkinContainer *pSkinContainer, ESettingsResourcePriority Priority) {
		// 已启动和已加载条目不参与准入，不能因窗口满而阻断其他候选。
		if(pSkinContainer->m_State != CSkinContainer::EState::PENDING && pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)
			return true;
		if(Stats.m_NumPending == 0 && pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)
		{
			return true;
		}
		// 解码并发必须与上传吞吐匹配：设置页浏览目录时曾出现 512 张同时 LOADING，
		// 而已解码等待上传的纹理实际只以约 7 张/秒被消费，既占内存又拉长可见项延迟。
		if(TeeSettingsActive)
		{
			const ESettingsResourcePriority EffectivePriority =
				pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED ? pSkinContainer->m_LoadPriority : Priority;
			const int LoadingLimit = EffectivePriority == ESettingsResourcePriority::VISIBLE ?
							 SETTINGS_SKIN_VISIBLE_LOADING_LIMIT :
							 SETTINGS_SKIN_BACKGROUND_LOADING_LIMIT;
			if(!SettingsSkinShouldAdmitLoading((int)Stats.m_NumLoading, LoadingLimit))
			{
				str_copy(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason, "loading_backlog", sizeof(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason));
				LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), "loading_backlog",
					GameClient()->GpuUploadLimiter()->RemainingUploads(),
					GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
				return false;
			}
		}
		auto Admission = DetermineAdmission(pSkinContainer, Priority);
		if(!Admission.m_PromoteAllowed &&
			Priority == ESettingsResourcePriority::VISIBLE &&
			Admission.m_pBlockReason == SettingsSkinSourceAdmissionBlockReasonName(ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE) &&
			ReclaimBackgroundSkinForPriorityRequest(pSkinContainer->Name(), CountFuseLimit))
		{
			Stats = LoadingStats();
			Admission = DetermineAdmission(pSkinContainer, Priority);
		}
		if(!Admission.m_PromoteAllowed)
		{
			str_copy(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason, Admission.m_pBlockReason, sizeof(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason));
			LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), Admission.m_pBlockReason,
				GameClient()->GpuUploadLimiter()->RemainingUploads(),
				GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
			return false;
		}
		const bool CountFuseApplies = Admission.m_CountFuseApplies;
		// PENDING 已计入在途额度，启动时只需要一个实际加载位置。
		const bool EnteringQueue = pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED;
		const auto CountedLoads = [&] { return EnteringQueue ? Stats.RealInflight() : Stats.m_NumLoading; };
		if(CountFuseApplies && CountFuseLimit > 0 && CountedLoads() >= (size_t)CountFuseLimit)
		{
			if(ReclaimBackgroundSkinForPriorityRequest(pSkinContainer->Name(), CountFuseLimit))
			{
				Stats = LoadingStats();
			}
			if(CountedLoads() >= (size_t)CountFuseLimit)
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
			try
			{
				pSkinContainer->m_pLoadJob = std::make_shared<CSkinLoadJob>(
					this, ResolveLoadName(pSkinContainer->Type(), pSkinContainer->Name(), m_SkinNameIndex), pSkinContainer->StorageType());
			}
			catch(const std::bad_alloc &)
			{
				return false;
			}
			break;
		case CSkinContainer::EType::DOWNLOAD:
		{
			// 初次目录扫描完成前先使用磁盘上的 canonical 名称，避免用服务器下发的小写名发出 404 请求。
			if(!m_HasInitialSkinDirectoryScan)
				return false;
			{
				const ESettingsResourcePriority EffectivePriority =
					pSkinContainer->m_State == CSkinContainer::EState::BACKGROUND_REQUESTED ? pSkinContainer->m_LoadPriority : Priority;
				const bool VisiblePriority = EffectivePriority == ESettingsResourcePriority::VISIBLE;
				const int ActiveSessions = VisiblePriority ? (int)m_vSkinDownloadNames.size() : BackgroundDownloadSessions;
				const int SessionLimit = VisiblePriority ? MAX_ACTIVE_SKIN_DOWNLOAD_SESSIONS : MAX_BACKGROUND_SKIN_DOWNLOAD_SESSIONS;
				if(!SettingsSkinCanStartDownloadSession(ActiveSessions, SessionLimit))
				{
					const char *pReason = VisiblePriority ? "download_session_limit" : "background_download_limit";
					str_copy(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason, pReason, sizeof(m_SettingsSourceAdmissionTelemetry.m_aLastWaitReason));
					LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), pReason,
						GameClient()->GpuUploadLimiter()->RemainingUploads(),
						GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
					return false;
				}
				auto pSession = TryCreateTrackedSkinDownloadSession(pSkinContainer);
				if(!pSession)
					return false;
				pSkinContainer->m_pDownloadSession = std::move(pSession);
				if(!VisiblePriority)
					++BackgroundDownloadSessions;
			}
			break;
		}
		default:
			dbg_assert_failed("pSkinContainer->Type() invalid");
		}
		if(pSkinContainer->m_pLoadJob)
			Engine()->AddJob(pSkinContainer->m_pLoadJob);
		LogSettingsSkinSourceRequestEvent(pSkinContainer->Name(), Priority, pSkinContainer->m_State);
		pSkinContainer->SetState(CSkinContainer::EState::LOADING, Priority);
		Stats.m_NumPending--;
		Stats.m_NumLoading++;
		m_SettingsSourceAdmissionTelemetry.m_StartedDelta++;
		return true;
	};

	const std::vector<std::string> &vPrioritizedSkinNames = SettingsSkinNameSnapshot(m_SkinsUsageList);
	SettingsVisitSkinLoadRequests(vPrioritizedSkinNames, [this](const std::string &Name) {
			const auto It = m_Skins.find(Name);
			return It != m_Skins.end() ? It->second->m_LoadPriority : ESettingsResourcePriority::BACKGROUND; }, [this, TeeSettingsActive](const std::string &Name) { return TeeSettingsActive && m_SettingsLoadViewport.Contains(Name); }, [&](const std::string &SkinName) {
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
			return;
		}
		if(It == m_Skins.end())
		{
			return;
		}
		StartLoadJob(It->second.get(), It->second->m_LoadPriority); });

	const std::vector<std::string> &vBackgroundSkinNames = SettingsSkinNameSnapshot(m_SkinsBackgroundList);
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
	const char *pFallbackReason = "fallback_sweep";
	if(SettingsSkinFallbackSweepShouldRun(!m_SkinsUsageList.empty(), !m_SkinsBackgroundList.empty()))
	{
		// Always-loaded (built-in/vanilla) skins never join the request queues and follow no
		// list. They are the only entries this implicit sweep can still help; ordinary
		// unloaded/catalog-only containers must go through explicit priority requests.
		for(const auto &[SkinName, pSkinContainer] : m_Skins)
		{
			if(!pSkinContainer->IsAlwaysLoaded())
				continue;
			if(pSkinContainer->m_State != CSkinContainer::EState::PENDING &&
				pSkinContainer->m_State != CSkinContainer::EState::BACKGROUND_REQUESTED)
				continue;
			++FallbackScanned;
			const CSkinContainer::EState StateBefore = pSkinContainer->m_State;
			if(pSkinContainer->m_UsageEntryIterator.has_value() || pSkinContainer->m_BackgroundEntryIterator.has_value())
			{
				++FallbackSkipped;
				continue;
			}
			if(!StartLoadJob(pSkinContainer.get(), ESettingsResourcePriority::BACKGROUND))
				break;
			if(StateBefore != pSkinContainer->m_State && pSkinContainer->m_State == CSkinContainer::EState::LOADING)
				++FallbackStarted;
		}
	}
	else
	{
		pFallbackReason = "explicit_queues";
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

void CSkins::UpdateSkinDownloads(CSkinLoadingStats &Stats)
{
	for(auto It = m_vSkinDownloadNames.begin(); It != m_vSkinDownloadNames.end();)
	{
		auto SkinIt = m_Skins.find(*It);
		if(SkinIt == m_Skins.end() || !SkinIt->second->m_pDownloadSession)
		{
			It = m_vSkinDownloadNames.erase(It);
			continue;
		}
		CSkinContainer *pContainer = SkinIt->second.get();
		// 上传消费完上一份素材后再接管更新，避免覆盖正在分帧上传的数据。
		if(pContainer->m_pLoadJob || pContainer->m_pPendingSkin)
		{
			++It;
			continue;
		}
		const auto Result = pContainer->m_pDownloadSession->Poll();
		const std::optional<ESkinSource> &DownloadedSource = pContainer->m_pDownloadSession->DownloadedSource();
		if(DownloadedSource.has_value())
			RecordSkinSource(pContainer, DownloadedSource.value());
		if(Result == CQmSkinDownloadSession::EResult::READY)
		{
			pContainer->m_pLoadJob = std::static_pointer_cast<CAbstractSkinLoadJob>(pContainer->m_pDownloadSession->TakeReadyJob());
			m_SkinNotFoundKeys.erase(CSkinNameIndex::Key(pContainer->Name()));
			// 验证提交完成后取得当前磁盘身份，避免后续扫描重复加载已发布更新。
			char aPath[IO_MAX_PATH_LENGTH];
			const char *pCanonicalName = CanonicalSkinName(pContainer->Name());
			str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", pCanonicalName);
			pContainer->m_pLoadJob->m_Data.ReadLastModified(Storage(), aPath, IStorage::TYPE_SAVE);
			const std::string &DownloadedUrl = pContainer->m_pDownloadSession->DownloadedUrl();
			if(!DownloadedUrl.empty() && pContainer->m_pLoadJob->m_Data.m_LastModified.has_value())
				m_VerifiedSkinCacheUrls[pCanonicalName] = {DownloadedUrl, *pContainer->m_pLoadJob->m_Data.m_LastModified};
		}
		else if(Result != CQmSkinDownloadSession::EResult::WAITING)
		{
			if(pContainer->m_State != CSkinContainer::EState::LOADED)
			{
				const auto State = Stats.FinishLoadingFailure(pContainer->m_State, pContainer->m_pSkin != nullptr, Result == CQmSkinDownloadSession::EResult::NOT_FOUND);
				pContainer->SetState(State, pContainer->m_LoadPriority);
				if(State == CSkinContainer::EState::NOT_FOUND)
					m_SkinNotFoundKeys.insert(CSkinNameIndex::Key(pContainer->Name()));
				log_error("skins", "Skin '%s' download ended: %s", pContainer->Name(), Result == CQmSkinDownloadSession::EResult::NOT_FOUND ? "not found in enabled sources" : "request or image validation failed");
			}
			pContainer->m_pDownloadSession.reset();
			It = m_vSkinDownloadNames.erase(It);
			continue;
		}
		++It;
	}
}

void CSkins::RecordSkinSource(CSkinContainer *pSkinContainer, ESkinSource Source)
{
	if(pSkinContainer == nullptr || Source == ESkinSource::UNKNOWN || Source == ESkinSource::LOCAL)
		return;
	const ESkinSourceMask SourcesBefore = pSkinContainer->EffectiveSkinSources();
	pSkinContainer->SetSkinSource(Source);
	if(m_SkinSources.Set(pSkinContainer->Name(), Source))
	{
		// 目录已经提供来源掩码时，记录下载来源不会改变筛选归属；
		// 此前每个下载完成都强刷整表，7k 列表在浏览时会持续重建。
		// “全部/仅原生”都不看目录来源，加载期间无需为来源登记反复重建计划。
		const ESkinListFilter ListFilter = ResolveSkinListFilter(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0);
		if(pSkinContainer->EffectiveSkinSources() != SourcesBefore &&
			ListFilter != ESkinListFilter::ALL &&
			ListFilter != ESkinListFilter::VANILLA)
		{
			m_SkinListRefreshPending = true;
			m_SkinListRefreshDirtyAt = time_get_nanoseconds();
		}
		// 注册表每次 Save 都会整表重写，成本随目录规模增长；同一帧内多个下载完成时
		// 逐条写盘会叠加成几十毫秒的主线程停顿，因此这里只标记，交给定时合并写盘。
		m_SkinSourcesSavePending = true;
	}
}

void CSkins::FlushSkinSourcesSave(std::chrono::nanoseconds Now)
{
	const bool HasPreviousSave = m_SkinSourcesSaveLast.has_value();
	const int64_t LastNs = HasPreviousSave ? m_SkinSourcesSaveLast.value().count() : 0;
	if(!SkinSourceSaveShouldFlush(m_SkinSourcesSavePending, HasPreviousSave, Now.count(), LastNs, SKIN_SOURCES_SAVE_INTERVAL_NS))
		return;
	if(m_SkinSources.Save(Storage()))
		m_SkinSourcesSavePending = false;
	m_SkinSourcesSaveLast = Now;
}

const std::vector<std::string> &CSkins::SettingsSkinNameSnapshot(const std::list<std::string> &Names, bool Reverse)
{
	m_vSettingsSkinNameSnapshot.clear();
	m_vSettingsSkinNameSnapshot.reserve(Names.size());
	if(Reverse)
		m_vSettingsSkinNameSnapshot.insert(m_vSettingsSkinNameSnapshot.end(), Names.rbegin(), Names.rend());
	else
		m_vSettingsSkinNameSnapshot.insert(m_vSettingsSkinNameSnapshot.end(), Names.begin(), Names.end());
	return m_vSettingsSkinNameSnapshot;
}

CSkins::ESkinProcessResult CSkins::ProcessSkinContainer(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,
	int &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,
	std::chrono::nanoseconds MaxTime)
{
	if((pSkinContainer->m_State != CSkinContainer::EState::LOADING && pSkinContainer->m_State != CSkinContainer::EState::LOADED) || !pSkinContainer->m_pLoadJob)
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
		if(pSkinContainer->m_State == CSkinContainer::EState::LOADED)
		{
			pSkinContainer->m_pLoadJob.reset();
			pSkinContainer->m_pDownloadSession.reset();
			return ESkinProcessResult::CONTINUE;
		}
		SkinsProcessedThisFrame++;
		const auto State = Stats.FinishLoadingFailure(pSkinContainer->m_State, pSkinContainer->m_pSkin != nullptr, false);
		pSkinContainer->SetState(State, pSkinContainer->m_LoadPriority);
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
	const bool ReplacingLoadedSkin = pSkinContainer->m_State == CSkinContainer::EState::LOADED;
	if(!m_SkinUploadFrameBudget.CanUpload())
		return ESkinProcessResult::BREAK_UPLOAD;
	const int MaxSkinsPerFrame = SettingsSkinMaxPerFrame(GameClient());
	const auto FailUpload = [&] {
		pSkinContainer->m_pLoadJob.reset();
		const auto State = Stats.FinishLoadingFailure(pSkinContainer->m_State, pSkinContainer->m_pSkin != nullptr, false);
		pSkinContainer->SetState(State, pSkinContainer->m_LoadPriority);
		DiscardSkinPreviewUpload(pSkinContainer);
		pSkinContainer->m_pDownloadSession.reset();
		++SkinsProcessedThisFrame;
		return ESkinProcessResult::BREAK_UPLOAD;
	};
	if(!pSkinContainer->m_pPendingSkin &&
		!BeginSkinPreviewUpload(pSkinContainer, std::move(pSkinContainer->m_pLoadJob->m_Data)))
		return FailUpload();
	CSkinPreviewUploadBudget SkinPreviewUploadBudget(SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS - (int)pSkinContainer->m_SettingsPendingUploadSprite,
		GameClient()->GpuUploadLimiter(), SettingsFrameBudgetOrNull(GameClient()));
	while(pSkinContainer->m_SettingsPendingUploadSprite < SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)
	{
		if(!m_SkinUploadFrameBudget.CanUpload())
			return ESkinProcessResult::BREAK_UPLOAD;
		if(!SkinPreviewUploadBudget.Consume())
		{
			LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), "gpu_upload_budget",
				GameClient()->GpuUploadLimiter()->RemainingUploads(),
				GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame());
			LogSkinSettingsResourcePerf("upload", 0, MaxSkinsPerFrame, Stats.m_NumLoading, ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET, 0.0);
			return ESkinProcessResult::BREAK_GPU_LIMIT;
		}
		const auto UploadStart = time_get_nanoseconds();
		const bool Uploaded = UploadNextSkinPreviewSprite(pSkinContainer);
		if(!Uploaded)
		{
			m_SkinUploadFrameBudget.RecordUpload(time_get_nanoseconds() - UploadStart);
			return FailUpload();
		}
		SkinPreviewUploadBudget.Commit();
		LogSettingsSkinSourceStageEvent("preview_uploads", pSkinContainer->Name(),
			pSkinContainer->m_SettingsPendingUploadData.m_SourceWidth,
			pSkinContainer->m_SettingsPendingUploadData.m_SourceHeight, 0, 0.0, 1);
		m_SkinUploadFrameBudget.RecordUpload(time_get_nanoseconds() - UploadStart);
	}

	if(!ReplacingLoadedSkin)
		Stats.m_NumLoading--;
	SkinsProcessedThisFrame++;
	FinishSkinPreviewUpload(pSkinContainer);
	++m_SettingsSourceUploadsCompleted;
	GameClient()->OnSkinUpdate(pSkinContainer->Name());
	pSkinContainer->m_pLoadJob = nullptr;
	if(!ReplacingLoadedSkin)
		Stats.m_NumLoaded++;
	++m_SettingsSourceLoadsCompleted;
	LogSkinSettingsResourcePerf("upload", 1, MaxSkinsPerFrame, (int)Stats.m_NumLoading, ESettingsWarmupMissReason::NONE, 0.0);

	if(time_get_nanoseconds() - StartTime >= MaxTime)
		return ESkinProcessResult::BREAK_TIME_EXCEEDED;
	return ESkinProcessResult::CONTINUE;
}

void CSkins::UpdateFinishLoading(CSkinLoadingStats &Stats, std::chrono::nanoseconds StartTime, std::chrono::nanoseconds MaxTime)
{
	const bool RefreshUploadPending = std::any_of(m_vSkinDownloadNames.begin(), m_vSkinDownloadNames.end(), [this](const std::string &Name) {
		const auto It = m_Skins.find(Name);
		return It != m_Skins.end() && It->second->m_State == CSkinContainer::EState::LOADED && It->second->m_pLoadJob != nullptr;
	});
	if(!Stats.ShouldFinishLoading(RefreshUploadPending))
		return;
	int SkinsProcessedThisFrame = 0;
	int TrackedLoading = 0;
	const int MaxSkinsPerFrame = SettingsSkinMaxPerFrame(GameClient());
	LogSettingsSkinFrameCapEvent(GameClient());
	bool ProcessedHighPrioritySkin = false;
	const std::vector<std::string> &vUsageSnapshot = SettingsSkinNameSnapshot(m_SkinsUsageList);

	// First, try to process skins from the usage list (most recently used first)
	// This prioritizes visible/commonly used skins for better perceived performance
	for(const std::string &SkinName : vUsageSnapshot)
	{
		if(Stats.m_NumLoading == 0 && !RefreshUploadPending)
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
		if(It->second->m_State == CSkinContainer::EState::LOADING)
			++TrackedLoading;
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
		if(Stats.m_NumLoading == 0 && !RefreshUploadPending)
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
		if(It->second->m_State == CSkinContainer::EState::LOADING)
			++TrackedLoading;
		if(Result == ESkinProcessResult::BREAK_GPU_LIMIT || Result == ESkinProcessResult::BREAK_TIME_EXCEEDED || Result == ESkinProcessResult::BREAK_UPLOAD)
		{
			return;
		}
	}

	if(Stats.m_NumLoading <= (size_t)TrackedLoading && !RefreshUploadPending)
		return;

	// Process remaining loading skins that are not tracked by either priority queue.
	// This ensures legacy and direct-load paths can still finish.
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if((Stats.m_NumLoading == 0 && !RefreshUploadPending) || SkinsProcessedThisFrame >= MaxSkinsPerFrame)
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
	m_vPendingSkinListEntries.clear();
	m_SkinList.ClearPlan();
	m_SkinListRefreshPending = false;
	m_SkinListRefreshDirtyAt.reset();
	for(SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
	{
		if(Catalog.m_pRequest)
			Catalog.m_pRequest->Abort();
		Catalog.m_pRequest.reset();
	}
	m_vPendingSkinCatalogEntries.clear();
	m_SkinCatalogMergeCursor = 0;
	m_vPendingSkinDirectoryEntries.clear();
	m_SkinDirectoryMergeCursor = 0;
	m_SkinNameIndex.Clear();
	m_SkinNotFoundKeys.clear();
	m_HasInitialSkinDirectoryScan = false;

	for(auto &[_, pSkinContainer] : m_Skins)
	{
		if(str_comp(pSkinContainer->Name(), "default") == 0)
		{
			continue;
		}
		pSkinContainer->m_pDownloadSession.reset();
		if(pSkinContainer->m_pLoadJob)
		{
			pSkinContainer->m_pLoadJob->Abort();
			pSkinContainer->m_pLoadJob = nullptr;
		}
		if(pSkinContainer->m_pPendingSkin)
		{
			DiscardSkinPreviewUpload(pSkinContainer.get());
		}
		if(pSkinContainer->m_State != CSkinContainer::EState::LOADED)
			pSkinContainer->SetState(pSkinContainer->m_pSkin != nullptr ? CSkinContainer::EState::LOADED : pSkinContainer->DetermineInitialState(), pSkinContainer->m_LoadPriority);
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
	BuildSkinCatalogSources();
	LoadSkinCatalogCaches();
	SkinLoadedCallback();
	QueueSkinCatalogRequests();
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
	return m_LoadingStats;
}

CSkins::CSkinList &CSkins::SkinList(int Dummy)
{
	m_SkinList.m_RequestedDummy = Dummy;
	return m_SkinList;
}

void CSkins::CSkinList::ResetPlan(std::vector<SSettingsSkinListEntry> &&vPlannedEntries, int UnfilteredCount)
{
	m_vPlannedEntries = std::move(vPlannedEntries);
	// 条目数从一开始就是计划最终值；未物化槽位保持空容器，UI 画骨架但不改滚动范围。
	m_vSkins.clear();
	m_vSkins.resize(m_vPlannedEntries.size());
	m_MergeCursor = 0;
	m_MaterializedCount = 0;
	m_UnfilteredCount = UnfilteredCount;
	m_PlanKnown = true;
	m_FullIndexMerged = m_vPlannedEntries.empty();
	++m_Revision;
	++m_MaterializedRevision;
}

void CSkins::CSkinList::ClearPlan()
{
	const bool HadPlan = m_PlanKnown || !m_vPlannedEntries.empty() || !m_vSkins.empty();
	m_vPlannedEntries.clear();
	m_vSkins.clear();
	m_MergeCursor = 0;
	m_MaterializedCount = 0;
	m_UnfilteredCount = 0;
	m_PlanKnown = false;
	m_FullIndexMerged = true;
	if(HadPlan)
	{
		++m_Revision;
		++m_MaterializedRevision;
	}
}

bool CSkins::MaterializeSkinListEntry(int Index)
{
	if(Index < 0 || (size_t)Index >= m_SkinList.m_vSkins.size() || (size_t)Index >= m_SkinList.m_vPlannedEntries.size())
		return false;
	CSkinListEntry &Entry = m_SkinList.m_vSkins[Index];
	if(Entry.SkinContainer() != nullptr)
		return true;

	const SSettingsSkinListEntry &PlannedEntry = m_SkinList.m_vPlannedEntries[Index];
	const auto SkinIt = m_Skins.find(PlannedEntry.m_Name);
	if(SkinIt == m_Skins.end())
		return false;
	Entry = MakeSkinListEntry(PlannedEntry, SkinIt->second.get());
	++m_SkinList.m_MaterializedCount;
	++m_SkinList.m_MaterializedRevision;
	return true;
}

void CSkins::MaterializeSkinListRange(int FirstIndex, int EndIndex, bool RequestVisible)
{
	const int First = maximum(0, FirstIndex);
	const int End = minimum(EndIndex, m_SkinList.ItemCount());
	for(int Index = First; Index < End; ++Index)
	{
		MaterializeSkinListEntry(Index);
		if(RequestVisible)
			RequestSkinListLoad(Index, ESettingsResourcePriority::VISIBLE);
	}
}

void CSkins::RequestSkinListLoad(int Index, ESettingsResourcePriority Priority)
{
	if(Index < 0 || Index >= m_SkinList.ItemCount())
		return;
	if(m_SkinList.m_vSkins[Index].SkinContainer() != nullptr)
	{
		m_SkinList.m_vSkins[Index].RequestLoad(Priority);
		return;
	}

	// 未物化或计划名字对应的容器刚被创建：按计划名字请求，绝不回退到默认皮肤。
	const SSettingsSkinListEntry *pPlannedEntry = m_SkinList.PlannedEntry(Index);
	if(pPlannedEntry == nullptr)
		return;
	const CSkinContainer *pSkinContainer = FindContainerOrNullptr(pPlannedEntry->m_Name.c_str(), false);
	if(pSkinContainer != nullptr)
		const_cast<CSkinContainer *>(pSkinContainer)->RequestLoad(Priority);
}

bool CSkins::SkinListPlanQueryCurrent() const
{
	return m_SkinList.m_Dummy == m_SkinList.m_RequestedDummy &&
	       m_SkinList.m_ListFilter == static_cast<int>(ResolveSkinListFilter(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0)) &&
	       m_SkinList.m_SortMode == std::clamp(g_Config.m_QmSkinSortMode, 0, 1) &&
	       m_SkinList.m_Search == g_Config.m_ClSkinFilterString &&
	       SkinListColorKeyEquals(m_SkinList.m_MainColorKey, MakeSkinListColorKey(0)) &&
	       SkinListColorKeyEquals(m_SkinList.m_DummyColorKey, MakeSkinListColorKey(1));
}

void CSkins::RebuildSkinListPlan()
{
	m_SkinList.ForceRefresh();
}

bool CSkins::SkinListReady() const
{
	return m_SkinList.PlanKnown() &&
	       !m_SkinList.m_NeedsUpdate &&
	       m_pSkinDirectoryScanJob == nullptr &&
	       m_pSkinListPlanJob == nullptr &&
	       m_vPendingSkinDirectoryEntries.empty() &&
	       m_SkinList.FullIndexMerged();
}

bool CSkins::SkinListSourcesPending() const
{
	SSkinListSourceSettleState State;
	State.m_HasInitialDirectoryScan = m_HasInitialSkinDirectoryScan;
	State.m_DirectoryScanPending = m_pSkinDirectoryScanJob != nullptr || !m_vPendingSkinDirectoryEntries.empty();
	State.m_CatalogMergePending = !m_vPendingSkinCatalogEntries.empty();
	State.m_CatalogRequestPending = std::any_of(m_vSkinCatalogs.begin(), m_vSkinCatalogs.end(), [](const SSkinCatalogRuntime &Catalog) {
		return Catalog.m_pRequest != nullptr && !Catalog.m_Snapshot.m_Loaded;
	});
	return !SkinListSourcesSettled(State);
}

bool CSkins::SkinListSourceDataReady()
{
	return SkinListPlanQueryCurrent() && SkinListReady() && !SkinListSourcesPending();
}

bool CSkins::SkinListSkeletonReady() const
{
	const SSkinListPlanState State{
		m_pSkinDirectoryScanJob != nullptr || !m_vPendingSkinDirectoryEntries.empty(),
		m_SkinList.PlanKnown() && m_SkinList.FullIndexMerged(),
		m_SkinList.ItemCount(),
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

		const CSkinContainer *pContainer = FindContainerOrNullptr(Name.c_str(), false);
		if(pContainer == nullptr)
			continue;

		const_cast<CSkinContainer *>(pContainer)->RequestLoad(Priority);
	}
}

bool CSkins::PoseSkinsReady(const std::vector<std::string> &vNames, std::string *pOutPendingName)
{
	for(const std::string &Name : vNames)
	{
		if(Name.empty())
			continue;
		// 预热必须每帧重试：一次请求可能在准入队列里被后续帧挤掉。
		const CSkinContainer *pContainer = FindContainerOrNullptr(Name.c_str(), true);
		if(pContainer == nullptr)
		{
			// 名字非法（例如被过滤的特殊皮肤名）：CPlayers 同样会回退 default，
			// 这里也按"已确定不可用"处理，否则会永远等下去。
			continue;
		}
		if(pContainer->Skin() != nullptr)
			continue;
		// 终态提前放行，让成片至少和预览保持一致，而不是一直等到超时。
		if(PoseSkinWaitIsFutile(pContainer->State()))
			continue;
		if(pOutPendingName != nullptr)
			*pOutPendingName = Name;
		return false;
	}
	return true;
}

bool CSkins::PrewarmPlayerPreviewReady(int Dummy, int MaxEntries, bool ProgressiveListReady)
{
	CSkinList &List = SkinList(Dummy);
	const int VisibleTotal = minimum(List.ItemCount(), MaxEntries);
	const char *pSelectedSkinRaw = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const char *pSelectedSkin = pSelectedSkinRaw != nullptr && pSelectedSkinRaw[0] != '\0' ? pSelectedSkinRaw : "default";

	// 选中皮肤与可见条目都要保持 LRU 使用痕迹：即使已就绪也不能停，
	// 否则可见皮肤被回收后会在同一页面反复加载。
	const CSkinContainer *pSelectedContainer = FindContainerOrNullptr(pSelectedSkin);
	const bool SelectedReady = pSelectedContainer == nullptr ||
				   pSelectedContainer->Skin() != nullptr ||
				   pSelectedContainer->State() == CSkinContainer::EState::ERROR ||
				   pSelectedContainer->State() == CSkinContainer::EState::NOT_FOUND;

	int VisibleReadyCount = 0;
	int SourceLoadedCount = 0;
	const char *pMissReason = SelectedReady ? "visible_source_loading" : "selected_source_loading";
	for(int i = 0; i < VisibleTotal; ++i)
	{
		const CSkinContainer *pContainer = nullptr;
		if(i < (int)List.Skins().size())
			pContainer = List.Skins()[i].SkinContainer();
		if(pContainer == nullptr)
		{
			const SSettingsSkinListEntry *pPlannedEntry = List.PlannedEntry(i);
			if(pPlannedEntry != nullptr)
				pContainer = FindContainerOrNullptr(pPlannedEntry->m_Name.c_str(), false);
		}
		if(pContainer == nullptr)
			continue;

		const CSkinContainer::EState State = pContainer->State();
		if(pContainer->Skin() != nullptr)
		{
			++VisibleReadyCount;
			++SourceLoadedCount;
		}
		else if(State == CSkinContainer::EState::ERROR || State == CSkinContainer::EState::NOT_FOUND)
		{
			++VisibleReadyCount;
		}
		else
		{
			pMissReason = "visible_source_loading";
		}
		const_cast<CSkinContainer *>(pContainer)->RequestLoad(ESettingsResourcePriority::VISIBLE);
	}

	const bool ProgressiveEntriesReady = SettingsSkinListHasProgressiveWarmEntries(List.MaterializedCount(), MaxEntries, List.ItemCount());
	const bool ListReady = !m_SkinList.m_NeedsUpdate &&
			       m_pSkinDirectoryScanJob == nullptr &&
			       m_pSkinListPlanJob == nullptr &&
			       m_vPendingSkinDirectoryEntries.empty() &&
			       (ProgressiveListReady ? ProgressiveEntriesReady : (List.PlanKnown() && List.FullIndexMerged()));
	if(!ListReady)
	{
		LogSettingsSkinSourceWarmupEvent("warmup_miss", "reason=list_pending");
		return false;
	}

	const CSkinContainer *pDefaultContainer = FindContainerOrNullptr("default");
	const bool DefaultReady = pDefaultContainer != nullptr && pDefaultContainer->Skin() != nullptr;
	if(!DefaultReady)
	{
		LogSettingsSkinSourceWarmupEvent("warmup_miss", "reason=default_loading");
		return false;
	}

	// 已全部就绪：不再构造名字列表，也不再每帧格式化 warmup 诊断串。
	if(SelectedReady && VisibleReadyCount >= VisibleTotal)
		return true;

	char aWarmupGate[192];
	str_format(aWarmupGate, sizeof(aWarmupGate),
		"dummy=%d selected_ready=%d visible_ready_count=%d visible_total=%d source_loaded_count=%d preview_ready_count=0 restore_queue_count=0",
		Dummy, SelectedReady ? 1 : 0, VisibleReadyCount, VisibleTotal, SourceLoadedCount);
	LogSettingsSkinSourceWarmupEvent("warmup_gate", aWarmupGate);

	char aWarmupMiss[96];
	str_format(aWarmupMiss, sizeof(aWarmupMiss), "dummy=%d reason=%s", Dummy, pMissReason);
	LogSettingsSkinSourceWarmupEvent("warmup_miss", aWarmupMiss);
	return false;
}

void CSkins::QueueSkinListPlanJob(int Dummy)
{
	// 列表重建只登记收藏，不代表这些皮肤当前可见；否则启动便会批量下载所有收藏。
	for(const auto &FavoriteSkin : m_Favorites)
		FindContainerOrNullptr(FavoriteSkin.c_str(), false);

	const CSkinListEntry::SColorKey CurrentColorKey = MakeSkinListColorKey(Dummy);
	const CSkinListEntry::SColorKey MainColorKey = MakeSkinListColorKey(0);
	const CSkinListEntry::SColorKey DummyColorKey = MakeSkinListColorKey(1);
	const int ListFilterValue = static_cast<int>(ResolveSkinListFilter(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0));
	const ESkinListFilter ListFilter = SkinListFilterFromInt(ListFilterValue);

	std::vector<SSkinListSnapshotEntry> vEntries;
	vEntries.reserve(m_Skins.size() + m_aSkinQueue[Dummy].size());
	const auto HasListEntry = [&](const CSkinContainer *pSkinContainer, const CSkinListEntry::SColorKey &ColorKey) {
		return std::any_of(vEntries.begin(), vEntries.end(), [&](const SSkinListSnapshotEntry &Entry) {
			return Entry.m_Name == pSkinContainer->Name() && SkinListColorKeyEquals(Entry.m_ColorKey.value_or(CurrentColorKey), ColorKey);
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
		if(!SkinListFilterMatches(pSkinContainer->EffectiveSkinSources(), ListFilter))
			return;
		if(pSkinContainer->m_State == CSkinContainer::EState::NOT_FOUND &&
			!pSkinContainer->IsCatalogOnly() &&
			!SelectedMainColor &&
			!SelectedDummyColor &&
			!Favorite &&
			!ForceShowNotFound)
		{
			return;
		}
		// 每个基础容器只遍历一次，只有队列的颜色条目需要去重。
		if(ForceShowNotFound && HasListEntry(pSkinContainer, EffectiveColorKey))
			return;

		SSkinListSnapshotEntry Entry;
		Entry.m_Name = pSkinContainer->Name();
		Entry.m_ColorKey = ColorKey;
		Entry.m_SelectedMain = SelectedMainColor;
		Entry.m_SelectedDummy = SelectedDummyColor;
		Entry.m_Favorite = Favorite;
		Entry.m_NotFound = pSkinContainer->m_State == CSkinContainer::EState::NOT_FOUND;
		Entry.m_Special = pSkinContainer->IsSpecial();
		Entry.m_ForceShowNotFound = ForceShowNotFound || pSkinContainer->IsCatalogOnly();
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
			const CSkinContainer::EType Type = PreferredContainerType(QueueEntry.m_SkinName.c_str());
			CSkinContainer SkinContainer(this, QueueEntry.m_SkinName.c_str(), Type, ContainerStorageTypeFor(Type));
			auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
			pSkinContainer->SetCatalogOnly(false);
			ApplyCatalogSources(pSkinContainer.get());
			pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
			SkinIt = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
		}
		const CSkinListEntry::SColorKey QueueColorKey = MakeSkinListColorKey(QueueEntry.m_UseCustomColor, QueueEntry.m_ColorBody, QueueEntry.m_ColorFeet);
		AddSkinListSnapshotEntry(SkinIt->second.get(), QueueColorKey, true);
	}

	m_SkinList.m_ListFilter = ListFilterValue;
	m_SkinList.m_Search = g_Config.m_ClSkinFilterString;
	m_SkinList.m_SortMode = std::clamp(g_Config.m_QmSkinSortMode, 0, 1);
	m_pSkinListPlanJob = std::make_shared<CSkinListPlanJob>(std::move(vEntries), m_SkinList.m_Search, ++m_SkinListPlanGeneration, m_SkinList.m_SortMode);
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
	if(m_pSkinDirectoryScanJob && m_pSkinDirectoryScanJob->Finished())
	{
		if(m_pSkinDirectoryScanJob->State() == IJob::STATE_DONE)
		{
			m_vPendingSkinDirectoryEntries = m_pSkinDirectoryScanJob->TakeResult().m_vEntries;
			m_SkinNameIndex.Clear();
			for(const CSkinDirectoryScanJob::SResult::SEntry &Entry : m_vPendingSkinDirectoryEntries)
			{
				m_SkinNameIndex.Add(Entry.m_Name.c_str(), Entry.m_Type == CSkinContainer::EType::LOCAL ? 1 : 0);
				m_SkinNotFoundKeys.erase(CSkinNameIndex::Key(Entry.m_Name.c_str()));
			}
			for(const SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
			{
				if(!Catalog.m_Snapshot.m_Loaded)
					continue;
				for(const auto &[_, Entry] : Catalog.m_Snapshot.m_Entries)
					m_SkinNameIndex.Add(Entry.m_Name.c_str(), 0);
			}
			m_SkinDirectoryMergeCursor = 0;
		}
		else
			log_warn("skins", "Skin directory scan failed; retaining existing entries");
		m_HasInitialSkinDirectoryScan = true;
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
			const SQmSkinSourceIdentity RequestedSource{pSkinContainer->Type() == CSkinContainer::EType::DOWNLOAD, pSkinContainer->StorageType(), pSkinContainer->LastModified()};
			// 加载失败时仍保留旧预览；下次刷新与已发布版本比较，允许重新尝试。
			const SQmSkinSourceIdentity OldSource = pSkinContainer->m_State == CSkinContainer::EState::LOADED ? pSkinContainer->m_LoadedSource.value_or(RequestedSource) : RequestedSource;
			const SQmSkinSourceIdentity NewSource{Entry.m_Type == CSkinContainer::EType::DOWNLOAD, Entry.m_StorageType, Entry.m_LastModified};
			const ESkinSource ScannedSource = SkinSourceForScanEntry(m_SkinSources, Entry.m_Type == CSkinContainer::EType::LOCAL, Entry.m_Name.c_str());
			// 扫描结果来自磁盘，缓存已存在；下载开关只限制后续网络请求，不能把可用缓存重新标成未找到。
			const bool DownloadAllowed = true;
			const bool VanillaSkinsOnly = VanillaSkinsOnlyActive(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0);
			const auto RefreshState = CSkinContainer::SourceRefreshState(OldSource, NewSource, pSkinContainer->m_State, pSkinContainer->m_AlwaysLoaded, !VanillaSkinsOnly || pSkinContainer->m_Vanilla, DownloadAllowed);
			if(RefreshState.has_value())
			{
				pSkinContainer->m_pDownloadSession.reset();
				const auto Priority = pSkinContainer->m_LoadPriority;
				if(pSkinContainer->m_pLoadJob)
				{
					pSkinContainer->m_pLoadJob->Abort();
					pSkinContainer->m_pLoadJob.reset();
				}
				pSkinContainer->m_Type = Entry.m_Type;
				pSkinContainer->m_StorageType = Entry.m_StorageType;
				pSkinContainer->SetCatalogOnly(false);
				pSkinContainer->SetSkinSource(ScannedSource);
				ApplyCatalogSources(pSkinContainer);
				pSkinContainer->SetLastModified(Entry.m_LastModified);
				// 更新期间继续显示旧纹理，新数据上传完成后统一替换并通知所有预览。
				pSkinContainer->SetState(RefreshState.value(), Priority);
				DirectoryScanDirty = true;
			}
			continue;
		}

		CSkinContainer SkinContainer(this, Entry.m_Name.c_str(), Entry.m_Type, Entry.m_StorageType);
		auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
		pSkinContainer->SetCatalogOnly(false);
		pSkinContainer->SetSkinSource(SkinSourceForScanEntry(m_SkinSources, Entry.m_Type == CSkinContainer::EType::LOCAL, Entry.m_Name.c_str()));
		ApplyCatalogSources(pSkinContainer.get());
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

void CSkins::BuildSkinCatalogSources()
{
	m_vSkinCatalogs.clear();
	const auto AddCatalog = [&](const char *pUrl, const char *pCachePath, qm_skin_catalog::EFormat Format, ESkinSourceMask DisplaySources, ESkinSourceMask RequestSources) {
		const std::string UrlIdentity = SkinUrlIdentity(pUrl);
		if(UrlIdentity.empty())
			return;
		const auto Existing = std::find_if(m_vSkinCatalogs.begin(), m_vSkinCatalogs.end(), [&](const SSkinCatalogRuntime &Catalog) {
			return str_comp_nocase(SkinUrlIdentity(Catalog.m_Source.m_Url.c_str()).c_str(), UrlIdentity.c_str()) == 0;
		});
		if(Existing != m_vSkinCatalogs.end())
		{
			Existing->m_Source.m_DisplaySources |= DisplaySources;
			Existing->m_Source.m_RequestSources |= RequestSources;
			if(Format == qm_skin_catalog::EFormat::DDSTATS_HTML)
				Existing->m_Source.m_Format = Format;
			return;
		}

		SSkinCatalogRuntime Catalog;
		Catalog.m_Source.m_Url = pUrl;
		Catalog.m_Source.m_CachePath = pCachePath;
		Catalog.m_Source.m_Format = Format;
		Catalog.m_Source.m_DisplaySources = DisplaySources;
		Catalog.m_Source.m_RequestSources = RequestSources;
		m_vSkinCatalogs.push_back(std::move(Catalog));
	};

	AddCatalog(OFFICIAL_SKIN_INDEX_URL, OFFICIAL_SKIN_INDEX_CACHE_PATH, qm_skin_catalog::EFormat::DDNET_JSON,
		ESkinSourceMask::NONE, ESkinSourceMask::NONE);
	const std::string DdStatsCachePath = SkinCatalogCachePath("ddstats_skins", g_Config.m_QmDdStatsSkinDownloadUrl);
	AddCatalog(g_Config.m_QmDdStatsSkinDownloadUrl, DdStatsCachePath.c_str(), DetectDirectoryCatalogFormat(g_Config.m_QmDdStatsSkinDownloadUrl),
		ESkinSourceMask::DDSTATS, ESkinSourceMask::DDSTATS);
	if(!SameSkinUrl(g_Config.m_ClSkinDownloadUrl, DEFAULT_OFFICIAL_SKIN_URL))
	{
		const std::string CustomOfficialCachePath = SkinCatalogCachePath("custom_official_skins", g_Config.m_ClSkinDownloadUrl);
		const qm_skin_catalog::EFormat Format = DetectDirectoryCatalogFormat(g_Config.m_ClSkinDownloadUrl);
		const ESkinSourceMask KnownDisplaySources = Format == qm_skin_catalog::EFormat::DDSTATS_HTML ? ESkinSourceMask::DDSTATS : ESkinSourceMask::NONE;
		AddCatalog(g_Config.m_ClSkinDownloadUrl, CustomOfficialCachePath.c_str(), Format,
			ESkinSourceMask::CUSTOM | KnownDisplaySources, ESkinSourceMask::OFFICIAL | KnownDisplaySources);
	}
	if(!SameSkinUrl(g_Config.m_ClSkinCommunityDownloadUrl, DEFAULT_COMMUNITY_SKIN_URL))
	{
		const std::string CustomCommunityCachePath = SkinCatalogCachePath("custom_community_skins", g_Config.m_ClSkinCommunityDownloadUrl);
		const qm_skin_catalog::EFormat Format = DetectDirectoryCatalogFormat(g_Config.m_ClSkinCommunityDownloadUrl);
		const ESkinSourceMask KnownDisplaySources = Format == qm_skin_catalog::EFormat::DDSTATS_HTML ? ESkinSourceMask::DDSTATS : ESkinSourceMask::NONE;
		AddCatalog(g_Config.m_ClSkinCommunityDownloadUrl, CustomCommunityCachePath.c_str(), Format,
			ESkinSourceMask::CUSTOM | KnownDisplaySources, ESkinSourceMask::COMMUNITY | KnownDisplaySources);
	}
}

void CSkins::LoadSkinCatalogCaches()
{
	for(SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
		LoadSkinCatalogCache(Catalog);
}

bool CSkins::LoadSkinCatalogCache(SSkinCatalogRuntime &Catalog)
{
	CQmCheckedFileData Cache;
	if(!Cache.Read(*Storage(), Catalog.m_Source.m_CachePath.c_str(), IStorage::TYPE_SAVE, SKIN_CATALOG_MAX_BYTES))
		return false;
	SSkinCatalogSnapshot Snapshot;
	const bool Valid = ParseSkinCatalog(Catalog.m_Source, Cache.Data(), Cache.Size(), Snapshot);
	if(Valid)
		PublishSkinCatalog(Catalog, std::move(Snapshot));
	return Valid;
}

bool CSkins::ParseSkinCatalog(const SSkinCatalogSource &Source, const void *pFileData, size_t FileSize, SSkinCatalogSnapshot &Snapshot) const
{
	if(pFileData == nullptr || FileSize == 0 || FileSize > SKIN_CATALOG_MAX_BYTES)
		return false;
	std::vector<qm_skin_catalog::SEntry> vEntries;
	const char *pData = static_cast<const char *>(pFileData);
	switch(Source.m_Format)
	{
	case qm_skin_catalog::EFormat::DDNET_JSON:
		vEntries = qm_skin_catalog::ParseDdnetJson(pData, FileSize);
		break;
	case qm_skin_catalog::EFormat::DDSTATS_HTML:
		vEntries = qm_skin_catalog::MakeCatalogEntries(
			qm_skin_catalog::ParseDdStatsCatalogNames(std::string_view(pData, FileSize)),
			Source.m_DisplaySources, Source.m_RequestSources);
		break;
	case qm_skin_catalog::EFormat::DIRECTORY_HTML:
		vEntries = qm_skin_catalog::MakeCatalogEntries(
			qm_skin_catalog::ParseDirectoryCatalogNames(std::string_view(pData, FileSize)),
			Source.m_DisplaySources, Source.m_RequestSources);
		break;
	}
	Snapshot.m_Entries.reserve(vEntries.size());
	for(auto &Entry : vEntries)
	{
		if(!Entry.m_Name.empty() && CSkin::IsValidName(Entry.m_Name.c_str()))
			qm_skin_catalog::MergeEntry(Snapshot.m_Entries, std::move(Entry));
	}
	if(Snapshot.m_Entries.empty())
		return false;
	Snapshot.m_Loaded = true;
	Snapshot.m_ContentHash = sha256(pFileData, FileSize);
	return true;
}

void CSkins::QueueSkinCatalogRequests()
{
	for(SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
	{
		if(Catalog.m_RequestIssued)
			continue;
		Catalog.m_pRequest = HttpGetBoth(
			Catalog.m_Source.m_Url.c_str(), Storage(), Catalog.m_Source.m_CachePath.c_str(), IStorage::TYPE_SAVE);
		Catalog.m_pRequest->Timeout(CTimeout{10000, 30000, 8192, 10});
		Catalog.m_pRequest->MaxResponseSize(SKIN_CATALOG_MAX_BYTES);
		Catalog.m_pRequest->SkipByFileTime(Catalog.m_Snapshot.m_Loaded);
		Catalog.m_pRequest->ValidateBeforeOverwrite(true);
		Catalog.m_pRequest->LogProgress(HTTPLOG::NONE);
		Catalog.m_pRequest->FailOnErrorStatus(false);
		Http()->Run(Catalog.m_pRequest);
		Catalog.m_RequestIssued = true;
	}
}

void CSkins::ProcessSkinCatalogRequests()
{
	for(SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
	{
		if(Catalog.m_pRequest == nullptr || !Catalog.m_pRequest->Done())
			continue;
		// 请求完成不等于 HTTP 成功：断网、TLS 失败或中止都会进入非 DONE 终态。
		// 此时只能读取 CompletedStatusCode，StatusCode 会触发“Request not done”断言。
		auto pRequest = std::exchange(Catalog.m_pRequest, nullptr);
		const EHttpState RequestState = pRequest->State();
		const int StatusCode = pRequest->CompletedStatusCode();
		bool Success = false;
		if(RequestState == EHttpState::DONE)
		{
			CHttpResponseValidation Validation(*pRequest);
			SSkinCatalogSnapshot Snapshot;
			if(StatusCode == 200)
			{
				unsigned char *pData = nullptr;
				size_t Size = 0;
				pRequest->Result(&pData, &Size);
				Success = ParseSkinCatalog(Catalog.m_Source, pData, Size, Snapshot);
			}
			else if(StatusCode == 304)
				Success = Catalog.m_Snapshot.m_Loaded;
			Success = Success && Validation.Commit();
			if(Success && Snapshot.m_Loaded)
				PublishSkinCatalog(Catalog, std::move(Snapshot));
		}
		if(!Success)
		{
			log_warn("skins", "Failed to load skin catalog %s (state=%d, HTTP %d); keeping cached entries",
				Catalog.m_Source.m_Url.c_str(), (int)RequestState, StatusCode);
		}
	}
}

void CSkins::PublishSkinCatalog(SSkinCatalogRuntime &Catalog, SSkinCatalogSnapshot Snapshot)
{
	if(Catalog.m_Snapshot.m_ContentHash == Snapshot.m_ContentHash)
		return;
	Catalog.m_Snapshot = std::move(Snapshot);
	RefreshSkinCatalogSourceMasks();
	for(const auto &[_, Entry] : Catalog.m_Snapshot.m_Entries)
		m_vPendingSkinCatalogEntries.push_back(Entry);
	m_SkinList.ForceRefresh();
	log_info("skins", "Loaded skin catalog %s: %" PRIzu " names", Catalog.m_Source.m_Url.c_str(), Catalog.m_Snapshot.m_Entries.size());
}

ESkinSourceMask CSkins::CatalogSourcesForName(const char *pName, bool RequestSources) const
{
	ESkinSourceMask Sources = ESkinSourceMask::NONE;
	if(pName == nullptr)
		return Sources;
	const std::string Key = CSkinNameIndex::Key(pName);
	for(const SSkinCatalogRuntime &Catalog : m_vSkinCatalogs)
	{
		const auto Entry = Catalog.m_Snapshot.m_Entries.find(Key);
		if(Entry == Catalog.m_Snapshot.m_Entries.end())
			continue;
		Sources |= RequestSources ? Entry->second.m_RequestSources : Entry->second.m_DisplaySources;
	}
	return Sources;
}

void CSkins::ApplyCatalogSources(CSkinContainer *pSkinContainer) const
{
	if(pSkinContainer == nullptr)
		return;
	pSkinContainer->SetCatalogSources(
		CatalogSourcesForName(pSkinContainer->Name(), false),
		CatalogSourcesForName(pSkinContainer->Name(), true));
}

void CSkins::RefreshSkinCatalogSourceMasks()
{
	for(auto &[_, pSkinContainer] : m_Skins)
	{
		ApplyCatalogSources(pSkinContainer.get());
		if(pSkinContainer->IsCatalogOnly() && pSkinContainer->State() == CSkinContainer::EState::NOT_FOUND)
			pSkinContainer->SetState(CSkinContainer::EState::UNLOADED);
	}
}

void CSkins::ProcessPendingSkinCatalogEntries()
{
	if(m_SkinCatalogMergeCursor >= m_vPendingSkinCatalogEntries.size())
		return;

	bool Dirty = false;
	SSettingsResourceMergeBudget MergeBudget;
	MergeBudget.m_MaxListEntries = SKIN_CATALOG_MERGE_BUDGET;
	while(m_SkinCatalogMergeCursor < m_vPendingSkinCatalogEntries.size() &&
		SettingsResourceConsumeMergeEntry(MergeBudget, SettingsFrameBudgetOrNull(GameClient())))
	{
		const qm_skin_catalog::SEntry &Entry = m_vPendingSkinCatalogEntries[m_SkinCatalogMergeCursor++];
		m_SkinNameIndex.Add(Entry.m_Name.c_str(), 0);
		m_SkinNotFoundKeys.erase(CSkinNameIndex::Key(Entry.m_Name.c_str()));

		auto ExistingSkin = m_Skins.find(Entry.m_Name);
		if(ExistingSkin == m_Skins.end())
		{
			const CSkinContainer::EType Type = PreferredContainerType(Entry.m_Name.c_str());
			CSkinContainer SkinContainer(this, Entry.m_Name.c_str(), Type, ContainerStorageTypeFor(Type));
			auto pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
			pSkinContainer->SetCatalogOnly(Type == CSkinContainer::EType::DOWNLOAD);
			ApplyCatalogSources(pSkinContainer.get());
			pSkinContainer->SetOfficialReleaseDate(Entry.m_OfficialReleaseDate);
			pSkinContainer->SetOfficialCreator(Entry.m_OfficialCreator.c_str());
			pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
			m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)});
		}
		else
		{
			CSkinContainer *pSkinContainer = ExistingSkin->second.get();
			ApplyCatalogSources(pSkinContainer);
			if(Entry.m_OfficialReleaseDate != 0)
				pSkinContainer->SetOfficialReleaseDate(Entry.m_OfficialReleaseDate);
			if(!Entry.m_OfficialCreator.empty())
				pSkinContainer->SetOfficialCreator(Entry.m_OfficialCreator.c_str());
		}
		Dirty = true;
	}
	if(Dirty)
		m_SkinList.ForceRefresh();
	if(m_SkinCatalogMergeCursor >= m_vPendingSkinCatalogEntries.size())
	{
		m_vPendingSkinCatalogEntries.clear();
		m_SkinCatalogMergeCursor = 0;
	}
}

void CSkins::ProcessSkinListPlanJob()
{
	if(!SkinListPlanQueryCurrent())
	{
		m_SkinList.ForceRefresh();
		if(m_pSkinListPlanJob)
			m_pSkinListPlanJob->Abort();
		m_pSkinListPlanJob.reset();
		m_vPendingSkinListEntries.clear();
		// 查询条件（筛选/搜索/排序/目标角色/颜色）变化时允许一次整体重置；
		// 同一计划内的目录或下载状态变化只做原位填充，不重排已发布前缀。
		m_SkinList.ClearPlan();
	}
	if(m_pSkinListPlanJob && m_pSkinListPlanJob->Finished() && m_pSkinListPlanJob->State() != IJob::STATE_DONE)
	{
		m_pSkinListPlanJob.reset();
		m_SkinList.ForceRefresh();
		return;
	}
	if(m_pSkinListPlanJob && m_pSkinListPlanJob->State() == IJob::STATE_DONE)
	{
		auto Result = m_pSkinListPlanJob->TakeResult();
		LogSkinSettingsResourcePerf("complete", (int)Result.m_Plan.m_vNames.size(), (int)Result.m_UnfilteredCount, 0, ESettingsWarmupMissReason::NONE, 0.0);
		m_pSkinListPlanJob.reset();
		if(Result.m_Generation == m_SkinListPlanGeneration)
		{
			ApplySkinListPlan(std::move(Result.m_Plan.m_vEntries), Result.m_UnfilteredCount);
		}
		else
			m_SkinList.ForceRefresh();
	}

	if(m_SkinList.m_NeedsUpdate && m_pSkinListPlanJob == nullptr && m_SkinList.FullIndexMerged() &&
		m_HasInitialSkinDirectoryScan && m_vPendingSkinDirectoryEntries.empty() && m_vPendingSkinCatalogEntries.empty())
	{
		m_vPendingSkinListEntries.clear();
		QueueSkinListPlanJob(m_SkinList.m_RequestedDummy);
		m_SkinList.m_NeedsUpdate = false;
		return;
	}

	if(m_SkinList.PlanKnown() && !m_SkinList.FullIndexMerged())
		MergeSkinListEntries();
}

void CSkins::ApplySkinListPlan(std::vector<SSettingsSkinListEntry> &&vPlannedEntries, int UnfilteredCount)
{
	m_SkinList.ResetPlan(std::move(vPlannedEntries), UnfilteredCount);
	// 先按计划长度建整表，再一次性发布；发布后 m_vSkins 只原位填充，任何后续
	// merge 都不会清空或重排已出现的前缀。
	m_vPendingSkinListEntries.clear();
	m_vPendingSkinListEntries.resize(m_SkinList.m_vSkins.size());
	const int FirstPageEntries = minimum((int)m_SkinList.m_vPlannedEntries.size(), SettingsSkinListFirstPageEntryCount(SETTINGS_SKIN_LIST_MAX_COLUMNS));
	for(int Index = 0; Index < FirstPageEntries; ++Index)
	{
		if(MaterializeSkinListEntry(Index))
			m_vPendingSkinListEntries[Index] = m_SkinList.m_vSkins[Index];
	}
	m_SkinList.m_vSkins = std::move(m_vPendingSkinListEntries);
	m_SkinList.m_MergeCursor = FirstPageEntries;
	// 计划完成即请求首屏名字，不去等目录/纹理或全量 merge 完成。
	for(int Index = 0; Index < FirstPageEntries; ++Index)
	{
		if(m_SkinList.m_vSkins[Index].SkinContainer() != nullptr)
			m_SkinList.m_vSkins[Index].RequestLoad(ESettingsResourcePriority::VISIBLE);
	}
	if(m_SkinList.m_vPlannedEntries.empty())
		FinishSkinListMaterialization();
}

bool CSkins::MergeSkinListEntries()
{
	const size_t TotalEntries = m_SkinList.m_vPlannedEntries.size();
	if(m_SkinList.m_FullIndexMerged || m_SkinList.m_MergeCursor >= TotalEntries)
	{
		FinishSkinListMaterialization();
		return false;
	}

	const auto MergeStartTime = time_get_nanoseconds();
	const auto MergeDeadline = MergeStartTime + std::chrono::microseconds(SETTINGS_SKIN_LIST_MERGE_BUDGET_US);
	SSettingsResourceMergeBudget MergeBudget;
	MergeBudget.m_MaxListEntries = SETTINGS_SKIN_LIST_MERGE_MAX_PER_FRAME;
	size_t MergedEntries = 0;
	while(m_SkinList.m_MergeCursor < TotalEntries && MergedEntries < (size_t)SETTINGS_SKIN_LIST_MERGE_MAX_PER_FRAME)
	{
		// 每 8 条检查一次墙钟，避免逐条 time_get 的开销破坏预算本身的意义。
		if(MergedEntries > 0 && MergedEntries % 8 == 0 && time_get_nanoseconds() >= MergeDeadline)
			break;
		// 列表物化使用独立墙钟预算，不再叠加 warmup 的全局 merge token：
		// 否则设置页预热被关闭或 token 用尽时，列表会重新退回整块等待。
		if(!SettingsResourceConsumeMergeEntry(MergeBudget))
			break;

		const size_t Index = m_SkinList.m_MergeCursor++;
		++MergedEntries;
		if(m_SkinList.m_vSkins[Index].SkinContainer() != nullptr)
			continue;
		const SSettingsSkinListEntry &PlannedEntry = m_SkinList.m_vPlannedEntries[Index];
		const auto SkinIt = m_Skins.find(PlannedEntry.m_Name);
		if(SkinIt == m_Skins.end())
			continue;

		m_SkinList.m_vSkins[Index] = MakeSkinListEntry(PlannedEntry, SkinIt->second.get());
		++m_SkinList.m_MaterializedCount;
		++m_SkinList.m_MaterializedRevision;
	}
	LogSkinSettingsResourcePerf("merge", (int)MergedEntries, SETTINGS_SKIN_LIST_MERGE_MAX_PER_FRAME, (int)(TotalEntries - m_SkinList.m_MergeCursor), m_SkinList.m_MergeCursor < TotalEntries ? ESettingsWarmupMissReason::JOB_RESULT_PENDING : ESettingsWarmupMissReason::NONE, 0.0);
	if(m_SkinList.m_MergeCursor >= TotalEntries)
		FinishSkinListMaterialization();
	return MergedEntries > 0;
}

void CSkins::FinishSkinListMaterialization()
{
	if(m_SkinList.m_FullIndexMerged)
		return;
	m_SkinList.m_FullIndexMerged = true;
	// 全量索引完成时推一次结构版本，让收藏/最近集合与整表统计重新结算。
	++m_SkinList.m_Revision;
	++m_SkinList.m_MaterializedRevision;
}

CSkins::CSkinListEntry CSkins::MakeSkinListEntry(const SSettingsSkinListEntry &PlannedEntry, const CSkinContainer *pSkinContainer) const
{
	const std::optional<CSkinListEntry::SColorKey> ColorKey = PlannedEntry.m_ColorKey.has_value() ? std::make_optional(MakeSkinListColorKey(PlannedEntry.m_ColorKey.value())) : std::nullopt;
	std::optional<std::pair<int, int>> NameMatch;
	if(PlannedEntry.m_NameMatchStart >= 0 && PlannedEntry.m_NameMatchLength > 0)
		NameMatch = std::make_pair(PlannedEntry.m_NameMatchStart, PlannedEntry.m_NameMatchLength);
	return CSkinListEntry(
		const_cast<CSkinContainer *>(pSkinContainer),
		PlannedEntry.m_Favorite,
		PlannedEntry.m_SelectedMain,
		PlannedEntry.m_SelectedDummy,
		ColorKey,
		NameMatch);
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

const CSkins::CSkinContainer *CSkins::FindContainerOrNullptr(const char *pName, bool RequestLoad)
{
	const char *pSkinPrefix = SkinPrefix();
	if(pSkinPrefix[0] != '\0')
	{
		char aNameWithPrefix[2 * MAX_SKIN_LENGTH + 2]; // Larger than skin name length to allow IsValidName to check if it's too long
		str_format(aNameWithPrefix, sizeof(aNameWithPrefix), "%s_%s", pSkinPrefix, pName);
		// If we find something, use it, otherwise fall back to normal skins.
		const CSkinContainer *pSkinContainer = FindContainerImpl(aNameWithPrefix, RequestLoad);
		if(pSkinContainer != nullptr && pSkinContainer->Skin() != nullptr)
		{
			return pSkinContainer;
		}
	}
	return FindContainerImpl(pName, RequestLoad);
}

const CSkins::CSkinContainer *CSkins::FindContainerImpl(const char *pName, bool RequestLoad)
{
	if(!CSkin::IsValidName(pName))
	{
		return nullptr;
	}

	const char *pCanonicalName = CanonicalSkinName(pName);
	auto ExistingSkin = m_Skins.find(pCanonicalName);
	if(ExistingSkin == m_Skins.end())
	{
		const CSkinContainer::EType Type = PreferredContainerType(pCanonicalName);
		CSkinContainer SkinContainer(this, pCanonicalName, Type, ContainerStorageTypeFor(Type));
		auto &&pSkinContainer = std::make_unique<CSkinContainer>(std::move(SkinContainer));
		ApplyCatalogSources(pSkinContainer.get());
		pSkinContainer->SetState(pSkinContainer->DetermineInitialState());
		ExistingSkin = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;
	}
	else
	{
		ApplyCatalogSources(ExistingSkin->second.get());
	}
	if(RequestLoad && m_SkinNotFoundKeys.find(CSkinNameIndex::Key(pName)) == m_SkinNotFoundKeys.end())
		ExistingSkin->second->RequestLoad(true);
	return ExistingSkin->second.get();
}

const CSkin *CSkins::FindOrNullptr(const char *pName)
{
	const CSkinContainer *pSkinContainer = FindContainerOrNullptr(pName);
	if(pSkinContainer == nullptr || pSkinContainer->m_pSkin == nullptr)
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

bool CSkins::ShuffleSkinQueue(int Dummy)
{
	if(Dummy < 0 || Dummy >= NUM_DUMMIES || m_aSkinQueue[Dummy].size() < 2)
		return false;
	SkinQueueIndexVar(Dummy) = ShuffleSkinQueueEntries(m_aSkinQueue[Dummy], SkinQueueIndexVar(Dummy), (uint32_t)rand());
	// 服务器预设是动态快照，编辑顺序后转为自定义队列，防止下一次同步覆盖排序。
	SkinQueueRotateMapVar(Dummy) = 0;
	if(m_aAppliedSkinQueuePresetIndex[Dummy] == (int)SKIN_QUEUE_SERVER_PRESET)
		m_aAppliedSkinQueuePresetIndex[Dummy] = -1;
	m_aSkinQueueDirty[Dummy] = true;
	m_SkinList.ForceRefresh();
	return true;
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

static bool SkinQueuePresetNameInUse(const std::vector<CSkins::CSkinQueuePreset> &vPresets, const char *pName, const CSkins::CSkinQueuePreset *pIgnoredPreset = nullptr)
{
	return std::any_of(vPresets.begin(), vPresets.end(), [&](const CSkins::CSkinQueuePreset &Preset) {
		return &Preset != pIgnoredPreset && Preset.Kind() == CSkins::CSkinQueuePreset::EKind::USER && Preset.m_Name == pName;
	});
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
		str_format(aPresetName, sizeof(aPresetName), Localize("Preset %d"), maximum(1, (int)Presets.size() - 1));
	else
		str_copy(aPresetName, pName, sizeof(aPresetName));

	if(SkinQueuePresetNameInUse(Presets, aPresetName))
		return true;

	Presets.push_back({aPresetName, {}, CSkinQueuePreset::EKind::USER});
	return true;
}

bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, int Dummy)
{
	return AddSkinQueuePresetItem(PresetIndex, pSkinName, false, 0, 0, Dummy);
}

bool CSkins::AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, int)
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
	// 预设由本体和分身共享，读取时使用预设容量，不受任一播放队列长度影响。
	if(Queue.size() >= SKIN_QUEUE_HARD_LIMIT)
		return false;
	CSkinQueueEntry Entry = MakeSkinQueueEntry(pSkinName, UseCustomColor, ColorBody, ColorFeet);
	if(std::find(Queue.begin(), Queue.end(), Entry) == Queue.end())
		Queue.push_back(std::move(Entry));
	return true;
}

bool CSkins::AddSkinQueuePresetFromCurrent(int Dummy)
{
	const auto &Queue = m_aSkinQueue[Dummy];
	auto &Presets = m_vSkinQueuePresets;
	if(Queue.empty() || Presets.size() >= SKIN_QUEUE_PRESET_HARD_LIMIT)
		return false;

	char aPresetName[MAX_SKIN_LENGTH];
	const char *pNameFormat = Localize("Preset %d");
	const int FirstNumber = maximum(1, (int)Presets.size() - 1);
	for(size_t Offset = 0; Offset <= Presets.size(); ++Offset)
	{
		str_format(aPresetName, sizeof(aPresetName), pNameFormat, FirstNumber + (int)Offset);
		if(SkinQueuePresetNameInUse(Presets, aPresetName))
			continue;
		Presets.push_back({aPresetName, Queue, CSkinQueuePreset::EKind::USER});
		m_aAppliedSkinQueuePresetIndex[Dummy] = (int)Presets.size() - 1;
		m_aSkinQueueDirty[Dummy] = false;
		return true;
	}
	return false;
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
	if(aTrimmedName[0] == '\0' || SkinQueuePresetNameInUse(Presets, aTrimmedName, &Presets[PresetIndex]))
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
	// 显式应用只更新指定目标；列表选择本身不改变正在使用的队列。
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
	if(VanillaSkinsOnlyActive(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0))
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

	m_Data.ReadLastModified(m_pSkins->Storage(), aPath, m_StorageType);
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

std::unique_ptr<CQmSkinDownloadSession> CSkins::TryCreateTrackedSkinDownloadSession(const CSkinContainer *pSkinContainer)
{
	dbg_assert(pSkinContainer != nullptr, "Download session requires a skin container");
	const char *pName = pSkinContainer->Name();
	return QmCreateTrackedSkinDownloadSession(m_vSkinDownloadNames, pName, [&] {
		const std::string Name = CanonicalSkinName(pName);
		char aEscapedName[256];
		EscapeUrl(aEscapedName, Name.c_str());
		const ESkinSourceMask RequestSources = pSkinContainer->EffectiveRequestSources();
		std::vector<CQmSkinDownloadSession::SSource> vSources;
		const auto AddSource = [&](const char *pBaseUrl, ESkinSource Slot) {
			if(pBaseUrl == nullptr || pBaseUrl[0] == '\0')
				return;
			// 开关授权按配置槽位判断，来源身份只用于记录和列表归属。这样同一
			// DDStats URL 同时作为社区槽时，社区开关开启仍能发起请求。
			if(!CSkinContainer::RequestSourcesAllowDownloadSlot(RequestSources, Slot))
				return;
			const ESkinSource Source = Slot == ESkinSource::DDSTATS ? ESkinSource::DDSTATS : ClassifyDownloadSource(Slot, pBaseUrl);
			vSources.push_back({std::string(pBaseUrl) + aEscapedName + ".png", Source});
		};
		if(g_Config.m_ClDownloadSkins)
			AddSource(g_Config.m_ClSkinDownloadUrl, ESkinSource::OFFICIAL);
		if(g_Config.m_ClDownloadCommunitySkins)
			AddSource(g_Config.m_ClSkinCommunityDownloadUrl, ESkinSource::COMMUNITY);
		if(g_Config.m_QmDownloadDdStatsSkins)
			AddSource(g_Config.m_QmDdStatsSkinDownloadUrl, ESkinSource::DDSTATS);
		const std::string Path = "downloadedskins/" + Name + ".png";
		std::string CacheUrl;
		if(const auto Verified = m_VerifiedSkinCacheUrls.find(Name); Verified != m_VerifiedSkinCacheUrls.end())
		{
			time_t Created = 0, Modified = 0;
			if(Storage()->RetrieveTimes(Path.c_str(), IStorage::TYPE_SAVE, &Created, &Modified) && Modified != 0 && Modified == Verified->second.second)
				CacheUrl = Verified->second.first;
		}
		return std::make_unique<CQmSkinDownloadSession>(*Http(), std::move(vSources), std::make_shared<CQmSkinDownloadJob>(Storage(), Name.c_str(), PrepareSkinData), [this, Path](const char *pUrl, bool Unconditional) -> std::shared_ptr<IHttpRequest> {
			auto pRequest = HttpGetBoth(pUrl, Storage(), Path.c_str(), IStorage::TYPE_SAVE);
			pRequest->SkipByFileTime(!Unconditional);
			return pRequest; }, [this, Name](std::shared_ptr<IHttpRequest> pResponse) -> std::shared_ptr<IQmSkinDataJob> { return std::make_shared<CQmSkinDownloadJob>(Storage(), Name.c_str(), PrepareSkinData, std::move(pResponse)); }, [this](std::shared_ptr<IJob> pJob) { Engine()->AddJob(std::move(pJob)); }, std::move(CacheUrl));
	});
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
			char aEscapedName[2 * MAX_SKIN_LENGTH];
			char *pDst = aEscapedName;
			str_escape(&pDst, Preset.m_Name.c_str(), aEscapedName + sizeof(aEscapedName));
			char aBuffer[64 + sizeof(aEscapedName)];
			str_format(aBuffer, sizeof(aBuffer), "add_skin_queue_preset \"%s\"", aEscapedName);
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
