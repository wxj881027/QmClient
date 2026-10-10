#include "SettingsCardDeckLogic.h"

#include <base/system.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardGeometry.h>

#include <algorithm>
#include <cmath>

void CSettingsCardDeckPackingPlan::Reset(const float CursorY, const float CardGap)
{
	m_aCursorY.fill(CursorY);
	m_aNextFullWidthFrame.fill(0);
	m_FullWidthCursorY = CursorY;
	m_CardGap = std::max(0.0f, CardGap);
	// 保留容量，稳定帧及同帧预布局重算不重复分配。
	m_vFullWidthFrames.clear();
}

SSettingsCardColumnFrame CSettingsCardDeckPackingPlan::Append(const int Column, const float CardHeight, const bool LeadingFullWidth)
{
	dbg_assert(Column >= 0 && Column <= 2, "Invalid settings card column");
	if(Column == 0)
	{
		const float CursorY = std::max({m_aCursorY[0], m_aCursorY[1], m_FullWidthCursorY});
		const SSettingsCardColumnFrame Frame = ResolveSettingsCardColumnFrame(CursorY, CardHeight, m_CardGap);
		m_vFullWidthFrames.push_back(Frame);
		m_FullWidthCursorY = Frame.m_NextY;
		// 页首全宽卡保持置顶，后续卡不能回填到它们之间。
		if(LeadingFullWidth)
			m_aCursorY.fill(Frame.m_NextY);
		return Frame;
	}

	float &CursorY = m_aCursorY[Column - 1];
	size_t &NextFullWidthFrame = m_aNextFullWidthFrame[Column - 1];
	const float Height = std::max(0.0f, CardHeight);
	while(NextFullWidthFrame < m_vFullWidthFrames.size())
	{
		const SSettingsCardColumnFrame &Full = m_vFullWidthFrames[NextFullWidthFrame];
		if(CursorY >= Full.m_NextY)
		{
			++NextFullWidthFrame;
			continue;
		}
		// 空位必须同时容纳卡片与下边间距，避免贴住或穿过全宽卡。
		if(CursorY + Height + m_CardGap <= Full.m_Y)
			break;
		CursorY = Full.m_NextY;
		++NextFullWidthFrame;
	}
	const SSettingsCardColumnFrame Frame = ResolveSettingsCardColumnFrame(CursorY, Height, m_CardGap);
	CursorY = Frame.m_NextY;
	return Frame;
}

std::array<std::vector<int>, 3> BuildSettingsCardDeckColumnOrder(const qm_card_order::CModel &Model, const char *pTab, const std::vector<int> &vActiveStateIndices)
{
	std::array<std::vector<int>, 3> aColumns;
	for(int Column = 0; Column < (int)aColumns.size(); ++Column)
	{
		const std::vector<int> vModelColumn = Model.ColumnIndices(pTab, Column);
		for(const int StateIndex : vModelColumn)
		{
			if(std::find(vActiveStateIndices.begin(), vActiveStateIndices.end(), StateIndex) != vActiveStateIndices.end())
				aColumns[Column].push_back(StateIndex);
		}
	}
	return aColumns;
}

namespace settings_card_deck_logic
{
	const std::array<std::vector<int>, 3> &CProjectionCache::Resolve(const qm_card_order::CModel &Model, const char *pTab, const std::vector<int> &vActiveStateIndices)
	{
		const char *pResolvedTab = pTab != nullptr ? pTab : "";
		if(m_LayoutRevision == Model.LayoutRevision() && str_comp(m_Tab.c_str(), pResolvedTab) == 0 && m_vActiveStateIndices == vActiveStateIndices)
			return m_aColumns;

		m_aColumns = BuildSettingsCardDeckColumnOrder(Model, pResolvedTab, vActiveStateIndices);
		m_LayoutRevision = Model.LayoutRevision();
		m_Tab = pResolvedTab;
		m_vActiveStateIndices = vActiveStateIndices;
		++m_RebuildCount;
		return m_aColumns;
	}
} // namespace settings_card_deck_logic

bool CSettingsCardDeckFrameRuntime::BeginDisplayCycle(const uint64_t DisplayCycle, const bool AnimateEntry)
{
	const bool Changed = m_DisplayCycle != DisplayCycle;
	if(Changed)
	{
		m_EntryDisplayCycle = UINT64_MAX;
		m_EntryWasActive = false;
	}
	m_DisplayCycle = DisplayCycle;
	m_AnimateEntry = AnimateEntry;
	return Changed;
}

void CSettingsCardDeckFrameRuntime::OnTabChanged(const bool StartEntryCycle)
{
	// 新版子分类在点击当帧触发入场；父页下一帧补交 display cycle 时沿用活动轨道。
	if(StartEntryCycle)
		m_EntryDisplayCycle = UINT64_MAX;
	m_EntryWasActive = false;
}

bool CSettingsCardDeckFrameRuntime::ConsumeEntryCycle()
{
	if(!EntryCyclePending())
		return false;
	m_EntryDisplayCycle = m_DisplayCycle;
	return true;
}

float CSettingsCardDeckFrameRuntime::ResolveContinuousEntryOffset(CUiV2AnimationRuntime &AnimRuntime, const uint64_t NodeKey, const SCardMotionSpec &Motion)
{
	if(ConsumeEntryCycle())
	{
		const bool Animate = m_AnimateEntry && Motion.m_EntryDuration > 0.0f;
		// 分类切换只替换内容，正在运动的共享轨道继续走完，不能重新跳到入场起点。
		if(!Animate || !AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y))
			AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, Animate ? Motion.m_EntryDistance : 0.0f);
		m_EntryWasActive = Animate;
	}
	if(!m_EntryWasActive)
		return 0.0f;
	if(Motion.m_EntryDuration <= 0.0f)
	{
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);
		m_EntryWasActive = false;
		return 0.0f;
	}

	CountEntryAnimationResolve();
	SUiAnimTransition Transition;
	Transition.m_Driver = EUiAnimDriver::SPRING;
	Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Transition.m_Priority = 1;
	// 阻尼比 0.8 只产生约 1.5% 的轻微回弹；减少动效模式沿用更短时长和更小位移。
	const float Frequency = 5.0f / Motion.m_EntryDuration;
	Transition.m_Spring = {1.0f, Frequency * Frequency, 1.6f * Frequency, 0.05f, 0.5f};
	// Motion 已按用户动效等级缩放，避免再次缩放导致切换手感不一致。
	Transition.m_RespectMotionLevel = false;
	const float Offset = AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f, Transition);
	m_EntryWasActive = AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y);
	return Offset;
}

void CSettingsCardDeckFrameRuntime::BeginFrame(SSettingsCardDeckFrameDiagnostics *pDiagnostics)
{
	m_pDiagnostics = pDiagnostics;
	if(m_pDiagnostics != nullptr)
		*m_pDiagnostics = {};
}

void CSettingsCardDeckFrameRuntime::CountDefinitionsPrepare()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_DefinitionsPrepareCount;
}

void CSettingsCardDeckFrameRuntime::CountMeasure()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_MeasureCount;
}

void CSettingsCardDeckFrameRuntime::CountEntryAnimationResolve()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_EntryAnimationResolveCount;
}

void CSettingsCardDeckFrameRuntime::CountHeightAnimationResolve()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_HeightAnimationResolveCount;
}

void CSettingsCardDeckFrameRuntime::CountReflowAnimationResolve()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_ReflowAnimationResolveCount;
}

void CSettingsCardDeckFrameRuntime::CountPreLayoutInput()
{
	if(m_pDiagnostics != nullptr)
		++m_pDiagnostics->m_PreLayoutInputCount;
}

void CSettingsCardDeckFrameRuntime::CountRenderedCard(const bool ChromePlanned)
{
	if(m_pDiagnostics == nullptr)
		return;
	++m_pDiagnostics->m_RenderedCardCount;
	if(ChromePlanned)
		++m_pDiagnostics->m_ChromePlanCount;
}

void CSettingsCardDeckFrameRuntime::MarkFirstLayout()
{
	if(m_pDiagnostics != nullptr)
		m_pDiagnostics->m_FirstLayout = true;
}

void CSettingsCardDeckFrameRuntime::RecordGeometry(const SSettingsCardDeckDiagnosticGeometry &Geometry)
{
	if(m_pDiagnostics == nullptr)
		return;
	++m_pDiagnostics->m_TotalGeometryCount;
	if(m_pDiagnostics->m_GeometryCount < m_pDiagnostics->m_aGeometry.size())
		m_pDiagnostics->m_aGeometry[m_pDiagnostics->m_GeometryCount++] = Geometry;
}

void ApplySettingsCardDeckDragPlacement(std::array<std::vector<int>, 3> &aColumns, int ActiveStateIndex, int TargetColumn, int TargetOrder)
{
	if(ActiveStateIndex < 0 || TargetColumn < 0 || TargetColumn >= (int)aColumns.size())
		return;
	for(std::vector<int> &vColumn : aColumns)
		vColumn.erase(std::remove(vColumn.begin(), vColumn.end(), ActiveStateIndex), vColumn.end());
	std::vector<int> &vTarget = aColumns[TargetColumn];
	const int InsertAt = std::clamp(TargetOrder, 0, (int)vTarget.size());
	vTarget.insert(vTarget.begin() + InsertAt, ActiveStateIndex);
}

void ApplySettingsCardDeckSingleColumnDragPlacement(std::array<std::vector<int>, 3> &aColumns, int ActiveStateIndex, int TargetOrder)
{
	int SourceColumn = -1;
	for(int Column = 0; Column < (int)aColumns.size(); ++Column)
	{
		if(std::find(aColumns[Column].begin(), aColumns[Column].end(), ActiveStateIndex) != aColumns[Column].end())
		{
			SourceColumn = Column;
			break;
		}
	}
	if(SourceColumn < 0)
		return;
	if(SourceColumn == 0)
	{
		ApplySettingsCardDeckDragPlacement(aColumns, ActiveStateIndex, 0, TargetOrder);
		return;
	}

	std::vector<int> vVisualOrder;
	std::vector<int> vVisualColumnSlots;
	vVisualOrder.reserve(aColumns[1].size() + aColumns[2].size());
	vVisualColumnSlots.reserve(vVisualOrder.capacity());
	const size_t NumLayers = std::max(aColumns[1].size(), aColumns[2].size());
	for(size_t Layer = 0; Layer < NumLayers; ++Layer)
	{
		if(Layer < aColumns[1].size())
		{
			vVisualOrder.push_back(aColumns[1][Layer]);
			vVisualColumnSlots.push_back(1);
		}
		if(Layer < aColumns[2].size())
		{
			vVisualOrder.push_back(aColumns[2][Layer]);
			vVisualColumnSlots.push_back(2);
		}
	}
	vVisualOrder.erase(std::remove(vVisualOrder.begin(), vVisualOrder.end(), ActiveStateIndex), vVisualOrder.end());
	const int InsertAt = std::clamp(TargetOrder, 0, (int)vVisualOrder.size());
	vVisualOrder.insert(vVisualOrder.begin() + InsertAt, ActiveStateIndex);
	aColumns[1].clear();
	aColumns[2].clear();
	for(size_t i = 0; i < vVisualOrder.size(); ++i)
		aColumns[vVisualColumnSlots[i]].push_back(vVisualOrder[i]);
}

int ResolveSettingsCardDeckDropOrder(float MouseY, int TargetColumn, const std::vector<SSettingsCardDeckItemGeometry> &vItems, int IgnoredStateIndex)
{
	int Order = 0;
	for(const SSettingsCardDeckItemGeometry &Item : vItems)
	{
		if(Item.m_Column != TargetColumn || Item.m_StateIndex == IgnoredStateIndex)
			continue;
		if(MouseY < Item.m_Rect.y + Item.m_Rect.h * 0.5f)
			return Order;
		++Order;
	}
	return Order;
}

float SettingsCardDeckAutoScrollDelta(float MouseY, const CUIRect &Viewport, float UiScale)
{
	const float Scale = std::max(0.1f, UiScale);
	const float EdgeSize = 32.0f * Scale;
	const float MaxSpeed = 180.0f * Scale;
	if(EdgeSize <= 0.0f || Viewport.h <= 0.0f)
		return 0.0f;
	const float TopDistance = MouseY - Viewport.y;
	if(TopDistance < EdgeSize)
		return -MaxSpeed * std::clamp((EdgeSize - TopDistance) / EdgeSize, 0.0f, 1.0f);
	const float BottomDistance = Viewport.y + Viewport.h - MouseY;
	if(BottomDistance < EdgeSize)
		return MaxSpeed * std::clamp((EdgeSize - BottomDistance) / EdgeSize, 0.0f, 1.0f);
	return 0.0f;
}

bool SettingsCardDeckNeedsContentMeasure(const bool Collapsed, const bool MeasureEachFrame, const float CachedContentHeight)
{
	return !Collapsed && (CachedContentHeight < 0.0f || MeasureEachFrame);
}

bool SettingsCardDeckRendersContent(const bool Collapsed)
{
	return !Collapsed;
}

bool SettingsCardDeckContentHeightChanged(const float PreviousContentHeight, const float ContentHeight)
{
	return PreviousContentHeight >= 0.0f && std::abs(PreviousContentHeight - ContentHeight) > 0.01f;
}

bool SettingsCardDeckShouldSnapReflow(const bool GeometryStateChanged, const bool DragActive)
{
	return GeometryStateChanged && !DragActive;
}

bool SettingsCardDeckScrollMoved(const bool HasPreviousOffset, const float PreviousOffsetY, const float CurrentOffsetY)
{
	return HasPreviousOffset && std::abs(PreviousOffsetY - CurrentOffsetY) > 0.001f;
}

bool SettingsCardDeckAllowsDragStart(const bool EntryPending, const bool EntryPositionActive, const bool ReflowTargetChanged, const bool ReflowPositionActive)
{
	return !EntryPending && !EntryPositionActive && !ReflowTargetChanged && !ReflowPositionActive;
}

SSettingsCardAnimationWork ResolveSettingsCardAnimationWork(const float EntryDuration, const bool EntryWasActive, const bool ReflowInitializedThisFrame, const bool SnapReflow, const float ReflowDuration, const bool ReflowTargetChanged, const bool ReflowWasActive)
{
	SSettingsCardAnimationWork Work;
	Work.m_ResolveEntry = EntryDuration > 0.0f && EntryWasActive;
	Work.m_ResetEntry = EntryDuration <= 0.0f && EntryWasActive;
	const bool ReflowNeedsWork = ReflowTargetChanged || ReflowWasActive;
	Work.m_ResolveReflow = !SnapReflow && ReflowDuration > 0.0f && ReflowNeedsWork;
	Work.m_SetReflowTarget = !ReflowInitializedThisFrame && ReflowNeedsWork && !Work.m_ResolveReflow;
	return Work;
}

SSettingsCardHeightAnimationWork ResolveSettingsCardHeightAnimationWork(const bool InitializedThisFrame, const bool TargetChanged, const bool AnimationWasActive, const float Duration, const bool Snap)
{
	SSettingsCardHeightAnimationWork Work;
	if(InitializedThisFrame)
		return Work;
	const bool NeedsWork = TargetChanged || AnimationWasActive;
	Work.m_ResolveHeight = !Snap && Duration > 0.0f && NeedsWork;
	Work.m_SetHeightTarget = NeedsWork && !Work.m_ResolveHeight;
	return Work;
}

bool SettingsCardDeckShouldClipContent(const bool HasRenderableContent, const bool ContentHeightAnimationActive)
{
	return HasRenderableContent && ContentHeightAnimationActive;
}

SSettingsCardColumnFrame ResolveSettingsCardColumnFrame(const float CursorY, const float CardHeight, const float CardGap)
{
	const float ResolvedHeight = std::max(0.0f, CardHeight);
	return {CursorY, ResolvedHeight, CursorY + ResolvedHeight + std::max(0.0f, CardGap)};
}

bool CommitSettingsCardDeckDrop(qm_card_order::CModel &Model, const char *pTab, const char *pStableId, int TargetColumn, int TargetOrder, const std::vector<int> *pActiveStateIndices)
{
	if(pTab == nullptr || pStableId == nullptr || TargetColumn < 0 || TargetColumn > 2)
		return false;
	const int Index = Model.FindByStableId(pStableId);
	if(Index < 0)
		return false;
	const qm_card_order::SEntry &Entry = Model.Entry(Index);
	if(Entry.m_pDefaultTab == nullptr || str_comp(Entry.m_pDefaultTab, pTab) != 0)
		return false;
	const std::vector<int> vTargetEntries = Model.ColumnIndices(pTab, TargetColumn);
	const bool SameColumn = Entry.m_Column == TargetColumn;
	std::vector<int> vTargetEntriesWithoutDragged;
	vTargetEntriesWithoutDragged.reserve(vTargetEntries.size());
	for(const int TargetEntry : vTargetEntries)
	{
		if(TargetEntry != Index)
			vTargetEntriesWithoutDragged.push_back(TargetEntry);
	}
	int ClampedOrder = std::clamp(TargetOrder, 0, (int)vTargetEntriesWithoutDragged.size());
	if(pActiveStateIndices != nullptr)
	{
		std::vector<int> vVisibleTargetEntries;
		for(const int TargetEntry : vTargetEntriesWithoutDragged)
		{
			if(std::find(pActiveStateIndices->begin(), pActiveStateIndices->end(), TargetEntry) != pActiveStateIndices->end())
				vVisibleTargetEntries.push_back(TargetEntry);
		}
		const int VisibleOrder = std::clamp(TargetOrder, 0, (int)vVisibleTargetEntries.size());
		if(SameColumn)
		{
			int CurrentVisibleOrder = 0;
			bool FoundDraggedEntry = false;
			for(const int TargetEntry : vTargetEntries)
			{
				if(std::find(pActiveStateIndices->begin(), pActiveStateIndices->end(), TargetEntry) == pActiveStateIndices->end())
					continue;
				if(TargetEntry == Index)
				{
					FoundDraggedEntry = true;
					break;
				}
				++CurrentVisibleOrder;
			}
			if(FoundDraggedEntry && CurrentVisibleOrder == VisibleOrder)
				return false;
		}
		if(vVisibleTargetEntries.empty())
		{
			ClampedOrder = 0;
		}
		else if(VisibleOrder < (int)vVisibleTargetEntries.size())
		{
			const auto It = std::find(vTargetEntriesWithoutDragged.begin(), vTargetEntriesWithoutDragged.end(), vVisibleTargetEntries[VisibleOrder]);
			ClampedOrder = It != vTargetEntriesWithoutDragged.end() ? (int)(It - vTargetEntriesWithoutDragged.begin()) : ClampedOrder;
		}
		else if(!vVisibleTargetEntries.empty())
		{
			const auto It = std::find(vTargetEntriesWithoutDragged.begin(), vTargetEntriesWithoutDragged.end(), vVisibleTargetEntries.back());
			ClampedOrder = It != vTargetEntriesWithoutDragged.end() ? (int)(It - vTargetEntriesWithoutDragged.begin()) + 1 : ClampedOrder;
		}
	}
	else if(SameColumn)
	{
		ClampedOrder = std::clamp(TargetOrder, 0, (int)vTargetEntriesWithoutDragged.size());
	}
	if(SameColumn && Entry.m_OrderInColumn == ClampedOrder)
		return false;
	Model.Move(pStableId, TargetColumn, ClampedOrder);
	return true;
}

bool CommitSettingsCardDeckSingleColumnDrop(qm_card_order::CModel &Model, const char *pTab, const char *pStableId, int TargetOrder, const std::vector<int> &vActiveStateIndices)
{
	const int Index = Model.FindByStableId(pStableId);
	if(Index < 0 || Model.Entry(Index).m_pDefaultTab == nullptr || str_comp(Model.Entry(Index).m_pDefaultTab, pTab != nullptr ? pTab : "") != 0)
		return false;
	const int SourceColumn = Model.Entry(Index).m_Column;
	if(SourceColumn == 0)
		return CommitSettingsCardDeckDrop(Model, pTab, pStableId, 0, TargetOrder, &vActiveStateIndices);
	if(SourceColumn != 1 && SourceColumn != 2)
		return false;

	std::array<std::vector<int>, 3> aColumns = BuildSettingsCardDeckColumnOrder(Model, pTab, vActiveStateIndices);
	const std::array<std::vector<int>, 3> aPreviousColumns = aColumns;
	ApplySettingsCardDeckSingleColumnDragPlacement(aColumns, Index, TargetOrder);
	if(aColumns == aPreviousColumns)
		return false;

	std::array<std::vector<int>, 3> aCanonicalColumns;
	for(const int Column : {1, 2})
	{
		const std::vector<int> vCurrentColumn = Model.ColumnIndices(pTab, Column);
		size_t VisibleIndex = 0;
		aCanonicalColumns[Column].reserve(vCurrentColumn.size());
		for(const int StateIndex : vCurrentColumn)
		{
			if(std::find(vActiveStateIndices.begin(), vActiveStateIndices.end(), StateIndex) == vActiveStateIndices.end())
				aCanonicalColumns[Column].push_back(StateIndex);
			else if(VisibleIndex < aColumns[Column].size())
				aCanonicalColumns[Column].push_back(aColumns[Column][VisibleIndex++]);
		}
	}

	// 先固定完整 canonical 顺序再提交，避免连续移动时动态可见锚点把隐藏卡推离原槽位。
	for(const int Column : {1, 2})
	{
		for(int Order = 0; Order < (int)aCanonicalColumns[Column].size(); ++Order)
		{
			const int StateIndex = aCanonicalColumns[Column][Order];
			if(StateIndex >= 0 && StateIndex < Model.Count())
				Model.Move(Model.Entry(StateIndex).m_pStableId, Column, Order);
		}
	}
	return true;
}
