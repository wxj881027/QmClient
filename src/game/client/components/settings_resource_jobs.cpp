#include <base/system.h>

#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>

#include <algorithm>
#include <cmath>

namespace
{

	constexpr int TEE_IDLE_DRAIN_FINALIZE_BUDGET = 32;

}

float SettingsSkinPreviewSize(float RowHeight, float PreviewWidth, float RequestedSize)
{
	const float MaxSize = std::max(0.0f, std::min(RowHeight, PreviewWidth) - 10.0f);
	return std::clamp(RequestedSize, 0.0f, MaxSize);
}

float SettingsSkinPreviewSize(float RowHeight, float PreviewWidth, float RequestedSize, float PreviewBoundsWidth, float PreviewBoundsHeight)
{
	const float MaxSize = std::max(0.0f, std::min(RowHeight, PreviewWidth) - 10.0f);
	const float BoundsExtent = std::max(PreviewBoundsWidth, PreviewBoundsHeight);
	if(MaxSize <= 0.0f || BoundsExtent <= 0.0f)
		return 0.0f;
	return std::clamp(RequestedSize * (MaxSize / BoundsExtent), 0.0f, MaxSize);
}

float SettingsSkinPreviewCenterOffset(float PreviewMinX, float PreviewMaxX)
{
	return -(PreviewMinX + PreviewMaxX) * 0.5f;
}

SSettingsSkinListVisibleRange SettingsSkinListVisibleRangeForScroll(float ScrollY, float ViewHeight, float RowHeight, int ItemsPerRow, int TotalItems, int ExtraRows)
{
	SSettingsSkinListVisibleRange Range;
	Range.m_TotalItems = std::max(0, TotalItems);
	if(Range.m_TotalItems <= 0 || RowHeight <= 0.0f || ItemsPerRow <= 0 || ViewHeight <= 0.0f)
		return Range;

	Range.m_TotalRows = (Range.m_TotalItems + ItemsPerRow - 1) / ItemsPerRow;
	const float TotalHeight = Range.m_TotalRows * RowHeight;
	const float MaxScrollY = std::max(0.0f, TotalHeight - ViewHeight);
	const float ClampedScrollY = std::clamp(ScrollY, 0.0f, MaxScrollY);
	const int SafeExtraRows = std::max(0, ExtraRows);
	const int FirstRow = std::max(0, (int)std::floor(ClampedScrollY / RowHeight) - SafeExtraRows);
	const int LastRowExclusive = std::min(
		Range.m_TotalRows,
		(int)std::ceil((ClampedScrollY + ViewHeight) / RowHeight) + SafeExtraRows);

	Range.m_FirstVisibleRow = std::min(FirstRow, Range.m_TotalRows);
	Range.m_LastVisibleRow = LastRowExclusive > Range.m_FirstVisibleRow ? LastRowExclusive - 1 : Range.m_FirstVisibleRow - 1;
	Range.m_FirstItem = std::min(Range.m_FirstVisibleRow * ItemsPerRow, Range.m_TotalItems);
	Range.m_EndItem = std::min(LastRowExclusive * ItemsPerRow, Range.m_TotalItems);
	Range.m_VisibleRows = std::max(0, LastRowExclusive - Range.m_FirstVisibleRow);
	Range.m_RenderedItems = std::max(0, Range.m_EndItem - Range.m_FirstItem);
	Range.m_SkippedItems = std::max(0, Range.m_TotalItems - Range.m_RenderedItems);
	return Range;
}

bool SettingsSkinListEntryVisualReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady)
{
	return SourceReady || TerminalFailure || PreviewCacheReady;
}

bool SettingsSkinListEntrySourceSettled(bool SourceReady, bool TerminalFailure)
{
	return SourceReady || TerminalFailure;
}

bool SettingsSkinListEntryReady(bool SourceReady, bool TerminalFailure, bool PreviewCacheReady)
{
	return SettingsSkinListEntryVisualReady(SourceReady, TerminalFailure, PreviewCacheReady);
}

SSettingsSkinListPlan BuildSettingsSkinListPlan(std::vector<SSettingsSkinListEntry> vEntries, int SortMode)
{
	std::stable_sort(vEntries.begin(), vEntries.end(), [SortMode](const SSettingsSkinListEntry &A, const SSettingsSkinListEntry &B) {
		if(A.m_Selected != B.m_Selected)
			return A.m_Selected && !B.m_Selected;
		if(A.m_Favorite != B.m_Favorite)
			return A.m_Favorite && !B.m_Favorite;
		if(SortMode == 1)
		{
			if(A.m_OfficialReleaseDate != B.m_OfficialReleaseDate)
				return A.m_OfficialReleaseDate > B.m_OfficialReleaseDate;
			if(A.m_LastModified != B.m_LastModified)
				return A.m_LastModified > B.m_LastModified;
		}
		return A.m_Name < B.m_Name;
	});

	SSettingsSkinListPlan Plan;
	Plan.m_vNames.reserve(vEntries.size());
	Plan.m_vEntries.reserve(vEntries.size());
	for(const SSettingsSkinListEntry &Entry : vEntries)
	{
		Plan.m_vNames.push_back(Entry.m_Name);
		Plan.m_vEntries.push_back(Entry);
	}
	return Plan;
}

std::vector<int> BuildSettingsCountryFlagWarmupPlan(const std::vector<int> &vCountryCodes)
{
	std::vector<int> vPlan;
	vPlan.reserve(vCountryCodes.size());
	for(int CountryCode : vCountryCodes)
	{
		if(std::find(vPlan.begin(), vPlan.end(), CountryCode) == vPlan.end())
			vPlan.push_back(CountryCode);
	}
	return vPlan;
}

bool SettingsResourceConsumeMergeEntry(SSettingsResourceMergeBudget &Budget)
{
	if(Budget.m_MaxListEntries <= 0)
	{
		Budget.m_StopReason = ESettingsWarmupStopReason::MERGE_BUDGET;
		return false;
	}

	--Budget.m_MaxListEntries;
	return true;
}

bool SettingsResourceConsumeGpuUpload(SSettingsResourceMergeBudget &Budget)
{
	if(Budget.m_MaxGpuUploads <= 0)
	{
		Budget.m_StopReason = ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET;
		return false;
	}

	--Budget.m_MaxGpuUploads;
	return true;
}

bool SettingsResourceConsumeMergeEntry(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget)
{
	if(pFrameBudget != nullptr && !Budget.m_FrameMergeBudgetConsumed)
	{
		if(!SettingsWarmupConsumeBudget(*pFrameBudget, ESettingsWarmupCost::JOB_RESULT_MERGE))
		{
			Budget.m_StopReason = pFrameBudget->m_StopReason;
			return false;
		}
		Budget.m_FrameMergeBudgetConsumed = true;
	}
	if(!SettingsResourceConsumeMergeEntry(Budget))
		return false;
	return true;
}

bool SettingsResourceConsumeGpuUpload(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget)
{
	if(!SettingsResourceConsumeGpuUpload(Budget))
		return false;
	if(pFrameBudget == nullptr)
		return true;
	if(SettingsWarmupConsumeBudget(*pFrameBudget, ESettingsWarmupCost::GPU_UPLOAD))
		return true;
	Budget.m_StopReason = pFrameBudget->m_StopReason;
	++Budget.m_MaxGpuUploads;
	return false;
}

bool SettingsResourceConsumeGpuUploads(SSettingsResourceMergeBudget &Budget, SSettingsWarmupFrameBudget *pFrameBudget, int Count)
{
	if(Count <= 0)
		return true;
	for(int Upload = 0; Upload < Count; ++Upload)
	{
		if(!SettingsResourceConsumeGpuUpload(Budget, pFrameBudget))
			return false;
	}
	return true;
}

bool SettingsSkinListPlanGenerationMatches(const SSettingsSkinListPlanResult &Result, int CurrentGeneration)
{
	return Result.m_Generation == CurrentGeneration;
}

bool SettingsAssetListJobGenerationMatches(int JobGeneration, int CurrentGeneration)
{
	return JobGeneration == CurrentGeneration;
}

bool SettingsSkinListShouldPublishMergedList(size_t Cursor, size_t Total)
{
	return Total == 0 || Cursor >= Total;
}

bool SettingsSkinListShouldReplacePublishedEntries(int PublishedEntries, int PendingEntries, bool DirectoryScanPending, bool MergeComplete)
{
	if(!MergeComplete)
		return false;
	if(DirectoryScanPending && PendingEntries <= PublishedEntries)
		return false;
	return true;
}

bool SettingsSkinListSkeletonReady(const SSkinListPlanState &State)
{
	if(State.m_DirectoryScanPending)
		return false;
	if(State.m_MergeComplete)
		return true;
	return State.m_ItemCount > 0;
}

bool SettingsSkinListResourcesSettled(const SSkinListPlanState &State)
{
	return SettingsSkinListSkeletonReady(State) &&
	       State.m_MergeComplete &&
	       State.m_VisibleBacklog == 0 &&
	       State.m_BackgroundBacklog == 0;
}

bool SettingsSkinListShouldLogAllVisibleReady(bool VisibleSettled, bool AlreadyLogged, int VisibleRows)
{
	return VisibleSettled && !AlreadyLogged && VisibleRows > 0;
}

bool SettingsSkinListHasPendingMergeWork(bool HasPendingPlan, size_t PendingNames, size_t PendingEntries, size_t Cursor)
{
	if(!HasPendingPlan)
		return false;
	return PendingNames == 0 ||
	       Cursor < PendingNames ||
	       PendingEntries < PendingNames ||
	       (Cursor >= PendingNames && PendingEntries >= PendingNames);
}

int SettingsSkinListFirstPageWarmupEntries(float ListHeight, float RowHeight, int ItemsPerRow, int ExtraRows)
{
	if(ListHeight <= 0.0f || RowHeight <= 0.0f || ItemsPerRow <= 0)
		return 0;

	const int VisibleRows = std::max(1, (int)std::ceil(ListHeight / RowHeight));
	const int WarmRows = std::max(1, VisibleRows + std::max(0, ExtraRows));
	return WarmRows * ItemsPerRow;
}

int SettingsTeeSkinListFirstPageWarmupEntries(float ListHeight)
{
	constexpr float TeeSkinListRowHeight = 50.0f;
	constexpr int TeeSkinListItemsPerRow = 4;
	constexpr int TeeSkinListWarmupExtraRows = 1;
	constexpr int TeeSkinListWarmupMinimumEntries = 24;
	return maximum(
		TeeSkinListWarmupMinimumEntries,
		SettingsSkinListFirstPageWarmupEntries(
			ListHeight,
			TeeSkinListRowHeight,
			TeeSkinListItemsPerRow,
			TeeSkinListWarmupExtraRows));
}

int SettingsLoadingPrewarmMaxAttempts(int BaseWarmupSteps, int TeeFirstPageEntries)
{
	const int BaseAttempts = maximum(BaseWarmupSteps * 4, 1);
	const int TeeSourceSettleAttempts = maximum(maximum(TeeFirstPageEntries, 0) * 2, 32);
	return BaseAttempts + TeeSourceSettleAttempts;
}

bool SettingsLoadingPrewarmShouldKeepPumping(bool WarmupReady, int CompletedSteps, int MaxAttempts, int ConsecutiveNoProgressSteps)
{
	if(WarmupReady)
		return false;
	if(CompletedSteps < MaxAttempts)
		return true;
	return ConsecutiveNoProgressSteps < 8;
}

bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries)
{
	return NewTextPoolEntries > PreviousTextPoolEntries;
}

bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries, int PreviousMissingTextPlanItems, int NewMissingTextPlanItems)
{
	return SettingsLoadingPrewarmMadeProgress(PreviousTextPoolEntries, NewTextPoolEntries) ||
	       (NewMissingTextPlanItems >= 0 && (PreviousMissingTextPlanItems < 0 || NewMissingTextPlanItems < PreviousMissingTextPlanItems));
}

bool SettingsLoadingPrewarmMadeProgress(int PreviousTextPoolEntries, int NewTextPoolEntries, int PreviousMissingTextPlanItems, int NewMissingTextPlanItems, int PreviousMissingTextPlanCollectionUnits, int NewMissingTextPlanCollectionUnits)
{
	return SettingsLoadingPrewarmMadeProgress(PreviousTextPoolEntries, NewTextPoolEntries, PreviousMissingTextPlanItems, NewMissingTextPlanItems) ||
	       (NewMissingTextPlanCollectionUnits >= 0 && (PreviousMissingTextPlanCollectionUnits < 0 || NewMissingTextPlanCollectionUnits < PreviousMissingTextPlanCollectionUnits));
}

void SettingsLoadingPrewarmAdvance(SSettingsLoadingPrewarmState &State, int NewTextPoolEntries, int MissingTextPlanItems, int MissingTextPlanCollectionUnits)
{
	const bool MadeProgress = SettingsLoadingPrewarmMadeProgress(State.m_LastBuiltTextContainers, NewTextPoolEntries, State.m_LastMissingTextPlanItems, MissingTextPlanItems, State.m_LastMissingTextPlanCollectionUnits, MissingTextPlanCollectionUnits);
	if(MissingTextPlanItems <= 0 && MissingTextPlanCollectionUnits <= 0)
	{
		State.m_ConsecutiveNoProgressSteps = 0;
		State.m_WarmupReady = true;
	}
	else if(MadeProgress)
	{
		State.m_ConsecutiveNoProgressSteps = 0;
		State.m_WarmupReady = false;
	}
	else
	{
		++State.m_ConsecutiveNoProgressSteps;
		State.m_WarmupReady = false;
	}

	State.m_LastBuiltTextContainers = NewTextPoolEntries;
	State.m_LastMissingTextPlanItems = MissingTextPlanItems;
	State.m_LastMissingTextPlanCollectionUnits = MissingTextPlanCollectionUnits;
	++State.m_CompletedSteps;
}

int SettingsSkinListPrefetchCount(int FirstVisibleIndex, int LastVisibleIndex, int ItemsPerRow, int PrefetchRows, int TotalEntries)
{
	if(FirstVisibleIndex < 0 || LastVisibleIndex < FirstVisibleIndex || ItemsPerRow <= 0 || PrefetchRows <= 0 || TotalEntries <= 0)
		return 0;

	const int PrefetchItems = ItemsPerRow * PrefetchRows;
	const int PrefetchStart = LastVisibleIndex + 1;
	if(PrefetchStart >= TotalEntries)
		return 0;
	const int Remaining = TotalEntries - PrefetchStart;
	return std::min(PrefetchItems, Remaining);
}

int SettingsSkinListBackgroundWarmupCount(int TotalEntries, int MaxEntriesPerFrame)
{
	if(TotalEntries <= 0 || MaxEntriesPerFrame <= 0)
		return 0;
	return std::min(TotalEntries, MaxEntriesPerFrame);
}

size_t SettingsSkinBackgroundScanIndex(size_t StartCursor, size_t Attempt, size_t ItemCount)
{
	return ItemCount == 0 ? 0 : (StartCursor + Attempt) % ItemCount;
}

size_t SettingsSkinBackgroundScanNextCursor(size_t StartCursor, size_t ScannedCount, size_t ItemCount)
{
	return SettingsSkinBackgroundScanIndex(StartCursor, ScannedCount, ItemCount);
}

bool SettingsSkinBackgroundWarmupShouldRun(bool PageVisible, bool VisibleBacklog, bool InputActive)
{
	return PageVisible && !VisibleBacklog && !InputActive;
}

bool SettingsSkinBackgroundWarmupWindowFull(size_t Loaded, size_t Loading, size_t Pending, int LoadedMax)
{
	(void)Loaded;
	return LoadedMax > 0 && Loading + Pending >= (size_t)LoadedMax;
}

bool SettingsSkinListHasProgressiveWarmEntries(int PublishedEntries, int RequestedEntries, int PlannedEntries)
{
	if(PublishedEntries <= 0)
		return false;
	if(RequestedEntries <= 0)
		return true;
	if(PlannedEntries > 0 && PublishedEntries >= PlannedEntries)
		return true;
	return PublishedEntries >= RequestedEntries;
}
bool SettingsSkinListSelectionStillValid(int SelectedIndex, int EntryCount)
{
	return SelectedIndex < 0 || (SelectedIndex >= 0 && SelectedIndex < EntryCount);
}

bool SettingsSkinListScrollResetNeeded(int PreviousCount, int CurrentCount, bool ListActive, bool ScrollbarActive)
{
	if(CurrentCount >= PreviousCount)
		return false;
	if(ScrollbarActive)
		return false;
	return !ListActive;
}

bool SettingsSkinListShouldRequestImmediateLoad(bool Visible, bool Prefetched)
{
	return Visible || Prefetched;
}

bool SettingsSkinListShouldRequestImmediateLoad(bool Visible)
{
	return SettingsSkinListShouldRequestImmediateLoad(Visible, false);
}

bool SettingsRuntimeWarmupShouldRun(bool WarmupEnabled, bool SettingsPageVisible, bool HasActiveItem, bool HasHotItem, bool ScrollInputActive, bool SettingsPageSwitchActive, bool SettingsScrollActive)
{
	return WarmupEnabled &&
	       SettingsPageVisible &&
	       !HasActiveItem &&
	       !HasHotItem &&
	       !ScrollInputActive &&
	       !SettingsPageSwitchActive &&
	       !SettingsScrollActive;
}

SSettingsResourceFrameContext SettingsBuildFrameContext(bool PersistentScrollActive, bool ImmediateScrollInput, bool JumpScrollActive, int PostScrollRecoveryFrames)
{
	SSettingsResourceFrameContext Context;
	Context.m_ScrollActive = PersistentScrollActive || ImmediateScrollInput;
	Context.m_JumpScrollActive = JumpScrollActive;
	Context.m_PostScrollRecoveryFrames = PostScrollRecoveryFrames;
	return Context;
}

SSettingsResourceFrameContext SettingsBuildFrameContext(bool PersistentScrollActive, bool ImmediateScrollInput, int PostScrollRecoveryFrames)
{
	return SettingsBuildFrameContext(PersistentScrollActive, ImmediateScrollInput, false, PostScrollRecoveryFrames);
}

int SettingsSkinFinalizeMaxPerFrame(bool TeeSettingsActive)
{
	return TeeSettingsActive ? 64 : 12;
}

int SettingsSkinGpuUploadUnits(bool TeeSettingsActive)
{
	return TeeSettingsActive ? 8 : 1;
}

static int SettingsRecoveryBudget(const SSettingsResourceFrameContext &Context, int ScrollBudget, int IdleBudget)
{
	const int RemainingFrames = maximum(Context.m_PostScrollRecoveryFrames, 0);
	if(RemainingFrames <= 0)
		return IdleBudget;
	const int RecoveryFrames = maximum(2, RemainingFrames);
	const int CompletedFrames = std::clamp(RecoveryFrames - RemainingFrames + 1, 1, RecoveryFrames);
	return ScrollBudget + (IdleBudget - ScrollBudget) * CompletedFrames / (RecoveryFrames + 1);
}

int SettingsSkinFinalizeFrameBudget(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return SettingsSkinFinalizeMaxPerFrame(false);
	if(Context.m_ScrollActive)
		return 16;
	if(Context.m_PostScrollRecoveryFrames > 0)
		return SettingsRecoveryBudget(Context, 16, SettingsSkinFinalizeMaxPerFrame(true));
	return SettingsSkinFinalizeMaxPerFrame(true);
}

int SettingsSkinGpuUploadFrameUnits(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return SettingsSkinGpuUploadUnits(false);
	if(Context.m_ScrollActive)
		return 4;
	if(Context.m_PostScrollRecoveryFrames > 0)
		return SettingsRecoveryBudget(Context, 4, SettingsSkinGpuUploadUnits(true));
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return 12;
	return SettingsSkinGpuUploadUnits(true);
}

int SettingsSkinGpuUploadLimiterUnits(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return 0;
	if(Context.m_ScrollActive)
		return 96;
	if(Context.m_PostScrollRecoveryFrames > 0)
		return SettingsRecoveryBudget(Context, 96, 192);
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return 288;
	return 192;
}

bool SettingsSkinBackgroundDrainActive(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	return TeeSettingsActive &&
	       !Context.m_ScrollActive &&
	       Context.m_PostScrollRecoveryFrames == 0 &&
	       Context.m_HighPrioritySettled;
}

int SettingsSkinBackgroundRequestFrameBudget(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return 0;
	if(Context.m_ScrollActive || Context.m_PostScrollRecoveryFrames > 0)
		return 0;
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return 8;
	return 6;
}

SSettingsSkinBackgroundRequestBudgetOutput SettingsSkinBackgroundRequestBudgetDecision(const SSettingsSkinBackgroundRequestBudgetInput &Input)
{
	SSettingsSkinBackgroundRequestBudgetOutput Output;
	Output.m_RealInflight = maximum(0, Input.m_Pending) + maximum(0, Input.m_Loading);
	if(!Input.m_DrainActive || Input.m_DefaultBudget <= 0)
	{
		Output.m_BlockReason = ESettingsSkinBackgroundRequestBlockReason::DRAIN_INACTIVE;
		return Output;
	}

	const int CountFuseLimit = maximum(0, Input.m_CountFuseLimit);
	const int VisibleReserve = maximum(0, Input.m_VisibleReserve);
	const int Available = maximum(0, CountFuseLimit - Output.m_RealInflight);
	if(Available <= VisibleReserve)
	{
		Output.m_BlockReason = ESettingsSkinBackgroundRequestBlockReason::VISIBLE_RESERVE;
		return Output;
	}

	const int HardBacklogLimit = maximum(CountFuseLimit, maximum(Input.m_DefaultBudget, 1) * 8);
	if(Input.m_BackgroundRequested >= HardBacklogLimit)
	{
		Output.m_BlockReason = ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE;
		return Output;
	}

	const int BacklogHighWatermark = maximum(VisibleReserve * 8, CountFuseLimit * 2);
	if(Input.m_BackgroundRequested >= BacklogHighWatermark &&
		Input.m_RecentLoadedDelta <= 0 &&
		Input.m_RecentAdmittedDelta <= 0)
	{
		Output.m_BlockReason = ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE;
		return Output;
	}

	Output.m_RequestBudget = minimum(Input.m_DefaultBudget, maximum(0, Available - VisibleReserve));
	return Output;
}

SSettingsTeeOffscreenLifecycleOutput SettingsTeeOffscreenLifecycleDecision(const SSettingsTeeOffscreenLifecycleInput &Input)
{
	SSettingsTeeOffscreenLifecycleOutput Output;
	Output.m_FullListReady = Input.m_TotalEntries > 0 &&
				 Input.m_ValidEntries == Input.m_TotalEntries &&
				 Input.m_SettledEntries == Input.m_TotalEntries;
	Output.m_CompleteDrainSession = Input.m_DrainSessionActive && Output.m_FullListReady;
	Output.m_LogCompletion = Output.m_CompleteDrainSession && Input.m_PerfDebugEnabled;
	return Output;
}

SSettingsSkinSourceAdmissionOutput SettingsSkinSourceAdmissionDecision(const SSettingsSkinSourceAdmissionInput &Input)
{
	SSettingsSkinSourceAdmissionOutput Output;
	Output.m_PromotePriority = Input.m_Priority;
	Output.m_CountFuseApplies = Input.m_Priority != ESettingsResourcePriority::VISIBLE;

	if(Input.m_BackgroundRequested &&
		Input.m_Priority == ESettingsResourcePriority::BACKGROUND &&
		!Input.m_BackgroundDrainActive)
	{
		Output.m_PromoteAllowed = false;
		Output.m_BlockReason = ESettingsSkinSourceAdmissionBlockReason::DRAIN_INACTIVE;
		return Output;
	}

	const bool HighPriority = Input.m_Priority == ESettingsResourcePriority::VISIBLE;
	if(!SettingsResourceCanUseHighPriorityBudget(
		   maximum(0, Input.m_Loading),
		   maximum(0, Input.m_NormalLoadingWindow),
		   maximum(0, Input.m_VisibleLoadingWindow),
		   HighPriority))
	{
		Output.m_PromoteAllowed = false;
		Output.m_BlockReason = HighPriority ?
					       ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE :
					       ESettingsSkinSourceAdmissionBlockReason::LOADING_WINDOW;
	}

	return Output;
}

static int SettingsSkinSourceLoadWindowClamp(int BaseWindow, int LoadedMax)
{
	if(LoadedMax > 0)
		return minimum(BaseWindow, LoadedMax);
	return BaseWindow;
}

int SettingsSkinSourceLoadNormalWindow(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax)
{
	if(!TeeSettingsActive)
		return SettingsSkinSourceLoadWindowClamp(maximum(LoadedMax, 0), LoadedMax);
	if(Context.m_ScrollActive)
		return SettingsSkinSourceLoadWindowClamp(48, LoadedMax);
	if(Context.m_PostScrollRecoveryFrames > 0)
		return SettingsSkinSourceLoadWindowClamp(SettingsRecoveryBudget(Context, 48, 256), LoadedMax);
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return 256;
	return SettingsSkinSourceLoadWindowClamp(256, LoadedMax);
}

int SettingsSkinSourceLoadVisibleWindow(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax)
{
	if(!TeeSettingsActive)
		return SettingsSkinSourceLoadWindowClamp(maximum(LoadedMax, 0), LoadedMax);
	if(Context.m_ScrollActive)
		return SettingsSkinSourceLoadWindowClamp(128, LoadedMax);
	if(Context.m_PostScrollRecoveryFrames > 0)
		return SettingsSkinSourceLoadWindowClamp(SettingsRecoveryBudget(Context, 128, 256), LoadedMax);
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return 256;
	return SettingsSkinSourceLoadWindowClamp(256, LoadedMax);
}

int SettingsSkinSourceCountFuseLimit(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive, int LoadedMax)
{
	const int EffectiveLoadedMax = maximum(LoadedMax, 0);
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return maximum(EffectiveLoadedMax, 128);
	return EffectiveLoadedMax;
}

namespace
{
	struct SSettingsSkinThroughputProfile
	{
		int m_GpuUploadLimitMin = 0;
		int m_GpuUploadLimitMax = 0;
		int m_GpuUploadLimitStep = 24;
		int m_FinalizeBudgetMin = 0;
		int m_FinalizeBudgetMax = 0;
		int m_FinalizeBudgetStep = 16;
		int m_NormalWindowMin = 0;
		int m_NormalWindowMax = 0;
		int m_NormalWindowStep = 32;
		int m_VisibleWindowMin = 0;
		int m_VisibleWindowMax = 0;
		int m_VisibleWindowStep = 32;
		int m_VisibleReserveMin = 0;
		int m_VisibleReserveMax = 0;
		int m_BackgroundRequestBudget = 0;
	};

	static int ClampSettingsSkinLoadWindow(int BaseWindow, int LoadedMax)
	{
		if(LoadedMax > 0)
			return minimum(BaseWindow, LoadedMax);
		return BaseWindow;
	}

	static SSettingsSkinThroughputProfile SettingsSkinThroughputProfileForMode(ESettingsSkinThroughputControllerMode Mode, int LoadedMax)
	{
		SSettingsSkinThroughputProfile Profile;
		switch(Mode)
		{
		case ESettingsSkinThroughputControllerMode::SCROLL_ACTIVE:
			Profile.m_GpuUploadLimitMin = 96;
			Profile.m_GpuUploadLimitMax = 144;
			Profile.m_FinalizeBudgetMin = 16;
			Profile.m_FinalizeBudgetMax = 24;
			Profile.m_FinalizeBudgetStep = 8;
			Profile.m_NormalWindowMin = ClampSettingsSkinLoadWindow(48, LoadedMax);
			Profile.m_NormalWindowMax = ClampSettingsSkinLoadWindow(64, LoadedMax);
			Profile.m_NormalWindowStep = 16;
			Profile.m_VisibleWindowMin = ClampSettingsSkinLoadWindow(128, LoadedMax);
			Profile.m_VisibleWindowMax = ClampSettingsSkinLoadWindow(160, LoadedMax);
			Profile.m_VisibleWindowStep = 16;
			Profile.m_VisibleReserveMin = 8;
			Profile.m_VisibleReserveMax = 8;
			Profile.m_BackgroundRequestBudget = 0;
			break;
		case ESettingsSkinThroughputControllerMode::POST_SCROLL_RECOVERY:
			Profile.m_GpuUploadLimitMin = 144;
			Profile.m_GpuUploadLimitMax = 240;
			Profile.m_FinalizeBudgetMin = 32;
			Profile.m_FinalizeBudgetMax = 64;
			Profile.m_NormalWindowMin = ClampSettingsSkinLoadWindow(96, LoadedMax);
			Profile.m_NormalWindowMax = ClampSettingsSkinLoadWindow(160, LoadedMax);
			Profile.m_VisibleWindowMin = ClampSettingsSkinLoadWindow(160, LoadedMax);
			Profile.m_VisibleWindowMax = ClampSettingsSkinLoadWindow(224, LoadedMax);
			Profile.m_VisibleReserveMin = 0;
			Profile.m_VisibleReserveMax = 4;
			Profile.m_BackgroundRequestBudget = 0;
			break;
		case ESettingsSkinThroughputControllerMode::IDLE_DRAIN:
			Profile.m_GpuUploadLimitMin = 192;
			Profile.m_GpuUploadLimitMax = 384;
			Profile.m_FinalizeBudgetMin = TEE_IDLE_DRAIN_FINALIZE_BUDGET;
			Profile.m_FinalizeBudgetMax = TEE_IDLE_DRAIN_FINALIZE_BUDGET;
			Profile.m_NormalWindowMin = ClampSettingsSkinLoadWindow(192, LoadedMax);
			Profile.m_NormalWindowMax = ClampSettingsSkinLoadWindow(320, LoadedMax);
			Profile.m_VisibleWindowMin = ClampSettingsSkinLoadWindow(192, LoadedMax);
			Profile.m_VisibleWindowMax = ClampSettingsSkinLoadWindow(192, LoadedMax);
			Profile.m_VisibleReserveMin = 0;
			Profile.m_VisibleReserveMax = 0;
			Profile.m_BackgroundRequestBudget = 8;
			break;
		case ESettingsSkinThroughputControllerMode::IDLE_VISIBLE:
		default:
			Profile.m_GpuUploadLimitMin = 192;
			Profile.m_GpuUploadLimitMax = 288;
			Profile.m_FinalizeBudgetMin = 48;
			Profile.m_FinalizeBudgetMax = 80;
			Profile.m_NormalWindowMin = ClampSettingsSkinLoadWindow(128, LoadedMax);
			Profile.m_NormalWindowMax = ClampSettingsSkinLoadWindow(256, LoadedMax);
			Profile.m_VisibleWindowMin = ClampSettingsSkinLoadWindow(192, LoadedMax);
			Profile.m_VisibleWindowMax = ClampSettingsSkinLoadWindow(256, LoadedMax);
			Profile.m_VisibleReserveMin = 0;
			Profile.m_VisibleReserveMax = 2;
			Profile.m_BackgroundRequestBudget = 6;
			break;
		}
		return Profile;
	}

	static int StepTowardLimit(int Value, int Target, int Step)
	{
		if(Value < Target)
			return minimum(Target, Value + maximum(1, Step));
		if(Value > Target)
			return maximum(Target, Value - maximum(1, Step));
		return Value;
	}

	static int StepUpClamped(int Value, int Step, int MaxValue)
	{
		return minimum(MaxValue, Value + maximum(1, Step));
	}

	static int StepDownClamped(int Value, int Step, int MinValue)
	{
		return maximum(MinValue, Value - maximum(1, Step));
	}
}

SSettingsSkinThroughputControllerOutput SettingsSkinThroughputControllerStep(const SSettingsSkinThroughputControllerInput &Input, SSettingsSkinThroughputControllerState &State)
{
	SSettingsSkinThroughputControllerOutput Output;
	if(!Input.m_TeeSettingsActive)
	{
		State = {};
		Output.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
		Output.m_Reason = ESettingsSkinThroughputControllerReason::NONE;
		return Output;
	}

	const bool IdleDrainAllowed =
		!Input.m_Context.m_ScrollActive &&
		Input.m_Context.m_PostScrollRecoveryFrames == 0 &&
		Input.m_Context.m_HighPrioritySettled &&
		Input.m_VisibleWaiting <= 0;
	const ESettingsSkinThroughputControllerMode Mode =
		Input.m_Context.m_ScrollActive                 ? ESettingsSkinThroughputControllerMode::SCROLL_ACTIVE :
		Input.m_Context.m_PostScrollRecoveryFrames > 0 ? ESettingsSkinThroughputControllerMode::POST_SCROLL_RECOVERY :
		IdleDrainAllowed                               ? ESettingsSkinThroughputControllerMode::IDLE_DRAIN :
								 ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	const SSettingsSkinThroughputProfile Profile = SettingsSkinThroughputProfileForMode(Mode, maximum(0, Input.m_LoadedMax));

	if(!State.m_Initialized)
	{
		State.m_Initialized = true;
		State.m_Mode = Mode;
		State.m_GpuUploadLimitUnits = Profile.m_GpuUploadLimitMin;
		State.m_GpuUploadFrameBudget = maximum(1, State.m_GpuUploadLimitUnits / 24);
		State.m_FinalizeBudgetLimit = Profile.m_FinalizeBudgetMin;
		State.m_NormalLoadingWindow = Profile.m_NormalWindowMin;
		State.m_VisibleLoadingWindow = Profile.m_VisibleWindowMin;
		State.m_VisibleReserve = Profile.m_VisibleReserveMax;
		State.m_UnderfedStreak = 0;
	}

	if(State.m_Mode != Mode)
	{
		State.m_Mode = Mode;
		State.m_GpuUploadLimitUnits = std::clamp(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitMin, Profile.m_GpuUploadLimitMax);
		State.m_FinalizeBudgetLimit = std::clamp(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetMin, Profile.m_FinalizeBudgetMax);
		State.m_NormalLoadingWindow = std::clamp(State.m_NormalLoadingWindow, Profile.m_NormalWindowMin, maximum(Profile.m_NormalWindowMin, Profile.m_NormalWindowMax));
		State.m_VisibleLoadingWindow = std::clamp(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowMin, maximum(Profile.m_VisibleWindowMin, Profile.m_VisibleWindowMax));
		State.m_VisibleReserve = Profile.m_VisibleReserveMax;
		State.m_UnderfedStreak = 0;
	}

	const bool FrameHealthy = Input.m_FrameTimeAverageMs <= 10.0f && Input.m_RenderFrameTimeMs <= 14.0f;
	const bool FramePressure = Input.m_FrameTimeAverageMs > 12.5f || Input.m_RenderFrameTimeMs > 16.7f;
	const bool GpuBound = str_comp(Input.m_pLastWaitReason, "gpu_upload_budget") == 0;
	const bool FinalizeBound = str_comp(Input.m_pLastWaitReason, "max_per_frame") == 0;
	const bool AdmissionUnderfed =
		Input.m_VisibleWaiting > 0 &&
		Input.m_UploadsDoneDelta == 0 &&
		Input.m_LoadedDelta == 0 &&
		Input.m_GpuUploadRemainingUnits > 0 &&
		!GpuBound &&
		!FinalizeBound &&
		!Input.m_DecodeJobsSaturated &&
		(str_comp(Input.m_pLastWaitReason, "visible_reserve") == 0 ||
			str_comp(Input.m_pLastWaitReason, "loading_window") == 0 ||
			str_comp(Input.m_pLastWaitReason, "none") == 0 ||
			str_comp(Input.m_pRequestBudgetBlockReason, "drain_inactive") == 0);
	const bool HealthyProgress = FrameHealthy && (Input.m_UploadsDoneDelta > 0 || Input.m_LoadedDelta > 0);

	if(AdmissionUnderfed)
		State.m_UnderfedStreak++;
	else
		State.m_UnderfedStreak = 0;

	Output.m_Reason = ESettingsSkinThroughputControllerReason::NONE;
	if(FramePressure)
	{
		State.m_GpuUploadLimitUnits = StepDownClamped(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitStep, Profile.m_GpuUploadLimitMin);
		State.m_FinalizeBudgetLimit = StepDownClamped(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetStep, Profile.m_FinalizeBudgetMin);
		State.m_NormalLoadingWindow = StepDownClamped(State.m_NormalLoadingWindow, Profile.m_NormalWindowStep, Profile.m_NormalWindowMin);
		State.m_VisibleLoadingWindow = StepDownClamped(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowStep, Profile.m_VisibleWindowMin);
		State.m_VisibleReserve = Profile.m_VisibleReserveMax;
		Output.m_Reason = ESettingsSkinThroughputControllerReason::FRAME_PRESSURE;
	}
	else if(GpuBound)
	{
		State.m_GpuUploadLimitUnits = StepDownClamped(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitStep, Profile.m_GpuUploadLimitMin);
		Output.m_Reason = ESettingsSkinThroughputControllerReason::GPU;
	}
	else if(FinalizeBound)
	{
		State.m_FinalizeBudgetLimit = StepDownClamped(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetStep, Profile.m_FinalizeBudgetMin);
		Output.m_Reason = ESettingsSkinThroughputControllerReason::FINALIZE;
	}
	else if(Input.m_DecodeJobsSaturated)
	{
		State.m_NormalLoadingWindow = StepUpClamped(State.m_NormalLoadingWindow, Profile.m_NormalWindowStep, Profile.m_NormalWindowMax);
		State.m_VisibleLoadingWindow = StepUpClamped(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowStep, Profile.m_VisibleWindowMax);
		State.m_VisibleReserve = Profile.m_VisibleReserveMin;
		Output.m_Reason = ESettingsSkinThroughputControllerReason::DECODE;
	}
	else if(AdmissionUnderfed)
	{
		State.m_NormalLoadingWindow = StepUpClamped(State.m_NormalLoadingWindow, Profile.m_NormalWindowStep, Profile.m_NormalWindowMax);
		State.m_VisibleLoadingWindow = StepUpClamped(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowStep, Profile.m_VisibleWindowMax);
		State.m_VisibleReserve = Profile.m_VisibleReserveMin;
		if(FrameHealthy)
		{
			State.m_GpuUploadLimitUnits = StepUpClamped(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitStep, Profile.m_GpuUploadLimitMax);
			State.m_FinalizeBudgetLimit = StepUpClamped(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetStep, Profile.m_FinalizeBudgetMax);
		}
		Output.m_Reason = ESettingsSkinThroughputControllerReason::ADMISSION;
	}
	else if(HealthyProgress)
	{
		State.m_GpuUploadLimitUnits = StepUpClamped(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitStep, Profile.m_GpuUploadLimitMax);
		State.m_FinalizeBudgetLimit = StepUpClamped(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetStep, Profile.m_FinalizeBudgetMax);
		State.m_NormalLoadingWindow = StepTowardLimit(State.m_NormalLoadingWindow, Profile.m_NormalWindowMax, Profile.m_NormalWindowStep);
		State.m_VisibleLoadingWindow = StepTowardLimit(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowMax, Profile.m_VisibleWindowStep);
		State.m_VisibleReserve = Profile.m_VisibleReserveMax;
		Output.m_Reason = ESettingsSkinThroughputControllerReason::PROGRESS;
	}
	else
	{
		State.m_GpuUploadLimitUnits = std::clamp(State.m_GpuUploadLimitUnits, Profile.m_GpuUploadLimitMin, Profile.m_GpuUploadLimitMax);
		State.m_FinalizeBudgetLimit = std::clamp(State.m_FinalizeBudgetLimit, Profile.m_FinalizeBudgetMin, Profile.m_FinalizeBudgetMax);
		State.m_NormalLoadingWindow = std::clamp(State.m_NormalLoadingWindow, Profile.m_NormalWindowMin, maximum(Profile.m_NormalWindowMin, Profile.m_NormalWindowMax));
		State.m_VisibleLoadingWindow = std::clamp(State.m_VisibleLoadingWindow, Profile.m_VisibleWindowMin, maximum(Profile.m_VisibleWindowMin, Profile.m_VisibleWindowMax));
		State.m_VisibleReserve = std::clamp(State.m_VisibleReserve, Profile.m_VisibleReserveMin, Profile.m_VisibleReserveMax);
	}

	State.m_GpuUploadFrameBudget = maximum(1, State.m_GpuUploadLimitUnits / 24);
	Output.m_GpuUploadLimitUnits = State.m_GpuUploadLimitUnits;
	Output.m_GpuUploadFrameBudget = State.m_GpuUploadFrameBudget;
	Output.m_FinalizeBudgetLimit = State.m_FinalizeBudgetLimit;
	Output.m_NormalLoadingWindow = State.m_NormalLoadingWindow;
	Output.m_VisibleLoadingWindow = State.m_VisibleLoadingWindow;
	Output.m_VisibleReserve = State.m_VisibleReserve;
	Output.m_BackgroundRequestBudget = Profile.m_BackgroundRequestBudget;
	Output.m_BackgroundDrainActive = Mode == ESettingsSkinThroughputControllerMode::IDLE_DRAIN;
	Output.m_CountFuseLimit = Output.m_BackgroundDrainActive ? maximum(maximum(0, Input.m_LoadedMax), 128) : maximum(0, Input.m_LoadedMax);
	Output.m_AdmissionUnderfed = AdmissionUnderfed;
	Output.m_UnderfedStreak = State.m_UnderfedStreak;
	Output.m_Mode = Mode;
	return Output;
}

SSettingsAdaptiveBudgetOutput SettingsAdaptiveBudgetStep(const SSettingsAdaptiveBudgetInput &Input, SSettingsAdaptiveBudgetState &State)
{
	SSettingsAdaptiveBudgetOutput Output;
	if(!State.m_Initialized)
	{
		State.m_Initialized = true;
		State.m_HealthyFrames = 0;
		State.m_BackgroundWindow = 1;
		State.m_PrefetchWindow = 1;
		State.m_VisibleWindow = 2;
		State.m_GpuUploadWindow = 1;
		State.m_TextPrebuildWindow = 1;
		State.m_DemoMetadataWindow = 1;
	}

	const float TargetMs = std::max(1.0f, Input.m_TargetFrameMs);
	const bool FramePressure =
		Input.m_FramePressure ||
		Input.m_FrameMsAverage > TargetMs * 1.35f ||
		Input.m_FrameMsP95 > TargetMs * 1.60f ||
		Input.m_TextContainerCreateMsEwma > TargetMs * 0.25f ||
		Input.m_TextContainerUploadMsEwma > TargetMs * 0.20f ||
		Input.m_GlyphRasterizeMsEwma > TargetMs * 0.20f ||
		Input.m_GlyphUploadMsEwma > TargetMs * 0.20f ||
		Input.m_GpuBudgetExhausted ||
		Input.m_FinalizeBudgetExhausted ||
		Input.m_UploadBudgetExhausted;
	const bool ScrollActive = Input.m_ScrollActive || Input.m_JumpScrollActive;
	const bool HasBacklog = Input.m_BackgroundBacklog > 0 || Input.m_VisibleWaiting > 0;
	const bool StableFrame = Input.m_FrameMsAverage > 0.0f &&
				 Input.m_FrameMsAverage <= TargetMs * 0.75f &&
				 Input.m_FrameMsP95 <= TargetMs * 0.95f &&
				 !FramePressure;

	if(!Input.m_WindowActive)
	{
		State.m_HealthyFrames = 0;
		State.m_BackgroundWindow = 0;
		State.m_PrefetchWindow = 0;
		State.m_VisibleWindow = 1;
		State.m_GpuUploadWindow = 1;
		State.m_TextPrebuildWindow = 0;
		State.m_DemoMetadataWindow = 0;
		Output.m_Mode = ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE;
		Output.m_Reason = ESettingsAdaptiveBudgetReason::WINDOW_INACTIVE;
	}
	else if(FramePressure)
	{
		State.m_HealthyFrames = 0;
		State.m_BackgroundWindow = 0;
		State.m_PrefetchWindow = std::max(0, State.m_PrefetchWindow / 2);
		State.m_VisibleWindow = std::max(1, State.m_VisibleWindow / 2);
		State.m_GpuUploadWindow = std::max(1, State.m_GpuUploadWindow / 2);
		State.m_TextPrebuildWindow = std::max(1, State.m_TextPrebuildWindow / 2);
		State.m_DemoMetadataWindow = std::max(1, State.m_DemoMetadataWindow / 2);
		Output.m_Mode = ESettingsAdaptiveBudgetMode::FRAME_PRESSURE;
		Output.m_Reason = ESettingsAdaptiveBudgetReason::FRAME_PRESSURE;
	}
	else if(ScrollActive)
	{
		State.m_HealthyFrames = 0;
		State.m_BackgroundWindow = 0;
		State.m_PrefetchWindow = 0;
		State.m_VisibleWindow = Input.m_JumpScrollActive ? 2 : std::max(2, State.m_VisibleWindow);
		State.m_GpuUploadWindow = std::max(1, State.m_GpuUploadWindow);
		State.m_TextPrebuildWindow = 1;
		State.m_DemoMetadataWindow = 1;
		Output.m_Mode = ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE;
		Output.m_Reason = ESettingsAdaptiveBudgetReason::SCROLL;
	}
	else if(Input.m_PostScrollRecoveryFrames > 0)
	{
		State.m_HealthyFrames = 0;
		State.m_BackgroundWindow = std::min(State.m_BackgroundWindow, 1);
		State.m_PrefetchWindow = std::max(1, State.m_PrefetchWindow);
		State.m_VisibleWindow = std::max(2, State.m_VisibleWindow);
		State.m_GpuUploadWindow = std::max(1, State.m_GpuUploadWindow);
		State.m_TextPrebuildWindow = std::max(1, State.m_TextPrebuildWindow);
		State.m_DemoMetadataWindow = std::max(1, State.m_DemoMetadataWindow);
		Output.m_Mode = ESettingsAdaptiveBudgetMode::POST_SCROLL_RECOVERY;
		Output.m_Reason = ESettingsAdaptiveBudgetReason::SCROLL;
	}
	else if(StableFrame && HasBacklog)
	{
		++State.m_HealthyFrames;
		if(State.m_HealthyFrames >= 2)
		{
			State.m_BackgroundWindow = std::min(16, State.m_BackgroundWindow + 1);
			State.m_PrefetchWindow = std::min(8, State.m_PrefetchWindow + 1);
			State.m_VisibleWindow = std::min(16, State.m_VisibleWindow + 1);
			State.m_GpuUploadWindow = std::min(8, State.m_GpuUploadWindow + 1);
			State.m_TextPrebuildWindow = std::min(maximum(1, Input.m_TextIdleHardCap), State.m_TextPrebuildWindow + 1);
			State.m_DemoMetadataWindow = std::min(12, State.m_DemoMetadataWindow + 1);
			State.m_HealthyFrames = 0;
			Output.m_Reason = ESettingsAdaptiveBudgetReason::PROGRESS;
		}
		Output.m_Mode = ESettingsAdaptiveBudgetMode::IDLE;
	}
	else
	{
		State.m_HealthyFrames = 0;
		Output.m_Mode = ESettingsAdaptiveBudgetMode::IDLE;
		Output.m_Reason = HasBacklog ? ESettingsAdaptiveBudgetReason::NONE : ESettingsAdaptiveBudgetReason::BACKLOG_EMPTY;
	}

	Output.m_VisibleTokens = std::max(1, State.m_VisibleWindow);
	Output.m_PrefetchTokens = std::max(0, State.m_PrefetchWindow);
	Output.m_BackgroundTokens = std::max(0, State.m_BackgroundWindow);
	Output.m_GpuUploadTokens = std::max(1, State.m_GpuUploadWindow);
	Output.m_ResourceUploadTokens = Output.m_GpuUploadTokens;
	const int TextHardCap =
		Output.m_Mode == ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE  ? Input.m_TextScrollHardCap :
		Output.m_Mode == ESettingsAdaptiveBudgetMode::FRAME_PRESSURE ? Input.m_TextPressureHardCap :
									       Input.m_TextIdleHardCap;
	const int GlyphHardCap =
		Output.m_Mode == ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE  ? Input.m_GlyphScrollHardCap :
		Output.m_Mode == ESettingsAdaptiveBudgetMode::FRAME_PRESSURE ? Input.m_GlyphPressureHardCap :
									       Input.m_GlyphIdleHardCap;
	const int TextMax = maximum(0, TextHardCap);
	const int GlyphMax = maximum(0, GlyphHardCap);
	const int TextMin = Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || TextMax == 0 ? 0 : 1;
	const int GlyphMin = Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || GlyphMax == 0 ? 0 : 1;
	Output.m_TextPrebuildTokens = std::clamp(State.m_TextPrebuildWindow, TextMin, TextMax);
	Output.m_TextContainerTokens = Output.m_TextPrebuildTokens;
	Output.m_GlyphRasterizeTokens = std::clamp(minimum(Output.m_TextPrebuildTokens, 2), GlyphMin, GlyphMax);
	Output.m_GlyphUploadTokens = std::clamp(minimum(Output.m_TextPrebuildTokens, 2), GlyphMin, GlyphMax);
	Output.m_ParagraphLayoutTokens = (Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || Output.m_Mode == ESettingsAdaptiveBudgetMode::FRAME_PRESSURE) ? 0 : std::max(1, minimum(Output.m_TextPrebuildTokens, 2));
	Output.m_MetadataLayoutTokens = (Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || Input.m_TabSwitchFirstFrame || Input.m_JumpScrollActive) ? 0 : std::max(1, minimum(Output.m_TextPrebuildTokens, 2));
	Output.m_PreviewArtifactTokens = (Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || Input.m_TabSwitchFirstFrame || Input.m_JumpScrollActive) ? 0 : (Output.m_Mode == ESettingsAdaptiveBudgetMode::FRAME_PRESSURE ? minimum(Output.m_VisibleTokens, 1) : std::max(1, minimum(Output.m_VisibleTokens, 2)));
	Output.m_TextureUploadTokens = (Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE || Input.m_TabSwitchFirstFrame || Input.m_JumpScrollActive) ? 0 : Output.m_ResourceUploadTokens;
	Output.m_CardDrawTokens = Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE ? 0 : std::max(1, Output.m_VisibleTokens);
	Output.m_DemoMetadataTokens = std::max(Output.m_Mode == ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE ? 0 : 1, State.m_DemoMetadataWindow);
	return Output;
}

SSettingsSkinBackgroundWindowOutput SettingsSkinBackgroundWindowUpdate(const SSettingsSkinBackgroundWindowInput &Input)
{
	SSettingsSkinBackgroundWindowOutput Output;
	Output.m_NextLimit = std::clamp(Input.m_CurrentLimit, Input.m_MinLimit, maximum(Input.m_MinLimit, Input.m_MaxLimit));
	Output.m_NextHealthyFrames = maximum(0, Input.m_HealthyFrames);
	if(!Input.m_DrainActive)
		return Output;

	const bool ShouldDecrease = Input.m_VisibleWaiting ||
				    Input.m_GpuBudgetExhausted ||
				    Input.m_FinalizeBudgetExhausted ||
				    Input.m_DecodeJobsSaturated ||
				    Input.m_ConsumerStalled ||
				    !Input.m_FrameStable;
	if(ShouldDecrease)
	{
		Output.m_NextLimit = maximum(Input.m_MinLimit, maximum(Input.m_CurrentLimit, Input.m_MinLimit) / 2);
		Output.m_NextHealthyFrames = 0;
		Output.m_Decision = ESettingsSkinBackgroundWindowDecision::DECREASE;
		return Output;
	}

	const bool Healthy = Input.m_LoadedProgress &&
			     !Input.m_VisibleWaiting &&
			     !Input.m_GpuBudgetExhausted &&
			     !Input.m_FinalizeBudgetExhausted &&
			     !Input.m_DecodeJobsSaturated &&
			     Input.m_FrameStable;
	if(!Healthy)
	{
		Output.m_NextHealthyFrames = 0;
		return Output;
	}

	Output.m_NextHealthyFrames++;
	if(Output.m_NextHealthyFrames >= maximum(1, Input.m_HealthyFramesToGrow) &&
		Output.m_NextLimit < Input.m_MaxLimit)
	{
		Output.m_NextLimit = minimum(Input.m_MaxLimit, Output.m_NextLimit + 1);
		Output.m_NextHealthyFrames = 0;
		Output.m_Decision = ESettingsSkinBackgroundWindowDecision::INCREASE;
	}
	return Output;
}

const char *SettingsSkinBackgroundRequestBlockReasonName(ESettingsSkinBackgroundRequestBlockReason Reason)
{
	switch(Reason)
	{
	case ESettingsSkinBackgroundRequestBlockReason::NONE: return "none";
	case ESettingsSkinBackgroundRequestBlockReason::DRAIN_INACTIVE: return "drain_inactive";
	case ESettingsSkinBackgroundRequestBlockReason::VISIBLE_RESERVE: return "visible_reserve";
	case ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE: return "stall_backpressure";
	}
	return "unknown";
}

const char *SettingsSkinSourceAdmissionBlockReasonName(ESettingsSkinSourceAdmissionBlockReason Reason)
{
	switch(Reason)
	{
	case ESettingsSkinSourceAdmissionBlockReason::NONE: return "none";
	case ESettingsSkinSourceAdmissionBlockReason::DRAIN_INACTIVE: return "drain_inactive";
	case ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE: return "visible_reserve";
	case ESettingsSkinSourceAdmissionBlockReason::LOADING_WINDOW: return "loading_window";
	case ESettingsSkinSourceAdmissionBlockReason::QUEUE_FUSE: return "queue_fuse";
	}
	return "unknown";
}

const char *SettingsSkinBackgroundWindowDecisionName(ESettingsSkinBackgroundWindowDecision Decision)
{
	switch(Decision)
	{
	case ESettingsSkinBackgroundWindowDecision::HOLD: return "hold";
	case ESettingsSkinBackgroundWindowDecision::INCREASE: return "increase_loading_window";
	case ESettingsSkinBackgroundWindowDecision::DECREASE: return "decrease_loading_window";
	}
	return "unknown";
}

const char *SettingsSkinThroughputControllerModeName(ESettingsSkinThroughputControllerMode Mode)
{
	switch(Mode)
	{
	case ESettingsSkinThroughputControllerMode::SCROLL_ACTIVE: return "scroll_active";
	case ESettingsSkinThroughputControllerMode::POST_SCROLL_RECOVERY: return "post_scroll_recovery";
	case ESettingsSkinThroughputControllerMode::IDLE_VISIBLE: return "idle_visible";
	case ESettingsSkinThroughputControllerMode::IDLE_DRAIN: return "idle_drain";
	}
	return "idle_visible";
}

const char *SettingsSkinThroughputControllerReasonName(ESettingsSkinThroughputControllerReason Reason)
{
	switch(Reason)
	{
	case ESettingsSkinThroughputControllerReason::NONE: return "none";
	case ESettingsSkinThroughputControllerReason::GPU: return "gpu";
	case ESettingsSkinThroughputControllerReason::FINALIZE: return "finalize";
	case ESettingsSkinThroughputControllerReason::DECODE: return "decode";
	case ESettingsSkinThroughputControllerReason::ADMISSION: return "admission";
	case ESettingsSkinThroughputControllerReason::FRAME_PRESSURE: return "frame_pressure";
	case ESettingsSkinThroughputControllerReason::PROGRESS: return "progress";
	}
	return "none";
}

const char *SettingsAdaptiveBudgetModeName(ESettingsAdaptiveBudgetMode Mode)
{
	switch(Mode)
	{
	case ESettingsAdaptiveBudgetMode::IDLE: return "idle";
	case ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE: return "scroll_active";
	case ESettingsAdaptiveBudgetMode::POST_SCROLL_RECOVERY: return "post_scroll_recovery";
	case ESettingsAdaptiveBudgetMode::FRAME_PRESSURE: return "frame_pressure";
	case ESettingsAdaptiveBudgetMode::WINDOW_INACTIVE: return "window_inactive";
	}
	return "idle";
}

const char *SettingsAdaptiveBudgetReasonName(ESettingsAdaptiveBudgetReason Reason)
{
	switch(Reason)
	{
	case ESettingsAdaptiveBudgetReason::NONE: return "none";
	case ESettingsAdaptiveBudgetReason::PROGRESS: return "progress";
	case ESettingsAdaptiveBudgetReason::FRAME_PRESSURE: return "frame_pressure";
	case ESettingsAdaptiveBudgetReason::SCROLL: return "scroll";
	case ESettingsAdaptiveBudgetReason::WINDOW_INACTIVE: return "window_inactive";
	case ESettingsAdaptiveBudgetReason::BACKLOG_EMPTY: return "backlog_empty";
	}
	return "none";
}

const char *SettingsSkinEffectiveFrameContextName(const SSettingsResourceFrameContext &Context, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return "non_tee";
	if(Context.m_ScrollActive)
		return "scroll";
	if(Context.m_PostScrollRecoveryFrames > 0)
		return "recovery";
	if(SettingsSkinBackgroundDrainActive(Context, TeeSettingsActive))
		return "idle_drain";
	return "idle_visible";
}

size_t SettingsSkinSourceBytesEstimate(int Width, int Height, int PixelCopies)
{
	if(Width <= 0 || Height <= 0 || PixelCopies <= 0)
		return 0;
	return (size_t)Width * (size_t)Height * 4u * (size_t)PixelCopies;
}

bool SettingsSkinResidencyShouldReclaim(bool BytesBudgetExceeded, bool CountFuseExceeded)
{
	return BytesBudgetExceeded || CountFuseExceeded;
}

const char *SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource Source)
{
	switch(Source)
	{
	case ESettingsWorkshopCatalogSource::LOCAL_ONLY: return "local-only";
	case ESettingsWorkshopCatalogSource::WORKSHOP_CACHE: return "workshop-cache";
	case ESettingsWorkshopCatalogSource::WORKSHOP_HTTP: return "workshop-http";
	}
	return "unknown";
}

const char *SettingsWorkshopBytesSourceName(ESettingsWorkshopBytesSource Source)
{
	switch(Source)
	{
	case ESettingsWorkshopBytesSource::LOCAL_INSTALL: return "local-install";
	case ESettingsWorkshopBytesSource::LOCAL_THUMB_CACHE: return "local-thumb-cache";
	case ESettingsWorkshopBytesSource::REMOTE_THUMB_HTTP: return "remote-thumb-http";
	}
	return "unknown";
}

void SettingsApplyActiveTeeSkinFrameBudget(SSettingsWarmupFrameBudget &Budget, bool TeeSettingsActive)
{
	if(!TeeSettingsActive)
		return;

	Budget.m_MaxGpuUploads = SettingsSkinGpuUploadUnits(true);
	Budget.m_MaxJobResultMerges = 2;
}

bool SettingsSkinFinalizeShouldDeferBackgroundSweep(bool ProcessedHighPrioritySkin, int ProcessedThisFrame, int MaxPerFrame)
{
	return ProcessedHighPrioritySkin && ProcessedThisFrame > 0 && ProcessedThisFrame < MaxPerFrame;
}

bool SettingsAssetListShouldShowBlockingLoading(bool Loading, int VisibleEntries)
{
	return Loading && VisibleEntries <= 0;
}

bool SettingsAssetListCanStartPreviewDecode(bool Loading, bool Merging, bool Loaded)
{
	return !Loading && !Merging && Loaded;
}

bool SettingsAssetPreviewShouldDeferFinalize(int FinalizedThisFrame, double ElapsedMs, int MaxFinalizesPerFrame, double MaxFinalizeMsPerFrame)
{
	return FinalizedThisFrame >= MaxFinalizesPerFrame ||
	       (FinalizedThisFrame > 0 && ElapsedMs >= MaxFinalizeMsPerFrame);
}

bool SettingsAssetWorkAllowedWhileWindowInactive(bool WindowActive, bool HighPriority)
{
	(void)HighPriority;
	return WindowActive;
}

bool SettingsAssetPreviewResidentTextureSatisfiesRequest(bool TextureValid, size_t ResidentBytes, int RequestedTextureSize)
{
	if(!TextureValid)
		return false;
	if(RequestedTextureSize <= 0)
		return true;
	return ResidentBytes >= PreviewTextureSizeBytesEstimate(RequestedTextureSize);
}

bool SettingsAssetPreviewDecodeStartNeeded(bool DecodeJobActive, bool TextureValid, size_t ResidentBytes, int RequestedTextureSize, bool HasReadyImage)
{
	if(DecodeJobActive || HasReadyImage)
		return false;
	return !SettingsAssetPreviewResidentTextureSatisfiesRequest(TextureValid, ResidentBytes, RequestedTextureSize);
}

bool SettingsAssetPreviewShouldPrioritizeVisibleRange(int Index, int FirstVisibleIndex, int LastVisibleIndex)
{
	return FirstVisibleIndex >= 0 && LastVisibleIndex >= FirstVisibleIndex && Index >= FirstVisibleIndex && Index <= LastVisibleIndex;
}

bool SettingsAssetPreviewShouldUploadHighPriorityFirst(bool CurrentHighPriority, bool CandidateHighPriority)
{
	return !CurrentHighPriority && CandidateHighPriority;
}

bool SettingsWorkshopThumbShouldStartHighPriority(int VisibleDownloadableIndex, int FirstVisibleDownloadableIndex, int LastVisibleDownloadableIndex)
{
	return SettingsAssetPreviewShouldPrioritizeVisibleRange(VisibleDownloadableIndex, FirstVisibleDownloadableIndex, LastVisibleDownloadableIndex);
}

bool SettingsResourceCanUseHighPriorityBudget(int StartedThisFrame, int NormalBudget, int HighPriorityBudget, bool HighPriority)
{
	if(StartedThisFrame < NormalBudget)
		return true;
	return HighPriority && StartedThisFrame < HighPriorityBudget;
}

int SettingsResourceFrameStageBudget(const SSettingsResourceFrameContext &Context, ESettingsResourcePriority Priority, int NormalBudget, int ScrollActiveVisibleBudget)
{
	if(!Context.m_ScrollActive && !Context.m_JumpScrollActive)
		return std::max(0, NormalBudget);
	if(Priority != ESettingsResourcePriority::VISIBLE)
		return 0;
	return std::max(0, ScrollActiveVisibleBudget);
}

int SettingsScrollInteractionCooldown(bool ActiveThisFrame, int CurrentCooldownFrames, int CooldownFrames)
{
	if(ActiveThisFrame)
		return std::max(0, CooldownFrames);
	if(CurrentCooldownFrames <= 0)
		return 0;
	return CurrentCooldownFrames - 1;
}

int SettingsScrollInteractionRecovery(bool ScrollActiveThisFrame, int PreviousCooldownFrames, int CurrentCooldownFrames, int CurrentRecoveryFrames, int RecoveryFrames)
{
	if(ScrollActiveThisFrame || CurrentCooldownFrames > 0)
		return 0;
	if(PreviousCooldownFrames > 0)
		return std::max(0, RecoveryFrames);
	if(CurrentRecoveryFrames <= 0)
		return 0;
	return CurrentRecoveryFrames - 1;
}

int SettingsResourceSharedHeavyBudget(const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget)
{
	if(Context.m_ScrollActive || Context.m_JumpScrollActive)
		return 0;
	if(Context.m_PostScrollRecoveryFrames > 0)
	{
		const int ClampedRecoveryBudget = std::max(0, RecoveryBudget);
		if(ClampedRecoveryBudget <= 0)
			return 0;
		return std::min(ClampedRecoveryBudget, std::max(1, SettingsRecoveryBudget(Context, 0, ClampedRecoveryBudget)));
	}
	return std::max(0, NormalBudget);
}

int SettingsResourceClampSharedHeavyBudget(int RemainingBudget, const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget)
{
	return std::min(std::max(0, RemainingBudget), SettingsResourceSharedHeavyBudget(Context, NormalBudget, RecoveryBudget));
}

int SettingsResourceClampSharedHeavyBudgetWithScrollTrickle(int RemainingBudget, const SSettingsResourceFrameContext &Context, int NormalBudget, int RecoveryBudget, int ScrollTrickleBatches)
{
	const int ClampedBudget = SettingsResourceClampSharedHeavyBudget(RemainingBudget, Context, NormalBudget, RecoveryBudget);
	if(!Context.m_ScrollActive && !Context.m_JumpScrollActive)
		return ClampedBudget;
	// 滚动期共享 heavy 池原设计整池归零，但 finalize/upload 的涓流预算最终都要
	// 经这池放行，池子为 0 时涓流名存实亡（商店缩略图滚动期不补图的直接原因）。
	// 这里给每条排空链路一个独立的小额保底；各链路合计仍是每帧个位数的小流量。
	return std::max(ClampedBudget, std::max(0, ScrollTrickleBatches));
}

bool SettingsResourceConsumeSharedHeavyBudget(int &RemainingBudget)
{
	if(RemainingBudget <= 0)
		return false;
	--RemainingBudget;
	return true;
}

bool SettingsResourceUploadWithinByteBudget(int UploadedThisFrame, size_t UploadedBytesThisFrame, size_t ItemBytes, size_t MaxBytesPerFrame)
{
	if(MaxBytesPerFrame == 0)
		return false;
	if(ItemBytes > MaxBytesPerFrame)
		return false;
	return UploadedBytesThisFrame + ItemBytes <= MaxBytesPerFrame;
}

bool SettingsResourceOversizedUploadAllowed(const SSettingsResourceFrameContext &Context, bool PageSwitchActive, ESettingsResourcePriority Priority, int OversizedUploadsThisFrame, size_t ItemBytes, size_t MaxBytesPerFrame)
{
	if(ItemBytes <= MaxBytesPerFrame)
		return false;
	(void)Context;
	(void)PageSwitchActive;
	(void)Priority;
	(void)OversizedUploadsThisFrame;
	(void)MaxBytesPerFrame;
	return false;
}

size_t SettingsAssetPreviewResidentBudgetBytes(size_t OverrideMb, int Percent, float GpuBudgetMb)
{
	return PreviewBudgetBytes(OverrideMb, Percent, GpuBudgetMb);
}

int SettingsAssetPreviewBudgetedTextureSize(int MaxTextureSize, int MinTextureSize, size_t TextureBudgetBytes, size_t CurrentTextureMemoryBytes, size_t ResidentPreviewBytes)
{
	const int BudgetedTextureSize = ComputePreviewBudgetedTextureSize(MaxTextureSize, MinTextureSize, TextureBudgetBytes, CurrentTextureMemoryBytes, ResidentPreviewBytes);
	return PreviewUploadBudgetTextureSize(BudgetedTextureSize, MinTextureSize, ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME);
}

std::string SettingsAssetPreviewHandleKey(const SSettingsAssetPreviewHandle &Handle)
{
	return std::to_string(Handle.m_Tab) + ":" + std::to_string(Handle.m_Epoch) + ":" + std::to_string(Handle.m_Index) + ":" + Handle.m_Name;
}

bool SettingsAssetPreviewHandleMatches(const SSettingsAssetPreviewHandle &Handle, int CurrentTab, unsigned CurrentEpoch, size_t CurrentIndex, const char *pName)
{
	return pName != nullptr &&
	       Handle.m_Tab == CurrentTab &&
	       Handle.m_Epoch == CurrentEpoch &&
	       Handle.m_Index == CurrentIndex &&
	       Handle.m_Name == pName;
}

const char *SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason StopReason)
{
	switch(StopReason)
	{
	case ESettingsWarmupStopReason::NONE: return "none";
	case ESettingsWarmupStopReason::TEXT_BUDGET: return "text_budget";
	case ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET: return "gpu_upload_budget";
	case ESettingsWarmupStopReason::MERGE_BUDGET: return "merge_budget";
	case ESettingsWarmupStopReason::ACTIVE_ITEM: return "active_item";
	}
	return "unknown";
}

bool SettingsAssetWarmupAllTabsReady(const bool *pReadyTabs, int TabCount)
{
	if(pReadyTabs == nullptr || TabCount <= 0)
		return true;
	for(int Tab = 0; Tab < TabCount; ++Tab)
	{
		if(!pReadyTabs[Tab])
			return false;
	}
	return true;
}

int SettingsAssetWarmupNextTab(int CurrentTab, int TabCount)
{
	if(TabCount <= 0)
		return -1;
	if(CurrentTab < 0 || CurrentTab >= TabCount)
		return 0;
	return (CurrentTab + 1) % TabCount;
}
