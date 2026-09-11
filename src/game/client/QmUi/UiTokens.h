// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITOKENS_H
#define GAME_CLIENT_QMUI_UITOKENS_H

#include "QmAnimCurves.h"

#include <base/color.h>

#include <algorithm>

// Centralized QmUi design tokens. Values are unscaled; callers apply
// responsive UiScale on top, matching existing LG_* convention in
// menus_qmclient.cpp.

namespace ui_token::color
{
	// surface
	inline constexpr ColorRGBA SURFACE_GLASS{0.08f, 0.09f, 0.12f, 0.70f};
	inline constexpr ColorRGBA SURFACE_ELEVATED{0.10f, 0.11f, 0.14f, 0.82f};
	inline constexpr ColorRGBA SURFACE_OVERLAY{0.0f, 0.0f, 0.0f, 0.40f};
	inline constexpr ColorRGBA SURFACE_HIGHLIGHT{1.0f, 1.0f, 1.0f, 0.05f};
	inline constexpr ColorRGBA SURFACE_SHADOW{0.0f, 0.0f, 0.0f, 0.12f};
	inline constexpr ColorRGBA BORDER_SUBTLE{1.0f, 1.0f, 1.0f, 0.08f};
	inline constexpr ColorRGBA BORDER_FOCUS{0.4f, 0.753f, 0.957f, 0.85f};
	inline constexpr ColorRGBA LIST_ITEM_SELECTED{1.0f, 1.0f, 1.0f, 0.14f};
	inline constexpr ColorRGBA LIST_ITEM_HOVER{1.0f, 1.0f, 1.0f, 0.08f};

	// accent
	inline constexpr ColorRGBA ACCENT_PRIMARY{0.4f, 0.753f, 0.957f, 1.0f};
	inline constexpr ColorRGBA ACCENT_PRIMARY_HOVER{0.55f, 0.82f, 0.98f, 1.0f};
	inline constexpr ColorRGBA ACCENT_PRIMARY_PRESS{0.30f, 0.65f, 0.88f, 1.0f};
	inline constexpr ColorRGBA ACCENT_PRIMARY_DIM{0.4f, 0.753f, 0.957f, 0.18f};

	// text
	inline constexpr ColorRGBA TEXT_PRIMARY{1.0f, 1.0f, 1.0f, 1.0f};
	inline constexpr ColorRGBA TEXT_SECONDARY{1.0f, 1.0f, 1.0f, 0.72f};
	inline constexpr ColorRGBA TEXT_TIP{1.0f, 1.0f, 1.0f, 0.65f};
	inline constexpr ColorRGBA TEXT_DISABLED{1.0f, 1.0f, 1.0f, 0.38f};
	inline constexpr ColorRGBA TEXT_ON_ACCENT{0.06f, 0.08f, 0.11f, 1.0f};

	// semantic
	inline constexpr ColorRGBA SUCCESS{0.42f, 0.85f, 0.52f, 1.0f};
	inline constexpr ColorRGBA WARNING{0.98f, 0.78f, 0.30f, 1.0f};
	inline constexpr ColorRGBA DANGER{0.95f, 0.41f, 0.38f, 1.0f};

	inline ColorRGBA UiColorSurface(ColorRGBA UiColor, float AlphaScale, float ColorScale)
	{
		return ColorRGBA(
			std::clamp(UiColor.r * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.g * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.b * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.a * AlphaScale, 0.0f, 1.0f));
	}

	inline ColorRGBA UiColorAccent(ColorRGBA UiColor, float AlphaScale)
	{
		return UiColor.WithAlpha(std::clamp(UiColor.a * AlphaScale, 0.0f, 1.0f));
	}
} // namespace ui_token::color

namespace ui_token::spacing
{
	inline constexpr float XS = 4.0f;
	inline constexpr float SM = 8.0f;
	inline constexpr float MD = 12.0f;
	inline constexpr float LG = 16.0f;
	inline constexpr float XL = 24.0f;
} // namespace ui_token::spacing

// 圆角标尺：微型 TIGHT / 标准控件 BASE / 卡片容器 CARD。
// 同一屏内只允许出现这三档加 PILL，禁止再引入字面量半径。
namespace ui_token::radius
{
	inline constexpr float NONE = 0.0f;
	inline constexpr float TIGHT = 6.0f;
	inline constexpr float BASE = 8.0f;
	inline constexpr float CARD = 14.0f;
	inline constexpr float PILL = 999.0f; // 由绘制层按短边一半裁剪
} // namespace ui_token::radius

namespace ui_token::settings
{
	inline constexpr float MAX_CONTENT_WIDTH = 1000.0f;
	inline constexpr float OUTER_SCROLLBAR_SLOT = 20.0f;
	inline constexpr float PAGE_INSET = 16.0f;
	inline constexpr float TAB_HEIGHT = 26.0f;
	inline constexpr float TAB_GAP = 10.0f;
	inline constexpr float TAB_FONT_SIZE = 13.0f;
	inline constexpr float SUB_TAB_HEIGHT = TAB_HEIGHT;
	inline constexpr float SUB_TAB_GAP = TAB_GAP;
	inline constexpr float CARD_GAP = 16.0f;
	inline constexpr float ROW_HEIGHT = 20.0f;
	inline constexpr float ROW_GAP = 5.0f;
	inline constexpr float TWO_COLUMN_MIN_WIDTH = 760.0f;
	inline constexpr float CARD_PADDING = 14.0f;
	inline constexpr float CARD_HEADER_TITLE_HEIGHT = 18.0f;
	inline constexpr float CARD_HEADER_SUBTITLE_HEIGHT = 12.0f;
	inline constexpr float CARD_HEADER_GAP = 6.0f;
	inline constexpr float CARD_HANDLE_SIZE = 24.0f;
	inline constexpr float CARD_RADIUS = radius::CARD;
} // namespace ui_token::settings

namespace ui_token::elevation
{
	inline constexpr float SHADOW_X_LOW = 1.0f;
	inline constexpr float SHADOW_Y_LOW = 1.0f;
	inline constexpr float SHADOW_X_MED = 1.5f;
	inline constexpr float SHADOW_Y_MED = 2.0f;
	inline constexpr float SHADOW_X_HIGH = 3.0f;
	inline constexpr float SHADOW_Y_HIGH = 6.0f;
} // namespace ui_token::elevation

namespace ui_token::font
{
	inline constexpr float HEADLINE_LG = 16.0f;
	inline constexpr float HEADLINE = 14.0f;
	inline constexpr float TITLE = 18.0f;
	inline constexpr float BODY = 12.0f;
	inline constexpr float SMALL = 10.0f;
	inline constexpr float CAPTION = 10.0f;
	inline constexpr float TIP = 9.0f;
} // namespace ui_token::font

namespace ui_token::ime
{
	inline constexpr float SCALE = 0.68f;
	inline constexpr ColorRGBA PANEL_BG_LIGHT{1.0f, 1.0f, 1.0f, 0.96f};
	inline constexpr ColorRGBA PANEL_BG_DARK{0.110f, 0.110f, 0.118f, 0.96f};
	inline constexpr ColorRGBA PANEL_BORDER_LIGHT{0.0f, 0.0f, 0.0f, 0.08f};
	inline constexpr ColorRGBA PANEL_BORDER_DARK{1.0f, 1.0f, 1.0f, 0.10f};
	inline constexpr ColorRGBA PANEL_SHADOW_LIGHT{0.0f, 0.0f, 0.0f, 0.16f};
	inline constexpr ColorRGBA PANEL_SHADOW_DARK{0.0f, 0.0f, 0.0f, 0.0f};
	inline constexpr ColorRGBA COMPOSITION_BG_LIGHT{1.0f, 1.0f, 1.0f, 0.18f};
	inline constexpr ColorRGBA COMPOSITION_BG_DARK{1.0f, 1.0f, 1.0f, 0.03f};
	inline constexpr ColorRGBA COMPOSITION_SELECTION{0.0f, 0.478f, 1.0f, 0.18f};
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE_LIGHT{0.05f, 0.06f, 0.08f, 0.18f};
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE_DARK{1.0f, 1.0f, 1.0f, 0.16f};
	inline constexpr ColorRGBA SELECTED_BG_LIGHT{0.0f, 0.478f, 1.0f, 0.22f};
	inline constexpr ColorRGBA SELECTED_BG_DARK{0.26f, 0.55f, 1.0f, 0.28f};
	inline constexpr ColorRGBA TEXT_LIGHT{0.055f, 0.065f, 0.08f, 0.98f};
	inline constexpr ColorRGBA TEXT_DARK{1.0f, 1.0f, 1.0f, 0.96f};
	inline constexpr ColorRGBA TEXT_MUTED_LIGHT{0.08f, 0.09f, 0.11f, 0.52f};
	inline constexpr ColorRGBA TEXT_MUTED_DARK{0.557f, 0.557f, 0.580f, 0.56f};
	inline constexpr ColorRGBA TEXT_SELECTED_LIGHT{0.0f, 0.32f, 0.74f, 1.0f};
	inline constexpr ColorRGBA TEXT_SELECTED_DARK{0.184f, 0.502f, 0.929f, 1.0f};
	inline constexpr ColorRGBA PANEL_BG = PANEL_BG_LIGHT;
	inline constexpr ColorRGBA PANEL_BORDER = PANEL_BORDER_LIGHT;
	inline constexpr ColorRGBA PANEL_SHADOW = PANEL_SHADOW_LIGHT;
	inline constexpr ColorRGBA COMPOSITION_BG = COMPOSITION_BG_LIGHT;
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE = COMPOSITION_UNDERLINE_LIGHT;
	inline constexpr ColorRGBA SELECTED_BG = SELECTED_BG_LIGHT;
	inline constexpr ColorRGBA TEXT = TEXT_LIGHT;
	inline constexpr ColorRGBA TEXT_MUTED = TEXT_MUTED_LIGHT;
	inline constexpr ColorRGBA TEXT_SELECTED = TEXT_SELECTED_LIGHT;
	inline constexpr float FONT_COMPOSITION = 7.2f * SCALE;
	inline constexpr float FONT_CANDIDATE = 8.1f * SCALE;
	inline constexpr float PADDING_X = 4.6f * SCALE;
	inline constexpr float PADDING_Y = 2.6f * SCALE;
	inline constexpr float ROW_GAP = 1.4f * SCALE;
	inline constexpr float ROW_HEIGHT = 14.0f * SCALE;
	inline constexpr float COMPOSITION_ROW_HEIGHT = 11.8f * SCALE;
	inline constexpr float NUM_WIDTH = 5.5f * SCALE;
	inline constexpr float MIN_WIDTH = 72.0f * SCALE;
	inline constexpr float MAX_WIDTH = 360.0f * SCALE;
	inline constexpr float RADIUS = 8.2f * SCALE;
	inline constexpr float SCREEN_HEIGHT = 300.0f;
	inline constexpr float SCREEN_MARGIN = 4.0f;
	inline constexpr float SHADOW_X = 0.0f;
	inline constexpr float SHADOW_Y = 2.0f * SCALE;
	inline constexpr float BORDER_INSET = 0.35f * SCALE;
	inline constexpr float COMPOSITION_TEXT_PADDING_X = 4.6f * SCALE;
	inline constexpr float CANDIDATE_NUM_PADDING_X = 3.6f * SCALE;
	inline constexpr float CANDIDATE_GAP = 7.0f * SCALE;
	inline constexpr float CANDIDATE_PADDING_X = 5.6f * SCALE;
	inline constexpr float SELECTED_PADDING_X = 6.2f * SCALE;
	inline constexpr float TEXT_SAFE_PADDING_X = 1.8f * SCALE;
	inline constexpr float TEXT_SAFE_PADDING_Y = 2.4f * SCALE;
	inline constexpr float TRAILING_WIDTH = 20.0f * SCALE;
	inline constexpr float MAX_CANDIDATE_TEXT_WIDTH = 1000.0f;
} // namespace ui_token::ime

namespace ui_token::motion
{
	inline constexpr const SUiAnimTransition &HOVER_FADE = ui_curve::DECELERATE;
	inline constexpr const SUiAnimTransition &PRESS_SCALE = ui_curve::ACCELERATE;
	inline constexpr const SUiAnimTransition &MODAL_FADE_SCALE = ui_curve::EMPHASIZED;
	inline constexpr const SUiAnimTransition &PAGE_SLIDE = ui_curve::STANDARD;
	inline constexpr const SUiAnimTransition &TAB_SWITCH = ui_curve::DECELERATE;
	inline constexpr const SUiAnimTransition &INPUT_FOCUS_RING = ui_curve::DECELERATE;
	inline constexpr const SUiAnimTransition &TOAST_SLIDE = ui_curve::EMPHASIZED;
	inline constexpr const SUiAnimTransition &TOOLTIP_FADE = ui_curve::DECELERATE;
	inline constexpr const SUiSpringConfig &TOGGLE_SPRING = ui_spring::SNAPPY;
	inline constexpr SUiSpringConfig CARD_REORDER{1.0f, 900.0f, 48.0f, 0.01f, 0.05f};

	inline constexpr const SUiAnimTransition &BTN_HOVER = HOVER_FADE;
	inline constexpr const SUiAnimTransition &BTN_PRESS = PRESS_SCALE;
	inline constexpr const SUiAnimTransition &MODAL_IN = MODAL_FADE_SCALE;
	inline constexpr const SUiSpringConfig &TOGGLE = TOGGLE_SPRING;
} // namespace ui_token::motion

#endif
