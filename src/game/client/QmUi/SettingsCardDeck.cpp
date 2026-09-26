#include "SettingsCardDeck.h"

#include "QmAnimResolve.h"
#include "UiContext.h"

#include <base/system.h>

#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	bool PointInRect(const CUIRect &Rect, float X, float Y)
	{
		return X >= Rect.x && X <= Rect.x + Rect.w && Y >= Rect.y && Y <= Rect.y + Rect.h;
	}

	void OffsetRectY(CUIRect &Rect, float OffsetY)
	{
		Rect.y += OffsetY;
	}

	uint64_t SettingsCardEntryNodeKey(const char *pTab)
	{
		const uint64_t TabKey = str_quickhash(pTab != nullptr ? pTab : "");
		return BuildUiAnimNodeKey(str_quickhash("settings-card-deck-entry"), TabKey);
	}

	uint64_t SettingsCardReflowNodeKey(const char *pTab, const char *pStableId)
	{
		const uint64_t TabKey = str_quickhash(pTab != nullptr ? pTab : "");
		return BuildUiAnimNodeKey(BuildUiAnimNodeKey(str_quickhash("settings-card-reflow"), TabKey), str_quickhash(pStableId != nullptr ? pStableId : ""));
	}

	uint64_t SettingsCardHeightNodeKey(const char *pTab, const char *pStableId)
	{
		const uint64_t TabKey = str_quickhash(pTab != nullptr ? pTab : "");
		return BuildUiAnimNodeKey(BuildUiAnimNodeKey(str_quickhash("settings-card-height"), TabKey), str_quickhash(pStableId != nullptr ? pStableId : ""));
	}
}

void CSettingsCardDeck::ResetDefinitionViewState()
{
	// Definition state is indexed by the current card-order model. A different
	// tab or stable-id set may reuse the same indices, so retaining these
	// values would make the new view inherit heights, animation tracks and
	// active-column state from the previous view.
	m_vRuntimeStates.clear();
	m_vContentHeights.clear();
	m_vContentWidths.clear();
	m_vMeasureRevisions.clear();
	m_vDefinitionsByState.clear();
	m_vBoundDefinitionStateIndices.clear();
	m_vActiveStateIndices.clear();
	m_vPreviousActiveStateIndices.clear();
	m_vLastRenderedActiveStateIndices.clear();
	m_vPreparedStableIds.clear();
	m_vPreparedCards.clear();
	m_vDragGeometry.clear();
	for(std::vector<int> &vColumn : m_aDragColumns)
		vColumn.clear();
	m_ProjectionCache = {};
	m_Drag.Reset();
	m_HasScrollOffset = false;
	m_LastScrollOffsetY = 0.0f;
	m_LastViewportHeight = -1.0f;
	m_PreparedDefinitionModelCount = -1;
	m_PreparedDefinitionStateIndexRevision = UINT64_MAX;
	m_pPreparedDefinitionData = nullptr;
	m_PreparedDefinitionCount = 0;
	m_PreparedDefinitionTab.clear();
	m_FrameRuntime.OnTabChanged();
	m_SuppressHoverFeedbackOnce = true;
}

void CSettingsCardDeck::PrepareDefinitions(const std::vector<SSettingsCardDefinition> &vCards, const qm_card_order::CModel &Model)
{
	if(m_vDefinitionsByState.size() != (size_t)Model.Count())
	{
		m_vDefinitionsByState.assign(Model.Count(), nullptr);
		m_vBoundDefinitionStateIndices.clear();
	}
	else
	{
		for(const int StateIndex : m_vBoundDefinitionStateIndices)
			m_vDefinitionsByState[StateIndex] = nullptr;
		m_vBoundDefinitionStateIndices.clear();
	}
	m_vBoundDefinitionStateIndices.reserve(vCards.size());
	for(const SSettingsCardDefinition &Definition : vCards)
	{
		if(Definition.m_Spec.m_pStableId == nullptr)
			continue;
		const int StateIndex = Model.StateIndexForStableId(Definition.m_Spec.m_pStableId);
		if(StateIndex >= 0)
		{
			m_vDefinitionsByState[StateIndex] = &Definition;
			m_vBoundDefinitionStateIndices.push_back(StateIndex);
		}
	}
	m_vRuntimeStates.resize(Model.Count());
	m_vContentHeights.resize(Model.Count(), -1.0f);
	m_vContentWidths.resize(Model.Count(), -1.0f);
	m_vMeasureRevisions.resize(Model.Count(), UINT64_MAX);
	for(const int StateIndex : m_vBoundDefinitionStateIndices)
	{
		const SSettingsCardDefinition *pDefinition = m_vDefinitionsByState[StateIndex];
		if(pDefinition != nullptr)
			m_vRuntimeStates[StateIndex].m_DefaultCollapsed = SettingsCardDeckLoadCollapsed(m_DefaultCollapsedByStableId, pDefinition->m_Spec.m_pStableId, m_vRuntimeStates[StateIndex].m_DefaultCollapsed);
	}
}

void CSettingsCardDeck::RequestReveal(const char *pStableId)
{
	m_PendingRevealStableId = pStableId != nullptr ? pStableId : "";
}

void CSettingsCardDeck::BeginDisplayCycle(uint64_t DisplayCycle, bool AnimateEntry)
{
	if(m_FrameRuntime.BeginDisplayCycle(DisplayCycle, AnimateEntry))
	{
		m_Drag.Reset();
		m_SuppressHoverFeedbackOnce = true;
		m_HasScrollOffset = false;
		m_vLastRenderedActiveStateIndices.clear();
		m_FrameRuntime.SetEntryActive(false);
		for(SRuntimeState &Runtime : m_vRuntimeStates)
		{
			Runtime.m_ReflowInitialized = false;
			Runtime.m_ReflowWasActive = false;
			Runtime.m_ContentHeightInitialized = false;
			Runtime.m_ContentHeightWasActive = false;
			Runtime.m_CollapsedInitialized = false;
			Runtime.m_PointerInsideLastFrame = false;
			Runtime.m_SubtitleMotionWasActive = false;
			Runtime.m_SubtitleVisibleDuringMotion = false;
			Runtime.m_MotionGeometryInitialized = false;
			Runtime.m_LastDrawOffsetX = 0.0f;
			Runtime.m_LastDrawOffsetY = 0.0f;
		}
	}
}

SSettingsCardDeckResult CSettingsCardDeck::Render(const IUiContext &Ctx, const SSettingsPageLayoutFrame &Layout, const char *pTab, const std::vector<SSettingsCardDefinition> &vCards, qm_card_order::CModel &Model, CScrollRegion *pScrollRegion, const SSettingsCardDeckInput &Input, const SCardMotionSpec &Motion, const SSettingsCardDeckVisualOptions &VisualOptions)
{
	return RenderInternal(Ctx, Layout, pTab, vCards, Model, pScrollRegion, Input, Motion, VisualOptions, true);
}

SSettingsCardDeckResult CSettingsCardDeck::RenderInternal(const IUiContext &Ctx, const SSettingsPageLayoutFrame &Layout, const char *pTab, const std::vector<SSettingsCardDefinition> &vCards, qm_card_order::CModel &Model, CScrollRegion *pScrollRegion, const SSettingsCardDeckInput &Input, const SCardMotionSpec &Motion, const SSettingsCardDeckVisualOptions &VisualOptions, bool PersistentDefinitions)
{
	SSettingsCardDeckResult Result;
	m_FrameRuntime.BeginFrame(Input.m_pDiagnostics);
	if(pTab == nullptr)
		return Result;
	const bool TabChanged = m_LastRenderedTab != pTab;
	if(TabChanged)
	{
		m_LastRenderedTab = pTab;
		m_FrameRuntime.OnTabChanged(Motion.m_ContinuousEntry);
		m_SuppressHoverFeedbackOnce = true;
	}

	bool StableIdsChanged = m_vPreparedStableIds.size() != vCards.size();
	if(!StableIdsChanged)
	{
		for(size_t i = 0; i < vCards.size(); ++i)
		{
			const char *pPreparedStableId = m_vPreparedStableIds[i] != nullptr ? m_vPreparedStableIds[i] : "";
			const char *pCurrentStableId = vCards[i].m_Spec.m_pStableId != nullptr ? vCards[i].m_Spec.m_pStableId : "";
			if(str_comp(pPreparedStableId, pCurrentStableId) != 0)
			{
				StableIdsChanged = true;
				break;
			}
		}
	}
	const bool ModelCountChanged = m_PreparedDefinitionModelCount != Model.Count();
	const bool StateIndexChanged = m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision();
	if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)
		ResetDefinitionViewState();
	if(!PersistentDefinitions || m_CachedDefinitionsDirty || m_PreparedDefinitionModelCount != Model.Count() || m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision() || m_pPreparedDefinitionData != vCards.data() || m_PreparedDefinitionCount != vCards.size() || m_PreparedDefinitionTab != pTab || StableIdsChanged)
	{
		if(Input.m_pDiagnostics != nullptr)
			m_FrameRuntime.CountDefinitionsPrepare();
		PrepareDefinitions(vCards, Model);
		if(m_InvalidateMeasurementsOnDefinitionsPrepare)
		{
			// Definitions revision 表示测量闭包或其依赖状态已经改变。同 stable ID
			// 不能继续复用旧高度，否则动态行会被旧卡片矩形裁掉。
			std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f);
			std::fill(m_vMeasureRevisions.begin(), m_vMeasureRevisions.end(), UINT64_MAX);
			m_InvalidateMeasurementsOnDefinitionsPrepare = false;
		}
		m_CachedDefinitionsDirty = false;
		m_PreparedDefinitionModelCount = Model.Count();
		m_PreparedDefinitionStateIndexRevision = Model.StateIndexRevision();
		m_pPreparedDefinitionData = vCards.data();
		m_PreparedDefinitionCount = vCards.size();
		m_PreparedDefinitionTab = pTab;
		m_vPreparedStableIds.resize(vCards.size());
		for(size_t i = 0; i < vCards.size(); ++i)
			m_vPreparedStableIds[i] = vCards[i].m_Spec.m_pStableId;
	}
	auto RebuildActiveStateIndices = [&]() {
		m_vActiveStateIndices.clear();
		m_vActiveStateIndices.reserve(vCards.size());
		for(const int StateIndex : m_vBoundDefinitionStateIndices)
		{
			const SSettingsCardDefinition *pDefinition = m_vDefinitionsByState[StateIndex];
			if(pDefinition != nullptr && (!pDefinition->m_IsVisible || pDefinition->m_IsVisible()))
				m_vActiveStateIndices.push_back(StateIndex);
		}
	};

	// 有真实滚动容器时由 Begin 扣除固定槽位；预热阶段没有容器，直接使用壳层提供的有效 viewport。
	CUIRect ScrollViewport = pScrollRegion != nullptr ? Layout.m_UnreservedScrollViewport : Layout.m_ScrollViewport;
	vec2 ScrollOffset(0.0f, 0.0f);
	if(pScrollRegion != nullptr)
		pScrollRegion->Begin(&ScrollViewport, &ScrollOffset, Input.m_pScrollParams);
	if(std::abs(m_LastViewportHeight - ScrollViewport.h) > 0.01f)
	{
		m_LastViewportHeight = ScrollViewport.h;
		std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f);
	}
	bool ScrollMovedThisFrame = false;
	if(pScrollRegion != nullptr)
	{
		ScrollMovedThisFrame = SettingsCardDeckScrollMoved(m_HasScrollOffset, m_LastScrollOffsetY, ScrollOffset.y);
		m_LastScrollOffsetY = ScrollOffset.y;
		m_HasScrollOffset = true;
	}

	SSettingsPageLayoutFrame DrawLayout = ResolveSettingsPageLayoutForScrollViewport(Layout, ScrollViewport, Ctx.m_UiScale);
	OffsetRectY(DrawLayout.m_ContentViewport, ScrollOffset.y);
	OffsetRectY(DrawLayout.m_aColumns[0], ScrollOffset.y);
	OffsetRectY(DrawLayout.m_aColumns[1], ScrollOffset.y);

	bool MeasuredGeometryChanged = false;
	bool ContentHeightTargetChanged = false;
	bool ContentHeightAnimationActive = false;
	auto BuildPreparedCards = [&](const std::array<std::vector<int>, 3> &aDisplayColumns) {
		m_vPreparedCards.clear();
		m_vPreparedCards.reserve(m_vActiveStateIndices.size());
		auto AppendCard = [&](int StateIndex, int Column, CUIRect ColumnRect, CSettingsCardColumnFramePlan &ColumnPlan) {
			if(StateIndex < 0 || StateIndex >= (int)m_vDefinitionsByState.size())
				return;
			const SSettingsCardDefinition *pDefinition = m_vDefinitionsByState[StateIndex];
			if(pDefinition == nullptr)
				return;
			CUIRect Slot{ColumnRect.x, ColumnPlan.CursorY(), ColumnRect.w, 0.0f};
			float &CachedContentHeight = m_vContentHeights[StateIndex];
			float &CachedContentWidth = m_vContentWidths[StateIndex];
			uint64_t &CachedMeasureRevision = m_vMeasureRevisions[StateIndex];
			const float PreviousContentHeight = CachedContentHeight;
			SRuntimeState &Runtime = m_vRuntimeStates[StateIndex];
			const bool HasCustomCollapsedState = static_cast<bool>(pDefinition->m_IsCollapsed);
			const bool Collapsed = SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed);
			const float ContentWidth = std::max(0.0f, Slot.w - 2.0f * ui_token::settings::CARD_PADDING * (Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f));
			if(std::abs(CachedContentWidth - ContentWidth) > 0.01f)
			{
				MeasuredGeometryChanged = MeasuredGeometryChanged || PreviousContentHeight >= 0.0f;
				CachedContentWidth = ContentWidth;
				CachedContentHeight = -1.0f;
			}
			if(CachedMeasureRevision != pDefinition->m_MeasureRevision)
			{
				CachedMeasureRevision = pDefinition->m_MeasureRevision;
				CachedContentHeight = -1.0f;
			}
			if(SettingsCardDeckNeedsContentMeasure(Collapsed, pDefinition->m_MeasureEachFrame, CachedContentHeight))
			{
				if(Input.m_pDiagnostics != nullptr)
					m_FrameRuntime.CountMeasure();
				CachedContentHeight = pDefinition->m_Measure ? std::max(0.0f, pDefinition->m_Measure(ContentWidth)) : 0.0f;
				MeasuredGeometryChanged = MeasuredGeometryChanged || SettingsCardDeckContentHeightChanged(PreviousContentHeight, CachedContentHeight);
			}
			const float TargetContentHeight = Collapsed ? 0.0f : std::max(0.0f, CachedContentHeight);
			const bool HeightInitializedThisFrame = !Runtime.m_ContentHeightInitialized;
			const bool HeightTargetChanged = Runtime.m_ContentHeightInitialized && std::abs(Runtime.m_LastContentHeightTarget - TargetContentHeight) > 0.01f;
			const SSettingsCardHeightAnimationWork HeightWork = ResolveSettingsCardHeightAnimationWork(HeightInitializedThisFrame, HeightTargetChanged, Runtime.m_ContentHeightWasActive, Motion.m_ContentHeightDuration, m_Drag.Active() || Ctx.m_pAnim == nullptr);
			if(HeightInitializedThisFrame)
			{
				if(Input.m_pDiagnostics != nullptr)
					m_FrameRuntime.MarkFirstLayout();
				Runtime.m_ContentHeightInitialized = true;
				Runtime.m_AnimatedContentHeight = TargetContentHeight;
				if(Ctx.m_pAnim != nullptr)
					Ctx.m_pAnim->SetValue(SettingsCardHeightNodeKey(pTab, pDefinition->m_Spec.m_pStableId), EUiAnimProperty::HEIGHT, TargetContentHeight);
			}
			else if(HeightWork.m_ResolveHeight)
			{
				if(Input.m_pDiagnostics != nullptr)
					m_FrameRuntime.CountHeightAnimationResolve();
				const uint64_t HeightKey = SettingsCardHeightNodeKey(pTab, pDefinition->m_Spec.m_pStableId);
				Runtime.m_AnimatedContentHeight = ResolveUiAnimValue(*Ctx.m_pAnim, HeightKey, EUiAnimProperty::HEIGHT, TargetContentHeight, Motion.m_ContentHeightDuration, EEasing::EASE_OUT);
				Runtime.m_ContentHeightWasActive = Ctx.m_pAnim->HasActiveAnimation(HeightKey, EUiAnimProperty::HEIGHT);
			}
			else if(HeightWork.m_SetHeightTarget)
			{
				Runtime.m_AnimatedContentHeight = TargetContentHeight;
				if(Ctx.m_pAnim != nullptr)
					Ctx.m_pAnim->SetValue(SettingsCardHeightNodeKey(pTab, pDefinition->m_Spec.m_pStableId), EUiAnimProperty::HEIGHT, TargetContentHeight);
				Runtime.m_ContentHeightWasActive = false;
			}
			Runtime.m_LastContentHeightTarget = TargetContentHeight;
			ContentHeightTargetChanged = ContentHeightTargetChanged || HeightTargetChanged;
			ContentHeightAnimationActive = ContentHeightAnimationActive || Runtime.m_ContentHeightWasActive;
			const float ContentHeight = std::max(0.0f, Runtime.m_AnimatedContentHeight);
			const SSettingsCardFrame Frame = BuildSettingsCardFrame(Slot, pDefinition->m_Spec, ContentHeight, Ctx.m_UiScale);
			m_vPreparedCards.push_back({pDefinition, StateIndex, Column, Frame, TargetContentHeight, HeightInitializedThisFrame, Runtime.m_ContentHeightWasActive});
			ColumnPlan.Append(Frame.m_Rect.h);
		};
		auto AppendColumn = [&](const std::vector<int> &vStateIndices, int Column, CUIRect ColumnRect, CSettingsCardColumnFramePlan &ColumnPlan) {
			for(const int StateIndex : vStateIndices)
				AppendCard(StateIndex, Column, ColumnRect, ColumnPlan);
		};

		if(DrawLayout.m_TwoColumns && !aDisplayColumns[0].empty())
		{
			const size_t NumLayers = std::max({aDisplayColumns[0].size(), aDisplayColumns[1].size(), aDisplayColumns[2].size()});
			CSettingsCardColumnFramePlan LeftPlan(DrawLayout.m_aColumns[0].y, DrawLayout.m_CardGap);
			CSettingsCardColumnFramePlan RightPlan(DrawLayout.m_aColumns[1].y, DrawLayout.m_CardGap);
			for(size_t Layer = 0; Layer < NumLayers; ++Layer)
			{
				if(Layer < aDisplayColumns[1].size())
					AppendCard(aDisplayColumns[1][Layer], 1, DrawLayout.m_aColumns[0], LeftPlan);
				if(Layer < aDisplayColumns[2].size())
					AppendCard(aDisplayColumns[2][Layer], 2, DrawLayout.m_aColumns[1], RightPlan);

				if(Layer < aDisplayColumns[0].size())
				{
					CSettingsCardColumnFramePlan FullPlan(std::max(LeftPlan.CursorY(), RightPlan.CursorY()), DrawLayout.m_CardGap);
					AppendCard(aDisplayColumns[0][Layer], 0, DrawLayout.m_ContentViewport, FullPlan);
					LeftPlan.SetCursorY(FullPlan.CursorY());
					RightPlan.SetCursorY(FullPlan.CursorY());
				}
			}
		}
		else if(DrawLayout.m_TwoColumns)
		{
			CSettingsCardColumnFramePlan LeftPlan(DrawLayout.m_aColumns[0].y, DrawLayout.m_CardGap);
			CSettingsCardColumnFramePlan RightPlan(DrawLayout.m_aColumns[1].y, DrawLayout.m_CardGap);
			AppendColumn(aDisplayColumns[1], 1, DrawLayout.m_aColumns[0], LeftPlan);
			AppendColumn(aDisplayColumns[2], 2, DrawLayout.m_aColumns[1], RightPlan);
		}
		else
		{
			CSettingsCardColumnFramePlan ColumnPlan(DrawLayout.m_ContentViewport.y, DrawLayout.m_CardGap);
			ForEachSettingsCardDeckVisualOrder(aDisplayColumns, [&](int StateIndex, int Column) {
				AppendCard(StateIndex, Column, DrawLayout.m_ContentViewport, ColumnPlan);
			});
		}
	};

	RebuildActiveStateIndices();
	bool GeometryStateChanged = m_vActiveStateIndices != m_vLastRenderedActiveStateIndices;
	const std::array<std::vector<int>, 3> *pColumns = &m_ProjectionCache.Resolve(Model, pTab, m_vActiveStateIndices);
	BuildPreparedCards(*pColumns);
	GeometryStateChanged = GeometryStateChanged || MeasuredGeometryChanged;

	// 先用当前 active snapshot 的几何处理控制器输入，再为最终 active snapshot 重新计算布局。
	m_vPreviousActiveStateIndices = m_vActiveStateIndices;
	bool PreLayoutGeometryChanged = false;
	std::vector<int> vPreLayoutGeometryChangedStates;
	const bool HasPointerInput = Input.m_MousePressed || Input.m_MouseDown || Input.m_MouseReleased;
	// Header buttons必须每帧运行以先建立 HotItem；否则鼠标首次按下时
	// DoButtonLogic 无法进入 active，释放时也就不会提交点击。
	{
		if(Ctx.m_pUi != nullptr)
			Ctx.m_pUi->BeginPreLayoutInput();
		for(const SPreparedCard &Card : m_vPreparedCards)
		{
			SRuntimeState &Runtime = m_vRuntimeStates[Card.m_StateIndex];
			// 鼠标按下与释放跨帧完成。预布局必须沿用上一帧实际绘制位置，
			// 否则动画中的目标矩形会先清除 active item，正式绘制阶段便无法提交点击。
			const SSettingsCardFrame PreLayoutFrame = ResolveSettingsCardDrawFrame(Card.m_Frame, Runtime.m_LastDrawOffsetX, Runtime.m_LastDrawOffsetY);
			const bool ControllerVisible = pScrollRegion == nullptr || !pScrollRegion->RectClipped(PreLayoutFrame.m_Rect) || Card.m_pDefinition->m_RenderWhenClipped;
			bool CardGeometryChanged = false;
			const bool HasCustomCollapsedState = static_cast<bool>(Card.m_pDefinition->m_IsCollapsed);
			const bool CollapsedBeforeHeader = SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed);
			bool HeaderGeometryChanged = false;
			// ActiveItem 可能在前一张卡片的释放阶段被清除。逐卡读取，避免
			// 已经完成的点击继续驱动后续被裁剪卡片的预布局回调。
			const bool HasActiveHeaderContinuation = SettingsCardDeckHasActiveItemContinuation(HasPointerInput, Ctx.m_pUi != nullptr && Ctx.m_pUi->ActiveItem() != nullptr);
			if((ControllerVisible || HasActiveHeaderContinuation) && Card.m_pDefinition->m_PreLayoutHeaderInput)
			{
				m_FrameRuntime.CountPreLayoutInput();
				HeaderGeometryChanged = Card.m_pDefinition->m_PreLayoutHeaderInput(PreLayoutFrame, CollapsedBeforeHeader);
				CardGeometryChanged = HeaderGeometryChanged;
			}
			else if((ControllerVisible || HasActiveHeaderContinuation) && Ctx.m_pUi != nullptr && !Ctx.m_pUi->RenderOnly() && SettingsCardDeckUsesDefaultCollapseControl(HasCustomCollapsedState, static_cast<bool>(Card.m_pDefinition->m_PreLayoutHeaderInput)) &&
				Ctx.m_pUi->DoButtonLogic(&Runtime.m_DefaultCollapseButtonId, CollapsedBeforeHeader, &PreLayoutFrame.m_HandleRect, BUTTONFLAG_LEFT))
			{
				Runtime.m_DefaultCollapsed = SettingsCardDeckApplyDefaultCollapseToggle(HasCustomCollapsedState, Runtime.m_DefaultCollapsed, true, false);
				SettingsCardDeckStoreCollapsed(m_DefaultCollapsedByStableId, Card.m_pDefinition->m_Spec.m_pStableId, Runtime.m_DefaultCollapsed);
				CardGeometryChanged = true;
				HeaderGeometryChanged = true;
			}
			if(HeaderGeometryChanged)
				Ctx.m_pUi->ClosePopupMenus();

			const bool Collapsed = SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed);
			const bool HasPendingPreLayoutInput = Card.m_pDefinition->m_HasPendingPreLayoutInput && Card.m_pDefinition->m_HasPendingPreLayoutInput();
			const bool HasActiveContentContinuation = SettingsCardDeckHasActiveItemContinuation(HasPointerInput, Ctx.m_pUi != nullptr && Ctx.m_pUi->ActiveItem() != nullptr);
			if(SettingsCardDeckShouldRunPreLayoutInput(HasPointerInput, HasPendingPreLayoutInput, HasActiveContentContinuation, ControllerVisible, Collapsed, PreLayoutFrame.m_ContentRect.h) && Card.m_pDefinition->m_PreLayoutInput)
			{
				m_FrameRuntime.CountPreLayoutInput();
				CardGeometryChanged = Card.m_pDefinition->m_PreLayoutInput(PreLayoutFrame.m_ContentRect) || CardGeometryChanged;
			}
			if(CardGeometryChanged)
			{
				m_vContentHeights[Card.m_StateIndex] = -1.0f;
				vPreLayoutGeometryChangedStates.push_back(Card.m_StateIndex);
				PreLayoutGeometryChanged = true;
			}
		}
		if(Ctx.m_pUi != nullptr)
			Ctx.m_pUi->EndPreLayoutInput();
	}
	RebuildActiveStateIndices();
	if(m_vActiveStateIndices != m_vPreviousActiveStateIndices || PreLayoutGeometryChanged)
	{
		GeometryStateChanged = true;
		if(PreLayoutGeometryChanged)
		{
			// 只失效实际处理了输入的卡片。后续卡片的 Y 会由列布局自然
			// 推进，但它们的内容高度和测量缓存不能因邻卡折叠而被重算。
			for(const int StateIndex : vPreLayoutGeometryChangedStates)
			{
				if(StateIndex >= 0 && StateIndex < (int)m_vContentHeights.size())
					m_vContentHeights[StateIndex] = -1.0f;
			}
		}
		pColumns = &m_ProjectionCache.Resolve(Model, pTab, m_vActiveStateIndices);
		BuildPreparedCards(*pColumns);
	}
	GeometryStateChanged = GeometryStateChanged || MeasuredGeometryChanged;
	for(const SPreparedCard &Card : m_vPreparedCards)
	{
		SRuntimeState &Runtime = m_vRuntimeStates[Card.m_StateIndex];
		const bool HasCustomCollapsedState = static_cast<bool>(Card.m_pDefinition->m_IsCollapsed);
		const bool Collapsed = SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed);
		if(Runtime.m_CollapsedInitialized && Runtime.m_LastCollapsed != Collapsed)
			GeometryStateChanged = true;
		Runtime.m_CollapsedInitialized = true;
		Runtime.m_LastCollapsed = Collapsed;
	}
	if(m_Drag.Active() && std::find(m_vActiveStateIndices.begin(), m_vActiveStateIndices.end(), m_Drag.m_StateIndex) == m_vActiveStateIndices.end())
		m_Drag.Reset();
	if(m_Drag.Active() && !Input.m_MouseDown && !Input.m_MouseReleased)
		m_Drag.Reset();
	bool EntryPending = m_FrameRuntime.AnimateEntry() && m_FrameRuntime.EntryCyclePending();
	bool EntryPositionActive = false;
	float DeckEntryOffsetY = 0.0f;
	bool ReflowTargetChanged = false;
	bool ReflowPositionActive = false;
	// 入场由整个 Deck 共享一个偏移；高度变化按当前动画底边顺排后续卡片，始终保持无重叠几何。
	bool SnapReflow = SettingsCardDeckShouldSnapReflow(GeometryStateChanged, m_Drag.Active()) || ContentHeightTargetChanged || ContentHeightAnimationActive;
	if(Ctx.m_pAnim != nullptr)
	{
		if(Motion.m_ContinuousEntry)
		{
			if(m_FrameRuntime.EntryCyclePending() || m_FrameRuntime.EntryWasActive())
			{
				// 轨道属于可见 Deck，不随分类字符串变化；预热 Deck 使用自己的实例。
				const uint64_t EntryKey = BuildUiAnimNodeKey(str_quickhash("settings-card-deck-continuous-entry"), reinterpret_cast<uintptr_t>(this));
				DeckEntryOffsetY = m_FrameRuntime.ResolveContinuousEntryOffset(*Ctx.m_pAnim, EntryKey, Motion);
				EntryPositionActive = m_FrameRuntime.EntryWasActive();
			}
		}
		else
		{
			uint64_t EntryKey = 0;
			bool HasEntryKey = false;
			const auto ResolveEntryKey = [&]() {
				if(!HasEntryKey)
				{
					EntryKey = SettingsCardEntryNodeKey(pTab);
					HasEntryKey = true;
				}
				return EntryKey;
			};
			if(m_FrameRuntime.ConsumeEntryCycle())
			{
				Ctx.m_pAnim->SetValue(ResolveEntryKey(), EUiAnimProperty::POS_Y, m_FrameRuntime.AnimateEntry() ? Motion.m_EntryDistance : 0.0f);
				m_FrameRuntime.SetEntryActive(m_FrameRuntime.AnimateEntry() && Motion.m_EntryDuration > 0.0f);
			}
			if(m_FrameRuntime.EntryWasActive() && Motion.m_EntryDuration > 0.0f)
			{
				if(Input.m_pDiagnostics != nullptr)
					m_FrameRuntime.CountEntryAnimationResolve();
				const uint64_t ResolvedEntryKey = ResolveEntryKey();
				DeckEntryOffsetY = ResolveUiAnimValue(*Ctx.m_pAnim, ResolvedEntryKey, EUiAnimProperty::POS_Y, 0.0f, Motion.m_EntryDuration, EEasing::EASE_OUT);
				EntryPositionActive = Ctx.m_pAnim->HasActiveAnimation(ResolvedEntryKey, EUiAnimProperty::POS_Y);
				m_FrameRuntime.SetEntryActive(EntryPositionActive);
			}
			else if(m_FrameRuntime.EntryWasActive())
			{
				Ctx.m_pAnim->SetValue(ResolveEntryKey(), EUiAnimProperty::POS_Y, 0.0f);
				m_FrameRuntime.SetEntryActive(false);
			}
		}
		EntryPending = false;
		for(const SPreparedCard &Card : m_vPreparedCards)
		{
			const SRuntimeState &Runtime = m_vRuntimeStates[Card.m_StateIndex];
			const char *pStableId = Card.m_pDefinition->m_Spec.m_pStableId;
			if(!ContentHeightTargetChanged && !ContentHeightAnimationActive && Motion.m_ReflowDuration > 0.0f && Runtime.m_ReflowInitialized)
			{
				const float ReflowTargetY = Card.m_Frame.m_Rect.y - ScrollOffset.y;
				ReflowTargetChanged = ReflowTargetChanged || std::abs(Runtime.m_LastReflowTargetY - ReflowTargetY) > 0.001f;
				if(Runtime.m_ReflowWasActive)
				{
					const uint64_t ReflowKey = SettingsCardReflowNodeKey(pTab, pStableId);
					ReflowPositionActive = ReflowPositionActive || Ctx.m_pAnim->HasActiveAnimation(ReflowKey, EUiAnimProperty::POS_Y);
				}
			}
		}
	}
	if(!m_Drag.Active() && SettingsCardDeckAllowsDragStart(EntryPending, EntryPositionActive, ReflowTargetChanged, ReflowPositionActive || ContentHeightAnimationActive) && (Input.m_CtrlPressed || Input.m_AllowHeaderDrag) && Input.m_MousePressed)
	{
		for(const SPreparedCard &Card : m_vPreparedCards)
		{
			const bool InHeader = PointInRect(Card.m_Frame.m_HeaderRect, Input.m_MouseX, Input.m_MouseY);
			const bool InHeaderAction = PointInRect(Card.m_Frame.m_HandleRect, Input.m_MouseX, Input.m_MouseY);
			if(InHeader && !InHeaderAction)
			{
				m_Drag.m_StateIndex = Card.m_StateIndex;
				m_Drag.m_SourceColumn = Card.m_Column;
				m_Drag.m_TargetColumn = Card.m_Column;
				m_Drag.m_GrabOffsetX = Input.m_MouseX - Card.m_Frame.m_Rect.x;
				m_Drag.m_GrabOffsetY = Input.m_MouseY - Card.m_Frame.m_Rect.y;
				break;
			}
		}
	}

	if(m_Drag.Active())
	{
		const bool MouseInScrollViewport = PointInRect(ScrollViewport, Input.m_MouseX, Input.m_MouseY);
		if(DrawLayout.m_TwoColumns && m_Drag.m_SourceColumn != 0)
		{
			if(MouseInScrollViewport && Input.m_MouseX >= DrawLayout.m_aColumns[0].x && Input.m_MouseX <= DrawLayout.m_aColumns[0].x + DrawLayout.m_aColumns[0].w)
				m_Drag.m_TargetColumn = 1;
			else if(MouseInScrollViewport && Input.m_MouseX >= DrawLayout.m_aColumns[1].x && Input.m_MouseX <= DrawLayout.m_aColumns[1].x + DrawLayout.m_aColumns[1].w)
				m_Drag.m_TargetColumn = 2;
		}
		m_vDragGeometry.clear();
		m_vDragGeometry.reserve(m_vPreparedCards.size());
		for(const SPreparedCard &Card : m_vPreparedCards)
		{
			const int GeometryColumn = !DrawLayout.m_TwoColumns && m_Drag.m_SourceColumn != 0 && Card.m_Column != 0 ? 1 : Card.m_Column;
			m_vDragGeometry.push_back({Card.m_StateIndex, GeometryColumn, Card.m_Frame.m_Rect});
		}
		const int GeometryTargetColumn = !DrawLayout.m_TwoColumns && m_Drag.m_SourceColumn != 0 ? 1 : m_Drag.m_TargetColumn;
		m_Drag.m_TargetOrder = ResolveSettingsCardDeckDropOrder(Input.m_MouseY, GeometryTargetColumn, m_vDragGeometry, m_Drag.m_StateIndex);
		m_aDragColumns = *pColumns;
		if(DrawLayout.m_TwoColumns)
			ApplySettingsCardDeckDragPlacement(m_aDragColumns, m_Drag.m_StateIndex, m_Drag.m_TargetColumn, m_Drag.m_TargetOrder);
		else
			ApplySettingsCardDeckSingleColumnDragPlacement(m_aDragColumns, m_Drag.m_StateIndex, m_Drag.m_TargetOrder);
		BuildPreparedCards(m_aDragColumns);

		if(pScrollRegion != nullptr && MouseInScrollViewport)
		{
			Result.m_AutoScrollDelta = SettingsCardDeckAutoScrollDelta(Input.m_MouseY, ScrollViewport, Ctx.m_UiScale);
			if(Result.m_AutoScrollDelta != 0.0f)
				// 拖拽边缘滚动是当前输入，恢复前台时不补做后台期间的交互位移。
				pScrollRegion->ScrollRelativeDirect(Result.m_AutoScrollDelta * std::clamp(Input.m_FrameDt, 0.0f, 1.0f / 15.0f));
		}

		if(Input.m_MouseReleased)
		{
			const char *pStableId = m_Drag.m_StateIndex >= 0 && m_Drag.m_StateIndex < Model.Count() ? Model.Entry(m_Drag.m_StateIndex).m_pStableId : nullptr;
			Result.m_OrderChanged = DrawLayout.m_TwoColumns ? CommitSettingsCardDeckDrop(Model, pTab, pStableId, m_Drag.m_TargetColumn, m_Drag.m_TargetOrder, &m_vActiveStateIndices) : CommitSettingsCardDeckSingleColumnDrop(Model, pTab, pStableId, m_Drag.m_TargetOrder, m_vActiveStateIndices);
			if(Result.m_OrderChanged && m_Drag.m_StateIndex >= 0 && m_Drag.m_StateIndex < (int)m_vRuntimeStates.size())
			{
				SRuntimeState &Runtime = m_vRuntimeStates[m_Drag.m_StateIndex];
				Runtime.m_DropFeedbackRemaining = Motion.m_KeepDropFeedback ? Motion.m_DropFeedbackDuration : 0.0f;
				Result.m_DropFeedbackConsumed = Runtime.m_DropFeedbackRemaining > 0.0f;
			}
			m_Drag.Reset();
		}
	}
	if(Input.m_pDiagnostics != nullptr)
	{
		for(const SPreparedCard &Card : m_vPreparedCards)
		{
			m_FrameRuntime.RecordGeometry({
				Card.m_pDefinition->m_Spec.m_pStableId,
				Card.m_Column,
				Card.m_Frame.m_Rect,
				Card.m_TargetContentHeight,
				std::max(0.0f, Card.m_Frame.m_ContentRect.h),
				Card.m_FirstLayout,
				Card.m_ContentHeightAnimationActive,
			});
		}
	}
	for(const SPreparedCard &Card : m_vPreparedCards)
	{
		SRuntimeState &Runtime = m_vRuntimeStates[Card.m_StateIndex];
		const char *pStableId = Card.m_pDefinition->m_Spec.m_pStableId;
		SSettingsCardVisualState State;
		State.m_DrawOffsetY = DeckEntryOffsetY;
		State.m_ClipContent = SettingsCardDeckShouldClipContent(Card.m_Frame.m_ContentRect.w > 0.0f && Card.m_Frame.m_ContentRect.h > 0.0f, Card.m_ContentHeightAnimationActive);
		if(Ctx.m_pAnim != nullptr)
		{
			const bool ReflowInitializedThisFrame = !Runtime.m_ReflowInitialized;
			const float ReflowTargetY = Card.m_Frame.m_Rect.y - ScrollOffset.y;
			const bool TargetChanged = Runtime.m_ReflowInitialized && std::abs(Runtime.m_LastReflowTargetY - ReflowTargetY) > 0.001f;
			const SSettingsCardAnimationWork AnimationWork = ResolveSettingsCardAnimationWork(0.0f, false, ReflowInitializedThisFrame, SnapReflow, Motion.m_ReflowDuration, TargetChanged, Runtime.m_ReflowWasActive);
			uint64_t ReflowKey = 0;
			bool HasReflowKey = false;
			const auto ResolveReflowKey = [&]() {
				if(!HasReflowKey)
				{
					ReflowKey = SettingsCardReflowNodeKey(pTab, pStableId);
					HasReflowKey = true;
				}
				return ReflowKey;
			};
			if(ContentHeightAnimationActive || ContentHeightTargetChanged)
			{
				// 当前帧的位置已经由动画高度的底边推导，独立 POS_Y 动画会重新制造重叠。
				Runtime.m_ReflowInitialized = false;
				Runtime.m_ReflowWasActive = false;
			}
			else if(ReflowInitializedThisFrame)
			{
				Runtime.m_ReflowInitialized = true;
				Ctx.m_pAnim->SetValue(ResolveReflowKey(), EUiAnimProperty::POS_Y, ReflowTargetY);
			}
			if(ContentHeightAnimationActive || ContentHeightTargetChanged)
			{
				// 下一稳定帧再用最终位置初始化 reflow target。
			}
			else if(SnapReflow)
			{
				if(AnimationWork.m_SetReflowTarget)
					Ctx.m_pAnim->SetValue(ResolveReflowKey(), EUiAnimProperty::POS_Y, ReflowTargetY);
				Runtime.m_ReflowWasActive = false;
			}
			else if(AnimationWork.m_ResolveReflow)
			{
				if(Input.m_pDiagnostics != nullptr)
					m_FrameRuntime.CountReflowAnimationResolve();
				const uint64_t ResolvedReflowKey = ResolveReflowKey();
				const float ReflowY = ResolveUiAnimValue(*Ctx.m_pAnim, ResolvedReflowKey, EUiAnimProperty::POS_Y, ReflowTargetY, Motion.m_ReflowDuration, EEasing::EASE_OUT);
				State.m_DrawOffsetY += ReflowY - ReflowTargetY;
				const bool ReflowActive = Ctx.m_pAnim->HasActiveAnimation(ResolvedReflowKey, EUiAnimProperty::POS_Y);
				Runtime.m_ReflowWasActive = ReflowActive;
			}
			else
			{
				if(AnimationWork.m_SetReflowTarget)
					Ctx.m_pAnim->SetValue(ResolveReflowKey(), EUiAnimProperty::POS_Y, ReflowTargetY);
				Runtime.m_ReflowWasActive = false;
			}
			Runtime.m_LastReflowTargetY = ReflowTargetY;
		}
		// 用未滚动的布局坐标检测真实几何动效。屏幕滚动只改变绘制位置，
		// 不能让所有卡片的副标题在滚轮事件中短暂出现。
		const float MotionY = Card.m_Frame.m_Rect.y - ScrollOffset.y + State.m_DrawOffsetY;
		const float MotionHeight = Card.m_Frame.m_Rect.h;
		const bool GeometryMotionActive = SettingsCardDeckGeometryMoved(Runtime.m_MotionGeometryInitialized, Runtime.m_LastMotionY, Runtime.m_LastMotionHeight, MotionY, MotionHeight);
		Runtime.m_MotionGeometryInitialized = true;
		Runtime.m_LastMotionY = MotionY;
		Runtime.m_LastMotionHeight = MotionHeight;
		State.m_Dragged = m_Drag.Active() && Card.m_StateIndex == m_Drag.m_StateIndex;
		if(State.m_Dragged)
		{
			State.m_DrawOffsetX = Input.m_MouseX - m_Drag.m_GrabOffsetX - Card.m_Frame.m_Rect.x;
			State.m_DrawOffsetY = Input.m_MouseY - m_Drag.m_GrabOffsetY - Card.m_Frame.m_Rect.y;
		}
		State.m_DropFeedback = Runtime.m_DropFeedbackRemaining > 0.0f;
		State.m_ReflowCompleteFeedback = false;
		Result.m_DropFeedbackConsumed = Result.m_DropFeedbackConsumed || State.m_DropFeedback;
		const bool Reveal = !m_PendingRevealStableId.empty() && str_comp(pStableId, m_PendingRevealStableId.c_str()) == 0;
		const bool Visible = pScrollRegion == nullptr || pScrollRegion->AddRect(Card.m_Frame.m_Rect, Reveal);
		CUIRect CurrentDrawRect = Card.m_Frame.m_Rect;
		CurrentDrawRect.x += State.m_DrawOffsetX;
		CurrentDrawRect.y += State.m_DrawOffsetY;
		const bool PointerInsideCurrentFrame = Visible && Ctx.m_pUi != nullptr && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->MouseHovered(&CurrentDrawRect);
		// 入场是整个 Deck 的统一动效；高度/重排只影响当前卡片或被其推动的同列卡片。
		// 副标题从动效开始的当前绘制帧锁存，避免首次入场时依赖上一帧旧几何而漏显。
		const bool SubtitleMotionActive = EntryPositionActive || Runtime.m_ReflowWasActive || Card.m_ContentHeightAnimationActive || GeometryMotionActive;
		Runtime.m_SubtitleVisibleDuringMotion = ResolveSettingsCardSubtitleMotionLatch(PointerInsideCurrentFrame, SubtitleMotionActive, Runtime.m_SubtitleMotionWasActive, Runtime.m_SubtitleVisibleDuringMotion);
		Runtime.m_SubtitleMotionWasActive = SubtitleMotionActive;
		State.m_SubtitleVisibleDuringMotion = Runtime.m_SubtitleVisibleDuringMotion;
		if(Reveal)
		{
			Result.m_pRevealedStableId = Model.Entry(Card.m_StateIndex).m_pStableId;
			m_PendingRevealStableId.clear();
		}
		if(Visible || Card.m_pDefinition->m_RenderWhenClipped)
		{
			if(Input.m_pDiagnostics != nullptr)
				m_FrameRuntime.CountRenderedCard(SettingsCardShouldDrawChrome(Ctx.m_pUi != nullptr && Ctx.m_pUi->RenderOnly()));
			const bool HasCustomCollapsedState = static_cast<bool>(Card.m_pDefinition->m_IsCollapsed);
			const bool Collapsed = SettingsCardDeckResolveCollapsed(HasCustomCollapsedState, HasCustomCollapsedState && Card.m_pDefinition->m_IsCollapsed(), Runtime.m_DefaultCollapsed);
			State.m_Collapsed = Collapsed;
			State.m_ShowDefaultCollapseButton = SettingsCardDeckUsesDefaultCollapseControl(HasCustomCollapsedState, static_cast<bool>(Card.m_pDefinition->m_PreLayoutHeaderInput));
			State.m_HoverFeedbackEnabled = !m_SuppressHoverFeedbackOnce && !ScrollMovedThisFrame && !EntryPositionActive &&
						       !ContentHeightAnimationActive && !ReflowTargetChanged && !ReflowPositionActive;
			bool PointerInsideDrawFrame = false;
			SettingsCard(Ctx, Card.m_Frame, Card.m_pDefinition->m_Spec, State, VisualOptions,
				SettingsCardDeckRendersContent(Collapsed) ? Card.m_pDefinition->m_Render : FSettingsCardRender{}, Card.m_pDefinition->m_HeaderAction,
				SettingsCardDeckRendersContent(Collapsed) ? Card.m_pDefinition->m_RenderMeasured : FSettingsCardRenderMeasured{}, &PointerInsideDrawFrame);
			Runtime.m_PointerInsideLastFrame = PointerInsideDrawFrame;
			if(Ctx.m_pUi != nullptr && !Ctx.m_pUi->RenderOnly())
			{
				Runtime.m_LastDrawOffsetX = State.m_DrawOffsetX;
				Runtime.m_LastDrawOffsetY = State.m_DrawOffsetY;
			}
		}
		else
			Runtime.m_PointerInsideLastFrame = false;
	}

	if(pScrollRegion != nullptr)
		pScrollRegion->End();
	for(SRuntimeState &Runtime : m_vRuntimeStates)
	{
		Runtime.m_DropFeedbackRemaining = std::max(0.0f, Runtime.m_DropFeedbackRemaining - std::max(0.0f, Input.m_FrameDt));
	}
	// 入场或重排期间鼠标可能仍停在卡片最终位置；只有布局稳定后再次移动鼠标才恢复 hover 亮度，
	// 避免动画结束的首帧从普通背景突然跳到高亮背景。
	const bool LayoutStable = !EntryPending && !EntryPositionActive && !ContentHeightAnimationActive && !ReflowTargetChanged && !ReflowPositionActive;
	if(m_SuppressHoverFeedbackOnce && LayoutStable && m_HasPointerPosition &&
		(std::abs(Input.m_MouseX - m_LastPointerX) > 0.001f || std::abs(Input.m_MouseY - m_LastPointerY) > 0.001f))
		m_SuppressHoverFeedbackOnce = false;
	m_LastPointerX = Input.m_MouseX;
	m_LastPointerY = Input.m_MouseY;
	m_HasPointerPosition = true;
	m_vLastRenderedActiveStateIndices = m_vActiveStateIndices;
	return Result;
}
