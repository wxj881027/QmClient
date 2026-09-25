/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMDROPDOWN_H
#define GAME_CLIENT_QMUI_QMDROPDOWN_H

#include "QmScroll.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>

struct SQmDropdownVisualStyle
{
	ColorRGBA m_TriggerColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
	ColorRGBA m_PopupBackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
	ColorRGBA m_PopupBorderColor = ColorRGBA(0.7f, 0.7f, 0.7f, 0.9f);
	ColorRGBA m_ActiveEntryColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f);
	bool m_TransparentEntries = true;
};

inline SQmDropdownVisualStyle QmSettingsDropdownVisualStyle(const SUiTheme &Theme)
{
	SQmDropdownVisualStyle Style;
	Style.m_TriggerColor = Theme.m_InputSurface;
	// 弹层遮住底层内容，边框保持强调色。
	Style.m_PopupBackgroundColor = ui_token::color::SURFACE_ELEVATED.WithAlpha(1.0f);
	Style.m_PopupBorderColor = Theme.m_Accent.WithAlpha(1.0f);
	Style.m_ActiveEntryColor = Theme.m_Selected;
	return Style;
}

struct SQmDropdownGeometryConfig
{
	float m_Width = 0.0f;
	float m_Height = 0.0f;
	float m_Gap = 0.0f;
	float m_Margin = 0.0f;
	float m_RowHeight = 0.0f;
	float m_RowSpacing = 0.0f;
	float m_FixedHeight = 0.0f;
	float m_LeadingRowSpacing = 0.0f;
	bool m_PreferBelow = true;
};

struct SQmDropdownGeometryResult
{
	CUIRect m_Rect{};
	bool m_AnchorVisible = false;
	bool m_PopupVisible = false;
	bool m_PlacedBelow = true;
	bool m_Clamped = false;
};

struct SQmDropdownPopupPolicy
{
	int m_ItemCount = 0;
	int m_MaxVisibleItems = QM_POPUP_LIST_MAX_VISIBLE_ITEMS;
	float m_ContentHeight = 0.0f;
	float m_PreferredHeight = 0.0f;
};

struct SQmDropdownInput
{
	bool m_TogglePressed = false;
	int m_InitialIndex = -1;
	bool m_ClickOutside = false;
	bool m_KeyUp = false;
	bool m_KeyDown = false;
	bool m_KeyEnter = false;
	bool m_KeyEscape = false;
	int m_HoveredIndex = -1;
	bool m_MouseSelectPressed = false;
};

struct SQmDropdownUpdateResult
{
	bool m_Opened = false;
	bool m_Closed = false;
	bool m_Selected = false;
	int m_SelectedIndex = -1;
};

inline float QmDropdownFixedHeight(const bool HasMessage, const float MessageHeight, const float OuterHeight)
{
	return std::max(0.0f, OuterHeight) + (HasMessage ? std::max(0.0f, MessageHeight) : 0.0f);
}

SQmDropdownGeometryResult QmComputeDropdownPopupGeometry(const CUIRect &AnchorRect, const CUIRect &ViewportRect, const SQmDropdownGeometryConfig &Config);
SQmDropdownPopupPolicy QmResolveDropdownPopupPolicy(int ItemCount, float EntryHeight, float EntrySpacing, bool HasMessage, float MessageHeight, float OuterHeight, int MinimumVisibleItems = 0);
bool QmDropdownPopupScrollable(const SQmDropdownPopupPolicy &Policy, float PopupHeight);
bool QmDropdownPopupBlocksUnderlying(bool PopupVisible);
bool QmDropdownSourceAlive(uint64_t CurrentFrame, uint64_t LastSourceFrame, bool AnchorFullyVisible);
bool QmDropdownAnchorFullyVisible(const CUIRect &AnchorRect, const CUIRect &ViewportRect);
bool QmDropdownActiveItemShouldScrollIntoView(bool ScrollRequested, bool ActiveEntry);
bool QmDropdownShouldRequestActiveScroll(bool PopupOpen, int PreviousActiveIndex, int ActiveIndex);

class CQmDropdownState
{
public:
	void Reset();
	bool Disable(bool PopupOpen);
	SQmDropdownUpdateResult Update(const SQmDropdownInput &Input, int ItemCount);

	bool IsOpen() const { return m_Open; }
	int ActiveIndex() const { return m_ActiveIndex; }

private:
	bool m_Open = false;
	int m_ActiveIndex = -1;
};

#endif
