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

inline SQmDropdownVisualStyle QmSettingsDropdownVisualStyle(const SUiTheme &Theme, const ColorRGBA &PopupBorderColor)
{
	SQmDropdownVisualStyle Style;
	Style.m_TriggerColor = Theme.m_InputSurface;
	// 弹层遮住底层内容：背景跟随用户主题表面色（qm_ui_color / qm_ui_opacity 可调），
	// 仅整体压暗一档以表达悬浮层级，并保证最低不透明度维持可读性；
	// 边框与设置卡片边框同源（qm_ui_card_border_color 可调），不再使用强调色，
	// 避免下拉弹层出现突兀的高亮蓝框。
	const float ElevatedScale = 0.82f;
	const ColorRGBA Elevated = ColorRGBA(
		std::clamp(Theme.m_Surface.r * ElevatedScale, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.g * ElevatedScale, 0.0f, 1.0f),
		std::clamp(Theme.m_Surface.b * ElevatedScale, 0.0f, 1.0f),
		Theme.m_Surface.a);
	Style.m_PopupBackgroundColor = Elevated.WithAlpha(std::clamp(std::max(Elevated.a, 0.90f), 0.0f, 1.0f));
	Style.m_PopupBorderColor = PopupBorderColor;
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
	// 锚点对齐模式（外框包裹触发按钮+列表）：弹层宽度恒等于请求宽度、
	// 左缘恒对齐锚点，跳过水平钳制/压缩——否则几像素的水平偏移会让
	// 渲染端的锚点对齐判定失败，退回独立画边框，按钮与列表视觉断开。
	// 垂直方向（放不下时收缩/翻转到上方）不受影响。
	bool m_AlignToAnchor = false;
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
