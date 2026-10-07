// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIBUTTONS_H
#define GAME_CLIENT_QMUI_UIBUTTONS_H

#include "UiContext.h"

enum class EQmIcon;
class CButtonContainer;
class CUIRect;

namespace ui_widget
{

	// Solid blue-tinted button with hover color spring. Wraps CMenus::DoButton_Menu
	// and drives the fill color via ResolveUiAnimValueColor (ACCENT_PRIMARY_DIM ↔
	// ACCENT_PRIMARY_HOVER based on prev-frame hover state). Returns true on
	// click. Falls back to a flat disabled rendering when Disabled is set.
	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false);

	// 次级按钮沿用控件背景配置，悬浮/按下时增加可辨识的背景和边框反馈。
	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false);

	// Icon-only button. pIcon is a FONT_ICON_* UTF-8 string. Wraps CUi::DoButton_FontIcon
	// with hover color animation. Useful for compact toolbar entries (close, refresh,
	// settings glyphs).
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled = false);
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled = false);

} // namespace ui_widget

#endif
