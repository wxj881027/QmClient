#ifndef GAME_CLIENT_COMPONENTS_SETTINGS_RESOURCE_JOBS_H
#define GAME_CLIENT_COMPONENTS_SETTINGS_RESOURCE_JOBS_H

#include <game/client/components/settings_runtime_cache.h>
#include <game/client/ui_rect.h>

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct SSettingsSkinListColorKey
{
	bool m_UseCustomColor = false;
	int m_ColorBody = 0;
	int m_ColorFeet = 0;
};

struct SSettingsSkinListEntry
{
	std::string m_Name;
	bool m_Selected = false;
	bool m_Favorite = false;
	std::optional<SSettingsSkinListColorKey> m_ColorKey;
	int m_OfficialReleaseDate = 0;
	time_t m_LastModified = 0;
};

struct SSettingsSkinListPlan
{
	std::vector<std::string> m_vNames;
	std::vector<SSettingsSkinListEntry> m_vEntries;
};

struct SSettingsSkinListPlanResult
{
	int m_Generation = 0;
	SSettingsSkinListPlan m_Plan;
};

struct SSettingsSkinListVisibleRange
{
	int m_TotalItems = 0;
	int m_TotalRows = 0;
	int m_FirstVisibleRow = 0;
	int m_LastVisibleRow = -1;
	int m_FirstItem = 0;
	int m_EndItem = 0;
	int m_VisibleRows = 0;
	int m_RenderedItems = 0;
	int m_SkippedItems = 0;
};

struct SSkinListPlanState
{
	bool m_DirectoryScanPending = true;
	bool m_MergeComplete = false;
	int m_ItemCount = 0;
	int m_VisibleBacklog = -1;
	int m_BackgroundBacklog = -1;
};

struct SSkinListPlanSnapshot
{
	int m_ItemCount = 0;
};

struct SSettingsResourceMergeBudget
{
	int m_MaxListEntries = 64;
	int m_MaxGpuUploads = 1;
	ESettingsWarmupStopReason m_StopReason = ESettingsWarmupStopReason::NONE;
	bool m_FrameMergeBudgetConsumed = false;
};

struct SSettingsResourceFrameContext
{
	bool m_ScrollActive = false;
	bool m_JumpScrollActive = false;
	int m_PostScrollRecoveryFrames = 0;
	bool m_HighPrioritySettled = false;
};

enum class ESettingsResourcePriority
{
	BACKGROUND,
	PREFETCH,
	VISIBLE,
};

class CSettingsSkinLoadViewport
{
	std::vector<std::string> m_vNames;
	std::unordered_set<std::string> m_Names;
	size_t m_Frame = 0;
	std::optional<size_t> m_PublishedFrame;

public:
	void BeginFrame() { ++m_Frame; }
	void Publish(std::vector<std::string> Names, bool MeasurementOnly = false)
	{
		if(MeasurementOnly)
			return;
		m_PublishedFrame = m_Frame;
		if(Names == m_vNames)
			return;
		m_vNames = std::move(Names);
		m_Names.clear();
		m_Names.insert(m_vNames.begin(), m_vNames.end());
	}
	bool Contains(const std::string &Name) const { return m_PublishedFrame == m_Frame && m_Names.contains(Name); }
	static bool RowVisible(bool MeasurementOnly, const CUIRect &Row, const CUIRect *pClip)
	{
		const CUIRect Visible = pClip ? Row.Intersection(*pClip) : Row;
		return !MeasurementOnly && Visible.w > 0.0f && Visible.h > 0.0f;
	}
};

// 当前视窗先于其他可见需求，其次预取和后台；单项被拒绝不阻断后续请求。
template<typename T, typename FPriority, typename FVisible, typename FVisit>
void SettingsVisitSkinLoadRequests(const std::vector<T> &Requests, FPriority Priority, FVisible CurrentVisible, FVisit Visit)
{
	std::vector<int> vRanks;
	vRanks.reserve(Requests.size());
	for(const auto &Request : Requests)
	{
		const auto RequestPriority = Priority(Request);
		vRanks.push_back(CurrentVisible(Request) ? 0 : RequestPriority == ESettingsResourcePriority::VISIBLE ? 1 :
						       RequestPriority == ESettingsResourcePriority::PREFETCH        ? 2 :
														       3);
	}
	for(int Rank = 0; Rank < 4; ++Rank)
	{
		for(size_t Index = 0; Index < Requests.size(); ++Index)
		{
			if(vRanks[Index] == Rank)
				Visit(Requests[Index]);
		}
	}
}

enum class ESettingsSkinBackgroundRequestBlockReason
{
	NONE = 0,
	DRAIN_INACTIVE,
	VISIBLE_RESERVE,
	STALL_BACKPRESSURE,
};

enum class ESettingsSkinSourceAdmissionBlockReason
{
	NONE = 0,
	DRAIN_INACTIVE,
	VISIBLE_RESERVE,
	LOADING_WINDOW,
	QUEUE_FUSE,
};

struct SSettingsSkinBackgroundRequestBudgetInput
{
	int m_DefaultBudget = 0;
	int m_Pending = 0;
	int m_Loading = 0;
	int m_BackgroundRequested = 0;
	int m_CountFuseLimit = 0;
	int m_VisibleReserve = 0;
	int m_RecentLoadedDelta = 0;
	int m_RecentAdmittedDelta = 0;
	bool m_DrainActive = false;
};

struct SSettingsSkinBackgroundRequestBudgetOutput
{
	int m_RequestBudget = 0;
	int m_RealInflight = 0;
	ESettingsSkinBackgroundRequestBlockReason m_BlockReason = ESettingsSkinBackgroundRequestBlockReason::NONE;
};

struct SSettingsTeeOffscreenLifecycleInput
{
	int m_TotalEntries = 0;
	int m_ValidEntries = 0;
	int m_SettledEntries = 0;
	bool m_DrainSessionActive = false;
	bool m_PerfDebugEnabled = false;
};

struct SSettingsTeeOffscreenLifecycleOutput
{
	bool m_FullListReady = false;
	bool m_CompleteDrainSession = false;
	bool m_LogCompletion = false;
};

struct SSettingsSkinSourceAdmissionInput
{
	bool m_BackgroundRequested = false;
	ESettingsResourcePriority m_Priority = ESettingsResourcePriority::BACKGROUND;
	bool m_BackgroundDrainActive = false;
	int m_Loading = 0;
	int m_NormalLoadingWindow = 0;
	int m_VisibleLoadingWindow = 0;
};

struct SSettingsSkinSourceAdmissionOutput
{
	bool m_PromoteAllowed = true;
	ESettingsResourcePriority m_PromotePriority = ESettingsResourcePriority::BACKGROUND;
	ESettingsSkinSourceAdmissionBlockReason m_BlockReason = ESettingsSkinSourceAdmissionBlockReason::NONE;
	bool m_CountFuseApplies = true;
};

enum class ESettingsSkinBackgroundWindowDecision
{
	HOLD = 0,
	INCREASE,
	DECREASE,
};

enum class ESettingsSkinThroughputControllerMode
{
	SCROLL_ACTIVE = 0,
	POST_SCROLL_RECOVERY,
	IDLE_VISIBLE,
	IDLE_DRAIN,
};

enum class ESettingsSkinThroughputControllerReason
{
	NONE = 0,
	GPU,
	FINALIZE,
	DECODE,
	ADMISSION,
	FRAME_PRESSURE,
	PROGRESS,
};

enum class ESettingsAdaptiveBudgetMode
{
	IDLE = 0,
	SCROLL_ACTIVE,
	POST_SCROLL_RECOVERY,
	FRAME_PRESSURE,
	WINDOW_INACTIVE,
};

enum class ESettingsAdaptiveBudgetReason
{
	NONE = 0,
	PROGRESS,
	FRAME_PRESSURE,
	SCROLL,
	WINDOW_INACTIVE,
	BACKLOG_EMPTY,
};

struct SSettingsAdaptiveBudgetInput
{
	uint64_t m_FrameId = 0;
	char m_aOperation[64] = "";
	char m_aPage[64] = "";
	char m_aTab[32] = "";
	int m_Subtab = 0;
	char m_aContext[32] = "";
	float m_FrameMsAverage = 0.0f;
	float m_FrameMsP95 = 0.0f;
	float m_TargetFrameMs = 8.333f;
	float m_TextContainerCreateMsEwma = 0.0f;
	float m_TextContainerUploadMsEwma = 0.0f;
	float m_GlyphRasterizeMsEwma = 0.0f;
	float m_GlyphUploadMsEwma = 0.0f;
	bool m_ScrollActive = false;
	bool m_JumpScrollActive = false;
	bool m_TabSwitchFirstFrame = false;
	bool m_FramePressure = false;
	int m_PostScrollRecoveryFrames = 0;
	int m_VisibleWaiting = 0;
	int m_BackgroundBacklog = 0;
	bool m_GpuBudgetExhausted = false;
	bool m_FinalizeBudgetExhausted = false;
	bool m_UploadBudgetExhausted = false;
	bool m_WindowActive = true;
	int m_TextScrollHardCap = 2;
	int m_TextPressureHardCap = 2;
	int m_TextIdleHardCap = 64;
	int m_GlyphScrollHardCap = 1;
	int m_GlyphPressureHardCap = 1;
	int m_GlyphIdleHardCap = 8;
};

struct SSettingsAdaptiveBudgetState
{
	bool m_Initialized = false;
	int m_HealthyFrames = 0;
	int m_BackgroundWindow = 1;
	int m_PrefetchWindow = 1;
	int m_VisibleWindow = 2;
	int m_GpuUploadWindow = 1;
	int m_TextPrebuildWindow = 1;
	int m_DemoMetadataWindow = 1;
};

struct SSettingsAdaptiveBudgetOutput
{
	int m_VisibleTokens = 0;
	int m_PrefetchTokens = 0;
	int m_BackgroundTokens = 0;
	int m_GpuUploadTokens = 0;
	int m_ResourceUploadTokens = 0;
	int m_TextPrebuildTokens = 0;
	int m_TextContainerTokens = 0;
	int m_GlyphRasterizeTokens = 0;
	int m_GlyphUploadTokens = 0;
	int m_ParagraphLayoutTokens = 0;
	int m_MetadataLayoutTokens = 0;
	int m_PreviewArtifactTokens = 0;
	int m_TextureUploadTokens = 0;
	int m_CardDrawTokens = 0;
	int m_DemoMetadataTokens = 0;
	ESettingsAdaptiveBudgetMode m_Mode = ESettingsAdaptiveBudgetMode::IDLE;
	ESettingsAdaptiveBudgetReason m_Reason = ESettingsAdaptiveBudgetReason::NONE;
};

struct SSettingsSkinBackgroundWindowInput
{
	int m_CurrentLimit = 0;
	int m_MinLimit = 0;
	int m_MaxLimit = 0;
	int m_HealthyFrames = 0;
	int m_HealthyFramesToGrow = 4;
	bool m_DrainActive = false;
	bool m_FrameStable = false;
	bool m_VisibleWaiting = false;
	bool m_GpuBudgetExhausted = false;
	bool m_FinalizeBudgetExhausted = false;
	bool m_DecodeJobsSaturated = false;
	bool m_LoadedProgress = false;
	bool m_ConsumerStalled = false;
};

struct SSettingsSkinBackgroundWindowOutput
{
	int m_NextLimit = 0;
	int m_NextHealthyFrames = 0;
	ESettingsSkinBackgroundWindowDecision m_Decision = ESettingsSkinBackgroundWindowDecision::HOLD;
};

struct SSettingsSkinThroughputControllerState
{
	bool m_Initialized = false;
	ESettingsSkinThroughputControllerMode m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	int m_GpuUploadLimitUnits = 0;
	int m_GpuUploadFrameBudget = 0;
	int m_FinalizeBudgetLimit = 0;
	int m_NormalLoadingWindow = 0;
	int m_VisibleLoadingWindow = 0;
	int m_VisibleReserve = 0;
	int m_UnderfedStreak = 0;
};

struct SSettingsSkinThroughputControllerInput
{
	SSettingsResourceFrameContext m_Context;
	bool m_TeeSettingsActive = false;
	float m_FrameTimeAverageMs = 0.0f;
	float m_RenderFrameTimeMs = 0.0f;
	int m_LoadedMax = 0;
	int m_VisibleTotal = 0;
	int m_VisibleReady = 0;
	int m_VisibleWaiting = 0;
	int m_VisibleBackgroundRequested = 0;
	int m_VisibleNonterminalWaiting = 0;
	int m_Requested = 0;
	int m_Pending = 0;
	int m_Loading = 0;
	int m_Loaded = 0;
	int m_RealInflight = 0;
	int m_UploadsDoneDelta = 0;
	int m_LoadedDelta = 0;
	int m_AdmittedDelta = 0;
	int m_StartedDelta = 0;
	int m_GpuUploadRemainingUnits = 0;
	bool m_DecodeJobsSaturated = false;
	const char *m_pLastWaitReason = "none";
	const char *m_pRequestBudgetBlockReason = "none";
};

struct SSettingsSkinThroughputControllerOutput
{
	int m_GpuUploadLimitUnits = 0;
	int m_GpuUploadFrameBudget = 0;
	int m_FinalizeBudgetLimit = 0;
	int m_NormalLoadingWindow = 0;
	int m_VisibleLoadingWindow = 0;
	int m_VisibleReserve = 0;
	int m_BackgroundRequestBudget = 0;
	int m_CountFuseLimit = 0;
	bool m_BackgroundDrainActive = false;
	bool m_AdmissionUnderfed = false;
	int m_UnderfedStreak = 0;
	ESettingsSkinThroughputControllerMode m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	ESettingsSkinThroughputControllerReason m_Reason = ESettingsSkinThroughputControllerReason::NONE;
};

struct SSettingsLoadingPrewarmState
{
	int m_CompletedSteps = 0;
	int m_ConsecutiveNoProgressSteps = 0;
	int m_LastBuiltTextContainers = 0;
	int m_LastMissingTextPlanItems = -1;
	int m_LastMissingTextPlanCollectionUnits = -1;
	bool m_WarmupReady = false;
};

enum class ESettingsWorkshopCatalogSource
{
	LOCAL_ONLY = 0,
	WORKSHOP_CACHE,
	WORKSHOP_HTTP,
};

enum class ESettingsWorkshopBytesSource
{
	LOCAL_INSTALL = 0,
	LOCAL_THUMB_CACHE,
	REMOTE_THUMB_HTTP,
};

struct SSettingsAssetPreviewHandle
{
	int m_Tab = -1;
	unsigned m_Epoch = 0;
	size_t m_Index = 0;
	std::string m_Name;
};

float SettingsSkinPreviewSize(float RowHeight, float PreviewWidth, float RequestedSize);
float SettingsSkinPreviewSize(float RowHeight, float PreviewWidth, float RequestedSize, float PreviewBoundsWidth, float PreviewBoundsHeight);
float SettingsSkinPreviewCenterOffset(float PreviewMinX, float PreviewMaxX);
SSettingsSkinListVisibleRange SettingsSkinListVisibleRangeForScroll(float ScrollY, float ViewHeight, float RowHeight, int ItemsPerRow, int TotalItems, int ExtraRows);
bool SettingsSkinListEntryVisualReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady);
bool SettingsSkinListEntrySourceSettled(bool SourceReady, bool TerminalFailure);
bool SettingsSkinListEntryReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady);
SSettingsSkinListPlan BuildSettingsSkinListPlan(std::vector<SSettingsSkinListEntry> vEntries, int SortMode = 0);
std::vector<int> BuildSettingsCountryFlagWarmupPlan(const std::vector<int> &vCountryCodes);
bool SettingsResourceConsumeMergeEntry(SSettingsResourceMergeBudget &Budget);
bool SettingsResourceConsumeGpuUpload(SSettingsResourceMergeBudget &Budget);
bool SettingsResourceConsumeMergeEntry(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget);
bool SettingsResourceConsumeGpuUpload(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget);
bool SettingsResourceConsumeGpuUploads(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget, int Count);
bool SettingsSkinListPlanGenerationMatches(const SSettingsSkinListPlanResult &Result, int CurrentGeneration);
bool SettingsAssetListJobGenerationMatches(int JobGeneration, int CurrentGeneration);
bool SettingsSkinListShouldPublishMergedList(size_t Cursor, size_t Total);
bool SettingsSkinListShouldReplacePublishedEntries(int PublishedEntries, int PendingEntries, bool DirectoryScanPending, bool MergeComplete);
bool SettingsSkinListSkeletonReady(const SSkinListPlanState &State);
bool SettingsSkinListResourcesSettled(const SSkinListPlanState &State);
bool SettingsSkinListShouldLogAllVisibleReady(bool VisibleSettled, bool AlreadyLogged, int VisibleRows);
bool SettingsSkinListHasPendingMergeWork(bool HasPendingPlan, size_t PendingNames, size_t PendingEntries, size_t Cursor);
int SettingsSkinListFirstPageWarmupEntries(float ListHeight, float RowHeight, int ItemsPerRow, int ExtraRows);
int SettingsTeeSkinListFirstPageWarmupEntries(float ListHeight);
int SettingsLoadingPrewarmMaxAttempts(int BaseWarmupSteps, int TeeFirstPageEntries);
bool SettingsLoadingPrewarmShouldKeepPumping(bool WarmupReady, int CompletedSteps, int MaxAttempts, int ConsecutiveNoProgressSteps);
bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries);
bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries, int PreviousMissingTextPlanItems, int NewMissingTextPlanItems);
bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries, int PreviousMissingTextPlanItems, int NewMissingTextPlanItems, int PreviousMissingTextPlanCollectionUnits, int NewMissingTextPlanCollectionUnits);
void SettingsLoadingPrewarmAdvance(SSettingsLoadingPrewarmState &State, int NewTextPoolEntries, int MissingTextPlanItems, int MissingTextPlanCollectionUnits);
int SettingsSkinListPrefetchCount(int FirstVisibleIndex, int LastVisibleIndex, int ItemsPerRow, int PrefetchRows, int TotalEntries);
int SettingsSkinListBackgroundWarmupCount(int TotalEntries, int MaxEntriesPerFrame);
size_t SettingsSkinBackgroundScanIndex(size_t StartCursor, size_t Attempt, size_t ItemCount);
size_t SettingsSkinBackgroundScanNextCursor(size_t StartCursor, size_t ScannedCount, size_t ItemCount);
bool SettingsSkinBackgroundWarmupShouldRun(bool PageVisible, bool VisibleBacklog, bool InputActive);
bool SettingsSkinBackgroundWarmupWindowFull(size_t Loaded, size_t Loading, size_t Pending, int LoadedMax);
bool SettingsSkinListHasProgressiveWarmEntries(int PublishedEntries, int RequestedEntries, int PlannedEntries);
bool SettingsSkinListSelectionStillValid(int SelectedIndex, int EntryCount);
bool SettingsSkinListScrollResetNeeded(int PreviousCount, int CurrentCount, bool ListActive, bool ScrollbarActive);
bool SettingsSkinListShouldRequestImmediateLoad(bool Visible, bool Prefetched);
bool SettingsSkinListShouldRequestImmediateLoad(bool Visible);
bool SettingsRuntimeWarmupShouldRun(bool WarmupEnabled, bool SettingsPageVisible, bool HasActiveItem, bool HasHotItem, bool ScrollInputActive, bool SettingsPageSwitchActive, bool SettingsScrollActive);
SSettingsResourceFrameContext SettingsBuildFrameContext(bool PersistentScrollActive, bool ImmediateScrollInput, bool JumpScrollActive, int PostScrollRecoveryFrames);
SSettingsResourceFrameContext SettingsBuildFrameContext(bool PersistentScrollActive, bool ImmediateScrollInput, int PostScrollRecoveryFrames);
int SettingsSkinFinalizeMaxPerFrame(bool TeeSettingsActive);
int SettingsSkinGpuUploadUnits(bool TeeSettingsActive);
int SettingsSkinFinalizeFrameBudget(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
int SettingsSkinGpuUploadFrameUnits(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
int SettingsSkinGpuUploadLimiterUnits(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
bool SettingsSkinBackgroundDrainActive(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
int SettingsSkinBackgroundRequestFrameBudget(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
SSettingsSkinBackgroundRequestBudgetOutput SettingsSkinBackgroundRequestBudgetDecision(const SSettingsSkinBackgroundRequestBudgetInput &Input);
SSettingsTeeOffscreenLifecycleOutput SettingsTeeOffscreenLifecycleDecision(const SSettingsTeeOffscreenLifecycleInput &Input);
SSettingsSkinSourceAdmissionOutput SettingsSkinSourceAdmissionDecision(const SSettingsSkinSourceAdmissionInput &Input);
int SettingsSkinSourceLoadNormalWindow(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax);
int SettingsSkinSourceLoadVisibleWindow(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax);
int SettingsSkinSourceCountFuseLimit(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax);
SSettingsSkinBackgroundWindowOutput SettingsSkinBackgroundWindowUpdate(const SSettingsSkinBackgroundWindowInput &Input);
SSettingsSkinThroughputControllerOutput SettingsSkinThroughputControllerStep(const SSettingsSkinThroughputControllerInput &Input, SSettingsSkinThroughputControllerState &State);
SSettingsAdaptiveBudgetOutput SettingsAdaptiveBudgetStep(const SSettingsAdaptiveBudgetInput &Input, SSettingsAdaptiveBudgetState &State);
const char *SettingsSkinBackgroundRequestBlockReasonName(ESettingsSkinBackgroundRequestBlockReason Reason);
const char *SettingsSkinSourceAdmissionBlockReasonName(ESettingsSkinSourceAdmissionBlockReason Reason);
const char *SettingsSkinBackgroundWindowDecisionName(ESettingsSkinBackgroundWindowDecision Decision);
const char *SettingsSkinThroughputControllerModeName(ESettingsSkinThroughputControllerMode Mode);
const char *SettingsSkinThroughputControllerReasonName(ESettingsSkinThroughputControllerReason Reason);
const char *SettingsAdaptiveBudgetModeName(ESettingsAdaptiveBudgetMode Mode);
const char *SettingsAdaptiveBudgetReasonName(ESettingsAdaptiveBudgetReason Reason);
const char *SettingsSkinEffectiveFrameContextName(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive);
size_t SettingsSkinSourceBytesEstimate(int Width, int Height, int PixelCopies);
bool SettingsSkinResidencyShouldReclaim(bool BytesBudgetExceeded, bool CountFuseExceeded);
const char *SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource Source);
const char *SettingsWorkshopBytesSourceName(ESettingsWorkshopBytesSource Source);
void SettingsApplyActiveTeeSkinFrameBudget(SSettingsWarmupFrameBudget &Budget, bool TeeSettingsActive);
bool SettingsSkinFinalizeShouldDeferBackgroundSweep(bool ProcessedHighPrioritySkin, int ProcessedThisFrame, int MaxPerFrame);
bool SettingsAssetListShouldShowBlockingLoading(bool Loading, int VisibleEntries);
bool SettingsAssetListCanStartPreviewDecode(bool Loading, bool Merging, bool Loaded);
bool SettingsAssetPreviewShouldDeferFinalize(int FinalizedThisFrame, double ElapsedMs, int MaxFinalizesPerFrame, double MaxFinalizeMsPerFrame);
bool SettingsAssetWorkAllowedWhileWindowInactive(bool WindowActive, bool HighPriority);
bool SettingsAssetPreviewResidentTextureSatisfiesRequest(bool TextureValid, size_t ResidentBytes, int RequestedTextureSize);
bool SettingsAssetPreviewDecodeStartNeeded(bool DecodeJobActive, bool TextureValid, size_t ResidentBytes, int RequestedTextureSize, bool HasReadyImage);
bool SettingsAssetPreviewShouldPrioritizeVisibleRange(int Index, int FirstVisibleIndex, int LastVisibleIndex);
bool SettingsAssetPreviewShouldUploadHighPriorityFirst(bool CurrentHighPriority, bool CandidateHighPriority);
bool SettingsWorkshopThumbShouldStartHighPriority(int VisibleDownloadableIndex, int FirstVisibleDownloadableIndex, int LastVisibleDownloadableIndex);
bool SettingsResourceCanUseHighPriorityBudget(int StartedThisFrame, int NormalBudget, int HighPriorityBudget, bool HighPriority);
int SettingsResourceFrameStageBudget(const SSettingsResourceFrameContext &Context, ESettingsResourcePriority Priority, int NormalBudget, int ScrollActiveVisibleBudget);
int SettingsScrollInteractionCooldown(bool ActiveThisFrame, int CurrentCooldownFrames, int CooldownFrames);
int SettingsScrollInteractionRecovery(bool ScrollActiveThisFrame, int PreviousCooldownFrames, int CurrentCooldownFrames, int CurrentRecoveryFrames, int RecoveryFrames);
int SettingsResourceSharedHeavyBudget(const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget);
int SettingsResourceClampSharedHeavyBudget(int RemainingBudget, const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget);
int SettingsResourceClampSharedHeavyBudgetWithScrollTrickle(int RemainingBudget, const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget, int ScrollTrickleBatches);
bool SettingsResourceConsumeSharedHeavyBudget(int &RemainingBudget);
bool SettingsResourceUploadWithinByteBudget(int UploadedThisFrame, size_t UploadedBytesThisFrame, size_t ItemBytes, size_t MaxBytesPerFrame);
bool SettingsResourceOversizedUploadAllowed(const SSettingsResourceFrameContext &Context, bool PageSwitchActive, ESettingsResourcePriority Priority, int OversizedUploadsThisFrame, size_t ItemBytes, size_t MaxBytesPerFrame);
size_t SettingsAssetPreviewResidentBudgetBytes(size_t OverrideMb, int Percent, float GpuBudgetMb);
int SettingsAssetPreviewBudgetedTextureSize(int MaxTextureSize, int MinTextureSize, size_t TextureBudgetBytes, size_t CurrentTextureMemoryBytes, size_t ResidentPreviewBytes);
std::string SettingsAssetPreviewHandleKey(const SSettingsAssetPreviewHandle &Handle);
bool SettingsAssetPreviewHandleMatches(const SSettingsAssetPreviewHandle &Handle, int CurrentTab, unsigned CurrentEpoch, size_t CurrentIndex, const char *pName);
// 只看「这个句柄是不是还在说这张卡」：列表在软重扫合并后会重新排序，条目下标
// 会变，名字在同一 tab 内唯一且稳定，所以跨帧收尾一律按名字认领。
bool SettingsAssetPreviewHandleReferencesAsset(const SSettingsAssetPreviewHandle &Handle, const char *pName);
const char *SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason StopReason);
bool SettingsAssetWarmupAllTabsReady(const bool *pReadyTabs, int TabCount);
int SettingsAssetWarmupNextTab(int CurrentTab, int TabCount);

#endif // GAME_CLIENT_COMPONENTS_SETTINGS_RESOURCE_JOBS_H
