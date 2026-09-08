/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_drag_logic.h"

#include <algorithm>
#include <cmath>

bool CardDragRectContains(const SCardDragRect &Rect, const float X, const float Y)
{
	return X >= Rect.m_X && X <= Rect.m_X + Rect.m_W && Y >= Rect.m_Y && Y <= Rect.m_Y + Rect.m_H;
}

bool CardDragThresholdExceeded(const float StartX, const float StartY, const float X, const float Y, const float Threshold)
{
	const float DistanceX = X - StartX;
	const float DistanceY = Y - StartY;
	return std::sqrt(DistanceX * DistanceX + DistanceY * DistanceY) > std::max(0.0f, Threshold);
}

void CCardDragState::Arm(std::string PageId, std::string CardId, const int Column, SCardDragRect HeaderRect, const float GrabX, const float GrabY)
{
	if(m_Phase != ECardDragPhase::IDLE)
		return;
	m_Phase = ECardDragPhase::ARMED;
	m_SourcePageId = std::move(PageId);
	m_CardId = std::move(CardId);
	m_PreviewPageId = m_SourcePageId;
	m_SourceColumn = Column;
	m_TargetColumn = Column;
	m_TargetOrder = 0;
	m_HeaderRect = HeaderRect;
	m_GrabOffsetX = GrabX - HeaderRect.m_X;
	m_GrabOffsetY = GrabY - HeaderRect.m_Y;
	m_StartX = GrabX;
	m_StartY = GrabY;
	m_PointerX = GrabX;
	m_PointerY = GrabY;
	m_HasStart = true;
}

void CCardDragState::Reset()
{
	m_Phase = ECardDragPhase::IDLE;
	m_SourcePageId.clear();
	m_CardId.clear();
	m_PreviewPageId.clear();
	m_SourceColumn = 0;
	m_TargetColumn = 0;
	m_TargetOrder = 0;
	m_HasStart = false;
}

SCardDragUpdate CCardDragState::Update(const SCardDragInput &Input, const SCardDragFrame &Frame, const float Threshold)
{
	SCardDragUpdate Update;
	if(m_Phase == ECardDragPhase::IDLE)
		return Update;

	m_PointerX = Input.m_X;
	m_PointerY = Input.m_Y;

	if(m_Phase == ECardDragPhase::ARMED)
	{
		if(Input.m_Cancelled || !Input.m_Down)
		{
			// 阈值前松手是点击，不是取消拖拽事务。
			Reset();
			return Update;
		}
		if(!m_HasStart || !CardDragThresholdExceeded(m_StartX, m_StartY, Input.m_X, Input.m_Y, Threshold * std::max(0.1f, Input.m_Scale)))
			return Update;
		m_Phase = ECardDragPhase::DRAGGING;
		Update.m_Started = true;
	}

	// DRAGGING：先解析预览页与目标，再处理提交与取消。
	if(Input.m_Cancelled)
	{
		Update.m_Cancelled = true;
		Reset();
		return Update;
	}

	ResolveTarget(Input, Frame, Update);

	if(!Input.m_Down && Input.m_Released)
	{
		Update.m_Committed = true;
		Update.m_CardId = m_CardId;
		Update.m_SourcePageId = m_SourcePageId;
		Update.m_TargetPageId = m_PreviewPageId;
		Update.m_TargetColumn = m_TargetColumn;
		Update.m_TargetOrder = m_TargetOrder;
		Reset();
		return Update;
	}
	if(!Input.m_Down)
	{
		// 指针序列异常（失焦等）时视为取消，恢复原布局并释放捕获。
		Update.m_Cancelled = true;
		Reset();
		return Update;
	}

	Update.m_AutoScrollDelta = CardDragRectContains(Frame.m_Viewport, Input.m_X, Input.m_Y) ? CardDragAutoScrollDelta(Input.m_Y, Frame.m_Viewport, Input.m_Scale) : 0.0f;
	return Update;
}

void CCardDragState::ResolveTarget(const SCardDragInput &Input, const SCardDragFrame &Frame, SCardDragUpdate &Update)
{
	// 跨页预览：指针悬停页面 tab 时切换预览页，保留 source 和事务。
	for(size_t Index = 0; Index < Frame.m_vPageTabRects.size() && Index < Frame.m_vPageTabIds.size(); ++Index)
	{
		if(CardDragRectContains(Frame.m_vPageTabRects[Index], Input.m_X, Input.m_Y) && Frame.m_vPageTabIds[Index] != m_PreviewPageId)
		{
			m_PreviewPageId = Frame.m_vPageTabIds[Index];
			Update.m_PreviewPageChanged = true;
			break;
		}
	}

	// 目标列：full 卡保持 full 语义；侧列卡在双列布局下可跨列，单视觉列时合并。
	int TargetColumn = m_SourceColumn;
	if(Frame.m_TwoColumns && m_SourceColumn != 0 && !Frame.m_SingleVisualColumn)
	{
		for(const int Column : {1, 2})
		{
			if(CardDragRectContains(Frame.m_aColumnRects[Column], Input.m_X, Input.m_Y))
			{
				TargetColumn = Column;
				break;
			}
		}
	}
	else if(Frame.m_TwoColumns && m_SourceColumn != 0 && Frame.m_SingleVisualColumn)
		TargetColumn = 1;

	// 目标命中使用稳定布局槽位：以静态几何快照解析插入序。
	const int GeometryColumn = Frame.m_SingleVisualColumn && m_SourceColumn != 0 ? 1 : TargetColumn;
	const int PreviousColumn = m_TargetColumn;
	const int PreviousOrder = m_TargetOrder;
	m_TargetColumn = TargetColumn;
	m_TargetOrder = ResolveCardDropOrder(Input.m_Y, GeometryColumn, Frame.m_vItems, m_CardId);
	Update.m_TargetChanged = m_TargetColumn != PreviousColumn || m_TargetOrder != PreviousOrder;
}

int ResolveCardDropOrder(const float MouseY, const int TargetColumn, const std::vector<SCardDragItem> &vItems, const std::string &DraggedId)
{
	int Order = 0;
	for(const SCardDragItem &Item : vItems)
	{
		if(Item.m_Column != TargetColumn || Item.m_Id == DraggedId)
			continue;
		if(MouseY < Item.m_Rect.m_Y + Item.m_Rect.m_H * 0.5f)
			return Order;
		++Order;
	}
	return Order;
}

void ApplyCardDragPlacement(std::array<std::vector<std::string>, 3> &aColumns, const std::string &DraggedId, const int TargetColumn, const int TargetOrder)
{
	if(TargetColumn < 0 || TargetColumn >= static_cast<int>(aColumns.size()))
		return;
	for(std::vector<std::string> &vColumn : aColumns)
		vColumn.erase(std::remove(vColumn.begin(), vColumn.end(), DraggedId), vColumn.end());
	std::vector<std::string> &vTarget = aColumns[TargetColumn];
	const int InsertAt = std::clamp(TargetOrder, 0, static_cast<int>(vTarget.size()));
	vTarget.insert(vTarget.begin() + InsertAt, DraggedId);
}

void ApplyCardDragSingleColumnPlacement(std::array<std::vector<std::string>, 3> &aColumns, const std::string &DraggedId, const int TargetOrder)
{
	int SourceColumn = -1;
	for(int Column = 0; Column < static_cast<int>(aColumns.size()); ++Column)
	{
		if(std::find(aColumns[Column].begin(), aColumns[Column].end(), DraggedId) != aColumns[Column].end())
		{
			SourceColumn = Column;
			break;
		}
	}
	if(SourceColumn < 0)
		return;
	if(SourceColumn == 0)
	{
		ApplyCardDragPlacement(aColumns, DraggedId, 0, TargetOrder);
		return;
	}

	// 单视觉列的 drop order 以左列（canonical 侧列）可见序解析，提交时也按左列序
	// 映射回模型；预览必须产生与提交一致的布局：被拖卡成为左列第 TargetOrder 个
	// 卡片，右列卡片保持原位。
	std::vector<std::string> &vLeft = aColumns[1];
	std::vector<std::string> &vRight = aColumns[2];
	vLeft.erase(std::remove(vLeft.begin(), vLeft.end(), DraggedId), vLeft.end());
	vRight.erase(std::remove(vRight.begin(), vRight.end(), DraggedId), vRight.end());
	const int InsertAt = std::clamp(TargetOrder, 0, static_cast<int>(vLeft.size()));
	vLeft.insert(vLeft.begin() + InsertAt, DraggedId);
}

float CardDragAutoScrollDelta(const float MouseY, const SCardDragRect &Viewport, const float UiScale)
{
	const float Scale = std::max(0.1f, UiScale);
	const float EdgeSize = 32.0f * Scale;
	const float MaxSpeed = 180.0f * Scale;
	if(EdgeSize <= 0.0f || Viewport.m_H <= 0.0f)
		return 0.0f;
	const float TopDistance = MouseY - Viewport.m_Y;
	if(TopDistance < EdgeSize)
		return -MaxSpeed * std::clamp((EdgeSize - TopDistance) / EdgeSize, 0.0f, 1.0f);
	const float BottomDistance = Viewport.m_Y + Viewport.m_H - MouseY;
	if(BottomDistance < EdgeSize)
		return MaxSpeed * std::clamp((EdgeSize - BottomDistance) / EdgeSize, 0.0f, 1.0f);
	return 0.0f;
}

void UpdateCardReflowTrack(SCardReflowTrack &Track, const float Target, const float Dt, const float Duration, const bool Snap)
{
	if(!Track.m_Initialized || Snap)
	{
		Track.m_Initialized = true;
		Track.m_Value = Target;
		Track.m_From = Target;
		Track.m_Target = Target;
		Track.m_Elapsed = 0.0f;
		Track.m_Duration = std::max(0.0f, Duration);
		Track.m_Active = false;
		return;
	}
	if(std::abs(Track.m_Value - Target) < 0.01f)
	{
		Track.m_Value = Target;
		Track.m_From = Target;
		Track.m_Target = Target;
		Track.m_Elapsed = 0.0f;
		Track.m_Active = false;
		return;
	}
	if(std::abs(Track.m_Target - Target) > 0.001f)
	{
		// 目标变化时从当前动画位置续接，不跳位。
		Track.m_From = Track.m_Value;
		Track.m_Target = Target;
		Track.m_Elapsed = 0.0f;
	}
	Track.m_Duration = std::max(0.0f, Duration);
	if(Track.m_Duration <= 0.0f)
	{
		Track.m_Value = Target;
		Track.m_Active = false;
		return;
	}
	Track.m_Elapsed += std::max(0.0f, Dt);
	const float Progress = std::clamp(Track.m_Elapsed / Track.m_Duration, 0.0f, 1.0f);
	const float Eased = 1.0f - (1.0f - Progress) * (1.0f - Progress) * (1.0f - Progress);
	Track.m_Value = Track.m_From + (Track.m_Target - Track.m_From) * Eased;
	Track.m_Active = Progress < 1.0f;
	if(!Track.m_Active)
		Track.m_Value = Track.m_Target;
}
