#include "assets_author_persistence.h"
#include "assets_preview_scale.h"
#include "assets_resource_registry.h"
#include "background.h"
#include "menus.h"
#include "qmclient/perf_logging.h"
#include "settings_resource_jobs.h"

#include <base/lock.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace FontIcons;
using namespace std::chrono_literals;

static void CollectEntityBgPreviewPaths(IStorage *pStorage, const char *pAssetName, std::vector<std::string> &vOutPaths);

namespace
{
	const char *AssetsSettingsTabName(int Tab)
	{
		switch(Tab)
		{
		case ASSETS_TAB_ENTITIES: return "entities";
		case ASSETS_TAB_GAME: return "game";
		case ASSETS_TAB_EMOTICONS: return "emoticons";
		case ASSETS_TAB_PARTICLES: return "particles";
		case ASSETS_TAB_HUD: return "hud";
		case ASSETS_TAB_GUI_CURSOR: return "gui_cursor";
		case ASSETS_TAB_ARROW: return "arrow";
		case ASSETS_TAB_STRONG_WEAK: return "strong_weak";
		case ASSETS_TAB_ENTITY_BG: return "entity_bg";
		case ASSETS_TAB_EXTRAS: return "extras";
		default: return "unknown";
		}
	}

	bool AssetsPerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogAssetsPerfStage(const IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!AssetsPerfDebugEnabled())
			return;
		QmPerfLogStage("perf/assets", pStage, DurationMs, Force, pClient, "assets", nullptr, pExtra);
	}

	void LogAssetsPerfStageForClient(const IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		if(!AssetsPerfDebugEnabled())
			return;
		QmPerfLogStage("perf/assets", pStage, DurationMs, Force, pClient, "assets", nullptr, pExtra);
	}

	const char *AssetsResourcePriorityName(ESettingsResourcePriority Priority)
	{
		switch(Priority)
		{
		case ESettingsResourcePriority::BACKGROUND: return "background";
		case ESettingsResourcePriority::PREFETCH: return "prefetch";
		case ESettingsResourcePriority::VISIBLE: return "visible";
		}
		return "background";
	}

	const char *AssetsResourceFrameContextName(const SSettingsResourceFrameContext &Context)
	{
		if(Context.m_ScrollActive)
			return "scroll";
		if(Context.m_PostScrollRecoveryFrames > 0)
			return "recovery";
		return "idle";
	}

	struct SSettingsAssetsVisibleAdmission
	{
		int m_FirstCombined = -1;
		int m_EndCombined = -1;
		int m_LocalCount = 0;
		int m_VisibleStarts = 0;
		int m_PrefetchStarts = 0;
		int m_BackgroundStarts = 0;

		bool IsCombinedVisible(int CombinedIndex) const
		{
			return m_FirstCombined >= 0 && CombinedIndex >= m_FirstCombined && CombinedIndex < m_EndCombined;
		}

		void Record(bool Visible, bool Prefetch)
		{
			if(Visible)
				++m_VisibleStarts;
			else if(Prefetch)
				++m_PrefetchStarts;
			else
				++m_BackgroundStarts;
		}
	};

	enum class EAssetsVisiblePreflightState
	{
		IDLE,
		PLANNING,
		WARMING_VISIBLE,
		READY_TO_SHOW,
		VISIBLE,
	};

	struct SSettingsAssetsVisiblePreflight
	{
		EAssetsVisiblePreflightState m_State = EAssetsVisiblePreflightState::IDLE;
		int m_VisibleCount = 0;
		int m_HalfVisibleCount = 0;
		int m_ReadyCount = 0;
		int m_NotReadyCount = 0;
		int m_ThumbStartsBeforeVisible = 0;
		int m_ThumbStartsDuringDraw = 0;
		bool m_VisibleReady = false;
		bool m_GeometryStable = false;
	};

	enum class ESettingsAssetsCardHydrationLayer
	{
		SHELL,
		METADATA,
		PREVIEW,
	};

	struct SSettingsAssetsCardShell
	{
		CUIRect m_CardRect;
		CUIRect m_TextureRect;
		CUIRect m_TitleRect;
		CUIRect m_AuthorRect;
		CUIRect m_ActionButtonRect;
		CUIRect m_StatusTagRect;
		CUIRect m_LocalOnlyBadgeRect;
		bool m_HasActionButton = false;
		bool m_HasStatusTag = false;
		bool m_HasAuthorRow = false;
		bool m_HasLocalOnlyBadge = false;
	};

	struct SSettingsAssetsCardCacheKey
	{
		std::string m_AssetId;
		int m_Tab = -1;
		uint64_t m_LocaleHash = 0;
		int m_UiScale = 0;
		int m_CardWidth = 0;
		unsigned m_StatusHash = 0;
		bool m_Installed = false;
		bool m_DownloadFailed = false;
		bool m_LocalOnly = false;

		bool operator==(const SSettingsAssetsCardCacheKey &Other) const
		{
			return m_Tab == Other.m_Tab &&
			       m_LocaleHash == Other.m_LocaleHash &&
			       m_UiScale == Other.m_UiScale &&
			       m_CardWidth == Other.m_CardWidth &&
			       m_StatusHash == Other.m_StatusHash &&
			       m_Installed == Other.m_Installed &&
			       m_DownloadFailed == Other.m_DownloadFailed &&
			       m_LocalOnly == Other.m_LocalOnly &&
			       m_AssetId == Other.m_AssetId;
		}
	};

	struct SSettingsAssetsCardCacheKeyHash
	{
		size_t operator()(const SSettingsAssetsCardCacheKey &Key) const
		{
			size_t Hash = std::hash<std::string>{}(Key.m_AssetId);
			Hash ^= std::hash<int>{}(Key.m_Tab) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<uint64_t>{}(Key.m_LocaleHash) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<int>{}(Key.m_UiScale) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<int>{}(Key.m_CardWidth) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<unsigned>{}(Key.m_StatusHash) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<bool>{}(Key.m_Installed) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<bool>{}(Key.m_DownloadFailed) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<bool>{}(Key.m_LocalOnly) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
			return Hash;
		}
	};

	struct SSettingsAssetsCardMetadataCacheEntry
	{
		std::string m_Title;
		std::string m_Author;
		std::string m_StatusLabel;
		std::unique_ptr<CUIElement> m_pTitleElement;
		std::unique_ptr<CUIElement> m_pAuthorElement;
		std::unique_ptr<CUIElement> m_pErrorElement;
		bool m_Installed = false;
		bool m_DownloadFailed = false;
		bool m_LocalOnly = false;
		bool m_Ready = false;
	};

	struct SSettingsAssetsCardMetadataRequest
	{
		SSettingsAssetsCardCacheKey m_Key;
		std::string m_Title;
		std::string m_Author;
		std::string m_StatusLabel;
		bool m_Installed = false;
		bool m_DownloadFailed = false;
		bool m_LocalOnly = false;
	};

	struct SSettingsAssetsCardPreviewState
	{
		IGraphics::CTextureHandle m_Texture;
		bool m_DrawEntityTileArtifact = false;
		bool m_DrawFolderIcon = false;
		bool m_FolderIsParent = false;
		bool m_FolderIsWorkshopRoot = false;
		bool m_EntityBgHeavyPreviewDeferred = false;
		bool m_Loading = false;
	};

	struct SSettingsAssetsCardHydrationScheduler
	{
		int m_MetadataBudget = 0;
		int m_PreviewBudget = 0;
		bool m_TabSwitchShellOnlyFrame = false;

		bool CanHydrateMetadata(bool Visible)
		{
			if(m_TabSwitchShellOnlyFrame && !Visible)
				return false;
			if(m_MetadataBudget <= 0)
				return false;
			--m_MetadataBudget;
			return true;
		}

		bool CanRenderMetadata(bool Visible, bool Cached)
		{
			if(m_TabSwitchShellOnlyFrame)
				return CanHydrateMetadata(Visible);
			if(Cached)
				return true;
			return CanHydrateMetadata(Visible);
		}

		bool CanHydratePreview(bool Visible, bool Ready, bool HeavyPreviewDeferred = false)
		{
			if(!Ready)
				return false;
			if(HeavyPreviewDeferred)
				return false;
			if(m_TabSwitchShellOnlyFrame)
				return false;
			if(m_PreviewBudget <= 0)
				return false;
			--m_PreviewBudget;
			return true;
		}

		bool CanRenderPreview(bool Visible, bool HasPreviewContent, bool HeavyPreviewDeferred = false)
		{
			if(HeavyPreviewDeferred)
				return false;
			if(!HasPreviewContent)
				return true;
			if(!m_TabSwitchShellOnlyFrame)
				return true;
			return CanHydratePreview(Visible, true, HeavyPreviewDeferred);
		}
	};

	static std::unordered_map<SSettingsAssetsCardCacheKey, SSettingsAssetsCardMetadataCacheEntry, SSettingsAssetsCardCacheKeyHash> gs_SettingsAssetsCardMetadataCache;
	static int gs_SettingsAssetsCardMetadataCacheHits = 0;
	static int gs_SettingsAssetsCardMetadataCacheMisses = 0;
	static int gs_SettingsAssetsCardMetadataCacheEvictions = 0;
	static std::deque<SSettingsAssetsCardMetadataRequest> gs_SettingsAssetsCardMetadataRequests;
	static std::unordered_set<SSettingsAssetsCardCacheKey, SSettingsAssetsCardCacheKeyHash> gs_SettingsAssetsCardMetadataRequestKeys;
	static CSettingsResourcePreviewCache gs_SettingsAssetsResourcePreviewCache;
	static CSettingsResourcePreviewUploadScheduler gs_SettingsAssetsResourcePreviewUploadScheduler;
	static std::unordered_map<SResourcePreviewKey, std::shared_ptr<CSettingsResourcePreviewJob>, SResourcePreviewKeyHash> m_vEntityBgPreviewJobs;

	static uint64_t AssetsCardLocaleHash()
	{
		// The cache is intentionally local to Assets cards. Use the localized stable labels
		// that affect card metadata width without tying dynamic names into settings text pool.
		uint64_t Hash = 1469598103934665603ull;
		for(const char *pText : {Localize("Downloaded"), Localize("Not downloaded"), Localize("Local-only"), Localize("Download failed")})
		{
			for(const char *p = pText; *p != '\0'; ++p)
			{
				Hash ^= (unsigned char)*p;
				Hash *= 1099511628211ull;
			}
		}
		return Hash;
	}

	static int AssetsUiScaleBucket(float UiScale)
	{
		return round_to_int((std::isfinite(UiScale) && UiScale > 0.0f ? UiScale : 1.0f) * 1000.0f);
	}

	static int AssetsCardWidthBucket(float CardWidth)
	{
		return round_to_int(std::isfinite(CardWidth) && CardWidth > 0.0f ? CardWidth : 1.0f);
	}

	static SSettingsAssetsCardCacheKey BuildAssetsCardCacheKey(const char *pAssetId, int Tab, float UiScale, float CardWidth, const char *pStatusLabel, bool Installed, bool DownloadFailed, bool LocalOnly)
	{
		SSettingsAssetsCardCacheKey Key;
		Key.m_AssetId = pAssetId != nullptr ? pAssetId : "";
		Key.m_Tab = Tab;
		Key.m_LocaleHash = AssetsCardLocaleHash();
		Key.m_UiScale = AssetsUiScaleBucket(UiScale);
		Key.m_CardWidth = AssetsCardWidthBucket(CardWidth);
		Key.m_StatusHash = str_quickhash(pStatusLabel != nullptr ? pStatusLabel : "");
		Key.m_Installed = Installed;
		Key.m_DownloadFailed = DownloadFailed;
		Key.m_LocalOnly = LocalOnly;
		return Key;
	}

	static SSettingsAssetsCardMetadataCacheEntry *FindAssetsCardMetadata(const SSettingsAssetsCardCacheKey &Key)
	{
		const auto It = gs_SettingsAssetsCardMetadataCache.find(Key);
		if(It == gs_SettingsAssetsCardMetadataCache.end() || !It->second.m_Ready)
		{
			++gs_SettingsAssetsCardMetadataCacheMisses;
			return nullptr;
		}
		++gs_SettingsAssetsCardMetadataCacheHits;
		return &It->second;
	}

	static void TrimAssetsCardMetadataCacheForInsert(const SSettingsAssetsCardCacheKey &Key)
	{
		if(gs_SettingsAssetsCardMetadataCache.size() < QM_ASSET_METADATA_CACHE_CAPACITY)
			return;
		if(gs_SettingsAssetsCardMetadataCache.find(Key) != gs_SettingsAssetsCardMetadataCache.end())
			return;
		gs_SettingsAssetsCardMetadataCache.erase(gs_SettingsAssetsCardMetadataCache.begin());
		++gs_SettingsAssetsCardMetadataCacheEvictions;
	}

	static SSettingsAssetsCardMetadataCacheEntry *HydrateAssetsCardMetadata(const SSettingsAssetsCardCacheKey &Key, const char *pTitle, const char *pAuthor, const char *pStatusLabel, bool Installed, bool DownloadFailed, bool LocalOnly)
	{
		TrimAssetsCardMetadataCacheForInsert(Key);
		SSettingsAssetsCardMetadataCacheEntry &Entry = gs_SettingsAssetsCardMetadataCache[Key];
		Entry.m_Title = pTitle != nullptr ? pTitle : "";
		Entry.m_Author = pAuthor != nullptr && pAuthor[0] != '\0' ? pAuthor : "--";
		Entry.m_StatusLabel = pStatusLabel != nullptr ? pStatusLabel : "";
		Entry.m_Installed = Installed;
		Entry.m_DownloadFailed = DownloadFailed;
		Entry.m_LocalOnly = LocalOnly;
		Entry.m_Ready = true;
		return &Entry;
	}

	static SSettingsAssetsCardMetadataCacheEntry *HydrateAssetsCardMetadataTimed(const SSettingsAssetsCardCacheKey &Key, const char *pTitle, const char *pAuthor, const char *pStatusLabel, bool Installed, bool DownloadFailed, bool LocalOnly, SResourcePreviewTelemetry &ResourcePreviewTelemetry)
	{
		CPerfTimer HydrateTimer;
		SSettingsAssetsCardMetadataCacheEntry *pEntry = HydrateAssetsCardMetadata(Key, pTitle, pAuthor, pStatusLabel, Installed, DownloadFailed, LocalOnly);
		ResourcePreviewTelemetry.m_MetadataHydrateMs += HydrateTimer.ElapsedMs();
		return pEntry;
	}

	static void QueueAssetsCardMetadataHydration(const SSettingsAssetsCardCacheKey &Key, const char *pTitle, const char *pAuthor, const char *pStatusLabel, bool Installed, bool DownloadFailed, bool LocalOnly)
	{
		if(FindAssetsCardMetadata(Key) != nullptr)
			return;
		if(gs_SettingsAssetsCardMetadataRequestKeys.find(Key) != gs_SettingsAssetsCardMetadataRequestKeys.end())
			return;
		SSettingsAssetsCardMetadataRequest Request;
		Request.m_Key = Key;
		Request.m_Title = pTitle != nullptr ? pTitle : "";
		Request.m_Author = pAuthor != nullptr && pAuthor[0] != '\0' ? pAuthor : "--";
		Request.m_StatusLabel = pStatusLabel != nullptr ? pStatusLabel : "";
		Request.m_Installed = Installed;
		Request.m_DownloadFailed = DownloadFailed;
		Request.m_LocalOnly = LocalOnly;
		gs_SettingsAssetsCardMetadataRequestKeys.insert(Request.m_Key);
		gs_SettingsAssetsCardMetadataRequests.push_back(std::move(Request));
	}

	static void RequestAssetsCardMetadataHydration(const SSettingsAssetsCardCacheKey &Key, const char *pTitle, const char *pAuthor, const char *pStatusLabel, bool Installed, bool DownloadFailed, bool LocalOnly, SSettingsAssetsCardHydrationScheduler &CardHydrationScheduler, CSettingsResourcePreviewScheduler &PreviewPipelineScheduler, SResourcePreviewTelemetry &ResourcePreviewTelemetry, bool CombinedVisible)
	{
		(void)CardHydrationScheduler;
		(void)PreviewPipelineScheduler;
		(void)ResourcePreviewTelemetry;
		(void)CombinedVisible;
		if(FindAssetsCardMetadata(Key) != nullptr)
			return;
		QueueAssetsCardMetadataHydration(Key, pTitle, pAuthor, pStatusLabel, Installed, DownloadFailed, LocalOnly);
	}

	static int DrainAssetsCardMetadataHydrationRequests(int MetadataLayoutTokens, SResourcePreviewTelemetry &ResourcePreviewTelemetry)
	{
		int Hydrated = 0;
		while(MetadataLayoutTokens > 0 && !gs_SettingsAssetsCardMetadataRequests.empty())
		{
			SSettingsAssetsCardMetadataRequest Request = std::move(gs_SettingsAssetsCardMetadataRequests.front());
			gs_SettingsAssetsCardMetadataRequests.pop_front();
			gs_SettingsAssetsCardMetadataRequestKeys.erase(Request.m_Key);
			if(FindAssetsCardMetadata(Request.m_Key) != nullptr)
				continue;
			HydrateAssetsCardMetadataTimed(Request.m_Key, Request.m_Title.c_str(), Request.m_Author.c_str(), Request.m_StatusLabel.c_str(), Request.m_Installed, Request.m_DownloadFailed, Request.m_LocalOnly, ResourcePreviewTelemetry);
			--MetadataLayoutTokens;
			++Hydrated;
		}
		return Hydrated;
	}

	static void RenderAssetsCardMetadataCached(const SSettingsAssetsCardShell &Shell, SSettingsAssetsCardMetadataCacheEntry *pMetadata, const std::function<void(const SSettingsAssetsCardShell &, SSettingsAssetsCardMetadataCacheEntry &, bool)> &RenderMetadata)
	{
		if(pMetadata != nullptr)
			RenderMetadata(Shell, *pMetadata, true);
	}

	constexpr float AssetsCardTitleFontSize = 9.0f;
	constexpr float AssetsCardAuthorFontSize = 7.0f;
	constexpr float AssetsCardStatusTagFontSize = 6.0f;
	constexpr float AssetsCardStatusTagMinWidth = 24.0f;
	constexpr float AssetsCardStatusTagMaxWidth = 36.0f;
	constexpr float AssetsCardStatusTagHorizontalPadding = 2.0f;
	constexpr ColorRGBA AssetsCardStatusReadyColor = ColorRGBA(0.18f, 0.62f, 0.32f, 0.88f);
	constexpr ColorRGBA AssetsCardStatusNetworkColor = ColorRGBA(0.35f, 0.45f, 0.56f, 0.86f);

	static void RenderAssetsCardMetadataFallback(CUi *pUi, const SSettingsAssetsCardShell &Shell, const char *pTitle, const char *pAuthor, const char *pStatusLabel, bool StatusReady, bool ShowAuthorRow)
	{
		if(pUi == nullptr)
			return;
		const char *pSafeTitle = pTitle != nullptr && pTitle[0] != '\0' ? pTitle : "--";
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = static_cast<int>(Shell.m_TitleRect.w);
		TitleProps.m_StopAtEnd = true;
		TitleProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Shell.m_TitleRect, pSafeTitle, AssetsCardTitleFontSize, TEXTALIGN_ML, TitleProps);
		if(ShowAuthorRow)
		{
			SLabelProperties AuthorProps;
			AuthorProps.m_MaxWidth = static_cast<int>(Shell.m_AuthorRect.w);
			AuthorProps.m_StopAtEnd = true;
			AuthorProps.m_EllipsisAtEnd = true;
			pUi->DoLabel(&Shell.m_AuthorRect, pAuthor != nullptr && pAuthor[0] != '\0' ? pAuthor : "--", AssetsCardAuthorFontSize, TEXTALIGN_ML, AuthorProps);
		}
		if(Shell.m_HasStatusTag && pStatusLabel != nullptr && pStatusLabel[0] != '\0')
		{
			CUIRect StatusRect = Shell.m_StatusTagRect;
			DrawRoundedSurface(pUi, StatusRect, StatusReady ? AssetsCardStatusReadyColor : AssetsCardStatusNetworkColor, ColorRGBA(), minimum(StatusRect.h / 2.0f, 6.0f));
			SLabelProperties StatusLabelProps;
			StatusLabelProps.m_MaxWidth = static_cast<int>(StatusRect.w - AssetsCardStatusTagHorizontalPadding * 2.0f);
			StatusLabelProps.m_StopAtEnd = true;
			StatusLabelProps.m_EllipsisAtEnd = true;
			pUi->DoLabel(&StatusRect, pStatusLabel, AssetsCardStatusTagFontSize, TEXTALIGN_MC, StatusLabelProps);
		}
	}

	constexpr int AssetsTabSwitchCooldownFrames = 8;

	static SSettingsAssetsCardHydrationScheduler BeginAssetsCardHydrationFrame(bool AssetsTabSwitchFirstFrame, bool AssetsTabSwitchCooldownActive, int VisibleCardCount, int MetadataLayoutTokens, int PreviewArtifactTokens)
	{
		SSettingsAssetsCardHydrationScheduler Scheduler;
		Scheduler.m_TabSwitchShellOnlyFrame = AssetsTabSwitchFirstFrame;
		if(AssetsTabSwitchFirstFrame)
		{
			Scheduler.m_MetadataBudget = maximum(1, minimum(VisibleCardCount, MetadataLayoutTokens));
			Scheduler.m_PreviewBudget = 0;
			return Scheduler;
		}
		Scheduler.m_MetadataBudget = maximum(0, minimum(VisibleCardCount, MetadataLayoutTokens));
		Scheduler.m_PreviewBudget = AssetsTabSwitchCooldownActive ? 0 : maximum(0, minimum(VisibleCardCount, PreviewArtifactTokens));
		return Scheduler;
	}

	static bool AssetsCardShellShowsAuthorRow(int Tab, bool WorkshopCard, bool IsDirectory)
	{
		(void)WorkshopCard;
		if(IsDirectory)
			return false;
		return Tab != ASSETS_TAB_ENTITY_BG;
	}

	static CUIRect AssetsCardListAreaWithStableScrollbar(CUIRect ListArea, float ScrollbarWidth, float ScrollbarMargin)
	{
		(void)ScrollbarMargin;
		if(ListArea.w > ScrollbarWidth)
			ListArea.w -= ScrollbarWidth;
		return ListArea;
	}

	static SResourcePreviewKey BuildAssetsResourcePreviewKey(const char *pAssetId, int Tab, bool Workshop, float UiScale, float CardWidth)
	{
		SResourcePreviewKey Key;
		Key.m_Type = "assets";
		Key.m_Id = pAssetId != nullptr ? pAssetId : "";
		Key.m_Workshop = Workshop;
		Key.m_LocaleHash = AssetsCardLocaleHash();
		Key.m_UiScale = AssetsUiScaleBucket(UiScale);
		Key.m_CardWidth = AssetsCardWidthBucket(CardWidth);
		Key.m_Type += ":";
		Key.m_Type += AssetsSettingsTabName(Tab);
		return Key;
	}

	static bool ResolveEntityBgPreviewArtifactSource(IStorage *pStorage, const char *pAssetName, std::string &Path)
	{
		Path.clear();
		if(pStorage == nullptr || pAssetName == nullptr || pAssetName[0] == '\0')
			return false;

		std::vector<std::string> vPossiblePaths;
		CollectEntityBgPreviewPaths(pStorage, pAssetName, vPossiblePaths);
		for(const std::string &Candidate : vPossiblePaths)
		{
			if(pStorage->FileExists(Candidate.c_str(), IStorage::TYPE_ALL))
			{
				Path = Candidate;
				return true;
			}
		}
		return false;
	}

	static bool StartAssetsEntityBgPreviewArtifactJob(const SResourcePreviewKey &PreviewKey, const char *pAssetName, IStorage *pStorage, int TargetTextureSize, IEngine *pEngine, SResourcePreviewTelemetry &ResourcePreviewTelemetry)
	{
		if(pAssetName == nullptr || pAssetName[0] == '\0' || pEngine == nullptr)
			return false;
		SResourcePreviewState &State = gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey);
		if(State.m_TextureReady || State.m_PreviewJobPending || State.m_UploadPending)
			return false;
		if(m_vEntityBgPreviewJobs.find(PreviewKey) != m_vEntityBgPreviewJobs.end())
			return false;

		std::string SourcePath;
		if(!ResolveEntityBgPreviewArtifactSource(pStorage, pAssetName, SourcePath))
			return false;
		auto pJob = CSettingsResourcePreviewJob::FromPath(PreviewKey.m_Id, std::move(SourcePath), pStorage, IStorage::TYPE_ALL, TargetTextureSize);
		m_vEntityBgPreviewJobs.emplace(PreviewKey, pJob);
		gs_SettingsAssetsResourcePreviewCache.MarkPreviewJobStarted(PreviewKey);
		pEngine->AddJob(pJob);
		++ResourcePreviewTelemetry.m_PreviewJobsStarted;
		return true;
	}

	static void ProcessAssetsResourcePreviewJobs(IGraphics *pGraphics, SResourcePreviewTelemetry &ResourcePreviewTelemetry, SResourcePreviewUploadBudget &UploadBudget)
	{
		for(auto It = m_vEntityBgPreviewJobs.begin(); It != m_vEntityBgPreviewJobs.end();)
		{
			const std::shared_ptr<CSettingsResourcePreviewJob> &pJob = It->second;
			if(!pJob || !pJob->Completed())
			{
				++It;
				continue;
			}
			CSettingsResourcePreviewJob::SResult Result = pJob->TakeResult();
			ResourcePreviewTelemetry.m_PreviewArtifactMs += Result.m_Artifact.m_DurationMs;
			if(Result.m_Artifact.m_Success)
				gs_SettingsAssetsResourcePreviewCache.MarkArtifactReady(It->first);
			else
				gs_SettingsAssetsResourcePreviewCache.MarkPreviewJobDone(It->first, false);
			if(Result.m_Artifact.m_Success)
				gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUpload(It->first, std::move(Result.m_Artifact.m_Image), It->first.m_Id.c_str());
			++ResourcePreviewTelemetry.m_PreviewJobsDone;
			It = m_vEntityBgPreviewJobs.erase(It);
		}
		gs_SettingsAssetsResourcePreviewUploadScheduler.Drain(UploadBudget, ResourcePreviewTelemetry, gs_SettingsAssetsResourcePreviewCache, pGraphics);
	}

}

void CMenus::ClearSettingsAssetsCardMetadataCache()
{
	gs_SettingsAssetsCardMetadataCache.clear();
	gs_SettingsAssetsCardMetadataRequests.clear();
	gs_SettingsAssetsCardMetadataRequestKeys.clear();
	gs_SettingsAssetsCardMetadataCacheHits = 0;
	gs_SettingsAssetsCardMetadataCacheMisses = 0;
	gs_SettingsAssetsCardMetadataCacheEvictions = 0;
}

typedef std::function<void()> TMenuAssetScanLoadedFunc;

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CImageDecodeJob : public IJob
{
public:
	struct SResult
	{
		CImageInfo m_Image;
		bool m_Success = false;
	};

private:
	std::vector<uint8_t> m_vFileData;
	std::string m_Name;
	mutable CLock m_Lock;
	SResult m_Result;
	mutable bool m_Completed = false;

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		if(m_vFileData.empty())
		{
			const CLockScope Lock(m_Lock);
			m_Completed = true;
			return;
		}

		CImageInfo Image;
		bool Success = false;

		constexpr uint8_t PngSignature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		constexpr uint8_t WebpRiff[] = {0x52, 0x49, 0x46, 0x46};
		constexpr uint8_t WebpWebp[] = {0x57, 0x45, 0x42, 0x50};

		const bool IsPng = m_vFileData.size() >= 8 &&
				   memcmp(m_vFileData.data(), PngSignature, 8) == 0;
		const bool IsWebp = m_vFileData.size() >= 12 &&
				    memcmp(m_vFileData.data(), WebpRiff, 4) == 0 &&
				    memcmp(m_vFileData.data() + 8, WebpWebp, 4) == 0;

		if(IsWebp)
		{
			Success = CImageLoader::LoadWebP(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image);
		}
		else if(IsPng)
		{
			Success = CImageLoader::LoadPng(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image);
		}
		else
		{
			if(CImageLoader::LoadWebP(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image))
				Success = true;
			else if(CImageLoader::LoadPng(m_vFileData.data(), m_vFileData.size(), m_Name.c_str(), Image))
				Success = true;
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = Success;
			m_Completed = true;
		}

		m_vFileData.clear();
		m_vFileData.shrink_to_fit();
	}

public:
	CImageDecodeJob(std::vector<uint8_t> &&vFileData, const char *pName) :
		m_vFileData(std::move(vFileData)),
		m_Name(pName)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	SResult GetResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

// 全异步图片加载 Job：在后台线程完成文件读取和解码
// NOLINTNEXTLINE(misc-use-internal-linkage)
class CFullAsyncImageLoadJob : public IJob
{
public:
	struct SResult
	{
		CImageInfo m_Image;
		bool m_Success = false;
		bool m_Resized = false;
		int m_SourceWidth = 0;
		int m_SourceHeight = 0;
	};

private:
	std::vector<std::string> m_vPossiblePaths; // 可能的文件路径列表，按优先级排序
	IStorage *m_pStorage;
	int m_StorageType; // 存储类型
	int m_MaxTextureSize;
	std::string m_Name;
	mutable CLock m_Lock;
	SResult m_Result;
	mutable bool m_Completed = false;

	// 在后台线程读取文件
	bool TryLoadFile(const char *pFilename, std::vector<uint8_t> &vBuffer)
	{
		IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, m_StorageType);
		if(!File)
			return false;

		io_seek(File, 0, IOSEEK_END);
		const int64_t Size = io_tell(File);
		io_seek(File, 0, IOSEEK_START);

		if(Size <= 0 || Size > LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE)
		{
			io_close(File);
			return false;
		}

		vBuffer.resize(Size);
		const size_t Read = io_read(File, vBuffer.data(), Size);
		io_close(File);

		return Read == (size_t)Size;
	}

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		// 1. 尝试按优先级读取文件
		std::vector<uint8_t> vFileData;
		for(const std::string &Path : m_vPossiblePaths)
		{
			if(TryLoadFile(Path.c_str(), vFileData))
				break;
		}

		if(vFileData.empty())
		{
			const CLockScope Lock(m_Lock);
			m_Completed = true;
			return;
		}

		// 2. 解码图片
		CImageInfo Image;
		bool Success = false;

		constexpr uint8_t PngSignature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
		constexpr uint8_t WebpRiff[] = {0x52, 0x49, 0x46, 0x46};
		constexpr uint8_t WebpWebp[] = {0x57, 0x45, 0x42, 0x50};

		const bool IsPng = vFileData.size() >= 8 &&
				   memcmp(vFileData.data(), PngSignature, 8) == 0;
		const bool IsWebp = vFileData.size() >= 12 &&
				    memcmp(vFileData.data(), WebpRiff, 4) == 0 &&
				    memcmp(vFileData.data() + 8, WebpWebp, 4) == 0;

		if(IsWebp)
		{
			Success = CImageLoader::LoadWebP(vFileData.data(), vFileData.size(), m_Name.c_str(), Image);
		}
		else if(IsPng)
		{
			Success = CImageLoader::LoadPng(vFileData.data(), vFileData.size(), m_Name.c_str(), Image);
		}
		else
		{
			if(CImageLoader::LoadWebP(vFileData.data(), vFileData.size(), m_Name.c_str(), Image))
				Success = true;
			else if(CImageLoader::LoadPng(vFileData.data(), vFileData.size(), m_Name.c_str(), Image))
				Success = true;
		}

		bool Resized = false;
		int SourceWidth = 0;
		int SourceHeight = 0;
		if(Success && Image.m_pData != nullptr && m_MaxTextureSize > 0)
		{
			SourceWidth = Image.m_Width;
			SourceHeight = Image.m_Height;
			const SPreviewTargetSize TargetSize = ComputePreviewTargetSize(Image.m_Width, Image.m_Height, m_MaxTextureSize);
			if(TargetSize.m_Resized)
			{
				ResizeImage(Image, TargetSize.m_Width, TargetSize.m_Height);
				Resized = true;
			}
		}

		{
			const CLockScope Lock(m_Lock);
			m_Result.m_Image = std::move(Image);
			m_Result.m_Success = Success;
			m_Result.m_Resized = Resized;
			m_Result.m_SourceWidth = SourceWidth;
			m_Result.m_SourceHeight = SourceHeight;
			m_Completed = true;
		}
	}

public:
	CFullAsyncImageLoadJob(std::vector<std::string> &&vPossiblePaths, IStorage *pStorage, const char *pName, int StorageType = IStorage::TYPE_ALL, int MaxTextureSize = 0) :
		m_vPossiblePaths(std::move(vPossiblePaths)),
		m_pStorage(pStorage),
		m_StorageType(StorageType),
		m_MaxTextureSize(MaxTextureSize),
		m_Name(pName)
	{
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	SResult GetResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

static std::string JsonEscape(const std::string &Str)
{
	std::string Result;
	Result.reserve(Str.size());
	for(char c : Str)
	{
		switch(c)
		{
		case '"': Result += "\\\""; break;
		case '\\': Result += "\\\\"; break;
		case '\n': Result += "\\n"; break;
		case '\r': Result += "\\r"; break;
		case '\t': Result += "\\t"; break;
		default: Result += c; break;
		}
	}
	return Result;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

// ============================================================================
// ASYNC ASSET LIST LOADING
// ============================================================================
//
// Problem: Synchronous Storage()->ListDirectory() calls block the render thread
// causing UI stutters when switching asset tabs.
//
// Solution: Use background jobs to scan directories and populate asset lists.
// The render thread polls for completion and displays loading indicators.

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CAssetListLoadJob : public IJob
{
public:
	enum EAssetType
	{
		ASSET_TYPE_ENTITIES = 0,
		ASSET_TYPE_GAME,
		ASSET_TYPE_EMOTICONS,
		ASSET_TYPE_PARTICLES,
		ASSET_TYPE_HUD,
		ASSET_TYPE_GUI_CURSOR,
		ASSET_TYPE_ARROW,
		ASSET_TYPE_STRONG_WEAK,
		ASSET_TYPE_ENTITY_BG,
		ASSET_TYPE_EXTRAS,
	};

	using SAssetEntry = CMenus::SSettingsAssetMergeEntry;

	struct SScanContext
	{
		IStorage *m_pStorage;
		std::vector<SAssetEntry> *m_pEntries;
		EAssetResourceKind m_Kind;
		bool m_SkipReservedDefault;
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

private:
	EAssetType m_Type;
	IStorage *m_pStorage;
	mutable CLock m_Lock;
	std::vector<SAssetEntry> m_vEntries;
	mutable bool m_Completed = false;
	int m_Generation = 0;

	static int ScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		const auto *pContext = static_cast<const SScanContext *>(pUser);
		auto *pEntries = pContext->m_pEntries;

		if(IsDir)
		{
			if(pContext->m_Kind != EAssetResourceKind::DIRECTORY)
				return 0;
			if(pName[0] == '.')
				return 0;
			if(pContext->m_SkipReservedDefault && str_comp(pName, "default") == 0)
				return 0;

			SAssetEntry Entry;
			str_copy(Entry.m_aName, pName);
			Entry.m_IsDir = true;
			Entry.m_Source = pContext->m_Source;
			pEntries->push_back(Entry);
		}
		else
		{
			const char *pExt = nullptr;
			if(pContext->m_Kind == EAssetResourceKind::MAP_FILE)
				pExt = ".map";
			else
				pExt = ".png";

			if(str_endswith(pName, pExt))
			{
				char aName[IO_MAX_PATH_LENGTH];
				str_truncate(aName, sizeof(aName), pName, str_length(pName) - str_length(pExt));
				if(pContext->m_SkipReservedDefault && str_comp(aName, "default") == 0)
					return 0;

				SAssetEntry Entry;
				str_copy(Entry.m_aName, aName);
				Entry.m_IsDir = false;
				Entry.m_Source = pContext->m_Source;
				pEntries->push_back(Entry);
			}
		}
		return 0;
	}

	struct SRecursiveMapScanContext
	{
		SScanContext *m_pScanContext;
		char m_aBasePath[IO_MAX_PATH_LENGTH];
		char m_aRelativePath[IO_MAX_PATH_LENGTH];
		char m_aNamePrefix[IO_MAX_PATH_LENGTH];
		EEntityBgHierarchyEntrySource m_Source = EEntityBgHierarchyEntrySource::LOCAL;
	};

	static int RecursiveMapScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		auto *pContext = static_cast<SRecursiveMapScanContext *>(pUser);
		if(!str_comp(pInfo->m_pName, ".") || !str_comp(pInfo->m_pName, ".."))
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pContext->m_aRelativePath[0] != '\0')
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pContext->m_aRelativePath, pInfo->m_pName);
		else
			str_copy(aRelativePath, pInfo->m_pName);

		if(IsDir)
		{
			if(pInfo->m_pName[0] == '.')
				return 0;

			SRecursiveMapScanContext NextContext = *pContext;
			str_copy(NextContext.m_aRelativePath, aRelativePath, sizeof(NextContext.m_aRelativePath));

			char aFullPath[IO_MAX_PATH_LENGTH];
			str_format(aFullPath, sizeof(aFullPath), "%s/%s", pContext->m_aBasePath, aRelativePath);
			pContext->m_pScanContext->m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, aFullPath, RecursiveMapScanCallback, &NextContext);
			return 0;
		}

		const char *pExtension = str_endswith_nocase(pInfo->m_pName, ".map") ? ".map" : FindBackgroundFileExtension(pInfo->m_pName);
		if(pExtension == nullptr)
			return 0;

		char aName[IO_MAX_PATH_LENGTH];
		if(pContext->m_aNamePrefix[0] != '\0')
		{
			if(str_comp_nocase(pExtension, ".map") == 0)
			{
				char aWithoutExt[IO_MAX_PATH_LENGTH];
				str_truncate(aWithoutExt, sizeof(aWithoutExt), aRelativePath, str_length(aRelativePath) - str_length(pExtension));
				str_format(aName, sizeof(aName), "%s/%s", pContext->m_aNamePrefix, aWithoutExt);
			}
			else
			{
				str_format(aName, sizeof(aName), "%s/%s", pContext->m_aNamePrefix, aRelativePath);
			}
		}
		else
		{
			if(str_comp_nocase(pExtension, ".map") == 0)
				str_truncate(aName, sizeof(aName), aRelativePath, str_length(aRelativePath) - str_length(pExtension));
			else
				str_copy(aName, aRelativePath);
		}
		if(pContext->m_pScanContext->m_SkipReservedDefault && str_comp(aName, "default") == 0)
			return 0;

		SAssetEntry Entry;
		str_copy(Entry.m_aName, aName);
		Entry.m_IsDir = false;
		Entry.m_Source = pContext->m_Source;
		pContext->m_pScanContext->m_pEntries->push_back(Entry);
		return 0;
	}

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		std::vector<SAssetEntry> vEntries;
		SScanContext ScanContext{m_pStorage, &vEntries, EAssetResourceKind::DIRECTORY, true, EEntityBgHierarchyEntrySource::LOCAL};

		if(m_Type == ASSET_TYPE_EXTRAS)
		{
			m_pStorage->ListDirectory(IStorage::TYPE_ALL, "assets/extras", ScanCallback, &ScanContext);
		}
		else
		{
			const SAssetResourceCategory *pCategory = nullptr;
			switch(m_Type)
			{
			case ASSET_TYPE_ENTITIES:
				pCategory = FindAssetResourceCategory("entities");
				break;
			case ASSET_TYPE_GAME:
				pCategory = FindAssetResourceCategory("game");
				break;
			case ASSET_TYPE_EMOTICONS:
				pCategory = FindAssetResourceCategory("emoticons");
				break;
			case ASSET_TYPE_PARTICLES:
				pCategory = FindAssetResourceCategory("particles");
				break;
			case ASSET_TYPE_HUD:
				pCategory = FindAssetResourceCategory("hud");
				break;
			case ASSET_TYPE_GUI_CURSOR:
				pCategory = FindAssetResourceCategory("gui_cursor");
				break;
			case ASSET_TYPE_ARROW:
				pCategory = FindAssetResourceCategory("arrow");
				break;
			case ASSET_TYPE_STRONG_WEAK:
				pCategory = FindAssetResourceCategory("strong_weak");
				break;
			case ASSET_TYPE_ENTITY_BG:
				pCategory = FindAssetResourceCategory("entity_bg");
				break;
			case ASSET_TYPE_EXTRAS:
				break;
			}

			dbg_assert(pCategory != nullptr, "asset list category must exist");
			ScanContext.m_Kind = pCategory->m_Kind;
			ScanContext.m_SkipReservedDefault = pCategory->m_Kind != EAssetResourceKind::MAP_FILE;
			if(m_Type == ASSET_TYPE_ENTITY_BG)
			{
				ScanContext.m_Kind = EAssetResourceKind::MAP_FILE;
				ScanContext.m_SkipReservedDefault = false;

				SRecursiveMapScanContext AssetsContext;
				AssetsContext.m_pScanContext = &ScanContext;
				str_copy(AssetsContext.m_aBasePath, pCategory->m_pInstallFolder, sizeof(AssetsContext.m_aBasePath));
				AssetsContext.m_aRelativePath[0] = '\0';
				str_copy(AssetsContext.m_aNamePrefix, "entity_bg", sizeof(AssetsContext.m_aNamePrefix));
				AssetsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &AssetsContext);

				SRecursiveMapScanContext MapsContext;
				MapsContext.m_pScanContext = &ScanContext;
				str_copy(MapsContext.m_aBasePath, "maps", sizeof(MapsContext.m_aBasePath));
				MapsContext.m_aRelativePath[0] = '\0';
				MapsContext.m_aNamePrefix[0] = '\0';
				MapsContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, "maps", RecursiveMapScanCallback, &MapsContext);
			}
			else if(pCategory->m_Kind == EAssetResourceKind::MAP_FILE)
			{
				SRecursiveMapScanContext RecursiveContext;
				RecursiveContext.m_pScanContext = &ScanContext;
				str_copy(RecursiveContext.m_aBasePath, pCategory->m_pInstallFolder, sizeof(RecursiveContext.m_aBasePath));
				RecursiveContext.m_aRelativePath[0] = '\0';
				RecursiveContext.m_aNamePrefix[0] = '\0';
				RecursiveContext.m_Source = EEntityBgHierarchyEntrySource::LOCAL;
				m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, RecursiveMapScanCallback, &RecursiveContext);
			}
			else
			{
				m_pStorage->ListDirectory(IStorage::TYPE_ALL, pCategory->m_pInstallFolder, ScanCallback, &ScanContext);
			}
		}

		if(m_Type == ASSET_TYPE_ENTITY_BG)
		{
			std::unordered_set<std::string> vSeenNames;
			const auto DuplicateIt = std::remove_if(vEntries.begin(), vEntries.end(), [&](const SAssetEntry &Entry) {
				return !vSeenNames.insert(Entry.m_aName).second;
			});
			vEntries.erase(DuplicateIt, vEntries.end());
		}

		// Sort entries by name
		std::sort(vEntries.begin(), vEntries.end(),
			[](const SAssetEntry &Left, const SAssetEntry &Right) {
				return str_comp(Left.m_aName, Right.m_aName) < 0;
			});

		{
			const CLockScope Lock(m_Lock);
			m_vEntries = std::move(vEntries);
			m_Completed = true;
		}
	}

public:
	CAssetListLoadJob(EAssetType Type, IStorage *pStorage) :
		m_Type(Type),
		m_pStorage(pStorage)
	{
	}

	void SetGeneration(int Generation)
	{
		m_Generation = Generation;
	}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	std::vector<SAssetEntry> TakeEntries() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return std::move(m_vEntries);
	}

	EAssetType GetType() const { return m_Type; }
	int Generation() const { return m_Generation; }
};

static bool LoadFileToBuffer(IStorage *pStorage, const char *pFilename, int StorageType, std::vector<uint8_t> &vBuffer)
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType);
	if(!File)
		return false;

	io_seek(File, 0, IOSEEK_END);
	const int64_t Size = io_tell(File);
	io_seek(File, 0, IOSEEK_START);

	if(Size <= 0 || Size > LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE)
	{
		io_close(File);
		return false;
	}

	vBuffer.resize(Size);
	const size_t Read = io_read(File, vBuffer.data(), Size);
	io_close(File);

	return Read == (size_t)Size;
}

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

static void StartEntitiesDecode(CMenus::SCustomEntities *pEntitiesItem, IStorage *pStorage, IEngine *pEngine, int MaxTextureSize)
{
	if(!SettingsAssetPreviewDecodeStartNeeded(
		   pEntitiesItem->m_pDecodeJob != nullptr,
		   pEntitiesItem->m_RenderTexture.IsValid(),
		   pEntitiesItem->m_PreviewResidentBytes,
		   MaxTextureSize,
		   pEntitiesItem->m_PreviewImage.m_pData != nullptr))
		return;

	// 构建可能的文件路径列表（按优先级排序）
	std::vector<std::string> vPossiblePaths;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			vPossiblePaths.emplace_back(aPath);
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			vPossiblePaths.emplace_back(aPath);
		}
		str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
		vPossiblePaths.emplace_back(aPath);
	}

	// 创建全异步 Job，在后台线程完成文件读取和解码
	pEntitiesItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pEntitiesItem->m_aName, IStorage::TYPE_ALL, MaxTextureSize);
	pEngine->AddJob(pEntitiesItem->m_pDecodeJob);
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void StartAssetDecode(TName *pAssetItem, const char *pAssetName, IStorage *pStorage, IEngine *pEngine, int MaxTextureSize)
{
	if(!SettingsAssetPreviewDecodeStartNeeded(
		   pAssetItem->m_pDecodeJob != nullptr,
		   pAssetItem->m_RenderTexture.IsValid(),
		   pAssetItem->m_PreviewResidentBytes,
		   MaxTextureSize,
		   pAssetItem->m_PreviewImage.m_pData != nullptr))
		return;

	// 构建可能的文件路径列表（按优先级排序）
	std::vector<std::string> vPossiblePaths;
	char aPath[IO_MAX_PATH_LENGTH];

	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		if(str_comp(pAssetName, "gui_cursor") == 0 || str_comp(pAssetName, "arrow") == 0 || str_comp(pAssetName, "strong_weak") == 0)
		{
			const auto aCandidates = BuildNamedSingleFileAssetCandidates(pAssetName, pAssetItem->m_aName);
			for(const std::string &Candidate : aCandidates)
			{
				if(!Candidate.empty())
					vPossiblePaths.emplace_back(Candidate);
			}
		}
		else
		{
			str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
			vPossiblePaths.emplace_back(aPath);
		}
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		vPossiblePaths.emplace_back(aPath);
		str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
		vPossiblePaths.emplace_back(aPath);
	}

	// 创建全异步 Job，在后台线程完成文件读取和解码
	pAssetItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pAssetItem->m_aName, IStorage::TYPE_ALL, MaxTextureSize);
	pEngine->AddJob(pAssetItem->m_pDecodeJob);
}

static bool IsEntityBgSelectionMatched(const char *pItemName)
{
	if(str_comp(pItemName, "default") == 0)
		return IsDefaultBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);

	char aSelectedAsset[IO_MAX_PATH_LENGTH];
	if(!TryGetBackgroundEntitiesAssetName(g_Config.m_ClBackgroundEntities, aSelectedAsset, sizeof(aSelectedAsset)))
		return false;
	return str_comp(aSelectedAsset, pItemName) == 0;
}

static void ApplyEntityBgSelection(CGameClient *pGameClient, const char *pItemName)
{
	BuildBackgroundEntitiesValueFromAsset(pItemName, g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
	pGameClient->m_Background.LoadBackground();
}

static bool IsEntityBgManagedAssetName(const char *pAssetName)
{
	return pAssetName != nullptr && str_startswith(pAssetName, "entity_bg/");
}

static void CollectEntityBgPreviewPaths(IStorage *pStorage, const char *pAssetName, std::vector<std::string> &vOutPaths)
{
	vOutPaths.clear();
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return;

	auto AddCandidate = [&vOutPaths](const char *pPath) {
		if(pPath == nullptr || pPath[0] == '\0')
			return;
		if(std::none_of(vOutPaths.begin(), vOutPaths.end(), [pPath](const std::string &Path) { return Path == pPath; }))
			vOutPaths.emplace_back(pPath);
	};

	char aManagedPath[IO_MAX_PATH_LENGTH];
	char aMapPath[IO_MAX_PATH_LENGTH];

	const char *pExplicitExtension = IsBackgroundImageExtension(pAssetName) ? FindBackgroundFileExtension(pAssetName) : nullptr;
	if(IsEntityBgManagedAssetName(pAssetName))
	{
		for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
		{
			if(pExplicitExtension != nullptr && str_comp_nocase(pExplicitExtension, pExtension) != 0)
				continue;
			str_format(aManagedPath, sizeof(aManagedPath), "assets/%s%s", pAssetName, pExplicitExtension != nullptr ? "" : pExtension);
			str_format(aMapPath, sizeof(aMapPath), "maps/%s%s", pAssetName, pExplicitExtension != nullptr ? "" : pExtension);
			const bool ManagedExists = pStorage != nullptr && pStorage->FileExists(aManagedPath, IStorage::TYPE_ALL);
			const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, IStorage::TYPE_ALL);
			if(ManagedExists || !MapExists)
				AddCandidate(aManagedPath);
			if(MapExists || !ManagedExists)
				AddCandidate(aMapPath);
			if(pExplicitExtension != nullptr)
				break;
		}
	}
	else
	{
		for(const char *pExtension : BACKGROUND_IMAGE_EXTENSIONS)
		{
			if(pExplicitExtension != nullptr && str_comp_nocase(pExplicitExtension, pExtension) != 0)
				continue;
			str_format(aMapPath, sizeof(aMapPath), "maps/%s%s", pAssetName, pExplicitExtension != nullptr ? "" : pExtension);
			AddCandidate(aMapPath);
			if(pExplicitExtension != nullptr)
				break;
		}
	}
}

static void ResolveEntityBgLocalMapPath(IStorage *pStorage, const char *pAssetName, int StorageType, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return;

	const char *pExplicitExtension = FindBackgroundFileExtension(pAssetName);
	if(pExplicitExtension != nullptr && str_comp_nocase(pExplicitExtension, ".map") != 0)
	{
		if(IsEntityBgManagedAssetName(pAssetName))
		{
			char aManagedPath[IO_MAX_PATH_LENGTH];
			char aMapPath[IO_MAX_PATH_LENGTH];
			str_format(aManagedPath, sizeof(aManagedPath), "assets/%s", pAssetName);
			str_format(aMapPath, sizeof(aMapPath), "maps/%s", pAssetName);
			const bool ManagedExists = pStorage != nullptr && pStorage->FileExists(aManagedPath, StorageType);
			const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, StorageType);
			if(ManagedExists || !MapExists)
				str_copy(pOut, aManagedPath, OutSize);
			else
				str_copy(pOut, aMapPath, OutSize);
		}
		else
		{
			str_format(pOut, OutSize, "maps/%s", pAssetName);
		}
		return;
	}

	char aManagedPath[IO_MAX_PATH_LENGTH];
	char aMapPath[IO_MAX_PATH_LENGTH];
	str_format(aManagedPath, sizeof(aManagedPath), "assets/%s.map", pAssetName);
	str_format(aMapPath, sizeof(aMapPath), "maps/%s.map", pAssetName);

	if(IsEntityBgManagedAssetName(pAssetName))
	{
		const bool ManagedExists = pStorage != nullptr && pStorage->FileExists(aManagedPath, StorageType);
		const bool MapExists = pStorage != nullptr && pStorage->FileExists(aMapPath, StorageType);
		if(ManagedExists || !MapExists)
			str_copy(pOut, aManagedPath, OutSize);
		else
			str_copy(pOut, aMapPath, OutSize);
	}
	else
	{
		str_copy(pOut, aMapPath, OutSize);
	}
}

static void StartEntityBgDecode(CMenus::SCustomEntityBg *pAssetItem, IStorage *pStorage, IEngine *pEngine, int MaxTextureSize)
{
	if(!SettingsAssetPreviewDecodeStartNeeded(
		   pAssetItem->m_pDecodeJob != nullptr,
		   pAssetItem->m_RenderTexture.IsValid(),
		   pAssetItem->m_PreviewResidentBytes,
		   MaxTextureSize,
		   pAssetItem->m_PreviewImage.m_pData != nullptr))
		return;
	if(IsBackgroundVideoExtension(pAssetItem->m_aName))
		return;

	std::vector<std::string> vPossiblePaths;
	CollectEntityBgPreviewPaths(pStorage, pAssetItem->m_aName, vPossiblePaths);
	if(vPossiblePaths.empty())
		return;

	pAssetItem->m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, pAssetItem->m_aName, IStorage::TYPE_ALL, MaxTextureSize);
	pEngine->AddJob(pAssetItem->m_pDecodeJob);
}

static bool IsEntityBgConfigSelected(const char *pAssetName)
{
	if(pAssetName == nullptr || pAssetName[0] == '\0')
		return false;
	return IsEntityBgSelectionMatched(pAssetName);
}

static bool IsEntityBgVideoAsset(const char *pAssetName)
{
	return IsBackgroundVideoExtension(pAssetName);
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vGameList, pUser);
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, pUser);
}

static std::vector<CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<CMenus::SCustomGuiCursor *> gs_vpSearchGuiCursorList;
static std::vector<CMenus::SCustomArrow *> gs_vpSearchArrowList;
static std::vector<CMenus::SCustomStrongWeak *> gs_vpSearchStrongWeakList;
static std::vector<CMenus::SCustomEntityBg *> gs_vpSearchEntityBgList;
bool gs_SettingsAssetsEntityGamePreview = true;
static std::vector<CMenus::SCustomExtras *> gs_vpSearchExtrasList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true, // ASSETS_TAB_ENTITIES
	true, // ASSETS_TAB_GAME
	true, // ASSETS_TAB_EMOTICONS
	true, // ASSETS_TAB_PARTICLES
	true, // ASSETS_TAB_HUD
	true, // ASSETS_TAB_GUI_CURSOR
	true, // ASSETS_TAB_ARROW
	true, // ASSETS_TAB_STRONG_WEAK
	true, // ASSETS_TAB_ENTITY_BG
	true, // ASSETS_TAB_EXTRAS
};

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};
static int gs_NextAssetWarmupTab = ASSETS_TAB_ENTITIES;
static bool gs_aAssetWarmupReady[NUMBER_OF_ASSETS_TABS] = {};

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

namespace
{
	constexpr const char *WORKSHOP_ASSETS_URL = "https://www.ddrace.cn/data/assets.json";

	const char *AssetResourceCategoryIdByTab(int Tab)
	{
		if(Tab == ASSETS_TAB_ENTITIES)
			return "entities";
		if(Tab == ASSETS_TAB_GAME)
			return "game";
		if(Tab == ASSETS_TAB_EMOTICONS)
			return "emoticons";
		if(Tab == ASSETS_TAB_PARTICLES)
			return "particles";
		if(Tab == ASSETS_TAB_HUD)
			return "hud";
		if(Tab == ASSETS_TAB_GUI_CURSOR)
			return "gui_cursor";
		if(Tab == ASSETS_TAB_ARROW)
			return "arrow";
		if(Tab == ASSETS_TAB_STRONG_WEAK)
			return "strong_weak";
		if(Tab == ASSETS_TAB_ENTITY_BG)
			return "entity_bg";
		if(Tab == ASSETS_TAB_EXTRAS)
			return "extras";
		return nullptr;
	}

	const SAssetResourceCategory *AssetResourceCategoryByTab(int Tab)
	{
		const char *pCategoryId = AssetResourceCategoryIdByTab(Tab);
		if(pCategoryId == nullptr)
			return nullptr;

		const SAssetResourceCategory *pCategory = FindAssetResourceCategory(pCategoryId);
		dbg_assert(pCategory != nullptr, "asset category must exist");
		return pCategory;
	}

	struct SWorkshopHudAsset
	{
		std::string m_Id;
		std::string m_Name;
		std::string m_Author;
		std::string m_LocalName;
		std::string m_ImageUrl;
		std::string m_DownloadUrl;
		std::string m_ThumbUrl;
		std::string m_ThumbCachePath;
		std::string m_InstallPath;
		IGraphics::CTextureHandle m_ThumbTexture;
		std::shared_ptr<CHttpRequest> m_pThumbTask;
		std::shared_ptr<CHttpRequest> m_pDownloadTask;
		bool m_DownloadFailed = false;
		bool m_Installed = false;
		CImageInfo m_ThumbImage;
		size_t m_ThumbBytes = 0;
		bool m_ThumbResized = false;
		bool m_ThumbHighPriority = false;
		bool m_ThumbCacheFailed = false;
		bool m_ThumbRemoteFailed = false;
		bool m_ThumbDecodeFromRemote = false;
		std::shared_ptr<CFullAsyncImageLoadJob> m_pDecodeJob;
		int m_ThumbRequestedTextureSize = 0;
		size_t m_ThumbResidentBytes = 0;
		int m_ThumbQueuedTier = 0;
		unsigned m_ThumbQueuedEpoch = 0;
		int m_ThumbQueuedTab = -1;

		SWorkshopHudAsset() = default;

		SWorkshopHudAsset(const SWorkshopHudAsset &Other) :
			m_Id(Other.m_Id),
			m_Name(Other.m_Name),
			m_Author(Other.m_Author),
			m_LocalName(Other.m_LocalName),
			m_ImageUrl(Other.m_ImageUrl),
			m_DownloadUrl(Other.m_DownloadUrl),
			m_ThumbUrl(Other.m_ThumbUrl),
			m_ThumbCachePath(Other.m_ThumbCachePath),
			m_InstallPath(Other.m_InstallPath),
			m_ThumbTexture(Other.m_ThumbTexture),
			m_pThumbTask(Other.m_pThumbTask),
			m_pDownloadTask(Other.m_pDownloadTask),
			m_DownloadFailed(Other.m_DownloadFailed),
			m_Installed(Other.m_Installed),
			m_ThumbBytes(Other.m_ThumbBytes),
			m_ThumbResized(Other.m_ThumbResized),
			m_ThumbHighPriority(Other.m_ThumbHighPriority),
			m_ThumbCacheFailed(Other.m_ThumbCacheFailed),
			m_ThumbRemoteFailed(Other.m_ThumbRemoteFailed),
			m_ThumbDecodeFromRemote(Other.m_ThumbDecodeFromRemote),
			m_pDecodeJob(Other.m_pDecodeJob),
			m_ThumbRequestedTextureSize(Other.m_ThumbRequestedTextureSize),
			m_ThumbResidentBytes(Other.m_ThumbResidentBytes),
			m_ThumbQueuedTier(Other.m_ThumbQueuedTier),
			m_ThumbQueuedEpoch(Other.m_ThumbQueuedEpoch),
			m_ThumbQueuedTab(Other.m_ThumbQueuedTab)
		{
			if(Other.m_ThumbImage.m_pData != nullptr)
				m_ThumbImage = Other.m_ThumbImage.DeepCopy();
		}

		SWorkshopHudAsset &operator=(const SWorkshopHudAsset &Other)
		{
			if(this == &Other)
				return *this;

			m_Id = Other.m_Id;
			m_Name = Other.m_Name;
			m_Author = Other.m_Author;
			m_LocalName = Other.m_LocalName;
			m_ImageUrl = Other.m_ImageUrl;
			m_DownloadUrl = Other.m_DownloadUrl;
			m_ThumbUrl = Other.m_ThumbUrl;
			m_ThumbCachePath = Other.m_ThumbCachePath;
			m_InstallPath = Other.m_InstallPath;
			m_ThumbTexture = Other.m_ThumbTexture;
			m_pThumbTask = Other.m_pThumbTask;
			m_pDownloadTask = Other.m_pDownloadTask;
			m_DownloadFailed = Other.m_DownloadFailed;
			m_Installed = Other.m_Installed;
			m_ThumbImage.Free();
			if(Other.m_ThumbImage.m_pData != nullptr)
				m_ThumbImage = Other.m_ThumbImage.DeepCopy();
			m_ThumbBytes = Other.m_ThumbBytes;
			m_ThumbResized = Other.m_ThumbResized;
			m_ThumbHighPriority = Other.m_ThumbHighPriority;
			m_ThumbCacheFailed = Other.m_ThumbCacheFailed;
			m_ThumbRemoteFailed = Other.m_ThumbRemoteFailed;
			m_ThumbDecodeFromRemote = Other.m_ThumbDecodeFromRemote;
			m_pDecodeJob = Other.m_pDecodeJob;
			m_ThumbRequestedTextureSize = Other.m_ThumbRequestedTextureSize;
			m_ThumbResidentBytes = Other.m_ThumbResidentBytes;
			m_ThumbQueuedTier = Other.m_ThumbQueuedTier;
			m_ThumbQueuedEpoch = Other.m_ThumbQueuedEpoch;
			m_ThumbQueuedTab = Other.m_ThumbQueuedTab;
			return *this;
		}

		SWorkshopHudAsset(SWorkshopHudAsset &&Other) = default;
		SWorkshopHudAsset &operator=(SWorkshopHudAsset &&Other) = default;

		~SWorkshopHudAsset()
		{
			m_ThumbImage.Free();
		}
	};

	struct SWorkshopHudState
	{
		std::vector<SWorkshopHudAsset> m_vAssets;
		std::unordered_map<std::string, std::string> m_vEntityBgPreviewExtByName;
		std::deque<std::string> m_vDecodeThumbQueue;
		std::unordered_set<std::string> m_vDecodeThumbQueued;
		std::deque<std::string> m_vReadyThumbQueue;
		std::unordered_set<std::string> m_vReadyThumbQueued;
		std::shared_ptr<CHttpRequest> m_pListTask;
		std::shared_ptr<CHttpRequest> m_pEntityBgPreviewTask;
		bool m_Requested = false;
		bool m_EntityBgPreviewRequested = false;
		bool m_LoadFailed = false;
		std::string m_EntityBgPreviewBaseUrl;
		char m_aError[128] = "";
		double m_CacheTime = 0.0;
		double m_LastRefreshTime = 0.0;
	};

	struct SDeleteDirectoryEntry
	{
		char m_aName[IO_MAX_PATH_LENGTH];
		bool m_IsDir = false;
	};

	struct SDeleteDirectoryScanUser
	{
		std::vector<SDeleteDirectoryEntry> *m_pEntries;
	};

	// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
	static SWorkshopHudState gs_aWorkshopStates[NUMBER_OF_ASSETS_TABS];
	struct SPersistedLocalAssetAuthor
	{
		int m_Tab = -1;
		std::string m_LocalName;
		std::string m_Author;
		std::string m_ContentHash;
	};

	struct SPersistedLocalAssetAuthorCache
	{
		std::unordered_map<std::string, SPersistedLocalAssetAuthor> m_vAuthorsByKey;
		std::unordered_map<std::string, std::string> m_vContentHashByKey;
		bool m_Loaded = false;
		bool m_Dirty = false;
	};

	static SPersistedLocalAssetAuthorCache gs_PersistedLocalAssetAuthors;

	static SWorkshopHudState *WorkshopStateByTab(int Tab)
	{
		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(Tab);
		if(pCategory == nullptr || !pCategory->m_WorkshopEnabled)
			return nullptr;
		return &gs_aWorkshopStates[Tab];
	}

} // namespace

namespace
{

	static bool SearchFilterMatches(const char *pFilter, const char *pName, const char *pAuthor = nullptr)
	{
		if(pFilter == nullptr || pFilter[0] == '\0')
			return true;
		if(pName != nullptr && str_utf8_find_nocase(pName, pFilter))
			return true;
		if(pAuthor != nullptr && pAuthor[0] != '\0' && str_utf8_find_nocase(pAuthor, pFilter))
			return true;
		return false;
	}

	static const char *NormalizeWorkshopAuthorName(const char *pAuthor)
	{
		if(pAuthor == nullptr || pAuthor[0] == '\0')
			return "";
		if(str_comp(pAuthor, "资源来源于网络") == 0)
			return Localize("Internet (From online source)");
		return pAuthor;
	}

	static bool WriteSmallTextFile(IStorage *pStorage, const char *pPath, std::string_view Text);
	static void NormalizeNamedSingleFileWorkshopAsset(SWorkshopHudAsset &Asset, IStorage *pStorage, const SAssetResourceCategory &Category);

	static const SWorkshopHudAsset *FindWorkshopAssetByLocalName(const SWorkshopHudState *pState, const char *pLocalName)
	{
		if(pState == nullptr || pLocalName == nullptr || pLocalName[0] == '\0')
			return nullptr;

		for(const SWorkshopHudAsset &Asset : pState->m_vAssets)
		{
			if(str_comp(Asset.m_LocalName.c_str(), pLocalName) == 0)
				return &Asset;
		}
		return nullptr;
	}

	static SWorkshopHudAsset *FindWorkshopAssetByLocalName(SWorkshopHudState *pState, const char *pLocalName)
	{
		if(pState == nullptr || pLocalName == nullptr || pLocalName[0] == '\0')
			return nullptr;

		for(SWorkshopHudAsset &Asset : pState->m_vAssets)
		{
			if(str_comp(Asset.m_LocalName.c_str(), pLocalName) == 0)
				return &Asset;
		}
		return nullptr;
	}

	static int PersistedLocalAssetAuthorTabByCategoryId(const char *pCategoryId)
	{
		for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
		{
			const char *pTabCategoryId = AssetResourceCategoryIdByTab(Tab);
			if(pTabCategoryId != nullptr && str_comp(pTabCategoryId, pCategoryId) == 0)
				return Tab;
		}
		return -1;
	}

	static bool SupportsPersistedLocalAssetAuthor(int Tab)
	{
		const char *pCategoryId = AssetResourceCategoryIdByTab(Tab);
		return pCategoryId != nullptr && SupportsPersistedLocalAssetAuthorCategory(pCategoryId);
	}

	static std::string BuildPersistedLocalAssetAuthorKeyByTab(int Tab, const char *pLocalName)
	{
		const char *pCategoryId = AssetResourceCategoryIdByTab(Tab);
		if(pCategoryId == nullptr)
			return {};
		return BuildPersistedLocalAssetAuthorKey(pCategoryId, pLocalName);
	}

	static bool TryGetLocalAssetSourcePath(IStorage *pStorage, int Tab, const char *pLocalName, char *pPath, int PathSize)
	{
		if(pStorage == nullptr || pPath == nullptr || PathSize <= 0 || pLocalName == nullptr || pLocalName[0] == '\0')
			return false;
		pPath[0] = '\0';

		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(Tab);
		if(pCategory == nullptr)
			return false;
		const SLocalAssetPrimarySourcePath Paths = LocalAssetPrimarySourcePaths(*pCategory, pLocalName);
		if(Paths.m_PrimaryPath.empty())
			return false;
		const bool PrimaryExists = pStorage->FileExists(Paths.m_PrimaryPath.c_str(), IStorage::TYPE_SAVE);
		const bool FallbackExists = !Paths.m_FallbackPath.empty() && pStorage->FileExists(Paths.m_FallbackPath.c_str(), IStorage::TYPE_SAVE);
		const std::string ResolvedPath = ResolveLocalAssetPrimarySourcePath(*pCategory, pLocalName, PrimaryExists, FallbackExists);
		if(ResolvedPath.empty())
			return false;
		str_copy(pPath, ResolvedPath.c_str(), PathSize);
		return pStorage->FileExists(pPath, IStorage::TYPE_SAVE);
	}

	static bool TryGetLocalAssetContentHash(IStorage *pStorage, int Tab, const char *pLocalName, std::string &ContentHash)
	{
		if(!SupportsPersistedLocalAssetAuthor(Tab))
			return false;

		char aPath[IO_MAX_PATH_LENGTH];
		if(!TryGetLocalAssetSourcePath(pStorage, Tab, pLocalName, aPath, sizeof(aPath)))
			return false;

		SHA256_DIGEST Sha256 = {};
		if(!pStorage->CalculateHashes(aPath, IStorage::TYPE_SAVE, &Sha256))
			return false;

		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(Sha256, aSha256, sizeof(aSha256));
		ContentHash = aSha256;
		return true;
	}

	static SPersistedLocalAssetAuthorCache &PersistedLocalAssetAuthorCache(IStorage *pStorage)
	{
		SPersistedLocalAssetAuthorCache &Cache = gs_PersistedLocalAssetAuthors;
		if(Cache.m_Loaded)
			return Cache;

		Cache.m_Loaded = true;
		if(pStorage == nullptr)
			return Cache;

		IOHANDLE File = pStorage->OpenFile(LOCAL_ASSET_AUTHOR_CACHE_FILENAME, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return Cache;

		const long FileSize = io_length(File);
		if(FileSize <= 0 || FileSize >= 1024 * 512)
		{
			io_close(File);
			return Cache;
		}

		std::vector<char> vBuffer(FileSize + 1, '\0');
		io_read(File, vBuffer.data(), FileSize);
		io_close(File);

		json_settings JsonSettings = {};
		char aJsonError[1024];
		json_value *pJson = JsonParseEx(&JsonSettings, vBuffer.data(), FileSize, aJsonError);
		if(!pJson || pJson->type != json_array)
		{
			if(pJson)
				json_value_free(pJson);
			return Cache;
		}

		for(unsigned i = 0; i < pJson->u.array.length; ++i)
		{
			json_value *pItem = pJson->u.array.values[i];
			if(pItem == nullptr || pItem->type != json_object)
				continue;

			const json_value *pName = json_object_get(pItem, "name");
			const json_value *pTab = json_object_get(pItem, "tab");
			const json_value *pAuthor = json_object_get(pItem, "author");
			const json_value *pContentHash = json_object_get(pItem, "content_hash");
			if(pName == &json_value_none || pName->type != json_string ||
				pTab == &json_value_none || pTab->type != json_string ||
				pAuthor == &json_value_none || pAuthor->type != json_string ||
				pContentHash == &json_value_none || pContentHash->type != json_string)
				continue;

			const char *pLocalName = json_string_get(pName);
			const char *pTabName = json_string_get(pTab);
			const char *pAuthorValue = NormalizeWorkshopAuthorName(json_string_get(pAuthor));
			const char *pContentHashValue = json_string_get(pContentHash);
			const char *pCategoryId = pTabName != nullptr ? pTabName : "";
			const int Tab = PersistedLocalAssetAuthorTabByCategoryId(pCategoryId);
			if(pLocalName[0] == '\0' || pAuthorValue[0] == '\0' || pContentHashValue[0] == '\0' || Tab < 0)
				continue;

			SPersistedLocalAssetAuthor Entry;
			Entry.m_Tab = Tab;
			Entry.m_LocalName = pLocalName;
			Entry.m_Author = pAuthorValue;
			Entry.m_ContentHash = pContentHashValue;
			Cache.m_vAuthorsByKey[BuildPersistedLocalAssetAuthorKeyByTab(Entry.m_Tab, pLocalName)] = std::move(Entry);
		}

		json_value_free(pJson);
		return Cache;
	}

	static bool FlushPersistedLocalAssetAuthors(IStorage *pStorage)
	{
		SPersistedLocalAssetAuthorCache &Cache = PersistedLocalAssetAuthorCache(pStorage);
		if(!Cache.m_Dirty)
			return true;

		if(pStorage == nullptr)
			return false;

		std::vector<std::pair<std::string, SPersistedLocalAssetAuthor>> vSortedAuthors(Cache.m_vAuthorsByKey.begin(), Cache.m_vAuthorsByKey.end());
		std::sort(vSortedAuthors.begin(), vSortedAuthors.end(), [](const auto &Left, const auto &Right) {
			return Left.first < Right.first;
		});

		std::string JsonStr = "[";
		bool FirstEntry = true;
		for(size_t i = 0; i < vSortedAuthors.size(); ++i)
		{
			const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(vSortedAuthors[i].second.m_Tab);
			if(pCategory == nullptr)
				continue;
			if(!FirstEntry)
				JsonStr += ",";
			FirstEntry = false;
			JsonStr += "{";
			JsonStr += "\"tab\":\"" + JsonEscape(pCategory->m_pId) + "\",";
			JsonStr += "\"name\":\"" + JsonEscape(vSortedAuthors[i].second.m_LocalName) + "\",";
			JsonStr += "\"author\":\"" + JsonEscape(vSortedAuthors[i].second.m_Author) + "\"";
			JsonStr += ",\"content_hash\":\"" + JsonEscape(vSortedAuthors[i].second.m_ContentHash) + "\"";
			JsonStr += "}";
		}
		JsonStr += "]";
		const bool Saved = WriteSmallTextFile(pStorage, LOCAL_ASSET_AUTHOR_CACHE_FILENAME, JsonStr);
		if(Saved)
			Cache.m_Dirty = false;
		return Saved;
	}

	static const char *FindPersistedLocalAssetAuthor(IStorage *pStorage, int Tab, const char *pLocalName)
	{
		if(pStorage == nullptr || pLocalName == nullptr || pLocalName[0] == '\0')
			return nullptr;
		if(!SupportsPersistedLocalAssetAuthor(Tab))
			return nullptr;

		SPersistedLocalAssetAuthorCache &Cache = PersistedLocalAssetAuthorCache(pStorage);
		const std::string Key = BuildPersistedLocalAssetAuthorKeyByTab(Tab, pLocalName);
		auto AuthorIt = Cache.m_vAuthorsByKey.find(Key);
		if(AuthorIt == Cache.m_vAuthorsByKey.end())
			return nullptr;

		if(auto HashIt = Cache.m_vContentHashByKey.find(Key);
			HashIt != Cache.m_vContentHashByKey.end() &&
			AuthorIt->second.m_ContentHash != HashIt->second)
		{
			Cache.m_vContentHashByKey.erase(Key);
			Cache.m_vAuthorsByKey.erase(AuthorIt);
			Cache.m_Dirty = true;
			return nullptr;
		}

		return AuthorIt->second.m_Author.c_str();
	}

	static bool PersistLocalAssetAuthor(int Tab, const char *pLocalName, const char *pAuthor, IStorage *pStorage, bool FlushImmediately = false)
	{
		if(pStorage == nullptr || pLocalName == nullptr || pLocalName[0] == '\0' || pAuthor == nullptr || pAuthor[0] == '\0')
			return false;
		if(!SupportsPersistedLocalAssetAuthor(Tab))
			return false;

		std::string ContentHash;
		SPersistedLocalAssetAuthorCache &Cache = PersistedLocalAssetAuthorCache(pStorage);
		const std::string Key = BuildPersistedLocalAssetAuthorKeyByTab(Tab, pLocalName);
		if(auto HashIt = Cache.m_vContentHashByKey.find(Key); HashIt != Cache.m_vContentHashByKey.end())
			ContentHash = HashIt->second;
		else if(TryGetLocalAssetContentHash(pStorage, Tab, pLocalName, ContentHash))
			Cache.m_vContentHashByKey[Key] = ContentHash;
		if(ContentHash.empty())
			return false;

		SPersistedLocalAssetAuthor &Entry = Cache.m_vAuthorsByKey[Key];
		const std::string NormalizedAuthor = NormalizeWorkshopAuthorName(pAuthor);
		if(Entry.m_Tab == Tab && Entry.m_LocalName == pLocalName && Entry.m_Author == NormalizedAuthor && Entry.m_ContentHash == ContentHash)
			return true;

		Entry.m_Tab = Tab;
		Entry.m_LocalName = pLocalName;
		Entry.m_Author = NormalizedAuthor;
		Entry.m_ContentHash = std::move(ContentHash);
		Cache.m_Dirty = true;
		return !FlushImmediately || FlushPersistedLocalAssetAuthors(pStorage);
	}

	static bool RemovePersistedLocalAssetAuthor(int Tab, const char *pLocalName, IStorage *pStorage)
	{
		if(pStorage == nullptr || pLocalName == nullptr || pLocalName[0] == '\0')
			return false;

		SPersistedLocalAssetAuthorCache &Cache = PersistedLocalAssetAuthorCache(pStorage);
		const std::string Key = BuildPersistedLocalAssetAuthorKeyByTab(Tab, pLocalName);
		Cache.m_vContentHashByKey.erase(Key);
		const size_t Erased = Cache.m_vAuthorsByKey.erase(Key);
		if(Erased == 0)
			return false;
		Cache.m_Dirty = true;
		return FlushPersistedLocalAssetAuthors(pStorage);
	}

	static bool PersistLocalAssetAuthorForWorkshopAsset(int Tab, const SWorkshopHudAsset &Asset, IStorage *pStorage)
	{
		if(!Asset.m_Installed || Asset.m_LocalName.empty() || Asset.m_Author.empty())
			return false;
		return PersistLocalAssetAuthor(Tab, Asset.m_LocalName.c_str(), Asset.m_Author.c_str(), pStorage);
	}

	static void FlushPersistedLocalAssetAuthorsIfDirty(IStorage *pStorage, int Tab)
	{
		(void)Tab;
		(void)FlushPersistedLocalAssetAuthors(pStorage);
	}

	static void NormalizeEntityBgWorkshopAsset(SWorkshopHudAsset &Asset, IStorage *pStorage)
	{
		if(Asset.m_InstallPath.empty())
			return;

		const std::string NormalizedInstallPath = NormalizeEntityBgWorkshopInstallPath(Asset.m_InstallPath);
		if(!NormalizedInstallPath.empty() && NormalizedInstallPath != Asset.m_InstallPath)
		{
			if(pStorage != nullptr &&
				pStorage->FileExists(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE) &&
				!pStorage->FileExists(NormalizedInstallPath.c_str(), IStorage::TYPE_SAVE))
			{
				pStorage->RenameFile(Asset.m_InstallPath.c_str(), NormalizedInstallPath.c_str(), IStorage::TYPE_SAVE);
			}
			Asset.m_InstallPath = NormalizedInstallPath;
		}

		const std::string LocalName = RebuildEntityBgWorkshopLocalName(Asset.m_InstallPath);
		if(!LocalName.empty())
			Asset.m_LocalName = LocalName;
	}

	static bool EntityBgWorkshopInstallLooksCorrupt(IStorage *pStorage, const SWorkshopHudAsset &Asset)
	{
		if(pStorage == nullptr || Asset.m_InstallPath.empty() || !str_endswith_nocase(Asset.m_InstallPath.c_str(), ".map"))
			return false;

		IOHANDLE File = pStorage->OpenFile(Asset.m_InstallPath.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return false;

		unsigned char aHeader[16] = {};
		const unsigned BytesRead = io_read(File, aHeader, sizeof(aHeader));
		io_close(File);
		return DetectCorruptEntityBgInstallHeader(aHeader, BytesRead);
	}

	static bool WorkshopAssetCanDecodePreviewFromInstall(const SWorkshopHudAsset &Asset, int Tab)
	{
		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(Tab);
		return WorkshopAssetCanDecodeInstalledPreview(
			pCategory != nullptr ? pCategory->m_pId : nullptr,
			Asset.m_Installed,
			Asset.m_ThumbCacheFailed);
	}

	static const char *WorkshopThumbSourceUrl(const SWorkshopHudAsset &Asset, int Tab)
	{
		const bool RequiresEntityBgPreviewUrl = Tab == ASSETS_TAB_ENTITY_BG;
		const char *pThumbSourceUrl = Asset.m_ThumbUrl.c_str();
		if(!RequiresEntityBgPreviewUrl && pThumbSourceUrl[0] == '\0')
			pThumbSourceUrl = Asset.m_ImageUrl.c_str();
		return pThumbSourceUrl;
	}

	static bool StartWorkshopRemoteThumbRequest(SWorkshopHudAsset &Asset, int Tab, int PreviewEpoch, int TargetTextureSize, int &ThumbStartsThisFrame, IStorage *pStorage, IHttp *pHttp)
	{
		const char *pThumbSourceUrl = WorkshopThumbSourceUrl(Asset, Tab);
		if(pThumbSourceUrl[0] == '\0' || Asset.m_ThumbRemoteFailed || pHttp == nullptr || pStorage == nullptr)
			return false;

		pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		pStorage->CreateFolder("qmclient/workshop", IStorage::TYPE_SAVE);
		pStorage->CreateFolder("qmclient/workshop/thumbs", IStorage::TYPE_SAVE);
		char aWebpUrl[IO_MAX_PATH_LENGTH];
		str_format(aWebpUrl, sizeof(aWebpUrl), "%s!/format/webp", pThumbSourceUrl);
		auto pThumbTask = HttpGetFile(aWebpUrl, pStorage, Asset.m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
		pThumbTask->Timeout(CTimeout{8000, 20000, 100, 10});
		pThumbTask->LogProgress(HTTPLOG::FAILURE);
		pThumbTask->FailOnErrorStatus(false);
		pThumbTask->SkipByFileTime(false);
		Asset.m_ThumbQueuedTier = TargetTextureSize;
		Asset.m_ThumbQueuedEpoch = PreviewEpoch;
		Asset.m_ThumbQueuedTab = Tab;
		Asset.m_ThumbRequestedTextureSize = TargetTextureSize;
		Asset.m_pThumbTask = std::move(pThumbTask);
		pHttp->Run(Asset.m_pThumbTask);
		++ThumbStartsThisFrame;
		return true;
	}

	static void RepairWorkshopAssetInstallState(const SAssetResourceCategory &Category, SWorkshopHudAsset &Asset, IStorage *pStorage)
	{
		if(str_comp(Category.m_pId, "entity_bg") == 0)
		{
			NormalizeEntityBgWorkshopAsset(Asset, pStorage);
			if(EntityBgWorkshopInstallLooksCorrupt(pStorage, Asset))
				pStorage->RemoveFile(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
		}
		else if(Category.m_Kind == EAssetResourceKind::NAMED_SINGLE_FILE)
		{
			NormalizeNamedSingleFileWorkshopAsset(Asset, pStorage, Category);
		}

		Asset.m_Installed = pStorage != nullptr && pStorage->FileExists(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
	}

	static bool IsEntityBgWorkshopFolderOrChild(const char *pPath)
	{
		return pPath != nullptr && (IsEntityBgWorkshopFolderPath(pPath) || str_startswith(pPath, "entity_bg/"));
	}

	static bool UsesCombinedAssetList(const SAssetResourceCategory *pCategory)
	{
		return pCategory != nullptr && pCategory->m_WorkshopEnabled;
	}

	static void StartBackgroundDecode(SWorkshopHudAsset &Asset, IStorage *pStorage, IEngine *pEngine, int MaxTextureSize)
	{
		if(!SettingsAssetPreviewDecodeStartNeeded(
			   Asset.m_pDecodeJob != nullptr,
			   Asset.m_ThumbTexture.IsValid(),
			   Asset.m_ThumbResidentBytes,
			   MaxTextureSize,
			   Asset.m_ThumbImage.m_pData != nullptr))
			return;

		// 构建可能的文件路径列表（按优先级排序）
		std::vector<std::string> vPossiblePaths;

		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		const char *pCategoryId = pCategory != nullptr ? pCategory->m_pId : nullptr;
		const SWorkshopPreviewDecodeSourcePlan SourcePlan = BuildWorkshopPreviewDecodeSourcePlan(pCategoryId, Asset.m_Installed, !Asset.m_ThumbCachePath.empty());
		if(SourcePlan.m_UseInstallSource && !Asset.m_InstallPath.empty())
		{
			vPossiblePaths.emplace_back(Asset.m_InstallPath);
		}
		if(SourcePlan.m_UseThumbCache)
		{
			vPossiblePaths.emplace_back(Asset.m_ThumbCachePath);
		}

		if(!vPossiblePaths.empty())
		{
			// 创建全异步 Job，在后台线程完成文件读取和解码
			Asset.m_ThumbRequestedTextureSize = MaxTextureSize;
			Asset.m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, Asset.m_Name.c_str(), IStorage::TYPE_SAVE, MaxTextureSize);
			pEngine->AddJob(Asset.m_pDecodeJob);
		}
	}

	static void ResetWorkshopThumbReadyState(SWorkshopHudAsset &Asset)
	{
		Asset.m_ThumbImage.Free();
		Asset.m_ThumbBytes = 0;
		Asset.m_ThumbResized = false;
		Asset.m_ThumbHighPriority = false;
		Asset.m_ThumbCacheFailed = false;
		Asset.m_ThumbRemoteFailed = false;
		Asset.m_ThumbDecodeFromRemote = false;
		Asset.m_ThumbQueuedTier = 0;
		Asset.m_ThumbQueuedEpoch = 0;
		Asset.m_ThumbQueuedTab = -1;
	}

	static SWorkshopHudAsset *FindWorkshopAssetById(SWorkshopHudState &State, const std::string &Id)
	{
		for(auto &Asset : State.m_vAssets)
		{
			if(Asset.m_Id == Id)
				return &Asset;
		}
		return nullptr;
	}

	static void ClearWorkshopDecodeThumbQueue(SWorkshopHudState &State)
	{
		State.m_vDecodeThumbQueue.clear();
		State.m_vDecodeThumbQueued.clear();
	}

	static void QueueWorkshopDecodeThumb(SWorkshopHudState &State, SWorkshopHudAsset &Asset, int CurTab, const IClient *pClient)
	{
		if(State.m_vDecodeThumbQueued.insert(Asset.m_Id).second)
		{
			if(Asset.m_ThumbHighPriority)
				State.m_vDecodeThumbQueue.push_front(Asset.m_Id);
			else
				State.m_vDecodeThumbQueue.push_back(Asset.m_Id);
			char aExtra[160];
			str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s queue_size=%d",
				CurTab, Asset.m_Name.c_str(), (int)State.m_vDecodeThumbQueue.size());
			LogAssetsPerfStage(pClient, "assets_workshop_thumb_decode_queue_push", 0.0, true, aExtra);
		}
		else if(Asset.m_ThumbHighPriority)
		{
			auto It = std::find(State.m_vDecodeThumbQueue.begin(), State.m_vDecodeThumbQueue.end(), Asset.m_Id);
			if(It != State.m_vDecodeThumbQueue.end())
			{
				State.m_vDecodeThumbQueue.erase(It);
				State.m_vDecodeThumbQueue.push_front(Asset.m_Id);
			}
		}
	}

	static void PruneWorkshopDecodeThumbQueue(SWorkshopHudState &State)
	{
		std::deque<std::string> vQueue;
		std::unordered_set<std::string> vQueued;
		for(const std::string &Id : State.m_vDecodeThumbQueue)
		{
			SWorkshopHudAsset *pAsset = FindWorkshopAssetById(State, Id);
			if(pAsset == nullptr || !pAsset->m_pDecodeJob)
				continue;
			if(vQueued.insert(Id).second)
				vQueue.push_back(Id);
		}
		State.m_vDecodeThumbQueue = std::move(vQueue);
		State.m_vDecodeThumbQueued = std::move(vQueued);
	}

	static void ClearWorkshopReadyThumbQueue(SWorkshopHudState &State)
	{
		State.m_vReadyThumbQueue.clear();
		State.m_vReadyThumbQueued.clear();
	}

	static void PruneWorkshopReadyThumbQueue(SWorkshopHudState &State)
	{
		std::deque<std::string> vQueue;
		std::unordered_set<std::string> vQueued;
		for(const std::string &Id : State.m_vReadyThumbQueue)
		{
			SWorkshopHudAsset *pAsset = FindWorkshopAssetById(State, Id);
			if(pAsset == nullptr || pAsset->m_ThumbTexture.IsValid() || pAsset->m_ThumbImage.m_pData == nullptr)
				continue;
			if(vQueued.insert(Id).second)
				vQueue.push_back(Id);
		}
		State.m_vReadyThumbQueue = std::move(vQueue);
		State.m_vReadyThumbQueued = std::move(vQueued);
	}

	static void ResetWorkshopAssetRuntimeState(SWorkshopHudAsset &Asset, IGraphics *pGraphics, bool AbortTasks)
	{
		if(AbortTasks && Asset.m_pThumbTask)
			Asset.m_pThumbTask->Abort();
		if(AbortTasks && Asset.m_pDownloadTask)
			Asset.m_pDownloadTask->Abort();
		Asset.m_pThumbTask.reset();
		Asset.m_pDownloadTask.reset();
		Asset.m_pDecodeJob.reset();
		ResetWorkshopThumbReadyState(Asset);
		Asset.m_ThumbRequestedTextureSize = 0;
		Asset.m_ThumbResidentBytes = 0;
		pGraphics->UnloadTexture(&Asset.m_ThumbTexture);
	}

	static void QueueWorkshopReadyThumb(SWorkshopHudState &State, SWorkshopHudAsset &Asset, int CurTab, const IClient *pClient)
	{
		if(State.m_vReadyThumbQueued.insert(Asset.m_Id).second)
		{
			if(Asset.m_ThumbHighPriority)
				State.m_vReadyThumbQueue.push_front(Asset.m_Id);
			else
				State.m_vReadyThumbQueue.push_back(Asset.m_Id);
			char aExtra[160];
			str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s queue_size=%d bytes=%u",
				CurTab, Asset.m_Name.c_str(), (int)State.m_vReadyThumbQueue.size(), (unsigned)Asset.m_ThumbBytes);
			LogAssetsPerfStage(pClient, "assets_workshop_thumb_upload_queue_push", 0.0, true, aExtra);
		}
		else if(Asset.m_ThumbHighPriority)
		{
			auto It = std::find(State.m_vReadyThumbQueue.begin(), State.m_vReadyThumbQueue.end(), Asset.m_Id);
			if(It != State.m_vReadyThumbQueue.end())
			{
				State.m_vReadyThumbQueue.erase(It);
				State.m_vReadyThumbQueue.push_front(Asset.m_Id);
			}
		}
	}

	static bool EnsureInstalledWorkshopEntityBgThumbReady(SWorkshopHudState &State, const char *pLocalName, bool WindowActive, bool HighPriority, int CurTab, int PreviewEpoch, size_t PreviewBudgetBytes, size_t TextureMemoryUsageBytes, int &ThumbStartsThisFrame, int MaxThumbStartsPerFrame, int MaxHighPriorityThumbStartsPerFrame, int &VisibleStartsThisFrame, int MaxVisibleStartsPerFrame, IStorage *pStorage, IEngine *pEngine, IHttp *pHttp, const IClient *pClient)
	{
		SWorkshopHudAsset *pAsset = FindWorkshopAssetByLocalName(&State, pLocalName);
		if(pAsset == nullptr || !pAsset->m_Installed)
			return false;

		const int TargetTextureSize = SettingsAssetPreviewBudgetedTextureSize(
			WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
			ASSET_PREVIEW_MIN_TEXTURE_SIZE,
			PreviewBudgetBytes,
			TextureMemoryUsageBytes,
			pAsset->m_ThumbResidentBytes);
		if(SettingsAssetPreviewResidentTextureSatisfiesRequest(
			   pAsset->m_ThumbTexture.IsValid(),
			   pAsset->m_ThumbResidentBytes,
			   TargetTextureSize))
			return true;

		if(HighPriority)
			pAsset->m_ThumbHighPriority = true;
		if(pAsset->m_ThumbImage.m_pData != nullptr)
		{
			pAsset->m_ThumbQueuedTier = TargetTextureSize;
			pAsset->m_ThumbQueuedEpoch = PreviewEpoch;
			pAsset->m_ThumbQueuedTab = CurTab;
			/*
			QueueWorkshopReadyThumb(State, *pAsset, CurTab);
			return true;
			*/
			QueueWorkshopReadyThumb(State, *pAsset, CurTab, pClient);
			return true;
		}
		if(pAsset->m_pDecodeJob)
		{
			pAsset->m_ThumbQueuedTier = TargetTextureSize;
			pAsset->m_ThumbQueuedEpoch = PreviewEpoch;
			pAsset->m_ThumbQueuedTab = CurTab;
			/*
			QueueWorkshopDecodeThumb(State, *pAsset, CurTab);
			return true;
			*/
			QueueWorkshopDecodeThumb(State, *pAsset, CurTab, pClient);
			return true;
		}
		if(pAsset->m_pThumbTask)
			return true;
		if(HighPriority && VisibleStartsThisFrame >= MaxVisibleStartsPerFrame)
			return false;
		if(!SettingsResourceCanUseHighPriorityBudget(ThumbStartsThisFrame, MaxThumbStartsPerFrame, MaxHighPriorityThumbStartsPerFrame, HighPriority))
			return false;
		if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, HighPriority))
			return false;

		const bool HasUsableInstalledThumb = WorkshopAssetCanDecodePreviewFromInstall(*pAsset, CurTab);
		const bool HasUsableThumbCache = !pAsset->m_ThumbCacheFailed && !pAsset->m_ThumbCachePath.empty() && pStorage->FileExists(pAsset->m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
		if(!HasUsableInstalledThumb && !HasUsableThumbCache)
			return StartWorkshopRemoteThumbRequest(*pAsset, CurTab, PreviewEpoch, TargetTextureSize, ThumbStartsThisFrame, pStorage, pHttp);

		pAsset->m_ThumbDecodeFromRemote = false;
		pAsset->m_ThumbQueuedTier = TargetTextureSize;
		pAsset->m_ThumbQueuedEpoch = PreviewEpoch;
		pAsset->m_ThumbQueuedTab = CurTab;
		StartBackgroundDecode(*pAsset, pStorage, pEngine, TargetTextureSize);
		if(!pAsset->m_pDecodeJob)
			return false;
		/*
		++ThumbStartsThisFrame;
		QueueWorkshopDecodeThumb(State, *pAsset, CurTab);
		return true;
		*/
		++ThumbStartsThisFrame;
		if(HighPriority)
			++VisibleStartsThisFrame;
		QueueWorkshopDecodeThumb(State, *pAsset, CurTab, pClient);
		return true;
	}

	int CollectDeleteDirectoryEntries(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		if(pName[0] == '.')
			return 0;

		auto *pScanUser = static_cast<SDeleteDirectoryScanUser *>(pUser);
		SDeleteDirectoryEntry Entry;
		str_copy(Entry.m_aName, pName);
		Entry.m_IsDir = IsDir != 0;
		pScanUser->m_pEntries->push_back(Entry);
		return 0;
	}

	bool RemoveFolderTree(IStorage *pStorage, const char *pFolderPath)
	{
		std::vector<SDeleteDirectoryEntry> vEntries;
		SDeleteDirectoryScanUser User;
		User.m_pEntries = &vEntries;
		pStorage->ListDirectory(IStorage::TYPE_SAVE, pFolderPath, CollectDeleteDirectoryEntries, &User);

		bool RemovedAnything = false;
		for(const SDeleteDirectoryEntry &Entry : vEntries)
		{
			char aChildPath[IO_MAX_PATH_LENGTH];
			str_format(aChildPath, sizeof(aChildPath), "%s/%s", pFolderPath, Entry.m_aName);
			if(Entry.m_IsDir)
			{
				RemovedAnything |= RemoveFolderTree(pStorage, aChildPath);
			}
			else
			{
				RemovedAnything |= pStorage->RemoveFile(aChildPath, IStorage::TYPE_SAVE);
			}
		}

		RemovedAnything |= pStorage->RemoveFolder(pFolderPath, IStorage::TYPE_SAVE);
		return RemovedAnything;
	}

	void GuessUrlExtension(const char *pUrl, char *pOutExt, int OutExtSize)
	{
		str_copy(pOutExt, "png", OutExtSize);
		if(!pUrl || pUrl[0] == '\0')
			return;

		char aUrl[IO_MAX_PATH_LENGTH];
		str_copy(aUrl, pUrl, sizeof(aUrl));
		if(const char *pQuery = str_find(aUrl, "?"))
			aUrl[pQuery - aUrl] = '\0';

		const char *pSlash = str_rchr(aUrl, '/');
		const char *pDot = str_rchr(aUrl, '.');
		if(!pDot || (pSlash && pDot < pSlash))
			return;

		char aExt[16];
		str_copy(aExt, pDot + 1, sizeof(aExt));
		if(aExt[0] == '\0' || str_length(aExt) > 8)
			return;

		for(char &c : aExt)
		{
			if(c >= 'A' && c <= 'Z')
				c = c - 'A' + 'a';
			if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
				return;
		}
		str_copy(pOutExt, aExt, OutExtSize);
	}

	void SanitizeFilenameInPlace(char *pFilename)
	{
		str_sanitize_filename(pFilename);
		for(char *pChar = pFilename; *pChar != '\0'; ++pChar)
		{
			if(*pChar == ' ')
				*pChar = '_';
		}
	}

	std::string BuildSafeFilename(const char *pName, const char *pFallbackName, const char *pExt)
	{
		char aName[128];
		if(pName && pName[0] != '\0')
			str_copy(aName, pName, sizeof(aName));
		else
			str_copy(aName, pFallbackName, sizeof(aName));
		SanitizeFilenameInPlace(aName);
		if(aName[0] == '\0')
			str_copy(aName, pFallbackName, sizeof(aName));

		char aDotExt[20];
		str_format(aDotExt, sizeof(aDotExt), ".%s", pExt);

		char aFilename[160];
		if(str_endswith_nocase(aName, aDotExt))
			str_copy(aFilename, aName, sizeof(aFilename));
		else
			str_format(aFilename, sizeof(aFilename), "%s%s", aName, aDotExt);
		return aFilename;
	}

	std::string UniqueWorkshopAssetBaseName(const SAssetResourceCategory &Category, std::string_view PreferredBaseName, const char *pAssetId, const std::unordered_set<std::string> &vUsedLocalNames)
	{
		std::string BaseName(PreferredBaseName);
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
			return BaseName;

		const bool ReservedName = IsReservedNamedSingleFileAssetName(Category, BaseName);
		const bool DuplicateName = vUsedLocalNames.contains(BaseName);
		if(!ReservedName && !DuplicateName)
			return BaseName;

		char aSafeId[80];
		str_copy(aSafeId, pAssetId != nullptr && pAssetId[0] != '\0' ? pAssetId : "workshop", sizeof(aSafeId));
		SanitizeFilenameInPlace(aSafeId);
		if(aSafeId[0] == '\0')
			str_copy(aSafeId, "workshop", sizeof(aSafeId));

		std::string Candidate = BaseName + "_" + aSafeId;
		int Suffix = 2;
		while(IsReservedNamedSingleFileAssetName(Category, Candidate) || vUsedLocalNames.contains(Candidate))
		{
			Candidate = BaseName + "_" + aSafeId + "_" + std::to_string(Suffix);
			++Suffix;
		}

		return Candidate;
	}

	template<typename TName>
	void EnsureDefaultAssetVisible(const SAssetResourceCategory &Category, std::vector<TName> &vAssetList)
	{
		(void)Category;

		const auto HasDefault = std::any_of(vAssetList.begin(), vAssetList.end(), [](const TName &AssetItem) {
			return IsProtectedDefaultAsset(AssetItem.m_aName);
		});
		if(!HasDefault)
		{
			TName DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			vAssetList.push_back(DefaultItem);
		}

		std::sort(vAssetList.begin(), vAssetList.end(), [](const TName &LeftItem, const TName &RightItem) {
			return AssetResourceNameLess(LeftItem.m_aName, RightItem.m_aName);
		});
	}

	static const char *AssetCardDisplayName(const CMenus::SCustomItem *pItem)
	{
		if(pItem == nullptr)
			return "";
		const char *pDisplayName = pItem->m_aDisplayName[0] != '\0' ? pItem->m_aDisplayName : pItem->m_aName;
		if(str_comp(pDisplayName, "entity_bg (Workshop)") == 0)
			return Localize("entity_bg (Workshop)");
		return pDisplayName;
	}

	static bool IsCustomAssetSelectedByTab(int Tab, const char *pName)
	{
		if(pName == nullptr || pName[0] == '\0')
			return false;
		if(Tab == ASSETS_TAB_ENTITIES)
			return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
		if(Tab == ASSETS_TAB_GAME)
			return str_comp(pName, g_Config.m_ClAssetGame) == 0;
		if(Tab == ASSETS_TAB_EMOTICONS)
			return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
		if(Tab == ASSETS_TAB_PARTICLES)
			return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
		if(Tab == ASSETS_TAB_HUD)
			return str_comp(pName, g_Config.m_ClAssetHud) == 0;
		if(Tab == ASSETS_TAB_GUI_CURSOR)
			return str_comp(pName, g_Config.m_ClAssetGuiCursor) == 0;
		if(Tab == ASSETS_TAB_ARROW)
			return str_comp(pName, g_Config.m_ClAssetArrow) == 0;
		if(Tab == ASSETS_TAB_STRONG_WEAK)
			return str_comp(pName, g_Config.m_ClAssetStrongWeak) == 0;
		if(Tab == ASSETS_TAB_ENTITY_BG)
			return IsEntityBgConfigSelected(pName);
		if(Tab == ASSETS_TAB_EXTRAS)
			return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
		return false;
	}

	struct SNamedSingleFileNameScanUser
	{
		std::vector<std::string> *m_pNames;
	};

	int CollectNamedSingleFileAssetNamesCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		if(IsDir || pName[0] == '.')
			return 0;
		if(!str_endswith(pName, ".png"))
			return 0;

		auto *pScanUser = static_cast<SNamedSingleFileNameScanUser *>(pUser);
		char aName[IO_MAX_PATH_LENGTH];
		str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
		pScanUser->m_pNames->emplace_back(aName);
		return 0;
	}

	bool WriteSmallTextFile(IStorage *pStorage, const char *pPath, std::string_view Text)
	{
		IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;

		const size_t Written = io_write(File, Text.data(), Text.size());
		io_close(File);
		return Written == Text.size();
	}

	bool CopyStorageFile(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, int TargetStorageType)
	{
		std::vector<uint8_t> vBuffer;
		if(!LoadFileToBuffer(pStorage, pSourcePath, SourceStorageType, vBuffer))
			return false;

		IOHANDLE File = pStorage->OpenFile(pTargetPath, IOFLAG_WRITE, TargetStorageType);
		if(!File)
			return false;

		const size_t Written = io_write(File, vBuffer.data(), vBuffer.size());
		io_close(File);
		return Written == vBuffer.size();
	}

	void CollectNamedSingleFileAssetNames(IStorage *pStorage, const SAssetResourceCategory &Category, std::vector<std::string> &vAssetNames)
	{
		vAssetNames.clear();
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
			return;

		SNamedSingleFileNameScanUser ScanUser{&vAssetNames};
		pStorage->ListDirectory(IStorage::TYPE_ALL, Category.m_pInstallFolder, CollectNamedSingleFileAssetNamesCallback, &ScanUser);
		::EnsureDefaultAssetVisible(vAssetNames);
	}

	std::string LocalNameFromNamedSingleFileInstallPath(const SAssetResourceCategory &Category, std::string_view InstallPath)
	{
		if(InstallPath.empty())
			return {};

		const std::string Prefix = std::string(Category.m_pInstallFolder) + "/";
		if(InstallPath.size() >= Prefix.size() && InstallPath.substr(0, Prefix.size()) == Prefix)
			InstallPath.remove_prefix(Prefix.size());
		if(const size_t SlashPos = InstallPath.find_last_of('/'); SlashPos != std::string_view::npos)
			InstallPath = InstallPath.substr(SlashPos + 1);
		if(const size_t DotPos = InstallPath.find_last_of('.'); DotPos != std::string_view::npos)
			InstallPath = InstallPath.substr(0, DotPos);
		return std::string(InstallPath);
	}

	void BuildNamedSingleFileWorkshopInstallInfo(const SAssetResourceCategory &Category, const SWorkshopHudAsset &Asset, std::string &OutLocalName, std::string &OutInstallPath)
	{
		OutLocalName.clear();
		OutInstallPath.clear();
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE || Asset.m_ImageUrl.empty())
			return;

		std::unordered_set<std::string> vUsedLocalNames;
		char aExt[16];
		GuessUrlExtension(Asset.m_ImageUrl.c_str(), aExt, sizeof(aExt));
		std::string SafeInstallName = BuildSafeFilename(Asset.m_Name.c_str(), Asset.m_Id.c_str(), aExt);
		const size_t DotPos = SafeInstallName.find_last_of('.');
		const std::string PreferredLocalBaseName = DotPos == std::string::npos ? SafeInstallName : SafeInstallName.substr(0, DotPos);
		const std::string LocalBaseName = UniqueWorkshopAssetBaseName(Category, PreferredLocalBaseName, Asset.m_Id.c_str(), vUsedLocalNames);
		if(LocalBaseName != PreferredLocalBaseName)
			SafeInstallName = LocalBaseName + "." + aExt;

		OutLocalName = LocalBaseName;
		OutInstallPath = std::string(Category.m_pInstallFolder) + "/" + SafeInstallName;
	}

	std::string FindLegacyMigratedNamedSingleFileInstallPath(IStorage *pStorage, const SAssetResourceCategory &Category, std::string_view LegacyBaseName)
	{
		if(pStorage == nullptr || LegacyBaseName.empty())
			return {};

		std::vector<std::string> vSaveNames;
		SNamedSingleFileNameScanUser ScanUser{&vSaveNames};
		pStorage->ListDirectory(IStorage::TYPE_SAVE, Category.m_pInstallFolder, CollectNamedSingleFileAssetNamesCallback, &ScanUser);

		const std::string Prefix = std::string(LegacyBaseName) + "_local";
		std::string BestName;
		int BestPriority = 0x7fffffff;
		for(const std::string &Name : vSaveNames)
		{
			if(Name == Prefix)
			{
				BestName = Name;
				break;
			}

			if(Name.size() <= Prefix.size() + 1 || Name.compare(0, Prefix.size() + 1, Prefix + "_") != 0)
				continue;

			const char *pSuffix = Name.c_str() + Prefix.size() + 1;
			int SuffixValue = 0;
			if(!str_toint(pSuffix, &SuffixValue) || SuffixValue < 2)
				continue;
			if(SuffixValue < BestPriority)
			{
				BestPriority = SuffixValue;
				BestName = Name;
			}
		}

		if(BestName.empty())
			return {};
		return std::string(Category.m_pInstallFolder) + "/" + BestName + ".png";
	}

	void NormalizeNamedSingleFileWorkshopAsset(SWorkshopHudAsset &Asset, IStorage *pStorage, const SAssetResourceCategory &Category)
	{
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
			return;

		const std::string CachedInstallPath = Asset.m_InstallPath;
		const std::string CachedLocalName = !Asset.m_LocalName.empty() ? Asset.m_LocalName : LocalNameFromNamedSingleFileInstallPath(Category, CachedInstallPath);

		std::string CanonicalLocalName;
		std::string CanonicalInstallPath;
		BuildNamedSingleFileWorkshopInstallInfo(Category, Asset, CanonicalLocalName, CanonicalInstallPath);
		if(CanonicalInstallPath.empty())
			return;

		if(pStorage != nullptr && pStorage->FileExists(CanonicalInstallPath.c_str(), IStorage::TYPE_SAVE))
		{
			Asset.m_LocalName = CanonicalLocalName;
			Asset.m_InstallPath = CanonicalInstallPath;
			return;
		}

		if(!CachedInstallPath.empty() && pStorage != nullptr && pStorage->FileExists(CachedInstallPath.c_str(), IStorage::TYPE_SAVE))
		{
			Asset.m_LocalName = !CachedLocalName.empty() ? CachedLocalName : CanonicalLocalName;
			Asset.m_InstallPath = CachedInstallPath;
			return;
		}

		if(IsReservedNamedSingleFileAssetName(Category, CachedLocalName))
		{
			const std::string MigratedInstallPath = FindLegacyMigratedNamedSingleFileInstallPath(pStorage, Category, CachedLocalName);
			if(!MigratedInstallPath.empty())
			{
				Asset.m_LocalName = LocalNameFromNamedSingleFileInstallPath(Category, MigratedInstallPath);
				Asset.m_InstallPath = MigratedInstallPath;
				return;
			}
		}

		Asset.m_LocalName = CanonicalLocalName;
		Asset.m_InstallPath = CanonicalInstallPath;
	}

	std::string NextReservedNamedSingleFileMigrationName(const SAssetResourceCategory &Category, std::string_view OldName, const std::vector<std::string> &vExistingNames)
	{
		const auto NameExists = [&Category, &vExistingNames](std::string_view Candidate) {
			if(IsReservedNamedSingleFileAssetName(Category, Candidate))
				return true;
			return std::any_of(vExistingNames.begin(), vExistingNames.end(), [Candidate](const std::string &Name) {
				return Name == Candidate;
			});
		};

		std::string Candidate = std::string(OldName) + "_local";
		if(!NameExists(Candidate))
			return Candidate;

		for(int Suffix = 2;; ++Suffix)
		{
			Candidate = std::string(OldName) + "_local_" + std::to_string(Suffix);
			if(!NameExists(Candidate))
				return Candidate;
		}
	}

	void UpdateNamedSingleFileConfigSelectionAfterMigration(const SAssetResourceCategory &Category, const char *pOldName, const char *pNewName)
	{
		if(str_comp(Category.m_pId, "gui_cursor") == 0)
		{
			if(str_comp(g_Config.m_ClAssetGuiCursor, pOldName) == 0)
				str_copy(g_Config.m_ClAssetGuiCursor, pNewName, sizeof(g_Config.m_ClAssetGuiCursor));
		}
		else if(str_comp(Category.m_pId, "arrow") == 0)
		{
			if(str_comp(g_Config.m_ClAssetArrow, pOldName) == 0)
				str_copy(g_Config.m_ClAssetArrow, pNewName, sizeof(g_Config.m_ClAssetArrow));
		}
		else if(str_comp(Category.m_pId, "strong_weak") == 0)
		{
			if(str_comp(g_Config.m_ClAssetStrongWeak, pOldName) == 0)
				str_copy(g_Config.m_ClAssetStrongWeak, pNewName, sizeof(g_Config.m_ClAssetStrongWeak));
		}
	}

	void MigrateReservedNamedSingleFileAssets(IStorage *pStorage, const SAssetResourceCategory &Category)
	{
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
			return;

		std::vector<std::string> vExistingNames;
		CollectNamedSingleFileAssetNames(pStorage, Category, vExistingNames);

		std::vector<std::string> vSaveNames;
		SNamedSingleFileNameScanUser ScanUser{&vSaveNames};
		pStorage->ListDirectory(IStorage::TYPE_SAVE, Category.m_pInstallFolder, CollectNamedSingleFileAssetNamesCallback, &ScanUser);

		for(const std::string &Name : vSaveNames)
		{
			if(IsProtectedDefaultAsset(Name) || !IsReservedNamedSingleFileAssetName(Category, Name))
				continue;

			const std::string NewName = NextReservedNamedSingleFileMigrationName(Category, Name, vExistingNames);
			char aOldPath[IO_MAX_PATH_LENGTH];
			char aNewPath[IO_MAX_PATH_LENGTH];
			str_format(aOldPath, sizeof(aOldPath), "%s/%s.png", Category.m_pInstallFolder, Name.c_str());
			str_format(aNewPath, sizeof(aNewPath), "%s/%s.png", Category.m_pInstallFolder, NewName.c_str());
			if(pStorage->FileExists(aOldPath, IStorage::TYPE_SAVE) &&
				!pStorage->FileExists(aNewPath, IStorage::TYPE_SAVE) &&
				pStorage->RenameFile(aOldPath, aNewPath, IStorage::TYPE_SAVE))
			{
				vExistingNames.emplace_back(NewName);
				UpdateNamedSingleFileConfigSelectionAfterMigration(Category, Name.c_str(), NewName.c_str());
			}
		}
	}

	void TryImportLegacySingleFileAsset(IStorage *pStorage, const SAssetResourceCategory &Category)
	{
		if(Category.m_Kind != EAssetResourceKind::NAMED_SINGLE_FILE)
			return;

		char aMarkerPath[IO_MAX_PATH_LENGTH];
		str_format(aMarkerPath, sizeof(aMarkerPath), "%s/.legacy_imported", Category.m_pInstallFolder);
		if(pStorage->FileExists(aMarkerPath, IStorage::TYPE_SAVE))
			return;

		const char *pLegacySourcePath = LegacySingleFileAssetSourcePath(Category);
		if(pLegacySourcePath == nullptr || !pStorage->FileExists(pLegacySourcePath, IStorage::TYPE_SAVE))
			return;

		std::vector<std::string> vExistingNames;
		CollectNamedSingleFileAssetNames(pStorage, Category, vExistingNames);
		const std::string ImportedAssetName = NextLegacyAssetName(vExistingNames);

		pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
		pStorage->CreateFolder(Category.m_pInstallFolder, IStorage::TYPE_SAVE);

		char aTargetPath[IO_MAX_PATH_LENGTH];
		str_format(aTargetPath, sizeof(aTargetPath), "%s/%s.png", Category.m_pInstallFolder, ImportedAssetName.c_str());
		if(!pStorage->FileExists(aTargetPath, IStorage::TYPE_SAVE) &&
			!CopyStorageFile(pStorage, pLegacySourcePath, IStorage::TYPE_SAVE, aTargetPath, IStorage::TYPE_SAVE))
		{
			return;
		}

		WriteSmallTextFile(pStorage, aMarkerPath, ImportedAssetName);
	}

	std::string NormalizeWorkshopAssetUrl(const char *pUrl)
	{
		if(!pUrl || pUrl[0] == '\0')
			return {};

		std::string Url = pUrl;
		const size_t TransformPos = Url.find("!/");
		if(TransformPos != std::string::npos)
			Url.resize(TransformPos);
		return Url;
	}

	static constexpr const char *ENTITY_BG_PREVIEW_MAP_URL = "https://www.ddrace.cn/data/entity-bg-preview-map.json";

	bool ParseEntityBgPreviewMap(const json_value *pRoot, std::unordered_map<std::string, std::string> &vPreviewExtByName, std::string &BaseUrl, char *pErr, int ErrSize)
	{
		vPreviewExtByName.clear();
		BaseUrl.clear();

		if(!pRoot || pRoot->type != json_object)
		{
			str_copy(pErr, "Invalid entity bg preview response", ErrSize);
			return false;
		}

		const json_value *pBaseUrl = json_object_get(pRoot, "baseUrl");
		const json_value *pPreviewMap = json_object_get(pRoot, "previewMap");
		if(pBaseUrl == &json_value_none || pBaseUrl->type != json_string || pPreviewMap == &json_value_none || pPreviewMap->type != json_object)
		{
			str_copy(pErr, "Entity bg preview data is missing", ErrSize);
			return false;
		}

		BaseUrl = NormalizeWorkshopAssetUrl(json_string_get(pBaseUrl));
		if(BaseUrl.empty())
		{
			str_copy(pErr, "Entity bg preview base url is empty", ErrSize);
			return false;
		}

		for(unsigned i = 0; i < pPreviewMap->u.object.length; ++i)
		{
			const auto &Entry = pPreviewMap->u.object.values[i];
			if(Entry.name == nullptr || Entry.value == nullptr || Entry.value->type != json_string)
				continue;
			const char *pExt = json_string_get(Entry.value);
			if(pExt == nullptr || pExt[0] == '\0')
				continue;
			vPreviewExtByName[Entry.name] = pExt;
		}

		str_copy(pErr, "", ErrSize);
		return !vPreviewExtByName.empty();
	}

	bool BuildEntityBgPreviewThumbUrl(const SWorkshopHudState &WorkshopState, const char *pAssetName, char *pOut, int OutSize)
	{
		if(OutSize <= 0)
			return false;
		pOut[0] = '\0';
		if(pAssetName == nullptr || pAssetName[0] == '\0' || WorkshopState.m_EntityBgPreviewBaseUrl.empty())
			return false;

		const auto It = WorkshopState.m_vEntityBgPreviewExtByName.find(pAssetName);
		if(It == WorkshopState.m_vEntityBgPreviewExtByName.end())
			return false;

		char aEscapedName[IO_MAX_PATH_LENGTH * 3];
		EscapeUrl(aEscapedName, sizeof(aEscapedName), pAssetName);
		str_format(pOut, OutSize, "%s/%s.%s", WorkshopState.m_EntityBgPreviewBaseUrl.c_str(), aEscapedName, It->second.c_str());
		return true;
	}

	void ApplyEntityBgPreviewThumbUrls(SWorkshopHudState &WorkshopState)
	{
		char aThumbUrl[IO_MAX_PATH_LENGTH * 2];
		for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
		{
			if(BuildEntityBgPreviewThumbUrl(WorkshopState, Asset.m_Name.c_str(), aThumbUrl, sizeof(aThumbUrl)))
				Asset.m_ThumbUrl = aThumbUrl;
			else
				Asset.m_ThumbUrl.clear();
		}
	}

	bool WorkshopCategoryMatches(const char *pCategoryValue, const SAssetResourceCategory &Category)
	{
		if(!pCategoryValue || pCategoryValue[0] == '\0')
			return false;

		if((Category.m_pWorkshopCategory && Category.m_pWorkshopCategory[0] != '\0' && str_comp(pCategoryValue, Category.m_pWorkshopCategory) == 0) ||
			(Category.m_pWorkshopCategoryAlt && Category.m_pWorkshopCategoryAlt[0] != '\0' && str_comp(pCategoryValue, Category.m_pWorkshopCategoryAlt) == 0))
			return true;

		for(const char *pAlias : Category.m_vWorkshopCategoryAliases)
		{
			if(pAlias != nullptr && pAlias[0] != '\0' && str_comp(pCategoryValue, pAlias) == 0)
				return true;
		}

		return false;
	}

	bool ParseWorkshopAssets(const json_value *pRoot, const SAssetResourceCategory &Category, std::vector<SWorkshopHudAsset> &vOut, char *pErr, int ErrSize)
	{
		vOut.clear();
		std::unordered_set<std::string> vUsedLocalNames;
		if(!pRoot || pRoot->type != json_object)
		{
			str_copy(pErr, "Invalid workshop response", ErrSize);
			return false;
		}

		const json_value *pAssets = json_object_get(pRoot, "assets");
		bool LegacyApi = false;
		if(pAssets == &json_value_none)
		{
			const json_value *pCode = json_object_get(pRoot, "code");
			if(pCode != &json_value_none && (pCode->type != json_integer || pCode->u.integer != 0))
			{
				const json_value *pMessage = json_object_get(pRoot, "message");
				if(pMessage != &json_value_none && pMessage->type == json_string)
					str_copy(pErr, json_string_get(pMessage), ErrSize);
				else
					str_copy(pErr, "Workshop api returned error", ErrSize);
				return false;
			}
			pAssets = json_object_get(pRoot, "data");
			LegacyApi = true;
		}

		if(pAssets == &json_value_none || pAssets->type != json_array)
		{
			str_copy(pErr, "Workshop asset list is missing", ErrSize);
			return false;
		}

		vOut.reserve(pAssets->u.array.length);
		for(unsigned i = 0; i < pAssets->u.array.length; ++i)
		{
			const json_value &Entry = (*pAssets)[i];
			if(Entry.type != json_object)
				continue;

			const json_value *pCategory = json_object_get(&Entry, "category");
			if(pCategory == &json_value_none || pCategory->type != json_string)
				continue;
			const char *pCategoryValue = json_string_get(pCategory);
			if(!WorkshopCategoryMatches(pCategoryValue, Category))
				continue;

			const json_value *pImage = json_object_get(&Entry, LegacyApi ? "image" : "image_url");
			if(pImage == &json_value_none)
				pImage = json_object_get(&Entry, "image");
			if(pImage == &json_value_none)
				pImage = json_object_get(&Entry, "image_url");
			if(pImage == &json_value_none || pImage->type != json_string)
				continue;

			const json_value *pId = json_object_get(&Entry, "id");
			const json_value *pName = json_object_get(&Entry, "name");
			const json_value *pAuthor = json_object_get(&Entry, "author");
			const json_value *pDownload = json_object_get(&Entry, "download_url");

			SWorkshopHudAsset Asset;
			Asset.m_Id = pId != &json_value_none && pId->type == json_string ? json_string_get(pId) : std::to_string(i);
			Asset.m_Name = pName != &json_value_none && pName->type == json_string ? json_string_get(pName) : Asset.m_Id;
			Asset.m_Author = pAuthor != &json_value_none && pAuthor->type == json_string ? NormalizeWorkshopAuthorName(json_string_get(pAuthor)) : "";
			Asset.m_ImageUrl = NormalizeWorkshopAssetUrl(json_string_get(pImage));
			if(Asset.m_ImageUrl.empty())
				continue;
			Asset.m_DownloadUrl = pDownload != &json_value_none && pDownload->type == json_string ? NormalizeWorkshopAssetUrl(json_string_get(pDownload)) : "";
			if(Asset.m_DownloadUrl.empty() && WorkshopEntityBgAllowsImageUrlFallback(Category.m_pId))
				Asset.m_DownloadUrl = Asset.m_ImageUrl;
			Asset.m_ThumbUrl = str_comp(Category.m_pId, "entity_bg") == 0 ? "" : Asset.m_ImageUrl;

			char aExt[16];
			GuessUrlExtension(Asset.m_ImageUrl.c_str(), aExt, sizeof(aExt));

			std::string SafeInstallName = BuildSafeFilename(Asset.m_Name.c_str(), Asset.m_Id.c_str(), aExt);
			const size_t DotPos = SafeInstallName.find_last_of('.');
			const std::string PreferredLocalBaseName = DotPos == std::string::npos ? SafeInstallName : SafeInstallName.substr(0, DotPos);
			const std::string LocalBaseName = UniqueWorkshopAssetBaseName(Category, PreferredLocalBaseName, Asset.m_Id.c_str(), vUsedLocalNames);
			if(LocalBaseName != PreferredLocalBaseName)
				SafeInstallName = LocalBaseName + "." + aExt;
			if(str_comp(Category.m_pId, "entity_bg") == 0)
				Asset.m_LocalName = "entity_bg/" + LocalBaseName;
			else
				Asset.m_LocalName = LocalBaseName;
			vUsedLocalNames.insert(Asset.m_LocalName);
			char aInstallPath[IO_MAX_PATH_LENGTH];
			str_format(aInstallPath, sizeof(aInstallPath), "%s/%s", Category.m_pInstallFolder, SafeInstallName.c_str());
			Asset.m_InstallPath = aInstallPath;
			if(str_comp(Category.m_pId, "entity_bg") == 0)
				NormalizeEntityBgWorkshopAsset(Asset, nullptr);

			char aSafeId[80];
			str_copy(aSafeId, Asset.m_Id.c_str(), sizeof(aSafeId));
			SanitizeFilenameInPlace(aSafeId);
			if(aSafeId[0] == '\0')
				str_copy(aSafeId, "asset", sizeof(aSafeId));
			char aThumbPath[IO_MAX_PATH_LENGTH];
			// Always use webp for thumbnail cache to save space
			str_format(aThumbPath, sizeof(aThumbPath), "qmclient/workshop/thumbs/%s.webp", aSafeId);
			Asset.m_ThumbCachePath = aThumbPath;
			if(!WorkshopAssetHasRequiredDownloadUrl(Category.m_pId, !Asset.m_DownloadUrl.empty()))
				continue;

			vOut.push_back(std::move(Asset));
		}

		std::sort(vOut.begin(), vOut.end(), [](const SWorkshopHudAsset &Left, const SWorkshopHudAsset &Right) { return str_comp(Left.m_Name.c_str(), Right.m_Name.c_str()) < 0; });
		str_copy(pErr, "", ErrSize);
		return true;
	}

	void ResetWorkshopState(SWorkshopHudState &WorkshopState, IGraphics *pGraphics, bool AbortTasks)
	{
		if(AbortTasks && WorkshopState.m_pListTask)
		{
			WorkshopState.m_pListTask->Abort();
		}
		if(AbortTasks && WorkshopState.m_pEntityBgPreviewTask)
		{
			WorkshopState.m_pEntityBgPreviewTask->Abort();
		}
		WorkshopState.m_pListTask.reset();
		WorkshopState.m_pEntityBgPreviewTask.reset();

		for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
		{
			ResetWorkshopAssetRuntimeState(Asset, pGraphics, AbortTasks);
		}

		WorkshopState.m_vAssets.clear();
		WorkshopState.m_vEntityBgPreviewExtByName.clear();
		WorkshopState.m_EntityBgPreviewBaseUrl.clear();
		ClearWorkshopDecodeThumbQueue(WorkshopState);
		ClearWorkshopReadyThumbQueue(WorkshopState);
		WorkshopState.m_Requested = false;
		WorkshopState.m_EntityBgPreviewRequested = false;
		WorkshopState.m_LoadFailed = false;
		// Keep m_CacheTime and m_LastRefreshTime for cache reuse
		str_copy(WorkshopState.m_aError, "");
	}

	// Serialize Workshop assets to JSON file for local cache
	static bool SaveWorkshopCache(SWorkshopHudState &WorkshopState, IStorage *pStorage, const char *pCachePath)
	{
		if(WorkshopState.m_vAssets.empty())
			return false;

		char aFullPath[IO_MAX_PATH_LENGTH];
		str_format(aFullPath, sizeof(aFullPath), "cache/%s", pCachePath);

		IOHANDLE File = pStorage->OpenFile(aFullPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;

		std::string JsonStr = "[";
		for(size_t i = 0; i < WorkshopState.m_vAssets.size(); ++i)
		{
			const SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[i];
			if(i > 0)
				JsonStr += ",";
			JsonStr += "{";
			JsonStr += "\"id\":\"" + JsonEscape(Asset.m_Id) + "\",";
			JsonStr += "\"name\":\"" + JsonEscape(Asset.m_Name) + "\",";
			JsonStr += "\"author\":\"" + JsonEscape(Asset.m_Author) + "\",";
			JsonStr += "\"image_url\":\"" + JsonEscape(Asset.m_ImageUrl) + "\",";
			JsonStr += "\"download_url\":\"" + JsonEscape(Asset.m_DownloadUrl) + "\",";
			JsonStr += "\"thumb_url\":\"" + JsonEscape(Asset.m_ThumbUrl) + "\",";
			JsonStr += "\"thumb_cache\":\"" + JsonEscape(Asset.m_ThumbCachePath) + "\",";
			JsonStr += "\"install_path\":\"" + JsonEscape(Asset.m_InstallPath) + "\"";
			JsonStr += "}";
		}
		JsonStr += "]";

		io_write(File, JsonStr.c_str(), JsonStr.size());
		io_close(File);
		return true;
	}

	// Load Workshop assets from local cache
	static bool LoadWorkshopCache(SWorkshopHudState &WorkshopState, IStorage *pStorage, const char *pCachePath)
	{
		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		char aFullPath[IO_MAX_PATH_LENGTH];
		str_format(aFullPath, sizeof(aFullPath), "cache/%s", pCachePath);

		IOHANDLE File = pStorage->OpenFile(aFullPath, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return false;

		char aBuffer[1024 * 512]; // 512KB buffer
		long FileSize = io_length(File);
		if(FileSize <= 0 || FileSize >= (long)sizeof(aBuffer))
		{
			io_close(File);
			return false;
		}

		io_read(File, aBuffer, FileSize);
		aBuffer[FileSize] = '\0';
		io_close(File);

		json_settings JsonSettings = {};
		char JsonError[1024];
		json_value *pJson = JsonParseEx(&JsonSettings, aBuffer, FileSize, JsonError);
		if(!pJson)
			return false;

		std::vector<SWorkshopHudAsset> vLoadedAssets;
		ClearWorkshopDecodeThumbQueue(WorkshopState);
		ClearWorkshopReadyThumbQueue(WorkshopState);
		bool MissingDownloadMetadata = false;
		for(unsigned i = 0; i < pJson->u.array.length; ++i)
		{
			json_value *pItem = pJson->u.array.values[i];
			if(pItem->type != json_object)
				continue;

			SWorkshopHudAsset Asset;
			for(unsigned j = 0; j < pItem->u.object.length; ++j)
			{
				const char *pKey = pItem->u.object.values[j].name;
				json_value *pVal = pItem->u.object.values[j].value;
				if(str_comp(pKey, "id") == 0 && pVal->type == json_string)
					Asset.m_Id = pVal->u.string.ptr;
				else if(str_comp(pKey, "name") == 0 && pVal->type == json_string)
					Asset.m_Name = pVal->u.string.ptr;
				else if(str_comp(pKey, "author") == 0 && pVal->type == json_string)
					Asset.m_Author = NormalizeWorkshopAuthorName(pVal->u.string.ptr);
				else if(str_comp(pKey, "image_url") == 0 && pVal->type == json_string)
					Asset.m_ImageUrl = NormalizeWorkshopAssetUrl(pVal->u.string.ptr);
				else if(str_comp(pKey, "download_url") == 0 && pVal->type == json_string)
					Asset.m_DownloadUrl = NormalizeWorkshopAssetUrl(pVal->u.string.ptr);
				else if(str_comp(pKey, "thumb_url") == 0 && pVal->type == json_string)
					Asset.m_ThumbUrl = NormalizeWorkshopAssetUrl(pVal->u.string.ptr);
				else if(str_comp(pKey, "thumb_cache") == 0 && pVal->type == json_string)
					Asset.m_ThumbCachePath = pVal->u.string.ptr;
				else if(str_comp(pKey, "install_path") == 0 && pVal->type == json_string)
					Asset.m_InstallPath = pVal->u.string.ptr;
			}
			if(Asset.m_ImageUrl.empty())
				continue;
			if(Asset.m_DownloadUrl.empty())
			{
				if(WorkshopEntityBgAllowsImageUrlFallback(pCategory != nullptr ? pCategory->m_pId : nullptr))
					Asset.m_DownloadUrl = Asset.m_ImageUrl;
				else
					MissingDownloadMetadata = true;
			}
			if(!WorkshopAssetHasRequiredDownloadUrl(pCategory != nullptr ? pCategory->m_pId : nullptr, !Asset.m_DownloadUrl.empty()))
			{
				MissingDownloadMetadata = true;
				continue;
			}
			if(pCategory != nullptr)
				RepairWorkshopAssetInstallState(*pCategory, Asset, pStorage);
			if(Asset.m_ThumbUrl.empty() && !str_endswith_nocase(Asset.m_ImageUrl.c_str(), ".map"))
				Asset.m_ThumbUrl = Asset.m_ImageUrl;
			vLoadedAssets.push_back(std::move(Asset));
		}

		json_value_free(pJson);
		if(MissingDownloadMetadata)
			return false;
		WorkshopState.m_vAssets = std::move(vLoadedAssets);
		return !WorkshopState.m_vAssets.empty();
	}

	// Get cache filename for a workshop tab
	static const char *GetWorkshopCacheFilename(int Tab)
	{
		switch(Tab)
		{
		case ASSETS_TAB_HUD: return "workshop_hud.json";
		case ASSETS_TAB_ENTITIES: return "workshop_entities.json";
		case ASSETS_TAB_GAME: return "workshop_game.json";
		case ASSETS_TAB_EMOTICONS: return "workshop_emoticons.json";
		case ASSETS_TAB_PARTICLES: return "workshop_particles.json";
		case ASSETS_TAB_GUI_CURSOR: return "workshop_gui_cursor.json";
		case ASSETS_TAB_ARROW: return "workshop_arrow.json";
		case ASSETS_TAB_STRONG_WEAK: return "workshop_strong_weak.json";
		case ASSETS_TAB_ENTITY_BG: return "workshop_entity_bg.json";
		case ASSETS_TAB_EXTRAS: return "workshop_extras.json";
		default: return nullptr;
		}
	}

	bool DeleteLocalAssetByTab(IStorage *pStorage, int CurTab, const char *pAssetName)
	{
		if(IsProtectedDefaultAsset(pAssetName))
			return false;

		const char *pSubFolder = nullptr;
		switch(CurTab)
		{
		case ASSETS_TAB_ENTITIES: pSubFolder = "entities"; break;
		case ASSETS_TAB_GAME: pSubFolder = "game"; break;
		case ASSETS_TAB_EMOTICONS: pSubFolder = "emoticons"; break;
		case ASSETS_TAB_PARTICLES: pSubFolder = "particles"; break;
		case ASSETS_TAB_HUD: pSubFolder = "hud"; break;
		case ASSETS_TAB_GUI_CURSOR: pSubFolder = "gui_cursor"; break;
		case ASSETS_TAB_ARROW: pSubFolder = "arrow"; break;
		case ASSETS_TAB_STRONG_WEAK: pSubFolder = "strong_weak"; break;
		case ASSETS_TAB_ENTITY_BG:
		{
			char aMapPath[IO_MAX_PATH_LENGTH];
			ResolveEntityBgLocalMapPath(pStorage, pAssetName, IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
			const bool Removed = pStorage->RemoveFile(aMapPath, IStorage::TYPE_SAVE);
			if(Removed)
				RemovePersistedLocalAssetAuthor(CurTab, pAssetName, pStorage);
			return Removed;
		}
		case ASSETS_TAB_EXTRAS: pSubFolder = "extras"; break;
		default: return false;
		}

		char aSingleFilePath[IO_MAX_PATH_LENGTH];
		str_format(aSingleFilePath, sizeof(aSingleFilePath), "assets/%s/%s.png", pSubFolder, pAssetName);
		bool Removed = pStorage->RemoveFile(aSingleFilePath, IStorage::TYPE_SAVE);

		char aFolderPath[IO_MAX_PATH_LENGTH];
		str_format(aFolderPath, sizeof(aFolderPath), "assets/%s/%s", pSubFolder, pAssetName);
		if(pStorage->FolderExists(aFolderPath, IStorage::TYPE_SAVE))
			Removed |= RemoveFolderTree(pStorage, aFolderPath);
		if(Removed)
			RemovePersistedLocalAssetAuthor(CurTab, pAssetName, pStorage);

		return Removed;
	}

	bool CanDeleteLocalAssetByTab(IStorage *pStorage, int CurTab, const char *pAssetName)
	{
		if(IsProtectedDefaultAsset(pAssetName))
			return false;

		switch(CurTab)
		{
		case ASSETS_TAB_ENTITY_BG:
		{
			char aMapPath[IO_MAX_PATH_LENGTH];
			ResolveEntityBgLocalMapPath(pStorage, pAssetName, IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
			return pStorage->FileExists(aMapPath, IStorage::TYPE_SAVE);
		}
		case ASSETS_TAB_ENTITIES:
		case ASSETS_TAB_GAME:
		case ASSETS_TAB_EMOTICONS:
		case ASSETS_TAB_PARTICLES:
		case ASSETS_TAB_HUD:
		case ASSETS_TAB_GUI_CURSOR:
		case ASSETS_TAB_ARROW:
		case ASSETS_TAB_STRONG_WEAK:
		case ASSETS_TAB_EXTRAS:
			return true;
		default:
			return false;
		}
	}
} // namespace

template<typename TName>
static const CMenus::SCustomItem *GetCustomItemFromSearchList(const std::vector<TName *> &vpSearchList, size_t Index)
{
	if(Index >= vpSearchList.size())
		return nullptr;
	return vpSearchList[Index];
}

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return GetCustomItemFromSearchList(gs_vpSearchEntitiesList, Index);
	else if(CurTab == ASSETS_TAB_GAME)
		return GetCustomItemFromSearchList(gs_vpSearchGamesList, Index);
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return GetCustomItemFromSearchList(gs_vpSearchEmoticonsList, Index);
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return GetCustomItemFromSearchList(gs_vpSearchParticlesList, Index);
	else if(CurTab == ASSETS_TAB_HUD)
		return GetCustomItemFromSearchList(gs_vpSearchHudList, Index);
	else if(CurTab == ASSETS_TAB_GUI_CURSOR)
		return GetCustomItemFromSearchList(gs_vpSearchGuiCursorList, Index);
	else if(CurTab == ASSETS_TAB_ARROW)
		return GetCustomItemFromSearchList(gs_vpSearchArrowList, Index);
	else if(CurTab == ASSETS_TAB_STRONG_WEAK)
		return GetCustomItemFromSearchList(gs_vpSearchStrongWeakList, Index);
	else if(CurTab == ASSETS_TAB_ENTITY_BG)
		return GetCustomItemFromSearchList(gs_vpSearchEntityBgList, Index);
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return GetCustomItemFromSearchList(gs_vpSearchExtrasList, Index);
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

static size_t GetCustomItemCount(int CurTab)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList.size();
	if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList.size();
	if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList.size();
	if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList.size();
	if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList.size();
	if(CurTab == ASSETS_TAB_GUI_CURSOR)
		return gs_vpSearchGuiCursorList.size();
	if(CurTab == ASSETS_TAB_ARROW)
		return gs_vpSearchArrowList.size();
	if(CurTab == ASSETS_TAB_STRONG_WEAK)
		return gs_vpSearchStrongWeakList.size();
	if(CurTab == ASSETS_TAB_ENTITY_BG)
		return gs_vpSearchEntityBgList.size();
	if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList.size();
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

static CMenus::SCustomItem *GetCustomItemMutable(int CurTab, size_t Index)
{
	return const_cast<CMenus::SCustomItem *>(GetCustomItem(CurTab, Index));
}

static void PopulateLocalAssetAuthor(CMenus::SCustomItem &Item, int Tab, IStorage *pStorage)
{
	Item.m_aAuthor[0] = '\0';
	if(const char *pPersistedAuthor = FindPersistedLocalAssetAuthor(pStorage, Tab, Item.m_aName))
		str_copy(Item.m_aAuthor, pPersistedAuthor, sizeof(Item.m_aAuthor));
}

static void RefreshPublishedLocalAssetAuthorsForTab(int Tab, IStorage *pStorage)
{
	const size_t Count = GetCustomItemCount(Tab);
	for(size_t i = 0; i < Count; ++i)
	{
		if(CMenus::SCustomItem *pItem = GetCustomItemMutable(Tab, i))
			PopulateLocalAssetAuthor(*pItem, Tab, pStorage);
	}
}

static void ResetCustomItemPreviewState(CMenus::SCustomItem &Item)
{
	Item.m_pDecodeJob.reset();
	Item.m_PreviewImage.Free();
	Item.m_PreviewState = CMenus::SCustomItem::PREVIEW_STATE_UNLOADED;
	Item.m_PreviewEpoch = 0;
	Item.m_PreviewListIndex = 0;
	Item.m_PreviewBytes = 0;
	Item.m_PreviewRequestedTextureSize = 0;
	Item.m_PreviewResidentBytes = 0;
	Item.m_PreviewResized = false;
	Item.m_PreviewHighPriority = false;
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
		ResetCustomItemPreviewState(Asset);
	}
	vList.clear();
}

void CMenus::SyncEntityBgInstalledWorkshopSources()
{
	SWorkshopHudState *pWorkshopState = WorkshopStateByTab(ASSETS_TAB_ENTITY_BG);
	if(pWorkshopState == nullptr)
		return;

	for(auto NameIt = m_vEntityBgSourceNames.begin(); NameIt != m_vEntityBgSourceNames.end();)
	{
		const auto SourceIt = m_vEntityBgSourceKinds.find(*NameIt);
		if(SourceIt != m_vEntityBgSourceKinds.end() && SourceIt->second == EEntityBgHierarchyEntrySource::WORKSHOP)
		{
			char aMapPath[IO_MAX_PATH_LENGTH];
			ResolveEntityBgLocalMapPath(Storage(), NameIt->c_str(), IStorage::TYPE_SAVE, aMapPath, sizeof(aMapPath));
			if(aMapPath[0] == '\0' || !Storage()->FileExists(aMapPath, IStorage::TYPE_SAVE))
			{
				m_vEntityBgSourceKinds.erase(SourceIt);
				NameIt = m_vEntityBgSourceNames.erase(NameIt);
				continue;
			}
		}
		++NameIt;
	}

	for(const SWorkshopHudAsset &Asset : pWorkshopState->m_vAssets)
	{
		if(!Asset.m_Installed || Asset.m_LocalName.empty())
			continue;

		if(std::none_of(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), [&Asset](const std::string &Name) { return Name == Asset.m_LocalName; }))
			m_vEntityBgSourceNames.push_back(Asset.m_LocalName);
		const auto ExistingIt = m_vEntityBgSourceKinds.find(Asset.m_LocalName);
		if(ExistingIt == m_vEntityBgSourceKinds.end())
			m_vEntityBgSourceKinds.emplace(Asset.m_LocalName, EEntityBgHierarchyEntrySource::WORKSHOP);
		else
			ExistingIt->second = MergeEntityBgHierarchyEntrySource(ExistingIt->second, EEntityBgHierarchyEntrySource::WORKSHOP);
	}
}

void CMenus::RefreshEntityBgHierarchyView()
{
	++m_aCustomPreviewEpoch[ASSETS_TAB_ENTITY_BG];
	m_aaCustomPreviewDecodeQueue[ASSETS_TAB_ENTITY_BG].clear();
	m_aaCustomPreviewReadyQueue[ASSETS_TAB_ENTITY_BG].clear();
	m_aaCustomPreviewReadyQueued[ASSETS_TAB_ENTITY_BG].clear();

	ClearAssetList(m_vEntityBgList, Graphics());
	gs_vpSearchEntityBgList.clear();

	::EnsureDefaultAssetVisible(m_vEntityBgSourceNames);
	m_vEntityBgSourceKinds["default"] = EEntityBgHierarchyEntrySource::LOCAL;
	SyncEntityBgInstalledWorkshopSources();

	if(!m_ShowWorkshopAssets && IsEntityBgWorkshopFolderOrChild(m_aEntityBgCurrentFolder))
		m_aEntityBgCurrentFolder[0] = '\0';

	std::vector<SEntityBgHierarchyEntry> vEntries;
	const SWorkshopHudState *pEntityBgWorkshopState = WorkshopStateByTab(ASSETS_TAB_ENTITY_BG);
	if(s_aFilterInputs[ASSETS_TAB_ENTITY_BG].IsEmpty())
	{
		const bool ForceShowWorkshopFolder = pEntityBgWorkshopState != nullptr && !pEntityBgWorkshopState->m_vAssets.empty();
		vEntries = BuildEntityBgHierarchyEntries(m_vEntityBgSourceNames, m_aEntityBgCurrentFolder, m_ShowWorkshopAssets, &m_vEntityBgSourceKinds, ForceShowWorkshopFolder);
	}
	else
	{
		const char *pFilter = s_aFilterInputs[ASSETS_TAB_ENTITY_BG].GetString();
		for(const std::string &AssetName : m_vEntityBgSourceNames)
		{
			const auto SourceIt = m_vEntityBgSourceKinds.find(AssetName);
			const EEntityBgHierarchyEntrySource Source = SourceIt != m_vEntityBgSourceKinds.end() ? SourceIt->second : EEntityBgHierarchyEntrySource::LOCAL;
			if(!m_ShowWorkshopAssets && Source == EEntityBgHierarchyEntrySource::WORKSHOP)
				continue;

			SEntityBgHierarchyEntry Entry;
			str_copy(Entry.m_aName, AssetName.c_str(), sizeof(Entry.m_aName));
			const char *pDisplayName = AssetName.c_str();
			if(const char *pSlash = str_rchr(pDisplayName, '/'))
				pDisplayName = pSlash + 1;
			str_copy(Entry.m_aDisplayName, pDisplayName, sizeof(Entry.m_aDisplayName));
			Entry.m_IsDirectory = false;
			Entry.m_Source = Source;
			SCustomEntityBg PreviewItem;
			str_copy(PreviewItem.m_aName, Entry.m_aName, sizeof(PreviewItem.m_aName));
			PopulateLocalAssetAuthor(PreviewItem, ASSETS_TAB_ENTITY_BG, Storage());
			const char *pAuthor = PreviewItem.m_aAuthor;
			if(!SearchFilterMatches(pFilter, Entry.m_aDisplayName, pAuthor) &&
				!SearchFilterMatches(pFilter, Entry.m_aName, pAuthor))
				continue;

			vEntries.push_back(Entry);
		}

		std::stable_sort(vEntries.begin(), vEntries.end(), [](const SEntityBgHierarchyEntry &Left, const SEntityBgHierarchyEntry &Right) {
			return AssetResourceNameLess(Left.m_aDisplayName, Right.m_aDisplayName);
		});
	}

	m_vEntityBgList.reserve(vEntries.size());
	for(const SEntityBgHierarchyEntry &Entry : vEntries)
	{
		SCustomEntityBg Item;
		str_copy(Item.m_aName, Entry.m_aName, sizeof(Item.m_aName));
		str_copy(Item.m_aDisplayName, Entry.m_aDisplayName, sizeof(Item.m_aDisplayName));
		Item.m_IsDirectory = Entry.m_IsDirectory;
		if(!Item.m_IsDirectory)
			PopulateLocalAssetAuthor(Item, ASSETS_TAB_ENTITY_BG, Storage());
		m_vEntityBgList.push_back(Item);
	}

	dbg_assert(s_CurCustomTab == ASSETS_TAB_ENTITY_BG, "entity bg hierarchy refresh is only valid for entity bg tab");
	gs_aInitCustomList[s_CurCustomTab] = true;
}

void CMenus::ClearCustomItems(int CurTab)
{
	// Reset async loading state first
	m_aAssetLoadStates[CurTab] = ASSET_LOAD_STATE_UNLOADED;
	if(CurTab >= ASSETS_TAB_ENTITIES && CurTab < NUMBER_OF_ASSETS_TABS)
	{
		gs_aAssetWarmupReady[CurTab] = false;
		gs_NextAssetWarmupTab = CurTab;
		m_aAssetPendingMerges[CurTab] = {};
		++m_aAssetLoadGenerations[CurTab];
	}
	++m_aCustomPreviewEpoch[CurTab];
	m_aaCustomPreviewDecodeQueue[CurTab].clear();
	m_aaCustomPreviewReadyQueue[CurTab].clear();
	m_aaCustomPreviewReadyQueued[CurTab].clear();
	if(m_apAssetLoadJobs[CurTab])
	{
		// Note: We don't wait for the job to complete here as it could cause a stall.
		// The job will complete in the background and be ignored on next frame.
		m_apAssetLoadJobs[CurTab].reset();
	}

	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			ResetCustomItemPreviewState(Entity);
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();
		gs_vpSearchEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());
		gs_vpSearchGamesList.clear();

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());
		gs_vpSearchEmoticonsList.clear();

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());
		gs_vpSearchParticlesList.clear();

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());
		gs_vpSearchHudList.clear();

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_GUI_CURSOR)
	{
		ClearAssetList(m_vGuiCursorList, Graphics());
		gs_vpSearchGuiCursorList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
	}
	else if(CurTab == ASSETS_TAB_ARROW)
	{
		ClearAssetList(m_vArrowList, Graphics());
		gs_vpSearchArrowList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
	}
	else if(CurTab == ASSETS_TAB_STRONG_WEAK)
	{
		ClearAssetList(m_vStrongWeakList, Graphics());
		gs_vpSearchStrongWeakList.clear();

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
	}
	else if(CurTab == ASSETS_TAB_ENTITY_BG)
	{
		ClearAssetList(m_vEntityBgList, Graphics());
		m_vEntityBgSourceNames.clear();
		m_vEntityBgSourceKinds.clear();
		m_aEntityBgCurrentFolder[0] = '\0';
		gs_vpSearchEntityBgList.clear();

		GameClient()->m_Background.LoadBackground();
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());
		gs_vpSearchExtrasList.clear();

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else
	{
		dbg_assert_failed("Invalid CurTab: %d", CurTab);
	}
	gs_aInitCustomList[CurTab] = true;
}

void CMenus::InvalidateSettingsAssetResourcePlan()
{
	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		switch(Tab)
		{
		case ASSETS_TAB_ENTITIES:
			for(auto &Entity : m_vEntitiesList)
			{
				ResetCustomItemPreviewState(Entity);
				for(auto &Image : Entity.m_aImages)
					Graphics()->UnloadTexture(&Image.m_Texture);
			}
			m_vEntitiesList.clear();
			gs_vpSearchEntitiesList.clear();
			break;
		case ASSETS_TAB_GAME:
			ClearAssetList(m_vGameList, Graphics());
			gs_vpSearchGamesList.clear();
			break;
		case ASSETS_TAB_EMOTICONS:
			ClearAssetList(m_vEmoticonList, Graphics());
			gs_vpSearchEmoticonsList.clear();
			break;
		case ASSETS_TAB_PARTICLES:
			ClearAssetList(m_vParticlesList, Graphics());
			gs_vpSearchParticlesList.clear();
			break;
		case ASSETS_TAB_HUD:
			ClearAssetList(m_vHudList, Graphics());
			gs_vpSearchHudList.clear();
			break;
		case ASSETS_TAB_GUI_CURSOR:
			ClearAssetList(m_vGuiCursorList, Graphics());
			gs_vpSearchGuiCursorList.clear();
			break;
		case ASSETS_TAB_ARROW:
			ClearAssetList(m_vArrowList, Graphics());
			gs_vpSearchArrowList.clear();
			break;
		case ASSETS_TAB_STRONG_WEAK:
			ClearAssetList(m_vStrongWeakList, Graphics());
			gs_vpSearchStrongWeakList.clear();
			break;
		case ASSETS_TAB_ENTITY_BG:
			ClearAssetList(m_vEntityBgList, Graphics());
			m_vEntityBgSourceNames.clear();
			m_vEntityBgSourceKinds.clear();
			m_aEntityBgCurrentFolder[0] = '\0';
			gs_vpSearchEntityBgList.clear();
			break;
		case ASSETS_TAB_EXTRAS:
			ClearAssetList(m_vExtrasList, Graphics());
			gs_vpSearchExtrasList.clear();
			break;
		default:
			break;
		}
		m_aAssetPendingMerges[Tab] = {};
		++m_aAssetLoadGenerations[Tab];
		++m_aCustomPreviewEpoch[Tab];
		m_aaCustomPreviewDecodeQueue[Tab].clear();
		m_aaCustomPreviewReadyQueue[Tab].clear();
		m_aaCustomPreviewReadyQueued[Tab].clear();
		m_apAssetLoadJobs[Tab].reset();
		gs_aAssetWarmupReady[Tab] = false;
		m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_UNLOADED;
		gs_aInitCustomList[Tab] = true;
	}
	gs_NextAssetWarmupTab = ASSETS_TAB_ENTITIES;
}

template<typename TItem>
static void PublishSettingsAssetMergeEntriesTyped(std::vector<TItem> &vList, int Tab, const std::vector<CMenus::SSettingsAssetMergeEntry> &vEntries, IStorage *pStorage)
{
	vList.reserve(vList.size() + vEntries.size());
	for(const CMenus::SSettingsAssetMergeEntry &Entry : vEntries)
	{
		TItem Item;
		str_copy(Item.m_aName, Entry.m_aName);
		PopulateLocalAssetAuthor(Item, Tab, pStorage);
		vList.push_back(Item);
	}
}

void CMenus::PublishSettingsAssetMergeEntries(int Tab, const std::vector<SSettingsAssetMergeEntry> &vEntries)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES:
		PublishSettingsAssetMergeEntriesTyped<SCustomEntities>(m_vEntitiesList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_GAME:
		PublishSettingsAssetMergeEntriesTyped<SCustomGame>(m_vGameList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_EMOTICONS:
		PublishSettingsAssetMergeEntriesTyped<SCustomEmoticon>(m_vEmoticonList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_PARTICLES:
		PublishSettingsAssetMergeEntriesTyped<SCustomParticle>(m_vParticlesList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_HUD:
		PublishSettingsAssetMergeEntriesTyped<SCustomHud>(m_vHudList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_GUI_CURSOR:
		PublishSettingsAssetMergeEntriesTyped<SCustomGuiCursor>(m_vGuiCursorList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_ARROW:
		PublishSettingsAssetMergeEntriesTyped<SCustomArrow>(m_vArrowList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_STRONG_WEAK:
		PublishSettingsAssetMergeEntriesTyped<SCustomStrongWeak>(m_vStrongWeakList, Tab, vEntries, Storage());
		break;
	case ASSETS_TAB_ENTITY_BG:
		// ENTITY_BG maintains a source-name/source-kind index instead of an item list,
		// so it cannot share the typed merge path used by the other tabs.
		m_vEntityBgSourceNames.reserve(m_vEntityBgSourceNames.size() + vEntries.size());
		for(const SSettingsAssetMergeEntry &Entry : vEntries)
		{
			m_vEntityBgSourceNames.emplace_back(Entry.m_aName);
			const auto ExistingIt = m_vEntityBgSourceKinds.find(Entry.m_aName);
			if(ExistingIt == m_vEntityBgSourceKinds.end())
				m_vEntityBgSourceKinds.emplace(Entry.m_aName, Entry.m_Source);
			else
				ExistingIt->second = MergeEntityBgHierarchyEntrySource(ExistingIt->second, Entry.m_Source);
		}
		break;
	case ASSETS_TAB_EXTRAS:
		PublishSettingsAssetMergeEntriesTyped<SCustomExtras>(m_vExtrasList, Tab, vEntries, Storage());
		break;
	default:
		break;
	}
}

bool CMenus::PrewarmSettingsAssetResources()
{
	if(SettingsAssetWarmupAllTabsReady(gs_aAssetWarmupReady, NUMBER_OF_ASSETS_TABS))
		return true;

	for(int Attempts = 0; Attempts < NUMBER_OF_ASSETS_TABS; ++Attempts)
	{
		const int Tab = gs_NextAssetWarmupTab;
		gs_NextAssetWarmupTab = SettingsAssetWarmupNextTab(gs_NextAssetWarmupTab, NUMBER_OF_ASSETS_TABS);
		if(Tab < ASSETS_TAB_ENTITIES || Tab >= NUMBER_OF_ASSETS_TABS || gs_aAssetWarmupReady[Tab])
			continue;

		if(m_aAssetLoadStates[Tab] == ASSET_LOAD_STATE_UNLOADED)
		{
			const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(Tab);
			switch(Tab)
			{
			case ASSETS_TAB_ENTITIES:
				dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
				break;
			case ASSETS_TAB_GAME:
				dbg_assert(pCurrentCategory != nullptr, "game category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
				break;
			case ASSETS_TAB_EMOTICONS:
				dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
				break;
			case ASSETS_TAB_PARTICLES:
				dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
				break;
			case ASSETS_TAB_HUD:
				dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
				break;
			case ASSETS_TAB_GUI_CURSOR:
				dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
				break;
			case ASSETS_TAB_ARROW:
				dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
				break;
			case ASSETS_TAB_STRONG_WEAK:
				dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
				break;
			case ASSETS_TAB_ENTITY_BG:
			{
				const int SavedTab = s_CurCustomTab;
				s_CurCustomTab = Tab;
				RefreshEntityBgHierarchyView();
				s_CurCustomTab = SavedTab;
				break;
			}
			case ASSETS_TAB_EXTRAS:
				dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
				break;
			default:
				break;
			}

			auto pJob = std::make_shared<CAssetListLoadJob>(
				static_cast<CAssetListLoadJob::EAssetType>(Tab), Storage());
			pJob->SetGeneration(m_aAssetLoadGenerations[Tab]);
			m_apAssetLoadJobs[Tab] = pJob;
			Engine()->AddJob(m_apAssetLoadJobs[Tab]);
			m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_LOADING;
		}

		if(m_aAssetLoadStates[Tab] == ASSET_LOAD_STATE_LOADING &&
			m_apAssetLoadJobs[Tab] &&
			std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[Tab])->IsCompleted())
		{
			auto pJob = std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[Tab]);
			if(!SettingsAssetListJobGenerationMatches(pJob->Generation(), m_aAssetLoadGenerations[Tab]))
			{
				m_apAssetLoadJobs[Tab].reset();
				m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_UNLOADED;
				gs_aAssetWarmupReady[Tab] = false;
				gs_NextAssetWarmupTab = Tab;
				return false;
			}
			std::vector<CAssetListLoadJob::SAssetEntry> vEntries = pJob->TakeEntries();
			++m_aCustomPreviewEpoch[Tab];
			m_aaCustomPreviewDecodeQueue[Tab].clear();
			m_aaCustomPreviewReadyQueue[Tab].clear();
			m_aaCustomPreviewReadyQueued[Tab].clear();
			SSettingsAssetPendingMerge &PendingMerge = m_aAssetPendingMerges[Tab];
			PendingMerge = {};
			PendingMerge.m_Tab = Tab;
			PendingMerge.m_Generation = ++m_aAssetLoadGenerations[Tab];
			PendingMerge.m_vEntries = std::move(vEntries);
			m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_MERGING;
			m_apAssetLoadJobs[Tab].reset();
		}

		if(m_aAssetLoadStates[Tab] == ASSET_LOAD_STATE_MERGING)
		{
			SSettingsAssetPendingMerge &PendingMerge = m_aAssetPendingMerges[Tab];
			if(PendingMerge.m_Tab != Tab || PendingMerge.m_Generation != m_aAssetLoadGenerations[Tab])
			{
				PendingMerge = {};
				m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_UNLOADED;
				gs_aAssetWarmupReady[Tab] = false;
				gs_NextAssetWarmupTab = Tab;
				return false;
			}
			SSettingsResourceMergeBudget MergeBudget;
			MergeBudget.m_MaxListEntries = 64;
			std::vector<SSettingsAssetMergeEntry> vMergeBatch;
			vMergeBatch.reserve(MergeBudget.m_MaxListEntries);
			const size_t MergeStartCursor = PendingMerge.m_Cursor;
			while(PendingMerge.m_Cursor < PendingMerge.m_vEntries.size() && SettingsResourceConsumeMergeEntry(MergeBudget, SettingsFrameBudget()))
				vMergeBatch.push_back(PendingMerge.m_vEntries[PendingMerge.m_Cursor++]);
			if(!vMergeBatch.empty())
				PublishSettingsAssetMergeEntries(Tab, vMergeBatch);
			const int MergedEntries = (int)(PendingMerge.m_Cursor - MergeStartCursor);
			const int RemainingEntries = (int)(PendingMerge.m_vEntries.size() - PendingMerge.m_Cursor);
			const ESettingsWarmupMissReason MergeReason = PendingMerge.m_Cursor < PendingMerge.m_vEntries.size() ? ESettingsWarmupMissReason::JOB_RESULT_PENDING : ESettingsWarmupMissReason::NONE;

			if(PendingMerge.m_Cursor >= PendingMerge.m_vEntries.size())
			{
				const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(Tab);
				switch(Tab)
				{
				case ASSETS_TAB_ENTITIES:
					dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
					break;
				case ASSETS_TAB_GAME:
					dbg_assert(pCurrentCategory != nullptr, "game category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
					break;
				case ASSETS_TAB_EMOTICONS:
					dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
					break;
				case ASSETS_TAB_PARTICLES:
					dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
					break;
				case ASSETS_TAB_HUD:
					dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
					break;
				case ASSETS_TAB_GUI_CURSOR:
					dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
					break;
				case ASSETS_TAB_ARROW:
					dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
					break;
				case ASSETS_TAB_STRONG_WEAK:
					dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
					break;
				case ASSETS_TAB_ENTITY_BG:
				{
					const int SavedTab = s_CurCustomTab;
					s_CurCustomTab = Tab;
					RefreshEntityBgHierarchyView();
					s_CurCustomTab = SavedTab;
					break;
				}
				case ASSETS_TAB_EXTRAS:
					dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
					EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
					break;
				default:
					break;
				}
				PendingMerge = {};
				m_aAssetLoadStates[Tab] = ASSET_LOAD_STATE_LOADED;
				gs_aInitCustomList[Tab] = true;
			}
			LogSettingsResourcePerf(SETTINGS_ASSETS, "merge", MergedEntries, 64, RemainingEntries, MergeReason, 0.0);
		}

		gs_aAssetWarmupReady[Tab] = m_aAssetLoadStates[Tab] == ASSET_LOAD_STATE_LOADED;
		return false;
	}

	return SettingsAssetWarmupAllTabsReady(gs_aAssetWarmupReady, NUMBER_OF_ASSETS_TABS);
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, FS_LISTDIR_CALLBACK pfnCallback, IStorage *pStorage, TCaller Caller)
{
	if(vAssetList.empty())
	{
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	int ListSize = vAssetList.size();
	const char *pFilter = s_aFilterInputs[s_CurCustomTab].GetString();
	const SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab);
	for(int i = 0; i < ListSize; ++i)
	{
		TName *pAsset = &vAssetList[i];

		if constexpr(std::is_same_v<TName, CMenus::SCustomEntityBg>)
		{
			if(pAsset->m_IsDirectory)
			{
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aDisplayName, pFilter))
					continue;
				vpSearchList.push_back(pAsset);
				continue;
			}
		}

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty())
		{
			(void)pWorkshopState;
			const char *pAuthor = pAsset->m_aAuthor;
			const char *pDisplayName = pAsset->m_aDisplayName[0] != '\0' ? pAsset->m_aDisplayName : pAsset->m_aName;
			if(!SearchFilterMatches(pFilter, pDisplayName, pAuthor) && !SearchFilterMatches(pFilter, pAsset->m_aName, pAuthor))
				continue;
		}

		vpSearchList.push_back(pAsset);
	}
	return vAssetList.size();
}

void CMenus::RenderSettingsCustom(CUIRect MainView)
{
	const SSettingsContentMetrics ContentMetrics = ResolveSettingsContentMetrics(MainView.w);
	s_CurCustomTab = std::clamp(s_CurCustomTab, (int)ASSETS_TAB_ENTITIES, NUMBER_OF_ASSETS_TABS - 1);

	if(m_AssetsEditorState.m_Open)
	{
		RenderAssetsEditorScreen(MainView);
		return;
	}
	CUIRect TabBar, CustomList, QuickSearch, ReloadButton, WorkshopHudView;
	const SSettingsSubTabLayoutFrame AssetsSubTabs = ResolveSettingsSubTabLayout(MainView, ContentMetrics.m_UiScale);
	TabBar = AssetsSubTabs.m_TabBarRect;
	MainView = AssetsSubTabs.m_ContentRect;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, ContentMetrics.m_UiScale);
	MainView = Page.m_UnreservedScrollViewport;

	const IUiContext AssetsSearchCtx = SettingsUiContext("settings_assets_search", ContentMetrics.m_UiScale);
	static bool s_AssetsTransitionInitialized = false;
	static int s_PrevAssetsTab = ASSETS_TAB_ENTITIES;
	static int s_AssetsTabSwitchFirstFrame = 0;
	static int s_AssetsTabSwitchCooldownFrames = 0;
	static bool s_AssetsResetListScrollOnTabSwitch = false;
	static float s_AssetsTransitionDirection = 0.0f;
	const uint64_t AssetsTabSwitchNode = UiAnimNodeKey("settings_assets_tab_switch");
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto AssetsUiBudgetStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	gs_SettingsAssetsCardMetadataCacheHits = 0;
	gs_SettingsAssetsCardMetadataCacheMisses = 0;
	gs_SettingsAssetsCardMetadataCacheEvictions = 0;
	auto LogAssetsFramePerfStage = [&](const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr) {
		LogAssetsPerfStageForClient(Client(), pStage, DurationMs, Force, pExtra);
	};

	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	static const char *s_apAssetsTabNames[NUMBER_OF_ASSETS_TABS] = {};
	static char s_aAssetsLanguageFile[IO_MAX_PATH_LENGTH] = {};
	static bool s_AssetsTabNamesInitialized = false;
	if(!s_AssetsTabNamesInitialized || str_comp(s_aAssetsLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		s_AssetsTabNamesInitialized = true;
		str_copy(s_aAssetsLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aAssetsLanguageFile));
		s_apAssetsTabNames[ASSETS_TAB_ENTITIES] = Localize("Entities");
		s_apAssetsTabNames[ASSETS_TAB_GAME] = Localize("Game");
		s_apAssetsTabNames[ASSETS_TAB_EMOTICONS] = Localize("Emoticons");
		s_apAssetsTabNames[ASSETS_TAB_PARTICLES] = Localize("Particles");
		s_apAssetsTabNames[ASSETS_TAB_HUD] = Localize("HUD");
		s_apAssetsTabNames[ASSETS_TAB_GUI_CURSOR] = Localize("Mouse");
		s_apAssetsTabNames[ASSETS_TAB_ARROW] = Localize("Direction Keys");
		s_apAssetsTabNames[ASSETS_TAB_STRONG_WEAK] = Localize("Strong Weak Hook");
		s_apAssetsTabNames[ASSETS_TAB_ENTITY_BG] = Localize("Entity Background Image");
		s_apAssetsTabNames[ASSETS_TAB_EXTRAS] = Localize("Other");
	}

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurCustomTab = Tab;
		}
	}

	if(!s_AssetsTransitionInitialized)
	{
		s_PrevAssetsTab = s_CurCustomTab;
		s_AssetsTransitionInitialized = true;
	}
	else if(s_CurCustomTab != s_PrevAssetsTab)
	{
		s_AssetsTransitionDirection = s_CurCustomTab > s_PrevAssetsTab ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(AssetsTabSwitchNode, 0.18f);
		s_PrevAssetsTab = s_CurCustomTab;
		s_AssetsTabSwitchFirstFrame = 1;
		s_AssetsTabSwitchCooldownFrames = AssetsTabSwitchCooldownFrames;
		s_AssetsResetListScrollOnTabSwitch = true;
		char aAssetsPerfTab[16];
		str_format(aAssetsPerfTab, sizeof(aAssetsPerfTab), "%d", s_CurCustomTab);
		StartSettingsPerfFixedWindow("settings_assets_tab_switch", SettingsPerfContextName(), CurrentQmUiPerfPage(), aAssetsPerfTab, 30);
	}

	ReadUiSwitchAnimation(AssetsTabSwitchNode);

	// ============================================================================
	// ASYNC ASSET LIST LOADING
	// ============================================================================
	// Start async loading if list is empty and not already loading
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_UNLOADED)
	{
		for(const SAssetResourceCategory &Category : GetAssetResourceCategories())
		{
			if(Category.m_Kind == EAssetResourceKind::NAMED_SINGLE_FILE)
			{
				TryImportLegacySingleFileAsset(Storage(), Category);
				MigrateReservedNamedSingleFileAssets(Storage(), Category);
			}
		}

		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
		GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);

		const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		switch(s_CurCustomTab)
		{
		case ASSETS_TAB_ENTITIES:
			dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
			break;
		case ASSETS_TAB_GAME:
			dbg_assert(pCurrentCategory != nullptr, "game category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
			break;
		case ASSETS_TAB_EMOTICONS:
			dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
			break;
		case ASSETS_TAB_PARTICLES:
			dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
			break;
		case ASSETS_TAB_HUD:
			dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
			break;
		case ASSETS_TAB_GUI_CURSOR:
			dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
			break;
		case ASSETS_TAB_ARROW:
			dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
			break;
		case ASSETS_TAB_STRONG_WEAK:
			dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
			break;
		case ASSETS_TAB_ENTITY_BG:
			dbg_assert(pCurrentCategory != nullptr, "entity bg category must exist");
			RefreshEntityBgHierarchyView();
			break;
		case ASSETS_TAB_EXTRAS:
			dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
			EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
			break;
		}

		// Start async loading job
		auto pJob = std::make_shared<CAssetListLoadJob>(
			static_cast<CAssetListLoadJob::EAssetType>(s_CurCustomTab), Storage());
		pJob->SetGeneration(m_aAssetLoadGenerations[s_CurCustomTab]);
		m_apAssetLoadJobs[s_CurCustomTab] = pJob;
		Engine()->AddJob(m_apAssetLoadJobs[s_CurCustomTab]);
		m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_LOADING;
		LogSettingsResourcePerf(SETTINGS_ASSETS, "queued", 1, 1, 0, ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING, 0.0);
	}

	// Check if async loading completed
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING &&
		m_apAssetLoadJobs[s_CurCustomTab] &&
		std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[s_CurCustomTab])->IsCompleted())
	{
		CPerfTimer CompletedTimer;
		auto pJob = std::static_pointer_cast<CAssetListLoadJob>(m_apAssetLoadJobs[s_CurCustomTab]);
		if(!SettingsAssetListJobGenerationMatches(pJob->Generation(), m_aAssetLoadGenerations[s_CurCustomTab]))
		{
			m_apAssetLoadJobs[s_CurCustomTab].reset();
			m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_UNLOADED;
			gs_aAssetWarmupReady[s_CurCustomTab] = false;
			gs_NextAssetWarmupTab = s_CurCustomTab;
			return;
		}
		std::vector<CAssetListLoadJob::SAssetEntry> vEntries = pJob->TakeEntries();
		{
			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "tab=%d entries=%d", s_CurCustomTab, (int)vEntries.size());
			LogAssetsFramePerfStage("assets_load_job_complete", CompletedTimer.ElapsedMs(), true, aExtra);
			LogSettingsResourcePerf(SETTINGS_ASSETS, "complete", (int)vEntries.size(), (int)vEntries.size(), 0, ESettingsWarmupMissReason::NONE, CompletedTimer.ElapsedMs());
		}

		// The list merge below can reallocate backing storage. Drop queued preview pointers
		// for the current tab before mutating the vectors.
		++m_aCustomPreviewEpoch[s_CurCustomTab];
		m_aaCustomPreviewDecodeQueue[s_CurCustomTab].clear();
		m_aaCustomPreviewReadyQueue[s_CurCustomTab].clear();
		m_aaCustomPreviewReadyQueued[s_CurCustomTab].clear();
		SSettingsAssetPendingMerge &PendingMerge = m_aAssetPendingMerges[s_CurCustomTab];
		PendingMerge = {};
		PendingMerge.m_Tab = s_CurCustomTab;
		PendingMerge.m_Generation = ++m_aAssetLoadGenerations[s_CurCustomTab];
		PendingMerge.m_vEntries = std::move(vEntries);
		m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_MERGING;
		m_apAssetLoadJobs[s_CurCustomTab].reset();
	}

	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_MERGING)
	{
		SSettingsAssetPendingMerge &PendingMerge = m_aAssetPendingMerges[s_CurCustomTab];
		if(PendingMerge.m_Tab != s_CurCustomTab || PendingMerge.m_Generation != m_aAssetLoadGenerations[s_CurCustomTab])
		{
			PendingMerge = {};
			m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_UNLOADED;
			gs_aAssetWarmupReady[s_CurCustomTab] = false;
			gs_NextAssetWarmupTab = s_CurCustomTab;
			return;
		}
		CPerfTimer MergeTimer;
		SSettingsResourceMergeBudget MergeBudget;
		MergeBudget.m_MaxListEntries = 64;
		std::vector<SSettingsAssetMergeEntry> vMergeBatch;
		vMergeBatch.reserve(MergeBudget.m_MaxListEntries);
		const size_t MergeStartCursor = PendingMerge.m_Cursor;
		while(PendingMerge.m_Cursor < PendingMerge.m_vEntries.size() && SettingsResourceConsumeMergeEntry(MergeBudget, SettingsFrameBudget()))
			vMergeBatch.push_back(PendingMerge.m_vEntries[PendingMerge.m_Cursor++]);
		if(!vMergeBatch.empty())
			PublishSettingsAssetMergeEntries(s_CurCustomTab, vMergeBatch);

		if(PendingMerge.m_Cursor < PendingMerge.m_vEntries.size())
		{
			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "tab=%d merged=%d remaining=%d reason=%s",
				s_CurCustomTab, (int)(PendingMerge.m_Cursor - MergeStartCursor), (int)(PendingMerge.m_vEntries.size() - PendingMerge.m_Cursor),
				SettingsWarmupMissReasonName(ESettingsWarmupMissReason::JOB_RESULT_PENDING));
			LogAssetsFramePerfStage("assets_merge_budget", MergeTimer.ElapsedMs(), true, aExtra);
			LogSettingsResourcePerf(SETTINGS_ASSETS, "merge", (int)(PendingMerge.m_Cursor - MergeStartCursor), 64, (int)(PendingMerge.m_vEntries.size() - PendingMerge.m_Cursor), ESettingsWarmupMissReason::JOB_RESULT_PENDING, MergeTimer.ElapsedMs());
		}
		else
		{
			const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);
			switch(s_CurCustomTab)
			{
			case ASSETS_TAB_ENTITIES:
				dbg_assert(pCurrentCategory != nullptr, "entities category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEntitiesList);
				break;
			case ASSETS_TAB_GAME:
				dbg_assert(pCurrentCategory != nullptr, "game category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGameList);
				break;
			case ASSETS_TAB_EMOTICONS:
				dbg_assert(pCurrentCategory != nullptr, "emoticons category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vEmoticonList);
				break;
			case ASSETS_TAB_PARTICLES:
				dbg_assert(pCurrentCategory != nullptr, "particles category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vParticlesList);
				break;
			case ASSETS_TAB_HUD:
				dbg_assert(pCurrentCategory != nullptr, "hud category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vHudList);
				break;
			case ASSETS_TAB_GUI_CURSOR:
				dbg_assert(pCurrentCategory != nullptr, "gui cursor category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vGuiCursorList);
				break;
			case ASSETS_TAB_ARROW:
				dbg_assert(pCurrentCategory != nullptr, "arrow category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vArrowList);
				break;
			case ASSETS_TAB_STRONG_WEAK:
				dbg_assert(pCurrentCategory != nullptr, "strong weak category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vStrongWeakList);
				break;
			case ASSETS_TAB_ENTITY_BG:
				RefreshEntityBgHierarchyView();
				break;
			case ASSETS_TAB_EXTRAS:
				dbg_assert(pCurrentCategory != nullptr, "extras category must exist");
				EnsureDefaultAssetVisible(*pCurrentCategory, m_vExtrasList);
				break;
			default:
				break;
			}

			char aExtra[128];
			str_format(aExtra, sizeof(aExtra), "tab=%d merged=%d", s_CurCustomTab, (int)(PendingMerge.m_Cursor - MergeStartCursor));
			LogAssetsFramePerfStage("assets_merge_results", MergeTimer.ElapsedMs(), true, aExtra);
			LogSettingsResourcePerf(SETTINGS_ASSETS, "merge", (int)(PendingMerge.m_Cursor - MergeStartCursor), 64, 0, ESettingsWarmupMissReason::NONE, MergeTimer.ElapsedMs());
			PendingMerge = {};
			m_aAssetLoadStates[s_CurCustomTab] = ASSET_LOAD_STATE_LOADED;
			gs_aAssetWarmupReady[s_CurCustomTab] = true;
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
	}

	// Mark for search list rebuild if size changed
	switch(s_CurCustomTab)
	{
	case ASSETS_TAB_ENTITIES:
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_GAME:
		if(m_vGameList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_EMOTICONS:
		if(m_vEmoticonList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_PARTICLES:
		if(m_vParticlesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_HUD:
		if(m_vHudList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_GUI_CURSOR:
		if(m_vGuiCursorList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_ARROW:
		if(m_vArrowList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_STRONG_WEAK:
		if(m_vStrongWeakList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_ENTITY_BG:
		if(m_vEntityBgList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	case ASSETS_TAB_EXTRAS:
		if(m_vExtrasList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
		break;
	default:
		dbg_assert_failed("Invalid s_CurCustomTab: %d", s_CurCustomTab);
		break;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	const SAssetResourceCategory *pCurrentCategory = AssetResourceCategoryByTab(s_CurCustomTab);
	const bool SupportsWorkshopSync = pCurrentCategory != nullptr && pCurrentCategory->m_WorkshopEnabled;
	const bool SupportsAssetsEditor = s_CurCustomTab == ASSETS_TAB_ENTITIES || s_CurCustomTab == ASSETS_TAB_GAME ||
					  s_CurCustomTab == ASSETS_TAB_EMOTICONS || s_CurCustomTab == ASSETS_TAB_PARTICLES ||
					  s_CurCustomTab == ASSETS_TAB_HUD || s_CurCustomTab == ASSETS_TAB_GUI_CURSOR ||
					  s_CurCustomTab == ASSETS_TAB_ARROW || s_CurCustomTab == ASSETS_TAB_STRONG_WEAK ||
					  s_CurCustomTab == ASSETS_TAB_EXTRAS;
	const bool ShowEntityPreviewToggle = s_CurCustomTab == ASSETS_TAB_ENTITIES;
	auto ComputeToolbarButtonWidth = [&](const char *pLabel) {
		constexpr float MinButtonWidth = 90.0f;
		constexpr float HorizontalPadding = 18.0f;
		return maximum(MinButtonWidth, TextRender()->TextWidth(10.0f, Localize(pLabel), -1, -1.0f) + HorizontalPadding);
	};
	const float AssetsDirButtonWidth = ComputeToolbarButtonWidth("Assets directory");
	const float AssetsEditorButtonWidth = ComputeToolbarButtonWidth("Assets editor");
	const float EntityPreviewButtonWidth = ComputeToolbarButtonWidth("Entity Preview");
	const float ShowWorkshopAssetsButtonWidth = ComputeToolbarButtonWidth("Show Workshop Assets");
	const float WorkshopSyncButtonWidth = ComputeToolbarButtonWidth("Sync Workshop Assets");
	constexpr float ToolbarGap = 10.0f;
	constexpr float ToolbarRowGap = 5.0f;
	auto ForEachToolbarButtonWidth = [&](auto &&Callback) {
		if(ShowEntityPreviewToggle)
			Callback(EntityPreviewButtonWidth);
		Callback(AssetsDirButtonWidth);
		if(SupportsAssetsEditor)
			Callback(AssetsEditorButtonWidth);
		if(SupportsWorkshopSync)
		{
			Callback(WorkshopSyncButtonWidth);
			Callback(ShowWorkshopAssetsButtonWidth);
		}
		Callback(25.0f);
	};
	auto ComputeToolbarWidth = [&]() {
		float Width = 0.0f;
		ForEachToolbarButtonWidth([&](float ButtonWidth) {
			if(Width > 0.0f)
				Width += ToolbarGap;
			Width += ButtonWidth;
		});
		return Width;
	};
	const float ToolbarWidth = ComputeToolbarWidth();
	auto ComputeToolbarRowCount = [&](float AvailableWidth) {
		int RowCount = 1;
		float UsedWidth = 0.0f;
		ForEachToolbarButtonWidth([&](float ButtonWidth) {
			const float RequiredWidth = (UsedWidth > 0.0f ? ToolbarGap : 0.0f) + ButtonWidth;
			if(UsedWidth > 0.0f && UsedWidth + RequiredWidth > AvailableWidth)
			{
				++RowCount;
				UsedWidth = ButtonWidth;
			}
			else
				UsedWidth += RequiredWidth;
		});
		return RowCount;
	};
	constexpr float AssetsSearchWidth = 220.0f;
	const bool ToolbarUsesOwnRows = MainView.w < AssetsSearchWidth + ToolbarGap + ToolbarWidth;
	const int ToolbarRowCount = ToolbarUsesOwnRows ? ComputeToolbarRowCount(MainView.w) : 1;
	const float FooterHeight = ToolbarUsesOwnRows ? ms_ButtonHeight * (1.0f + ToolbarRowCount) + ToolbarRowGap * ToolbarRowCount : ms_ButtonHeight;

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - FooterHeight, &CustomList, &MainView);
	if(UsesCombinedAssetList(AssetResourceCategoryByTab(s_CurCustomTab)))
	{
		WorkshopHudView = CustomList;
	}

	// Show loading indicator while async loading is in progress
	if(m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING && !UsesCombinedAssetList(AssetResourceCategoryByTab(s_CurCustomTab)))
	{
		int VisibleEntries = 0;
		switch(s_CurCustomTab)
		{
		case ASSETS_TAB_ENTITIES: VisibleEntries = (int)m_vEntitiesList.size(); break;
		case ASSETS_TAB_GAME: VisibleEntries = (int)m_vGameList.size(); break;
		case ASSETS_TAB_EMOTICONS: VisibleEntries = (int)m_vEmoticonList.size(); break;
		case ASSETS_TAB_PARTICLES: VisibleEntries = (int)m_vParticlesList.size(); break;
		case ASSETS_TAB_HUD: VisibleEntries = (int)m_vHudList.size(); break;
		case ASSETS_TAB_GUI_CURSOR: VisibleEntries = (int)m_vGuiCursorList.size(); break;
		case ASSETS_TAB_ARROW: VisibleEntries = (int)m_vArrowList.size(); break;
		case ASSETS_TAB_STRONG_WEAK: VisibleEntries = (int)m_vStrongWeakList.size(); break;
		case ASSETS_TAB_ENTITY_BG: VisibleEntries = (int)m_vEntityBgList.size(); break;
		case ASSETS_TAB_EXTRAS: VisibleEntries = (int)m_vExtrasList.size(); break;
		}

		if(SettingsAssetListShouldShowBlockingLoading(true, VisibleEntries))
		{
			// Draw loading spinner in the center of the list area
			const float SpinnerSize = 40.0f;
			CUIRect SpinnerRect;
			SpinnerRect.w = SpinnerSize;
			SpinnerRect.h = SpinnerSize;
			SpinnerRect.x = CustomList.x + (CustomList.w - SpinnerSize) / 2.0f;
			SpinnerRect.y = CustomList.y + (CustomList.h - SpinnerSize) / 2.0f - 20.0f;

			// Use a rotating icon as spinner
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			// Calculate rotation angle based on time
			const float Time = Client()->LocalTime();
			const float Rotation = Time * 360.0f * 2.0f; // 2 rotations per second

			// Render spinner with rotation
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.8f);

			// Draw multiple segments to simulate rotation
			const int NumSegments = 8;
			const float SegmentAngle = 360.0f / NumSegments;
			for(int i = 0; i < NumSegments; i++)
			{
				float Alpha = 0.1f + 0.9f * ((i + (int)(Rotation / SegmentAngle)) % NumSegments) / (float)(NumSegments - 1);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);

				float Angle = (i * SegmentAngle + Rotation) * (3.14159f / 180.0f);
				float CenterX = SpinnerRect.x + SpinnerRect.w / 2.0f;
				float CenterY = SpinnerRect.y + SpinnerRect.h / 2.0f;
				float Radius = SpinnerRect.w / 3.0f;

				IGraphics::CQuadItem Quad(
					CenterX + cosf(Angle) * Radius - 3.0f,
					CenterY + sinf(Angle) * Radius - 3.0f,
					6.0f, 6.0f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}
			Graphics()->QuadsEnd();

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

			// Loading text
			CUIRect LoadingTextRect;
			CustomList.HSplitTop(SpinnerRect.y - CustomList.y + SpinnerSize + 10.0f, nullptr, &LoadingTextRect);
			LoadingTextRect.h = 20.0f;
			DoSettingsMenuLabel(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, "assets-loading-list", &LoadingTextRect, Localize("Loading assets..."), ContentMetrics.m_BodySize, TEXTALIGN_MC);
		}
	}

	if(gs_aInitCustomList[s_CurCustomTab])
	{
		CPerfTimer SearchTimer;
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			gs_vpSearchEntitiesList.clear();
			ListSize = m_vEntitiesList.size();
			for(int i = 0; i < ListSize; ++i)
			{
				SCustomEntities *pEntity = &m_vEntitiesList[i];

				// filter quick search
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pEntity->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
					continue;

				gs_vpSearchEntitiesList.push_back(pEntity);
			}
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
		{
			ListSize = InitSearchList(gs_vpSearchGuiCursorList, m_vGuiCursorList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ListSize = InitSearchList(gs_vpSearchArrowList, m_vArrowList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
		{
			ListSize = InitSearchList(gs_vpSearchStrongWeakList, m_vStrongWeakList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
		{
			ListSize = InitSearchList(gs_vpSearchEntityBgList, m_vEntityBgList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
		char aExtra[160];
		str_format(aExtra, sizeof(aExtra), "tab=%d list_size=%d filter_len=%d",
			s_CurCustomTab, ListSize, (int)str_length(s_aFilterInputs[s_CurCustomTab].GetString()));
		LogAssetsFramePerfStage("assets_search_rebuild", SearchTimer.ElapsedMs(), true, aExtra);
	}

	int OldSelected = -1;
	float Margin = 10;
	constexpr float AssetCardRadius = 10.0f;
	float TextureWidth = 128;
	float TextureHeight = 128;
	SMenuAssetScanUser LazyLoadUser;
	LazyLoadUser.m_pUser = this;
	constexpr size_t MaxPreviewUploadBytesPerFrame = ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME;
	constexpr int MaxPreviewDecodeFinalizesPerFrame = 16;
	constexpr double MaxPreviewDecodeFinalizeMsPerFrame = 2.0;
	constexpr int PreviewPrefetchRows = 2;
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	static bool s_AssetsWindowActive = true;
	static int s_AssetsFocusPerfFrames = 0;
	int UploadedPreviewsThisFrame = 0;
	int ResizedPreviewsThisFrame = 0;
	int PreviewDecodeStartsThisFrame = 0;
	int PreviewDecodeFinalizesThisFrame = 0;
	int WorkshopThumbStartsThisFrame = 0;
	SSettingsAssetsVisibleAdmission CombinedVisibleAdmission;
	SSettingsAssetsVisiblePreflight *pActiveVisiblePreflight = nullptr;
	int VisiblePreviewStartsDuringDraw = 0;
	size_t SearchListSize = 0;
	auto &vDecodeQueue = m_aaCustomPreviewDecodeQueue[s_CurCustomTab];
	auto &vReadyQueue = m_aaCustomPreviewReadyQueue[s_CurCustomTab];
	auto &vReadyQueued = m_aaCustomPreviewReadyQueued[s_CurCustomTab];
	const unsigned PreviewEpoch = m_aCustomPreviewEpoch[s_CurCustomTab];
	static bool s_AssetsScrollActiveLastFrame = false;
	static int s_AssetsScrollCooldownFrames = 0;
	static int s_AssetsPostScrollRecoveryFrames = 0;
	static int s_AssetsScrollUploadCooldownFrames = 0;
	static float s_aAssetsLastLocalScrollOffsetY[NUMBER_OF_ASSETS_TABS] = {};
	static float s_aAssetsLastWorkshopScrollOffsetY[NUMBER_OF_ASSETS_TABS] = {};
	static int s_AssetsLastFirstVisibleIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_AssetsLastLastVisibleIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_AssetsLastFirstVisibleCombinedIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_AssetsLastLastVisibleCombinedIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_AssetsLastFirstVisibleDownloadableIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_AssetsLastLastVisibleDownloadableIndex[NUMBER_OF_ASSETS_TABS] = {-1};
	static int s_aAssetsLastGeometryColumns[NUMBER_OF_ASSETS_TABS] = {};
	static float s_aAssetsLastGeometryRowHeight[NUMBER_OF_ASSETS_TABS] = {};
	const bool AssetsScrollInteraction = m_SettingsScrollActive || s_AssetsScrollActiveLastFrame;
	const int PreviousAssetsScrollCooldownFrames = s_AssetsScrollCooldownFrames;
	s_AssetsScrollCooldownFrames = SettingsScrollInteractionCooldown(AssetsScrollInteraction, s_AssetsScrollCooldownFrames, 3);
	s_AssetsPostScrollRecoveryFrames = SettingsScrollInteractionRecovery(
		AssetsScrollInteraction, PreviousAssetsScrollCooldownFrames, s_AssetsScrollCooldownFrames, s_AssetsPostScrollRecoveryFrames, 2);
	const SSettingsResourceFrameContext ResourceFrameContext = SettingsBuildFrameContext(
		s_AssetsScrollCooldownFrames > 0, m_SettingsScrollActive, false, s_AssetsPostScrollRecoveryFrames);
	int RemainingHeavyResourceBatches = SettingsResourceSharedHeavyBudget(ResourceFrameContext, 4, 1);
	const SQmPerformanceMetrics &PerfSnapshot = GameClient()->m_QmMonitoring.Snapshot().m_Performance;
	const bool AssetsPageSwitchActive = m_SettingsPageSwitchActive;
	const bool AssetsTabSwitchFirstFrame = s_AssetsTabSwitchFirstFrame > 0;
	const bool AssetsTabSwitchCooldownActive = s_AssetsTabSwitchCooldownFrames > 0;
	const bool AssetsShellOnlyFrame = AssetsTabSwitchFirstFrame || AssetsPageSwitchActive;
	if(AssetsTabSwitchFirstFrame)
	{
		char aShellFirstExtra[160];
		str_format(aShellFirstExtra, sizeof(aShellFirstExtra), "operation=settings_assets_tab_switch page=settings:assets tab=%s frame=%" PRIu64 " tab_switch_shell_only=1", AssetsSettingsTabName(s_CurCustomTab), Client()->PerfFrame());
		LogAssetsFramePerfStage("assets_tab_switch_shell_first", 0.0, true, aShellFirstExtra);
	}
	SSettingsAdaptiveBudgetInput AdaptiveBudgetInput;
	AdaptiveBudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(AdaptiveBudgetInput.m_aOperation, SettingsPerfActiveOperation(), sizeof(AdaptiveBudgetInput.m_aOperation));
	str_copy(AdaptiveBudgetInput.m_aPage, "settings:assets", sizeof(AdaptiveBudgetInput.m_aPage));
	str_copy(AdaptiveBudgetInput.m_aTab, AssetsSettingsTabName(s_CurCustomTab), sizeof(AdaptiveBudgetInput.m_aTab));
	str_copy(AdaptiveBudgetInput.m_aContext, SettingsPerfContextName(), sizeof(AdaptiveBudgetInput.m_aContext));
	AdaptiveBudgetInput.m_FrameMsAverage = PerfSnapshot.m_FrameTimeMs;
	AdaptiveBudgetInput.m_FrameMsP95 = PerfSnapshot.m_FrameTimeP95Ms > 0.0f ? PerfSnapshot.m_FrameTimeP95Ms : PerfSnapshot.m_FrameTimeMs;
	AdaptiveBudgetInput.m_TargetFrameMs = 8.333f;
	AdaptiveBudgetInput.m_ScrollActive = ResourceFrameContext.m_ScrollActive;
	AdaptiveBudgetInput.m_JumpScrollActive = ResourceFrameContext.m_JumpScrollActive;
	AdaptiveBudgetInput.m_TabSwitchFirstFrame = AssetsTabSwitchFirstFrame || AssetsPageSwitchActive;
	AdaptiveBudgetInput.m_PostScrollRecoveryFrames = ResourceFrameContext.m_PostScrollRecoveryFrames;
	AdaptiveBudgetInput.m_BackgroundBacklog = (int)vDecodeQueue.size() + (int)vReadyQueue.size();
	AdaptiveBudgetInput.m_WindowActive = WindowActive;
	const SSettingsAdaptiveBudgetOutput AdaptiveBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::Assets, "assets", AdaptiveBudgetInput);
	(void)CurrentSettingsUiFrameBudget();
	const bool AssetsScrollPressure = ResourceFrameContext.m_ScrollActive || ResourceFrameContext.m_JumpScrollActive;
	const bool AssetsContentWarmupBlocked = AssetsShellOnlyFrame || AssetsScrollPressure;
	const bool AssetsRenderCardMetadataFallback = !AssetsShellOnlyFrame;
	constexpr int AssetsScrollUploadCooldownFrames = 6;
	const int AdaptivePrefetchTokens = AssetsScrollPressure ? 0 : AdaptiveBudget.m_PrefetchTokens;
	const int AdaptiveBackgroundTokens = AssetsScrollPressure ? 0 : AdaptiveBudget.m_BackgroundTokens;
	const int AdaptiveMetadataLayoutTokens = AdaptiveBudget.m_MetadataLayoutTokens;
	const int AdaptivePreviewArtifactTokens = AdaptiveBudget.m_PreviewArtifactTokens;
	const int AdaptiveTextureUploadTokens = AdaptiveBudget.m_TextureUploadTokens;
	const int AssetsInitialMetadataLayoutTokens = maximum(1, minimum(AdaptiveBudget.m_VisibleTokens, 4));
	const int AssetsMetadataLayoutTokensThisFrame = AssetsShellOnlyFrame ? AssetsInitialMetadataLayoutTokens : (AssetsScrollPressure ? AssetsInitialMetadataLayoutTokens : AdaptiveMetadataLayoutTokens);
	const int AssetsPreviewArtifactTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptivePreviewArtifactTokens;
	const int AssetsTextureUploadTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptiveTextureUploadTokens;
	const int MaxPreviewDecodeStartsPerFrame = AssetsContentWarmupBlocked ? 0 : maximum(1, AdaptiveBudget.m_VisibleTokens + AdaptivePrefetchTokens + AdaptiveBackgroundTokens);
	const int MaxPreviewHighPriorityDecodeStartsPerFrame = AssetsContentWarmupBlocked ? 0 : maximum(MaxPreviewDecodeStartsPerFrame, AdaptiveBudget.m_VisibleTokens + AdaptivePrefetchTokens);
	const int MaxWorkshopThumbStartsPerFrameAdaptive = AssetsContentWarmupBlocked ? 0 : maximum(1, AdaptiveBudget.m_VisibleTokens + AdaptivePrefetchTokens + AdaptiveBackgroundTokens);
	const int MaxWorkshopThumbHighPriorityStartsPerFrame = AssetsContentWarmupBlocked ? 0 : maximum(MaxWorkshopThumbStartsPerFrameAdaptive, AdaptiveBudget.m_VisibleTokens);
	const int MaxWorkshopThumbJumpStartsPerFrame = maximum(1, minimum(MaxWorkshopThumbStartsPerFrameAdaptive, AdaptiveBudget.m_VisibleTokens));
	constexpr int MaxAssetsTabSwitchVisibleThumbStartsFirstFrame = 2;
	constexpr int MaxAssetsTabSwitchVisibleThumbStartsPerFrame = 2;
	const int VisibleThumbStartLimitThisFrame = AssetsContentWarmupBlocked ? 0 : (AssetsTabSwitchFirstFrame ? MaxAssetsTabSwitchVisibleThumbStartsFirstFrame : (AssetsTabSwitchCooldownActive ? MaxAssetsTabSwitchVisibleThumbStartsPerFrame : MaxWorkshopThumbStartsPerFrameAdaptive));
	auto DecayAssetsScrollUploadCooldown = [&]() {
		if(s_AssetsScrollUploadCooldownFrames > 0)
			--s_AssetsScrollUploadCooldownFrames;
	};
	auto RefreshAssetsScrollUploadCooldown = [&](bool ScrollActive, bool ScrollOffsetChanged, bool ScrollOffsetJump) {
		if(ScrollActive || ScrollOffsetChanged || ScrollOffsetJump)
			s_AssetsScrollUploadCooldownFrames = AssetsScrollUploadCooldownFrames;
	};
	auto AssetsUploadBlockFrameContextName = [&](const SSettingsResourceFrameContext &FrameContext) {
		if(s_AssetsScrollUploadCooldownFrames > 0)
			return "scroll_cooldown";
		return AssetsResourceFrameContextName(FrameContext);
	};
	DecayAssetsScrollUploadCooldown();
	bool AssetsDirectScrollUploadBlocked = s_AssetsScrollUploadCooldownFrames > 0;
	bool AssetsUploadBlocked = AssetsContentWarmupBlocked || AssetsDirectScrollUploadBlocked;
	const char *pAssetsUploadBlockFrameContext = AssetsDirectScrollUploadBlocked ? "scroll_cooldown" : (AssetsShellOnlyFrame ? "shell_only" : (AssetsScrollPressure ? "scroll_pressure" : "active"));
	int MaxPreviewUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;
	auto RefreshAssetsUploadBudget = [&]() {
		AssetsDirectScrollUploadBlocked = s_AssetsScrollUploadCooldownFrames > 0;
		AssetsUploadBlocked = AssetsContentWarmupBlocked || AssetsDirectScrollUploadBlocked;
		pAssetsUploadBlockFrameContext = AssetsDirectScrollUploadBlocked ? "scroll_cooldown" : (AssetsShellOnlyFrame ? "shell_only" : (AssetsScrollPressure ? "scroll_pressure" : "active"));
		MaxPreviewUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;
	};
	auto RefreshAssetsScrollUploadCooldownForOffset = [&](bool ScrollActive, float CurrentScrollOffsetY, float &PreviousScrollOffsetY, float RowHeight, bool JumpScrollActive) {
		const bool ScrollOffsetChanged = absolute(CurrentScrollOffsetY - PreviousScrollOffsetY) > 0.01f;
		const bool ScrollOffsetJump = absolute(CurrentScrollOffsetY - PreviousScrollOffsetY) >= RowHeight;
		PreviousScrollOffsetY = CurrentScrollOffsetY;
		RefreshAssetsScrollUploadCooldown(ScrollActive, ScrollOffsetChanged, ScrollOffsetJump || JumpScrollActive);
		RefreshAssetsUploadBudget();
	};
	RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(MaxPreviewUploadsPerFrame, ResourceFrameContext, 4, 1);
	m_SettingsFrameBudget.m_MaxGpuUploads = maximum(m_SettingsFrameBudget.m_MaxGpuUploads, MaxPreviewUploadsPerFrame);
	const size_t TextureMemoryUsageBytes = Graphics()->TextureMemoryUsage();
	auto CountResidentPreviewBytes = [&]() {
		size_t ResidentBytes = 0;
		for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
		{
			const size_t ItemCount = GetCustomItemCount(Tab);
			for(size_t Index = 0; Index < ItemCount; ++Index)
			{
				const SCustomItem *pItem = GetCustomItem(Tab, Index);
				if(pItem != nullptr && pItem->m_RenderTexture.IsValid())
					ResidentBytes += pItem->m_PreviewResidentBytes;
			}
		}
		return ResidentBytes;
	};
	auto CountWorkshopResidentPreviewBytes = [&]() {
		size_t ResidentBytes = 0;
		for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
		{
			const SWorkshopHudState *pWorkshopState = WorkshopStateByTab(Tab);
			if(pWorkshopState == nullptr)
				continue;
			for(const SWorkshopHudAsset &Asset : pWorkshopState->m_vAssets)
			{
				if(Asset.m_ThumbTexture.IsValid())
					ResidentBytes += Asset.m_ThumbResidentBytes;
			}
		}
		return ResidentBytes;
	};
	const bool ShouldCollectFocusResidentStats = AssetsPerfDebugEnabled() && (WindowActive != s_AssetsWindowActive || s_AssetsFocusPerfFrames > 0);
	const size_t ResidentPreviewBytes = ShouldCollectFocusResidentStats ? CountResidentPreviewBytes() : 0;
	const size_t WorkshopResidentPreviewBytes = ShouldCollectFocusResidentStats ? CountWorkshopResidentPreviewBytes() : 0;
	const size_t PreviewBudgetBytes = SettingsAssetPreviewResidentBudgetBytes(
		(size_t)g_Config.m_QmAssetsPreviewBudgetMbOverride,
		g_Config.m_QmAssetsPreviewBudgetPercent,
		PerfSnapshot.m_GpuDedicatedVramBudgetMb);

	auto LogAssetsFocusState = [&](const char *pEvent) {
		int WorkshopDecodePending = 0;
		int WorkshopReadyPending = 0;
		if(const SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab))
		{
			WorkshopDecodePending = (int)pWorkshopState->m_vDecodeThumbQueue.size();
			WorkshopReadyPending = (int)pWorkshopState->m_vReadyThumbQueue.size();
		}
		char aExtra[512];
		str_format(aExtra, sizeof(aExtra),
			"event=%s window_active=%d gfx_backgroundrender=%d cl_refresh_rate_inactive=%d page=%s tab=%s local_decode_queue=%d local_ready_queue=%d workshop_decode_queue=%d workshop_ready_queue=%d texture_memory_usage=%u resident_preview_bytes=%u workshop_resident_preview_bytes=%u preview_budget_bytes=%u",
			pEvent, WindowActive ? 1 : 0, g_Config.m_GfxBackgroundRender, g_Config.m_ClRefreshRateInactive,
			"assets", AssetsSettingsTabName(s_CurCustomTab), (int)m_aaCustomPreviewDecodeQueue[s_CurCustomTab].size(),
			(int)m_aaCustomPreviewReadyQueue[s_CurCustomTab].size(), WorkshopDecodePending, WorkshopReadyPending,
			(unsigned)TextureMemoryUsageBytes, (unsigned)ResidentPreviewBytes, (unsigned)WorkshopResidentPreviewBytes, (unsigned)PreviewBudgetBytes);
		LogAssetsPerfStageForClient(Client(), "assets_window_focus", 0.0, true, aExtra);
	};
	if(WindowActive != s_AssetsWindowActive)
	{
		LogAssetsFocusState(WindowActive ? "gained" : "lost");
		s_AssetsWindowActive = WindowActive;
		s_AssetsFocusPerfFrames = WindowActive ? 10 : 0;
	}

	auto MakePreviewHandle = [&](const SCustomItem &Item) {
		SSettingsAssetPreviewHandle Handle;
		Handle.m_Tab = s_CurCustomTab;
		Handle.m_Epoch = PreviewEpoch;
		Handle.m_Index = Item.m_PreviewListIndex;
		Handle.m_Name = Item.m_aName;
		return Handle;
	};

	auto FindCustomItemByPreviewHandle = [&](const SSettingsAssetPreviewHandle &Handle) -> SCustomItem * {
		if(Handle.m_Tab < ASSETS_TAB_ENTITIES || Handle.m_Tab >= NUMBER_OF_ASSETS_TABS || Handle.m_Index >= GetCustomItemCount(Handle.m_Tab))
			return nullptr;
		SCustomItem *pItem = GetCustomItemMutable(Handle.m_Tab, Handle.m_Index);
		return pItem != nullptr && SettingsAssetPreviewHandleMatches(Handle, Handle.m_Tab, Handle.m_Epoch, Handle.m_Index, pItem->m_aName) ? pItem : nullptr;
	};

	auto QueueReadyPreview = [&](SCustomItem *pItem) {
		const SSettingsAssetPreviewHandle Handle = MakePreviewHandle(*pItem);
		const std::string Key = SettingsAssetPreviewHandleKey(Handle);
		if(vReadyQueued.insert(Key).second)
		{
			if(pItem->m_PreviewHighPriority)
				vReadyQueue.push_front(Handle);
			else
				vReadyQueue.push_back(Handle);
			char aExtra[160];
			str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s queue_size=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, (int)vReadyQueue.size(), (unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStageForClient(Client(), "assets_preview_upload_queue_push", 0.0, true, aExtra);
		}
		else if(pItem->m_PreviewHighPriority)
		{
			auto It = std::find_if(vReadyQueue.begin(), vReadyQueue.end(), [&](const SSettingsAssetPreviewHandle &QueuedHandle) {
				return SettingsAssetPreviewHandleMatches(QueuedHandle, s_CurCustomTab, PreviewEpoch, pItem->m_PreviewListIndex, pItem->m_aName);
			});
			const SCustomItem *pFrontItem = !vReadyQueue.empty() ? FindCustomItemByPreviewHandle(vReadyQueue.front()) : nullptr;
			if(It != vReadyQueue.end() && !vReadyQueue.empty() &&
				SettingsAssetPreviewShouldUploadHighPriorityFirst(pFrontItem != nullptr && pFrontItem->m_PreviewHighPriority, pItem->m_PreviewHighPriority))
			{
				vReadyQueue.erase(It);
				vReadyQueue.push_front(Handle);
			}
		}
	};

	auto StartPreviewDecode = [&](size_t Index, bool HighPriority) {
		const int CurTab = s_CurCustomTab;
		if(CurTab < ASSETS_TAB_ENTITIES || CurTab >= NUMBER_OF_ASSETS_TABS)
			return false;
		if(!SettingsAssetListCanStartPreviewDecode(
			   m_aAssetLoadStates[CurTab] == ASSET_LOAD_STATE_LOADING,
			   m_aAssetLoadStates[CurTab] == ASSET_LOAD_STATE_MERGING,
			   m_aAssetLoadStates[CurTab] == ASSET_LOAD_STATE_LOADED))
			return false;
		if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, HighPriority))
			return false;
		SCustomItem *pItem = GetCustomItemMutable(CurTab, Index);
		if(pItem == nullptr || !SettingsResourceCanUseHighPriorityBudget(PreviewDecodeStartsThisFrame, MaxPreviewDecodeStartsPerFrame, MaxPreviewHighPriorityDecodeStartsPerFrame, HighPriority))
			return false;
		if(CurTab == ASSETS_TAB_ENTITY_BG && static_cast<SCustomEntityBg *>(pItem)->m_IsDirectory)
			return false;
		if(CurTab == ASSETS_TAB_ENTITY_BG)
		{
			if(SWorkshopHudState *pWorkshopState = WorkshopStateByTab(CurTab))
			{
				if(EnsureInstalledWorkshopEntityBgThumbReady(
					   *pWorkshopState,
					   pItem->m_aName,
					   WindowActive,
					   HighPriority,
					   CurTab,
					   PreviewEpoch,
					   PreviewBudgetBytes,
					   TextureMemoryUsageBytes,
					   WorkshopThumbStartsThisFrame,
					   MaxWorkshopThumbStartsPerFrameAdaptive,
					   MaxWorkshopThumbHighPriorityStartsPerFrame,
					   CombinedVisibleAdmission.m_VisibleStarts,
					   VisibleThumbStartLimitThisFrame,
					   Storage(),
					   Engine(),
					   Http(),
					   Client()))
					return false;
			}
		}
		if(pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADING)
		{
			if(HighPriority)
			{
				pItem->m_PreviewHighPriority = true;
				auto It = std::find_if(vDecodeQueue.begin(), vDecodeQueue.end(), [&](const SSettingsAssetPreviewHandle &QueuedHandle) {
					return SettingsAssetPreviewHandleMatches(QueuedHandle, CurTab, PreviewEpoch, pItem->m_PreviewListIndex, pItem->m_aName);
				});
				if(It != vDecodeQueue.end())
				{
					const SSettingsAssetPreviewHandle Handle = *It;
					vDecodeQueue.erase(It);
					vDecodeQueue.push_front(Handle);
				}
			}
			return false;
		}
		if(pItem->m_RenderTexture.IsValid() && pItem->m_PreviewResidentBytes >= PreviewTextureSizeBytesEstimate(pItem->m_PreviewRequestedTextureSize))
			return false;
		if(pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_READY || pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADED)
			return false;
		pItem->m_PreviewImage.Free();
		pItem->m_PreviewEpoch = PreviewEpoch;
		pItem->m_PreviewListIndex = Index;
		pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_LOADING;
		pItem->m_PreviewBytes = 0;
		pItem->m_PreviewRequestedTextureSize = SettingsAssetPreviewBudgetedTextureSize(
			LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
			ASSET_PREVIEW_MIN_TEXTURE_SIZE,
			PreviewBudgetBytes,
			TextureMemoryUsageBytes,
			pItem->m_PreviewResidentBytes);
		pItem->m_PreviewResized = false;
		pItem->m_PreviewHighPriority = HighPriority;
		if(CurTab == ASSETS_TAB_ENTITIES)
		{
			SCustomEntities *pEntity = gs_vpSearchEntitiesList[Index];
			StartEntitiesDecode(pEntity, Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_GAME)
		{
			SCustomGame *pGame = gs_vpSearchGamesList[Index];
			StartAssetDecode(pGame, "game", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_EMOTICONS)
		{
			SCustomEmoticon *pEmoticon = gs_vpSearchEmoticonsList[Index];
			StartAssetDecode(pEmoticon, "emoticons", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_PARTICLES)
		{
			SCustomParticle *pParticle = gs_vpSearchParticlesList[Index];
			StartAssetDecode(pParticle, "particles", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_HUD)
		{
			SCustomHud *pHud = gs_vpSearchHudList[Index];
			StartAssetDecode(pHud, "hud", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_GUI_CURSOR)
		{
			SCustomGuiCursor *pGuiCursor = gs_vpSearchGuiCursorList[Index];
			StartAssetDecode(pGuiCursor, "gui_cursor", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_ARROW)
		{
			SCustomArrow *pArrow = gs_vpSearchArrowList[Index];
			StartAssetDecode(pArrow, "arrow", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_STRONG_WEAK)
		{
			SCustomStrongWeak *pStrongWeak = gs_vpSearchStrongWeakList[Index];
			StartAssetDecode(pStrongWeak, "strong_weak", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_ENTITY_BG)
		{
			SCustomEntityBg *pEntityBg = gs_vpSearchEntityBgList[Index];
			StartEntityBgDecode(pEntityBg, Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		else if(CurTab == ASSETS_TAB_EXTRAS)
		{
			SCustomExtras *pExtras = gs_vpSearchExtrasList[Index];
			StartAssetDecode(pExtras, "extras", Storage(), Engine(), pItem->m_PreviewRequestedTextureSize);
		}
		if(pItem->m_pDecodeJob)
		{
			const SSettingsAssetPreviewHandle Handle = MakePreviewHandle(*pItem);
			if(HighPriority)
				vDecodeQueue.push_front(Handle);
			else
				vDecodeQueue.push_back(Handle);
			++PreviewDecodeStartsThisFrame;
			if(HighPriority && pActiveVisiblePreflight != nullptr)
				++pActiveVisiblePreflight->m_ThumbStartsBeforeVisible;
			else if(HighPriority && pActiveVisiblePreflight == nullptr)
				++VisiblePreviewStartsDuringDraw;
			return true;
		}
		else
		{
			pItem->m_PreviewHighPriority = false;
			pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_FAILED;
		}
		return false;
	};

	auto SchedulePreviewRange = [&](int FirstIndex, int LastIndex, int ItemsPerRow) {
		if(SearchListSize == 0 || FirstIndex < 0 || LastIndex < 0)
			return;
		for(int Index = FirstIndex; Index <= LastIndex && PreviewDecodeStartsThisFrame < MaxPreviewHighPriorityDecodeStartsPerFrame; ++Index)
		{
			const bool Started = StartPreviewDecode((size_t)Index, SettingsAssetPreviewShouldPrioritizeVisibleRange(Index, FirstIndex, LastIndex));
			if(Started)
				CombinedVisibleAdmission.Record(true, false);
		}

		const int PrefetchItems = maximum(1, ItemsPerRow) * PreviewPrefetchRows;
		const int FirstRelevant = maximum(0, FirstIndex - PrefetchItems);
		const int LastRelevant = minimum((int)SearchListSize - 1, LastIndex + PrefetchItems);
		for(int Index = FirstRelevant; Index <= LastRelevant && PreviewDecodeStartsThisFrame < MaxPreviewDecodeStartsPerFrame; ++Index)
		{
			if(SettingsAssetPreviewShouldPrioritizeVisibleRange(Index, FirstIndex, LastIndex))
				continue;
			const bool Started = StartPreviewDecode((size_t)Index, false);
			if(Started)
				CombinedVisibleAdmission.Record(false, true);
		}
	};

	auto SelectedCustomAssetIndex = [&](int Tab, size_t CurrentSearchListSize) {
		for(size_t Index = 0; Index < CurrentSearchListSize; ++Index)
		{
			const SCustomItem *pItem = GetCustomItem(Tab, Index);
			if(pItem != nullptr && IsCustomAssetSelectedByTab(Tab, pItem->m_aName))
				return (int)Index;
		}
		return -1;
	};

	auto SelectedCombinedAssetIndex = [&](int Tab, const std::vector<size_t> &vVisibleLocalAssetIndices) {
		for(size_t CombinedIndex = 0; CombinedIndex < vVisibleLocalAssetIndices.size(); ++CombinedIndex)
		{
			const SCustomItem *pItem = GetCustomItem(Tab, vVisibleLocalAssetIndices[CombinedIndex]);
			if(pItem != nullptr && IsCustomAssetSelectedByTab(Tab, pItem->m_aName))
				return (int)CombinedIndex;
		}
		return -1;
	};

	auto FindInstalledWorkshopAssetByLocalName = [&](const char *pLocalName) -> const SWorkshopHudAsset * {
		const SWorkshopHudState *pWorkshopStateForTab = WorkshopStateByTab(s_CurCustomTab);
		if(pLocalName == nullptr || pLocalName[0] == '\0' || pCurrentCategory == nullptr || !pCurrentCategory->m_WorkshopEnabled || pWorkshopStateForTab == nullptr)
			return nullptr;
		for(const SWorkshopHudAsset &Asset : pWorkshopStateForTab->m_vAssets)
		{
			if(!Asset.m_Installed || Asset.m_LocalName.empty())
				continue;
			if(str_comp(Asset.m_LocalName.c_str(), pLocalName) == 0)
				return &Asset;
		}
		return nullptr;
	};

	auto ResolveLocalAssetStatusLabel = [&](const SCustomItem *pItem, bool ShowLocalOnlyBadge) -> const char * {
		if(pItem == nullptr || ShowLocalOnlyBadge)
			return nullptr;
		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<const SCustomEntityBg *>(pItem)->m_IsDirectory)
			return nullptr;
		const SWorkshopHudAsset *pWorkshopAsset = FindInstalledWorkshopAssetByLocalName(pItem->m_aName);
		return pWorkshopAsset != nullptr ? Localize("Downloaded") : Localize("Local");
	};

	auto RenderCardBadge = [&](const CUIRect &Rect, const char *pLabel, const ColorRGBA &FillColor, float FontSize) {
		CUIRect BadgeRect = Rect;
		DrawRoundedSurface(Ui(), BadgeRect, FillColor, ColorRGBA(), minimum(BadgeRect.h / 2.0f, 6.0f));
		SLabelProperties BadgeLabelProps;
		BadgeLabelProps.m_MaxWidth = static_cast<int>(BadgeRect.w - AssetsCardStatusTagHorizontalPadding * 2.0f);
		BadgeLabelProps.m_StopAtEnd = true;
		BadgeLabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&BadgeRect, pLabel, FontSize, TEXTALIGN_MC, BadgeLabelProps);
	};

	auto RenderAssetStatusTag = [&](const CUIRect &TagRect, const char *pLabel, bool Positive) {
		CUIRect StatusRect = TagRect;
		const ColorRGBA TagColor = Positive ? AssetsCardStatusReadyColor : AssetsCardStatusNetworkColor;
		DrawRoundedSurface(Ui(), StatusRect, TagColor, ColorRGBA(), minimum(StatusRect.h / 2.0f, 6.0f));
		SLabelProperties StatusLabelProps;
		StatusLabelProps.m_MaxWidth = static_cast<int>(StatusRect.w - AssetsCardStatusTagHorizontalPadding * 2.0f);
		StatusLabelProps.m_StopAtEnd = true;
		StatusLabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&StatusRect, pLabel != nullptr && pLabel[0] != '\0' ? pLabel : "--", AssetsCardStatusTagFontSize, TEXTALIGN_MC, StatusLabelProps);
	};

	auto ComputePreviewDrawRect = [&](const CUIRect &TextureRect, float ContentWidth, float ContentHeight) {
		const float SafeContentWidth = maximum(ContentWidth, 1.0f);
		const float SafeContentHeight = maximum(ContentHeight, 1.0f);
		const float FrameScale = minimum(TextureRect.w / SafeContentWidth, TextureRect.h / SafeContentHeight);
		const float DrawWidth = SafeContentWidth * FrameScale;
		const float DrawHeight = SafeContentHeight * FrameScale;
		return CUIRect{
			TextureRect.x + (TextureRect.w - DrawWidth) / 2.0f,
			TextureRect.y + (TextureRect.h - DrawHeight) / 2.0f,
			DrawWidth,
			DrawHeight};
	};

	auto DrawPreviewFrame = [&](const CUIRect &TextureRect) -> CUIRect {
		CUIRect PreviewFrame = TextureRect;
		PreviewFrame.Margin(3.0f, &PreviewFrame);
		DrawRoundedSurface(Ui(), PreviewFrame, ColorRGBA(0.03f, 0.05f, 0.08f, 0.18f), ColorRGBA(), 10.0f);
		if(s_CurCustomTab != ASSETS_TAB_GAME && s_CurCustomTab != ASSETS_TAB_STRONG_WEAK)
			PreviewFrame.Margin(8.0f, &PreviewFrame);
		return PreviewFrame;
	};

	auto RenderAssetsCardShell = [&](const SSettingsAssetsCardShell &Shell) {
		CUIRect ShellRect = Shell.m_CardRect;
		DrawRoundedSurface(Ui(), ShellRect, ColorRGBA(0.03f, 0.04f, 0.06f, 0.16f), ColorRGBA(), AssetCardRadius);
	};

	constexpr float AssetCardFooterSpacing = 8.0f;
	constexpr float AssetCardHeaderHeightSingleLine = 30.0f;
	constexpr float AssetCardHeaderHeightWithAuthor = 40.0f;
	constexpr float AssetCardHeaderMargin = 3.0f;
	constexpr float AssetCardHeaderControlMargin = 1.0f;
	constexpr float AssetCardHeaderControlHeight = 18.0f;
	constexpr float AssetCardTextReserveSingleLine = 26.0f;
	constexpr float AssetCardTextReserveWithAuthor = 40.0f;
	auto LayoutAssetsCardShell = [&](const CUIRect &CardRect, bool HasActionButton, const char *pStatusLabel, bool ShowLocalOnlyBadge, bool ShowAuthorRow) {
		SSettingsAssetsCardShell Shell;
		Shell.m_CardRect = CardRect;
		CUIRect HeaderRect;
		CUIRect BodyRect = CardRect;
		const float AssetCardHeaderHeight = ShowAuthorRow ? AssetCardHeaderHeightWithAuthor : AssetCardHeaderHeightSingleLine;
		BodyRect.HSplitTop(AssetCardHeaderHeight, &HeaderRect, &Shell.m_TextureRect);
		Shell.m_TextureRect.HSplitTop(8.0f, nullptr, &Shell.m_TextureRect);
		CUIRect TitleRect = HeaderRect;
		TitleRect.Margin(AssetCardHeaderMargin, &TitleRect);
		if(HasActionButton)
		{
			Shell.m_HasActionButton = true;
			TitleRect.VSplitRight(16.0f, &TitleRect, &Shell.m_ActionButtonRect);
			TitleRect.VSplitRight(minimum(3.0f, TitleRect.w), &TitleRect, nullptr);
			Shell.m_ActionButtonRect.Margin(AssetCardHeaderControlMargin, &Shell.m_ActionButtonRect);
			const float ControlHeight = minimum(AssetCardHeaderControlHeight, Shell.m_ActionButtonRect.h);
			if(Shell.m_ActionButtonRect.h > ControlHeight)
			{
				const float VerticalInset = (Shell.m_ActionButtonRect.h - ControlHeight) / 2.0f;
				Shell.m_ActionButtonRect.y += VerticalInset;
				Shell.m_ActionButtonRect.h = ControlHeight;
			}
		}

		const float BadgeGap = 2.0f;
		const float TitleMinWidth = 12.0f;
		const float HeaderControlHeight = minimum(AssetCardHeaderControlHeight, TitleRect.h);
		auto ComputeBadgeWidth = [&](const char *pLabel) {
			if(pLabel == nullptr || pLabel[0] == '\0')
				return AssetsCardStatusTagMinWidth;
			const float DesiredBadgeWidth = TextRender()->TextWidth(AssetsCardStatusTagFontSize, pLabel, -1, -1.0f) + AssetsCardStatusTagHorizontalPadding * 2.0f;
			return maximum(AssetsCardStatusTagMinWidth, minimum(AssetsCardStatusTagMaxWidth, DesiredBadgeWidth));
		};
		auto ReserveTrailingRect = [&](CUIRect &AvailableRect, float DesiredWidth, float DesiredMinWidth, CUIRect &OutRect, bool &OutVisible) {
			if(AvailableRect.w <= 0.0f)
				return;
			const float ReservedTitleWidth = minimum(TitleMinWidth, AvailableRect.w);
			const float MaxWidth = maximum(0.0f, AvailableRect.w - ReservedTitleWidth);
			if(MaxWidth < DesiredMinWidth)
				return;
			const float Width = minimum(DesiredWidth, MaxWidth);
			AvailableRect.VSplitRight(Width, &AvailableRect, &OutRect);
			OutVisible = true;
			if(AvailableRect.w > BadgeGap)
				AvailableRect.VSplitRight(minimum(BadgeGap, AvailableRect.w), &AvailableRect, nullptr);
		};
		if(pStatusLabel != nullptr && pStatusLabel[0] != '\0')
		{
			ReserveTrailingRect(TitleRect, ComputeBadgeWidth(pStatusLabel), AssetsCardStatusTagMinWidth, Shell.m_StatusTagRect, Shell.m_HasStatusTag);
			if(Shell.m_HasStatusTag && Shell.m_StatusTagRect.h > HeaderControlHeight)
			{
				const float VerticalInset = (Shell.m_StatusTagRect.h - HeaderControlHeight) / 2.0f;
				Shell.m_StatusTagRect.y += VerticalInset;
				Shell.m_StatusTagRect.h = HeaderControlHeight;
			}
		}
		if(ShowLocalOnlyBadge)
		{
			ReserveTrailingRect(TitleRect, ComputeBadgeWidth(Localize("Local-only")), AssetsCardStatusTagMinWidth, Shell.m_LocalOnlyBadgeRect, Shell.m_HasLocalOnlyBadge);
			if(Shell.m_HasLocalOnlyBadge && Shell.m_LocalOnlyBadgeRect.h > HeaderControlHeight)
			{
				const float VerticalInset = (Shell.m_LocalOnlyBadgeRect.h - HeaderControlHeight) / 2.0f;
				Shell.m_LocalOnlyBadgeRect.y += VerticalInset;
				Shell.m_LocalOnlyBadgeRect.h = HeaderControlHeight;
			}
		}
		Shell.m_HasAuthorRow = ShowAuthorRow;
		if(Shell.m_HasAuthorRow)
			TitleRect.HSplitTop(11.0f, &Shell.m_TitleRect, &Shell.m_AuthorRect);
		else
			Shell.m_TitleRect = TitleRect;
		return Shell;
	};

	auto ComputeAssetPreviewContentSize = [&](bool WorkshopCard) {
		float ContentWidth = TextureWidth;
		float ContentHeight = TextureHeight;
		const bool EntityTilePreview = s_CurCustomTab == ASSETS_TAB_ENTITIES && gs_SettingsAssetsEntityGamePreview;
		if(EntityTilePreview)
		{
			const float TileContentSize = WorkshopCard ? 112.0f : 104.0f;
			ContentWidth = TileContentSize;
			ContentHeight = TileContentSize;
		}
		else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR || s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ContentWidth = 76.0f;
			ContentHeight = 76.0f;
		}
		return std::pair<float, float>(ContentWidth, ContentHeight);
	};

	auto RenderAssetsEntityTilePreviewArtifact = [&](const CUIRect &PreviewFrameRect, IGraphics::CTextureHandle Texture) {
		static constexpr int ASSETS_ENTITY_TILE_PREVIEW_COLS = 7;
		static constexpr int ASSETS_ENTITY_TILE_PREVIEW_ROWS = 7;
		static constexpr int COLS = ASSETS_ENTITY_TILE_PREVIEW_COLS;
		static constexpr int ROWS = ASSETS_ENTITY_TILE_PREVIEW_ROWS;
		static constexpr unsigned char aTilePreviewLayout[ROWS][COLS] = {
			{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
			{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
			{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
			{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
			{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
			{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
			{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
		};

		const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(true);
		const CUIRect PreviewRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);
		const float TileSize = ComputeEntityPreviewTileSize(PreviewRect.w, PreviewRect.h, COLS, ROWS);
		const float OffX = PreviewRect.x;
		const float OffY = PreviewRect.y + (PreviewRect.h - ROWS * TileSize) / 2.0f;

		constexpr float TileInset = 1.5f / 1024.0f;
		constexpr float TileScale = 1.0f / 16.0f;

		Graphics()->WrapClamp();
		Graphics()->TextureSet(Texture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		for(int Row = 0; Row < ROWS; Row++)
		{
			for(int Column = 0; Column < COLS; Column++)
			{
				unsigned char Tile = aTilePreviewLayout[Row][Column];
				if(Tile == 0)
					continue;
				const int Tx = Tile % 16;
				const int Ty = Tile / 16;
				const float U0 = Tx * TileScale + TileInset;
				const float V0 = Ty * TileScale + TileInset;
				const float U1 = U0 + TileScale - TileInset * 2;
				const float V1 = V0 + TileScale - TileInset * 2;
				Graphics()->QuadsSetSubset(U0, V0, U1, V1);
				IGraphics::CQuadItem QuadItem(OffX + Column * TileSize, OffY + Row * TileSize, TileSize, TileSize);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
			}
		}
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};

	auto EnsureAssetsCardPreviewArtifact = [&](const SSettingsAssetsCardPreviewState &PreviewState, const CUIRect &PreviewFrameRect, bool WorkshopCard) {
		if(PreviewState.m_DrawEntityTileArtifact && PreviewState.m_Texture.IsValid())
		{
			RenderAssetsEntityTilePreviewArtifact(PreviewFrameRect, PreviewState.m_Texture);
			return;
		}
		if(PreviewState.m_Texture.IsValid())
		{
			const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(WorkshopCard);
			const CUIRect PreviewRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);
			Graphics()->WrapClamp();
			Graphics()->TextureSet(PreviewState.m_Texture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem QuadItem(PreviewRect.x, PreviewRect.y, PreviewRect.w, PreviewRect.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	};

	auto DrawResourcePreviewPlaceholder = [&](const CUIRect &PreviewFrameRect, bool WorkshopCard, ESettingsResourcePreviewDrawResult DrawResult) {
		const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(WorkshopCard);
		CUIRect LoadingRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);
		const ColorRGBA PlaceholderColor = DrawResult == ESettingsResourcePreviewDrawResult::FAILED_PLACEHOLDER ? ColorRGBA(0.18f, 0.08f, 0.10f, 0.16f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.10f);
		DrawRoundedSurface(Ui(), LoadingRect, PlaceholderColor, ColorRGBA(), 6.0f);
	};

	auto RenderAssetsCardPreview = [&](const SSettingsAssetsCardShell &Shell, const SSettingsAssetsCardPreviewState &PreviewState, bool WorkshopCard, bool RenderPreview) {
		CUIRect PreviewFrameRect = DrawPreviewFrame(Shell.m_TextureRect);
		if(PreviewState.m_DrawFolderIcon)
		{
			CUIRect IconRect = Shell.m_TextureRect;
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			if(PreviewState.m_FolderIsWorkshopRoot)
				TextRender()->TextColor(ColorRGBA(1.0f, 0.78f, 0.78f, 1.0f));
			Ui()->DoLabel(&IconRect, PreviewState.m_FolderIsParent ? FONT_ICON_FOLDER_OPEN : FONT_ICON_FOLDER, 36.0f, TEXTALIGN_MC);
			if(PreviewState.m_FolderIsWorkshopRoot)
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			return;
		}
		if(PreviewState.m_Texture.IsValid())
		{
			if(!RenderPreview)
			{
				DrawResourcePreviewPlaceholder(PreviewFrameRect, WorkshopCard, ESettingsResourcePreviewDrawResult::PLACEHOLDER);
				return;
			}
			EnsureAssetsCardPreviewArtifact(PreviewState, PreviewFrameRect, WorkshopCard);
			return;
		}
		if(PreviewState.m_Loading)
		{
			const SResourcePreviewState PlaceholderState;
			const ESettingsResourcePreviewDrawResult DrawResult = SettingsResourcePreviewDrawResult(PlaceholderState);
			DrawResourcePreviewPlaceholder(PreviewFrameRect, WorkshopCard, DrawResult);
		}
	};

	auto RenderAssetsCardLoadingShells = [&](const CUIRect &ListArea, float RowHeight, int Columns, bool WorkshopCard) {
		if(RowHeight <= 0.0f || Columns <= 0 || ListArea.w <= 0.0f || ListArea.h <= 0.0f)
			return;
		CUIRect RowView = ListArea;
		const int VisibleRows = maximum(1, minimum(3, (int)std::ceil(ListArea.h / RowHeight)));
		const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, WorkshopCard, false);
		for(int Row = 0; Row < VisibleRows; ++Row)
		{
			CUIRect RowRect;
			RowView.HSplitTop(RowHeight, &RowRect, &RowView);
			for(int Column = 0; Column < Columns; ++Column)
			{
				CUIRect ItemRect;
				RowRect.VSplitLeft(RowRect.w / (Columns - Column), &ItemRect, &RowRect);
				ItemRect.Margin(Margin / 2.0f, &ItemRect);
				const SSettingsAssetsCardShell Shell = LayoutAssetsCardShell(ItemRect, WorkshopCard, WorkshopCard ? Localize("Not downloaded") : nullptr, false, ShowAuthorRow);
				RenderAssetsCardShell(Shell);
				SSettingsAssetsCardPreviewState PreviewState;
				PreviewState.m_Loading = true;
				RenderAssetsCardPreview(Shell, PreviewState, WorkshopCard, false);
			}
		}
	};

	auto RenderAssetsCardMetadata = [&](const SSettingsAssetsCardShell &Shell, SSettingsAssetsCardMetadataCacheEntry &Metadata, bool Render) {
		if(!Render)
			return;
		if(Metadata.m_pTitleElement == nullptr)
			Metadata.m_pTitleElement = std::make_unique<CUIElement>();
		if(!Metadata.m_pTitleElement->IsRegistered())
			Metadata.m_pTitleElement->Init(Ui(), 1);
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = static_cast<int>(Shell.m_TitleRect.w);
		TitleProps.m_StopAtEnd = true;
		TitleProps.m_EllipsisAtEnd = true;
		DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS, *Metadata.m_pTitleElement, &Shell.m_TitleRect, Metadata.m_Title.c_str(), AssetsCardTitleFontSize, TEXTALIGN_ML, TitleProps);

		if(Shell.m_HasAuthorRow)
		{
			if(Metadata.m_pAuthorElement == nullptr)
				Metadata.m_pAuthorElement = std::make_unique<CUIElement>();
			if(!Metadata.m_pAuthorElement->IsRegistered())
				Metadata.m_pAuthorElement->Init(Ui(), 1);
			SLabelProperties AuthorProps;
			AuthorProps.m_MaxWidth = static_cast<int>(Shell.m_AuthorRect.w);
			AuthorProps.m_StopAtEnd = true;
			AuthorProps.m_EllipsisAtEnd = true;
			DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS, *Metadata.m_pAuthorElement, &Shell.m_AuthorRect, Metadata.m_Author.c_str(), AssetsCardAuthorFontSize, TEXTALIGN_ML, AuthorProps);
		}
		if(Shell.m_HasStatusTag)
			RenderAssetStatusTag(Shell.m_StatusTagRect, Metadata.m_StatusLabel.c_str(), Metadata.m_Installed || Metadata.m_LocalOnly);
		if(Shell.m_HasLocalOnlyBadge)
			RenderCardBadge(Shell.m_LocalOnlyBadgeRect, Localize("Local-only"), ColorRGBA(0.46f, 0.41f, 0.20f, 0.88f), 7.5f);
		if(Metadata.m_DownloadFailed)
		{
			if(Metadata.m_pErrorElement == nullptr)
				Metadata.m_pErrorElement = std::make_unique<CUIElement>();
			if(!Metadata.m_pErrorElement->IsRegistered())
				Metadata.m_pErrorElement->Init(Ui(), 1);
			CUIRect ErrorRect = Shell.m_CardRect;
			ErrorRect.HSplitBottom(14.0f, nullptr, &ErrorRect);
			DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS, *Metadata.m_pErrorElement, &ErrorRect, Localize("Download failed"), 9.0f, TEXTALIGN_MC);
		}
	};

	auto RenderEntityBgFallback = [&](const CUIRect &Rect) {
		CUIRect FallbackRect = Rect;
		FallbackRect.Margin(6.0f, &FallbackRect);
		DrawRoundedSurface(Ui(), FallbackRect, ColorRGBA(0.14f, 0.16f, 0.18f, 0.9f), ColorRGBA(), 8.0f);

		CUIRect LabelRect = FallbackRect;
		LabelRect.Margin(8.0f, &LabelRect);
		Ui()->DoLabel(&LabelRect, Localize("Map Preview TODO"), 11.0f, TEXTALIGN_MC);
	};

	auto RenderEntityBgVideoFallback = [&](const CUIRect &Rect) {
		CUIRect FallbackRect = Rect;
		FallbackRect.Margin(6.0f, &FallbackRect);
		DrawRoundedSurface(Ui(), FallbackRect, ColorRGBA(0.12f, 0.14f, 0.19f, 0.92f), ColorRGBA(), 8.0f);

		CUIRect IconRect, LabelRect;
		FallbackRect.HSplitTop(FallbackRect.h * 0.58f, &IconRect, &LabelRect);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&IconRect, FONT_ICON_PLAY, 30.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		LabelRect.Margin(6.0f, &LabelRect);
		Ui()->DoLabel(&LabelRect, Localize("Video Background"), 10.5f, TEXTALIGN_MC);
	};

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 64;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
	{
		SearchListSize = gs_vpSearchGuiCursorList.size();
		TextureWidth = 96;
		TextureHeight = 96;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		SearchListSize = gs_vpSearchArrowList.size();
		TextureWidth = 96;
		TextureHeight = 96;
	}
	else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
	{
		SearchListSize = gs_vpSearchStrongWeakList.size();
		TextureHeight = 44;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
	{
		SearchListSize = gs_vpSearchEntityBgList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}

	const float AssetCardTextReserve = s_CurCustomTab == ASSETS_TAB_ENTITY_BG ? AssetCardTextReserveSingleLine : AssetCardTextReserveWithAuthor;
	auto FinalizeReadyPreviewDecodes = [&](const SSettingsResourceFrameContext &FinalizeFrameContext) {
		if(!WindowActive)
			return;
		RemainingHeavyResourceBatches = std::min(
			RemainingHeavyResourceBatches,
			SettingsResourceSharedHeavyBudget(FinalizeFrameContext, 4, 1));
		CPerfTimer ScanTimer;
		CPerfTimer DecodeFinalizeTimer;
		int ReadyCount = 0;
		int DroppedStale = 0;
		int DeferredCompleted = 0;
		constexpr size_t MaxPreviewDecodePollsPerFrame = 24;
		const size_t PendingCount = vDecodeQueue.size();
		const size_t DecodePolls = minimum(PendingCount, MaxPreviewDecodePollsPerFrame);
		for(size_t i = 0; i < DecodePolls; ++i)
		{
			const SSettingsAssetPreviewHandle Handle = vDecodeQueue.front();
			vDecodeQueue.pop_front();
			if(Handle.m_Tab != s_CurCustomTab || Handle.m_Epoch != PreviewEpoch)
			{
				++DroppedStale;
				char aDropExtra[160];
				str_format(aDropExtra, sizeof(aDropExtra), "tab=%d asset=%s epoch=%u current_epoch=%u",
					Handle.m_Tab, Handle.m_Name.c_str(), Handle.m_Epoch, PreviewEpoch);
				LogAssetsPerfStageForClient(Client(), "assets_preview_decode_drop_stale", 0.0, true, aDropExtra);
				continue;
			}
			SCustomItem *pItem = FindCustomItemByPreviewHandle(Handle);
			if(pItem == nullptr)
				continue;
			if(pItem->m_PreviewEpoch != Handle.m_Epoch || !SettingsAssetPreviewHandleMatches(Handle, s_CurCustomTab, PreviewEpoch, pItem->m_PreviewListIndex, pItem->m_aName))
			{
				++DroppedStale;
				char aDropExtra[128];
				str_format(aDropExtra, sizeof(aDropExtra), "tab=%d asset=%s", s_CurCustomTab, Handle.m_Name.c_str());
				LogAssetsPerfStageForClient(Client(), "assets_preview_decode_drop_stale", 0.0, true, aDropExtra);
				continue;
			}
			if(!pItem->m_pDecodeJob)
			{
				if(pItem->m_PreviewState == SCustomItem::PREVIEW_STATE_LOADING)
					pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_UNLOADED;
				continue;
			}

			auto pDecodeJob = std::static_pointer_cast<CFullAsyncImageLoadJob>(pItem->m_pDecodeJob);
			if(!pDecodeJob->IsCompleted())
			{
				vDecodeQueue.push_back(Handle);
				continue;
			}

			const ESettingsResourcePriority FinalizePriority = pItem->m_PreviewHighPriority ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::BACKGROUND;
			const int MaxFinalizesForFrame = SettingsResourceFrameStageBudget(FinalizeFrameContext, FinalizePriority, MaxPreviewDecodeFinalizesPerFrame, 0);
			if(SettingsAssetPreviewShouldDeferFinalize(PreviewDecodeFinalizesThisFrame, DecodeFinalizeTimer.ElapsedMs(), MaxFinalizesForFrame, MaxPreviewDecodeFinalizeMsPerFrame))
			{
				vDecodeQueue.push_back(Handle);
				++DeferredCompleted;
				continue;
			}
			if(!SettingsResourceConsumeSharedHeavyBudget(RemainingHeavyResourceBatches))
			{
				vDecodeQueue.push_back(Handle);
				++DeferredCompleted;
				continue;
			}

			CPerfTimer DecodeFinalizeBatchTimer;
			CPerfTimer FinalizeTimer;
			CFullAsyncImageLoadJob::SResult Result;
			{
				CPerfTimer GetResultTimer;
				Result = pDecodeJob->GetResult();
				LogAssetsPerfStageForClient(Client(), "assets_finalize_get_result", GetResultTimer.ElapsedMs(), false, pItem->m_aName);
			}
			pItem->m_pDecodeJob.reset();
			if(!Result.m_Success || !Result.m_Image.m_pData)
			{
				Result.m_Image.Free();
				pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_FAILED;
				continue;
			}

			const bool ResizedPreview = Result.m_Resized;
			{
				CPerfTimer TargetSizeTimer;
				char aTargetSizeExtra[160];
				str_format(aTargetSizeExtra, sizeof(aTargetSizeExtra), "asset=%s src=%dx%d dst=%dx%d resized=%d",
					pItem->m_aName, Result.m_SourceWidth > 0 ? Result.m_SourceWidth : (int)Result.m_Image.m_Width,
					Result.m_SourceHeight > 0 ? Result.m_SourceHeight : (int)Result.m_Image.m_Height,
					(int)Result.m_Image.m_Width, (int)Result.m_Image.m_Height, ResizedPreview ? 1 : 0);
				LogAssetsPerfStageForClient(Client(), "assets_finalize_target_size_calc", TargetSizeTimer.ElapsedMs(), false, aTargetSizeExtra);
			}

			{
				CPerfTimer PostProcessTimer;
				pItem->m_PreviewImage = std::move(Result.m_Image);
				pItem->m_PreviewBytes = pItem->m_PreviewImage.DataSize();
				if(pItem->m_PreviewRequestedTextureSize <= 0)
					pItem->m_PreviewRequestedTextureSize = LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE;
				pItem->m_PreviewResized = ResizedPreview;
				char aPostProcessExtra[160];
				str_format(aPostProcessExtra, sizeof(aPostProcessExtra), "asset=%s bytes=%u resized=%d",
					pItem->m_aName, (unsigned)pItem->m_PreviewBytes, ResizedPreview ? 1 : 0);
				LogAssetsPerfStageForClient(Client(), "assets_finalize_postprocess_pixels", PostProcessTimer.ElapsedMs(), false, aPostProcessExtra);
			}
			pItem->m_PreviewState = SCustomItem::PREVIEW_STATE_READY;
			QueueReadyPreview(pItem);
			++ReadyCount;
			++PreviewDecodeFinalizesThisFrame;
			char aFinalizeTotalExtra[192];
			str_format(aFinalizeTotalExtra, sizeof(aFinalizeTotalExtra), "tab=%d asset=%s resized=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, ResizedPreview ? 1 : 0, (unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStageForClient(Client(), "assets_finalize_total", FinalizeTimer.ElapsedMs(), false, aFinalizeTotalExtra);
			char aReadyExtra[160];
			str_format(aReadyExtra, sizeof(aReadyExtra), "tab=%d asset=%s w=%u h=%u bytes=%u resized=%d",
				s_CurCustomTab, pItem->m_aName, (unsigned)pItem->m_PreviewImage.m_Width,
				(unsigned)pItem->m_PreviewImage.m_Height, (unsigned)pItem->m_PreviewBytes, ResizedPreview ? 1 : 0);
			LogAssetsPerfStageForClient(Client(), "assets_preview_decode_ready", 0.0, true, aReadyExtra);
			char aFinalizeExtra[192];
			str_format(aFinalizeExtra, sizeof(aFinalizeExtra), "tab=%d asset=%s finalized=%d deferred=%d resized=%d bytes=%u",
				s_CurCustomTab, pItem->m_aName, PreviewDecodeFinalizesThisFrame, DeferredCompleted, ResizedPreview ? 1 : 0,
				(unsigned)pItem->m_PreviewBytes);
			LogAssetsPerfStageForClient(Client(), "assets_preview_decode_finalize_batch", DecodeFinalizeBatchTimer.ElapsedMs(), false, aFinalizeExtra);
		}
		char aFinalizeTotalExtra[160];
		str_format(aFinalizeTotalExtra, sizeof(aFinalizeTotalExtra), "tab=%d finalized=%d deferred=%d polled=%d pending_after=%d budget_ms=%.1f used_ms=%.3f frame_context=%s",
			s_CurCustomTab, PreviewDecodeFinalizesThisFrame, DeferredCompleted, (int)DecodePolls, (int)vDecodeQueue.size(),
			MaxPreviewDecodeFinalizeMsPerFrame, DecodeFinalizeTimer.ElapsedMs(), AssetsResourceFrameContextName(FinalizeFrameContext));
		LogAssetsPerfStageForClient(Client(), "assets_preview_decode_finalize_total", DecodeFinalizeTimer.ElapsedMs(), false, aFinalizeTotalExtra);

		char aExtra[224];
		str_format(aExtra, sizeof(aExtra), "tab=%d search_list=%d decode_pending=%d ready_queue=%d ready=%d dropped_stale=%d uploads=%d resized=%d finalized=%d deferred=%d finalize_budget_ms=%.1f scroll_cooldown=%d recovery_frames=%d heavy_batches_left=%d frame_context=%s jump_scroll=%d",
			s_CurCustomTab, (int)SearchListSize, (int)vDecodeQueue.size(), (int)vReadyQueue.size(),
			ReadyCount, DroppedStale, UploadedPreviewsThisFrame, ResizedPreviewsThisFrame, PreviewDecodeFinalizesThisFrame, DeferredCompleted,
			MaxPreviewDecodeFinalizeMsPerFrame, s_AssetsScrollCooldownFrames, s_AssetsPostScrollRecoveryFrames, RemainingHeavyResourceBatches,
			AssetsResourceFrameContextName(FinalizeFrameContext), FinalizeFrameContext.m_JumpScrollActive ? 1 : 0);
		LogAssetsFramePerfStage("assets_preview_gpu_upload_scan", ScanTimer.ElapsedMs(), false, aExtra);
	};
	auto DrainSharedResourcePreviewUploadQueue = [&](int MaxUploads, SResourcePreviewTelemetry &Telemetry) {
		if(AssetsShellOnlyFrame || MaxUploads <= 0 || gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0)
			return 0;
		SSettingsResourceMergeBudget UploadBudget;
		UploadBudget.m_MaxGpuUploads = MaxUploads;
		SResourcePreviewUploadBudget PreviewUploadBudget;
		PreviewUploadBudget.m_MaxUploads = MaxUploads;
		PreviewUploadBudget.m_pMergeBudget = &UploadBudget;
		PreviewUploadBudget.m_pFrameBudget = SettingsFrameBudget();
		PreviewUploadBudget.m_pGpuUploadLimiter = GameClient()->GpuUploadLimiter();
		return gs_SettingsAssetsResourcePreviewUploadScheduler.Drain(PreviewUploadBudget, Telemetry, gs_SettingsAssetsResourcePreviewCache, Graphics());
	};
	auto DrainReadyPreviewUploadsAfterList = [&](const SSettingsResourceFrameContext &UploadFrameContext) {
		if(!WindowActive)
			return;
		RemainingHeavyResourceBatches = std::min(
			RemainingHeavyResourceBatches,
			SettingsResourceSharedHeavyBudget(UploadFrameContext, 4, 1));
		size_t UploadedBytesThisFrame = 0;
		int OversizedUploadsThisFrame = 0;
		ESettingsWarmupMissReason UploadBlockReason = ESettingsWarmupMissReason::NONE;
		bool UploadBlocked = false;
		SSettingsResourceMergeBudget UploadBudget;
		UploadBudget.m_MaxGpuUploads = MaxPreviewUploadsPerFrame - UploadedPreviewsThisFrame;
		SResourcePreviewUploadBudget PreviewUploadBudget;
		PreviewUploadBudget.m_MaxUploads = MaxPreviewUploadsPerFrame - UploadedPreviewsThisFrame;
		PreviewUploadBudget.m_pMergeBudget = &UploadBudget;
		PreviewUploadBudget.m_pFrameBudget = SettingsFrameBudget();
		PreviewUploadBudget.m_pGpuUploadLimiter = GameClient()->GpuUploadLimiter();
		SResourcePreviewTelemetry PreviewUploadTelemetry;
		UploadedPreviewsThisFrame += DrainSharedResourcePreviewUploadQueue(MaxPreviewUploadsPerFrame - UploadedPreviewsThisFrame, PreviewUploadTelemetry);
		if(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() > 0)
		{
			UploadBlocked = true;
			UploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
		}
		while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !vReadyQueue.empty() && UploadedPreviewsThisFrame < MaxPreviewUploadsPerFrame)
		{
			const SSettingsAssetPreviewHandle Handle = vReadyQueue.front();
			vReadyQueue.pop_front();
			vReadyQueued.erase(SettingsAssetPreviewHandleKey(Handle));
			if(Handle.m_Tab != s_CurCustomTab || Handle.m_Epoch != PreviewEpoch)
				continue;
			SCustomItem *pItem = FindCustomItemByPreviewHandle(Handle);
			if(pItem == nullptr)
				continue;
			if(pItem->m_PreviewEpoch != Handle.m_Epoch || !SettingsAssetPreviewHandleMatches(Handle, s_CurCustomTab, PreviewEpoch, pItem->m_PreviewListIndex, pItem->m_aName) ||
				pItem->m_PreviewState != SCustomItem::PREVIEW_STATE_READY || !pItem->m_PreviewImage.m_pData)
			{
				if(pItem->m_PreviewEpoch == Handle.m_Epoch)
					ResetCustomItemPreviewState(*pItem);
				continue;
			}
			const ESettingsResourcePriority UploadPriority = pItem->m_PreviewHighPriority ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::BACKGROUND;
			const int MaxUploadsForFrame = SettingsResourceFrameStageBudget(UploadFrameContext, UploadPriority, MaxPreviewUploadsPerFrame, 0);
			if(UploadedPreviewsThisFrame >= MaxUploadsForFrame)
			{
				UploadBlocked = true;
				UploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
				vReadyQueue.push_front(Handle);
				vReadyQueued.insert(SettingsAssetPreviewHandleKey(Handle));
				break;
			}
			const size_t ItemBytes = pItem->m_PreviewBytes;
			if(!SettingsResourceUploadWithinByteBudget(UploadedPreviewsThisFrame, UploadedBytesThisFrame, ItemBytes, MaxPreviewUploadBytesPerFrame))
			{
				if(!SettingsResourceOversizedUploadAllowed(UploadFrameContext, m_SettingsPageSwitchActive, UploadPriority, OversizedUploadsThisFrame, ItemBytes, MaxPreviewUploadBytesPerFrame))
				{
					UploadBlocked = true;
					UploadBlockReason = ItemBytes > MaxPreviewUploadBytesPerFrame ?
								    ESettingsWarmupMissReason::OVERSIZED_UPLOAD_DEFERRED :
								    ESettingsWarmupMissReason::UPLOAD_BYTES_BUDGET;
					vReadyQueue.push_front(Handle);
					vReadyQueued.insert(SettingsAssetPreviewHandleKey(Handle));
					break;
				}
			}
			if(!SettingsResourceConsumeSharedHeavyBudget(RemainingHeavyResourceBatches))
			{
				UploadBlocked = true;
				UploadBlockReason = ESettingsWarmupMissReason::SHARED_HEAVY_BUDGET;
				vReadyQueue.push_front(Handle);
				vReadyQueued.insert(SettingsAssetPreviewHandleKey(Handle));
				break;
			}
			CPerfTimer UploadBatchTimer;
			const bool PreviewWasResized = pItem->m_PreviewResized;
			const SSettingsAssetPreviewHandle UploadHandle = Handle;
			const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pItem->m_aName, s_CurCustomTab, false, Graphics()->ScreenHiDPIScale(), TextureWidth);
			gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey).m_UploadPending = true;
			Graphics()->UnloadTexture(&pItem->m_RenderTexture);
			pItem->m_PreviewResidentBytes = 0;
			gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(
				PreviewKey,
				std::move(pItem->m_PreviewImage),
				[&, UploadHandle](bool TextureValid, IGraphics::CTextureHandle Texture) {
					SCustomItem *pUploadItem = FindCustomItemByPreviewHandle(UploadHandle);
					if(pUploadItem == nullptr || pUploadItem->m_PreviewEpoch != UploadHandle.m_Epoch)
					{
						if(Texture.IsValid())
							Graphics()->UnloadTexture(&Texture);
						return;
					}
					if(TextureValid && Texture.IsValid())
					{
						if(pUploadItem->m_RenderTexture.IsValid())
							Graphics()->UnloadTexture(&pUploadItem->m_RenderTexture);
						pUploadItem->m_RenderTexture = Texture;
					}
					pUploadItem->m_PreviewResidentBytes = TextureValid ? PreviewTextureSizeBytesEstimate(pUploadItem->m_PreviewRequestedTextureSize) : 0;
					pUploadItem->m_PreviewBytes = 0;
					pUploadItem->m_PreviewState = TextureValid ? CMenus::SCustomItem::PREVIEW_STATE_LOADED : CMenus::SCustomItem::PREVIEW_STATE_FAILED;
					pUploadItem->m_PreviewHighPriority = false;
					pUploadItem->m_PreviewResized = false;
				},
				pItem->m_aName);
			if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(PreviewUploadBudget, PreviewUploadTelemetry, gs_SettingsAssetsResourcePreviewCache, Graphics()))
			{
				UploadBlocked = true;
				UploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
				++RemainingHeavyResourceBatches;
				break;
			}
			char aFinalizeUploadExtra[160];
			str_format(aFinalizeUploadExtra, sizeof(aFinalizeUploadExtra), "tab=%d asset=%s bytes=%u",
				s_CurCustomTab, pItem->m_aName, (unsigned)ItemBytes);
			LogAssetsPerfStageForClient(Client(), "assets_finalize_load_texture_raw_move", UploadBatchTimer.ElapsedMs(), false, aFinalizeUploadExtra);
			UploadedBytesThisFrame += ItemBytes;
			++UploadedPreviewsThisFrame;
			if(ItemBytes > MaxPreviewUploadBytesPerFrame)
				++OversizedUploadsThisFrame;
			if(PreviewWasResized)
				++ResizedPreviewsThisFrame;
			char aUploadExtra[192];
			str_format(aUploadExtra, sizeof(aUploadExtra), "tab=%d asset=%s uploads_this_frame=%d bytes=%u bytes_used=%u bytes_budget=%u oversized=%d frame_context=%s jump_scroll=%d priority=%s queue_remaining=%d resized=%d",
				s_CurCustomTab, pItem->m_aName, UploadedPreviewsThisFrame, (unsigned)ItemBytes,
				(unsigned)UploadedBytesThisFrame, (unsigned)MaxPreviewUploadBytesPerFrame, ItemBytes > MaxPreviewUploadBytesPerFrame ? 1 : 0,
				AssetsUploadBlockFrameContextName(UploadFrameContext), UploadFrameContext.m_JumpScrollActive ? 1 : 0, AssetsResourcePriorityName(UploadPriority),
				(int)vReadyQueue.size(), PreviewWasResized ? 1 : 0);
			LogAssetsFramePerfStage("assets_preview_gpu_upload_batch", 0.0, true, aUploadExtra);
		}
		if(!vReadyQueue.empty() && !UploadBlocked && UploadedPreviewsThisFrame >= MaxPreviewUploadsPerFrame)
		{
			UploadBlocked = true;
			UploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
		}
		LogSettingsResourcePerf(SETTINGS_ASSETS, "upload", UploadedPreviewsThisFrame, MaxPreviewUploadsPerFrame, (int)vReadyQueue.size(), UploadBlocked ? UploadBlockReason : ESettingsWarmupMissReason::NONE, 0.0);
		char aDrainExtra[256];
		str_format(aDrainExtra, sizeof(aDrainExtra), "tab=%d processed=%d bytes_budget=%u queue_remaining=%d bytes_used=%u scroll_upload_cooldown=%d frame_context=%s upload_block=%s",
			s_CurCustomTab, UploadedPreviewsThisFrame, (unsigned)MaxPreviewUploadBytesPerFrame, (int)vReadyQueue.size(),
			(unsigned)UploadedBytesThisFrame, s_AssetsScrollUploadCooldownFrames,
			AssetsUploadBlockFrameContextName(UploadFrameContext), pAssetsUploadBlockFrameContext);
		LogAssetsFramePerfStage("assets_preview_upload_queue_drain", 0.0, true, aDrainExtra);
	};

	if(!UsesCombinedAssetList(pCurrentCategory))
	{
		static CListBox s_ListBox;
		s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);
		const CUIRect StableCustomList = AssetsCardListAreaWithStableScrollbar(CustomList, s_ListBox.ScrollbarWidthMax(), s_ListBox.ScrollbarMargin());
		const int LocalColumns = maximum(1, (int)(StableCustomList.w / (Margin + TextureWidth)));
		OldSelected = SelectedCustomAssetIndex(s_CurCustomTab, SearchListSize);
		if(s_AssetsResetListScrollOnTabSwitch)
		{
			s_ListBox.ResetScroll();
			s_AssetsResetListScrollOnTabSwitch = false;
		}
		s_ListBox.DoStart(TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin, SearchListSize, LocalColumns, 1, OldSelected, &CustomList, false);
		static std::vector<CButtonContainer> s_vLocalDeleteButtons;
		s_vLocalDeleteButtons.resize(SearchListSize);
		static char s_aPendingDeleteName[IO_MAX_PATH_LENGTH] = "";
		static CUi::SConfirmPopupContext s_DeleteConfirmPopup;
		bool DeleteLocalRequested = false;
		char aDeleteLocalName[IO_MAX_PATH_LENGTH] = "";
		int FirstVisibleIndex = -1;
		int LastVisibleIndex = -1;
		const float LocalRowHeight = TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin;
		const SSettingsSkinListVisibleRange LocalVisibleRange = SettingsSkinListVisibleRangeForScroll(
			s_ListBox.ScrollOffsetY(), s_ListBox.ViewHeight(), LocalRowHeight, LocalColumns, (int)SearchListSize, 1);
		SResourcePreviewTelemetry LocalResourcePreviewTelemetry;
		DrainAssetsCardMetadataHydrationRequests(AssetsMetadataLayoutTokensThisFrame, LocalResourcePreviewTelemetry);
		SSettingsAssetsCardHydrationScheduler LocalCardHydrationScheduler = BeginAssetsCardHydrationFrame(
			AssetsShellOnlyFrame, AssetsTabSwitchCooldownActive, LocalVisibleRange.m_RenderedItems, AssetsMetadataLayoutTokensThisFrame, AssetsPreviewArtifactTokensThisFrame);
		CSettingsResourcePreviewScheduler LocalPreviewPipelineScheduler;
		LocalPreviewPipelineScheduler.BeginFrame(AssetsPreviewArtifactTokensThisFrame, LocalColumns, AssetsScrollPressure ? 0 : AdaptiveBudget.m_BackgroundTokens, AssetsTextureUploadTokensThisFrame);
		LocalPreviewPipelineScheduler.SetShellOnlyFrame(AssetsShellOnlyFrame);
		const float LocalCardWidth = maximum(1.0f, StableCustomList.w / (float)LocalColumns - Margin);
		auto PrepareAssetsLocalVisibleContentBudgeted = [&]() {
			const bool ShowLocalOnlyBadge = pCurrentCategory != nullptr && pCurrentCategory->m_LocalOnlyBadge && !pCurrentCategory->m_WorkshopEnabled;
			for(int VisibleIndex = LocalVisibleRange.m_FirstItem; VisibleIndex < LocalVisibleRange.m_EndItem; ++VisibleIndex)
			{
				const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, VisibleIndex);
				if(pItem == nullptr)
					continue;
				const char *pLocalStatusLabel = ResolveLocalAssetStatusLabel(pItem, ShowLocalOnlyBadge);
				const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(pItem->m_aName, s_CurCustomTab, Graphics()->ScreenHiDPIScale(), LocalCardWidth, pLocalStatusLabel, true, false, ShowLocalOnlyBadge);
				if(FindAssetsCardMetadata(CardCacheKey) == nullptr)
					RequestAssetsCardMetadataHydration(CardCacheKey, AssetCardDisplayName(pItem), pItem->m_aAuthor[0] != '\0' ? pItem->m_aAuthor : "--", pLocalStatusLabel, true, false, ShowLocalOnlyBadge, LocalCardHydrationScheduler, LocalPreviewPipelineScheduler, LocalResourcePreviewTelemetry, true);
				if(AssetsContentWarmupBlocked)
					continue;
				if(s_CurCustomTab != ASSETS_TAB_ENTITY_BG || pItem->m_RenderTexture.IsValid())
					continue;
				const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pItem->m_aName, s_CurCustomTab, false, Graphics()->ScreenHiDPIScale(), TextureWidth);
				const SResourcePreviewState *pPipelineState = gs_SettingsAssetsResourcePreviewCache.Find(PreviewKey);
				if(pPipelineState != nullptr && (pPipelineState->m_Texture.IsValid() || pPipelineState->m_PreviewJobPending || pPipelineState->m_UploadPending))
					continue;
				if(!LocalPreviewPipelineScheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::VISIBLE))
					break;
				const int ArtifactTextureSize = SettingsAssetPreviewBudgetedTextureSize(
					LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
					ASSET_PREVIEW_MIN_TEXTURE_SIZE,
					PreviewBudgetBytes,
					TextureMemoryUsageBytes,
					pItem->m_PreviewResidentBytes);
				if(StartAssetsEntityBgPreviewArtifactJob(PreviewKey, pItem->m_aName, Storage(), ArtifactTextureSize, Engine(), LocalResourcePreviewTelemetry))
					++LocalResourcePreviewTelemetry.m_PreviewAdmissions;
			}
		};
		PrepareAssetsLocalVisibleContentBudgeted();
		s_ListBox.SkipItems(LocalVisibleRange.m_FirstItem);
		for(size_t i = LocalVisibleRange.m_FirstItem; i < (size_t)LocalVisibleRange.m_EndItem; ++i)
		{
			const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
			if(pItem == nullptr)
				continue;

			const CListboxItem Item = s_ListBox.DoNextItem(pItem, OldSelected >= 0 && (size_t)OldSelected == i, AssetCardRadius + Margin / 2.0f);
			CUIRect ItemRect = Item.m_Rect;
			ItemRect.Margin(Margin / 2, &ItemRect);
			if(!Item.m_Visible)
				continue;
			auto GaussianBlurSuppression = Item.SuppressGaussianBlur();
			if(FirstVisibleIndex < 0)
				FirstVisibleIndex = (int)i;
			LastVisibleIndex = (int)i;

			const CUIRect CardRect = ItemRect;
			const bool HasDeleteButton = CanDeleteLocalAssetByTab(Storage(), s_CurCustomTab, pItem->m_aName);
			const bool ShowLocalOnlyBadge = pCurrentCategory != nullptr && pCurrentCategory->m_LocalOnlyBadge && !pCurrentCategory->m_WorkshopEnabled;
			const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, false, false);
			const char *pLocalStatusLabel = ResolveLocalAssetStatusLabel(pItem, ShowLocalOnlyBadge);
			const SSettingsAssetsCardShell Shell = LayoutAssetsCardShell(CardRect, HasDeleteButton, pLocalStatusLabel, ShowLocalOnlyBadge, ShowAuthorRow);
			RenderAssetsCardShell(Shell);
			const bool CombinedVisible = i >= (size_t)LocalVisibleRange.m_FirstItem && i < (size_t)LocalVisibleRange.m_EndItem;
			const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(pItem->m_aName, s_CurCustomTab, Graphics()->ScreenHiDPIScale(), CardRect.w, pLocalStatusLabel, true, false, ShowLocalOnlyBadge);
			SSettingsAssetsCardMetadataCacheEntry *pMetadata = FindAssetsCardMetadata(CardCacheKey);
			(void)CombinedVisible;
			pMetadata = FindAssetsCardMetadata(CardCacheKey);
			if(pMetadata != nullptr && AssetsContentWarmupBlocked)
				RenderAssetsCardMetadataFallback(Ui(), Shell, pMetadata->m_Title.c_str(), pMetadata->m_Author.c_str(), pMetadata->m_StatusLabel.c_str(), pMetadata->m_Installed || pMetadata->m_LocalOnly, ShowAuthorRow);
			else if(pMetadata != nullptr)
				RenderAssetsCardMetadataCached(Shell, pMetadata, RenderAssetsCardMetadata);
			else if(AssetsRenderCardMetadataFallback)
				RenderAssetsCardMetadataFallback(Ui(), Shell, AssetCardDisplayName(pItem), pItem->m_aAuthor[0] != '\0' ? pItem->m_aAuthor : "--", pLocalStatusLabel, true, ShowAuthorRow);
			SSettingsAssetsCardPreviewState PreviewState;
			const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pItem->m_aName, s_CurCustomTab, false, Graphics()->ScreenHiDPIScale(), CardRect.w);
			SResourcePreviewState &ResourcePreviewState = gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey);
			PreviewState.m_Texture = pItem->m_RenderTexture;
			if(!PreviewState.m_Texture.IsValid())
			{
				const SResourcePreviewState *pPipelineState = gs_SettingsAssetsResourcePreviewCache.Find(PreviewKey);
				if(pPipelineState != nullptr && pPipelineState->m_Texture.IsValid())
					PreviewState.m_Texture = pPipelineState->m_Texture;
			}
			const ESettingsResourcePreviewDrawResult PipelineDrawResult = SettingsResourcePreviewDrawResult(ResourcePreviewState);
			const bool PreviewReady = PipelineDrawResult == ESettingsResourcePreviewDrawResult::READY_TEXTURE || PreviewState.m_Texture.IsValid();
			ResourcePreviewState.m_TextureReady = PreviewReady;
			if(PreviewReady)
			{
				++LocalResourcePreviewTelemetry.m_ReadyTextureCount;
			}
			else
			{
				++LocalResourcePreviewTelemetry.m_PlaceholderCount;
				PreviewState.m_Loading = true;
			}
			RenderAssetsCardPreview(Shell, PreviewState, false, LocalCardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady));
			if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady)
			{
				if(IsEntityBgVideoAsset(pItem->m_aName))
					RenderEntityBgVideoFallback(Shell.m_TextureRect);
				else
					RenderEntityBgFallback(Shell.m_TextureRect);
			}

			if(HasDeleteButton)
			{
				if(Ui()->DoButton_FontIcon(&s_vLocalDeleteButtons[i], FONT_ICON_TRASH, 0, &Shell.m_ActionButtonRect, IGraphics::CORNER_ALL))
				{
					DeleteLocalRequested = true;
					str_copy(aDeleteLocalName, pItem->m_aName, sizeof(aDeleteLocalName));
				}
			}
		}
		s_ListBox.SkipItems((int)SearchListSize - LocalVisibleRange.m_EndItem);
		char aLocalListFrameExtra[192];
		str_format(aLocalListFrameExtra, sizeof(aLocalListFrameExtra), "tab=%d items_total=%d items_rendered=%d items_skipped=%d rows_total=%d rows_visible=%d first=%d end=%d",
			s_CurCustomTab, LocalVisibleRange.m_TotalItems, LocalVisibleRange.m_RenderedItems, LocalVisibleRange.m_SkippedItems,
			LocalVisibleRange.m_TotalRows, LocalVisibleRange.m_VisibleRows, LocalVisibleRange.m_FirstItem, LocalVisibleRange.m_EndItem);
		LogAssetsFramePerfStage("assets_local_list_frame", 0.0, true, aLocalListFrameExtra);
		const int NewSelected = s_ListBox.DoEnd();
		const bool ListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
		if(ListScrollActive)
		{
			StartSettingsPerfScrollWindow("assets_grid_scroll", SettingsPerfContextName(), "settings:assets", "none");
			SQmMenuUiFramePerf MenuUiPerf;
			MenuUiPerf.m_pPage = "settings:assets";
			MenuUiPerf.m_pOperation = "assets_grid_scroll";
			MenuUiPerf.m_ItemsTotal = LocalVisibleRange.m_TotalItems;
			MenuUiPerf.m_ItemsVisible = LocalVisibleRange.m_RenderedItems;
			MenuUiPerf.m_ItemsProcessed = LocalVisibleRange.m_RenderedItems;
			MenuUiPerf.m_ItemsSkipped = LocalVisibleRange.m_SkippedItems;
			MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - AssetsUiBudgetStartTime).count() : -1.0f;
			MenuUiPerf.m_CacheHits = gs_SettingsAssetsCardMetadataCacheHits;
			MenuUiPerf.m_CacheMisses = gs_SettingsAssetsCardMetadataCacheMisses;
			MenuUiPerf.m_CacheEvictions = gs_SettingsAssetsCardMetadataCacheEvictions;
			QmLogMenuUiFramePerf(MenuUiPerf, Client());
		}
		const int PreviousFirstVisibleIndex = s_AssetsLastFirstVisibleIndex[s_CurCustomTab];
		const int PreviousLastVisibleIndex = s_AssetsLastLastVisibleIndex[s_CurCustomTab];
		const int VisibleJumpThreshold = maximum(1, LocalColumns) * 2;
		const bool ListJumpScrollActive =
			FirstVisibleIndex >= 0 && PreviousFirstVisibleIndex >= 0 &&
			(abs(FirstVisibleIndex - PreviousFirstVisibleIndex) >= VisibleJumpThreshold ||
				abs(LastVisibleIndex - PreviousLastVisibleIndex) >= VisibleJumpThreshold);
		RefreshAssetsScrollUploadCooldownForOffset(ListScrollActive, s_ListBox.ScrollOffsetY(), s_aAssetsLastLocalScrollOffsetY[s_CurCustomTab], LocalRowHeight, ListJumpScrollActive);
		s_AssetsLastFirstVisibleIndex[s_CurCustomTab] = FirstVisibleIndex;
		s_AssetsLastLastVisibleIndex[s_CurCustomTab] = LastVisibleIndex;
		m_SettingsScrollActive = m_SettingsScrollActive || ListScrollActive;
		s_AssetsScrollActiveLastFrame = ListScrollActive;
		const SSettingsResourceFrameContext PreviewUploadFrameContext = SettingsBuildFrameContext(
			s_AssetsScrollCooldownFrames > 0, ListScrollActive, ListJumpScrollActive, s_AssetsPostScrollRecoveryFrames);
		RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(
			RemainingHeavyResourceBatches, PreviewUploadFrameContext, 4, 1);
		if(!AssetsContentWarmupBlocked && FirstVisibleIndex >= 0)
		{
			SchedulePreviewRange(FirstVisibleIndex, LastVisibleIndex, LocalColumns);
			char aVisibleExtra[160];
			str_format(aVisibleExtra, sizeof(aVisibleExtra), "tab=%d first=%d last=%d starts=%d",
				s_CurCustomTab, FirstVisibleIndex, LastVisibleIndex, PreviewDecodeStartsThisFrame);
			LogAssetsFramePerfStage("assets_preview_decode_start_visible", 0.0, PreviewDecodeStartsThisFrame > 0, aVisibleExtra);
		}
		if(!AssetsContentWarmupBlocked)
		{
			FinalizeReadyPreviewDecodes(PreviewUploadFrameContext);
			DrainReadyPreviewUploadsAfterList(PreviewUploadFrameContext);
		}
		auto ResetSelectedAssetToDefault = [&](const char *pDeletedName) {
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES && str_comp(g_Config.m_ClAssetsEntities, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetsEntities, "default");
				GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME && str_comp(g_Config.m_ClAssetGame, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetGame, "default");
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS && str_comp(g_Config.m_ClAssetEmoticons, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetEmoticons, "default");
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES && str_comp(g_Config.m_ClAssetParticles, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetParticles, "default");
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD && str_comp(g_Config.m_ClAssetHud, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetHud, "default");
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR && str_comp(g_Config.m_ClAssetGuiCursor, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetGuiCursor, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW && str_comp(g_Config.m_ClAssetArrow, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetArrow, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
			}
			else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK && str_comp(g_Config.m_ClAssetStrongWeak, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetStrongWeak, "default");
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && IsEntityBgConfigSelected(pDeletedName))
			{
				g_Config.m_ClBackgroundEntities[0] = '\0';
				GameClient()->m_Background.LoadBackground();
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS && str_comp(g_Config.m_ClAssetExtras, pDeletedName) == 0)
			{
				str_copy(g_Config.m_ClAssetExtras, "default");
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
		};

		if(DeleteLocalRequested)
		{
			str_copy(s_aPendingDeleteName, aDeleteLocalName, sizeof(s_aPendingDeleteName));
			s_DeleteConfirmPopup.Reset();
			s_DeleteConfirmPopup.YesNoButtons();
			str_copy(s_DeleteConfirmPopup.m_aMessage, Localize("Are you sure you want to delete this asset?"));
			Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_DeleteConfirmPopup);
		}

		if(s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			if(DeleteLocalAssetByTab(Storage(), s_CurCustomTab, s_aPendingDeleteName))
			{
				ResetSelectedAssetToDefault(s_aPendingDeleteName);
				ClearCustomItems(s_CurCustomTab);
			}
			else
			{
				dbg_msg("assets", "failed to delete local asset '%s' in tab %d", s_aPendingDeleteName, s_CurCustomTab);
			}
			s_DeleteConfirmPopup.Reset();
			s_aPendingDeleteName[0] = '\0';
		}
		else if(s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
		{
			s_DeleteConfirmPopup.Reset();
			s_aPendingDeleteName[0] = '\0';
		}

		if(!DeleteLocalRequested && s_DeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && s_ListBox.WasItemSelected() && NewSelected >= 0 && OldSelected != NewSelected)
		{
			const SCustomItem *pSelectedItem = GetCustomItem(s_CurCustomTab, NewSelected);
			if(pSelectedItem->m_aName[0] != '\0')
			{
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
				{
					const auto *pEntityBgItem = static_cast<const SCustomEntityBg *>(pSelectedItem);
					if(pEntityBgItem->m_IsDirectory)
					{
						if(str_comp(pSelectedItem->m_aName, "..") == 0)
						{
							char aParentFolder[IO_MAX_PATH_LENGTH];
							BuildEntityBgParentFolder(m_aEntityBgCurrentFolder, aParentFolder, sizeof(aParentFolder));
							str_copy(m_aEntityBgCurrentFolder, aParentFolder, sizeof(m_aEntityBgCurrentFolder));
						}
						else
						{
							str_copy(m_aEntityBgCurrentFolder, pSelectedItem->m_aName, sizeof(m_aEntityBgCurrentFolder));
						}
						RefreshEntityBgHierarchyView();
						return;
					}
				}

				if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
				{
					str_copy(g_Config.m_ClAssetsEntities, pSelectedItem->m_aName);
					GameClient()->m_MapImages.ChangeEntitiesPath(pSelectedItem->m_aName);
				}
				else if(s_CurCustomTab == ASSETS_TAB_GAME)
				{
					str_copy(g_Config.m_ClAssetGame, pSelectedItem->m_aName);
					GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				{
					str_copy(g_Config.m_ClAssetEmoticons, pSelectedItem->m_aName);
					GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
				}
				else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				{
					str_copy(g_Config.m_ClAssetParticles, pSelectedItem->m_aName);
					GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
				}
				else if(s_CurCustomTab == ASSETS_TAB_HUD)
				{
					str_copy(g_Config.m_ClAssetHud, pSelectedItem->m_aName);
					GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
				}
				else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
				{
					str_copy(g_Config.m_ClAssetGuiCursor, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
				}
				else if(s_CurCustomTab == ASSETS_TAB_ARROW)
				{
					str_copy(g_Config.m_ClAssetArrow, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
				}
				else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
				{
					str_copy(g_Config.m_ClAssetStrongWeak, pSelectedItem->m_aName);
					GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
				}
				else if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
				{
					ApplyEntityBgSelection(GameClient(), pSelectedItem->m_aName);
				}
				else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
				{
					str_copy(g_Config.m_ClAssetExtras, pSelectedItem->m_aName);
					GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
				}
			}
		}
	}

	if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory) && WorkshopHudView.h > 0.0f)
	{
		SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab);
		dbg_assert(pWorkshopState != nullptr, "workshop asset state must exist");
		SWorkshopHudState &WorkshopState = *pWorkshopState;
		const char *pInstallFolder = pCategory->m_pInstallFolder;

		auto IsLocalAssetSelected = [&](const char *pName) {
			switch(s_CurCustomTab)
			{
			case ASSETS_TAB_HUD: return str_comp(pName, g_Config.m_ClAssetHud) == 0;
			case ASSETS_TAB_GAME: return str_comp(pName, g_Config.m_ClAssetGame) == 0;
			case ASSETS_TAB_EMOTICONS: return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
			case ASSETS_TAB_PARTICLES: return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
			case ASSETS_TAB_GUI_CURSOR: return str_comp(pName, g_Config.m_ClAssetGuiCursor) == 0;
			case ASSETS_TAB_ARROW: return str_comp(pName, g_Config.m_ClAssetArrow) == 0;
			case ASSETS_TAB_STRONG_WEAK: return str_comp(pName, g_Config.m_ClAssetStrongWeak) == 0;
			case ASSETS_TAB_ENTITY_BG: return IsEntityBgConfigSelected(pName);
			case ASSETS_TAB_EXTRAS: return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
			case ASSETS_TAB_ENTITIES: return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
			default: return false;
			}
		};

		auto ApplyLocalAssetSelection = [&](const char *pName) {
			switch(s_CurCustomTab)
			{
			case ASSETS_TAB_HUD:
				str_copy(g_Config.m_ClAssetHud, pName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
				break;
			case ASSETS_TAB_GAME:
				str_copy(g_Config.m_ClAssetGame, pName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
				break;
			case ASSETS_TAB_EMOTICONS:
				str_copy(g_Config.m_ClAssetEmoticons, pName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
				break;
			case ASSETS_TAB_PARTICLES:
				str_copy(g_Config.m_ClAssetParticles, pName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
				break;
			case ASSETS_TAB_GUI_CURSOR:
				str_copy(g_Config.m_ClAssetGuiCursor, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_CURSOR, "gui_cursor", g_Config.m_ClAssetGuiCursor);
				break;
			case ASSETS_TAB_ARROW:
				str_copy(g_Config.m_ClAssetArrow, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_ARROW, "arrow", g_Config.m_ClAssetArrow);
				break;
			case ASSETS_TAB_STRONG_WEAK:
				str_copy(g_Config.m_ClAssetStrongWeak, pName);
				GameClient()->ReloadNamedSingleFileAssetImage(IMAGE_STRONGWEAK, "strong_weak", g_Config.m_ClAssetStrongWeak);
				break;
			case ASSETS_TAB_ENTITY_BG:
				ApplyEntityBgSelection(GameClient(), pName);
				break;
			case ASSETS_TAB_EXTRAS:
				str_copy(g_Config.m_ClAssetExtras, pName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
				break;
			case ASSETS_TAB_ENTITIES:
				str_copy(g_Config.m_ClAssetsEntities, pName);
				GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
				break;
			default:
				break;
			}
		};

		auto StartWorkshopListTask = [&]() {
			auto pTask = HttpGet(WORKSHOP_ASSETS_URL);
			pTask->Timeout(CTimeout{10000, 20000, 200, 10});
			pTask->LogProgress(HTTPLOG::FAILURE);
			pTask->FailOnErrorStatus(false);
			WorkshopState.m_pListTask = std::move(pTask);
			WorkshopState.m_LoadFailed = false;
			str_copy(WorkshopState.m_aError, "");
			Http()->Run(WorkshopState.m_pListTask);
		};

		auto StartEntityBgPreviewTask = [&]() {
			auto pTask = HttpGet(ENTITY_BG_PREVIEW_MAP_URL);
			pTask->Timeout(CTimeout{10000, 20000, 200, 10});
			pTask->LogProgress(HTTPLOG::FAILURE);
			pTask->FailOnErrorStatus(false);
			WorkshopState.m_pEntityBgPreviewTask = std::move(pTask);
			Http()->Run(WorkshopState.m_pEntityBgPreviewTask);
		};

		// Load from local cache first for instant display
		if(!WorkshopState.m_Requested && WorkshopState.m_vAssets.empty())
		{
			const char *pCacheFile = GetWorkshopCacheFilename(s_CurCustomTab);
			if(pCacheFile && LoadWorkshopCache(WorkshopState, Storage(), pCacheFile))
			{
				WorkshopState.m_CacheTime = Client()->LocalTime();
				WorkshopState.m_Requested = true; // Mark as loaded from cache
				gs_aInitCustomList[s_CurCustomTab] = true;
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					RefreshEntityBgHierarchyView();
				// Don't set m_LastRefreshTime here, so we won't auto-refresh
			}
		}

		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !WorkshopState.m_EntityBgPreviewRequested && !WorkshopState.m_pEntityBgPreviewTask)
		{
			StartEntityBgPreviewTask();
		}

		// Only start HTTP request if no cache exists (first time or after refresh)
		if(pCategory->m_WorkshopEnabled && !WorkshopState.m_Requested && !WorkshopState.m_pListTask && WorkshopState.m_vAssets.empty())
		{
			StartWorkshopListTask();
		}

		if(WorkshopState.m_pListTask && WorkshopState.m_pListTask->Done())
		{
			bool Parsed = false;
			char aError[sizeof(WorkshopState.m_aError)] = "";
			std::vector<SWorkshopHudAsset> vParsedAssets;
			const EHttpState ListTaskState = WorkshopState.m_pListTask->State();
			if(ListTaskState == EHttpState::DONE)
			{
				if(WorkshopState.m_pListTask->StatusCode() == 200)
				{
					json_value *pJson = WorkshopState.m_pListTask->ResultJson();
					if(pJson)
					{
						Parsed = ParseWorkshopAssets(pJson, *pCategory, vParsedAssets, aError, sizeof(aError));
						json_value_free(pJson);
					}
					else
					{
						str_copy(aError, "Workshop json parse failed", sizeof(aError));
					}
				}
				else
				{
					str_format(aError, sizeof(aError), "Workshop request failed (%d)", WorkshopState.m_pListTask->StatusCode());
				}
			}
			else if(ListTaskState == EHttpState::ABORTED)
			{
				str_copy(aError, "Workshop request aborted", sizeof(aError));
			}
			else
			{
				str_copy(aError, "Workshop request failed", sizeof(aError));
			}

			WorkshopState.m_pListTask.reset();

			if(Parsed)
			{
				// Incremental update: merge new data with existing data
				// Build a map of existing assets by ID for quick lookup
				std::unordered_map<std::string, size_t> ExistingAssetIndexMap;
				for(size_t i = 0; i < WorkshopState.m_vAssets.size(); ++i)
				{
					ExistingAssetIndexMap[WorkshopState.m_vAssets[i].m_Id] = i;
				}

				// Track which existing assets are still present in new data
				std::unordered_set<std::string> NewAssetIds;

				for(SWorkshopHudAsset &NewAsset : vParsedAssets)
				{
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
						NormalizeEntityBgWorkshopAsset(NewAsset, Storage());
					NewAsset.m_Installed = Storage()->FileExists(NewAsset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
					NewAssetIds.insert(NewAsset.m_Id);

					auto It = ExistingAssetIndexMap.find(NewAsset.m_Id);
					if(It != ExistingAssetIndexMap.end())
					{
						// Update existing asset: preserve texture and tasks
						SWorkshopHudAsset &ExistingAsset = WorkshopState.m_vAssets[It->second];
						if(!NewAsset.m_Installed &&
							pCategory->m_Kind == EAssetResourceKind::NAMED_SINGLE_FILE &&
							ExistingAsset.m_Installed &&
							!ExistingAsset.m_InstallPath.empty() &&
							Storage()->FileExists(ExistingAsset.m_InstallPath.c_str(), IStorage::TYPE_SAVE))
						{
							NewAsset.m_Installed = true;
							NewAsset.m_InstallPath = ExistingAsset.m_InstallPath;
							if(!ExistingAsset.m_LocalName.empty())
								NewAsset.m_LocalName = ExistingAsset.m_LocalName;
						}
						NewAsset.m_ThumbTexture = ExistingAsset.m_ThumbTexture;
						ExistingAsset.m_ThumbTexture = IGraphics::CTextureHandle();
						NewAsset.m_ThumbImage = std::move(ExistingAsset.m_ThumbImage);
						NewAsset.m_ThumbBytes = ExistingAsset.m_ThumbBytes;
						NewAsset.m_ThumbResized = ExistingAsset.m_ThumbResized;
						NewAsset.m_ThumbRequestedTextureSize = ExistingAsset.m_ThumbRequestedTextureSize;
						NewAsset.m_ThumbResidentBytes = ExistingAsset.m_ThumbResidentBytes;
						NewAsset.m_ThumbQueuedTier = ExistingAsset.m_ThumbQueuedTier;
						NewAsset.m_ThumbQueuedEpoch = ExistingAsset.m_ThumbQueuedEpoch;
						NewAsset.m_ThumbQueuedTab = ExistingAsset.m_ThumbQueuedTab;
						ExistingAsset.m_ThumbBytes = 0;
						ExistingAsset.m_ThumbResized = false;
						ExistingAsset.m_ThumbRequestedTextureSize = 0;
						ExistingAsset.m_ThumbResidentBytes = 0;
						ExistingAsset.m_ThumbQueuedTier = 0;
						ExistingAsset.m_ThumbQueuedEpoch = 0;
						ExistingAsset.m_ThumbQueuedTab = -1;
						NewAsset.m_pDecodeJob = std::move(ExistingAsset.m_pDecodeJob);
						NewAsset.m_pThumbTask = std::move(ExistingAsset.m_pThumbTask);
						NewAsset.m_pDownloadTask = std::move(ExistingAsset.m_pDownloadTask);
						// Replace the existing asset with updated data
						ExistingAsset = std::move(NewAsset);
						if(ExistingAsset.m_Installed)
							PersistLocalAssetAuthorForWorkshopAsset(s_CurCustomTab, ExistingAsset, Storage());
						continue;
					}
					if(NewAsset.m_Installed)
						PersistLocalAssetAuthorForWorkshopAsset(s_CurCustomTab, NewAsset, Storage());
					// New asset: add to list
					WorkshopState.m_vAssets.push_back(std::move(NewAsset));
				}

				// Remove assets that are no longer in the remote list
				for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
				{
					if(!NewAssetIds.contains(Asset.m_Id))
						ResetWorkshopAssetRuntimeState(Asset, Graphics(), true);
				}
				WorkshopState.m_vAssets.erase(
					std::remove_if(WorkshopState.m_vAssets.begin(), WorkshopState.m_vAssets.end(),
						[&NewAssetIds](const SWorkshopHudAsset &Asset) {
							return !NewAssetIds.contains(Asset.m_Id);
						}),
					WorkshopState.m_vAssets.end());

				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					ApplyEntityBgPreviewThumbUrls(WorkshopState);
				PruneWorkshopDecodeThumbQueue(WorkshopState);
				PruneWorkshopReadyThumbQueue(WorkshopState);

				WorkshopState.m_Requested = true;
				WorkshopState.m_LastRefreshTime = Client()->LocalTime();
				gs_aInitCustomList[s_CurCustomTab] = true;
				if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					RefreshEntityBgHierarchyView();

				// Save to local cache
				const char *pCacheFile = GetWorkshopCacheFilename(s_CurCustomTab);
				if(pCacheFile)
					SaveWorkshopCache(WorkshopState, Storage(), pCacheFile);
				FlushPersistedLocalAssetAuthorsIfDirty(Storage(), s_CurCustomTab);
				RefreshPublishedLocalAssetAuthorsForTab(s_CurCustomTab, Storage());
			}
			else
			{
				// Parsing failed, keep existing data
				WorkshopState.m_Requested = true;
				if(WorkshopState.m_vAssets.empty())
				{
					WorkshopState.m_LoadFailed = true;
					str_copy(WorkshopState.m_aError, aError);
				}
			}
		}

		bool RefreshLocalList = false;

		constexpr int MaxWorkshopThumbDecodeFinalizesPerFrame = 16;
		int MaxWorkshopThumbDecodeFinalizesThisFrame = AssetsUploadBlocked ? 0 : MaxWorkshopThumbDecodeFinalizesPerFrame;
		int MaxWorkshopThumbUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;
		constexpr size_t MaxWorkshopThumbUploadBytesPerFrame = ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME;
		constexpr double MaxWorkshopThumbDecodeFinalizeMsPerFrame = 1.0;
		int WorkshopGpuUploadsThisFrame = 0;
		int WorkshopThumbFinalizesThisFrame = 0;
		int DeferredWorkshopThumbs = 0;
		size_t WorkshopThumbUploadedBytesThisFrame = 0;
		auto RefreshWorkshopAssetsUploadBudget = [&]() {
			RefreshAssetsUploadBudget();
			MaxWorkshopThumbDecodeFinalizesThisFrame = AssetsUploadBlocked ? 0 : MaxWorkshopThumbDecodeFinalizesPerFrame;
			MaxWorkshopThumbUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;
		};

		for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
		{
			if(Asset.m_pThumbTask && Asset.m_pThumbTask->Done())
			{
				const bool ThumbOk = Asset.m_pThumbTask->State() == EHttpState::DONE && Asset.m_pThumbTask->StatusCode() == 200;
				Asset.m_pThumbTask.reset();
				if(ThumbOk && !Asset.m_ThumbTexture.IsValid() && !Asset.m_pDecodeJob)
				{
					if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, Asset.m_ThumbHighPriority))
						continue;
					Asset.m_ThumbRemoteFailed = false;
					Asset.m_ThumbCacheFailed = false;
					Asset.m_ThumbDecodeFromRemote = true;
					StartBackgroundDecode(Asset, Storage(), Engine(), Asset.m_ThumbRequestedTextureSize > 0 ? Asset.m_ThumbRequestedTextureSize : WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
					if(!Asset.m_pDecodeJob)
					{
						Asset.m_ThumbDecodeFromRemote = false;
						Asset.m_ThumbRemoteFailed = true;
					}
					else
						QueueWorkshopDecodeThumb(WorkshopState, Asset, s_CurCustomTab, Client());
				}
				else if(!ThumbOk)
				{
					Asset.m_ThumbRemoteFailed = true;
				}
			}

			if(Asset.m_pDownloadTask && Asset.m_pDownloadTask->Done())
			{
				const bool DownloadOk = Asset.m_pDownloadTask->State() == EHttpState::DONE && Asset.m_pDownloadTask->StatusCode() == 200;
				Asset.m_pDownloadTask.reset();
				Asset.m_DownloadFailed = !DownloadOk;
				if(DownloadOk)
				{
					Asset.m_Installed = true;
					PersistLocalAssetAuthorForWorkshopAsset(s_CurCustomTab, Asset, Storage());
					FlushPersistedLocalAssetAuthorsIfDirty(Storage(), s_CurCustomTab);
					RefreshLocalList = true;
				}
			}
		}
		auto FinalizeWorkshopReadyThumbs = [&](const SSettingsResourceFrameContext &FinalizeFrameContext) {
			if(!WindowActive)
				return;
			RemainingHeavyResourceBatches = std::min(
				RemainingHeavyResourceBatches,
				SettingsResourceSharedHeavyBudget(FinalizeFrameContext, 4, 1));
			CPerfTimer WorkshopDecodeFinalizeTimer;
			constexpr size_t MaxWorkshopThumbDecodePollsPerFrame = 24;
			const size_t PendingWorkshopDecodes = WorkshopState.m_vDecodeThumbQueue.size();
			const size_t WorkshopDecodePolls = minimum(PendingWorkshopDecodes, MaxWorkshopThumbDecodePollsPerFrame);
			for(size_t Poll = 0; Poll < WorkshopDecodePolls; ++Poll)
			{
				const std::string AssetId = WorkshopState.m_vDecodeThumbQueue.front();
				WorkshopState.m_vDecodeThumbQueue.pop_front();
				WorkshopState.m_vDecodeThumbQueued.erase(AssetId);

				SWorkshopHudAsset *pAsset = FindWorkshopAssetById(WorkshopState, AssetId);
				if(pAsset == nullptr || !pAsset->m_pDecodeJob)
					continue;
				if(!pAsset->m_pDecodeJob->IsCompleted())
				{
					QueueWorkshopDecodeThumb(WorkshopState, *pAsset, s_CurCustomTab, Client());
					continue;
				}
				if(pAsset->m_ThumbTexture.IsValid() &&
					SettingsAssetPreviewResidentTextureSatisfiesRequest(
						true,
						pAsset->m_ThumbResidentBytes,
						pAsset->m_ThumbRequestedTextureSize))
				{
					pAsset->m_pDecodeJob.reset();
					continue;
				}

				const ESettingsResourcePriority FinalizePriority = pAsset->m_ThumbHighPriority ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::BACKGROUND;
				const int MaxFinalizesForFrame = SettingsResourceFrameStageBudget(FinalizeFrameContext, FinalizePriority, MaxWorkshopThumbDecodeFinalizesThisFrame, 0);
				if(SettingsAssetPreviewShouldDeferFinalize(WorkshopThumbFinalizesThisFrame, WorkshopDecodeFinalizeTimer.ElapsedMs(), MaxFinalizesForFrame, MaxWorkshopThumbDecodeFinalizeMsPerFrame))
				{
					QueueWorkshopDecodeThumb(WorkshopState, *pAsset, s_CurCustomTab, Client());
					++DeferredWorkshopThumbs;
					continue;
				}
				if(!SettingsResourceConsumeSharedHeavyBudget(RemainingHeavyResourceBatches))
				{
					QueueWorkshopDecodeThumb(WorkshopState, *pAsset, s_CurCustomTab, Client());
					++DeferredWorkshopThumbs;
					continue;
				}

				CPerfTimer DecodeFinalizeBatchTimer;
				CFullAsyncImageLoadJob::SResult Result = pAsset->m_pDecodeJob->GetResult();
				pAsset->m_pDecodeJob.reset();
				if(Result.m_Success && Result.m_Image.m_pData)
				{
					const bool WasHighPriority = pAsset->m_ThumbHighPriority;
					const bool ResizedPreview = Result.m_Resized;
					ResetWorkshopThumbReadyState(*pAsset);
					pAsset->m_ThumbHighPriority = WasHighPriority;
					pAsset->m_ThumbImage = std::move(Result.m_Image);
					pAsset->m_ThumbBytes = pAsset->m_ThumbImage.DataSize();
					if(pAsset->m_ThumbRequestedTextureSize <= 0)
						pAsset->m_ThumbRequestedTextureSize = WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE;
					pAsset->m_ThumbResized = ResizedPreview;
					QueueWorkshopReadyThumb(WorkshopState, *pAsset, s_CurCustomTab, Client());
					++WorkshopThumbFinalizesThisFrame;
					char aDecodeExtra[160];
					str_format(aDecodeExtra, sizeof(aDecodeExtra), "tab=%d asset=%s resized=%d finalized=%d w=%u h=%u",
						s_CurCustomTab, pAsset->m_Name.c_str(), ResizedPreview ? 1 : 0, WorkshopThumbFinalizesThisFrame,
						(unsigned)pAsset->m_ThumbImage.m_Width, (unsigned)pAsset->m_ThumbImage.m_Height);
					LogAssetsFramePerfStage("assets_workshop_thumb_decode_finalize_batch", DecodeFinalizeBatchTimer.ElapsedMs(), false, aDecodeExtra);
				}
				else
				{
					Result.m_Image.Free();
					const bool DecodeFromRemote = pAsset->m_ThumbDecodeFromRemote;
					pAsset->m_ThumbDecodeFromRemote = false;
					pAsset->m_ThumbCacheFailed = true;
					if(!pAsset->m_ThumbCachePath.empty())
					{
						Storage()->RemoveFile(pAsset->m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
					}
					if(DecodeFromRemote)
						pAsset->m_ThumbRemoteFailed = true;
				}
			}
		};
		auto DrainWorkshopReadyThumbUploads = [&](const SSettingsResourceFrameContext &UploadFrameContext) {
			if(!WindowActive)
				return;
			RemainingHeavyResourceBatches = std::min(
				RemainingHeavyResourceBatches,
				SettingsResourceSharedHeavyBudget(UploadFrameContext, 4, 1));
			SSettingsResourceMergeBudget UploadBudget;
			UploadBudget.m_MaxGpuUploads = MaxWorkshopThumbUploadsPerFrame - WorkshopGpuUploadsThisFrame;
			SResourcePreviewUploadBudget WorkshopPreviewUploadBudget;
			WorkshopPreviewUploadBudget.m_MaxUploads = MaxWorkshopThumbUploadsPerFrame - WorkshopGpuUploadsThisFrame;
			WorkshopPreviewUploadBudget.m_pMergeBudget = &UploadBudget;
			WorkshopPreviewUploadBudget.m_pFrameBudget = SettingsFrameBudget();
			WorkshopPreviewUploadBudget.m_pGpuUploadLimiter = GameClient()->GpuUploadLimiter();
			SResourcePreviewTelemetry WorkshopPreviewUploadTelemetry;
			int WorkshopOversizedUploadsThisFrame = 0;
			ESettingsWarmupMissReason WorkshopUploadBlockReason = ESettingsWarmupMissReason::NONE;
			bool WorkshopUploadBlocked = false;
			WorkshopGpuUploadsThisFrame += DrainSharedResourcePreviewUploadQueue(MaxWorkshopThumbUploadsPerFrame - WorkshopGpuUploadsThisFrame, WorkshopPreviewUploadTelemetry);
			if(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() > 0)
			{
				WorkshopUploadBlocked = true;
				WorkshopUploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
			}
			while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !WorkshopState.m_vReadyThumbQueue.empty() && WorkshopGpuUploadsThisFrame < MaxWorkshopThumbUploadsPerFrame)
			{
				const std::string ReadyAssetId = WorkshopState.m_vReadyThumbQueue.front();
				WorkshopState.m_vReadyThumbQueue.pop_front();
				WorkshopState.m_vReadyThumbQueued.erase(ReadyAssetId);

				SWorkshopHudAsset *pAsset = FindWorkshopAssetById(WorkshopState, ReadyAssetId);
				if(pAsset == nullptr)
					continue;
				if(pAsset->m_ThumbTexture.IsValid())
				{
					ResetWorkshopThumbReadyState(*pAsset);
					continue;
				}
				if(pAsset->m_ThumbImage.m_pData == nullptr)
					continue;

				CPerfTimer UploadBatchTimer;
				const size_t AssetBytes = pAsset->m_ThumbBytes;
				const ESettingsResourcePriority UploadPriority = pAsset->m_ThumbHighPriority ? ESettingsResourcePriority::VISIBLE : ESettingsResourcePriority::BACKGROUND;
				const int MaxUploadsForFrame = SettingsResourceFrameStageBudget(UploadFrameContext, UploadPriority, MaxWorkshopThumbUploadsPerFrame, 0);
				if(WorkshopGpuUploadsThisFrame >= MaxUploadsForFrame)
				{
					WorkshopUploadBlocked = true;
					WorkshopUploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
					WorkshopState.m_vReadyThumbQueue.push_front(ReadyAssetId);
					WorkshopState.m_vReadyThumbQueued.insert(ReadyAssetId);
					break;
				}
				if(!SettingsResourceUploadWithinByteBudget(WorkshopGpuUploadsThisFrame, WorkshopThumbUploadedBytesThisFrame, AssetBytes, MaxWorkshopThumbUploadBytesPerFrame))
				{
					if(!SettingsResourceOversizedUploadAllowed(UploadFrameContext, m_SettingsPageSwitchActive, UploadPriority, WorkshopOversizedUploadsThisFrame, AssetBytes, MaxWorkshopThumbUploadBytesPerFrame))
					{
						WorkshopUploadBlocked = true;
						WorkshopUploadBlockReason = AssetBytes > MaxWorkshopThumbUploadBytesPerFrame ?
										    ESettingsWarmupMissReason::OVERSIZED_UPLOAD_DEFERRED :
										    ESettingsWarmupMissReason::UPLOAD_BYTES_BUDGET;
						WorkshopState.m_vReadyThumbQueue.push_front(ReadyAssetId);
						WorkshopState.m_vReadyThumbQueued.insert(ReadyAssetId);
						break;
					}
				}
				if(!SettingsResourceConsumeSharedHeavyBudget(RemainingHeavyResourceBatches))
				{
					WorkshopUploadBlocked = true;
					WorkshopUploadBlockReason = ESettingsWarmupMissReason::SHARED_HEAVY_BUDGET;
					WorkshopState.m_vReadyThumbQueue.push_front(ReadyAssetId);
					WorkshopState.m_vReadyThumbQueued.insert(ReadyAssetId);
					break;
				}
				const bool ThumbWasResized = pAsset->m_ThumbResized;
				const std::string UploadAssetId = pAsset->m_Id;
				const unsigned UploadEpoch = pAsset->m_ThumbQueuedEpoch;
				const int UploadTab = pAsset->m_ThumbQueuedTab;
				const int UploadTextureSize = pAsset->m_ThumbRequestedTextureSize;
				const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pAsset->m_Id.empty() ? pAsset->m_Name.c_str() : pAsset->m_Id.c_str(), s_CurCustomTab, true, Graphics()->ScreenHiDPIScale(), TextureWidth);
				gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey).m_UploadPending = true;
				gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(
					PreviewKey,
					std::move(pAsset->m_ThumbImage),
					[&, UploadAssetId, UploadEpoch, UploadTab, UploadTextureSize](bool TextureValid, IGraphics::CTextureHandle Texture) {
						SWorkshopHudState *pUploadState = WorkshopStateByTab(UploadTab);
						SWorkshopHudAsset *pUploadAsset = pUploadState != nullptr ? FindWorkshopAssetById(*pUploadState, UploadAssetId) : nullptr;
						if(pUploadAsset == nullptr || pUploadAsset->m_ThumbQueuedEpoch != UploadEpoch || pUploadAsset->m_ThumbQueuedTab != UploadTab)
						{
							if(Texture.IsValid())
								Graphics()->UnloadTexture(&Texture);
							return;
						}
						if(TextureValid && Texture.IsValid())
						{
							if(pUploadAsset->m_ThumbTexture.IsValid())
								Graphics()->UnloadTexture(&pUploadAsset->m_ThumbTexture);
							pUploadAsset->m_ThumbTexture = Texture;
						}
						pUploadAsset->m_ThumbResidentBytes = TextureValid ? PreviewTextureSizeBytesEstimate(UploadTextureSize) : 0;
						ResetWorkshopThumbReadyState(*pUploadAsset);
					},
					pAsset->m_Name.c_str());
				if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(WorkshopPreviewUploadBudget, WorkshopPreviewUploadTelemetry, gs_SettingsAssetsResourcePreviewCache, Graphics()))
				{
					WorkshopUploadBlocked = true;
					WorkshopUploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
					++RemainingHeavyResourceBatches;
					break;
				}
				WorkshopThumbUploadedBytesThisFrame += AssetBytes;
				++WorkshopGpuUploadsThisFrame;
				if(AssetBytes > MaxWorkshopThumbUploadBytesPerFrame)
					++WorkshopOversizedUploadsThisFrame;
				char aExtra[192];
				str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s uploads_this_frame=%d bytes=%u bytes_used=%u bytes_budget=%u oversized=%d frame_context=%s priority=%s queue_remaining=%d resized=%d",
					s_CurCustomTab, pAsset->m_Name.c_str(), WorkshopGpuUploadsThisFrame, (unsigned)AssetBytes,
					(unsigned)WorkshopThumbUploadedBytesThisFrame, (unsigned)MaxWorkshopThumbUploadBytesPerFrame, AssetBytes > MaxWorkshopThumbUploadBytesPerFrame ? 1 : 0,
					AssetsUploadBlockFrameContextName(UploadFrameContext), AssetsResourcePriorityName(UploadPriority),
					(int)WorkshopState.m_vReadyThumbQueue.size(), ThumbWasResized ? 1 : 0);
				LogAssetsFramePerfStage("assets_workshop_thumb_upload_batch", UploadBatchTimer.ElapsedMs(), false, aExtra);
			}
			if(!WorkshopState.m_vReadyThumbQueue.empty() && !WorkshopUploadBlocked && WorkshopGpuUploadsThisFrame >= MaxWorkshopThumbUploadsPerFrame)
			{
				WorkshopUploadBlocked = true;
				WorkshopUploadBlockReason = ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET;
			}
			LogSettingsResourcePerf(SETTINGS_ASSETS, "upload", WorkshopGpuUploadsThisFrame, MaxWorkshopThumbUploadsPerFrame, (int)WorkshopState.m_vReadyThumbQueue.size(), WorkshopUploadBlocked ? WorkshopUploadBlockReason : ESettingsWarmupMissReason::NONE, 0.0);
			char aWorkshopFinalizeExtra[160];
			str_format(aWorkshopFinalizeExtra, sizeof(aWorkshopFinalizeExtra), "tab=%d finalized=%d deferred=%d ready_queue=%d recovery_frames=%d heavy_batches_left=%d scroll_upload_cooldown=%d frame_context=%s upload_block=%s",
				s_CurCustomTab, WorkshopThumbFinalizesThisFrame, DeferredWorkshopThumbs, (int)WorkshopState.m_vReadyThumbQueue.size(),
				s_AssetsPostScrollRecoveryFrames, RemainingHeavyResourceBatches, s_AssetsScrollUploadCooldownFrames,
				AssetsUploadBlockFrameContextName(UploadFrameContext), pAssetsUploadBlockFrameContext);
			LogAssetsFramePerfStage("assets_workshop_thumb_decode_finalize_total", 0.0, WorkshopThumbFinalizesThisFrame > 0 || DeferredWorkshopThumbs > 0 || !WorkshopState.m_vReadyThumbQueue.empty(), aWorkshopFinalizeExtra);
		};
		if(DeferredWorkshopThumbs > 0)
		{
			char aDeferredExtra[128];
			str_format(aDeferredExtra, sizeof(aDeferredExtra), "tab=%d deferred=%d uploads=%d ready_queue=%d",
				s_CurCustomTab, DeferredWorkshopThumbs, WorkshopGpuUploadsThisFrame, (int)WorkshopState.m_vReadyThumbQueue.size());
			LogAssetsPerfStageForClient(Client(), "assets_workshop_thumb_decode_deferred", 0.0, true, aDeferredExtra);
		}
		if(s_AssetsFocusPerfFrames > 0)
		{
			const char *pFocusFrameContext = AssetsResourceFrameContextName(ResourceFrameContext);
			char aFocusExtra[512];
			str_format(aFocusExtra, sizeof(aFocusExtra),
				"window_active=%d page=%s tab=%s frame_context=%s focus_resume=%d focus_frames_left=%d local_decode_queue=%d local_ready_queue=%d workshop_decode_queue=%d workshop_ready_queue=%d preview_finalize=%d preview_upload=%d workshop_finalize=%d workshop_upload=%d texture_memory_usage=%u resident_preview_bytes=%u workshop_resident_preview_bytes=%u graphics_swap=see_perf_main_thread",
				WindowActive ? 1 : 0, "assets", AssetsSettingsTabName(s_CurCustomTab), pFocusFrameContext,
				1, s_AssetsFocusPerfFrames, (int)m_aaCustomPreviewDecodeQueue[s_CurCustomTab].size(), (int)m_aaCustomPreviewReadyQueue[s_CurCustomTab].size(),
				(int)WorkshopState.m_vDecodeThumbQueue.size(), (int)WorkshopState.m_vReadyThumbQueue.size(),
				PreviewDecodeFinalizesThisFrame, UploadedPreviewsThisFrame, WorkshopThumbFinalizesThisFrame, WorkshopGpuUploadsThisFrame,
				(unsigned)TextureMemoryUsageBytes, (unsigned)ResidentPreviewBytes, (unsigned)WorkshopResidentPreviewBytes);
			LogAssetsPerfStageForClient(Client(), "assets_focus_observation", 0.0, true, aFocusExtra);
			--s_AssetsFocusPerfFrames;
		}
		if(RefreshLocalList)
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			{
				for(const SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
				{
					if(!Asset.m_Installed || Asset.m_LocalName.empty())
						continue;
					if(std::none_of(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), [&Asset](const std::string &Name) { return Name == Asset.m_LocalName; }))
						m_vEntityBgSourceNames.push_back(Asset.m_LocalName);
					if(!m_vEntityBgSourceKinds.contains(Asset.m_LocalName))
						m_vEntityBgSourceKinds.emplace(Asset.m_LocalName, EEntityBgHierarchyEntrySource::WORKSHOP);
				}
				RefreshEntityBgHierarchyView();
			}
			else
			{
				ClearCustomItems(s_CurCustomTab);
			}
			// Local list refresh invalidates SearchListSize and search pointer arrays for this frame.
			return;
		}

		DrawRoundedSurface(Ui(), WorkshopHudView, ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), ColorRGBA(), 8.0f);
		WorkshopHudView.Margin(8.0f, &WorkshopHudView);

		CUIRect WorkshopListArea = WorkshopHudView;
		WorkshopListArea.HSplitTop(4.0f, nullptr, &WorkshopListArea);
		const size_t LocalAssetTotalCount = SearchListSize;
		std::vector<size_t> vVisibleLocalAssetIndices;
		vVisibleLocalAssetIndices.reserve(LocalAssetTotalCount);
		for(size_t LocalIndex = 0; LocalIndex < LocalAssetTotalCount; ++LocalIndex)
		{
			if(GetCustomItem(s_CurCustomTab, LocalIndex) != nullptr)
				vVisibleLocalAssetIndices.push_back(LocalIndex);
		}
		const size_t LocalAssetCount = vVisibleLocalAssetIndices.size();

		const size_t AssetCount = WorkshopState.m_vAssets.size();
		static std::array<std::vector<CButtonContainer>, NUMBER_OF_ASSETS_TABS> s_avWorkshopActionButtons;
		auto &vWorkshopActionButtons = s_avWorkshopActionButtons[s_CurCustomTab];
		vWorkshopActionButtons.resize(AssetCount);

		std::vector<size_t> vVisibleDownloadableAssetIndices;
		vVisibleDownloadableAssetIndices.reserve(AssetCount);
		auto ShouldShowEntityBgWorkshopAssetsInCurrentFolder = [&]() {
			return m_ShowWorkshopAssets && IsEntityBgWorkshopFolderOrChild(m_aEntityBgCurrentFolder);
		};
		auto WorkshopAssetMatchesCurrentEntityBgFolder = [&](const SWorkshopHudAsset &Asset) {
			if(!ShouldShowEntityBgWorkshopAssetsInCurrentFolder())
				return false;
			if(IsEntityBgWorkshopFolderPath(m_aEntityBgCurrentFolder))
				return true;
			const int CurrentFolderLength = str_length(m_aEntityBgCurrentFolder);
			return str_startswith(Asset.m_LocalName.c_str(), m_aEntityBgCurrentFolder) && Asset.m_LocalName[CurrentFolderLength] == '/';
		};
		for(size_t AssetIndex = 0; AssetIndex < AssetCount; ++AssetIndex)
		{
			const SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[AssetIndex];
			if(Asset.m_Installed)
				continue;
			if(!m_ShowWorkshopAssets)
				continue;
			if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !WorkshopAssetMatchesCurrentEntityBgFolder(Asset))
				continue;
			if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() &&
				!SearchFilterMatches(s_aFilterInputs[s_CurCustomTab].GetString(), Asset.m_Name.c_str(), Asset.m_Author.c_str()))
				continue;
			vVisibleDownloadableAssetIndices.push_back(AssetIndex);
		}

		const size_t CombinedCount = LocalAssetCount + vVisibleDownloadableAssetIndices.size();
		if(CombinedCount == 0)
		{
			const CUIRect StableWorkshopListArea = AssetsCardListAreaWithStableScrollbar(WorkshopListArea, 20.0f, 5.0f);
			const int Columns = std::max(1, static_cast<int>(StableWorkshopListArea.w / (Margin + TextureWidth)));
			const float WorkshopRowHeight = TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin;
			const bool AssetsInitialEntryLoading = m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING || WorkshopState.m_pListTask != nullptr;
			if(AssetsInitialEntryLoading)
			{
				RenderAssetsCardLoadingShells(StableWorkshopListArea, WorkshopRowHeight, Columns, true);
				char aGeometryExtra[224];
				str_format(aGeometryExtra, sizeof(aGeometryExtra), "stage=assets_card_geometry tab=%d columns=%d row_height=%.2f first=%d end=%d stable=%d geometry_changed=%d loading_shell=1",
					s_CurCustomTab, Columns, WorkshopRowHeight, 0, 0, 1, 0);
				LogAssetsFramePerfStage("assets_card_geometry", 0.0, true, aGeometryExtra);
			}
			else if(WorkshopState.m_LoadFailed)
			{
				char aText[192];
				str_format(aText, sizeof(aText), "%s: %s", Localize("Failed to load"), WorkshopState.m_aError[0] != '\0' ? WorkshopState.m_aError : "unknown");
				SLabelProperties LabelProps;
				LabelProps.m_MaxWidth = static_cast<int>(WorkshopListArea.w);
				Ui()->DoLabel(&WorkshopListArea, aText, ContentMetrics.m_BodySize, TEXTALIGN_MC, LabelProps);
			}
			else
			{
				DoSettingsMenuLabel(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, "assets-workshop-no-assets", &WorkshopListArea, Localize("No assets"), ContentMetrics.m_BodySize, TEXTALIGN_MC);
			}
		}
		else
		{
			static CListBox s_WorkshopAssetsListBox;
			s_WorkshopAssetsListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);
			const CUIRect StableWorkshopListArea = AssetsCardListAreaWithStableScrollbar(WorkshopListArea, s_WorkshopAssetsListBox.ScrollbarWidthMax(), s_WorkshopAssetsListBox.ScrollbarMargin());
			const int Columns = std::max(1, static_cast<int>(StableWorkshopListArea.w / (Margin + TextureWidth)));
			const int OldCombinedSelected = SelectedCombinedAssetIndex(s_CurCustomTab, vVisibleLocalAssetIndices);
			const float WorkshopRowHeight = TextureHeight + AssetCardTextReserve + AssetCardFooterSpacing + Margin;
			if(s_AssetsResetListScrollOnTabSwitch)
			{
				s_WorkshopAssetsListBox.ResetScroll();
				s_AssetsResetListScrollOnTabSwitch = false;
			}
			s_WorkshopAssetsListBox.DoStart(WorkshopRowHeight, CombinedCount, Columns, 1, OldCombinedSelected, &WorkshopListArea, false, IGraphics::CORNER_ALL);

			static std::vector<CButtonContainer> s_vWorkshopLocalDeleteButtons;
			s_vWorkshopLocalDeleteButtons.resize(LocalAssetTotalCount);

			static char s_aWorkshopPendingDeleteName[IO_MAX_PATH_LENGTH] = "";
			static CUi::SConfirmPopupContext s_WorkshopDeleteConfirmPopup;
			static size_t s_PendingDownloadAssetIndex = SIZE_MAX;
			static CUi::SConfirmPopupContext s_WorkshopDownloadConfirmPopup;

			bool DeleteLocalRequested = false;
			char aDeleteLocalName[IO_MAX_PATH_LENGTH] = "";
			bool DownloadRequested = false;
			size_t RequestedDownloadAssetIndex = SIZE_MAX;
			float RequestedDownloadPopupX = 0.0f;
			float RequestedDownloadPopupY = 0.0f;
			bool WorkshopActionTriggered = false;
			CPerfTimer WorkshopCardsTimer;
			int FirstVisibleLocalIndex = -1;
			int FirstVisibleDownloadableIndex = -1;
			int LastVisibleDownloadableIndex = -1;
			const SSettingsSkinListVisibleRange WorkshopVisibleRange = SettingsSkinListVisibleRangeForScroll(
				s_WorkshopAssetsListBox.ScrollOffsetY(), s_WorkshopAssetsListBox.ViewHeight(), WorkshopRowHeight, Columns, (int)CombinedCount, 1);
			CombinedVisibleAdmission.m_FirstCombined = WorkshopVisibleRange.m_FirstItem;
			CombinedVisibleAdmission.m_EndCombined = WorkshopVisibleRange.m_EndItem;
			CombinedVisibleAdmission.m_LocalCount = (int)LocalAssetCount;
			const int PreviousFirstVisibleCombinedIndex = s_AssetsLastFirstVisibleCombinedIndex[s_CurCustomTab];
			const int PreviousLastVisibleCombinedIndex = s_AssetsLastLastVisibleCombinedIndex[s_CurCustomTab];
			const int WorkshopVisibleJumpThreshold = maximum(1, Columns) * 2;
			const bool WorkshopListJumpScrollActive =
				WorkshopVisibleRange.m_FirstItem < WorkshopVisibleRange.m_EndItem && PreviousFirstVisibleCombinedIndex >= 0 &&
				(abs(WorkshopVisibleRange.m_FirstItem - PreviousFirstVisibleCombinedIndex) >= WorkshopVisibleJumpThreshold ||
					abs((WorkshopVisibleRange.m_EndItem - 1) - PreviousLastVisibleCombinedIndex) >= WorkshopVisibleJumpThreshold);
			const int WorkshopThumbStartLimitThisFrame = WorkshopListJumpScrollActive ? MaxWorkshopThumbJumpStartsPerFrame : MaxWorkshopThumbStartsPerFrameAdaptive;
			auto StartWorkshopThumb = [&](SWorkshopHudAsset &Asset, bool HighPriority) {
				if(HighPriority && CombinedVisibleAdmission.m_VisibleStarts >= VisibleThumbStartLimitThisFrame)
					return false;
				const int TargetTextureSize = SettingsAssetPreviewBudgetedTextureSize(
					WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
					ASSET_PREVIEW_MIN_TEXTURE_SIZE,
					PreviewBudgetBytes,
					TextureMemoryUsageBytes,
					Asset.m_ThumbResidentBytes);
				if(SettingsAssetPreviewResidentTextureSatisfiesRequest(
					   Asset.m_ThumbTexture.IsValid(),
					   Asset.m_ThumbResidentBytes,
					   TargetTextureSize))
					return false;
				if(Asset.m_ThumbQueuedTier == TargetTextureSize && Asset.m_ThumbQueuedEpoch == PreviewEpoch && Asset.m_ThumbQueuedTab == s_CurCustomTab)
					return false;
				if(HighPriority)
					Asset.m_ThumbHighPriority = true;
				if(Asset.m_ThumbImage.m_pData != nullptr)
				{
					Asset.m_ThumbQueuedTier = TargetTextureSize;
					Asset.m_ThumbQueuedEpoch = PreviewEpoch;
					Asset.m_ThumbQueuedTab = s_CurCustomTab;
					QueueWorkshopReadyThumb(WorkshopState, Asset, s_CurCustomTab, Client());
					return false;
				}
				if(Asset.m_pDecodeJob)
				{
					Asset.m_ThumbQueuedTier = TargetTextureSize;
					Asset.m_ThumbQueuedEpoch = PreviewEpoch;
					Asset.m_ThumbQueuedTab = s_CurCustomTab;
					QueueWorkshopDecodeThumb(WorkshopState, Asset, s_CurCustomTab, Client());
					return false;
				}
				if(Asset.m_pThumbTask)
					return false;
				if(!SettingsResourceCanUseHighPriorityBudget(WorkshopThumbStartsThisFrame, WorkshopThumbStartLimitThisFrame, MaxWorkshopThumbHighPriorityStartsPerFrame, HighPriority))
					return false;
				if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, HighPriority))
					return false;

				const bool HasUsableInstalledThumb = WorkshopAssetCanDecodePreviewFromInstall(Asset, s_CurCustomTab);
				const bool HasUsableThumbCache = !Asset.m_ThumbCacheFailed && !Asset.m_ThumbCachePath.empty() && Storage()->FileExists(Asset.m_ThumbCachePath.c_str(), IStorage::TYPE_SAVE);
				if(HasUsableInstalledThumb || HasUsableThumbCache)
				{
					CPerfTimer ThumbStartTimer;
					Asset.m_ThumbDecodeFromRemote = false;
					Asset.m_ThumbQueuedTier = TargetTextureSize;
					Asset.m_ThumbQueuedEpoch = PreviewEpoch;
					Asset.m_ThumbQueuedTab = s_CurCustomTab;
					StartBackgroundDecode(Asset, Storage(), Engine(), TargetTextureSize);
					if(Asset.m_pDecodeJob)
					{
						QueueWorkshopDecodeThumb(WorkshopState, Asset, s_CurCustomTab, Client());
						++WorkshopThumbStartsThisFrame;
						const ESettingsWorkshopBytesSource BytesSource = HasUsableInstalledThumb ? ESettingsWorkshopBytesSource::LOCAL_INSTALL : ESettingsWorkshopBytesSource::LOCAL_THUMB_CACHE;
						char aExtra[240];
						str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s started=%d source=%s catalog_source=%s bytes_source=%s",
							s_CurCustomTab, Asset.m_Name.c_str(), WorkshopThumbStartsThisFrame, HasUsableInstalledThumb ? "installed" : "cache",
							SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource::WORKSHOP_CACHE),
							SettingsWorkshopBytesSourceName(BytesSource));
						LogAssetsPerfStageForClient(Client(), "assets_workshop_thumb_start_local", ThumbStartTimer.ElapsedMs(), false, aExtra);
						return true;
					}
				}

				if(WorkshopThumbSourceUrl(Asset, s_CurCustomTab)[0] == '\0')
					return false;
				if(Asset.m_ThumbRemoteFailed)
					return false;

				CPerfTimer ThumbStartTimer;
				if(!StartWorkshopRemoteThumbRequest(Asset, s_CurCustomTab, PreviewEpoch, TargetTextureSize, WorkshopThumbStartsThisFrame, Storage(), Http()))
					return false;
				char aExtra[240];
				str_format(aExtra, sizeof(aExtra), "tab=%d asset=%s started=%d source=remote catalog_source=%s bytes_source=%s",
					s_CurCustomTab, Asset.m_Name.c_str(), WorkshopThumbStartsThisFrame,
					SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource::WORKSHOP_CACHE),
					SettingsWorkshopBytesSourceName(ESettingsWorkshopBytesSource::REMOTE_THUMB_HTTP));
				LogAssetsPerfStageForClient(Client(), "assets_workshop_thumb_start_remote", ThumbStartTimer.ElapsedMs(), false, aExtra);
				return true;
			};
			double WorkshopCardLayoutTextMs = 0.0;
			double WorkshopCardPreviewDrawMs = 0.0;
			double WorkshopCardThumbSchedulingMs = 0.0;
			int EntityBgHeavyPreviewDeferredCount = 0;
			SResourcePreviewTelemetry ResourcePreviewTelemetry;
			ResourcePreviewTelemetry.m_VisibleCount = WorkshopVisibleRange.m_RenderedItems;
			SSettingsAssetsVisiblePreflight VisiblePreflight;
			CSettingsResourcePreviewScheduler PreviewPipelineScheduler;
			PreviewPipelineScheduler.BeginFrame(AssetsPreviewArtifactTokensThisFrame, Columns, AssetsScrollPressure ? 0 : AdaptiveBudget.m_BackgroundTokens, AssetsTextureUploadTokensThisFrame);
			PreviewPipelineScheduler.SetShellOnlyFrame(AssetsShellOnlyFrame);
			SSettingsAssetsCardHydrationScheduler CardHydrationScheduler = BeginAssetsCardHydrationFrame(AssetsShellOnlyFrame, AssetsTabSwitchCooldownActive, WorkshopVisibleRange.m_RenderedItems, AssetsMetadataLayoutTokensThisFrame, AssetsPreviewArtifactTokensThisFrame);
			const float WorkshopCardWidth = maximum(1.0f, StableWorkshopListArea.w / (float)Columns - Margin);
			auto PrepareAssetsVisibleContentBudgeted = [&]() {
				CPerfTimer ThumbSchedulingTimer;
				VisiblePreflight.m_State = EAssetsVisiblePreflightState::PLANNING;
				VisiblePreflight.m_VisibleCount = maximum(0, WorkshopVisibleRange.m_EndItem - WorkshopVisibleRange.m_FirstItem);
				VisiblePreflight.m_HalfVisibleCount = Columns;
				VisiblePreflight.m_GeometryStable = CombinedCount > 0 && Columns > 0 && WorkshopRowHeight > 0.0f;

				if(WorkshopVisibleRange.m_FirstItem < WorkshopVisibleRange.m_EndItem)
				{
					const int FirstLocalListIndex = WorkshopVisibleRange.m_FirstItem;
					const int LastLocalListIndex = minimum(WorkshopVisibleRange.m_EndItem - 1, (int)LocalAssetCount - 1);
					if(FirstLocalListIndex <= LastLocalListIndex)
					{
						for(int LocalListIndex = FirstLocalListIndex; LocalListIndex <= LastLocalListIndex; ++LocalListIndex)
						{
							const size_t LocalAssetIndex = vVisibleLocalAssetIndices[LocalListIndex];
							const SCustomItem *pLocalItem = GetCustomItem(s_CurCustomTab, LocalAssetIndex);
							if(pLocalItem != nullptr && pLocalItem->m_RenderTexture.IsValid())
								++VisiblePreflight.m_ReadyCount;
							else
								++VisiblePreflight.m_NotReadyCount;
							if(pLocalItem != nullptr)
							{
								const bool ShowLocalOnlyBadge = pCategory->m_LocalOnlyBadge && !pCategory->m_WorkshopEnabled;
								const char *pLocalStatusLabel = ResolveLocalAssetStatusLabel(pLocalItem, ShowLocalOnlyBadge);
								const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(pLocalItem->m_aName, s_CurCustomTab, Graphics()->ScreenHiDPIScale(), WorkshopCardWidth, pLocalStatusLabel, true, false, ShowLocalOnlyBadge);
								if(FindAssetsCardMetadata(CardCacheKey) == nullptr)
									RequestAssetsCardMetadataHydration(CardCacheKey, AssetCardDisplayName(pLocalItem), pLocalItem->m_aAuthor[0] != '\0' ? pLocalItem->m_aAuthor : "--", pLocalStatusLabel, true, false, ShowLocalOnlyBadge, CardHydrationScheduler, PreviewPipelineScheduler, ResourcePreviewTelemetry, true);
							}
							if(!AssetsContentWarmupBlocked && pLocalItem != nullptr && s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
							{
								const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pLocalItem->m_aName, s_CurCustomTab, false, Graphics()->ScreenHiDPIScale(), TextureWidth);
								const SResourcePreviewState *pPipelineState = gs_SettingsAssetsResourcePreviewCache.Find(PreviewKey);
								const bool PreviewReady = (pPipelineState != nullptr && pPipelineState->m_Texture.IsValid()) || pLocalItem->m_RenderTexture.IsValid();
								if(!PreviewReady && PreviewPipelineScheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::VISIBLE))
								{
									const int ArtifactTextureSize = SettingsAssetPreviewBudgetedTextureSize(
										LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
										ASSET_PREVIEW_MIN_TEXTURE_SIZE,
										PreviewBudgetBytes,
										TextureMemoryUsageBytes,
										pLocalItem->m_PreviewResidentBytes);
									if(StartAssetsEntityBgPreviewArtifactJob(PreviewKey, pLocalItem->m_aName, Storage(), ArtifactTextureSize, Engine(), ResourcePreviewTelemetry))
										++ResourcePreviewTelemetry.m_PreviewAdmissions;
								}
							}
						}
						const int FirstLocalAsset = (int)vVisibleLocalAssetIndices[FirstLocalListIndex];
						const int LastLocalAsset = (int)vVisibleLocalAssetIndices[LastLocalListIndex];
						pActiveVisiblePreflight = &VisiblePreflight;
						if(!AssetsContentWarmupBlocked)
							SchedulePreviewRange(FirstLocalAsset, LastLocalAsset, Columns);
						pActiveVisiblePreflight = nullptr;
					}
				}

				VisiblePreflight.m_State = EAssetsVisiblePreflightState::WARMING_VISIBLE;
				const int PrefetchEnd = minimum((int)CombinedCount, WorkshopVisibleRange.m_EndItem + (AssetsScrollPressure ? 0 : Columns));
				for(int CombinedIndex = WorkshopVisibleRange.m_FirstItem; CombinedIndex < PrefetchEnd; ++CombinedIndex)
				{
					if(CombinedIndex < (int)LocalAssetCount)
						continue;
					const int DownloadableIndex = CombinedIndex - (int)LocalAssetCount;
					if(DownloadableIndex < 0 || DownloadableIndex >= (int)vVisibleDownloadableAssetIndices.size())
						continue;
					SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[vVisibleDownloadableAssetIndices[DownloadableIndex]];
					const bool Visible = CombinedVisibleAdmission.IsCombinedVisible(CombinedIndex);
					const char *pWorkshopStatusLabel = Localize(Asset.m_Installed ? "Downloaded" : "Network");
					const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(Asset.m_Id.empty() ? Asset.m_Name.c_str() : Asset.m_Id.c_str(), s_CurCustomTab, Graphics()->ScreenHiDPIScale(), WorkshopCardWidth, pWorkshopStatusLabel, Asset.m_Installed, Asset.m_DownloadFailed, false);
					if(FindAssetsCardMetadata(CardCacheKey) == nullptr)
						RequestAssetsCardMetadataHydration(CardCacheKey, Asset.m_Name.c_str(), Asset.m_Author.empty() ? "--" : Asset.m_Author.c_str(), pWorkshopStatusLabel, Asset.m_Installed, Asset.m_DownloadFailed, false, CardHydrationScheduler, PreviewPipelineScheduler, ResourcePreviewTelemetry, Visible);
					if(Visible)
					{
						if(Asset.m_ThumbTexture.IsValid())
							++VisiblePreflight.m_ReadyCount;
						else
							++VisiblePreflight.m_NotReadyCount;
					}
					if(!AssetsContentWarmupBlocked && StartWorkshopThumb(Asset, Visible))
					{
						CombinedVisibleAdmission.Record(Visible, !Visible);
						++VisiblePreflight.m_ThumbStartsBeforeVisible;
					}
				}
				VisiblePreflight.m_ThumbStartsDuringDraw = VisiblePreviewStartsDuringDraw;
				VisiblePreflight.m_VisibleReady = VisiblePreflight.m_NotReadyCount == 0;
				VisiblePreflight.m_State = VisiblePreflight.m_VisibleReady ? EAssetsVisiblePreflightState::READY_TO_SHOW : EAssetsVisiblePreflightState::WARMING_VISIBLE;
				const double PreflightMs = ThumbSchedulingTimer.ElapsedMs();
				WorkshopCardThumbSchedulingMs += PreflightMs;
				const bool GeometryChanged =
					s_aAssetsLastGeometryColumns[s_CurCustomTab] != 0 &&
					(s_aAssetsLastGeometryColumns[s_CurCustomTab] != Columns ||
						absolute(s_aAssetsLastGeometryRowHeight[s_CurCustomTab] - WorkshopRowHeight) > 0.01f);
				s_aAssetsLastGeometryColumns[s_CurCustomTab] = Columns;
				s_aAssetsLastGeometryRowHeight[s_CurCustomTab] = WorkshopRowHeight;
				char aPreflightExtra[256];
				str_format(aPreflightExtra, sizeof(aPreflightExtra), "stage=assets_visible_preflight status=%d visible_count=%d half_visible_count=%d ready_count=%d not_ready_count=%d visible_ready=%d geometry_stable=%d thumb_starts_before_visible=%d thumb_starts_during_draw=%d",
					(int)VisiblePreflight.m_State, VisiblePreflight.m_VisibleCount, VisiblePreflight.m_HalfVisibleCount, VisiblePreflight.m_ReadyCount, VisiblePreflight.m_NotReadyCount,
					VisiblePreflight.m_VisibleReady ? 1 : 0, VisiblePreflight.m_GeometryStable ? 1 : 0, VisiblePreflight.m_ThumbStartsBeforeVisible, VisiblePreflight.m_ThumbStartsDuringDraw);
				LogAssetsFramePerfStage("assets_visible_preflight", PreflightMs, true, aPreflightExtra);
				char aGeometryExtra[224];
				str_format(aGeometryExtra, sizeof(aGeometryExtra), "stage=assets_card_geometry tab=%d columns=%d row_height=%.2f first=%d end=%d stable=%d geometry_changed=%d",
					s_CurCustomTab, Columns, WorkshopRowHeight, WorkshopVisibleRange.m_FirstItem, WorkshopVisibleRange.m_EndItem, VisiblePreflight.m_GeometryStable ? 1 : 0, GeometryChanged ? 1 : 0);
				LogAssetsFramePerfStage("assets_card_geometry", 0.0, true, aGeometryExtra);
				VisiblePreflight.m_State = EAssetsVisiblePreflightState::VISIBLE;
			};
			PrepareAssetsVisibleContentBudgeted();
			const int AssetsMetadataHydratedThisFrame = DrainAssetsCardMetadataHydrationRequests(AssetsMetadataLayoutTokensThisFrame, ResourcePreviewTelemetry);
			SSettingsResourceMergeBudget ResourcePreviewUploadMergeBudget;
			ResourcePreviewUploadMergeBudget.m_MaxGpuUploads = AssetsUploadBlocked ? 0 : minimum(AssetsTextureUploadTokensThisFrame, GameClient()->GpuUploadLimiter()->RemainingUploads());
			SResourcePreviewUploadBudget ResourcePreviewUploadBudget;
			ResourcePreviewUploadBudget.m_MaxUploads = ResourcePreviewUploadMergeBudget.m_MaxGpuUploads;
			ResourcePreviewUploadBudget.m_pMergeBudget = &ResourcePreviewUploadMergeBudget;
			ResourcePreviewUploadBudget.m_pFrameBudget = SettingsFrameBudget();
			ResourcePreviewUploadBudget.m_pGpuUploadLimiter = GameClient()->GpuUploadLimiter();
			if(!AssetsContentWarmupBlocked)
				ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry, ResourcePreviewUploadBudget);
			auto CountResourcePreviewPlaceholder = [&]() {
				++ResourcePreviewTelemetry.m_PlaceholderCount;
			};

			s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem);
			for(size_t ListIndex = WorkshopVisibleRange.m_FirstItem; ListIndex < (size_t)WorkshopVisibleRange.m_EndItem; ++ListIndex)
			{
				if(ListIndex < LocalAssetCount)
				{
					const size_t LocalIndex = vVisibleLocalAssetIndices[ListIndex];
					const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, LocalIndex);
					if(pItem == nullptr)
						continue;

					const bool Selected = IsLocalAssetSelected(pItem->m_aName);

					const CListboxItem Item = s_WorkshopAssetsListBox.DoNextItem(pItem, Selected, AssetCardRadius + Margin / 2.0f);
					CUIRect ItemRect = Item.m_Rect;
					ItemRect.Margin(Margin / 2, &ItemRect);
					if(!Item.m_Visible)
						continue;
					auto GaussianBlurSuppression = Item.SuppressGaussianBlur();
					if(FirstVisibleLocalIndex < 0)
						FirstVisibleLocalIndex = (int)LocalIndex;
					CPerfTimer CardLayoutTextTimer;
					const CUIRect CardRect = ItemRect;
					const bool IsEntityBgDirectory = s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<const SCustomEntityBg *>(pItem)->m_IsDirectory;
					const bool HasDeleteButton = !IsEntityBgDirectory && !IsProtectedDefaultAsset(pItem->m_aName);
					const bool ShowLocalOnlyBadge = pCategory->m_LocalOnlyBadge && !pCategory->m_WorkshopEnabled;
					const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, true, IsEntityBgDirectory);
					const char *pLocalStatusLabel = ResolveLocalAssetStatusLabel(pItem, ShowLocalOnlyBadge);
					const SSettingsAssetsCardShell Shell = LayoutAssetsCardShell(CardRect, HasDeleteButton, pLocalStatusLabel, ShowLocalOnlyBadge, ShowAuthorRow);
					RenderAssetsCardShell(Shell);
					const bool CombinedVisible = CombinedVisibleAdmission.IsCombinedVisible((int)ListIndex);
					const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(pItem->m_aName, s_CurCustomTab, Graphics()->ScreenHiDPIScale(), CardRect.w, pLocalStatusLabel, true, false, ShowLocalOnlyBadge);
					SSettingsAssetsCardMetadataCacheEntry *pMetadata = FindAssetsCardMetadata(CardCacheKey);
					const bool MetadataCached = pMetadata != nullptr;
					(void)MetadataCached;
					pMetadata = FindAssetsCardMetadata(CardCacheKey);
					if(pMetadata != nullptr && AssetsContentWarmupBlocked)
						RenderAssetsCardMetadataFallback(Ui(), Shell, pMetadata->m_Title.c_str(), pMetadata->m_Author.c_str(), pMetadata->m_StatusLabel.c_str(), pMetadata->m_Installed || pMetadata->m_LocalOnly, ShowAuthorRow);
					else if(pMetadata != nullptr)
						RenderAssetsCardMetadataCached(Shell, pMetadata, RenderAssetsCardMetadata);
					else if(AssetsRenderCardMetadataFallback)
						RenderAssetsCardMetadataFallback(Ui(), Shell, AssetCardDisplayName(pItem), pItem->m_aAuthor[0] != '\0' ? pItem->m_aAuthor : "--", pLocalStatusLabel, true, ShowAuthorRow);
					WorkshopCardLayoutTextMs += CardLayoutTextTimer.ElapsedMs();

					CPerfTimer CardPreviewDrawTimer;
					SSettingsAssetsCardPreviewState PreviewState;
					const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(pItem->m_aName, s_CurCustomTab, false, Graphics()->ScreenHiDPIScale(), CardRect.w);
					SResourcePreviewState &ResourcePreviewState = gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey);
					if(IsEntityBgDirectory)
					{
						const bool IsWorkshopRootFolder = IsEntityBgDirectory &&
										  IsEntityBgWorkshopFolderPath(pItem->m_aName);
						PreviewState.m_DrawFolderIcon = true;
						PreviewState.m_FolderIsParent = str_comp(pItem->m_aName, "..") == 0;
						PreviewState.m_FolderIsWorkshopRoot = IsWorkshopRootFolder;
					}
					else if(s_CurCustomTab == ASSETS_TAB_ENTITIES && gs_SettingsAssetsEntityGamePreview)
					{
						const auto *pEntitiesItem = static_cast<const SCustomEntities *>(pItem);
						for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT && !PreviewState.m_Texture.IsValid(); m++)
							PreviewState.m_Texture = pEntitiesItem->m_aImages[m].m_Texture;
						if(!PreviewState.m_Texture.IsValid())
							PreviewState.m_Texture = pItem->m_RenderTexture;
						if(!PreviewState.m_Texture.IsValid())
						{
							for(const auto &Asset : WorkshopState.m_vAssets)
							{
								if(Asset.m_Name == pItem->m_aName && Asset.m_ThumbTexture.IsValid())
								{
									PreviewState.m_Texture = Asset.m_ThumbTexture;
									break;
								}
							}
						}
						PreviewState.m_DrawEntityTileArtifact = PreviewState.m_Texture.IsValid();
					}
					else
					{
						PreviewState.m_Texture = pItem->m_RenderTexture;
						if(!PreviewState.m_Texture.IsValid())
						{
							for(const auto &Asset : WorkshopState.m_vAssets)
							{
								if(Asset.m_LocalName == pItem->m_aName && Asset.m_ThumbTexture.IsValid())
								{
									PreviewState.m_Texture = Asset.m_ThumbTexture;
									break;
								}
							}
						}
						const bool DeferEntityBgHeavyPreview = s_CurCustomTab == ASSETS_TAB_ENTITY_BG && AssetsTabSwitchCooldownActive;
						PreviewState.m_EntityBgHeavyPreviewDeferred = DeferEntityBgHeavyPreview && !PreviewState.m_Texture.IsValid();
						if(PreviewState.m_EntityBgHeavyPreviewDeferred)
							++EntityBgHeavyPreviewDeferredCount;
						PreviewState.m_Loading = !PreviewState.m_Texture.IsValid();
					}
					if(!PreviewState.m_Texture.IsValid() && !PreviewState.m_DrawFolderIcon)
					{
						const SResourcePreviewState *pPipelineState = gs_SettingsAssetsResourcePreviewCache.Find(PreviewKey);
						if(pPipelineState != nullptr && pPipelineState->m_Texture.IsValid())
							PreviewState.m_Texture = pPipelineState->m_Texture;
					}
					const ESettingsResourcePreviewDrawResult PipelineDrawResult = SettingsResourcePreviewDrawResult(ResourcePreviewState);
					const bool PreviewReady = PipelineDrawResult == ESettingsResourcePreviewDrawResult::READY_TEXTURE || PreviewState.m_Texture.IsValid();
					ResourcePreviewState.m_TextureReady = PreviewReady;
					if(PreviewReady)
					{
						++ResourcePreviewTelemetry.m_ReadyTextureCount;
					}
					else if(!PreviewState.m_DrawFolderIcon)
					{
						CountResourcePreviewPlaceholder();
					}
					RenderAssetsCardPreview(Shell, PreviewState, true, CardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady, PreviewState.m_EntityBgHeavyPreviewDeferred));
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady && !PreviewState.m_DrawFolderIcon)
					{
						if(IsEntityBgVideoAsset(pItem->m_aName))
							RenderEntityBgVideoFallback(Shell.m_TextureRect);
						else
							RenderEntityBgFallback(Shell.m_TextureRect);
					}
					WorkshopCardPreviewDrawMs += CardPreviewDrawTimer.ElapsedMs();

					if(HasDeleteButton)
					{
						if(Ui()->DoButton_FontIcon(&s_vWorkshopLocalDeleteButtons[LocalIndex], FONT_ICON_TRASH, 0, &Shell.m_ActionButtonRect, IGraphics::CORNER_ALL))
						{
							DeleteLocalRequested = true;
							str_copy(aDeleteLocalName, pItem->m_aName, sizeof(aDeleteLocalName));
						}
					}
				}
				else
				{
					const size_t DownloadableIndex = ListIndex - LocalAssetCount;
					const size_t AssetIndex = vVisibleDownloadableAssetIndices[DownloadableIndex];
					SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[AssetIndex];

					const CListboxItem Item = s_WorkshopAssetsListBox.DoNextItem(&Asset, false, AssetCardRadius + Margin / 2.0f);
					CUIRect ItemRect = Item.m_Rect;
					ItemRect.Margin(Margin / 2, &ItemRect);
					if(!Item.m_Visible)
						continue;
					auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

					const int VisibleDownloadableIndex = static_cast<int>(DownloadableIndex);
					if(FirstVisibleDownloadableIndex < 0)
						FirstVisibleDownloadableIndex = VisibleDownloadableIndex;
					LastVisibleDownloadableIndex = VisibleDownloadableIndex;
					const bool CombinedVisible = CombinedVisibleAdmission.IsCombinedVisible((int)ListIndex);
					(void)CombinedVisible;

					CPerfTimer CardLayoutTextTimer;
					const CUIRect CardRect = ItemRect;
					const bool Downloading = Asset.m_pDownloadTask && !Asset.m_pDownloadTask->Done();
					const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, true, false);
					const char *pWorkshopStatusLabel = Localize(Asset.m_Installed ? "Downloaded" : "Network");
					const SSettingsAssetsCardShell Shell = LayoutAssetsCardShell(CardRect, true, pWorkshopStatusLabel, false, ShowAuthorRow);
					RenderAssetsCardShell(Shell);
					const SSettingsAssetsCardCacheKey CardCacheKey = BuildAssetsCardCacheKey(Asset.m_Id.empty() ? Asset.m_Name.c_str() : Asset.m_Id.c_str(), s_CurCustomTab, Graphics()->ScreenHiDPIScale(), CardRect.w, pWorkshopStatusLabel, Asset.m_Installed, Asset.m_DownloadFailed, false);
					SSettingsAssetsCardMetadataCacheEntry *pMetadata = FindAssetsCardMetadata(CardCacheKey);
					const bool MetadataCached = pMetadata != nullptr;
					(void)MetadataCached;
					pMetadata = FindAssetsCardMetadata(CardCacheKey);
					if(pMetadata != nullptr && AssetsContentWarmupBlocked)
						RenderAssetsCardMetadataFallback(Ui(), Shell, pMetadata->m_Title.c_str(), pMetadata->m_Author.c_str(), pMetadata->m_StatusLabel.c_str(), pMetadata->m_Installed || pMetadata->m_LocalOnly, ShowAuthorRow);
					else if(pMetadata != nullptr)
						RenderAssetsCardMetadataCached(Shell, pMetadata, RenderAssetsCardMetadata);
					else if(AssetsRenderCardMetadataFallback)
						RenderAssetsCardMetadataFallback(Ui(), Shell, Asset.m_Name.c_str(), Asset.m_Author.empty() ? "--" : Asset.m_Author.c_str(), pWorkshopStatusLabel, Asset.m_Installed, ShowAuthorRow);
					WorkshopCardLayoutTextMs += CardLayoutTextTimer.ElapsedMs();

					CPerfTimer CardPreviewDrawTimer;
					SSettingsAssetsCardPreviewState PreviewState;
					const SResourcePreviewKey PreviewKey = BuildAssetsResourcePreviewKey(Asset.m_Id.empty() ? Asset.m_Name.c_str() : Asset.m_Id.c_str(), s_CurCustomTab, true, Graphics()->ScreenHiDPIScale(), CardRect.w);
					SResourcePreviewState &ResourcePreviewState = gs_SettingsAssetsResourcePreviewCache.GetOrCreate(PreviewKey);
					PreviewState.m_Texture = Asset.m_ThumbTexture;
					if(!PreviewState.m_Texture.IsValid())
					{
						const SResourcePreviewState *pPipelineState = gs_SettingsAssetsResourcePreviewCache.Find(PreviewKey);
						if(pPipelineState != nullptr && pPipelineState->m_Texture.IsValid())
							PreviewState.m_Texture = pPipelineState->m_Texture;
					}
					PreviewState.m_DrawEntityTileArtifact = s_CurCustomTab == ASSETS_TAB_ENTITIES && gs_SettingsAssetsEntityGamePreview && Asset.m_ThumbTexture.IsValid();
					PreviewState.m_Loading = !Asset.m_ThumbTexture.IsValid();
					const ESettingsResourcePreviewDrawResult PipelineDrawResult = SettingsResourcePreviewDrawResult(ResourcePreviewState);
					const bool PreviewReady = PipelineDrawResult == ESettingsResourcePreviewDrawResult::READY_TEXTURE || PreviewState.m_Texture.IsValid();
					ResourcePreviewState.m_TextureReady = PreviewReady;
					if(PreviewReady)
					{
						++ResourcePreviewTelemetry.m_ReadyTextureCount;
					}
					else
					{
						CountResourcePreviewPlaceholder();
					}
					RenderAssetsCardPreview(Shell, PreviewState, true, CardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady));
					WorkshopCardPreviewDrawMs += CardPreviewDrawTimer.ElapsedMs();

					const char *pActionIcon = Downloading ? FONT_ICON_ARROW_ROTATE_RIGHT : FONT_ICON_CIRCLE_CHEVRON_DOWN;
					if(Ui()->DoButton_FontIcon(&vWorkshopActionButtons[AssetIndex], pActionIcon, 0, &Shell.m_ActionButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Downloading))
					{
						DownloadRequested = true;
						RequestedDownloadAssetIndex = AssetIndex;
						RequestedDownloadPopupX = Shell.m_ActionButtonRect.x + Shell.m_ActionButtonRect.w;
						RequestedDownloadPopupY = Shell.m_ActionButtonRect.y + Shell.m_ActionButtonRect.h + 4.0f;
						WorkshopActionTriggered = true;
					}
				}
			}
			s_WorkshopAssetsListBox.SkipItems((int)CombinedCount - WorkshopVisibleRange.m_EndItem);
			char aExtra[768];
			str_format(aExtra, sizeof(aExtra), "tab=%d combined=%d local_total=%d remote_total=%d rendered=%d thumb_starts=%d visible_first=1 visible_starts=%d prefetch_starts=%d background_starts=%d visible_ready=%d geometry_stable=%d thumb_starts_before_visible=%d thumb_starts_during_draw=%d tab_switch_first_frame=%d tab_switch_cooldown_frames=%d tab_switch_shell_only=%d visible_thumb_start_limit=%d entity_bg_preview_deferred=%d entity_bg_preview_deferred_count=%d layout_text_ms=%.3f preview_draw_ms=%.3f thumb_scheduling_ms=%.3f preview_jobs_started=%d preview_jobs_done=%d preview_uploads=%d preview_admissions=%d preview_artifact_ms=%.3f metadata_hydrate_ms=%.3f metadata_hydrated=%d placeholder_count=%d ready_texture_count=%d visible_ready_ratio=%.3f",
				s_CurCustomTab, (int)CombinedCount, (int)LocalAssetCount, (int)vVisibleDownloadableAssetIndices.size(), WorkshopVisibleRange.m_RenderedItems, WorkshopThumbStartsThisFrame,
				CombinedVisibleAdmission.m_VisibleStarts, CombinedVisibleAdmission.m_PrefetchStarts, CombinedVisibleAdmission.m_BackgroundStarts,
				VisiblePreflight.m_VisibleReady ? 1 : 0, VisiblePreflight.m_GeometryStable ? 1 : 0, VisiblePreflight.m_ThumbStartsBeforeVisible, VisiblePreflight.m_ThumbStartsDuringDraw,
				AssetsTabSwitchFirstFrame ? 1 : 0, s_AssetsTabSwitchCooldownFrames, CardHydrationScheduler.m_TabSwitchShellOnlyFrame ? 1 : 0, VisibleThumbStartLimitThisFrame,
				s_CurCustomTab == ASSETS_TAB_ENTITY_BG && AssetsTabSwitchCooldownActive ? 1 : 0, EntityBgHeavyPreviewDeferredCount, WorkshopCardLayoutTextMs, WorkshopCardPreviewDrawMs, WorkshopCardThumbSchedulingMs,
				ResourcePreviewTelemetry.m_PreviewJobsStarted, ResourcePreviewTelemetry.m_PreviewJobsDone, ResourcePreviewTelemetry.m_PreviewUploads, ResourcePreviewTelemetry.m_PreviewAdmissions, ResourcePreviewTelemetry.m_PreviewArtifactMs, ResourcePreviewTelemetry.m_MetadataHydrateMs,
				AssetsMetadataHydratedThisFrame, ResourcePreviewTelemetry.m_PlaceholderCount, ResourcePreviewTelemetry.m_ReadyTextureCount, SettingsResourcePreviewVisibleReadyRatio(ResourcePreviewTelemetry.m_ReadyTextureCount, ResourcePreviewTelemetry.m_VisibleCount));
			LogAssetsFramePerfStage("assets_preview_draw_workshop_cards", WorkshopCardsTimer.ElapsedMs(), false, aExtra);
			LogAssetsFramePerfStage("assets_preview_draw_workshop_cards_layout_text", WorkshopCardLayoutTextMs, false, aExtra);
			LogAssetsFramePerfStage("assets_preview_draw_workshop_cards_preview_draw", WorkshopCardPreviewDrawMs, false, aExtra);
			LogAssetsFramePerfStage("assets_preview_draw_workshop_cards_thumb_scheduling", WorkshopCardThumbSchedulingMs, false, aExtra);
			SSettingsUiBudgetFrame UiBudget;
			UiBudget.m_LayoutMs = WorkshopCardsTimer.ElapsedMs();
			UiBudget.m_TextMs = WorkshopCardLayoutTextMs;
			UiBudget.m_TextNew = 0;
			UiBudget.m_TextReused = 0;
			UiBudget.m_DrawCalls = WorkshopVisibleRange.m_RenderedItems;
			UiBudget.m_Vertices = WorkshopVisibleRange.m_RenderedItems * 4;
			UiBudget.m_Indices = WorkshopVisibleRange.m_RenderedItems * 6;
			UiBudget.m_HeapAllocs = 0;
			UiBudget.m_VisibleWidgets = WorkshopVisibleRange.m_RenderedItems;
			UiBudget.m_Tab = s_CurCustomTab;
			UiBudget.m_Subtab = 0;
			LogSettingsUiBudget("settings:assets", UiBudget);
			char aWorkshopListFrameExtra[192];
			str_format(aWorkshopListFrameExtra, sizeof(aWorkshopListFrameExtra), "tab=%d items_total=%d items_rendered=%d items_skipped=%d rows_total=%d rows_visible=%d first=%d end=%d",
				s_CurCustomTab, WorkshopVisibleRange.m_TotalItems, WorkshopVisibleRange.m_RenderedItems, WorkshopVisibleRange.m_SkippedItems,
				WorkshopVisibleRange.m_TotalRows, WorkshopVisibleRange.m_VisibleRows, WorkshopVisibleRange.m_FirstItem, WorkshopVisibleRange.m_EndItem);
			LogAssetsFramePerfStage("assets_workshop_list_frame", 0.0, true, aWorkshopListFrameExtra);
			const int NewCombinedSelected = s_WorkshopAssetsListBox.DoEnd();
			const bool WorkshopListScrollActive = QmMenuUiScrollPerfActive(s_WorkshopAssetsListBox.WheelConsumedThisFrame(), s_WorkshopAssetsListBox.ScrollbarActive(), s_WorkshopAssetsListBox.ScrollbarAnimating());
			if(WorkshopListScrollActive)
			{
				StartSettingsPerfScrollWindow("assets_grid_scroll", SettingsPerfContextName(), "settings:assets", "none");
				SQmMenuUiFramePerf MenuUiPerf;
				MenuUiPerf.m_pPage = "settings:assets";
				MenuUiPerf.m_pOperation = "assets_grid_scroll";
				MenuUiPerf.m_ItemsTotal = WorkshopVisibleRange.m_TotalItems;
				MenuUiPerf.m_ItemsVisible = WorkshopVisibleRange.m_RenderedItems;
				MenuUiPerf.m_ItemsProcessed = WorkshopVisibleRange.m_RenderedItems;
				MenuUiPerf.m_ItemsSkipped = WorkshopVisibleRange.m_SkippedItems;
				MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - AssetsUiBudgetStartTime).count() : -1.0f;
				MenuUiPerf.m_CacheHits = gs_SettingsAssetsCardMetadataCacheHits;
				MenuUiPerf.m_CacheMisses = gs_SettingsAssetsCardMetadataCacheMisses;
				MenuUiPerf.m_CacheEvictions = gs_SettingsAssetsCardMetadataCacheEvictions;
				QmLogMenuUiFramePerf(MenuUiPerf, Client());
			}
			RefreshAssetsScrollUploadCooldownForOffset(WorkshopListScrollActive, s_WorkshopAssetsListBox.ScrollOffsetY(), s_aAssetsLastWorkshopScrollOffsetY[s_CurCustomTab], WorkshopRowHeight, WorkshopListJumpScrollActive);
			RefreshWorkshopAssetsUploadBudget();
			s_AssetsLastFirstVisibleCombinedIndex[s_CurCustomTab] = WorkshopVisibleRange.m_FirstItem < WorkshopVisibleRange.m_EndItem ? WorkshopVisibleRange.m_FirstItem : -1;
			s_AssetsLastLastVisibleCombinedIndex[s_CurCustomTab] = WorkshopVisibleRange.m_FirstItem < WorkshopVisibleRange.m_EndItem ? WorkshopVisibleRange.m_EndItem - 1 : -1;
			s_AssetsLastFirstVisibleDownloadableIndex[s_CurCustomTab] = FirstVisibleDownloadableIndex;
			s_AssetsLastLastVisibleDownloadableIndex[s_CurCustomTab] = LastVisibleDownloadableIndex;
			m_SettingsScrollActive = m_SettingsScrollActive || WorkshopListScrollActive;
			s_AssetsScrollActiveLastFrame = WorkshopListScrollActive;
			const SSettingsResourceFrameContext WorkshopUploadFrameContext = SettingsBuildFrameContext(
				s_AssetsScrollCooldownFrames > 0, WorkshopListScrollActive, WorkshopListJumpScrollActive, s_AssetsPostScrollRecoveryFrames);
			RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(
				RemainingHeavyResourceBatches, WorkshopUploadFrameContext, 4, 1);
			FinalizeReadyPreviewDecodes(WorkshopUploadFrameContext);
			DrainReadyPreviewUploadsAfterList(WorkshopUploadFrameContext);
			FinalizeWorkshopReadyThumbs(WorkshopUploadFrameContext);
			DrainWorkshopReadyThumbUploads(WorkshopUploadFrameContext);
			if(DeleteLocalRequested)
			{
				str_copy(s_aWorkshopPendingDeleteName, aDeleteLocalName, sizeof(s_aWorkshopPendingDeleteName));
				s_WorkshopDeleteConfirmPopup.Reset();
				s_WorkshopDeleteConfirmPopup.YesNoButtons();
				str_copy(s_WorkshopDeleteConfirmPopup.m_aMessage, Localize("Are you sure you want to delete this asset?"));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_WorkshopDeleteConfirmPopup);
			}
			if(DownloadRequested && RequestedDownloadAssetIndex < WorkshopState.m_vAssets.size())
			{
				s_PendingDownloadAssetIndex = RequestedDownloadAssetIndex;
				s_WorkshopDownloadConfirmPopup.Reset();
				s_WorkshopDownloadConfirmPopup.YesNoButtons();
				str_copy(s_WorkshopDownloadConfirmPopup.m_aMessage, Localize("Download this asset?"));
				Ui()->ShowPopupConfirm(RequestedDownloadPopupX, RequestedDownloadPopupY, &s_WorkshopDownloadConfirmPopup);
			}

			if(s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
			{
				if(DeleteLocalAssetByTab(Storage(), s_CurCustomTab, s_aWorkshopPendingDeleteName))
				{
					if(IsLocalAssetSelected(s_aWorkshopPendingDeleteName))
						ApplyLocalAssetSelection("default");
					for(SWorkshopHudAsset &Asset : WorkshopState.m_vAssets)
					{
						if(str_comp(Asset.m_LocalName.c_str(), s_aWorkshopPendingDeleteName) == 0)
						{
							Asset.m_Installed = false;
							break;
						}
					}
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
					{
						const auto EntryIt = std::find(m_vEntityBgSourceNames.begin(), m_vEntityBgSourceNames.end(), std::string(s_aWorkshopPendingDeleteName));
						if(EntryIt != m_vEntityBgSourceNames.end())
							m_vEntityBgSourceNames.erase(EntryIt);
						const auto SourceIt = m_vEntityBgSourceKinds.find(s_aWorkshopPendingDeleteName);
						if(SourceIt != m_vEntityBgSourceKinds.end() && SourceIt->second == EEntityBgHierarchyEntrySource::WORKSHOP)
							m_vEntityBgSourceKinds.erase(SourceIt);
						RefreshEntityBgHierarchyView();
					}
					else
					{
						ClearCustomItems(s_CurCustomTab);
					}
				}
				else
				{
					dbg_msg("assets", "failed to delete local asset '%s' in tab %d", s_aWorkshopPendingDeleteName, s_CurCustomTab);
				}
				s_WorkshopDeleteConfirmPopup.Reset();
				s_aWorkshopPendingDeleteName[0] = '\0';
			}
			else if(s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
			{
				s_WorkshopDeleteConfirmPopup.Reset();
				s_aWorkshopPendingDeleteName[0] = '\0';
			}

			if(s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::CONFIRMED && s_PendingDownloadAssetIndex < WorkshopState.m_vAssets.size())
			{
				SWorkshopHudAsset &Asset = WorkshopState.m_vAssets[s_PendingDownloadAssetIndex];
				Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
				Storage()->CreateFolder(pInstallFolder, IStorage::TYPE_SAVE);

				auto pDownloadTask = HttpGetFile(Asset.m_DownloadUrl.c_str(), Storage(), Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE);
				pDownloadTask->Timeout(CTimeout{10000, 30000, 100, 10});
				pDownloadTask->LogProgress(HTTPLOG::FAILURE);
				pDownloadTask->FailOnErrorStatus(false);
				pDownloadTask->SkipByFileTime(false);
				Asset.m_pDownloadTask = std::move(pDownloadTask);
				Asset.m_DownloadFailed = false;
				Http()->Run(Asset.m_pDownloadTask);
				WorkshopActionTriggered = true;
				s_WorkshopDownloadConfirmPopup.Reset();
				s_PendingDownloadAssetIndex = SIZE_MAX;
			}
			else if(s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::CANCELED)
			{
				s_WorkshopDownloadConfirmPopup.Reset();
				s_PendingDownloadAssetIndex = SIZE_MAX;
			}

			if(!DeleteLocalRequested && s_WorkshopDeleteConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && s_WorkshopDownloadConfirmPopup.m_Result == CUi::SConfirmPopupContext::UNSET && !WorkshopActionTriggered && s_WorkshopAssetsListBox.WasItemSelected() && NewCombinedSelected >= 0 && NewCombinedSelected != OldCombinedSelected && static_cast<size_t>(NewCombinedSelected) < LocalAssetCount)
			{
				const size_t LocalIndex = vVisibleLocalAssetIndices[static_cast<size_t>(NewCombinedSelected)];
				const SCustomItem *pNewItem = GetCustomItem(s_CurCustomTab, LocalIndex);
				if(pNewItem && pNewItem->m_aName[0] != '\0')
				{
					if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && static_cast<const SCustomEntityBg *>(pNewItem)->m_IsDirectory)
					{
						if(str_comp(pNewItem->m_aName, "..") == 0)
						{
							char aParentFolder[IO_MAX_PATH_LENGTH];
							BuildEntityBgParentFolder(m_aEntityBgCurrentFolder, aParentFolder, sizeof(aParentFolder));
							str_copy(m_aEntityBgCurrentFolder, aParentFolder, sizeof(m_aEntityBgCurrentFolder));
						}
						else
						{
							str_copy(m_aEntityBgCurrentFolder, pNewItem->m_aName, sizeof(m_aEntityBgCurrentFolder));
						}
						RefreshEntityBgHierarchyView();
					}
					else
					{
						ApplyLocalAssetSelection(pNewItem->m_aName);
					}
				}
			}
		}

		if(WorkshopState.m_pEntityBgPreviewTask && WorkshopState.m_pEntityBgPreviewTask->Done())
		{
			char aPreviewError[sizeof(WorkshopState.m_aError)] = "";
			const EHttpState PreviewTaskState = WorkshopState.m_pEntityBgPreviewTask->State();
			if(PreviewTaskState == EHttpState::DONE && WorkshopState.m_pEntityBgPreviewTask->StatusCode() == 200)
			{
				if(json_value *pJson = WorkshopState.m_pEntityBgPreviewTask->ResultJson())
				{
					if(ParseEntityBgPreviewMap(pJson, WorkshopState.m_vEntityBgPreviewExtByName, WorkshopState.m_EntityBgPreviewBaseUrl, aPreviewError, sizeof(aPreviewError)))
						ApplyEntityBgPreviewThumbUrls(WorkshopState);
					json_value_free(pJson);
				}
				else
				{
					str_copy(aPreviewError, "Entity bg preview json parse failed", sizeof(aPreviewError));
				}
			}
			else if(PreviewTaskState != EHttpState::DONE)
			{
				str_copy(aPreviewError, "Entity bg preview request failed", sizeof(aPreviewError));
			}
			else
			{
				str_format(aPreviewError, sizeof(aPreviewError), "Entity bg preview request failed (%d)", WorkshopState.m_pEntityBgPreviewTask->StatusCode());
			}

			WorkshopState.m_pEntityBgPreviewTask.reset();
			WorkshopState.m_EntityBgPreviewRequested = true;
			if(aPreviewError[0] != '\0' && WorkshopState.m_vEntityBgPreviewExtByName.empty())
				dbg_msg("assets", "%s", aPreviewError);
		}
	}

	// Quick search - 底部按钮栏布局
	CUIRect Footer;
	MainView.HSplitBottom(FooterHeight, &MainView, &Footer);
	CUIRect AssetsEditorButton, AssetsDirButton, EntityPreviewButton, ShowWorkshopAssetsButton, WorkshopSyncButton;
	CUIRect ToolbarArea;
	if(ToolbarUsesOwnRows)
	{
		Footer.HSplitTop(ms_ButtonHeight, &QuickSearch, &Footer);
		Footer.HSplitTop(ToolbarRowGap, nullptr, &ToolbarArea);
	}
	else
	{
		QuickSearch = Footer;
		QuickSearch.VSplitLeft(AssetsSearchWidth, &QuickSearch, &ToolbarArea);
	}
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	ui_widget::SInputFieldOptions AssetsSearchOptions;
	AssetsSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	AssetsSearchOptions.m_Clearable = true;
	AssetsSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	AssetsSearchOptions.m_FontSize = ContentMetrics.m_BodySize;
	if(ui_widget::InputField(AssetsSearchCtx, &s_aFilterInputs[s_CurCustomTab], QuickSearch, AssetsSearchOptions).m_Changed)
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}

	float ToolbarX = ToolbarUsesOwnRows ? ToolbarArea.x : ToolbarArea.x + maximum(0.0f, ToolbarArea.w - ToolbarWidth);
	float ToolbarY = ToolbarArea.y;
	const float ToolbarRowStartX = ToolbarX;
	auto PlaceToolbarButton = [&](CUIRect &ButtonRect, float ButtonWidth) {
		if(ToolbarUsesOwnRows && ToolbarX > ToolbarRowStartX && ToolbarX + ToolbarGap + ButtonWidth > ToolbarArea.x + ToolbarArea.w)
		{
			ToolbarX = ToolbarRowStartX;
			ToolbarY += ms_ButtonHeight + ToolbarRowGap;
		}
		if(ToolbarX > ToolbarRowStartX)
			ToolbarX += ToolbarGap;
		ButtonRect = {ToolbarX, ToolbarY + 5.0f, ButtonWidth, maximum(0.0f, ms_ButtonHeight - 5.0f)};
		ToolbarX += ButtonWidth;
	};
	if(ShowEntityPreviewToggle)
		PlaceToolbarButton(EntityPreviewButton, EntityPreviewButtonWidth);
	PlaceToolbarButton(AssetsDirButton, AssetsDirButtonWidth);
	static CButtonContainer s_AssetsEditorButton;
	if(SupportsAssetsEditor)
	{
		PlaceToolbarButton(AssetsEditorButton, AssetsEditorButtonWidth);
		if(DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_AssetsEditorButton, "assets-toolbar-editor", Localize("Assets editor"), 0, &AssetsEditorButton))
		{
			int AssetsEditorType = ASSETS_EDITOR_TYPE_GAME;
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
				AssetsEditorType = ASSETS_EDITOR_TYPE_ENTITIES;
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
				AssetsEditorType = ASSETS_EDITOR_TYPE_EMOTICONS;
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
				AssetsEditorType = ASSETS_EDITOR_TYPE_PARTICLES;
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
				AssetsEditorType = ASSETS_EDITOR_TYPE_HUD;
			else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)
				AssetsEditorType = ASSETS_EDITOR_TYPE_GUI_CURSOR;
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
				AssetsEditorType = ASSETS_EDITOR_TYPE_ARROW;
			else if(s_CurCustomTab == ASSETS_TAB_STRONG_WEAK)
				AssetsEditorType = ASSETS_EDITOR_TYPE_STRONG_WEAK;
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
				AssetsEditorType = ASSETS_EDITOR_TYPE_EXTRAS;
			AssetsEditorOpen(AssetsEditorType);
			return;
		}
	}
	if(SupportsWorkshopSync)
	{
		PlaceToolbarButton(WorkshopSyncButton, WorkshopSyncButtonWidth);
		PlaceToolbarButton(ShowWorkshopAssetsButton, ShowWorkshopAssetsButtonWidth);
	}
	PlaceToolbarButton(ReloadButton, 25.0f);

	// Entity Preview 按钮（仅实体层标签页显示）
	if(ShowEntityPreviewToggle)
	{
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_EntityPreviewToggleId, "assets-toolbar-entity-preview", Localize("Entity Preview"), gs_SettingsAssetsEntityGamePreview, &EntityPreviewButton))
		{
			gs_SettingsAssetsEntityGamePreview = !gs_SettingsAssetsEntityGamePreview;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &EntityPreviewButton, Localize("Toggle between game scene preview and raw texture"));
	}

	static CButtonContainer s_AssetsDirId;
	static CButtonContainer s_ShowWorkshopAssetsId;
	static CButtonContainer s_WorkshopSyncId;
	if(SupportsWorkshopSync && DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_ShowWorkshopAssetsId, "assets-toolbar-show-workshop-assets", Localize("Show Workshop Assets"), m_ShowWorkshopAssets, &ShowWorkshopAssetsButton))
	{
		m_ShowWorkshopAssets = !m_ShowWorkshopAssets;
		gs_aInitCustomList[s_CurCustomTab] = true;
		if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG)
			RefreshEntityBgHierarchyView();
	}
	if(SupportsWorkshopSync)
		GameClient()->m_Tooltips.DoToolTip(&s_ShowWorkshopAssetsId, &ShowWorkshopAssetsButton, Localize("Show Workshop Assets"));

	if(SupportsWorkshopSync && DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_WorkshopSyncId, "assets-toolbar-sync-workshop-assets", Localize("Sync Workshop Assets"), 0, &WorkshopSyncButton))
	{
		if(SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab))
			ResetWorkshopState(*pWorkshopState, Graphics(), true);
	}
	if(SupportsWorkshopSync)
		GameClient()->m_Tooltips.DoToolTip(&s_WorkshopSyncId, &WorkshopSyncButton, Localize("Sync Workshop Assets"));

	if(DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_AssetsDirId, "assets-toolbar-directory", Localize("Assets directory"), 0, &AssetsDirButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab);
		if(pCategory != nullptr)
			str_copy(aBufFull, pCategory->m_pInstallFolder);
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &AssetsDirButton, Localize("Open the directory to add custom assets"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_AssetsReloadBtnId;
	if(DoButton_Menu(&s_AssetsReloadBtnId, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		FlushPersistedLocalAssetAuthorsIfDirty(Storage(), s_CurCustomTab);
		ClearCustomItems(s_CurCustomTab);
		if(SWorkshopHudState *pWorkshopState = WorkshopStateByTab(s_CurCustomTab))
			ResetWorkshopState(*pWorkshopState, Graphics(), true);
		InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(s_AssetsTabSwitchFirstFrame > 0)
		--s_AssetsTabSwitchFirstFrame;
	if(s_AssetsTabSwitchCooldownFrames > 0)
		--s_AssetsTabSwitchCooldownFrames;
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}
int CMenus::CurrentSettingsAssetsTab() const
{
	return std::clamp(s_CurCustomTab, (int)ASSETS_TAB_ENTITIES, NUMBER_OF_ASSETS_TABS - 1);
}
