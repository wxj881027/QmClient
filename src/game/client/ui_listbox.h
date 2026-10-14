/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_UI_LISTBOX_H
#define GAME_CLIENT_UI_LISTBOX_H

#include "ui_scrollregion.h"

#include <game/client/QmUi/QmListEntryAnimation.h>

#include <algorithm>
#include <cstdint>
#include <optional>

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
	float m_EntryAnimationElapsedSec;
	CQmListEntryAnimation m_EntryAnimation;
	// 依次淡入 + 上浮入场：当前行的父层显隐系数已推入，待下一行发射或
	// DoEnd 时恢复。
	std::optional<CUiScopedControlAlphaScale> m_EntryAlphaScope;
	// 行延迟基准：本帧首个发射行的绝对索引（SkipItems 后），滚动到中段
	// 重放时错开也从头算起。
	int m_EntryBaseItemIndex = 0;
	bool m_EntryBaseIndexCaptured = false;
	bool m_EntryAnimationFadeOnly = false;
	float m_ItemSelectionInset = 0.0f;
	void QmRestoreEntryScope();
	ColorRGBA m_SelectedItemActiveColor;
	ColorRGBA m_SelectedItemInactiveColor;
	ColorRGBA m_HoveredItemColor;

protected:
	CListboxItem DoNextRow();

public:
	CListBox();
	void Reset();
	// 显式重放入场动画：页面切换回列表所在页时由调用方触发，
	// 不依赖渲染间隙启发（列表被背景趟持续渲染时间隙永远不会出现）。
	void RestartEntryAnimation() { m_EntryAnimation.Reset(); }
	// 入场只淡入、不位移（默认关闭）。大面积卡片网格用：整卡位移读作"晃"
	// （onetake：运动留给小元素，大 ground 保持稳定；opacity 领先位移），
	// 行级小列表仍保留淡入+上浮的 wordRise。
	void SetEntryAnimationFadeOnly(bool FadeOnly) { m_EntryAnimationFadeOnly = FadeOnly; }
	// 选中/悬浮反馈内缩：网格卡片在格子内有半格间隙，反馈矩形须贴合卡片
	// 而不是铺满格子；同时选中态附带按钮同款常驻描边。行列表保持 0。
	void SetItemSelectionInset(float Inset) { m_ItemSelectionInset = Inset; }

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
