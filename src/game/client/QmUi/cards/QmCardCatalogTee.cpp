/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmIconSwap.h>
#include <game/client/QmUi/QmListCard.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiProgressRing.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeSkinListState.h>
#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_label.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;

namespace
{
	void GetSettingsTeePreviewBounds(const CAnimState *pAnim, const CTeeRenderInfo &Info, float &MinX, float &MinY, float &MaxX, float &MaxY)
	{
		if(Info.m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY).IsValid())
		{
			MinX = -Info.m_Size * 0.5f;
			MaxX = Info.m_Size * 0.5f;
			MinY = -Info.m_Size * 0.5f;
			MaxY = Info.m_Size * 0.74f;
			return;
		}

		float AnimScale, BaseSize;
		CRenderTools::GetRenderTeeAnimScaleAndBaseSize(&Info, AnimScale, BaseSize);
		const float AssumedScale = BaseSize / 64.0f;
		const vec2 BodyPos = vec2(pAnim->GetBody()->m_X, pAnim->GetBody()->m_Y) * AnimScale;

		vec2 BodyOffset;
		float BodyWidth, BodyHeight;
		CRenderTools::GetRenderTeeBodySize(pAnim, &Info, BodyOffset, BodyWidth, BodyHeight);
		MinX = -32.0f * AssumedScale + BodyPos.x + BodyOffset.x;
		MinY = -32.0f * AssumedScale + BodyPos.y + BodyOffset.y;
		MaxX = MinX + BodyWidth;
		MaxY = MinY + BodyHeight;

		const CAnimKeyframe *apFeet[] = {pAnim->GetFrontFoot(), pAnim->GetBackFoot()};
		for(const CAnimKeyframe *pFoot : apFeet)
		{
			const vec2 FootPos = vec2(pFoot->m_X * AnimScale, pFoot->m_Y * AnimScale);
			vec2 FeetOffset;
			float FeetWidth, FeetHeight;
			CRenderTools::GetRenderTeeFeetSize(pAnim, &Info, FeetOffset, FeetWidth, FeetHeight);
			const float FeetMinX = -32.0f * AssumedScale + FootPos.x + FeetOffset.x;
			const float FeetMinY = -16.0f * AssumedScale + FootPos.y + FeetOffset.y;
			MinX = minimum(MinX, FeetMinX);
			MinY = minimum(MinY, FeetMinY);
			MaxX = maximum(MaxX, FeetMinX + FeetWidth);
			MaxY = maximum(MaxY, FeetMinY + FeetHeight);
		}
	}

	bool PerfDebugEnabled()
	{
		return QmPerfEnabled();
	}

	// 皮肤网格列表（皮肤格子，多列网格）的容器几何：行高、容器内边距与容器高度只在
	// 这里定义一次，卡片测高与交给 CListBox 的矩形都从它取——内边距只加在一处会把
	// 最后一行裁掉。可见行数固定 8 行；两档内边距只让卡片变高，不减少可见行数。
	constexpr int SETTINGS_TEE_SKIN_LIST_VISIBLE_ROWS = 8;
	static_assert(SETTINGS_TEE_SKIN_LIST_VISIBLE_ROWS == SETTINGS_SKIN_LIST_VISIBLE_ROWS);

	struct SSettingsTeeSkinListGeometry
	{
		float m_RowHeight = 0.0f;
		// 网格行距保持 0：行高本身已含格子间距，组件也不再给非胶囊网格叠加行距。
		float m_RowSpacing = 0.0f;
		float m_ContainerPadding = 0.0f;
		// 容器高度 = 可见行内容 + 上下两档内边距。
		float m_Height = 0.0f;
	};

	SSettingsTeeSkinListGeometry ResolveSettingsTeeSkinListGeometry(const SSettingsContentMetrics &Metrics)
	{
		SSettingsTeeSkinListGeometry Geometry;
		Geometry.m_RowHeight = 50.0f * Metrics.m_UiScale;
		Geometry.m_ContainerPadding = ui_token::list::ContainerPadding(Geometry.m_RowHeight);
		Geometry.m_Height = ui_token::list::PaddedRowsHeight(SETTINGS_TEE_SKIN_LIST_VISIBLE_ROWS, Geometry.m_RowHeight, Geometry.m_RowSpacing, Geometry.m_ContainerPadding);
		return Geometry;
	}

	struct SSettingsPreviewSkinTransitionState
	{
		char m_aSkinName[MAX_SKIN_LENGTH] = {};
		CTeeRenderInfo m_LastInfo;
		CTeeRenderInfo m_PreviousInfo;
		std::optional<std::chrono::nanoseconds> m_StartTime;

		void Update(const char *pSkinName, const CTeeRenderInfo &Info, std::chrono::nanoseconds Now)
		{
			const bool Changed = str_comp(m_aSkinName, pSkinName) != 0 || SkinPreviewColorsChanged(m_LastInfo, Info);
			if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0 || !Info.Valid())
			{
				m_PreviousInfo.Reset();
				m_StartTime.reset();
			}
			else if(Changed && m_LastInfo.Valid())
			{
				m_PreviousInfo = m_LastInfo;
				m_StartTime = Now;
			}

			str_copy(m_aSkinName, pSkinName);
			m_LastInfo = Info;
		}

		float Progress(std::chrono::nanoseconds Now) const
		{
			if(!m_StartTime.has_value())
			{
				return 1.0f;
			}

			const float ElapsedSeconds = std::chrono::duration<float>(Now - m_StartTime.value()).count();
			if(!g_Config.m_QmSkinChangeTransition)
			{
				return 1.0f;
			}
			return ResolveSkinChangeTransitionProgress(ElapsedSeconds, g_Config.m_QmSkinChangeTransitionMs);
		}

		const CTeeRenderInfo *PreviousInfo(std::chrono::nanoseconds Now) const
		{
			if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0 || !m_StartTime.has_value() || Progress(Now) >= 1.0f || !m_PreviousInfo.Valid())
			{
				return nullptr;
			}

			return &m_PreviousInfo;
		}
	};

	struct STeeListDrainPerfSession
	{
		bool m_Active = false;
		int64_t m_StartNs = 0;
		uint64_t m_UploadsBase = 0;
		uint64_t m_LoadsBase = 0;
		uint64_t m_LastUploads = 0;
		uint64_t m_LastLoads = 0;
		int m_LastVisibleReady = -1;
		int m_LastVisibleTotal = -1;
		int m_LastRequested = -1;
		int m_LastPending = -1;
		int m_LastLoading = -1;
		int m_LastLoaded = -1;
		int m_LastAdmittedDelta = 0;
		int m_LastStartedDelta = 0;
		bool m_LastBackgroundDrain = false;
		int m_MaxRequested = 0;
		int m_MaxPending = 0;
		int m_MaxLoading = 0;
		int m_MaxRealInflight = 0;
		int m_CountFuseLimit = 0;
		uint64_t m_TotalRequested = 0;
		uint64_t m_TotalAdmitted = 0;
		uint64_t m_TotalStarted = 0;
		int m_NumLoadingWindowWaits = 0;
		int m_NumGpuBudgetWaits = 0;
		int m_NumQueueFuseWaits = 0;
	};

	STeeListDrainPerfSession gs_TeeListDrainPerfSession;

	struct STeeSettingsPageState
	{
		std::array<int, NUM_DUMMIES> m_aSelectedQueuePreset = {-1, -1};
		bool m_SkinListScrollActiveLastFrame = false;
		int m_SkinListScrollCooldownFrames = 0;
		int m_SkinListPostScrollRecoveryFrames = 0;
		size_t m_BackgroundRequestCursor = 0;
		int m_LastLoggedVisibleCount = -1;
		int m_LastLoggedVisibleReadyCount = -1;
		int m_LastLoggedRecoveryFrames = -1;
		bool m_LastLoggedScrollActive = false;
		char m_aLastLoggedFirstVisibleSkin[MAX_SKIN_LENGTH] = "";
		bool m_TeePageActiveLastFrame = false;
		bool m_TeeClickActiveLastFrame = false;
		bool m_TeeScrollInteractionLastFrame = false;
		bool m_TeeFirstVisibleReadyLogged = false;
		bool m_TeeAllVisibleReadyLogged = false;
		bool m_TeeFullListReadyLogged = false;
		bool m_TeeRefreshInProgress = false;
		int64_t m_TeeEnterStartNs = 0;
		int64_t m_TeeRefreshStartNs = 0;
		int m_LastRequestBudgetActual = 0;
		ESettingsSkinBackgroundRequestBlockReason m_LastRequestBudgetBlockReason = ESettingsSkinBackgroundRequestBlockReason::NONE;
		bool m_BackgroundRequestScanComplete = false;
		uint64_t m_BackgroundRequestScanRevision = std::numeric_limits<uint64_t>::max();
		uint64_t m_FullListSettledRevision = std::numeric_limits<uint64_t>::max();
		int m_FullListSettledCount = 0;
		uint64_t m_SelectedIndexRevision = std::numeric_limits<uint64_t>::max();
		EQmTeeSkinCollection m_SelectedCollection = EQmTeeSkinCollection::ALL;
		SQmRecentTeeSkin m_SelectedAppearance;
		int m_SelectedIndex = -1;
		SSettingsPreviewSkinTransitionState m_ListPaletteTransition;
	};

	STeeSettingsPageState gs_TeeSettingsPageState;

	using ETeeSkinCollection = EQmTeeSkinCollection;

	struct STeeSkinCollectionState
	{
		ETeeSkinCollection m_Collection = ETeeSkinCollection::ALL;
		CQmTeeSkinCollectionCache<CSkins::CSkinListEntry> m_Cache;
		uint64_t Revision() const { return m_Cache.Revision(); }

		std::vector<CSkins::CSkinListEntry> &Resolve(CSkins &Skins, CSkins::CSkinList &Source)
		{
			// ALL 直接暴露计划本身的向量：条目会原位填充，缓存副本会停在旧快照。
			if(m_Collection == ETeeSkinCollection::ALL)
				return Source.Skins();
			const SQmRecentTeeSkin Main = QmCurrentTeeSkin(g_Config, false);
			const SQmRecentTeeSkin Dummy = QmCurrentTeeSkin(g_Config, true);
			const SQmTeeSkinCollectionRequest Request{Source.Revision(), Skins.RecentSkins().Revision(), m_Collection, g_Config.m_ClSkinFilterString, Main, Dummy};
			return m_Cache.Resolve(Request, Skins.RecentSkins().Entries(), Source.Skins(),
				[&](const SQmRecentTeeSkin &Entry) { return Skins.RecentSkinListEntry(Entry); });
		}
	};
	STeeSkinCollectionState gs_TeeSkinCollection;
	CSettingsTeeSkinListState gs_TeeSkinListState;
	constexpr size_t TEE_BACKGROUND_SCAN_LIMIT = 256;

	void BeginTeeListDrainPerfSession(const CSkins &Skins, int64_t NowNs)
	{
		gs_TeeListDrainPerfSession.m_Active = true;
		gs_TeeListDrainPerfSession.m_StartNs = NowNs;
		gs_TeeListDrainPerfSession.m_UploadsBase = Skins.SettingsSourceUploadsCompleted();
		gs_TeeListDrainPerfSession.m_LoadsBase = Skins.SettingsSourceLoadsCompleted();
		gs_TeeListDrainPerfSession.m_LastUploads = gs_TeeListDrainPerfSession.m_UploadsBase;
		gs_TeeListDrainPerfSession.m_LastLoads = gs_TeeListDrainPerfSession.m_LoadsBase;
		gs_TeeListDrainPerfSession.m_LastVisibleReady = -1;
		gs_TeeListDrainPerfSession.m_LastVisibleTotal = -1;
		gs_TeeListDrainPerfSession.m_LastRequested = -1;
		gs_TeeListDrainPerfSession.m_LastPending = -1;
		gs_TeeListDrainPerfSession.m_LastLoading = -1;
		gs_TeeListDrainPerfSession.m_LastLoaded = -1;
		gs_TeeListDrainPerfSession.m_LastAdmittedDelta = 0;
		gs_TeeListDrainPerfSession.m_LastStartedDelta = 0;
		gs_TeeListDrainPerfSession.m_LastBackgroundDrain = false;
		gs_TeeListDrainPerfSession.m_MaxRequested = 0;
		gs_TeeListDrainPerfSession.m_MaxPending = 0;
		gs_TeeListDrainPerfSession.m_MaxLoading = 0;
		gs_TeeListDrainPerfSession.m_MaxRealInflight = 0;
		gs_TeeListDrainPerfSession.m_CountFuseLimit = 0;
		gs_TeeListDrainPerfSession.m_TotalRequested = 0;
		gs_TeeListDrainPerfSession.m_TotalAdmitted = 0;
		gs_TeeListDrainPerfSession.m_TotalStarted = 0;
		gs_TeeListDrainPerfSession.m_NumLoadingWindowWaits = 0;
		gs_TeeListDrainPerfSession.m_NumGpuBudgetWaits = 0;
		gs_TeeListDrainPerfSession.m_NumQueueFuseWaits = 0;
	}

	void LogTeeListDrainSummary(IClient *pClient, const CSkins &Skins, const CSkins::CSkinLoadingStats &Stats, bool FullListReady, int64_t NowNs)
	{
		if(!gs_TeeListDrainPerfSession.m_Active)
			return;
		if(!g_Config.m_QmPerfEnabled)
		{
			if(FullListReady)
				gs_TeeListDrainPerfSession.m_Active = false;
			return;
		}

		const uint64_t UploadsDoneTotal = Skins.SettingsSourceUploadsCompleted() - gs_TeeListDrainPerfSession.m_UploadsBase;
		const uint64_t LoadedTotal = Skins.SettingsSourceLoadsCompleted() - gs_TeeListDrainPerfSession.m_LoadsBase;
		const double DurationMs = gs_TeeListDrainPerfSession.m_StartNs > 0 ? (NowNs - gs_TeeListDrainPerfSession.m_StartNs) / 1000000.0 : 0.0;
		const double DurationSec = DurationMs > 0.0 ? DurationMs / 1000.0 : 0.0;
		const double UploadsPerSec = DurationSec > 0.0 ? UploadsDoneTotal / DurationSec : 0.0;
		const double LoadedPerSec = DurationSec > 0.0 ? LoadedTotal / DurationSec : 0.0;
		const auto &Telemetry = Skins.SettingsSourceAdmissionTelemetry();
		char aPayload[1024];
		str_format(aPayload, sizeof(aPayload), "event=work_drain page=settings:tee kind=merge count=%llu bytes=%d dur_ms=%.3f stop=%s source=list_drain_summary scope=session uploads_done_total=%llu loaded_total=%llu uploads_per_sec=%.3f loaded_per_sec=%.3f requested=%d pending=%d loading=%d loaded=%d max_requested=%d max_pending=%d max_loading=%d max_real_inflight=%d count_fuse_limit=%d total_requested=%llu total_admitted=%llu total_started=%llu num_loading_window_waits=%d num_gpu_budget_waits=%d num_queue_fuse_waits=%d full_list_ready=%d final_real_inflight=%d last_wait_reason=%s last_dynamic_decision=%s last_request_budget_block_reason=%s",
			(unsigned long long)LoadedTotal,
			0,
			DurationMs,
			FullListReady ? "complete" : "pending",
			(unsigned long long)UploadsDoneTotal,
			(unsigned long long)LoadedTotal,
			UploadsPerSec,
			LoadedPerSec,
			(int)Stats.m_NumBackgroundRequested,
			(int)Stats.m_NumPending,
			(int)Stats.m_NumLoading,
			(int)Stats.m_NumLoaded,
			gs_TeeListDrainPerfSession.m_MaxRequested,
			gs_TeeListDrainPerfSession.m_MaxPending,
			gs_TeeListDrainPerfSession.m_MaxLoading,
			gs_TeeListDrainPerfSession.m_MaxRealInflight,
			gs_TeeListDrainPerfSession.m_CountFuseLimit,
			(unsigned long long)gs_TeeListDrainPerfSession.m_TotalRequested,
			(unsigned long long)gs_TeeListDrainPerfSession.m_TotalAdmitted,
			(unsigned long long)gs_TeeListDrainPerfSession.m_TotalStarted,
			gs_TeeListDrainPerfSession.m_NumLoadingWindowWaits,
			gs_TeeListDrainPerfSession.m_NumGpuBudgetWaits,
			gs_TeeListDrainPerfSession.m_NumQueueFuseWaits,
			FullListReady ? 1 : 0,
			Telemetry.m_RealInflight,
			Telemetry.m_aLastWaitReason,
			Telemetry.m_aDynamicDecision,
			SettingsSkinBackgroundRequestBlockReasonName(gs_TeeSettingsPageState.m_LastRequestBudgetBlockReason));
		QmPerfLogPayload("perf/settings-skin-source", aPayload, pClient, "settings:tee");
		gs_TeeListDrainPerfSession.m_Active = false;
	}

	void ResetTeeSettingsPageState()
	{
		gs_TeeSettingsPageState = {};
	}
}

void CMenus::ClearSettingsTeePreviewCache()
{
	gs_TeeSkinCollection.m_Cache.Invalidate();
}

void CMenus::FinalizeTeeListDrainPerfSession()
{
	CommitSettingsTeeSkinEdits();
	gs_TeeSkinCollection.m_Cache.Invalidate(true);
	gs_TeeSkinListState = {};
	LogTeeListDrainSummary(Client(), GameClient()->m_Skins, GameClient()->m_Skins.LoadingStats(), false, time_get_nanoseconds().count());
	m_SettingsHighPrioritySettled = false;
	ResetTeeSettingsPageState();
}

namespace
{
	struct STeeOptionsContentState
	{
		std::array<CSettingsContentLayoutCache, 2> m_aCaches;
		std::array<std::shared_ptr<const CSettingsContentLayout>, 2> m_apLayouts;
	};
	thread_local STeeOptionsContentState *s_pTeeOptionsContent = nullptr;
}

void CMenus::RenderSettingsTeeOptions(CUIRect Content, const SSettingsContentMetrics &TeeMetrics)
{
	const float BodySize = TeeMetrics.m_BodySize;
	CSettingsContentLayoutFlow Flow(Content);
	bool ShouldRefresh = false;
	std::array<SSettingsCardDefinition, 2> aColumns;
	aColumns[0].m_RenderMeasured = [&](CUIRect &Downloads) {
		CSettingsContentLayoutFlow Column(Downloads);
		const auto Checkbox = [&](const char *pId, const char *pLabel, int *pValue, float Spacing) {
			CUIRect Row;
			if(!Column.Control(pId, TeeMetrics.m_LineHeight, Row, Spacing))
				return false;
			const bool Changed = DoSettingsButton_CheckBox(SETTINGS_TEE, -1, pValue, pId, pLabel, *pValue, &Row);
			if(!Changed || !Column.Input() || Ui()->RenderOnly())
				return false;
			*pValue ^= 1;
			return true;
		};
		ShouldRefresh = Checkbox("tee-download-skins", Localize("Download skins"), &g_Config.m_ClDownloadSkins, 0.0f);
		ShouldRefresh = Checkbox("tee-download-community-skins", Localize("Download community skins"), &g_Config.m_ClDownloadCommunitySkins, TeeMetrics.m_LineSpacing) || ShouldRefresh;
		CUIRect DdStatsRow;
		if(Column.Control("tee-download-ddstats-skins", TeeMetrics.m_LineHeight, DdStatsRow, TeeMetrics.m_LineSpacing))
		{
			SLabelProperties DdStatsLabelProps;
			DdStatsLabelProps.m_DisallowNewline = true;
			DdStatsLabelProps.m_StopAtEnd = true;
			DdStatsLabelProps.m_MinimumFontSize = 6.0f;
			const bool Changed = DoSettingsButton_CheckBox(SETTINGS_TEE, -1, -1, &g_Config.m_QmDownloadDdStatsSkins, "tee-download-ddstats-skins",
				Localize("Download DDStats skins (including unapproved skins)"), g_Config.m_QmDownloadDdStatsSkins, &DdStatsRow, DdStatsLabelProps, true, TeeMetrics.m_SmallSize);
			if(Changed && Column.Input() && !Ui()->RenderOnly())
			{
				g_Config.m_QmDownloadDdStatsSkins ^= 1;
				ShouldRefresh = true;
			}
		}
		Checkbox("tee-fat-skins", Localize("Fat skins (DDFat)"), &g_Config.m_ClFatSkins, TeeMetrics.m_LineSpacing);
		Checkbox("tee-show-skin-metadata", Localize("Show skin date and author"), &g_Config.m_QmSkinShowMetadata, TeeMetrics.m_LineSpacing);
	};
	aColumns[1].m_RenderMeasured = [&](CUIRect &Prefix) {
		CSettingsContentLayoutFlow Column(Prefix);
		CUIRect Row;
		// 标签、输入框与预设胶囊同排，比原先三行布局更紧凑。
		if(Column.Control("tee-skin-prefix-row", TeeMetrics.m_InputHeight, Row))
		{
			CUIRect LabelColumn, Controls;
			SplitSettingsControlRow(Row, TeeMetrics, &LabelColumn, &Controls);
			DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_skin_prefix_label", &LabelColumn, Localize("Skin prefix"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);

			// 预设胶囊至少占两段可点宽度，输入框留够最小宽度后吃掉剩余；列太窄时控件自身退化成下拉。
			const float SegmentMinWidth = std::max(72.0f, TeeMetrics.m_InputHeight * 3.0f);
			CUIRect InputRect, PresetRect;
			Controls.VSplitRight(std::min(2.0f * SegmentMinWidth, std::max(0.0f, Controls.w - 88.0f * TeeMetrics.m_UiScale)), &InputRect, &PresetRect);
			InputRect.VSplitRight(TeeMetrics.m_LineSpacing, &InputRect, nullptr);
			static CLineInput s_SkinPrefixInput(g_Config.m_ClSkinPrefix, sizeof(g_Config.m_ClSkinPrefix));
			ui_widget::SInputFieldOptions InputOptions;
			InputOptions.m_Clearable = true;
			InputOptions.m_FontSize = BodySize;
			const bool Changed = ui_widget::InputField(SettingsUiContext("settings_tee_skin_prefix_text_input", TeeMetrics.m_UiScale), &s_SkinPrefixInput, InputRect, InputOptions).m_Changed;
			ShouldRefresh = (Changed && Column.Input() && !Ui()->RenderOnly()) || ShouldRefresh;

			static const char *s_apPrefixes[] = {"kitty", "santa"};
			static CButtonContainer s_aPrefixButtons[std::size(s_apPrefixes)];
			int CurrentPrefix = -1;
			for(size_t Index = 0; Index < std::size(s_apPrefixes); ++Index)
			{
				if(str_comp(g_Config.m_ClSkinPrefix, s_apPrefixes[Index]) == 0)
				{
					CurrentPrefix = (int)Index;
					break;
				}
			}
			const int NewPrefix = DoSegmentedChoice(s_aPrefixButtons, s_apPrefixes, (int)std::size(s_apPrefixes), CurrentPrefix, PresetRect, ui_token::radius::BASE, nullptr, BodySize);
			if(NewPrefix >= 0 && NewPrefix != CurrentPrefix && Column.Input() && !Ui()->RenderOnly())
			{
				str_copy(g_Config.m_ClSkinPrefix, s_apPrefixes[NewPrefix]);
				ShouldRefresh = true;
			}
		}
	};
	const auto *pScope = CSettingsContentLayoutScope::Current();
	if(pScope == nullptr)
	{
		const auto Layout = ResolveSettingsTeeOptionsLayout(Content, TeeMetrics);
		CUIRect Downloads = Layout.m_Downloads, Prefix = Layout.m_Prefix;
		aColumns[0].m_RenderMeasured(Downloads);
		aColumns[1].m_RenderMeasured(Prefix);
	}
	else
	{
		dbg_assert(s_pTeeOptionsContent != nullptr, "missing Tee options declaration owner");
		const float Width = pScope->m_pLayout ? pScope->m_pLayout->Width() : Content.w;
		auto Layout = ResolveSettingsTeeOptionsLayout({Content.x, Content.y, Width, 0}, TeeMetrics);
		const bool Inline = Layout.m_Downloads.y == Layout.m_Prefix.y;
		std::array<CUIRect, 2> aViews{Layout.m_Downloads, Layout.m_Prefix};
		for(size_t Index = 0; Index < aColumns.size(); ++Index)
		{
			qm_settings_content::UseDeclaredContent(aColumns[Index], Ui(), TextRender());
			if(Flow.Declaring())
				s_pTeeOptionsContent->m_apLayouts[Index] = aColumns[Index].m_ResolveContentLayout(s_pTeeOptionsContent->m_aCaches[Index], aViews[Index].w);
			if(s_pTeeOptionsContent->m_apLayouts[Index])
				aViews[Index].h = s_pTeeOptionsContent->m_apLayouts[Index]->Height();
		}
		const float Height = Inline ? std::max(aViews[0].h, aViews[1].h) : aViews[0].h + TeeMetrics.m_SectionGap + aViews[1].h;
		CUIRect Block;
		if(!Flow.Control("tee-skin-options-columns", Height, Block))
			return;
		aViews[0].x = aViews[1].x = Block.x;
		aViews[0].y = aViews[1].y = Block.y;
		if(Inline)
			aViews[1].x += aViews[0].w + TeeMetrics.m_SectionGap;
		else
			aViews[1].y += aViews[0].h + TeeMetrics.m_SectionGap;
		for(size_t Index = 0; Index < aColumns.size(); ++Index)
		{
			const auto &pLayout = s_pTeeOptionsContent->m_apLayouts[Index];
			if(!pLayout || pLayout->Width() <= 0.0f)
				continue;
			if(Flow.Input() && !Ui()->RenderOnly())
				aColumns[Index].m_ContentLayoutInput(*pLayout, aViews[Index]);
			else if(Flow.Drawing())
				aColumns[Index].m_ContentLayoutDraw(*pLayout, aViews[Index]);
		}
	}
	if(ShouldRefresh)
		RefreshSettingsTeeSkins();
}

namespace
{
	struct STeeEditorState
	{
		std::array<CButtonContainer, NUM_DUMMIES> m_aPreviewButtons;
		std::array<CButtonContainer, 2> m_aPreviewModes;
		bool m_FrozenPreview = false;
		std::array<CButtonContainer, 3> m_aSkinTransfers;
		std::array<CLineInput, NUM_DUMMIES> m_aSkinInputs;
		std::array<SSettingsPreviewSkinTransitionState, NUM_DUMMIES> m_aTransitions;
	};
	STeeEditorState gs_TeeEditorState;
}

void CMenus::CommitSettingsTeeSkinEdits()
{
	if(Ui()->RenderOnly())
		return;
	GameClient()->m_Skins.CommitRecentSkins();
}

bool CMenus::ProcessSettingsTeeEditorInput(CUIRect Content, const SSettingsContentMetrics &Metrics)
{
	if(m_MenuTextPlanCollecting || Ui()->RenderOnly())
		return false;
	int *pUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;
	const SSettingsTeeEditorLayout Layout = ResolveSettingsTeeEditorLayout(Content, Metrics, *pUseCustomColor != 0);
	for(int Target = 0; Target < NUM_DUMMIES; ++Target)
	{
		if(Ui()->DoButtonLogic(&gs_TeeEditorState.m_aPreviewButtons[Target], m_Dummy == (Target != 0), &Layout.m_aPreviews[Target], BUTTONFLAG_LEFT) && m_Dummy != (Target != 0))
		{
			CommitSettingsTeeSkinEdits();
			if(CLineInput::GetActiveInput() != nullptr)
				CLineInput::GetActiveInput()->Deactivate();
			m_Dummy = Target != 0;
			m_SkinListScrollToSelected = false;
			return true;
		}
	}
	for(int Action = 0; Action < 3; ++Action)
	{
		if(!Ui()->DoButtonLogic(&gs_TeeEditorState.m_aSkinTransfers[Action], 0, &Layout.m_aSkinTransfers[Action], BUTTONFLAG_LEFT))
			continue;
		CommitSettingsTeeSkinEdits();
		if(CLineInput::GetActiveInput() != nullptr)
			CLineInput::GetActiveInput()->Deactivate();
		if(Action == 1)
			QmSwapTeeSkinSettings(g_Config);
		else
			QmCopyTeeSkinSettings(g_Config, Action == 0 ? ETeeSkinApplyTarget::DUMMY : ETeeSkinApplyTarget::MAIN);
		for(int ChangedTarget = 0; ChangedTarget < NUM_DUMMIES; ++ChangedTarget)
		{
			if(Action != 1 && ChangedTarget != (Action == 0 ? 1 : 0))
				continue;
			SetNeedSendInfo(ChangedTarget != 0);
			GameClient()->m_Skins.RecordRecentSkin(ChangedTarget);
		}
		m_SkinListScrollToSelected = true;
		GameClient()->m_Skins.RebuildSkinListPlan();
		return true;
	}
	if(!Ui()->DoButtonLogic(pUseCustomColor, *pUseCustomColor, &Layout.m_CustomColors, BUTTONFLAG_LEFT))
		return false;
	*pUseCustomColor ^= 1;
	SetNeedSendInfo(m_Dummy);
	CommitSettingsTeeSkinEdits();
	GameClient()->m_Skins.RecordRecentSkin(m_Dummy);
	return true;
}

void CMenus::RenderSettingsTeeEditor(CUIRect Content, const SSettingsContentMetrics &TeeMetrics)
{
	const float UiScale = TeeMetrics.m_UiScale;
	const float BodySize = TeeMetrics.m_BodySize;
	const float Gap = TeeMetrics.m_LineSpacing;
	const int Target = m_Dummy ? 1 : 0;
	int *pUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;
	unsigned *pColorBody = m_Dummy ? &g_Config.m_ClDummyColorBody : &g_Config.m_ClPlayerColorBody;
	unsigned *pColorFeet = m_Dummy ? &g_Config.m_ClDummyColorFeet : &g_Config.m_ClPlayerColorFeet;
	int *pEmote = m_Dummy ? &g_Config.m_ClDummyDefaultEyes : &g_Config.m_ClPlayerDefaultEyes;
	char *pSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const size_t SkinNameSize = m_Dummy ? sizeof(g_Config.m_ClDummySkin) : sizeof(g_Config.m_ClPlayerSkin);
	const SSettingsTeeEditorLayout Layout = ResolveSettingsTeeEditorLayout(Content, TeeMetrics, *pUseCustomColor != 0);
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	SLabelProperties CompactLabel;
	CompactLabel.m_DisallowNewline = true;
	CompactLabel.m_StopAtEnd = true;
	CompactLabel.m_MinimumFontSize = BodySize;
	CUIRect ModeLabel, ModeControl;
	SplitSettingsControlRow(Layout.m_PreviewMode, TeeMetrics, &ModeLabel, &ModeControl, ESettingsControlWidth::SHORT_CHOICE);
	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-preview-mode", &ModeLabel, Localize("Preview"), BodySize, TEXTALIGN_ML);
	const char *apModes[] = {Localize("Normal"), Localize("Freeze")};
	const int Mode = DoSegmentedChoice(gs_TeeEditorState.m_aPreviewModes.data(), apModes, std::size(apModes), gs_TeeEditorState.m_FrozenPreview, ModeControl, ui_token::radius::BASE, nullptr, BodySize);
	if(!Ui()->RenderOnly() && !Ui()->MeasurementOnly())
		gs_TeeEditorState.m_FrozenPreview = Mode == 1;
	if(DoSettingsButton_CheckBox(SETTINGS_TEE, -1, &g_Config.m_ClShowNinja, "tee-show-ninja", Localize("Show ninja skin"), g_Config.m_ClShowNinja, &Layout.m_ShowNinja) && !Ui()->RenderOnly())
		g_Config.m_ClShowNinja ^= 1;
	float TeeScale = 1.0f;
	float TeeAlphaScale = 1.0f;
	if(g_Config.m_QmUiMotionLevel > 0 && m_TeeEntranceStartTime > 0)
	{
		const SCountryFlagEntryParams EntryParams = ResolveCountryFlagEntryParams(g_Config.m_QmUiMotionLevel);
		const float Elapsed = (time_get() - m_TeeEntranceStartTime) / (float)time_freq();
		if(Elapsed >= 0.0f && Elapsed < EntryParams.m_Duration)
		{
			const float Progress = Elapsed / EntryParams.m_Duration;
			TeeScale = ComputeCountryFlagEntryScale(Progress, EntryParams.m_Overshoot);
			TeeAlphaScale = ComputeCountryFlagEntryAlpha(Progress);
		}
	}
	for(int PreviewTarget = 0; PreviewTarget < NUM_DUMMIES; ++PreviewTarget)
	{
		const bool Dummy = PreviewTarget != 0;
		const bool Selected = PreviewTarget == Target;
		const CUIRect &Preview = Layout.m_aPreviews[PreviewTarget];
		const auto PreviewLayout = ResolveSettingsTeePreviewLayout(Preview, TeeMetrics, Ui()->PixelSize());
		auto *pButton = &gs_TeeEditorState.m_aPreviewButtons[PreviewTarget];
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Selected = Selected;
		Options.m_Radius = PreviewLayout.m_Radius;
		ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), pButton, Preview, Options);
		Ui()->DoButtonLogic(pButton, Selected, &Preview, BUTTONFLAG_LEFT);
		const char *pName = Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
		const char *pPreviewSkin = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
		if(pPreviewSkin[0] == '\0')
			pPreviewSkin = "default";
		char aHeading[128];
		str_format(aHeading, sizeof(aHeading), "%s: %s", Dummy ? Localize("Dummy") : Localize("Player"), pName);
		CompactLabel.m_MaxWidth = PreviewLayout.m_Heading.w;
		Ui()->DoLabel(&PreviewLayout.m_Heading, aHeading, BodySize, TEXTALIGN_ML, CompactLabel);
		const CUIRect &Flag = PreviewLayout.m_Flag;
		if(!Ui()->DrawingSuppressed())
			GameClient()->m_CountryFlags.Render(Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry, ui_token::color::NEUTRAL_WHITE, Flag.x, Flag.y, Flag.w, Flag.h, m_TeeEntranceStartTime);
		CompactLabel.m_MaxWidth = PreviewLayout.m_SkinName.w;
		Ui()->DoLabel(&PreviewLayout.m_SkinName, pPreviewSkin, BodySize, TEXTALIGN_MC, CompactLabel);

		const CUIRect &TeeRect = PreviewLayout.m_Tee;
		CTeeRenderInfo OwnInfo;
		OwnInfo.Apply(GameClient()->m_Skins.Find(pPreviewSkin));
		OwnInfo.ApplyColors(Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor, Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody, Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
		const char *pRenderedSkin = pPreviewSkin;
		if(gs_TeeEditorState.m_FrozenPreview)
		{
			OwnInfo.m_TeeRenderFlags |= TEE_EFFECT_FROZEN | TEE_NO_WEAPON;
			const auto &pNinja = GameClient()->m_Players.NinjaTeeRenderInfo();
			if(g_Config.m_ClShowNinja && pNinja && pNinja->TeeRenderInfo().Valid())
			{
				OwnInfo.ApplyNinjaSkin(pNinja->TeeRenderInfo(), OwnInfo, PreviewTarget, false,
					g_Config.m_QmColorFreeze, g_Config.m_QmColorFreezeFeet, g_Config.m_QmColorFreezeDarken);
				pRenderedSkin = "x_ninja";
			}
		}
		const float RequestedSize = 52.0f * UiScale;
		OwnInfo.m_Size = RequestedSize;
		float MinX, MinY, MaxX, MaxY;
		GetSettingsTeePreviewBounds(CAnimState::GetIdle(), OwnInfo, MinX, MinY, MaxX, MaxY);
		OwnInfo.m_Size = std::min(RequestedSize, SettingsSkinPreviewSize(TeeRect.h, TeeRect.w, RequestedSize, MaxX - MinX, MaxY - MinY));
		SSettingsPreviewSkinTransitionState &Transition = gs_TeeEditorState.m_aTransitions[PreviewTarget];
		if(!Ui()->AnimationsBlocked() && !Ui()->MeasurementOnly() && !m_MenuTextPlanCollecting)
			Transition.Update(pRenderedSkin, OwnInfo, Now);
		SQmTeeHueCycleConfig Hue;
		Hue.m_Enabled = g_Config.m_QmCycleTeeHue != 0 && (!Dummy || g_Config.m_QmCycleTeeHueDummy != 0);
		const bool CustomColors7 = Dummy ? (g_Config.m_ClDummy7UseCustomColorBody != 0 || g_Config.m_ClDummy7UseCustomColorFeet != 0) : (g_Config.m_ClPlayer7UseCustomColorBody != 0 || g_Config.m_ClPlayer7UseCustomColorFeet != 0);
		Hue.m_PlayerUsesCustomColors = (Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor) != 0 || CustomColors7;
		Hue.m_TClientRainbowTees = g_Config.m_QmRainbowTees != 0;
		Hue.m_SpeedDegreesPerSecond = g_Config.m_QmCycleTeeHueSpeed;
		Hue.m_TimeSeconds = Now.count() / 1000000000.0;
		Hue.m_SixupIndex = 0;
		CTeeRenderInfo Current = OwnInfo;
		QmApplyTeeHueCycle(Current, Hue);
		CTeeRenderInfo Previous;
		const CTeeRenderInfo *pPrevious = Transition.PreviousInfo(Now);
		if(pPrevious != nullptr)
		{
			Previous = *pPrevious;
			Previous.m_Size = Current.m_Size;
			QmApplyTeeHueCycle(Previous, Hue);
			pPrevious = &Previous;
		}
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Current, Offset);
		const vec2 Position = TeeRect.Center() + Offset + vec2(SettingsSkinPreviewCenterOffset(MinX, MaxX) * OwnInfo.m_Size / RequestedSize, 0.0f);
		const vec2 Delta = Ui()->MousePos() - Position;
		const float Distance = length(Delta);
		const vec2 Direction = Distance > 0.001f ? normalize(Delta) : vec2(1.0f, 0.0f);
		const int Emote = gs_TeeEditorState.m_FrozenPreview ? EMOTE_PAIN : (Distance < 20.0f ? EMOTE_HAPPY : (Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes));
		if(!Ui()->DrawingSuppressed() && TeeScale > 0.001f && TeeAlphaScale > 0.001f)
		{
			Current.m_Size *= TeeScale;
			Current.m_BloodColor.a *= TeeAlphaScale;
			Current.m_ColorBody.a *= TeeAlphaScale;
			Current.m_ColorFeet.a *= TeeAlphaScale;
			if(pPrevious != nullptr)
			{
				Previous.m_Size *= TeeScale;
				Previous.m_BloodColor.a *= TeeAlphaScale;
				Previous.m_ColorBody.a *= TeeAlphaScale;
				Previous.m_ColorFeet.a *= TeeAlphaScale;
			}
			Ui()->ClipEnable(&TeeRect);
			RenderTools()->RenderTeeWithSkinChangeTransition(CAnimState::GetIdle(), pPrevious, &Current, Emote, Direction, Position, Transition.Progress(Now));
			Ui()->ClipDisable();
		}
		const CSkins::CSkinContainer *pContainer = GameClient()->m_Skins.FindContainerOrNullptr(pPreviewSkin);
		if(pContainer == nullptr || pContainer->State() != CSkins::CSkinContainer::EState::LOADED)
		{
			CUIRect Status{TeeRect.x, TeeRect.y, TeeMetrics.m_ButtonHeight, TeeMetrics.m_ButtonHeight};
			const auto Indicator = pContainer == nullptr ? CSkins::CSkinContainer::EStatusIndicator::ERROR : CSkins::CSkinContainer::StatusIndicator(pContainer->State());
			static char s_aStatusIds[NUM_DUMMIES];
			Ui()->RegisterPassiveHotItem(&s_aStatusIds[PreviewTarget], &Status);
			if(Indicator == CSkins::CSkinContainer::EStatusIndicator::LOADING)
			{
				ui_widget::DrawProgressRing(Ui(), Status.Center(), 5.0f * UiScale);
				GameClient()->m_Tooltips.DoToolTip(&s_aStatusIds[PreviewTarget], &Status, Localize("Skin is loading."));
			}
			else
			{
				// 图标盒子按同行文字字号反推（字形 = 盒子 × 0.8），直接传字号会让字形小一圈。
				Ui()->DoLabel_QmIcon(&Status, EQmIcon::TRIANGLE_EXCLAMATION, FONT_ICON_TRIANGLE_EXCLAMATION, ui_token::control::IconBox(BodySize), TEXTALIGN_MC);
				const char *pStatus = pContainer == nullptr ? Localize("This skin name cannot be used.") : Indicator == CSkins::CSkinContainer::EStatusIndicator::NOT_FOUND ? Localize("Skin could not be found.") :
																							      Localize("Skin could not be loaded due to an error. Check the local console for details.");
				GameClient()->m_Tooltips.DoToolTip(&s_aStatusIds[PreviewTarget], &Status, pStatus);
			}
		}
	}

	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-edit-target", &Layout.m_TargetLabel, m_Dummy ? Localize("Dummy") : Localize("Player"), BodySize, TEXTALIGN_ML);
	RenderSettingsTeeIdentity(Layout.m_Identity, nullptr, BodySize, Layout.m_StackIdentity);
	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-current-skin", &Layout.m_SkinLabel, Localize("Your skin"), BodySize, TEXTALIGN_ML);
	CLineInput &SkinInput = gs_TeeEditorState.m_aSkinInputs[Target];
	SkinInput.SetBuffer(pSkinName, SkinNameSize);
	SkinInput.SetEmptyText("default");
	ui_widget::SInputFieldOptions SkinInputOptions;
	SkinInputOptions.m_Clearable = true;
	SkinInputOptions.m_FontSize = BodySize;
	if(ui_widget::InputField(SettingsUiContext("settings_tee_skin_name_text_input", UiScale), &SkinInput, Layout.m_SkinInput, SkinInputOptions).m_Changed)
	{
		SetNeedSendInfo(m_Dummy);
		m_SkinListScrollToSelected = true;
		GameClient()->m_Skins.RebuildSkinListPlan();
		GameClient()->m_Skins.StageRecentSkin(QmCurrentTeeSkin(g_Config, m_Dummy));
	}
	static CButtonContainer s_RandomSkin;
	if(Ui()->DoButton_QmIcon(&s_RandomSkin, EQmIcon::DICE_FIVE, FONT_ICON_DICE_FIVE, 0, &Layout.m_RandomSkin, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		CommitSettingsTeeSkinEdits();
		GameClient()->m_Skins.RandomizeSkin(Target);
		SetNeedSendInfo(m_Dummy);
		m_SkinListScrollToSelected = true;
		GameClient()->m_Skins.RecordRecentSkin(Target);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkin, &Layout.m_RandomSkin, Localize("Create a random skin"));

	const std::array<EQmIcon, 3> aTransferIcons = {EQmIcon::ARROW_RIGHT, EQmIcon::ARROWS_LEFT_RIGHT, EQmIcon::ARROW_LEFT};
	const std::array<const char *, 3> apTransferGlyphs = {"\xEE\x81\xAC", FONT_ICON_ARROWS_LEFT_RIGHT, "\xEE\x81\x98"};
	const std::array<const char *, 3> apTransferTooltips = {Localize("Copy skin from player"), Localize("Swap"), Localize("Copy skin from dummy")};
	for(int Action = 0; Action < 3; ++Action)
	{
		const CUIRect &Button = Layout.m_aSkinTransfers[Action];
		Ui()->DoButton_QmIcon(&gs_TeeEditorState.m_aSkinTransfers[Action], aTransferIcons[Action], apTransferGlyphs[Action], 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
		GameClient()->m_Tooltips.DoToolTip(&gs_TeeEditorState.m_aSkinTransfers[Action], &Button, apTransferTooltips[Action]);
	}

	CTeeRenderInfo EyeInfo;
	EyeInfo.Apply(GameClient()->m_Skins.Find(pSkinName[0] == '\0' ? "default" : pSkinName));
	EyeInfo.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
	const SSettingsTeeEmoteSliderLayout SliderLayout = ResolveSettingsTeeEmoteSliderLayout(Layout.m_Eyes, TeeMetrics);
	const CUIRect &Track = SliderLayout.m_TrackRect;
	const float EyeRequestedSize = SliderLayout.m_TeeSize;
	EyeInfo.m_Size = EyeRequestedSize;
	float EyeMinX, EyeMinY, EyeMaxX, EyeMaxY;
	GetSettingsTeePreviewBounds(CAnimState::GetIdle(), EyeInfo, EyeMinX, EyeMinY, EyeMaxX, EyeMaxY);
	EyeInfo.m_Size = std::min(EyeRequestedSize, SettingsSkinPreviewSize(Track.h, Track.w / (float)NUM_EMOTES - Gap, EyeRequestedSize, EyeMaxX - EyeMinX, EyeMaxY - EyeMinY));
	static char s_aEyeSliderIds[NUM_DUMMIES];
	static CButtonContainer s_aEyes[NUM_DUMMIES][NUM_EMOTES];
	const void *pSliderId = &s_aEyeSliderIds[Target];
	const auto SetEmote = [&](int Emote) {
		if(*pEmote == Emote)
			return;
		*pEmote = Emote;
		if(Target == g_Config.m_ClDummy)
			GameClient()->m_Emoticon.EyeEmote(Emote);
	};
	if(!Ui()->RenderOnly() && !Ui()->IsPopupOpen())
	{
		const bool MouseInsideTrack = Ui()->MouseHovered(&Track);
		if(Ui()->CheckActiveItem(pSliderId))
		{
			if(Ui()->MouseButton(0))
				SetEmote(ResolveTeeEmoteSliderTargetFromPoint(Track, Ui()->MouseX()));
			else
				Ui()->SetActiveItem(nullptr);
		}
		else if(MouseInsideTrack)
		{
			if(Ui()->MouseButtonClicked(0))
			{
				Ui()->SetActiveItem(pSliderId);
				SetEmote(ResolveTeeEmoteSliderTargetFromPoint(Track, Ui()->MouseX()));
			}
			else if(Ui()->HotItem() == nullptr)
				Ui()->SetHotItem(pSliderId);
		}
		Ui()->RegisterWheelOwner(pSliderId, EUiWheelOwnerPriority::COMPOSITE_CONTROL, Track, MouseInsideTrack);
		float WheelDelta = 0.0f;
		if(Ui()->TryConsumeWheel(pSliderId, &WheelDelta))
			SetEmote(StepTeeEmoteSlider(*pEmote, WheelDelta > 0.0f ? -1 : 1));
	}
	DrawRoundedSurface(TabBarUiContext(), Track, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), ui_token::radius::PILL);
	const int ActiveEmote = std::clamp(*pEmote, 0, NUM_EMOTES - 1);
	auto *pUiRuntime = GameClient()->UiRuntimeV2();
	const uint64_t ThumbNodeKey = BuildUiAnimNodeKey(MakeUiScopeHash("settings_tee_eyes_slider_thumb"), Target);
	// 测量/文字收集趟冻结滑块弹簧（AnimationsBlocked），防止预热帧把滑块推到目标位；
	// DRAW 回放与直绘（两 depth 相等）正常推进。
	const CUIRect AnimatedThumb = ResolveSettingsTeeEmoteSliderThumb(SliderLayout, ActiveEmote, UiScale, (pUiRuntime != nullptr && !Ui()->AnimationsBlocked()) ? &pUiRuntime->AnimRuntime() : nullptr, ThumbNodeKey);
	DrawRoundedSurface(TabBarUiContext(), AnimatedThumb, ColorRGBA(1.0f, 1.0f, 1.0f, 0.20f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), ui_token::radius::PILL);
	static const char *s_apEmoteNames[] = {"Normal", "Pain", "Happy", "Surprise", "Angry", "Blink"};
	for(int Emote = 0; Emote < NUM_EMOTES; ++Emote)
	{
		const CUIRect &Slot = SliderLayout.m_aSlotRects[Emote];
		const bool IsActive = Emote == ActiveEmote;
		const bool IsHovered = Ui()->VisualSurfaceHovered(&Slot) && !Ui()->IsPopupOpen();
		if(IsHovered && !IsActive)
		{
			CUIRect HoverRect;
			Slot.Margin(2.5f * UiScale, &HoverRect);
			DrawRoundedSurface(TabBarUiContext(), HoverRect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), ColorRGBA(), ui_token::radius::PILL);
		}
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeInfo, Offset);
		const float TeeAlpha = IsActive ? 1.0f : (IsHovered ? 0.90f : 0.78f);
		if(!Ui()->DrawingSuppressed())
		{
			Ui()->ClipEnable(&Slot);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &EyeInfo, Emote, vec2(1.0f, 0.0f), Slot.Center() + Offset + vec2(SettingsSkinPreviewCenterOffset(EyeMinX, EyeMaxX) * EyeInfo.m_Size / EyeRequestedSize, 0.0f), TeeAlpha);
			Ui()->ClipDisable();
		}
		char aTooltip[128];
		str_format(aTooltip, sizeof(aTooltip), "%s (%d) - %s", Localize(s_apEmoteNames[Emote]), Emote, Localize("Choose default eyes when joining a server"));
		GameClient()->m_Tooltips.DoToolTip(&s_aEyes[Target][Emote], &Slot, aTooltip);
	}
	DoSettingsButton_CheckBox(SETTINGS_TEE, -1, pUseCustomColor, m_Dummy ? "tee-dummy-custom-colors" : "tee-player-custom-colors", Localize("Custom colors"), *pUseCustomColor, &Layout.m_CustomColors);
	if(!*pUseCustomColor)
	{
		if(!Ui()->MouseButton(0) && !SkinInput.IsActive())
			CommitSettingsTeeSkinEdits();
		return;
	}

	static CButtonContainer s_RandomColors;
	if(Ui()->DoButton_QmIcon(&s_RandomColors, EQmIcon::DICE_FIVE, FONT_ICON_DICE_FIVE, 0, &Layout.m_RandomColors, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		CommitSettingsTeeSkinEdits();
		*pColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
		*pColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
		SetNeedSendInfo(m_Dummy);
		GameClient()->m_Skins.RecordRecentSkin(Target);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_RandomColors, &Layout.m_RandomColors, Localize("Random Colors"));
	static CLineInputBuffered<QM_TEE_COLOR_CODE_INPUT_SIZE> s_aaColorCodes[NUM_DUMMIES][2];
	const std::array<unsigned *, 2> apColors = {pColorBody, pColorFeet};
	const std::array<const char *, 2> apLabels = {Localize("Body"), Localize("Feet")};
	const std::array<CUIRect, 2> aTitles = {Layout.m_Colors.m_BodyTitle, Layout.m_Colors.m_FeetTitle};
	std::array<CUIRect, 2> aControls = {Layout.m_Colors.m_BodyControls, Layout.m_Colors.m_FeetControls};
	bool EditingText = SkinInput.IsActive();
	for(int Part = 0; Part < 2; ++Part)
	{
		CUIRect Label, Code;
		aTitles[Part].VSplitLeft(aTitles[Part].w * 0.42f, &Label, &Code);
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, Part == 0 ? "tee_custom_color_body_label" : "tee_custom_color_feet_label", &Label, apLabels[Part], BodySize, TEXTALIGN_ML);
		CLineInput &Input = s_aaColorCodes[Target][Part];
		if(!Input.IsActive())
		{
			const std::array<char, 8> Color = QmFormatTeeColorCode(*apColors[Part]);
			if(str_comp(Input.GetString(), Color.data()) != 0)
				Input.Set(Color.data());
		}
		ui_widget::SInputFieldOptions ColorOptions;
		ColorOptions.m_FontSize = BodySize;
		ColorOptions.m_TextAlign = TEXTALIGN_MC;
		if(ui_widget::InputField(SettingsUiContext(Part == 0 ? "settings_tee_color_body" : "settings_tee_color_feet", UiScale), &Input, Code, ColorOptions).m_Changed)
		{
			const std::optional<unsigned> Color = QmParseTeeColorCode(Input.GetString());
			if(Color.has_value() && *Color != *apColors[Part])
			{
				*apColors[Part] = *Color;
				SetNeedSendInfo(m_Dummy);
				GameClient()->m_Skins.StageRecentSkin(QmCurrentTeeSkin(g_Config, m_Dummy));
			}
		}
		if(RenderHslaScrollbars(&aControls[Part], apColors[Part], false, ColorHSLA::DARKEST_LGT, TeeMetrics))
		{
			SetNeedSendInfo(m_Dummy);
			GameClient()->m_Skins.StageRecentSkin(QmCurrentTeeSkin(g_Config, m_Dummy));
		}
		EditingText = EditingText || Input.IsActive();
	}
	if(!Ui()->MouseButton(0) && !EditingText)
		CommitSettingsTeeSkinEdits();
}

void CMenus::RenderSettingsTeeSkinList(CUIRect Content, const SSettingsContentMetrics &TeeMetrics)
{
	const float UiScale = TeeMetrics.m_UiScale;
	const float BodySize = TeeMetrics.m_BodySize;
	const int QueueDummy = m_Dummy ? 1 : 0;
	char *pSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const size_t SkinNameSize = m_Dummy ? sizeof(g_Config.m_ClDummySkin) : sizeof(g_Config.m_ClPlayerSkin);
	int *pUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;
	unsigned *pColorBody = m_Dummy ? &g_Config.m_ClDummyColorBody : &g_Config.m_ClPlayerColorBody;
	unsigned *pColorFeet = m_Dummy ? &g_Config.m_ClDummyColorFeet : &g_Config.m_ClPlayerColorFeet;
	int *pEmote = m_Dummy ? &g_Config.m_ClDummyDefaultEyes : &g_Config.m_ClPlayerDefaultEyes;
	bool ShouldRefresh = false;
	int RefreshVisibleRows = 0;
	char aRefreshFirstVisibleSkin[MAX_SKIN_LENGTH] = {};
	CUIRect MainView = Content;
	CUIRect Button, Label;
	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const auto RenderDefaultSkinPlaceholder = [&](const CUIRect &PreviewRect) {
		if(pDefaultSkin == nullptr || Ui()->DrawingSuppressed())
			return;
		CTeeRenderInfo Info;
		Info.Apply(pDefaultSkin);
		Info.m_Size = 50.0f;
		float MinX, MinY, MaxX, MaxY;
		GetSettingsTeePreviewBounds(CAnimState::GetIdle(), Info, MinX, MinY, MaxX, MaxY);
		Info.m_Size = SettingsSkinPreviewSize(PreviewRect.h, PreviewRect.w, 50.0f, MaxX - MinX, MaxY - MinY);
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
		CUIRect TeeClip = PreviewRect;
		TeeClip.Margin(3.0f, &TeeClip);
		const vec2 TeeRenderPos = vec2(TeeClip.Center().x + SettingsSkinPreviewCenterOffset(MinX, MaxX) * Info.m_Size / 50.0f, TeeClip.Center().y + OffsetToMid.y);
		Ui()->ClipEnable(&TeeClip);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, *pEmote, vec2(1.0f, 0.0f), TeeRenderPos);
		Ui()->ClipDisable();
	};
	// Skin loading status
	using ESkinStatus = CSkins::CSkinContainer::EStatusIndicator;
	const auto &&RenderSkinStatus = [&](CUIRect Parent, ESkinStatus Indicator, const void *pStatusTooltipId) {
		if(Indicator == ESkinStatus::NONE)
			return;

		CUIRect StatusIcon;
		const float IconBox = ui_token::control::IconBox(BodySize);
		Parent.HSplitTop(minimum(Parent.h, IconBox), &StatusIcon, nullptr);
		StatusIcon.VSplitLeft(minimum(StatusIcon.w, IconBox), &StatusIcon, nullptr);

		Ui()->RegisterPassiveHotItem(pStatusTooltipId, &StatusIcon);
		if(Indicator == ESkinStatus::LOADING)
		{
			ui_widget::DrawProgressRing(Ui(), StatusIcon.Center(), minimum(StatusIcon.w, StatusIcon.h) * 0.25f);
			GameClient()->m_Tooltips.DoToolTip(pStatusTooltipId, &StatusIcon, Localize("Skin is loading."));
		}
		else
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			// 状态图标字形与同行文字同号：盒子由字号反推，不再写死 12px 盒子。
			const bool NotFound = Indicator == ESkinStatus::NOT_FOUND;
			Ui()->DoLabel_QmIcon(&StatusIcon, NotFound ? EQmIcon::QUESTION : EQmIcon::TRIANGLE_EXCLAMATION, NotFound ? FONT_ICON_QUESTION : FONT_ICON_TRIANGLE_EXCLAMATION, minimum(StatusIcon.w, StatusIcon.h), TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			const char *pErrorTooltip = NotFound ? Localize("Skin could not be found.") : Localize("Skin could not be loaded due to an error. Check the local console for details.");
			GameClient()->m_Tooltips.DoToolTip(pStatusTooltipId, &StatusIcon, pErrorTooltip);
		}
	};
	const SSettingsTeeToolbarLayout Toolbar = ResolveSettingsTeeToolbarLayout(MainView, TeeMetrics);
	MainView.HSplitTop(Toolbar.m_Height, nullptr, &MainView);
	const CUIRect QuickSearch = Toolbar.m_Search;
	const CUIRect DatabaseButton = Toolbar.m_aTools[0];
	const CUIRect DirectoryButton = Toolbar.m_aTools[1];
	const CUIRect EditTextureButton = Toolbar.m_aTools[2];
	const CUIRect RefreshButton = Toolbar.m_aTools[3];
	// Skin selector
	static CListBox s_ListBox;
	s_ListBox.SetAutoScrollSelectionOnReentry(false);
	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	static CButtonContainer s_aCollectionButtons[3];
	const char *apCollectionLabels[] = {Localize("All"), Localize("Favorites"), Localize("Recent")};
	// 三档集合用胶囊滑块（设置页多选一的同一组件）：槽高与字号都不变，选中态由滑块
	// 表达，不再并排三个各自带底色的按钮。
	const int CurrentCollection = std::clamp(static_cast<int>(gs_TeeSkinCollection.m_Collection), 0, 2);
	const int NewCollection = DoSegmentedChoice(s_aCollectionButtons, apCollectionLabels, 3, CurrentCollection, Toolbar.m_Collection, ui_token::radius::BASE, nullptr, BodySize);
	if(NewCollection != CurrentCollection)
	{
		CommitSettingsTeeSkinEdits();
		gs_TeeSkinCollection.m_Collection = static_cast<ETeeSkinCollection>(NewCollection);
		// ALL 不经过缓存；切到收藏/最近时强制按当前已物化前缀重建一次，避免看到旧副本。
		gs_TeeSkinCollection.m_Cache.Invalidate();
		s_ListBox.ResetScroll();
		m_SkinListScrollToSelected = false;
	}
	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_skin_source_filter_label", &Toolbar.m_FilterLabel, Localize("Data source"), BodySize, TEXTALIGN_ML);
	static CUi::SDropDownState s_SourceFilterState;
	const char *apSourceFilters[] = {
		Localize("All"),
		Localize("Official"),
		Localize("Community"),
		Localize("DDStats"),
		Localize("Custom"),
		Localize("Vanilla skins only"),
	};
	const int CurrentSourceFilter = static_cast<int>(ResolveSkinListFilter(g_Config.m_QmSkinListFilter, g_Config.m_ClVanillaSkinsOnly != 0));
	CUIRect SourceFilter = Toolbar.m_Filter;
	const int NewSourceFilter = DoSettingsDropDown(&SourceFilter, CurrentSourceFilter, apSourceFilters, std::size(apSourceFilters), s_SourceFilterState, {}, &g_Config.m_QmSkinListFilter);
	if(NewSourceFilter >= 0 && NewSourceFilter < (int)std::size(apSourceFilters) && NewSourceFilter != CurrentSourceFilter)
	{
		CommitSettingsTeeSkinEdits();
		g_Config.m_QmSkinListFilter = NewSourceFilter;
		const int VanillaOnly = NewSourceFilter == (int)ESkinListFilter::VANILLA ? 1 : 0;
		if(g_Config.m_ClVanillaSkinsOnly != VanillaOnly)
		{
			g_Config.m_ClVanillaSkinsOnly = VanillaOnly;
			ShouldRefresh = true;
		}
		GameClient()->m_Skins.RebuildSkinListPlan();
		s_ListBox.ResetScroll();
		m_SkinListScrollToSelected = false;
	}
	if(gs_TeeSkinCollection.m_Collection == ETeeSkinCollection::RECENT)
	{
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-recent-label", &Toolbar.m_SortLabel, Localize("Recent"), BodySize, TEXTALIGN_ML);
		CUIRect Clear = Toolbar.m_Sort;
		Clear.VSplitRight(TeeMetrics.m_ButtonHeight, nullptr, &Clear);
		static CButtonContainer s_ClearRecent;
		if(Ui()->DoButton_QmIcon(&s_ClearRecent, EQmIcon::TRASH, FONT_ICON_TRASH, 0, &Clear, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
		{
			CommitSettingsTeeSkinEdits();
			GameClient()->m_Skins.ClearRecentSkins();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ClearRecent, &Clear, Localize("Clear recent skins"));
	}
	else
	{
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_skin_sort_label", &Toolbar.m_SortLabel, Localize("Skin sort"), BodySize, TEXTALIGN_ML);
		CUIRect Sort = Toolbar.m_Sort;
		static CUi::SDropDownState s_SortState;
		const char *apSortNames[] = {Localize("Name"), Localize("Time")};
		const int SortMode = DoSettingsDropDown(&Sort, std::clamp(g_Config.m_QmSkinSortMode, 0, 1), apSortNames, std::size(apSortNames), s_SortState, {}, &g_Config.m_QmSkinSortMode);
		if(SortMode != g_Config.m_QmSkinSortMode)
		{
			g_Config.m_QmSkinSortMode = SortMode;
			GameClient()->m_Skins.RebuildSkinListPlan();
		}
	}
	bool &s_SkinListScrollActiveLastFrame = gs_TeeSettingsPageState.m_SkinListScrollActiveLastFrame;
	int &s_SkinListScrollCooldownFrames = gs_TeeSettingsPageState.m_SkinListScrollCooldownFrames;
	int &s_SkinListPostScrollRecoveryFrames = gs_TeeSettingsPageState.m_SkinListPostScrollRecoveryFrames;
	size_t &s_BackgroundRequestCursor = gs_TeeSettingsPageState.m_BackgroundRequestCursor;
	if(!gs_TeeSettingsPageState.m_TeePageActiveLastFrame)
	{
		gs_TeeListDrainPerfSession.m_Active = false;
		ResetTeeSettingsPageState();
		m_SettingsHighPrioritySettled = false;
	}
	std::vector<CSkins::CSkinListEntry> &vSkinList = gs_TeeSkinCollection.Resolve(GameClient()->m_Skins, SkinList);
	char aSkinCount[96];
	const bool AllSkinsCollection = gs_TeeSkinCollection.m_Collection == ETeeSkinCollection::ALL;
	const bool PlanKnown = SkinList.PlanKnown();
	const int PlannedCount = AllSkinsCollection ? SkinList.ItemCount() : (int)vSkinList.size();
	if(PlanKnown || !vSkinList.empty())
	{
		str_format(aSkinCount, sizeof(aSkinCount), "%s: %d", Localize("Skin"), PlannedCount);
	}
	else
		str_format(aSkinCount, sizeof(aSkinCount), "%s: %s", Localize("Skin"), Localize("Loading…"));
	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_skin_list_count_label", &Toolbar.m_Count, aSkinCount, TeeMetrics.m_SmallSize, TEXTALIGN_ML);
	const uint64_t ListRevision = AllSkinsCollection ? SkinList.Revision() : gs_TeeSkinCollection.Revision();
	static std::vector<size_t> s_vVisibleSkinIndices;
	s_vVisibleSkinIndices.clear();
	if(s_vVisibleSkinIndices.capacity() < 32)
		s_vVisibleSkinIndices.reserve(32);
	std::vector<size_t> &vVisibleSkinIndices = s_vVisibleSkinIndices;
	std::vector<std::string> vCurrentVisibleSkinNames;
	SSettingsAdaptiveBudgetInput TeeBudgetInput;
	TeeBudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(TeeBudgetInput.m_aOperation, SettingsPerfActiveOperation(), sizeof(TeeBudgetInput.m_aOperation));
	str_copy(TeeBudgetInput.m_aPage, "settings:tee", sizeof(TeeBudgetInput.m_aPage));
	str_copy(TeeBudgetInput.m_aTab, "none", sizeof(TeeBudgetInput.m_aTab));
	str_copy(TeeBudgetInput.m_aContext, SettingsPerfContextName(), sizeof(TeeBudgetInput.m_aContext));
	TeeBudgetInput.m_FrameMsAverage = QmUiCpuMsAverage();
	TeeBudgetInput.m_FrameMsP95 = QmUiCpuMsP95();
	TeeBudgetInput.m_TargetFrameMs = QmUiTargetFrameMs();
	TeeBudgetInput.m_ScrollActive = m_SettingsScrollActive || s_SkinListScrollCooldownFrames > 0;
	TeeBudgetInput.m_JumpScrollActive = false;
	TeeBudgetInput.m_PostScrollRecoveryFrames = s_SkinListPostScrollRecoveryFrames;
	TeeBudgetInput.m_BackgroundBacklog = (int)vSkinList.size();
	TeeBudgetInput.m_WindowActive = true;
	const SSettingsAdaptiveBudgetOutput TeeSettingsFrameBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, "tee", TeeBudgetInput);
	int VisibleVisualReadyCount = 0;
	int VisibleSourceSettledCount = 0;
	int VisibleBackgroundRequestedCount = 0;
	int VisibleNonTerminalWaitingCount = 0;
	int TotalSourceSettledCount = gs_TeeSettingsPageState.m_FullListSettledCount;
	SResourcePreviewTelemetry TeePreviewTelemetry;
	const int TeeTextureUploadTokens = TeeSettingsFrameBudget.m_TextureUploadTokens;
	(void)TeeTextureUploadTokens;
	const bool NeedFullListSourceState = g_Config.m_QmSettingsPrewarm != 0;
	const SQmRecentTeeSkin SelectedAppearance = QmCurrentTeeSkin(g_Config, m_Dummy);
	const bool NeedSelectedIndexScan = gs_TeeSettingsPageState.m_SelectedIndexRevision != ListRevision ||
					   gs_TeeSettingsPageState.m_SelectedCollection != gs_TeeSkinCollection.m_Collection ||
					   !(gs_TeeSettingsPageState.m_SelectedAppearance == SelectedAppearance);
	const auto PrescanStartTime = time_get_nanoseconds();
	int PrescanItemsScanned = 0;
	if(NeedSelectedIndexScan)
	{
		gs_TeeSettingsPageState.m_SelectedIndex = -1;
		const size_t ScanCount = AllSkinsCollection ? (size_t)SkinList.ItemCount() : vSkinList.size();
		for(size_t i = 0; i < ScanCount; ++i)
		{
			const char *pName = nullptr;
			std::optional<SSettingsSkinListColorKey> Color;
			if(AllSkinsCollection)
			{
				const SSettingsSkinListEntry *pPlannedEntry = SkinList.PlannedEntry((int)i);
				if(pPlannedEntry == nullptr)
					continue;
				pName = pPlannedEntry->m_Name.c_str();
				Color = pPlannedEntry->m_ColorKey;
			}
			else
			{
				const CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
				if(SkinListEntry.SkinContainer() == nullptr)
					continue;
				pName = SkinListEntry.SkinContainer()->Name();
				const auto &ColorKey = SkinListEntry.ColorKey();
				if(ColorKey.has_value())
					Color = SSettingsSkinListColorKey{ColorKey->m_UseCustomColor, ColorKey->m_ColorBody, ColorKey->m_ColorFeet};
			}
			if(SettingsTeeSkinEntryMatches(pName, Color, SelectedAppearance))
			{
				if(Color.has_value() || gs_TeeSettingsPageState.m_SelectedIndex < 0)
					gs_TeeSettingsPageState.m_SelectedIndex = (int)i;
				if(Color.has_value())
					break;
			}
		}
		gs_TeeSettingsPageState.m_SelectedIndexRevision = ListRevision;
		gs_TeeSettingsPageState.m_SelectedCollection = gs_TeeSkinCollection.m_Collection;
		gs_TeeSettingsPageState.m_SelectedAppearance = SelectedAppearance;
	}
	const int PinnedIndex = gs_TeeSkinCollection.m_Collection == ETeeSkinCollection::RECENT ? -1 : gs_TeeSettingsPageState.m_SelectedIndex;
	const auto SwapFirst = [PinnedIndex](int Index) { return SettingsTeeSkinSwapFirst(Index, PinnedIndex); };
	const int OldSelected = SwapFirst(gs_TeeSettingsPageState.m_SelectedIndex);
	const uint64_t MaterializedRevision = SkinList.MaterializedRevision();
	const bool NeedFullListSettledScan = NeedFullListSourceState && gs_TeeSettingsPageState.m_FullListSettledRevision != MaterializedRevision;
	if(NeedFullListSettledScan)
	{
		TotalSourceSettledCount = 0;
		for(const CSkins::CSkinListEntry &SkinListEntry : vSkinList)
		{
			const CSkins::CSkinContainer *pSkinContainer = SkinListEntry.SkinContainer();
			if(pSkinContainer == nullptr)
				continue;
			++PrescanItemsScanned;
			const auto State = pSkinContainer->State();
			const bool SourceReady = pSkinContainer->Skin() != nullptr;
			const bool TerminalFailure = State == CSkins::CSkinContainer::EState::ERROR || State == CSkins::CSkinContainer::EState::NOT_FOUND;
			if(SettingsSkinListEntrySourceSettled(SourceReady, TerminalFailure))
				++TotalSourceSettledCount;
		}
		gs_TeeSettingsPageState.m_FullListSettledRevision = MaterializedRevision;
		gs_TeeSettingsPageState.m_FullListSettledCount = TotalSourceSettledCount;
	}
	if(PerfDebugEnabled() && (NeedSelectedIndexScan || NeedFullListSettledScan))
	{
		const double PrescanDurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - PrescanStartTime).count();
		if(QmPerfShouldLogDuration(PrescanDurationMs, false))
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload),
				"event=tee_skin_list_prescan items_total=%d items_scanned=%d selected_scan=%d ready_scan=%d dur_ms=%.3f full_list_ready=%d source_settled_count=%d",
				(int)vSkinList.size(), PrescanItemsScanned, NeedSelectedIndexScan ? 1 : 0, NeedFullListSettledScan ? 1 : 0, PrescanDurationMs,
				NeedFullListSourceState && SkinList.FullIndexMerged() && !vSkinList.empty() && TotalSourceSettledCount == (int)vSkinList.size() ? 1 : 0,
				TotalSourceSettledCount);
			QmPerfLogPayload("perf/settings-skin-source", aPayload, Client(), "settings:tee");
		}
	}
	const auto ListFrameStartTime = time_get_nanoseconds();
	// 行高、容器内边距与容器高度和卡片测高同源（同一个 helper），所以两档内边距只吃
	// 卡片变高的那一段，可见行数仍是 SETTINGS_TEE_SKIN_LIST_VISIBLE_ROWS 行。
	const SSettingsTeeSkinListGeometry SkinListGeometry = ResolveSettingsTeeSkinListGeometry(TeeMetrics);
	const float TeeSkinListRowHeight = SkinListGeometry.m_RowHeight;
	// 列数仍按容器整宽推导（与改前完全一致）：内边距只让每格窄一点，不会多切一列，
	// 行数因此也不会跟着变。
	const int TeeSkinListItemsPerRow = ResolveSettingsTeeSkinColumns(MainView.w, UiScale);
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
	// 皮肤行高包含预览图，一格比普通设置行高得多；默认「每格两行」的滚轮步长
	// 一下就滚过大半屏，减到一格一行又偏慢。0.7 ≈ 1.4 行/格，落在两者之间。
	// 只作用于这一个列表的滚轮位移，键盘 PageUp/Down 与其它列表速度不变；
	// 全局 qm_ui_scroll_step 仍作为等比基准生效。
	s_ListBox.SetWheelStepScale(0.7f);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
	// 网格不是胶囊行，组件不会替它接管内边距，所以在调用点显式给同一份 token：
	// 选中/悬浮填充不再压在容器圆角上（实机反馈「背景超出列表」）。
	s_ListBox.SetContainerPadding(SkinListGeometry.m_ContainerPadding);
	// 选中与悬浮反馈统一取主题强调色，与语言/主题列表、资源网格同一口径：白 8%/14%
	// 的旧 token 不再参与行反馈。
	const ColorRGBA SkinListRowFeedbackColor = Ui()->QmControlTheme().m_Selected;
	s_ListBox.SetItemColors(SkinListRowFeedbackColor, SkinListRowFeedbackColor, SkinListRowFeedbackColor);
	// 格子之间要有缝：网格行距是 0（行高已含卡片间距），相邻两格的选中/悬浮填充会连成
	// 一整条。反馈矩形按 token 内缩一档，横向与纵向都留出可见缝隙（实机反馈：
	// 「两两之间的悬浮态背景之间要有间距」）。
	s_ListBox.SetItemSelectionInset(ui_token::spacing::XS);
	s_ListBox.DoStart(TeeSkinListRowHeight, vSkinList.size(), TeeSkinListItemsPerRow, 2, OldSelected, &MainView);
	const bool InteractiveReflow = !Ui()->AnimationsBlocked() && !Ui()->MeasurementOnly() && !m_MenuTextPlanCollecting;
	const bool ReflowEnabled = g_Config.m_QmUiMotionLevel > 0;
	const auto PaletteNow = time_get_nanoseconds();
	auto &PaletteTransition = gs_TeeSettingsPageState.m_ListPaletteTransition;
	if(InteractiveReflow)
	{
		// 基础条目共用当前角色配色，只存一份过渡，不为整张目录缓存 Tee 纹理。
		CTeeRenderInfo Palette;
		Palette.Apply(pDefaultSkin);
		Palette.ApplyColors(*pUseCustomColor != 0, *pColorBody, *pColorFeet);
		PaletteTransition.Update("default", Palette, PaletteNow);
	}
	if(InteractiveReflow)
		gs_TeeSkinListState.BeginReflow(Ui()->PopupSourceFrame(), GameClient()->UiRuntimeV2()->FrameDt(), TeeSkinListItemsPerRow, gs_TeeSkinCollection.m_Collection, ReflowEnabled);
	const float ColumnWidth = s_ListBox.ClipRect().w / TeeSkinListItemsPerRow;
	if(m_SkinListScrollToSelected && OldSelected >= 0)
	{
		s_ListBox.ScrollToSelected();
		m_SkinListScrollToSelected = false;
	}
	const SSettingsSkinListVisibleRange VisibleRange = SettingsSkinListVisibleRangeForScroll(
		s_ListBox.ScrollOffsetY(),
		s_ListBox.ViewHeight(),
		TeeSkinListRowHeight,
		TeeSkinListItemsPerRow,
		(int)vSkinList.size(),
		ReflowEnabled ? CQmSlotReflow::MAX_ROW_DELTA : 1);
	if(AllSkinsCollection)
	{
		// 先按实际列数补齐首屏 + 预取，再把当前滚动窗口里的空槽按计划名字物化；
		// 两个范围都只改对应槽位，不移动已有条目，所以滚动条和列表 ID 不跳。
		const int FirstPageWarmupEntries = SettingsTeeSkinListFirstPageWarmupEntries(SkinListGeometry.m_Height, TeeSkinListItemsPerRow);
		GameClient()->m_Skins.MaterializeSkinListRange(0, minimum(SkinList.ItemCount(), FirstPageWarmupEntries), false);
	}
	int RowsIterated = 0;
	int RowsRendered = 0;
	int DoubleClickIndex = -1;
	ETeeSkinApplyTarget DoubleClickTarget = ETeeSkinApplyTarget::MAIN;
	const bool ShowSkinMetadata = g_Config.m_QmSkinShowMetadata != 0;
	// 并排动作图标按**墨迹高**归一：同字号下方框字形只占 em 的 0.750、星形占 0.844，
	// 各自沿用字号就会一高一矮（实机反馈：加号与收藏星高度不齐）。这里先取统一墨迹高，
	// 再按各自字形反算 em 字号；图标盒与格间缝仍按盒统一，所以格宽和间距不变。
	const float ActionInkHeight = ui_token::control::ActionIconInkHeight(BodySize);
	const float ActionQueueFontSize = QmIconInkFontSize(EQmIcon::SQUARE_PLUS, ActionInkHeight);
	const float ActionFavoriteFontSize = QmIconInkFontSize(EQmIcon::STAR, ActionInkHeight);
	auto DoButtonSkinQueue = [&](const void *pButtonId, const void *pParentId, bool InQueue, bool Disabled, const CUIRect *pRect) {
		if(InQueue || (pParentId != nullptr && Ui()->HotItem() == pParentId) || Ui()->HotItem() == pButtonId)
		{
			const float Alpha = Ui()->HotItem() == pButtonId ? 0.2f : 0.0f;
			ColorRGBA Color = InQueue ? ColorRGBA(0.2f, 0.8f, 0.4f, 0.8f + Alpha) : ColorRGBA(0.5f, 0.5f, 0.5f, 0.8f + Alpha);
			if(Disabled && !InQueue)
			{
				Color = ColorRGBA(0.9f, 0.3f, 0.3f, 0.6f + Alpha);
			}
			// 排队 ⇄ 退出排队：加/减方框形变（外框常量、竖臂压平），状态色随点击即时切换。
			IUiContext QueueIconCtx = ui_widget::ControlContext(Ui());
			ui_widget::SIconSwapBinding QueueBinding = ui_widget::ResolveIconSwapBinding(
				QueueIconCtx, EQmIconSwapPair::SQUARE_PLUS_MINUS,
				EQmIcon::SQUARE_PLUS, FONT_ICON_SQUARE_PLUS,
				EQmIcon::SQUARE_MINUS, FONT_ICON_SQUARE_MINUS,
				InQueue ? 1.0f : 0.0f, str_quickhash("skin-queue-icon"), pButtonId, 0.22f);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			TextRender()->TextColor(Color);
			SLabelProperties Props;
			Props.m_MaxWidth = pRect->w;
			// 排队按钮的图标与行文字同号：图标盒按「统一墨迹高」反推出的 em 字号换算。
			ui_widget::DrawIconSwapBinding(QueueIconCtx, QmIconLabelRect(*pRect, ui_token::control::IconBox(ActionQueueFontSize), TEXTALIGN_MC), Color, &QueueBinding);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		const bool Clicked = Ui()->DoButtonLogic(pButtonId, 0, pRect, BUTTONFLAG_LEFT);
		return Clicked && !Disabled;
	};
	if(VisibleRange.m_FirstItem > 0)
		s_ListBox.SkipItems(VisibleRange.m_FirstItem);
	for(int Slot = VisibleRange.m_FirstItem; Slot < VisibleRange.m_EndItem; ++Slot)
	{
		const bool RowStart = s_ListBox.ItemIndex() % s_ListBox.ItemsPerRow() == 0;
		if(RowStart)
			++RowsIterated;

		const int i = SwapFirst(Slot);
		if(AllSkinsCollection)
			GameClient()->m_Skins.MaterializeSkinListEntry(i);
		const SSettingsSkinListEntry *pPlannedEntry = AllSkinsCollection ? SkinList.PlannedEntry(i) : nullptr;
		CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
		const CSkins::CSkinContainer *pSkinContainer = SkinListEntry.SkinContainer();
		const char *pRowName = pPlannedEntry != nullptr ? pPlannedEntry->m_Name.c_str() : (pSkinContainer != nullptr ? pSkinContainer->Name() : nullptr);
		if(pRowName == nullptr)
		{
			s_ListBox.SkipItems(1);
			continue;
		}

		std::optional<SSettingsSkinListColorKey> RowColor;
		if(pPlannedEntry != nullptr && pPlannedEntry->m_ColorKey.has_value())
			RowColor = SSettingsSkinListColorKey{pPlannedEntry->m_ColorKey->m_UseCustomColor, pPlannedEntry->m_ColorKey->m_ColorBody, pPlannedEntry->m_ColorKey->m_ColorFeet};
		else if(pSkinContainer != nullptr)
		{
			const auto &EntryColorKey = SkinListEntry.ColorKey();
			if(EntryColorKey.has_value())
				RowColor = SSettingsSkinListColorKey{EntryColorKey->m_UseCustomColor, EntryColorKey->m_ColorBody, EntryColorKey->m_ColorFeet};
		}
		CSettingsTeeSkinListState::SIds &RowIds = gs_TeeSkinListState.Resolve(pRowName, RowColor);
		const bool ItemActivatedBefore = s_ListBox.WasItemActivated();
		const vec2 Offset = gs_TeeSkinListState.RowOffset(RowIds, Slot, ColumnWidth, TeeSkinListRowHeight, InteractiveReflow);
		const CListboxItem Item = s_ListBox.DoNextItem(&RowIds.m_ListItem, OldSelected == Slot, -1.0f, ReflowEnabled ? Offset : vec2(0.0f, 0.0f));
		if(!Item.m_Visible)
			continue;
		if(!ItemActivatedBefore && s_ListBox.WasItemActivated() && Ui()->LastMouseButton(0) && !Ui()->MouseButton(0))
		{
			DoubleClickIndex = (int)i;
			DoubleClickTarget = ETeeSkinApplyTarget::MAIN;
		}
		// 列表框已处理左键；右键使用独立标识，避免再次消费条目的按钮状态。
		if(Ui()->MouseButtonClicked(1) && !Ui()->IsPopupOpen() &&
			Ui()->HotItem() == &RowIds.m_ListItem && Ui()->MouseHovered(&Item.m_Rect) &&
			Ui()->DoDoubleClickLogic(&RowIds.m_RightDoubleClick))
		{
			DoubleClickIndex = (int)i;
			DoubleClickTarget = ETeeSkinApplyTarget::DUMMY;
		}
		if(RowStart)
			++RowsRendered;

		vVisibleSkinIndices.push_back(Slot);
		if(CSettingsSkinLoadViewport::RowVisible(Ui()->MeasurementOnly(), Item.m_Rect, Ui()->IsClipped() ? Ui()->ClipArea() : nullptr))
			vCurrentVisibleSkinNames.emplace_back(pRowName);

		// 皮肤格是列表卡片的「左右分栏」形态：左侧 60 宽 tee 预览、右侧名称与动作图标。
		// 几何由 qm_list_card 统一切分（与资源卡、工坊预设卡同一条路径）。
		qm_list_card::SMetrics SkinCardFrame;
		SkinCardFrame.m_UiScale = 1.0f;
		SkinCardFrame.m_Padding = 0.0f;
		SkinCardFrame.m_Radius = 0.0f;
		qm_list_card::SOptions SkinCardOptions;
		SkinCardOptions.m_Axis = qm_list_card::EAxis::ROW;
		SkinCardOptions.m_LeadingWidth = 60.0f;
		SkinCardOptions.m_RowGap = 0.0f;
		const qm_list_card::SLayout SkinCardLayout = qm_list_card::ResolveLayout(Item.m_Rect, SkinCardFrame.Scaled(), SkinCardOptions);
		Button = SkinCardLayout.m_Leading;
		Label = SkinCardLayout.m_Body;

		if(pSkinContainer == nullptr)
		{
			// 索引尚未物化时仍保留固定格子与滚动范围，预览位显示默认 Tee。
			++TeePreviewTelemetry.m_PlaceholderCount;
			++VisibleNonTerminalWaitingCount;
			RenderDefaultSkinPlaceholder(Button);
			RenderSkinStatus(Item.m_Rect, ESkinStatus::LOADING, &RowIds.m_ErrorTooltip);
			SLabelProperties Props;
			Props.m_MaxWidth = maximum(0.0f, Label.w - 5.0f);
			Props.m_DisallowNewline = true;
			Props.m_EllipsisAtEnd = true;
			Props.m_MinimumFontSize = BodySize * 0.8f;
			Props.m_FontSizeStep = 0.5f;
			if(pPlannedEntry != nullptr && pPlannedEntry->m_NameMatchStart >= 0 && pPlannedEntry->m_NameMatchLength > 0)
				Props.m_vColorSplits.emplace_back(pPlannedEntry->m_NameMatchStart, pPlannedEntry->m_NameMatchLength, ui_token::color::SEARCH_MATCH_ON_DARK);
			TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&Label, pRowName, BodySize, TEXTALIGN_ML, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			continue;
		}

		const auto State = pSkinContainer->State();
		const auto &EntryColorKey = SkinListEntry.ColorKey();
		const bool EntryUseCustomColor = EntryColorKey.has_value() ? EntryColorKey->m_UseCustomColor : *pUseCustomColor != 0;
		const int EntryColorBody = EntryColorKey.has_value() ? EntryColorKey->m_ColorBody : (int)*pColorBody;
		const int EntryColorFeet = EntryColorKey.has_value() ? EntryColorKey->m_ColorFeet : (int)*pColorFeet;
		const bool SourceReady = pSkinContainer->Skin() != nullptr;
		const bool TerminalFailure = State == CSkins::CSkinContainer::EState::ERROR || State == CSkins::CSkinContainer::EState::NOT_FOUND;
		const bool EntryVisualReady = SettingsSkinListEntryVisualReady(SourceReady, TerminalFailure, false);
		const bool EntrySourceSettled = SettingsSkinListEntrySourceSettled(SourceReady, TerminalFailure);
		SResourcePreviewState TeeResourcePreviewState;
		TeeResourcePreviewState.m_TextureReady = EntryVisualReady;
		TeeResourcePreviewState.m_Failed = TerminalFailure;
		const ESettingsResourcePreviewDrawResult TeePreviewDrawResult = SettingsResourcePreviewDrawResult(TeeResourcePreviewState);
		const ESkinStatus StatusIndicator = SourceReady ? ESkinStatus::NONE : CSkins::CSkinContainer::StatusIndicator(State);
		if(EntryVisualReady)
		{
			++VisibleVisualReadyCount;
			++TeePreviewTelemetry.m_ReadyTextureCount;
		}
		else
		{
			++TeePreviewTelemetry.m_PlaceholderCount;
			if(TeePreviewDrawResult == ESettingsResourcePreviewDrawResult::PLACEHOLDER)
				++TeePreviewTelemetry.m_PreviewAdmissions;
		}
		if(EntrySourceSettled)
			++VisibleSourceSettledCount;
		if(State == CSkins::CSkinContainer::EState::BACKGROUND_REQUESTED)
			++VisibleBackgroundRequestedCount;
		if(StatusIndicator == ESkinStatus::LOADING)
			++VisibleNonTerminalWaitingCount;
		const CSkin *pSkin = pSkinContainer->Skin() != nullptr ? pSkinContainer->Skin().get() : pDefaultSkin;

		if(EntryVisualReady && !Ui()->DrawingSuppressed())
		{
			CTeeRenderInfo Info;
			Info.Apply(pSkin);
			Info.ApplyColors(EntryUseCustomColor, EntryColorBody, EntryColorFeet);
			Info.m_Size = 50.0f;
			float PreviewMinX, PreviewMinY, PreviewMaxX, PreviewMaxY;
			GetSettingsTeePreviewBounds(CAnimState::GetIdle(), Info, PreviewMinX, PreviewMinY, PreviewMaxX, PreviewMaxY);
			Info.m_Size = SettingsSkinPreviewSize(Item.m_Rect.h, Button.w, 50.0f, PreviewMaxX - PreviewMinX, PreviewMaxY - PreviewMinY);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const float PreviewScale = Info.m_Size / 50.0f;
			const float PreviewCenterOffsetX = SettingsSkinPreviewCenterOffset(PreviewMinX, PreviewMaxX) * PreviewScale;
			CUIRect TeeClip = Button;
			TeeClip.Margin(3.0f, &TeeClip);
			const vec2 TeeRenderPos = vec2(TeeClip.x + TeeClip.w / 2.0f + PreviewCenterOffsetX, TeeClip.y + TeeClip.h / 2.0f + OffsetToMid.y);
			std::optional<CTeeRenderInfo> Previous;
			const CTeeRenderInfo *pPreviousPalette = EntryColorKey.has_value() ? nullptr : PaletteTransition.PreviousInfo(PaletteNow);
			if(pPreviousPalette != nullptr)
			{
				Previous = Info;
				Previous->m_CustomColoredSkin = pPreviousPalette->m_CustomColoredSkin;
				Previous->m_ColorBody = pPreviousPalette->m_ColorBody;
				Previous->m_ColorFeet = pPreviousPalette->m_ColorFeet;
			}
			Ui()->ClipEnable(&TeeClip);
			RenderTools()->RenderTeeWithSkinChangeTransition(CAnimState::GetIdle(), Previous ? &*Previous : nullptr, &Info, *pEmote, vec2(1.0f, 0.0f), TeeRenderPos, PaletteTransition.Progress(PaletteNow));
			Ui()->ClipDisable();
		}
		else if(!EntryVisualReady)
		{
			// 目标皮肤尚未就绪时显示默认 Tee，不铺骨架底色。
			RenderDefaultSkinPlaceholder(Button);
		}
		// 右上角功能图标条：只按本帧真的会画的动作从右到左排（隐藏的动作不占位），
		// 出现/消失走同一条缓动；下面的名称/元数据**跟着这条缓动**收缩可用宽度，
		// 字号随之连续下滑——这是悬浮动画的一部分，不是要消掉的抖动。
		// （公共约定的消费方式见 QmUi/UiIconActionStrip.h）
		const int64_t ActionNow = time_get();
		const int64_t ActionFreq = maximum<int64_t>(1, time_freq());
		const float ActionEaseSeconds = ui_token::motion::HOVER_FADE.m_DurationSec;
		const bool ActionMotion = g_Config.m_QmUiMotionLevel > 0;
		const bool ActionInteractive = !Ui()->MeasurementOnly() && !Ui()->RenderOnly();
		const float ActionIconSize = ui_token::control::IconBox(ActionQueueFontSize);
		const float ActionGap = ui_token::spacing::XS * 0.5f;
		const bool InSkinQueue = GameClient()->m_Skins.IsInSkinQueue(pSkinContainer->Name(), EntryUseCustomColor, EntryColorBody, EntryColorFeet, QueueDummy);
		// 队列按钮只在「已在队列 / 本行悬浮 / 按钮自身悬浮」时存在；不存在就不占位。
		const bool QueueActionPresent = InSkinQueue || Ui()->HotItem() == &RowIds.m_ListItem || Ui()->HotItem() == &RowIds.m_Queue;
		RowIds.m_ActionStrip.Update(ActionNow, ActionFreq, ActionMotion, (QueueActionPresent ? 1u : 0u) | (1u << 1), ActionEaseSeconds, ActionInteractive);
		// 图标条贴着「悬浮背景」的内缘排：列表把反馈矩形按 spacing::XS 内缩，图标若仍按
		// 整格右缘对齐就会露到背景外面（实机反馈：收藏星溢出圆角背景）。两处用同一档 token。
		const float ActionInset = ui_token::spacing::XS;
		CUIRect IconRow = Item.m_Rect;
		IconRow.x += ActionInset;
		IconRow.y += ActionInset;
		IconRow.w = maximum(0.0f, IconRow.w - 2.0f * ActionInset);
		IconRow.h = maximum(0.0f, IconRow.h - 2.0f * ActionInset);
		IconRow.HSplitTop(std::min(IconRow.h, 20.0f), &IconRow, nullptr);
		CUIRect aActionRects[2];
		RowIds.m_ActionStrip.Layout(IconRow, ActionIconSize, ActionGap, ActionNow, ActionFreq, ActionEaseSeconds, aActionRects);
		// 文本可用宽度跟着图标条缓动：图标每帧多占一点，字号就每帧小一点。
		const float ActionReserve = RowIds.m_ActionStrip.ReserveWidth(ActionIconSize, ActionGap, ActionNow, ActionFreq, ActionEaseSeconds);
		{
			CUIRect LabelContent = Label;
			if(EntryColorKey.has_value())
			{
				CUIRect Swatches, BodySwatch, FeetSwatch;
				LabelContent.VSplitLeft(20.0f, &Swatches, &LabelContent);
				Swatches.HMargin((Swatches.h - 16.0f) / 2.0f, &Swatches);
				Swatches.VSplitLeft(8.0f, &BodySwatch, &Swatches);
				Swatches.VSplitLeft(2.0f, nullptr, &Swatches);
				Swatches.VSplitLeft(8.0f, &FeetSwatch, nullptr);
				const ColorRGBA BodyColor = EntryUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(EntryColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f);
				const ColorRGBA FeetColor = EntryUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(EntryColorFeet).UnclampLighting(ColorHSLA::DARKEST_LGT)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f);
				BodySwatch.Draw(BodyColor, IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
				FeetSwatch.Draw(FeetColor, IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
			}
			SLabelProperties Props;
			// 可用宽度 = 行宽 − 图标条当前占位 − 文本缝；名称既不许换行也不许钻到图标下面。
			// 允许缩字号（悬浮收缩的一部分），但档位细到 0.5：宽度逐帧变化时字号连续下滑，
			// 不再在整数档之间一格一格跳。
			Props.m_MaxWidth = maximum(0.0f, LabelContent.w - ActionReserve - 5.0f);
			Props.m_DisallowNewline = true;
			Props.m_EllipsisAtEnd = true;
			Props.m_MinimumFontSize = BodySize * 0.8f;
			Props.m_FontSizeStep = 0.5f;
			const auto &NameMatch = SkinListEntry.NameMatch();
			if(NameMatch.has_value())
			{
				const auto [MatchStart, MatchLength] = NameMatch.value();
				Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ui_token::color::SEARCH_MATCH_ON_DARK);
			}
			char aSkinMetadata[96] = "";
			const int OfficialReleaseDate = pSkinContainer->OfficialReleaseDate();
			const char *pOfficialCreator = pSkinContainer->OfficialCreator();
			if(ShowSkinMetadata && (OfficialReleaseDate > 0 || pOfficialCreator[0] != '\0'))
			{
				char aDate[16] = "";
				if(OfficialReleaseDate > 0)
				{
					str_format(aDate, sizeof(aDate), "%04d-%02d-%02d", OfficialReleaseDate / 10000, (OfficialReleaseDate / 100) % 100, OfficialReleaseDate % 100);
				}
				if(aDate[0] != '\0' && pOfficialCreator[0] != '\0')
					str_format(aSkinMetadata, sizeof(aSkinMetadata), "%s - %s", aDate, pOfficialCreator);
				else if(aDate[0] != '\0')
					str_copy(aSkinMetadata, aDate, sizeof(aSkinMetadata));
				else
					str_copy(aSkinMetadata, pOfficialCreator, sizeof(aSkinMetadata));
			}
			if(aSkinMetadata[0] != '\0')
			{
				CUIRect NameLine, MetadataLine;
				LabelContent.HSplitTop(25.0f, &NameLine, &MetadataLine);
				Ui()->DoLabel(&NameLine, pSkinContainer->Name(), BodySize, TEXTALIGN_ML, Props);
				MetadataLine.HSplitTop(16.0f, &MetadataLine, nullptr);
				SLabelProperties MetadataProps;
				// 图标条只占第一行（与皮肤名同行），元数据行在它下方，不能跟着减图标占位：
				// 悬浮时队列按钮出现会让 Reserve 变大，作者名就凭空被截掉，右侧明明还空着。
				MetadataProps.m_MaxWidth = maximum(0.0f, MetadataLine.w - 5.0f);
				MetadataProps.m_DisallowNewline = true;
				MetadataProps.m_EllipsisAtEnd = true;
				// 作者较长时逐步缩小字号，仍放不下才显示省略号。
				MetadataProps.m_MinimumFontSize = TeeMetrics.m_SmallSize * 0.8f;
				MetadataProps.m_FontSizeStep = 0.5f;
				TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, 0.8f));
				Ui()->DoLabel(&MetadataLine, aSkinMetadata, TeeMetrics.m_SmallSize, TEXTALIGN_ML, MetadataProps);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else
			{
				Ui()->DoLabel(&LabelContent, pSkinContainer->Name(), BodySize, TEXTALIGN_ML, Props);
			}
		}

		if(g_Config.m_Debug && EntryVisualReady && !Ui()->DrawingSuppressed())
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(EntryUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(EntryColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT)) : pSkin->m_BloodColor);
			IGraphics::CQuadItem QuadItem(Label.x, Label.y, 12.0f, 12.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// 队列 / 收藏动作：矩形由上面的图标条按「实际存在的动作」给出；存在度为 0 时
		// 不画也不接点击（不存在就不占位，也不会留下空槽）。
		{
			const float QueueActionAlpha = RowIds.m_ActionStrip.Presence(0, ActionNow, ActionFreq, ActionEaseSeconds);
			if(QueueActionAlpha > 0.01f)
			{
				CUiScopedControlAlphaScale QueueActionFade(Ui(), QueueActionAlpha);
				if(DoButtonSkinQueue(&RowIds.m_Queue, &RowIds.m_ListItem, InSkinQueue, false, &aActionRects[0]))
				{
					if(InSkinQueue)
					{
						GameClient()->m_Skins.RemoveActiveSkinQueue(pSkinContainer->Name(), EntryUseCustomColor, EntryColorBody, EntryColorFeet, QueueDummy);
					}
					else
					{
						GameClient()->m_Skins.AddActiveSkinQueue(pSkinContainer->Name(), EntryUseCustomColor, EntryColorBody, EntryColorFeet, QueueDummy);
					}
				}
				const char *pQueueTooltip = InSkinQueue ? Localize("Remove from queue") : Localize("Add to queue");
				GameClient()->m_Tooltips.DoToolTip(&RowIds.m_Queue, &aActionRects[0], pQueueTooltip);
			}
			const float FavoriteActionAlpha = RowIds.m_ActionStrip.Presence(1, ActionNow, ActionFreq, ActionEaseSeconds);
			if(FavoriteActionAlpha > 0.01f)
			{
				CUiScopedControlAlphaScale FavoriteActionFade(Ui(), FavoriteActionAlpha);
				if(DoButton_Favorite(&RowIds.m_Favorite, &RowIds.m_ListItem, SkinListEntry.IsFavorite(), &aActionRects[1], ActionFavoriteFontSize))
				{
					if(SkinListEntry.IsFavorite())
					{
						GameClient()->m_Skins.RemoveFavorite(pSkinContainer->Name());
					}
					else
					{
						GameClient()->m_Skins.AddFavorite(pSkinContainer->Name());
					}
				}
			}
		}

		RenderSkinStatus(Item.m_Rect, StatusIndicator, &RowIds.m_ErrorTooltip);
	}
	const int TailItems = (int)vSkinList.size() - VisibleRange.m_EndItem;
	if(TailItems > 0)
		s_ListBox.SkipItems(TailItems);
	GameClient()->m_Skins.SetSettingsVisibleSkinLoads(std::move(vCurrentVisibleSkinNames), Ui()->MeasurementOnly());
	for(auto It = vVisibleSkinIndices.rbegin(); It != vVisibleSkinIndices.rend(); ++It)
	{
		if(!Ui()->MeasurementOnly())
		{
			const int SourceIndex = SwapFirst((int)*It);
			if(AllSkinsCollection)
				GameClient()->m_Skins.RequestSkinListLoad(SourceIndex, ESettingsResourcePriority::VISIBLE);
			else if(vSkinList[SourceIndex].SkinContainer() != nullptr)
				vSkinList[SourceIndex].RequestLoad(ESettingsResourcePriority::VISIBLE);
		}
	}
	const bool SkinListScrollInteraction = m_SettingsScrollActive || s_ListBox.ScrollbarActive() || s_ListBox.ScrollbarAnimating() || s_SkinListScrollActiveLastFrame;
	const int PreviousSkinListScrollCooldownFrames = s_SkinListScrollCooldownFrames;
	s_SkinListScrollCooldownFrames = SettingsScrollInteractionCooldown(SkinListScrollInteraction, s_SkinListScrollCooldownFrames, 3);
	s_SkinListPostScrollRecoveryFrames = SettingsScrollInteractionRecovery(
		SkinListScrollInteraction, PreviousSkinListScrollCooldownFrames, s_SkinListScrollCooldownFrames, s_SkinListPostScrollRecoveryFrames, 2);
	m_SettingsPostScrollRecoveryFrames = s_SkinListPostScrollRecoveryFrames;
	const bool RequestWindowScrollBlocked = SkinListScrollInteraction || s_SkinListScrollCooldownFrames > 0;
	SSettingsResourceFrameContext FrameContext = SettingsBuildFrameContext(RequestWindowScrollBlocked, false, s_SkinListPostScrollRecoveryFrames);
	const bool VisibleSourceSettled = VisibleSourceSettledCount == (int)vVisibleSkinIndices.size();
	m_SettingsHighPrioritySettled = VisibleSourceSettled;
	FrameContext.m_HighPrioritySettled = VisibleSourceSettled;
	const auto &Throughput = GameClient()->m_Skins.SettingsThroughputControllerOutput();
	const bool BackgroundDrainActive = Throughput.m_BackgroundDrainActive;
	const int CountFuseLimit = Throughput.m_CountFuseLimit;
	const auto AdmissionTelemetry = GameClient()->m_Skins.SettingsSourceAdmissionTelemetry();
	const auto SkinStatsBeforeBackgroundRequest = GameClient()->m_Skins.LoadingStats();
	const int DefaultBackgroundRequestBudget = Throughput.m_BackgroundRequestBudget;
	const int RecentLoadedDelta = gs_TeeListDrainPerfSession.m_Active ? (int)(GameClient()->m_Skins.SettingsSourceLoadsCompleted() - gs_TeeListDrainPerfSession.m_LastLoads) : 0;
	const auto BackgroundBudgetDecision = SettingsSkinBackgroundRequestBudgetDecision({
		DefaultBackgroundRequestBudget,
		(int)SkinStatsBeforeBackgroundRequest.m_NumPending,
		(int)SkinStatsBeforeBackgroundRequest.m_NumLoading,
		(int)SkinStatsBeforeBackgroundRequest.m_NumBackgroundRequested,
		CountFuseLimit,
		Throughput.m_VisibleReserve,
		RecentLoadedDelta,
		AdmissionTelemetry.m_AdmittedDelta,
		BackgroundDrainActive,
	});
	const int BackgroundRequestBudget = BackgroundBudgetDecision.m_RequestBudget;
	gs_TeeSettingsPageState.m_LastRequestBudgetActual = BackgroundRequestBudget;
	gs_TeeSettingsPageState.m_LastRequestBudgetBlockReason = BackgroundBudgetDecision.m_BlockReason;
	int BackgroundRequestsIssued = 0;
	int BackgroundScanItemsScanned = 0;
	int BackgroundScanSkippedVisible = 0;
	const char *pBackgroundScanBlockReason = "none";
	const auto BackgroundScanStartTime = time_get_nanoseconds();
	if(gs_TeeSettingsPageState.m_BackgroundRequestScanRevision != ListRevision)
	{
		gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = false;
		gs_TeeSettingsPageState.m_BackgroundRequestScanRevision = ListRevision;
		s_BackgroundRequestCursor = 0;
	}
	if(!VisibleSourceSettled)
	{
		pBackgroundScanBlockReason = "visible_source_unsettled";
	}
	else if(BackgroundRequestBudget <= 0)
	{
		pBackgroundScanBlockReason = SettingsSkinBackgroundRequestBlockReasonName(BackgroundBudgetDecision.m_BlockReason);
	}
	else if(g_Config.m_QmSettingsPrewarm == 0)
	{
		pBackgroundScanBlockReason = "prewarm_disabled";
	}
	else if(gs_TeeSettingsPageState.m_BackgroundRequestScanComplete)
	{
		pBackgroundScanBlockReason = "scan_complete";
	}
	if(g_Config.m_QmSettingsPrewarm != 0 && VisibleSourceSettled && BackgroundRequestBudget > 0 && !vSkinList.empty() && !gs_TeeSettingsPageState.m_BackgroundRequestScanComplete)
	{
		for(; s_BackgroundRequestCursor < vSkinList.size() && BackgroundScanItemsScanned < (int)TEE_BACKGROUND_SCAN_LIMIT && BackgroundRequestsIssued < BackgroundRequestBudget;)
		{
			const size_t BackgroundIndex = s_BackgroundRequestCursor++;
			++BackgroundScanItemsScanned;
			if(std::binary_search(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), (size_t)SwapFirst((int)BackgroundIndex)))
			{
				++BackgroundScanSkippedVisible;
				continue;
			}

			const CSkins::CSkinContainer *pBackgroundContainer = vSkinList[BackgroundIndex].SkinContainer();
			const SSettingsSkinListEntry *pBackgroundPlannedEntry = AllSkinsCollection ? SkinList.PlannedEntry((int)BackgroundIndex) : nullptr;
			if(pBackgroundContainer == nullptr && pBackgroundPlannedEntry != nullptr)
				pBackgroundContainer = GameClient()->m_Skins.FindContainerOrNullptr(pBackgroundPlannedEntry->m_Name.c_str(), false);
			if(pBackgroundContainer == nullptr || pBackgroundContainer->IsCatalogOnly() || pBackgroundContainer->State() != CSkins::CSkinContainer::EState::UNLOADED)
				continue;

			const_cast<CSkins::CSkinContainer *>(pBackgroundContainer)->RequestLoad(ESettingsResourcePriority::BACKGROUND);
			++BackgroundRequestsIssued;
		}
		if(s_BackgroundRequestCursor >= vSkinList.size())
		{
			gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = true;
			pBackgroundScanBlockReason = "scan_complete";
		}
	}
	if(PerfDebugEnabled())
	{
		const double BackgroundScanDurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - BackgroundScanStartTime).count();
		if(BackgroundScanItemsScanned > 0 || BackgroundRequestsIssued > 0 || gs_TeeSettingsPageState.m_BackgroundRequestScanComplete)
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload),
				"event=tee_skin_background_scan items_total=%d items_scanned=%d items_skipped_visible=%d requests_issued=%d complete=%d budget=%d dur_ms=%.3f block_reason=%s",
				(int)vSkinList.size(), BackgroundScanItemsScanned, BackgroundScanSkippedVisible, BackgroundRequestsIssued,
				gs_TeeSettingsPageState.m_BackgroundRequestScanComplete ? 1 : 0, BackgroundRequestBudget,
				BackgroundScanDurationMs, pBackgroundScanBlockReason);
			QmPerfLogPayload("perf/settings-skin-source", aPayload, Client(), "settings:tee");
		}
		char aPreviewPayload[192];
		str_format(aPreviewPayload, sizeof(aPreviewPayload),
			"event=tee_preview_pipeline page=settings:tee tee_preview_admissions=%d tee_ready_textures=%d tee_placeholders=%d visible_ready_ratio=%.3f",
			TeePreviewTelemetry.m_PreviewAdmissions, TeePreviewTelemetry.m_ReadyTextureCount, TeePreviewTelemetry.m_PlaceholderCount,
			SettingsResourcePreviewVisibleReadyRatio(TeePreviewTelemetry.m_ReadyTextureCount, (int)vVisibleSkinIndices.size()));
		QmPerfLogPayload("perf/settings-skin-source", aPreviewPayload, Client(), "settings:tee");
	}
	const auto SkinStats = GameClient()->m_Skins.LoadingStats();
	CSkins::SSettingsTeeVisibleSnapshot VisibleSnapshot;
	VisibleSnapshot.m_VisibleTotal = (int)vVisibleSkinIndices.size();
	VisibleSnapshot.m_VisibleReady = VisibleVisualReadyCount;
	VisibleSnapshot.m_VisibleWaiting = maximum(0, (int)vVisibleSkinIndices.size() - VisibleVisualReadyCount);
	VisibleSnapshot.m_VisibleBackgroundRequested = VisibleBackgroundRequestedCount;
	VisibleSnapshot.m_VisibleNonterminalWaiting = VisibleNonTerminalWaitingCount;
	str_copy(VisibleSnapshot.m_aRequestBudgetBlockReason,
		SettingsSkinBackgroundRequestBlockReasonName(BackgroundBudgetDecision.m_BlockReason),
		sizeof(VisibleSnapshot.m_aRequestBudgetBlockReason));
	GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);
	const auto VisibleRowName = [&](size_t Index) -> const char * {
		if(Index >= vSkinList.size())
			return "";
		if(AllSkinsCollection)
		{
			const SSettingsSkinListEntry *pPlannedEntry = SkinList.PlannedEntry((int)Index);
			if(pPlannedEntry != nullptr)
				return pPlannedEntry->m_Name.c_str();
		}
		const CSkins::CSkinContainer *pContainer = vSkinList[Index].SkinContainer();
		return pContainer != nullptr ? pContainer->Name() : "";
	};
	const int FirstVisibleIndex = !vVisibleSkinIndices.empty() ? (int)vVisibleSkinIndices.front() : -1;
	const int FirstSourceIndex = SwapFirst(FirstVisibleIndex);
	const char *pFirstVisibleSkin = FirstSourceIndex >= 0 ? VisibleRowName(FirstSourceIndex) : "";
	const int LastVisibleIndex = !vVisibleSkinIndices.empty() ? (int)vVisibleSkinIndices.back() : -1;
	const bool FirstVisibleReady = !vVisibleSkinIndices.empty() &&
				       vSkinList[FirstSourceIndex].SkinContainer() != nullptr &&
				       SettingsSkinListEntryVisualReady(
					       vSkinList[FirstSourceIndex].SkinContainer()->Skin() != nullptr,
					       vSkinList[FirstSourceIndex].SkinContainer()->State() == CSkins::CSkinContainer::EState::ERROR ||
						       vSkinList[FirstSourceIndex].SkinContainer()->State() == CSkins::CSkinContainer::EState::NOT_FOUND,
					       false);
	const bool FullListReady = NeedFullListSourceState && SkinList.PlanKnown() && SkinList.FullIndexMerged() &&
				   !vSkinList.empty() && TotalSourceSettledCount == (int)vSkinList.size();
	const int64_t NowNs = time_get_nanoseconds().count();
	if(!gs_TeeSettingsPageState.m_TeePageActiveLastFrame)
	{
		gs_TeeSettingsPageState.m_TeePageActiveLastFrame = true;
		gs_TeeSettingsPageState.m_TeeEnterStartNs = NowNs;
		BeginTeeListDrainPerfSession(GameClient()->m_Skins, NowNs);
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=tee_enter visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	}
	const bool ClickActive = Input()->KeyIsPressed(KEY_MOUSE_1) != 0;
	if(ClickActive && !gs_TeeSettingsPageState.m_TeeClickActiveLastFrame)
	{
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=click_begin visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	}
	else if(!ClickActive && gs_TeeSettingsPageState.m_TeeClickActiveLastFrame)
	{
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=click_end visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	}
	gs_TeeSettingsPageState.m_TeeClickActiveLastFrame = ClickActive;
	if(SkinListScrollInteraction && !gs_TeeSettingsPageState.m_TeeScrollInteractionLastFrame)
	{
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=scroll_begin visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	}
	else if(!SkinListScrollInteraction && gs_TeeSettingsPageState.m_TeeScrollInteractionLastFrame)
	{
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=scroll_end visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	}
	gs_TeeSettingsPageState.m_TeeScrollInteractionLastFrame = SkinListScrollInteraction;
	if(FirstVisibleReady && !gs_TeeSettingsPageState.m_TeeFirstVisibleReadyLogged)
	{
		gs_TeeSettingsPageState.m_TeeFirstVisibleReadyLogged = true;
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=first_visible_ready dur_ms=%.3f visible_rows=%d first_visible_index=%d first_visible_skin=%s",
			gs_TeeSettingsPageState.m_TeeEnterStartNs > 0 ? (NowNs - gs_TeeSettingsPageState.m_TeeEnterStartNs) / 1000000.0 : 0.0,
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/skin-ux", aPayload, Client(), "settings:tee");
	}
	if(SettingsSkinListShouldLogAllVisibleReady(
		   VisibleSourceSettled,
		   gs_TeeSettingsPageState.m_TeeAllVisibleReadyLogged,
		   (int)vVisibleSkinIndices.size()))
	{
		gs_TeeSettingsPageState.m_TeeAllVisibleReadyLogged = true;
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=all_visible_ready dur_ms=%.3f visible_rows=%d first_visible_index=%d last_visible_index=%d first_visible_skin=%s",
			gs_TeeSettingsPageState.m_TeeEnterStartNs > 0 ? (NowNs - gs_TeeSettingsPageState.m_TeeEnterStartNs) / 1000000.0 : 0.0,
			(int)vVisibleSkinIndices.size(), FirstVisibleIndex, LastVisibleIndex, pFirstVisibleSkin);
		QmPerfLogPayload("perf/skin-ux", aPayload, Client(), "settings:tee");
	}
	if(FullListReady && !gs_TeeSettingsPageState.m_TeeFullListReadyLogged)
	{
		gs_TeeSettingsPageState.m_TeeFullListReadyLogged = true;
		char aPayload[256];
		str_format(aPayload, sizeof(aPayload), "event=full_list_ready dur_ms=%.3f total=%d visible_rows=%d first_visible_skin=%s",
			gs_TeeSettingsPageState.m_TeeEnterStartNs > 0 ? (NowNs - gs_TeeSettingsPageState.m_TeeEnterStartNs) / 1000000.0 : 0.0,
			(int)vSkinList.size(), (int)vVisibleSkinIndices.size(), pFirstVisibleSkin);
		QmPerfLogPayload("perf/skin-ux", aPayload, Client(), "settings:tee");
		LogTeeListDrainSummary(Client(), GameClient()->m_Skins, GameClient()->m_Skins.LoadingStats(), true, NowNs);
		if(gs_TeeSettingsPageState.m_TeeRefreshInProgress)
		{
			char aRefreshPayload[256];
			str_format(aRefreshPayload, sizeof(aRefreshPayload), "event=tee_refresh_end dur_ms=%.3f visible_rows=%d first_visible_skin=%s",
				gs_TeeSettingsPageState.m_TeeRefreshStartNs > 0 ? (NowNs - gs_TeeSettingsPageState.m_TeeRefreshStartNs) / 1000000.0 : 0.0,
				(int)vVisibleSkinIndices.size(), pFirstVisibleSkin);
			QmPerfLogPayload("perf/interaction", aRefreshPayload, Client(), "settings:tee");
			gs_TeeSettingsPageState.m_TeeRefreshInProgress = false;
		}
	}
	if(PerfDebugEnabled() &&
		(BackgroundRequestsIssued > 0 ||
			gs_TeeSettingsPageState.m_LastLoggedVisibleCount != (int)vVisibleSkinIndices.size() ||
			gs_TeeSettingsPageState.m_LastLoggedVisibleReadyCount != VisibleSourceSettledCount ||
			gs_TeeSettingsPageState.m_LastLoggedScrollActive != FrameContext.m_ScrollActive ||
			gs_TeeSettingsPageState.m_LastLoggedRecoveryFrames != FrameContext.m_PostScrollRecoveryFrames ||
			str_comp(gs_TeeSettingsPageState.m_aLastLoggedFirstVisibleSkin, pFirstVisibleSkin) != 0))
	{
		const int GpuUploadLimitUnits = GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame();
		const int GpuUploadRemainingUnits = GameClient()->GpuUploadLimiter()->RemainingUploads();
		const int FinalizeBudgetLimit = Throughput.m_FinalizeBudgetLimit;
		const char *pEffectiveFrameContext = SettingsSkinThroughputControllerModeName(Throughput.m_Mode);
		char aPayload[768];
		str_format(aPayload, sizeof(aPayload),
			"event=request_window visible=%d visible_ready=%d visible_waiting=%d visible_background_requested=%d visible_nonterminal_waiting=%d background_budget=%d background_issued=%d requested=%d idle=%d scroll=%d recovery=%d pending=%zu loading=%zu loaded=%zu total=%d first_visible_index=%d first_visible_skin=%s count_fuse_limit=%d real_inflight=%d visible_reserve=%d request_budget_default=%d request_budget_actual=%d request_budget_block_reason=%s gpu_upload_limit_units=%d gpu_upload_remaining_units=%d finalize_budget_limit=%d effective_frame_context=%s controller_reason=%s frame_time_avg_ms=%.3f render_frame_time_ms=%.3f admission_underfed=%d underfed_streak=%d",
			(int)vVisibleSkinIndices.size(), VisibleSourceSettledCount, maximum(0, (int)vVisibleSkinIndices.size() - VisibleSourceSettledCount), VisibleBackgroundRequestedCount, VisibleNonTerminalWaitingCount, DefaultBackgroundRequestBudget, BackgroundRequestsIssued,
			(int)SkinStats.m_NumBackgroundRequested,
			!FrameContext.m_ScrollActive && FrameContext.m_PostScrollRecoveryFrames == 0 ? 1 : 0,
			FrameContext.m_ScrollActive ? 1 : 0, FrameContext.m_PostScrollRecoveryFrames,
			SkinStats.m_NumPending, SkinStats.m_NumLoading, SkinStats.m_NumLoaded, (int)vSkinList.size(), FirstVisibleIndex, pFirstVisibleSkin,
			CountFuseLimit, AdmissionTelemetry.m_RealInflight, Throughput.m_VisibleReserve, DefaultBackgroundRequestBudget, BackgroundRequestBudget,
			SettingsSkinBackgroundRequestBlockReasonName(BackgroundBudgetDecision.m_BlockReason),
			GpuUploadLimitUnits, GpuUploadRemainingUnits, FinalizeBudgetLimit, pEffectiveFrameContext,
			SettingsSkinThroughputControllerReasonName(Throughput.m_Reason),
			AdmissionTelemetry.m_FrameTimeAverageMs,
			AdmissionTelemetry.m_RenderFrameTimeMs,
			AdmissionTelemetry.m_AdmissionUnderfed ? 1 : 0,
			AdmissionTelemetry.m_UnderfedStreak);
		QmPerfLogPayload("perf/settings-skin-source", aPayload, Client(), "settings:tee");
		gs_TeeSettingsPageState.m_LastLoggedVisibleCount = (int)vVisibleSkinIndices.size();
		gs_TeeSettingsPageState.m_LastLoggedVisibleReadyCount = VisibleSourceSettledCount;
		gs_TeeSettingsPageState.m_LastLoggedScrollActive = FrameContext.m_ScrollActive;
		gs_TeeSettingsPageState.m_LastLoggedRecoveryFrames = FrameContext.m_PostScrollRecoveryFrames;
		str_copy(gs_TeeSettingsPageState.m_aLastLoggedFirstVisibleSkin, pFirstVisibleSkin, sizeof(gs_TeeSettingsPageState.m_aLastLoggedFirstVisibleSkin));
	}
	if(PerfDebugEnabled() && gs_TeeListDrainPerfSession.m_Active)
	{
		const uint64_t UploadsDoneNow = GameClient()->m_Skins.SettingsSourceUploadsCompleted();
		const uint64_t LoadedNow = GameClient()->m_Skins.SettingsSourceLoadsCompleted();
		const uint64_t UploadsDoneDelta = UploadsDoneNow - gs_TeeListDrainPerfSession.m_LastUploads;
		const uint64_t LoadedDelta = LoadedNow - gs_TeeListDrainPerfSession.m_LastLoads;
		const int RequestedDelta = gs_TeeListDrainPerfSession.m_LastRequested >= 0 ? (int)SkinStats.m_NumBackgroundRequested - gs_TeeListDrainPerfSession.m_LastRequested : (int)SkinStats.m_NumBackgroundRequested;
		const auto &Telemetry = GameClient()->m_Skins.SettingsSourceAdmissionTelemetry();
		if(UploadsDoneDelta > 0 ||
			LoadedDelta > 0 ||
			Telemetry.m_AdmittedDelta > 0 ||
			Telemetry.m_StartedDelta > 0 ||
			gs_TeeListDrainPerfSession.m_LastBackgroundDrain != BackgroundDrainActive ||
			gs_TeeListDrainPerfSession.m_LastVisibleReady != VisibleSourceSettledCount ||
			gs_TeeListDrainPerfSession.m_LastVisibleTotal != (int)vVisibleSkinIndices.size() ||
			gs_TeeListDrainPerfSession.m_LastRequested != (int)SkinStats.m_NumBackgroundRequested ||
			gs_TeeListDrainPerfSession.m_LastPending != (int)SkinStats.m_NumPending ||
			gs_TeeListDrainPerfSession.m_LastLoading != (int)SkinStats.m_NumLoading ||
			gs_TeeListDrainPerfSession.m_LastLoaded != (int)SkinStats.m_NumLoaded)
		{
			const int GpuUploadLimitUnits = GameClient()->GpuUploadLimiter()->MaxUploadsPerFrame();
			const int GpuUploadRemainingUnits = GameClient()->GpuUploadLimiter()->RemainingUploads();
			const int FinalizeBudgetLimit = Throughput.m_FinalizeBudgetLimit;
			const char *pEffectiveFrameContext = SettingsSkinThroughputControllerModeName(Throughput.m_Mode);
			char aPayload[1024];
			str_format(aPayload, sizeof(aPayload), "event=list_drain_tick mode=%s visible_ready=%d visible_total=%d visible_waiting=%d visible_background_requested=%d visible_nonterminal_waiting=%d requested=%d pending=%d loading=%d loaded=%d uploads_done_delta=%llu loaded_delta=%llu requested_delta=%d admitted_delta=%d started_delta=%d real_inflight=%d loading_window_limit=%d loading_window_used=%d dynamic_decision=%s request_budget_block_reason=%s last_wait_reason=%s gpu_upload_limit_units=%d gpu_upload_remaining_units=%d finalize_budget_limit=%d effective_frame_context=%s controller_reason=%s visible_reserve_effective=%d frame_time_avg_ms=%.3f render_frame_time_ms=%.3f admission_underfed=%d underfed_streak=%d",
				BackgroundDrainActive ? "background_drain" : "visible",
				VisibleSourceSettledCount,
				(int)vVisibleSkinIndices.size(),
				maximum(0, (int)vVisibleSkinIndices.size() - VisibleSourceSettledCount),
				VisibleBackgroundRequestedCount,
				VisibleNonTerminalWaitingCount,
				(int)SkinStats.m_NumBackgroundRequested,
				(int)SkinStats.m_NumPending,
				(int)SkinStats.m_NumLoading,
				(int)SkinStats.m_NumLoaded,
				(unsigned long long)UploadsDoneDelta,
				(unsigned long long)LoadedDelta,
				RequestedDelta,
				Telemetry.m_AdmittedDelta,
				Telemetry.m_StartedDelta,
				Telemetry.m_RealInflight,
				Telemetry.m_LoadingWindowLimit,
				Telemetry.m_LoadingWindowUsed,
				Telemetry.m_aDynamicDecision,
				SettingsSkinBackgroundRequestBlockReasonName(BackgroundBudgetDecision.m_BlockReason),
				Telemetry.m_aLastWaitReason,
				GpuUploadLimitUnits,
				GpuUploadRemainingUnits,
				FinalizeBudgetLimit,
				pEffectiveFrameContext,
				Telemetry.m_aControllerReason,
				Telemetry.m_VisibleReserve,
				Telemetry.m_FrameTimeAverageMs,
				Telemetry.m_RenderFrameTimeMs,
				Telemetry.m_AdmissionUnderfed ? 1 : 0,
				Telemetry.m_UnderfedStreak);
			QmPerfLogPayload("perf/settings-skin-source", aPayload, Client(), "settings:tee");
			gs_TeeListDrainPerfSession.m_TotalRequested += (uint64_t)maximum(0, RequestedDelta);
			gs_TeeListDrainPerfSession.m_TotalAdmitted += (uint64_t)maximum(0, Telemetry.m_AdmittedDelta);
			gs_TeeListDrainPerfSession.m_TotalStarted += (uint64_t)maximum(0, Telemetry.m_StartedDelta);
			gs_TeeListDrainPerfSession.m_MaxRequested = maximum(gs_TeeListDrainPerfSession.m_MaxRequested, (int)SkinStats.m_NumBackgroundRequested);
			gs_TeeListDrainPerfSession.m_MaxPending = maximum(gs_TeeListDrainPerfSession.m_MaxPending, (int)SkinStats.m_NumPending);
			gs_TeeListDrainPerfSession.m_MaxLoading = maximum(gs_TeeListDrainPerfSession.m_MaxLoading, (int)SkinStats.m_NumLoading);
			gs_TeeListDrainPerfSession.m_MaxRealInflight = maximum(gs_TeeListDrainPerfSession.m_MaxRealInflight, Telemetry.m_RealInflight);
			gs_TeeListDrainPerfSession.m_CountFuseLimit = CountFuseLimit;
			if(str_comp(Telemetry.m_aLastWaitReason, "loading_window") == 0)
				gs_TeeListDrainPerfSession.m_NumLoadingWindowWaits++;
			else if(str_comp(Telemetry.m_aLastWaitReason, "gpu_upload_budget") == 0)
				gs_TeeListDrainPerfSession.m_NumGpuBudgetWaits++;
			else if(str_comp(Telemetry.m_aLastWaitReason, "queue_fuse") == 0)
				gs_TeeListDrainPerfSession.m_NumQueueFuseWaits++;
			gs_TeeListDrainPerfSession.m_LastUploads = UploadsDoneNow;
			gs_TeeListDrainPerfSession.m_LastLoads = LoadedNow;
			gs_TeeListDrainPerfSession.m_LastBackgroundDrain = BackgroundDrainActive;
			gs_TeeListDrainPerfSession.m_LastVisibleReady = VisibleSourceSettledCount;
			gs_TeeListDrainPerfSession.m_LastVisibleTotal = (int)vVisibleSkinIndices.size();
			gs_TeeListDrainPerfSession.m_LastRequested = (int)SkinStats.m_NumBackgroundRequested;
			gs_TeeListDrainPerfSession.m_LastPending = (int)SkinStats.m_NumPending;
			gs_TeeListDrainPerfSession.m_LastLoading = (int)SkinStats.m_NumLoading;
			gs_TeeListDrainPerfSession.m_LastLoaded = (int)SkinStats.m_NumLoaded;
			gs_TeeListDrainPerfSession.m_LastAdmittedDelta = Telemetry.m_AdmittedDelta;
			gs_TeeListDrainPerfSession.m_LastStartedDelta = Telemetry.m_StartedDelta;
		}
		if(Telemetry.m_AdmissionInvariantViolated)
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload), "event=admission_invariant_violation pending=%d loading=%d real_inflight=%d count_fuse_limit=%d",
				(int)SkinStats.m_NumPending,
				(int)SkinStats.m_NumLoading,
				Telemetry.m_RealInflight,
				CountFuseLimit);
			QmPerfLogPayload("perf/settings-skin-source", aPayload, Client(), "settings:tee");
		}
	}
	const int NewSelected = s_ListBox.DoEnd();
	const double ListFrameDurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - ListFrameStartTime).count();
	if(PerfDebugEnabled())
	{
		if(QmPerfShouldLogDuration(ListFrameDurationMs, false))
		{
			const int RowsSkipped = maximum(0, VisibleRange.m_TotalRows - RowsRendered);
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload),
				"event=list_frame page=settings:tee rows_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d first_visible_index=%d last_visible_index=%d dur_ms=%.3f source=settings_tee",
				VisibleRange.m_TotalRows, VisibleRange.m_VisibleRows, RowsRendered, RowsIterated, RowsSkipped, FirstVisibleIndex, LastVisibleIndex, ListFrameDurationMs);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
		}
	}
	const bool SkinListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
	if(SkinListScrollActive)
	{
		StartSettingsPerfScrollWindow("skins_grid_scroll", SettingsPerfContextName(), "settings:tee", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "settings:tee";
		MenuUiPerf.m_pOperation = "skins_grid_scroll";
		MenuUiPerf.m_ItemsTotal = (int)vSkinList.size();
		MenuUiPerf.m_ItemsVisible = maximum(0, VisibleRange.m_EndItem - VisibleRange.m_FirstItem);
		MenuUiPerf.m_ItemsProcessed = MenuUiPerf.m_ItemsVisible;
		MenuUiPerf.m_ItemsSkipped = maximum(0, (int)vSkinList.size() - MenuUiPerf.m_ItemsProcessed);
		MenuUiPerf.m_UiMs = (float)ListFrameDurationMs;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}
	m_SettingsScrollActive = m_SettingsScrollActive || SkinListScrollActive;
	gs_TeeSettingsPageState.m_SkinListScrollActiveLastFrame = SkinListScrollActive;
	if(OldSelected != NewSelected)
	{
		if(NewSelected >= 0 && NewSelected < (int)vSkinList.size())
		{
			CommitSettingsTeeSkinEdits();
			const int SourceIndex = SwapFirst(NewSelected);
			gs_TeeSettingsPageState.m_SelectedIndex = SourceIndex;
			if(AllSkinsCollection)
				GameClient()->m_Skins.MaterializeSkinListEntry(SourceIndex);
			const CSkins::CSkinListEntry &SelectedSkinEntry = vSkinList[SourceIndex];
			if(SelectedSkinEntry.SkinContainer() != nullptr)
			{
				str_copy(pSkinName, SelectedSkinEntry.SkinContainer()->Name(), SkinNameSize);
				if(SelectedSkinEntry.ColorKey().has_value())
				{
					const auto &SelectedColorKey = SelectedSkinEntry.ColorKey().value();
					*pUseCustomColor = SelectedColorKey.m_UseCustomColor ? 1 : 0;
					if(SelectedColorKey.m_UseCustomColor)
					{
						*pColorBody = SelectedColorKey.m_ColorBody;
						*pColorFeet = SelectedColorKey.m_ColorFeet;
					}
				}
				SkinList.ForceRefresh();
				SetNeedSendInfo();
				GameClient()->m_Skins.RecordRecentSkin(QueueDummy);
			}
		}
	}
	if(DoubleClickIndex >= 0 && DoubleClickIndex < (int)vSkinList.size())
	{
		const CSkins::CSkinListEntry &Entry = vSkinList[DoubleClickIndex];
		if(Entry.SkinContainer() != nullptr)
		{
			CommitSettingsTeeSkinEdits();
			const auto &ColorKey = Entry.ColorKey();
			const bool TargetDummy = QmTeeSkinApplyTargetDummy(DoubleClickTarget) != 0;
			QmApplyTeeSkinToTarget(g_Config, DoubleClickTarget, Entry.SkinContainer()->Name(),
				ColorKey.has_value(),
				ColorKey.has_value() && ColorKey->m_UseCustomColor,
				ColorKey.has_value() ? ColorKey->m_ColorBody : 0,
				ColorKey.has_value() ? ColorKey->m_ColorFeet : 0);
			SkinList.ForceRefresh();
			SetNeedSendInfo(TargetDummy);
			GameClient()->m_Skins.RecordRecentSkin(TargetDummy);
		}
	}

	if(vSkinList.empty() && !PlanKnown)
	{
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_skin_list_loading", &MainView, Localize("Loading…"), BodySize, TEXTALIGN_MC);
	}
	else if(vSkinList.empty())
	{
		CUIRect FilterLabel, ResetButton;
		const float EmptyStateHeight = TeeMetrics.m_LineHeight + TeeMetrics.m_LineSpacing + TeeMetrics.m_ButtonHeight;
		MainView.HMargin(maximum(0.0f, (MainView.h - EmptyStateHeight) / 2.0f), &FilterLabel);
		FilterLabel.HSplitTop(TeeMetrics.m_LineHeight, &FilterLabel, &ResetButton);
		ResetButton.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &ResetButton);
		ResetButton.HSplitTop(TeeMetrics.m_ButtonHeight, &ResetButton, nullptr);
		ResetButton.VMargin(maximum(0.0f, (ResetButton.w - 200.0f * UiScale) / 2.0f), &ResetButton);
		const char *pEmptyText = gs_TeeSkinCollection.m_Collection == ETeeSkinCollection::RECENT && GameClient()->m_Skins.RecentSkins().Entries().empty() ? Localize("No recently used skins") : Localize("No skins match your filter criteria");
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_no_skins_match_label", &FilterLabel, pEmptyText, BodySize, TEXTALIGN_MC);
		static CButtonContainer s_ResetButton;
		if(DoSettingsButton_Menu(SETTINGS_TEE, -1, -1, &s_ResetButton, "tee-reset-filter", Localize("Reset filter"), 0, &ResetButton))
		{
			s_SkinFilterInput.Clear();
			gs_TeeSkinCollection.m_Collection = ETeeSkinCollection::ALL;
			g_Config.m_QmSkinListFilter = (int)ESkinListFilter::ALL;
			g_Config.m_ClVanillaSkinsOnly = 0;
			s_ListBox.ResetScroll();
			GameClient()->m_Skins.RebuildSkinListPlan();
			ShouldRefresh = true;
		}
	}

	const IUiContext TeeSkinSearchCtx = SettingsUiContext("settings_tee_skin_search", UiScale);
	ui_widget::SInputFieldOptions SkinSearchOptions;
	SkinSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SkinSearchOptions.m_Clearable = true;
	SkinSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	SkinSearchOptions.m_FontSize = BodySize;
	if(ui_widget::InputField(TeeSkinSearchCtx, &s_SkinFilterInput, QuickSearch, SkinSearchOptions).m_Changed)
	{
		GameClient()->m_Skins.RebuildSkinListPlan();
	}

	static CButtonContainer s_SkinDatabaseButton;
	if(Ui()->DoButton_QmIcon(&s_SkinDatabaseButton, EQmIcon::EARTH_AMERICAS, FONT_ICON_EARTH_AMERICAS, 0, &DatabaseButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		Client()->ViewLink("https://ddnet.org/skins/");
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SkinDatabaseButton, &DatabaseButton, Localize("Skin Database"));

	static CButtonContainer s_EditSkinTextureButton;
	if(Ui()->DoButton_QmIcon(&s_EditSkinTextureButton, EQmIcon::PEN_TO_SQUARE, FONT_ICON_PEN_TO_SQUARE, 0, &EditTextureButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
		AssetsEditorOpen(ASSETS_EDITOR_TYPE_SKIN);
	GameClient()->m_Tooltips.DoToolTip(&s_EditSkinTextureButton, &EditTextureButton, Localize("Edit skin texture"));

	static CButtonContainer s_DirectoryButton;
	if(Ui()->DoButton_QmIcon(&s_DirectoryButton, EQmIcon::FOLDER_OPEN, FONT_ICON_FOLDER_OPEN, 0, &DirectoryButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	const bool RefreshClicked = DoButton_Menu_QmIcon(&s_SkinRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton) != 0;
	if(!Ui()->RenderOnly() && (RefreshClicked || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed())))
	{
		ShouldRefresh = true;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SkinRefreshButton, &RefreshButton, Localize("Refresh"));
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	RefreshVisibleRows = (int)vVisibleSkinIndices.size();
	str_copy(aRefreshFirstVisibleSkin, pFirstVisibleSkin, sizeof(aRefreshFirstVisibleSkin));
	if(ShouldRefresh)
		RefreshSettingsTeeSkins(RefreshVisibleRows, aRefreshFirstVisibleSkin);
}

void CMenus::AdvanceSettingsTeeSkinListOffscreen()
{
	if(g_Config.m_QmSettingsPrewarm == 0)
	{
		gs_TeeListDrainPerfSession.m_Active = false;
		return;
	}
	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	std::vector<CSkins::CSkinListEntry> &vSkinList = gs_TeeSkinCollection.Resolve(GameClient()->m_Skins, SkinList);
	const bool AllSkinsCollection = gs_TeeSkinCollection.m_Collection == ETeeSkinCollection::ALL;
	const uint64_t ListRevision = AllSkinsCollection ? SkinList.Revision() : gs_TeeSkinCollection.Revision();
	if(!gs_TeeSettingsPageState.m_TeePageActiveLastFrame)
	{
		gs_TeeListDrainPerfSession.m_Active = false;
		ResetTeeSettingsPageState();
		m_SettingsHighPrioritySettled = false;
	}
	if(!gs_TeeSettingsPageState.m_TeePageActiveLastFrame)
	{
		const int64_t NowNs = time_get_nanoseconds().count();
		gs_TeeSettingsPageState.m_TeePageActiveLastFrame = true;
		gs_TeeSettingsPageState.m_TeeEnterStartNs = NowNs;
		BeginTeeListDrainPerfSession(GameClient()->m_Skins, NowNs);
	}
	int &ScrollCooldownFrames = gs_TeeSettingsPageState.m_SkinListScrollCooldownFrames;
	int &PostScrollRecoveryFrames = gs_TeeSettingsPageState.m_SkinListPostScrollRecoveryFrames;
	const int PreviousScrollCooldownFrames = ScrollCooldownFrames;
	ScrollCooldownFrames = SettingsScrollInteractionCooldown(false, ScrollCooldownFrames, 3);
	PostScrollRecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousScrollCooldownFrames, ScrollCooldownFrames, PostScrollRecoveryFrames, 2);
	m_SettingsPostScrollRecoveryFrames = PostScrollRecoveryFrames;

	SSettingsAdaptiveBudgetInput BudgetInput;
	BudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(BudgetInput.m_aOperation, SettingsPerfActiveOperation(), sizeof(BudgetInput.m_aOperation));
	str_copy(BudgetInput.m_aPage, "settings:tee", sizeof(BudgetInput.m_aPage));
	str_copy(BudgetInput.m_aTab, "none", sizeof(BudgetInput.m_aTab));
	str_copy(BudgetInput.m_aContext, SettingsPerfContextName(), sizeof(BudgetInput.m_aContext));
	BudgetInput.m_FrameMsAverage = QmUiCpuMsAverage();
	BudgetInput.m_FrameMsP95 = QmUiCpuMsP95();
	BudgetInput.m_TargetFrameMs = QmUiTargetFrameMs();
	BudgetInput.m_PostScrollRecoveryFrames = PostScrollRecoveryFrames;
	BudgetInput.m_BackgroundBacklog = (int)vSkinList.size();
	BudgetInput.m_WindowActive = true;
	BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, "tee", BudgetInput);

	constexpr int OffscreenVisibleWindow = 12;
	int VisibleReady = 0;
	int VisibleWaiting = 0;
	int VisibleBackgroundRequested = 0;
	int VisibleNonterminalWaiting = 0;
	const int VisibleTotal = minimum((int)vSkinList.size(), OffscreenVisibleWindow);
	int VisibleValid = 0;
	for(int i = VisibleTotal - 1; i >= 0; --i)
	{
		CSkins::CSkinListEntry &Entry = vSkinList[i];
		const CSkins::CSkinContainer *pContainer = Entry.SkinContainer();
		if(pContainer == nullptr)
			continue;
		++VisibleValid;
		const auto State = pContainer->State();
		const bool Settled = State == CSkins::CSkinContainer::EState::LOADED || State == CSkins::CSkinContainer::EState::ERROR || State == CSkins::CSkinContainer::EState::NOT_FOUND;
		VisibleReady += Settled ? 1 : 0;
		VisibleWaiting += Settled ? 0 : 1;
		VisibleBackgroundRequested += State == CSkins::CSkinContainer::EState::BACKGROUND_REQUESTED ? 1 : 0;
		VisibleNonterminalWaiting += State == CSkins::CSkinContainer::EState::UNLOADED || State == CSkins::CSkinContainer::EState::BACKGROUND_REQUESTED || State == CSkins::CSkinContainer::EState::PENDING || State == CSkins::CSkinContainer::EState::LOADING ? 1 : 0;
		Entry.RequestLoad(ESettingsResourcePriority::VISIBLE);
	}
	m_SettingsHighPrioritySettled = VisibleReady == VisibleValid;

	const auto &Throughput = GameClient()->m_Skins.SettingsThroughputControllerOutput();
	const auto AdmissionTelemetry = GameClient()->m_Skins.SettingsSourceAdmissionTelemetry();
	const auto StatsBeforeBackgroundRequest = GameClient()->m_Skins.LoadingStats();
	const int RecentLoadedDelta = gs_TeeListDrainPerfSession.m_Active ? (int)(GameClient()->m_Skins.SettingsSourceLoadsCompleted() - gs_TeeListDrainPerfSession.m_LastLoads) : 0;
	const auto BackgroundBudgetDecision = SettingsSkinBackgroundRequestBudgetDecision({
		Throughput.m_BackgroundRequestBudget,
		(int)StatsBeforeBackgroundRequest.m_NumPending,
		(int)StatsBeforeBackgroundRequest.m_NumLoading,
		(int)StatsBeforeBackgroundRequest.m_NumBackgroundRequested,
		Throughput.m_CountFuseLimit,
		Throughput.m_VisibleReserve,
		RecentLoadedDelta,
		AdmissionTelemetry.m_AdmittedDelta,
		Throughput.m_BackgroundDrainActive,
	});
	int BackgroundBudget = BackgroundBudgetDecision.m_RequestBudget;
	const int InitialBackgroundBudget = BackgroundBudget;
	gs_TeeSettingsPageState.m_LastRequestBudgetActual = BackgroundBudget;
	gs_TeeSettingsPageState.m_LastRequestBudgetBlockReason = BackgroundBudgetDecision.m_BlockReason;
	size_t &BackgroundCursor = gs_TeeSettingsPageState.m_BackgroundRequestCursor;
	if(gs_TeeSettingsPageState.m_BackgroundRequestScanRevision != ListRevision)
	{
		gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = false;
		gs_TeeSettingsPageState.m_BackgroundRequestScanRevision = ListRevision;
		BackgroundCursor = 0;
	}
	if(g_Config.m_QmSettingsPrewarm != 0 && m_SettingsHighPrioritySettled && !vSkinList.empty() && !gs_TeeSettingsPageState.m_BackgroundRequestScanComplete)
	{
		for(size_t Attempts = 0; Attempts < TEE_BACKGROUND_SCAN_LIMIT && BackgroundCursor < vSkinList.size() && BackgroundBudget > 0; ++Attempts)
		{
			const size_t Index = BackgroundCursor++;
			if((int)Index < VisibleTotal)
				continue;
			const CSkins::CSkinContainer *pContainer = vSkinList[Index].SkinContainer();
			if(pContainer == nullptr && AllSkinsCollection)
			{
				const SSettingsSkinListEntry *pPlannedEntry = SkinList.PlannedEntry((int)Index);
				if(pPlannedEntry != nullptr)
					pContainer = GameClient()->m_Skins.FindContainerOrNullptr(pPlannedEntry->m_Name.c_str(), false);
			}
			if(pContainer == nullptr || pContainer->IsCatalogOnly() || pContainer->State() != CSkins::CSkinContainer::EState::UNLOADED)
				continue;
			const_cast<CSkins::CSkinContainer *>(pContainer)->RequestLoad(ESettingsResourcePriority::BACKGROUND);
			--BackgroundBudget;
		}
		if(BackgroundCursor >= vSkinList.size())
			gs_TeeSettingsPageState.m_BackgroundRequestScanComplete = true;
	}

	CSkins::SSettingsTeeVisibleSnapshot VisibleSnapshot;
	VisibleSnapshot.m_VisibleTotal = VisibleValid;
	VisibleSnapshot.m_VisibleReady = VisibleReady;
	VisibleSnapshot.m_VisibleWaiting = VisibleWaiting;
	VisibleSnapshot.m_VisibleBackgroundRequested = VisibleBackgroundRequested;
	VisibleSnapshot.m_VisibleNonterminalWaiting = VisibleNonterminalWaiting;
	str_copy(VisibleSnapshot.m_aRequestBudgetBlockReason, SettingsSkinBackgroundRequestBlockReasonName(BackgroundBudgetDecision.m_BlockReason), sizeof(VisibleSnapshot.m_aRequestBudgetBlockReason));
	GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);

	if(gs_TeeListDrainPerfSession.m_Active)
	{
		const auto Stats = GameClient()->m_Skins.LoadingStats();
		const uint64_t UploadsNow = GameClient()->m_Skins.SettingsSourceUploadsCompleted();
		const uint64_t LoadsNow = GameClient()->m_Skins.SettingsSourceLoadsCompleted();
		const int RequestsIssued = InitialBackgroundBudget - BackgroundBudget;
		gs_TeeListDrainPerfSession.m_TotalRequested += (uint64_t)maximum(0, RequestsIssued);
		gs_TeeListDrainPerfSession.m_TotalAdmitted += (uint64_t)maximum(0, AdmissionTelemetry.m_AdmittedDelta);
		gs_TeeListDrainPerfSession.m_TotalStarted += (uint64_t)maximum(0, AdmissionTelemetry.m_StartedDelta);
		gs_TeeListDrainPerfSession.m_MaxRequested = maximum(gs_TeeListDrainPerfSession.m_MaxRequested, (int)Stats.m_NumBackgroundRequested);
		gs_TeeListDrainPerfSession.m_MaxPending = maximum(gs_TeeListDrainPerfSession.m_MaxPending, (int)Stats.m_NumPending);
		gs_TeeListDrainPerfSession.m_MaxLoading = maximum(gs_TeeListDrainPerfSession.m_MaxLoading, (int)Stats.m_NumLoading);
		gs_TeeListDrainPerfSession.m_MaxRealInflight = maximum(gs_TeeListDrainPerfSession.m_MaxRealInflight, AdmissionTelemetry.m_RealInflight);
		gs_TeeListDrainPerfSession.m_CountFuseLimit = Throughput.m_CountFuseLimit;
		if(str_comp(AdmissionTelemetry.m_aLastWaitReason, "loading_window") == 0)
			gs_TeeListDrainPerfSession.m_NumLoadingWindowWaits++;
		else if(str_comp(AdmissionTelemetry.m_aLastWaitReason, "gpu_upload_budget") == 0)
			gs_TeeListDrainPerfSession.m_NumGpuBudgetWaits++;
		else if(str_comp(AdmissionTelemetry.m_aLastWaitReason, "queue_fuse") == 0)
			gs_TeeListDrainPerfSession.m_NumQueueFuseWaits++;
		gs_TeeListDrainPerfSession.m_LastUploads = UploadsNow;
		gs_TeeListDrainPerfSession.m_LastLoads = LoadsNow;
		gs_TeeListDrainPerfSession.m_LastBackgroundDrain = Throughput.m_BackgroundDrainActive;
		gs_TeeListDrainPerfSession.m_LastVisibleReady = VisibleReady;
		gs_TeeListDrainPerfSession.m_LastVisibleTotal = VisibleValid;
		gs_TeeListDrainPerfSession.m_LastRequested = (int)Stats.m_NumBackgroundRequested;
		gs_TeeListDrainPerfSession.m_LastPending = (int)Stats.m_NumPending;
		gs_TeeListDrainPerfSession.m_LastLoading = (int)Stats.m_NumLoading;
		gs_TeeListDrainPerfSession.m_LastLoaded = (int)Stats.m_NumLoaded;
		gs_TeeListDrainPerfSession.m_LastAdmittedDelta = AdmissionTelemetry.m_AdmittedDelta;
		gs_TeeListDrainPerfSession.m_LastStartedDelta = AdmissionTelemetry.m_StartedDelta;

		const bool AllSourcesTerminal = Stats.m_NumUnloaded == 0 && Stats.m_NumBackgroundRequested == 0 && Stats.m_NumPending == 0 && Stats.m_NumLoading == 0;
		if(AllSourcesTerminal)
		{
			int FullListValid = 0;
			int FullListSettled = 0;
			for(const CSkins::CSkinListEntry &Entry : vSkinList)
			{
				const CSkins::CSkinContainer *pContainer = Entry.SkinContainer();
				if(pContainer == nullptr)
					continue;
				++FullListValid;
				const auto State = pContainer->State();
				FullListSettled += State == CSkins::CSkinContainer::EState::LOADED || State == CSkins::CSkinContainer::EState::ERROR || State == CSkins::CSkinContainer::EState::NOT_FOUND ? 1 : 0;
			}
			const auto Lifecycle = SettingsTeeOffscreenLifecycleDecision({
				(int)vSkinList.size(),
				FullListValid,
				FullListSettled,
				gs_TeeListDrainPerfSession.m_Active,
				PerfDebugEnabled(),
			});
			if(Lifecycle.m_CompleteDrainSession)
			{
				gs_TeeSettingsPageState.m_TeeFullListReadyLogged = true;
				if(Lifecycle.m_LogCompletion)
					LogTeeListDrainSummary(Client(), GameClient()->m_Skins, Stats, true, time_get_nanoseconds().count());
				else
					gs_TeeListDrainPerfSession.m_Active = false;
			}
		}
	}
}

void CMenus::RenderSettingsTeeSkinQueue(CUIRect Content, const SSettingsContentMetrics &TeeMetrics)
{
	const float UiScale = TeeMetrics.m_UiScale;
	const float BodySize = TeeMetrics.m_BodySize;
	const int QueueDummy = m_Dummy ? 1 : 0;
	CUIRect QueueSection = Content;
	// 队列条目预览以当前子页的皮肤为底，再逐条覆盖队列条目的皮肤与颜色
	const char *pQueueBaseSkinName = QueueDummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const CSkins::CSkinContainer *pQueueBaseContainer = GameClient()->m_Skins.FindContainerOrNullptr(pQueueBaseSkinName[0] == '\0' ? "default" : pQueueBaseSkinName);
	if(pQueueBaseContainer != nullptr && pQueueBaseContainer->IsSpecial())
		pQueueBaseContainer = nullptr;
	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pQueueBaseContainer == nullptr || pQueueBaseContainer->Skin() == nullptr ? GameClient()->m_Skins.Find("default") : pQueueBaseContainer->Skin().get());
	OwnSkinInfo.ApplyColors(QueueDummy ? g_Config.m_ClDummyUseCustomColor != 0 : g_Config.m_ClPlayerUseCustomColor != 0, QueueDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody, QueueDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
	OwnSkinInfo.m_Size = 60.0f;
	int &QueueEnabled = QueueDummy ? g_Config.m_QmDummySkinQueueEnabled : g_Config.m_QmSkinQueueEnabled;
	int &QueueInterval = QueueDummy ? g_Config.m_QmDummySkinQueueInterval : g_Config.m_QmSkinQueueInterval;
	int &QueueIndex = QueueDummy ? g_Config.m_QmDummySkinQueueIndex : g_Config.m_QmSkinQueueIndex;
	int &QueueRandomJoin = QueueDummy ? g_Config.m_QmDummySkinQueueRandomJoin : g_Config.m_QmSkinQueueRandomJoin;
	const int AppliedPresetIndex = GameClient()->m_Skins.AppliedSkinQueuePresetIndex(QueueDummy);
	int &SelectedPresetIndex = gs_TeeSettingsPageState.m_aSelectedQueuePreset[QueueDummy];
	if(SelectedPresetIndex < 0 || (size_t)SelectedPresetIndex >= GameClient()->m_Skins.SkinQueuePresets(QueueDummy).size())
		SelectedPresetIndex = AppliedPresetIndex >= 0 ? AppliedPresetIndex : 0;
	const int ActivePresetIndex = SelectedPresetIndex;
	const bool QueueDirty = GameClient()->m_Skins.SkinQueueDirty(QueueDummy);
	const auto &SkinQueue = GameClient()->m_Skins.SkinQueue(QueueDummy);
	const auto &vQueuePresets = GameClient()->m_Skins.SkinQueuePresets(QueueDummy);
	const SSettingsTeeQueuePanelGeometry QueueGeometry = ResolveSettingsTeeQueuePanelGeometry(TeeMetrics, static_cast<int>(SkinQueue.size()), static_cast<int>(vQueuePresets.size()), Content.w);
	const auto PresetDisplayName = [&vQueuePresets](size_t PresetIndex) {
		if(PresetIndex == 0)
			return Localize("Default preset");
		if(PresetIndex == 1)
			return Localize("Server preset");
		return vQueuePresets[PresetIndex].m_Name.c_str();
	};
	CUIRect QueueHeader, QueueList, QueuePresets;
	CUIRect QueueTarget;
	QueueSection.HSplitTop(TeeMetrics.m_LineHeight, &QueueTarget, &QueueSection);
	QueueSection.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueSection);
	CUIRect PlayerTarget, DummyTarget;
	static ui_widget::SSettingsActionGroupState s_QueueTargetWidths;
	const char *apQueueTargets[] = {Localize("Player"), Localize("Dummy")};
	const auto TargetActions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_QueueTargetWidths, QueueTarget, apQueueTargets, std::size(apQueueTargets), TeeMetrics);
	PlayerTarget = TargetActions.m_aButtons[0];
	DummyTarget = TargetActions.m_aButtons[1];
	static CButtonContainer s_aQueueTargets[NUM_DUMMIES];
	int RequestedTarget = QueueDummy;
	if(DoSettingsButton_Menu(SETTINGS_TEE, -1, -1, &s_aQueueTargets[0], "tee-queue-target-player", Localize("Player"), QueueDummy == 0, &PlayerTarget, TeeMetrics))
		RequestedTarget = 0;
	if(DoSettingsButton_Menu(SETTINGS_TEE, -1, -1, &s_aQueueTargets[1], "tee-queue-target-dummy", Localize("Dummy"), QueueDummy == 1, &DummyTarget, TeeMetrics))
		RequestedTarget = 1;
	QueueSection.HSplitTop(TeeMetrics.m_LineHeight, &QueueHeader, &QueueSection);
	if(DoSettingsButton_CheckBox(SETTINGS_TEE, -1, &QueueEnabled, QueueDummy ? "tee-dummy-skin-queue-enabled" : "tee-player-skin-queue-enabled", Localize("Enable rotation"), QueueEnabled, &QueueHeader))
	{
		QueueEnabled ^= 1;
	}
	char aCurrentQueueLabel[128];
	if(AppliedPresetIndex >= 0 && (size_t)AppliedPresetIndex < vQueuePresets.size())
	{
		str_format(aCurrentQueueLabel, sizeof(aCurrentQueueLabel), Localize("Queue preset: %s"), PresetDisplayName((size_t)AppliedPresetIndex));
	}
	else
	{
		str_format(aCurrentQueueLabel, sizeof(aCurrentQueueLabel), Localize("Queue preset: %s"), Localize("Custom"));
	}
	SLabelProperties CurrentQueueLabelProps;
	CurrentQueueLabelProps.m_DisallowNewline = true;
	CurrentQueueLabelProps.m_StopAtEnd = true;
	CurrentQueueLabelProps.m_MinimumFontSize = 6.0f;
	QueueSection.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueSection);

	const char *pQueueIntervalLabel = Localize("Switch interval");
	const float QueueValueInputWidth = 58.0f * UiScale;
	const float QueueValueUnitWidth = maximum(18.0f * UiScale, TextRender()->TextWidth(TeeMetrics.m_SmallSize, "ms") + TeeMetrics.m_LineSpacing);
	const float QueueIntervalControlsWidth = QueueValueInputWidth + QueueValueUnitWidth;
	const bool StackQueueInterval = QueueGeometry.m_StackInterval;
	const float QueueIntervalRowHeight = QueueGeometry.m_IntervalHeight;
	CUIRect IntervalRow, IntervalLabel, IntervalControls;
	QueueSection.HSplitTop(QueueIntervalRowHeight, &IntervalRow, &QueueSection);
	CUIRect IntervalInputGroup;
	if(StackQueueInterval)
	{
		IntervalRow.HSplitTop(TeeMetrics.m_LineHeight, &IntervalLabel, &IntervalControls);
		IntervalControls.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &IntervalControls);
		IntervalControls.VSplitLeft(minimum(IntervalControls.w, QueueIntervalControlsWidth), &IntervalControls, nullptr);
	}
	else
	{
		IntervalRow.VSplitRight(QueueIntervalControlsWidth, &IntervalLabel, &IntervalControls);
		IntervalLabel.VSplitRight(TeeMetrics.m_LineSpacing, &IntervalLabel, nullptr);
	}
	IntervalInputGroup = IntervalControls;
	IntervalInputGroup.VMargin(minimum(1.0f * UiScale, IntervalInputGroup.w * 0.5f), &IntervalInputGroup);
	SLabelProperties QueueControlLabelProps;
	QueueControlLabelProps.m_MaxWidth = IntervalLabel.w;
	QueueControlLabelProps.m_DisallowNewline = true;
	QueueControlLabelProps.m_StopAtEnd = true;
	QueueControlLabelProps.m_MinimumFontSize = 6.0f;
	DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-skin-queue-switch-interval", &IntervalLabel, pQueueIntervalLabel, BodySize, TEXTALIGN_ML, QueueControlLabelProps, (int)IntervalLabel.w);
	static ui_widget::SNumericFieldState s_aQueueIntervalStates[NUM_DUMMIES];
	IUiContext TeeSkinQueueIntervalCtx;
	TeeSkinQueueIntervalCtx.m_pUi = Ui();
	TeeSkinQueueIntervalCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TeeSkinQueueIntervalCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TeeSkinQueueIntervalCtx.m_ScopeHash = MakeUiScopeHash("settings_tee_skin_queue_interval_text_input");
	TeeSkinQueueIntervalCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SNumericFieldOptions QueueIntervalOptions;
	QueueIntervalOptions.m_FontSize = BodySize;
	QueueIntervalOptions.m_pSuffix = "ms";
	QueueIntervalOptions.m_TrailingWidth = QueueValueUnitWidth;
	QueueIntervalOptions.m_CommitPolicy = ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT;
	ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 0, 120000, IntervalInputGroup, QueueIntervalOptions);
	GameClient()->m_Tooltips.DoToolTip(&QueueInterval, &IntervalControls, Localize("0 disables timed rotation; skins only switch on random start events"));
	QueueSection.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueSection);

	// 进图随机起点开关
	CUIRect RandomJoinRow;
	QueueSection.HSplitTop(TeeMetrics.m_LineHeight, &RandomJoinRow, &QueueSection);
	if(DoSettingsButton_CheckBox(SETTINGS_TEE, -1, -1, &QueueRandomJoin, QueueDummy ? "tee-dummy-skin-queue-random-join" : "tee-player-skin-queue-random-join", Localize("Random skin on map join"), QueueRandomJoin, &RandomJoinRow, QueueControlLabelProps))
	{
		QueueRandomJoin ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&QueueRandomJoin, &RandomJoinRow, Localize("Start at a random queue position on every map join"));

	QueueSection.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueSection);
	QueueSection.HSplitTop(minimum(QueueSection.h, QueueGeometry.m_QueueListSurfaceHeight), &QueueList, &QueueSection);
	QueueSection.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueSection);
	QueueSection.HSplitTop(minimum(QueueSection.h, QueueGeometry.m_QueuePresetHeight), &QueuePresets, &QueueSection);
	QueueList.Margin(TeeMetrics.m_LineSpacing, &QueueList);
	QueuePresets.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueuePresets);
	QueuePresets.Margin(TeeMetrics.m_LineSpacing, &QueuePresets);

	CUIRect QueueListHeader, QueueListBody;
	QueueList.HSplitTop(TeeMetrics.m_LineHeight, &QueueListHeader, &QueueListBody);
	QueueListBody.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueListBody);
	QueueListBody.HSplitTop(minimum(QueueListBody.h, QueueGeometry.m_QueueListViewportHeight), &QueueListBody, nullptr);
	CUIRect QueueListHeaderLabel, QueueRandomRect, QueueShuffleRect, ClearQueueRect;
	const auto QueueHeaderActions = ResolveSettingsInputActionsLayout(QueueListHeader, 3, TeeMetrics);
	QueueListHeaderLabel = QueueHeaderActions.m_Input;
	QueueRandomRect = QueueHeaderActions.m_Actions.m_aButtons[0];
	QueueShuffleRect = QueueHeaderActions.m_Actions.m_aButtons[1];
	ClearQueueRect = QueueHeaderActions.m_Actions.m_aButtons[2];
	CurrentQueueLabelProps.m_MaxWidth = QueueListHeaderLabel.w;
	Ui()->DoLabel(&QueueListHeaderLabel, aCurrentQueueLabel, BodySize, TEXTALIGN_ML, CurrentQueueLabelProps);
	static CButtonContainer s_TeeRandomSkinQueueButton;
	if(Ui()->DoButton_QmIcon(&s_TeeRandomSkinQueueButton, EQmIcon::DICE_FIVE, FONT_ICON_DICE_FIVE, 0, &QueueRandomRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		if(!SkinQueue.empty())
		{
			CommitSettingsTeeSkinEdits();
			GameClient()->m_Skins.RandomSkinQueueIndex(QueueDummy);
			GameClient()->m_Skins.RecordRecentSkin(QueueDummy);
		}
	}
	GameClient()->m_Tooltips.DoToolTip(&s_TeeRandomSkinQueueButton, &QueueRandomRect, Localize("Apply a random skin from the queue"));
	static CButtonContainer s_TeeShuffleSkinQueueButton;
	const bool CanShuffleQueue = SkinQueue.size() > 1;
	if(Ui()->DoButton_QmIcon(&s_TeeShuffleSkinQueueButton, EQmIcon::ARROWS_LEFT_RIGHT, FONT_ICON_ARROWS_LEFT_RIGHT, CanShuffleQueue ? 0 : -1, &QueueShuffleRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanShuffleQueue)
		GameClient()->m_Skins.ShuffleSkinQueue(QueueDummy);
	GameClient()->m_Tooltips.DoToolTip(&s_TeeShuffleSkinQueueButton, &QueueShuffleRect, Localize("Shuffle skin queue"));
	static CButtonContainer s_TeeClearCurrentSkinQueueButton;
	if(Ui()->DoButton_QmIcon(&s_TeeClearCurrentSkinQueueButton, EQmIcon::TRASH, FONT_ICON_TRASH, 0, &ClearQueueRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
	{
		GameClient()->m_Skins.ClearSkinQueue(QueueDummy);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_TeeClearCurrentSkinQueueButton, &ClearQueueRect, Localize("Clear current queue"));

	static CListBox s_QueueListBox;
	static std::vector<char> s_QueueItemIds;
	static std::vector<char> s_QueueRemoveIds;
	static int s_QueueDragIndex = -1;
	static bool s_QueueDragging = false;
	static vec2 s_QueueDragStart = vec2(0.0f, 0.0f);
	static vec2 s_QueueDragGrabOffset = vec2(0.0f, 0.0f);
	static CUIRect s_QueueDraggedRect;
	static int s_QueueLastDummy = -1;

	if(s_QueueLastDummy != QueueDummy)
	{
		s_QueueLastDummy = QueueDummy;
		s_QueueDragIndex = -1;
		s_QueueDragging = false;
	}

	if(s_QueueDragIndex >= (int)SkinQueue.size())
	{
		s_QueueDragIndex = -1;
		s_QueueDragging = false;
	}

	if(SkinQueue.empty())
	{
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_queue_empty_label", &QueueListBody, Localize("Queue is empty"), BodySize, TEXTALIGN_MC);
	}
	else
	{
		s_QueueItemIds.resize(SkinQueue.size());
		s_QueueRemoveIds.resize(SkinQueue.size());

		int DragTarget = s_QueueDragIndex;
		int LastVisible = -1;
		CUIRect LastVisibleRect;
		int RemoveIndex = -1;
		int ApplyQueueIndex = -1;
		bool HasQueueDropLine = false;
		CUIRect QueueDropLine;
		if(s_QueueDragging)
		{
			DragTarget = -1;
		}
		s_QueueListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		s_QueueListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		// 统一行外观：胶囊整行 + token 行距 + 按字号派生的行内内边距；皮肤队列最多
		// 显示 6 行，滚动条改成覆盖式细轨（不再预留宽度，行宽不随队列长度横跳）。
		s_QueueListBox.SetCapsuleRows(BodySize);
		// 容器内边距：容器高度已在 ResolveSettingsTeeQueuePanelGeometry 里按
		// PaddedRowsHeight 计入两档留白，这里声明同一档，首/末行的胶囊不贴容器圆角。
		s_QueueListBox.SetContainerPadding(ui_token::list::ContainerPadding(TeeMetrics.m_ListRowHeight));
		s_QueueListBox.SetVisibleRowRange(1, ui_token::list::SKIN_QUEUE_MAX_VISIBLE_ROWS);
		s_QueueListBox.DoStart(TeeMetrics.m_ListRowHeight, (int)SkinQueue.size(), 1, 1, -1, &QueueListBody, true, IGraphics::CORNER_ALL);
		for(size_t i = 0; i < SkinQueue.size(); ++i)
		{
			const CListboxItem Item = s_QueueListBox.DoNextItem(&s_QueueItemIds[i], (int)i == QueueIndex);
			if(!Item.m_Visible)
			{
				continue;
			}
			auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

			LastVisible = (int)i;
			LastVisibleRect = Item.m_Rect;
			if(s_QueueDragging && DragTarget == -1 && Ui()->MouseY() < Item.m_Rect.y + Item.m_Rect.h * 0.5f)
			{
				DragTarget = (int)i;
			}

			if(s_QueueDragging && DragTarget == (int)i && (int)i != s_QueueDragIndex)
			{
				// 拖放落点高亮与行本身同一形状：胶囊行不能再用矩形小圆角。
				DrawRoundedSurface(Ui(), Item.m_Rect, ColorRGBA(0.4f, 0.4f, 1.0f, 0.2f), ColorRGBA(), ui_token::list::RowRadius(TeeMetrics.m_ListRowHeight));
			}
			if(s_QueueDragging && DragTarget == (int)i && (int)i != s_QueueDragIndex)
			{
				QueueDropLine = Item.m_Rect;
				QueueDropLine.x += 4.0f;
				QueueDropLine.w = maximum(0.0f, QueueDropLine.w - 8.0f);
				QueueDropLine.y += DragTarget > s_QueueDragIndex ? QueueDropLine.h - 1.0f : 0.0f;
				QueueDropLine.h = 2.0f;
				HasQueueDropLine = true;
			}

			// 行内左右内边距已由 CListBox 的胶囊行按字号让出，这里只排图标和删除按钮。
			CUIRect DragRect = Item.m_ContentRect;
			CUIRect RemoveRect;
			DragRect.VSplitRight(20.0f, &DragRect, &RemoveRect);
			CUIRect DragArea = DragRect;

			const float TeeSize = minimum(16.0f, TeeMetrics.m_ListRowHeight - 4.0f);
			CUIRect TeeRect, LabelRect;
			DragRect.VSplitLeft(TeeSize + 6.0f, &TeeRect, &LabelRect);

			char aEntryLabel[64];
			str_format(aEntryLabel, sizeof(aEntryLabel), "%d. %s", (int)i + 1, SkinQueue[i].m_SkinName.c_str());
			LabelRect.VSplitLeft(4.0f, nullptr, &LabelRect);
			Ui()->DoLabel(&LabelRect, aEntryLabel, BodySize, TEXTALIGN_ML);

			const CSkins::CSkinQueueEntry &QueueEntry = SkinQueue[i];
			const CSkin *pQueueSkin = GameClient()->m_Skins.Find(QueueEntry.m_SkinName.c_str());
			CTeeRenderInfo QueueInfo = OwnSkinInfo;
			QueueInfo.Apply(pQueueSkin);
			QueueInfo.ApplyColors(QueueEntry.m_UseCustomColor, QueueEntry.m_ColorBody, QueueEntry.m_ColorFeet);
			QueueInfo.m_Size = TeeSize;
			if(!Ui()->DrawingSuppressed())
				RenderTools()->RenderIdleTeeAtCenter(&QueueInfo, TeeRect.Center());

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			const float RemoveAlpha = Ui()->HotItem() == &s_QueueRemoveIds[i] ? 0.2f : 0.0f;
			TextRender()->TextColor(ColorRGBA(0.9f, 0.3f, 0.3f, 0.7f + RemoveAlpha));
			// 同一行左侧是 BodySize 的皮肤名，移除图标也跟行文字同号，而不是小字档。
			Ui()->DoLabel_QmIcon(&RemoveRect, EQmIcon::TRASH, FONT_ICON_TRASH, ui_token::control::IconBox(BodySize), TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			if(Ui()->DoButtonLogic(&s_QueueRemoveIds[i], 0, &RemoveRect, BUTTONFLAG_LEFT))
			{
				RemoveIndex = (int)i;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_QueueRemoveIds[i], &RemoveRect, Localize("Remove from queue"));

			if(s_QueueDragIndex == -1 && Ui()->MouseButtonClicked(0) && Ui()->MouseHovered(&DragArea))
			{
				s_QueueDragIndex = (int)i;
				s_QueueDragStart = Ui()->MousePos();
				s_QueueDragGrabOffset = Ui()->MousePos() - vec2(Item.m_Rect.x, Item.m_Rect.y);
				s_QueueDraggedRect = Item.m_Rect;
				s_QueueDragging = false;
			}
		}
		s_QueueListBox.DoEnd();

		if(s_QueueDragging && DragTarget == -1)
		{
			DragTarget = LastVisible >= 0 ? LastVisible : s_QueueDragIndex;
		}
		if(s_QueueDragging && !HasQueueDropLine && DragTarget >= 0 && DragTarget != s_QueueDragIndex && LastVisible >= 0)
		{
			QueueDropLine = LastVisibleRect;
			QueueDropLine.x = QueueList.x + 6.0f;
			QueueDropLine.w = maximum(0.0f, QueueList.w - 12.0f);
			QueueDropLine.y = LastVisibleRect.y + LastVisibleRect.h - 1.0f;
			QueueDropLine.h = 2.0f;
			HasQueueDropLine = true;
		}
		if(s_QueueDragging && HasQueueDropLine)
		{
			DrawRoundedSurface(Ui(), QueueDropLine, ColorRGBA(0.45f, 0.7f, 1.0f, 0.9f), ColorRGBA(), 1.0f);
		}
		if(s_QueueDragging && s_QueueDragIndex >= 0 && s_QueueDragIndex < (int)SkinQueue.size())
		{
			CUIRect QueueDragGhost = s_QueueDraggedRect;
			QueueDragGhost.x = Ui()->MouseX() - s_QueueDragGrabOffset.x;
			QueueDragGhost.y = Ui()->MouseY() - s_QueueDragGrabOffset.y;
			CUIRect QueueDragGhostShadow = QueueDragGhost;
			QueueDragGhostShadow.x += 1.5f;
			QueueDragGhostShadow.y += 2.0f;
			DrawRoundedSurface(Ui(), QueueDragGhostShadow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), ColorRGBA(), 4.0f);
			DrawRoundedSurface(Ui(), QueueDragGhost, ColorRGBA(0.18f, 0.2f, 0.24f, 0.92f), ColorRGBA(), 4.0f);
			CUIRect QueueDragGhostLabel = QueueDragGhost;
			QueueDragGhostLabel.VMargin(8.0f, &QueueDragGhostLabel);
			char aGhostLabel[64];
			str_format(aGhostLabel, sizeof(aGhostLabel), "%d. %s", s_QueueDragIndex + 1, SkinQueue[s_QueueDragIndex].m_SkinName.c_str());
			SLabelProperties QueueDragGhostLabelProps;
			QueueDragGhostLabelProps.m_MaxWidth = QueueDragGhostLabel.w;
			QueueDragGhostLabelProps.m_DisallowNewline = true;
			QueueDragGhostLabelProps.m_StopAtEnd = true;
			QueueDragGhostLabelProps.m_MinimumFontSize = 6.0f;
			Ui()->DoLabel(&QueueDragGhostLabel, aGhostLabel, BodySize, TEXTALIGN_ML, QueueDragGhostLabelProps);
		}

		if(s_QueueDragIndex >= 0 && Ui()->MouseButton(0))
		{
			if(!s_QueueDragging && distance(Ui()->MousePos(), s_QueueDragStart) > 5.0f)
			{
				s_QueueDragging = true;
			}
		}
		else if(s_QueueDragIndex >= 0 && !Ui()->MouseButton(0))
		{
			if(s_QueueDragging && DragTarget >= 0 && DragTarget != s_QueueDragIndex)
			{
				GameClient()->m_Skins.MoveActiveSkinQueueItem((size_t)s_QueueDragIndex, (size_t)DragTarget, QueueDummy);
			}
			else if(!s_QueueDragging)
			{
				ApplyQueueIndex = s_QueueDragIndex;
			}
			s_QueueDragIndex = -1;
			s_QueueDragging = false;
		}

		if(RemoveIndex >= 0 && RemoveIndex < (int)SkinQueue.size())
		{
			GameClient()->m_Skins.RemoveActiveSkinQueue(SkinQueue[RemoveIndex], QueueDummy);
			s_QueueDragIndex = -1;
			s_QueueDragging = false;
		}
		else if(ApplyQueueIndex >= 0 && ApplyQueueIndex < (int)SkinQueue.size())
		{
			CommitSettingsTeeSkinEdits();
			GameClient()->m_Skins.ApplySkinQueueIndex((size_t)ApplyQueueIndex, QueueDummy);
			GameClient()->m_Skins.RecordRecentSkin(QueueDummy);
		}
	}

	if(QueuePresets.h > 0.0f)
	{
		CUIRect PresetHeader, PresetList;
		QueuePresets.HSplitTop(TeeMetrics.m_LineHeight, &PresetHeader, &QueuePresets);
		const auto PresetHeaderActions = ResolveSettingsInputActionsLayout(PresetHeader, 4, TeeMetrics);
		PresetHeader = PresetHeaderActions.m_Input;
		char aPresetLabel[128];
		str_format(aPresetLabel, sizeof(aPresetLabel), "%s (%d)", Localize("Preset bar"), (int)vQueuePresets.size());
		SLabelProperties PresetLabelProps;
		PresetLabelProps.m_DisallowNewline = true;
		PresetLabelProps.m_StopAtEnd = true;
		PresetLabelProps.m_MinimumFontSize = 6.0f;
		PresetLabelProps.m_MaxWidth = PresetHeader.w;
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_queue_presets_label", &PresetHeader, aPresetLabel, BodySize, TEXTALIGN_ML, PresetLabelProps);
		CUIRect SaveButton, SaveAsButton, RenamePresetButton, RemovePresetButton;
		SaveButton = PresetHeaderActions.m_Actions.m_aButtons[0];
		SaveAsButton = PresetHeaderActions.m_Actions.m_aButtons[1];
		RenamePresetButton = PresetHeaderActions.m_Actions.m_aButtons[2];
		RemovePresetButton = PresetHeaderActions.m_Actions.m_aButtons[3];
		const bool HasAppliedPreset = ActivePresetIndex >= 0 && (size_t)ActivePresetIndex < vQueuePresets.size();
		const bool CanSavePreset = HasAppliedPreset && ActivePresetIndex == AppliedPresetIndex && ActivePresetIndex != (int)CSkins::SKIN_QUEUE_SERVER_PRESET && QueueDirty;
		const bool CanSaveAsPreset = !SkinQueue.empty();
		const bool CanRenamePreset = HasAppliedPreset && ActivePresetIndex != (int)CSkins::SKIN_QUEUE_SERVER_PRESET;
		const bool CanRemovePreset = HasAppliedPreset && (size_t)ActivePresetIndex >= 2;
		static CButtonContainer s_SavePresetButton;
		if(Ui()->DoButton_QmIcon(&s_SavePresetButton, EQmIcon::CHECK, "\xEE\x86\x82", CanSavePreset ? 0 : -1, &SaveButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanSavePreset)
		{
			GameClient()->m_Skins.SaveSkinQueueToAppliedPreset(QueueDummy);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SavePresetButton, &SaveButton, CanSavePreset ? Localize("Save changes back to this preset") : Localize("Apply a writable preset and edit first"));
		static CButtonContainer s_SaveAsPresetButton;
		if(Ui()->DoButton_QmIcon(&s_SaveAsPresetButton, EQmIcon::COPY, FONT_ICON_COPY, CanSaveAsPreset ? 0 : -1, &SaveAsButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanSaveAsPreset)
		{
			if(GameClient()->m_Skins.AddSkinQueuePresetFromCurrent(QueueDummy))
				SelectedPresetIndex = GameClient()->m_Skins.AppliedSkinQueuePresetIndex(QueueDummy);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SaveAsPresetButton, &SaveAsButton, CanSaveAsPreset ? Localize("Save current queue as a new preset") : Localize("Queue is empty"));
		static CButtonContainer s_RenameSelectedPresetButton;
		static CButtonContainer s_RemoveSelectedPresetButton;
		int RenamePresetIndex = -1;
		int RemovePresetIndex = -1;
		if(Ui()->DoButton_QmIcon(&s_RenameSelectedPresetButton, EQmIcon::PEN_TO_SQUARE, FONT_ICON_PEN_TO_SQUARE, CanRenamePreset ? 0 : -1, &RenamePresetButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanRenamePreset)
		{
			RenamePresetIndex = ActivePresetIndex;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_RenameSelectedPresetButton, &RenamePresetButton, CanRenamePreset ? Localize("Open rename dialog") : Localize("Apply a preset first"));
		if(Ui()->DoButton_QmIcon(&s_RemoveSelectedPresetButton, EQmIcon::TRASH, FONT_ICON_TRASH, CanRemovePreset ? 0 : -1, &RemovePresetButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL) && CanRemovePreset)
		{
			RemovePresetIndex = ActivePresetIndex;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_RemoveSelectedPresetButton, &RemovePresetButton, CanRemovePreset ? Localize("Delete this preset") : Localize("Apply a preset first"));

		QueuePresets.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueuePresets);
		CUIRect ApplyRow, ApplyPlayer, ApplyDummy;
		QueuePresets.HSplitTop(TeeMetrics.m_ButtonHeight, &ApplyRow, &QueuePresets);
		QueuePresets.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueuePresets);
		static ui_widget::SSettingsActionGroupState s_QueueApplyWidths;
		const char *apApplyLabels[] = {Localize("Apply to player"), Localize("Apply to dummy")};
		const auto ApplyActions = ui_widget::PrepareSettingsActionGroup(*Ui(), s_QueueApplyWidths, ApplyRow, apApplyLabels, std::size(apApplyLabels), TeeMetrics);
		ApplyPlayer = ApplyActions.m_aButtons[0];
		ApplyDummy = ApplyActions.m_aButtons[1];
		static CButtonContainer s_aApplyQueuePresetButtons[NUM_DUMMIES];
		int ApplyPresetTarget = -1;
		if(DoSettingsButton_Menu(SETTINGS_TEE, -1, -1, &s_aApplyQueuePresetButtons[0], "tee-queue-apply-player", Localize("Apply to player"), HasAppliedPreset ? 0 : -1, &ApplyPlayer, TeeMetrics) && HasAppliedPreset)
			ApplyPresetTarget = 0;
		if(DoSettingsButton_Menu(SETTINGS_TEE, -1, -1, &s_aApplyQueuePresetButtons[1], "tee-queue-apply-dummy", Localize("Apply to dummy"), HasAppliedPreset ? 0 : -1, &ApplyDummy, TeeMetrics) && HasAppliedPreset)
			ApplyPresetTarget = 1;
		PresetList = QueuePresets;
		if(vQueuePresets.empty())
		{
			DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee_no_presets_label", &PresetList, Localize("No presets yet"), BodySize, TEXTALIGN_MC);
		}
		else
		{
			static CListBox s_PresetListBox;
			static std::vector<char> s_vPresetItemIds;
			s_vPresetItemIds.resize(vQueuePresets.size());

			int SelectPresetIndex = -1;
			const int PresetSelectedOld = ActivePresetIndex >= 0 ? ActivePresetIndex : -1;
			s_PresetListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			// 统一行外观：胶囊整行 + token 行距（预设栏原本就用这半档行距）+ 按字号
			// 派生的行内内边距，可见 2~3 行，滚动条覆盖在行右侧不占行宽。
			s_PresetListBox.SetCapsuleRows(BodySize);
			// 容器内边距：预设栏高度同样在测高处（ResolveSettingsTeeQueuePresetHeight）按
			// PaddedRowsHeight 计入两档留白，这里声明同一档。
			s_PresetListBox.SetContainerPadding(ui_token::list::ContainerPadding(TeeMetrics.m_ListRowHeight));
			s_PresetListBox.SetVisibleRowRange(ui_token::list::PRESET_MIN_VISIBLE_ROWS, ui_token::list::PRESET_MAX_VISIBLE_ROWS);
			s_PresetListBox.DoStart(TeeMetrics.m_ListRowHeight, (int)vQueuePresets.size(), 1, 1, PresetSelectedOld, &PresetList, true, IGraphics::CORNER_ALL);
			for(size_t i = 0; i < vQueuePresets.size(); ++i)
			{
				const CListboxItem Item = s_PresetListBox.DoNextItem(&s_vPresetItemIds[i], ActivePresetIndex == (int)i);
				if(!Item.m_Visible)
					continue;

				// 提示框仍贴整行；文字排进已内退的内容区，不再自己补左缩进。
				CUIRect SelectRect = Item.m_Rect;
				CUIRect NameRect = Item.m_ContentRect;

				char aEntryLabel[96];
				if(GameClient()->m_Skins.IsBuiltInSkinQueuePreset(i))
				{
					str_format(aEntryLabel, sizeof(aEntryLabel), "%s (%d)", PresetDisplayName(i), (int)vQueuePresets[i].m_Queue.size());
				}
				else
				{
					str_format(aEntryLabel, sizeof(aEntryLabel), "%s (%d)", vQueuePresets[i].m_Name.c_str(), (int)vQueuePresets[i].m_Queue.size());
				}
				SLabelProperties PresetNameProps;
				PresetNameProps.m_MaxWidth = NameRect.w;
				PresetNameProps.m_DisallowNewline = true;
				PresetNameProps.m_StopAtEnd = true;
				PresetNameProps.m_MinimumFontSize = 6.0f;
				Ui()->DoLabel(&NameRect, aEntryLabel, BodySize, TEXTALIGN_ML, PresetNameProps);

				const char *pPresetTooltip = nullptr;
				if(i == CSkins::SKIN_QUEUE_SERVER_PRESET)
				{
					pPresetTooltip = Localize("Rotate all server player skins");
				}
				else if(GameClient()->m_Skins.IsBuiltInSkinQueuePreset(i))
				{
					pPresetTooltip = Localize("Default preset");
				}
				else
				{
					pPresetTooltip = Localize("Select a preset, then apply it to player or dummy");
				}
				GameClient()->m_Tooltips.DoToolTip(&s_vPresetItemIds[i], &SelectRect, pPresetTooltip);
			}
			const int PresetListSelectedIndex = s_PresetListBox.DoEnd();
			if(s_PresetListBox.WasItemSelected())
			{
				SelectPresetIndex = PresetListSelectedIndex;
			}

			if(RenamePresetIndex >= 0 && (size_t)RenamePresetIndex < vQueuePresets.size())
			{
				m_SkinQueuePresetRenamePopupContext.m_pMenus = this;
				m_SkinQueuePresetRenamePopupContext.m_Dummy = QueueDummy;
				m_SkinQueuePresetRenamePopupContext.m_PresetIndex = RenamePresetIndex;
				m_SkinQueuePresetRenamePopupContext.m_NameInput.Set(vQueuePresets[RenamePresetIndex].m_Name.c_str());
				m_SkinQueuePresetRenamePopupContext.m_NameInput.SelectAll();
				auto Props = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
				Props.m_OnClosed = [this] { Ui()->ReleaseActiveTextInput(&m_SkinQueuePresetRenamePopupContext.m_NameInput); };
				Ui()->DoPopupMenu(&m_SkinQueuePresetRenamePopupContext, Ui()->MouseX(), Ui()->MouseY(), 260.0f, 72.0f, &m_SkinQueuePresetRenamePopupContext, PopupSkinQueuePresetRename, Props);
			}
			else if(SelectPresetIndex >= 0)
			{
				SelectedPresetIndex = SelectPresetIndex;
			}
			else if(RemovePresetIndex >= 0)
			{
				GameClient()->m_Skins.RemoveSkinQueuePreset((size_t)RemovePresetIndex, QueueDummy);
				for(int &Index : gs_TeeSettingsPageState.m_aSelectedQueuePreset)
					Index = CSkins::NextAppliedPresetIndexAfterRemove(Index, RemovePresetIndex);
			}
			else if(ApplyPresetTarget >= 0)
			{
				CommitSettingsTeeSkinEdits();
				const SQmRecentTeeSkin Before = QmCurrentTeeSkin(g_Config, ApplyPresetTarget != 0);
				GameClient()->m_Skins.ApplySkinQueuePreset((size_t)ActivePresetIndex, ApplyPresetTarget);
				gs_TeeSettingsPageState.m_aSelectedQueuePreset[ApplyPresetTarget] = ActivePresetIndex;
				if(!(Before == QmCurrentTeeSkin(g_Config, ApplyPresetTarget != 0)))
					GameClient()->m_Skins.RecordRecentSkin(ApplyPresetTarget);
			}
		}
	}
	if(RequestedTarget != QueueDummy && !Ui()->RenderOnly())
	{
		CommitSettingsTeeSkinEdits();
		m_Dummy = RequestedTarget != 0;
		m_SkinListScrollToSelected = false;
	}
}

void CMenus::RenderSettingsTeeGlow(CUIRect Content, const SSettingsContentMetrics &TeeMetrics)
{
	const float BodySize = TeeMetrics.m_BodySize;
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect TeeGlowRow;
	if(Flow.Control("tee-team-glow", TeeMetrics.m_LineHeight, TeeGlowRow))
	{
		if(DoSettingsButton_CheckBox(SETTINGS_TEE, -1, &g_Config.m_QmTeamTeeGlow, "tee-team-glow", Localize("Team tee glow"), g_Config.m_QmTeamTeeGlow, &TeeGlowRow) && Flow.Input() && !Ui()->RenderOnly())
			g_Config.m_QmTeamTeeGlow ^= 1;
	}
	if(Flow.Control("tee-team-glow-team0-mode", TeeMetrics.m_LineHeight, TeeGlowRow, ui_token::settings::RowGap(TeeMetrics.m_LineSpacing)))
	{
		CUIRect Team0GlowLabel, Team0GlowDropDown;
		// 配置下拉沿用标准短选项宽度并贴齐行尾，避免多选颜色名把按钮撑满整行。
		SplitSettingsControlRow(TeeGlowRow, TeeMetrics, &Team0GlowLabel, &Team0GlowDropDown, ESettingsControlWidth::SHORT_CHOICE);
		Team0GlowDropDown.x = TeeGlowRow.x + TeeGlowRow.w - Team0GlowDropDown.w;
		DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-team-glow-team0-label", &Team0GlowLabel, Localize("Team 0 glow"), BodySize, TEXTALIGN_ML);
		const char *apTeam0GlowModes[] = {Localize("Off"), Localize("Tee color"), Localize("Custom color"), Localize("Rainbow")};
		static CUi::SDropDownState s_Team0GlowModeDropDownState;
		const int Team0ModeNew = DoSettingsDropDown(&Team0GlowDropDown, std::clamp(g_Config.m_QmTeamTeeGlowTeam0Mode, 0, 3), apTeam0GlowModes, std::size(apTeam0GlowModes), s_Team0GlowModeDropDownState, {}, &g_Config.m_QmTeamTeeGlowTeam0Mode, nullptr, &TeeGlowRow);
		if(Flow.Input() && !Ui()->RenderOnly() && Team0ModeNew != g_Config.m_QmTeamTeeGlowTeam0Mode)
			g_Config.m_QmTeamTeeGlowTeam0Mode = Team0ModeNew;
	}
	Flow.Space(ui_token::settings::RowGap(TeeMetrics.m_LineSpacing));
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "deck:tee-glow", TeeMetrics, &SectionReveal))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	const auto Slider = [&](const char *pId, const char *pLabel, int *pValue) {
		if(Flow.Control(pId, TeeMetrics.m_LineHeight, TeeGlowRow, ui_token::settings::RowGap(TeeMetrics.m_LineSpacing)))
			DoSettingsScrollbarOption(SETTINGS_TEE, -1, pId, pValue, pValue, &TeeGlowRow, pLabel, 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	};
	Slider("tee-team-glow-strength", Localize("Team tee glow intensity"), &g_Config.m_QmTeamTeeGlowStrength);
	Slider("tee-team-glow-size", Localize("Team tee glow halo size"), &g_Config.m_QmTeamTeeGlowSize);
	static CSettingsConditionalRows s_TeamTeeGlowTeam0Rows;
	if(s_TeamTeeGlowTeam0Rows.Update(g_Config.m_QmTeamTeeGlowTeam0Mode == 2, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope TeamTeeGlowTeam0RevealScope(Ui(), Flow, s_TeamTeeGlowTeam0Rows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("tee-team-glow-team0-color", TeeMetrics.m_ButtonHeight, TeeGlowRow, ui_token::settings::RowGap(TeeMetrics.m_LineSpacing)))
		{
			static CButtonContainer s_Team0GlowColorId;
			DoLine_ColorPicker(&s_Team0GlowColorId, TeeMetrics, &TeeGlowRow, Localize("Team 0 glow color"), &g_Config.m_QmTeamTeeGlowColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTeamTeeGlowColor)), false, nullptr, false, false);
		}
	}
}

void CMenus::RefreshSettingsTeeSkins(int RefreshVisibleRows, const char *aRefreshFirstVisibleSkin)
{
	if(Ui()->RenderOnly())
		return;
	const int64_t RefreshNowNs = time_get_nanoseconds().count();
	if(gs_TeeListDrainPerfSession.m_Active)
		LogTeeListDrainSummary(Client(), GameClient()->m_Skins, GameClient()->m_Skins.LoadingStats(), false, RefreshNowNs);
	BeginTeeListDrainPerfSession(GameClient()->m_Skins, RefreshNowNs);
	gs_TeeSettingsPageState.m_TeeFirstVisibleReadyLogged = false;
	gs_TeeSettingsPageState.m_TeeAllVisibleReadyLogged = false;
	gs_TeeSettingsPageState.m_TeeFullListReadyLogged = false;
	gs_TeeSettingsPageState.m_TeeRefreshInProgress = true;
	gs_TeeSettingsPageState.m_TeeRefreshStartNs = RefreshNowNs;
	char aPayload[192];
	str_format(aPayload, sizeof(aPayload), "event=tee_refresh_begin visible_rows=%d first_visible_skin=%s",
		RefreshVisibleRows, aRefreshFirstVisibleSkin);
	QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings:tee");
	ClearSettingsTeePreviewCache();
	GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
}

namespace qm_card_catalog
{
	bool QmCardRenderHook::BuildTeeCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr || pStableId == nullptr)
			return false;
		if(str_comp(pStableId, "qm:skin_appearance") == 0)
			return BuildSkinCard(Ctx, qm_module::EQmModuleId::SkinAppearance, Out);
		const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(pStableId);
		if(pDefault == nullptr)
			return false;
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		FSettingsCardMeasure MeasureBlock;
		Out = {};
		Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		if(str_comp(pStableId, "deck:tee-identity") == 0)
		{
			MeasureBlock = [pMenus, Metrics](float Width) {
				const bool Custom = (pMenus->m_Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor) != 0;
				return ResolveSettingsTeeEditorLayout({0.0f, 0.0f, Width, 0.0f}, Metrics, Custom).m_Height;
			};
			if(!Ctx.m_ReadOnly)
				Out.m_PreLayoutInput = [pMenus, Metrics](CUIRect Content) { return pMenus->ProcessSettingsTeeEditorInput(Content, Metrics); };
			Out.m_Render = [pMenus, Metrics](CUIRect Content) { pMenus->RenderSettingsTeeEditor(Content, Metrics); };
		}
		else if(str_comp(pStableId, "deck:tee-skin-options") == 0)
		{
			auto pState = std::make_shared<STeeOptionsContentState>();
			Out.m_RenderMeasured = [pMenus, Metrics, pState, ReadOnly = Ctx.m_ReadOnly](CUIRect &Content) {
				auto *pPrevious = s_pTeeOptionsContent;
				s_pTeeOptionsContent = pState.get();
				// 后台只隔离输入；只有测量和预热才抑制真实控件绘制。
				const bool PrewarmOnly = pMenus->Ui()->MeasurementOnly() || pMenus->m_MenuTextPlanCollecting || (ReadOnly && !pMenus->Ui()->BackgroundRender());
				const CUiScopedMeasurement Measurement(pMenus->Ui(), PrewarmOnly);
				pMenus->RenderSettingsTeeOptions(Content, Metrics);
				s_pTeeOptionsContent = pPrevious;
			};
			UseDeclaredModuleContent(Ctx, Out);
			return true;
		}
		else if(str_comp(pStableId, "deck:tee-skin-list") == 0)
		{
			// 工具栏 + 列表容器：容器高度取自与 DoStart 同一个 helper（可见行内容 +
			// 上下两档内边距），测高与传矩形因此永远同源。
			MeasureBlock = [Metrics](float Width) {
				return ResolveSettingsTeeToolbarLayout({0.0f, 0.0f, Width, 0.0f}, Metrics).m_Height + ResolveSettingsTeeSkinListGeometry(Metrics).m_Height;
			};
			Out.m_RenderWhenClipped = true;
			Out.m_Render = [pMenus, Metrics, Viewport = Ctx.m_Page.m_ScrollViewport](CUIRect Content) {
				if(pMenus->Ui()->RenderOnly() || Content.Intersection(Viewport).h > 0.0f)
					pMenus->RenderSettingsTeeSkinList(Content, Metrics);
				else if(g_Config.m_QmSettingsPrewarm != 0)
					pMenus->AdvanceSettingsTeeSkinListOffscreen();
			};
		}
		else if(str_comp(pStableId, "deck:tee-skin-queue") == 0)
		{
			MeasureBlock = [pMenus, Metrics](float Width) {
				const CSkins &Skins = pMenus->GameClient()->m_Skins;
				return ResolveSettingsTeeQueuePanelHeight(Metrics, static_cast<int>(Skins.SkinQueue(pMenus->m_Dummy).size()), static_cast<int>(Skins.SkinQueuePresets(pMenus->m_Dummy).size()), Width);
			};
			Out.m_Render = [pMenus, Metrics](CUIRect Content) { pMenus->RenderSettingsTeeSkinQueue(Content, Metrics); };
		}
		else if(str_comp(pStableId, "deck:tee-glow") == 0)
		{
			Out.m_RenderMeasured = [pMenus, Metrics, ReadOnly = Ctx.m_ReadOnly](CUIRect &Content) {
				// 与皮肤选项共用测量语义，后台保留发光控件的可见绘制。
				const bool PrewarmOnly = pMenus->Ui()->MeasurementOnly() || pMenus->m_MenuTextPlanCollecting || (ReadOnly && !pMenus->Ui()->BackgroundRender());
				const CUiScopedMeasurement Measurement(pMenus->Ui(), PrewarmOnly);
				pMenus->RenderSettingsTeeGlow(Content, Metrics);
			};
			UseDeclaredModuleContent(Ctx, Out);
			return true;
		}
		else
			return false;
		// 专用 Tee 编辑器与网格共用外层布局发布，内部仍由既有纯几何解析器管理。
		qm_settings_content::UseDeclaredBlockContent(Out, Ctx.m_UiContext.m_pUi, QmCardRenderHook::TextRenderer(pMenus), std::move(MeasureBlock));
		return true;
	}
}
