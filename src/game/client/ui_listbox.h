/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_UI_LISTBOX_H
#define GAME_CLIENT_UI_LISTBOX_H

#include "ui_scrollregion.h"

#include <game/client/QmUi/QmListEntryAnimation.h>

#include <algorithm>
#include <cstdint>

constexpr float QmListBoxScrollbarMetric(float PolicyValue, float CurrentValue, bool Overridden)
{
	return Overridden ? CurrentValue : PolicyValue;
}

constexpr bool QmListBoxShouldScrollToInitialSelection(bool InitialScrollPending, int SelectedIndex)
{
	return InitialScrollPending && SelectedIndex >= 0;
}

constexpr bool QmListBoxInitialScrollRemainsPending(bool InitialScrollPending, int SelectedIndex)
{
	return InitialScrollPending && SelectedIndex < 0;
}

inline CUIRect QmListBoxEntryAnimatedRect(const CUIRect &Rect, float OffsetY)
{
	CUIRect AnimatedRect = Rect;
	AnimatedRect.y += OffsetY;
	return AnimatedRect;
}

struct CListboxItem
{
	bool m_Visible;
	bool m_Selected;
	CUIRect m_Rect;
	CUi *m_pUi = nullptr;
	bool m_GaussianBlurSuppressed = false;

	CUiScopedGaussianBlurSuppression SuppressGaussianBlur() const
	{
		return CUiScopedGaussianBlurSuppression(m_pUi, m_GaussianBlurSuppressed);
	}
};

// Instances of CListBox must be static, as member addresses are used as UI item IDs
class CListBox : private CUIElementBase
{
private:
	CUIRect m_ListBoxView;
	CUIRect m_RowView;
	float m_ListBoxRowHeight;
	int m_ListBoxItemIndex;
	int m_ListBoxSelectedIndex;
	int m_ListBoxNewSelected;
	int m_ListBoxNewSelOffset;
	bool m_ListBoxUpdateScroll;
	int m_ListBoxNumItems;
	int m_ListBoxItemsPerRow;
	EUiWheelOwnerPriority m_WheelOwnerPriority;
	bool m_ListBoxItemSelected;
	bool m_ListBoxItemActivated;
	bool m_ScrollbarShown;
	float m_AutoSpacing;
	CScrollRegion m_ScrollRegion;
	EQmScrollProfile m_ScrollProfile = EQmScrollProfile::MENU_LIST;
	int m_BackgroundCorners;
	float m_ScrollbarWidth;
	float m_ScrollbarMargin;
	bool m_ScrollbarWidthOverridden;
	bool m_ScrollbarMarginOverridden;
	bool m_HasHeader;
	bool m_Active;
	bool m_HideScrollbar;
	bool m_ScrollbarAlwaysReserved;
	bool m_InitialScrollPending;
	float m_EntryAnimationOffset;
	CQmListEntryAnimation m_EntryAnimation;
	ColorRGBA m_SelectedItemActiveColor;
	ColorRGBA m_SelectedItemInactiveColor;
	ColorRGBA m_HoveredItemColor;

protected:
	CListboxItem DoNextRow();

public:
	CListBox();
	void Reset();

	void DoHeader(const CUIRect *pRect, const char *pTitle, float HeaderHeight = 20.0f, float Spacing = 2.0f, int BackgroundCorners = IGraphics::CORNER_ALL);
	void DoAutoSpacing(float Spacing = 20.0f) { m_AutoSpacing = Spacing; }
	void DoSpacing(float Spacing = 20.0f);
	void DoStart(float RowHeight, int NumItems, int ItemsPerRow, int RowsPerScroll, int SelectedIndex, const CUIRect *pRect = nullptr, bool Background = true, int BackgroundCorners = IGraphics::CORNER_ALL);
	int ItemIndex() const { return m_ListBoxItemIndex; }
	int ItemsPerRow() const { return m_ListBoxItemsPerRow; }
	float RowHeight() const { return m_ListBoxRowHeight; }
	float ScrollOffsetY() const { return -m_ScrollRegion.ContentScrollOffsetY(); }
	float ViewHeight() const { return m_ScrollRegion.ClipRect()->h; }
	void ScrollToSelected() { m_ListBoxUpdateScroll = true; }
	// 已保存的选中项不一定是本次浏览目标；调用方可取消初始自动定位，显式定位仍有效。
	void SuppressInitialSelectionScroll() { m_InitialScrollPending = false; }
	void SkipItems(int Count);
	CListboxItem DoNextItem(const void *pId, bool Selected = false, float CornerRadius = 5.0f);
	CListboxItem DoCustomRow(float Height, bool ScrollHere = false);
	void ResetScroll()
	{
		m_ScrollRegion.Reset();
		m_InitialScrollPending = true;
	}
	CListboxItem DoSubheader();
	int DoEnd();

	// Active state must be set before calling DoStart.
	bool Active() const { return m_Active; }
	void SetWheelOwnerPriority(EUiWheelOwnerPriority Priority) { m_WheelOwnerPriority = Priority; }
	void SetActive(bool Active) { m_Active = Active; }
	void SetScrollProfile(EQmScrollProfile Profile) { m_ScrollProfile = Profile; }
	void SetHideScrollbar(bool HideScrollbar) { m_HideScrollbar = HideScrollbar; }
	void SetScrollbarAlwaysReserved(bool AlwaysReserved) { m_ScrollbarAlwaysReserved = AlwaysReserved; }
	void SetItemColors(ColorRGBA SelectedActive, ColorRGBA SelectedInactive, ColorRGBA Hovered)
	{
		m_SelectedItemActiveColor = SelectedActive;
		m_SelectedItemInactiveColor = SelectedInactive;
		m_HoveredItemColor = Hovered;
	}

	bool WasItemSelected() const { return m_ListBoxItemSelected; }
	bool WasItemActivated() const { return m_ListBoxItemActivated; }

	bool ScrollbarShown() const { return m_ScrollbarShown; }
	float ScrollbarWidth() const { return ScrollbarShown() ? ScrollbarWidthMax() : 0.0f; }
	bool ScrollbarActive() const { return m_ScrollRegion.Active(); }
	bool ScrollbarAnimating() const { return m_ScrollRegion.Animating(); }
	bool WheelConsumedThisFrame() const { return m_ScrollRegion.WheelConsumedThisFrame(); }
	float ScrollbarWidthMax() const { return m_ScrollbarWidth; }
	void SetScrollbarWidth(float Width)
	{
		m_ScrollbarWidth = Width;
		m_ScrollbarWidthOverridden = true;
	}
	float ScrollbarMargin() const { return m_ScrollbarMargin; }
	void SetScrollbarMargin(float Margin)
	{
		m_ScrollbarMargin = Margin;
		m_ScrollbarMarginOverridden = true;
	}
};

#endif
