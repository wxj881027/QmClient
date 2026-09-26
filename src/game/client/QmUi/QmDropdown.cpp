/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmDropdown.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool RectsOverlap(const CUIRect &A, const CUIRect &B)
	{
		return A.x < B.x + B.w && A.x + A.w > B.x && A.y < B.y + B.h && A.y + A.h > B.y;
	}

	float ResolvePopupHeightForAvailableSpace(const SQmDropdownGeometryConfig &Config, const float RequestedHeight, const float AvailableHeight)
	{
		const float CandidateHeight = std::min(std::max(0.0f, RequestedHeight), std::max(0.0f, AvailableHeight));
		const float RowHeight = std::max(0.0f, Config.m_RowHeight);
		if(RowHeight <= 0.0f)
			return CandidateHeight;

		const float RowSpacing = std::max(0.0f, Config.m_RowSpacing);
		const float FixedHeight = std::min(std::max(0.0f, Config.m_FixedHeight), CandidateHeight);
		const float LeadingRowSpacing = std::min(std::max(0.0f, Config.m_LeadingRowSpacing), CandidateHeight - FixedHeight);
		const float RowArea = CandidateHeight - FixedHeight - LeadingRowSpacing;
		const float RowExtent = RowHeight + RowSpacing;
		if(RowArea + 0.001f < RowHeight || RowExtent <= 0.0f)
			return 0.0f;

		const int CompleteRows = std::max(0, (int)std::floor((RowArea + RowSpacing + 0.001f) / RowExtent));
		if(CompleteRows <= 0)
			return 0.0f;
		return FixedHeight + LeadingRowSpacing + CompleteRows * RowHeight + (CompleteRows - 1) * RowSpacing;
	}
}

SQmDropdownGeometryResult QmComputeDropdownPopupGeometry(const CUIRect &AnchorRect, const CUIRect &ViewportRect, const SQmDropdownGeometryConfig &Config)
{
	SQmDropdownGeometryResult Result;
	Result.m_AnchorVisible = QmDropdownAnchorFullyVisible(AnchorRect, ViewportRect);

	const float Margin = std::max(0.0f, Config.m_Margin);
	const float Gap = std::max(0.0f, Config.m_Gap);
	const float MinX = ViewportRect.x + Margin;
	const float MinY = ViewportRect.y + Margin;
	const float MaxX = ViewportRect.x + std::max(0.0f, ViewportRect.w - Margin);
	const float MaxY = ViewportRect.y + std::max(0.0f, ViewportRect.h - Margin);
	const float AvailableWidth = std::max(0.0f, MaxX - MinX);
	const float AvailableHeight = std::max(0.0f, MaxY - MinY);

	Result.m_Rect.w = std::min(std::max(0.0f, Config.m_Width), AvailableWidth);
	const float RequestedHeight = std::max(0.0f, Config.m_Height);

	const float BelowY = AnchorRect.y + AnchorRect.h + Gap;
	const float BelowAvailable = std::min(AvailableHeight, std::max(0.0f, MaxY - (AnchorRect.y + AnchorRect.h + Gap)));
	const float AboveAvailable = std::min(AvailableHeight, std::max(0.0f, AnchorRect.y - Gap - MinY));
	const float BelowHeight = ResolvePopupHeightForAvailableSpace(Config, RequestedHeight, BelowAvailable);
	const float AboveHeight = ResolvePopupHeightForAvailableSpace(Config, RequestedHeight, AboveAvailable);
	const bool FitsBelow = BelowAvailable + 0.001f >= RequestedHeight;
	const bool FitsAbove = AboveAvailable + 0.001f >= RequestedHeight;
	if(FitsBelow && (!FitsAbove || Config.m_PreferBelow))
	{
		Result.m_PlacedBelow = true;
		Result.m_Rect.h = BelowHeight;
	}
	else if(FitsAbove)
	{
		Result.m_PlacedBelow = false;
		Result.m_Rect.h = AboveHeight;
	}
	else if(BelowHeight > 0.0f || AboveHeight > 0.0f)
	{
		Result.m_PlacedBelow = Config.m_PreferBelow ? BelowHeight >= AboveHeight : BelowHeight > AboveHeight;
		Result.m_Rect.h = Result.m_PlacedBelow ? BelowHeight : AboveHeight;
	}
	else
	{
		Result.m_Rect.h = 0.0f;
	}

	Result.m_Rect.x = AnchorRect.x;
	Result.m_Rect.y = Result.m_PlacedBelow ? BelowY : AnchorRect.y - Gap - Result.m_Rect.h;

	const float ClampedX = std::clamp(Result.m_Rect.x, MinX, std::max(MinX, MaxX - Result.m_Rect.w));
	const float ClampedY = std::clamp(Result.m_Rect.y, MinY, std::max(MinY, MaxY - Result.m_Rect.h));
	Result.m_Clamped = ClampedX != Result.m_Rect.x || ClampedY != Result.m_Rect.y || Result.m_Rect.w != Config.m_Width || Result.m_Rect.h != Config.m_Height;
	Result.m_Rect.x = ClampedX;
	Result.m_Rect.y = ClampedY;
	Result.m_PopupVisible = Result.m_Rect.w > 0.0f && Result.m_Rect.h > 0.0f && RectsOverlap(Result.m_Rect, ViewportRect);
	return Result;
}

SQmDropdownPopupPolicy QmResolveDropdownPopupPolicy(int ItemCount, float EntryHeight, float EntrySpacing, bool HasMessage, float MessageHeight, float OuterHeight, int MinimumVisibleItems)
{
	SQmDropdownPopupPolicy Policy;
	const int ClampedItemCount = std::max(0, ItemCount);
	Policy.m_ItemCount = ClampedItemCount;
	const int VisibleItemCount = std::min(std::max(ClampedItemCount, std::max(0, MinimumVisibleItems)), Policy.m_MaxVisibleItems);
	const float ResolvedEntryHeight = std::max(0.0f, EntryHeight);
	const float ResolvedEntrySpacing = std::max(0.0f, EntrySpacing);
	const float ResolvedMessageHeight = HasMessage ? std::max(0.0f, MessageHeight) : 0.0f;
	const float ResolvedOuterHeight = std::max(0.0f, OuterHeight);
	const auto ItemsHeight = [ResolvedEntryHeight, ResolvedEntrySpacing, HasMessage](int Count) {
		if(Count <= 0)
			return 0.0f;
		return Count * ResolvedEntryHeight + (HasMessage ? Count : Count - 1) * ResolvedEntrySpacing;
	};
	Policy.m_ContentHeight = ResolvedMessageHeight + ItemsHeight(ClampedItemCount) + ResolvedOuterHeight;
	Policy.m_PreferredHeight = ResolvedMessageHeight + ItemsHeight(VisibleItemCount) + ResolvedOuterHeight;
	return Policy;
}

bool QmDropdownPopupScrollable(const SQmDropdownPopupPolicy &Policy, float PopupHeight)
{
	return std::max(0.0f, PopupHeight) + 0.001f < Policy.m_ContentHeight;
}

bool QmDropdownPopupBlocksUnderlying(const bool PopupVisible)
{
	return PopupVisible;
}

bool QmDropdownSourceAlive(const uint64_t CurrentFrame, const uint64_t LastSourceFrame, const bool AnchorFullyVisible)
{
	return AnchorFullyVisible && CurrentFrame == LastSourceFrame;
}

bool QmDropdownAnchorFullyVisible(const CUIRect &AnchorRect, const CUIRect &ViewportRect)
{
	constexpr float EdgeTolerance = 0.01f;
	return AnchorRect.w > 0.0f && AnchorRect.h > 0.0f &&
	       AnchorRect.x + EdgeTolerance >= ViewportRect.x && AnchorRect.y + EdgeTolerance >= ViewportRect.y &&
	       AnchorRect.x + AnchorRect.w <= ViewportRect.x + ViewportRect.w + EdgeTolerance &&
	       AnchorRect.y + AnchorRect.h <= ViewportRect.y + ViewportRect.h + EdgeTolerance;
}

bool QmDropdownActiveItemShouldScrollIntoView(const bool ScrollRequested, const bool ActiveEntry)
{
	return ScrollRequested && ActiveEntry;
}

bool QmDropdownShouldRequestActiveScroll(const bool PopupOpen, const int PreviousActiveIndex, const int ActiveIndex)
{
	// 已打开弹层只有键盘等方式真正改变 active item 时才重新定位；普通重绘、
	// 滚轮和滚动条拖动都必须保留用户当前 offset。
	return PopupOpen && PreviousActiveIndex != ActiveIndex;
}

void CQmDropdownState::Reset()
{
	m_Open = false;
	m_ActiveIndex = -1;
}

bool CQmDropdownState::Disable(const bool PopupOpen)
{
	Reset();
	return PopupOpen;
}

SQmDropdownUpdateResult CQmDropdownState::Update(const SQmDropdownInput &Input, int ItemCount)
{
	SQmDropdownUpdateResult Result;
	ItemCount = std::max(0, ItemCount);

	if(!m_Open)
	{
		if(Input.m_TogglePressed && ItemCount > 0)
		{
			m_Open = true;
			m_ActiveIndex = Input.m_InitialIndex >= 0 && Input.m_InitialIndex < ItemCount ? Input.m_InitialIndex : 0;
			Result.m_Opened = true;
		}
		return Result;
	}

	if(ItemCount <= 0 || Input.m_KeyEscape || Input.m_ClickOutside || Input.m_TogglePressed)
	{
		Reset();
		Result.m_Closed = true;
		return Result;
	}

	m_ActiveIndex = std::clamp(m_ActiveIndex, 0, ItemCount - 1);
	if(Input.m_HoveredIndex >= 0 && Input.m_HoveredIndex < ItemCount)
		m_ActiveIndex = Input.m_HoveredIndex;

	if(Input.m_KeyUp)
		m_ActiveIndex = (m_ActiveIndex + ItemCount - 1) % ItemCount;
	if(Input.m_KeyDown)
		m_ActiveIndex = (m_ActiveIndex + 1) % ItemCount;

	if(Input.m_KeyEnter || (Input.m_MouseSelectPressed && Input.m_HoveredIndex >= 0 && Input.m_HoveredIndex < ItemCount))
	{
		if(Input.m_MouseSelectPressed)
			m_ActiveIndex = Input.m_HoveredIndex;
		Result.m_Selected = true;
		Result.m_SelectedIndex = m_ActiveIndex;
		Reset();
		Result.m_Closed = true;
	}

	return Result;
}
