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
	// 半透明前景解析无法采样场景时使用的背板参考色，不改变实际表面绘制。
	inline constexpr ColorRGBA SURFACE_BACKDROP{0.10f, 0.10f, 0.10f, 1.0f};
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
	// 离散滑块的空槽保持中性灰，填充色由档位样式单独决定。
	inline constexpr ColorRGBA SLIDER_TRACK{0.27f, 0.27f, 0.27f, 1.0f};

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
	// 角色色：好友与收藏在各列表里反复出现，集中一处避免各页各写一份金色/粉色。
	// 收藏用的金色同时承担"已标记"的软警告语义，不再单独复制一个 WARNING_SOFT。
	inline constexpr ColorRGBA FAVORITE{1.0f, 0.85f, 0.0f, 1.0f};
	inline constexpr ColorRGBA FRIEND{0.95f, 0.45f, 0.62f, 1.0f};
	// 列表分组标题的底色层，比普通行更弱，只用来区分组与组。
	inline constexpr ColorRGBA SURFACE_SUBHEADER{0.0f, 0.0f, 0.0f, 0.15f};

	// 中性极值：只表达颜色本身，不承载角色；角色语义（正文/强调/危险）一律
	// 引用上面的 TEXT_*/ACCENT_*/DANGER token。顶点乘法直通（设白让纹理原色
	// 通过）与纯色基准、透明复位都引用这里，禁止再写裸 ColorRGBA 字面量。
	inline constexpr ColorRGBA NEUTRAL_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
	inline constexpr ColorRGBA NEUTRAL_BLACK{0.0f, 0.0f, 0.0f, 1.0f};
	inline constexpr ColorRGBA COLOR_TRANSPARENT{0.0f, 0.0f, 0.0f, 0.0f};
	// 手动文本模式的强制前景（ResolveUiTextColor 的模式 1/2），与自动对比推导无关。
	inline constexpr ColorRGBA TEXT_FORCED_LIGHT = NEUTRAL_WHITE;
	inline constexpr ColorRGBA TEXT_FORCED_DARK = NEUTRAL_BLACK;

	// 自适应对比双色：浅表面用深前景、深表面用浅前景（胶囊导航等明暗推导）。
	// 成对修改，保持两者的对比关系；透明度由调用点 WithAlpha 决定。
	inline constexpr ColorRGBA ADAPTIVE_ON_LIGHT{0.09f, 0.10f, 0.12f, 1.0f};
	inline constexpr ColorRGBA ADAPTIVE_ON_DARK{0.97f, 0.98f, 1.0f, 1.0f};

	// SURFACE_HIGHLIGHT 的暗色对偶：亮表面上的悬停/按压反馈（压暗而非提亮）。
	inline constexpr ColorRGBA SURFACE_HIGHLIGHT_DARK{0.0f, 0.0f, 0.0f, 0.06f};

	// 卡片内预览区（皮肤/地图/激光演示等）的暗化背板；跨卡片共享同一档，
	// 与文件内更深的演示背板（0.3/0.5 档）区分，视觉统一时在此收敛。
	inline constexpr ColorRGBA PREVIEW_BACKDROP{0.0f, 0.0f, 0.0f, 0.25f};

	// 容器与滚动条的默认外观。与既有 token 的近似值（BORDER_SUBTLE 0.08 等）
	// 是当前视觉现状，统一视觉时改这里一处生效，不在调用点写死。
	inline constexpr ColorRGBA CONTAINER_FILL{0.17f, 0.18f, 0.22f, 0.72f};
	inline constexpr ColorRGBA CONTAINER_HIGHLIGHT{1.0f, 1.0f, 1.0f, 0.06f};
	inline constexpr ColorRGBA CONTAINER_BORDER{1.0f, 1.0f, 1.0f, 0.10f};
	inline constexpr ColorRGBA SCROLLBAR_TRACK{1.0f, 1.0f, 1.0f, 0.12f};
	inline constexpr ColorRGBA SCROLLBAR_THUMB{1.0f, 1.0f, 1.0f, 0.42f};

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

// 离散滑块档位色板与满档渐变：UiDiscreteSliderStyle.h 与 UiDiscreteSliderLogic.cpp
// 共享的唯一来源，禁止在调用文件复制。
namespace ui_token::discrete_slider
{
	// 低档冷蓝 → 蓝紫 → 粉紫 → 高档亮粉，索引随档位升高。
	inline constexpr ColorRGBA TIER_COLORS[] = {
		ColorRGBA{0.48f, 0.67f, 0.91f, 1.0f},
		ColorRGBA{0.65f, 0.54f, 0.91f, 1.0f},
		ColorRGBA{0.84f, 0.48f, 0.78f, 1.0f},
		ColorRGBA{0.92f, 0.45f, 0.69f, 1.0f},
	};
	// 满档渐变：紫起点 → 亮紫中点 → 灰紫终点。
	inline constexpr ColorRGBA GRADIENT_START{0.51f, 0.16f, 0.52f, 1.0f};
	inline constexpr ColorRGBA GRADIENT_MIDDLE{0.77f, 0.53f, 0.97f, 1.0f};
	inline constexpr ColorRGBA GRADIENT_END{0.57f, 0.32f, 0.79f, 1.0f};
} // namespace ui_token::discrete_slider

// 图标按钮反馈统一强度；透明背景也保留可见的边界提示。
namespace ui_token::feedback
{
	inline constexpr float ICON_HOVER_ALPHA = 0.16f;
	inline constexpr float ICON_PRESS_ALPHA = 0.30f;
	inline constexpr float ICON_BORDER_ALPHA = 0.70f;
	inline constexpr float ICON_BORDER_WIDTH = 1.0f;
}

namespace ui_token::spacing
{
	inline constexpr float XS = 4.0f;
	inline constexpr float SM = 8.0f;
	inline constexpr float MD = 12.0f;
	inline constexpr float LG = 16.0f;
	inline constexpr float XL = 24.0f;
} // namespace ui_token::spacing

// 圆角标尺集中保存既有用途的数值；调用点选择用途，不再写字面量半径。
namespace ui_token::radius
{
	inline constexpr float NONE = 0.0f;
	inline constexpr float TIGHT = 6.0f;
	inline constexpr float BASE = 8.0f;
	inline constexpr float SMALL_MENU = 5.0f;
	inline constexpr float MENU = 10.0f;
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
	inline constexpr float CONTROL_PADDING_Y = 2.0f;
	inline constexpr float MIN_ROW_HEIGHT = 16.0f;
	inline constexpr float BADGE_HEIGHT = 16.0f;
	inline constexpr float ROW_GAP = 5.0f;
	inline constexpr float TWO_COLUMN_MIN_WIDTH = 760.0f;
	inline constexpr float CARD_PADDING = 14.0f;
	inline constexpr float CARD_HEADER_TITLE_HEIGHT = 18.0f;
	inline constexpr float CARD_HEADER_SUBTITLE_HEIGHT = 12.0f;
	inline constexpr float CARD_SUMMARY_GAP = spacing::XS;
	inline constexpr float CARD_HEADER_GAP = 6.0f;
	inline constexpr float CARD_SUMMARY_ALPHA = 0.72f;
	inline constexpr float CARD_HANDLE_SIZE = 24.0f;
	inline constexpr float CARD_RADIUS = radius::CARD;
} // namespace ui_token::settings

// 沿用既有二级面板数值，集中管理不同内容用途，接入组件时不重新定尺寸。
namespace ui_token::secondary_panel
{
	inline constexpr float BODY_SIZE = 7.5f;
	inline constexpr float MARGIN = 3.0f;
	inline constexpr float TITLE_HEIGHT = 16.0f;
	inline constexpr float ROW_HEIGHT = 16.0f;
	inline constexpr float LABEL_HEIGHT = 11.0f;
	inline constexpr float DROPDOWN_HEIGHT = 18.0f;
	inline constexpr float GAP = 4.0f;
	inline constexpr float SWITCH_WIDTH = 30.0f;
	inline constexpr float SWITCH_HEIGHT = 16.0f;
	inline constexpr float LIBRARY_WIDTH = 780.0f;
	inline constexpr float LIBRARY_HEIGHT = 470.0f;
	inline constexpr float LIBRARY_VIEWPORT_FRACTION = 0.74f;
	inline constexpr float NEWS_WIDTH = 820.0f;
	inline constexpr float NEWS_ACTION_HEIGHT = 26.0f;
	inline constexpr float UPDATE_WIDTH = 600.0f;
	inline constexpr float UPDATE_ACTION_LINE_FACTOR = 2.4f;
} // namespace ui_token::secondary_panel

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

// HUD 基础字号按 300 逻辑高度定义：辅助信息、正文、重点数值、关键提示。
namespace ui_token::hud::font
{
	inline constexpr float CAPTION = 6.0f;
	inline constexpr float BODY = 8.0f;
	inline constexpr float TITLE = 10.0f;
	inline constexpr float EMPHASIS = 12.0f;
} // namespace ui_token::hud::font

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
	// 菜单反馈使用独立的语义时长，避免通用预设改变交互节奏。
	inline constexpr SUiAnimTransition HOVER_FADE{.m_DurationSec = 0.12f, .m_Easing = EEasing::EASE_OUT_QUART};
	inline constexpr SUiAnimTransition PRESS_SCALE{.m_DurationSec = 0.08f, .m_Easing = EEasing::EASE_OUT_QUART};
	inline constexpr SUiAnimTransition MODAL_FADE_SCALE{
		.m_DurationSec = 0.20f,
		.m_Easing = EEasing::EASE_OUT_QUART,
		.m_Spring = {1.0f, 400.0f, 40.0f, 0.001f, 0.01f},
	};
	inline constexpr SUiAnimTransition PAGE_SLIDE{.m_DurationSec = 0.22f, .m_Easing = EEasing::EASE_OUT_QUART};
	inline constexpr SUiAnimTransition TAB_SWITCH{.m_DurationSec = 0.16f, .m_Easing = EEasing::EASE_OUT_QUART};
	inline constexpr const SUiAnimTransition &INPUT_FOCUS_RING = HOVER_FADE;
	inline constexpr SUiAnimTransition TOAST_SLIDE{
		.m_DurationSec = 0.22f,
		.m_Easing = EEasing::EASE_OUT_QUART,
		.m_Spring = MODAL_FADE_SCALE.m_Spring,
	};
	inline constexpr SUiAnimTransition TOOLTIP_FADE{.m_DurationSec = 0.12f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 导航滑块专用：切换 Tab / 分段时带速度续接地滑过去（ζ≈0.93）。
	inline constexpr SUiSpringConfig NAVIGATION_SPRING{1.0f, 500.0f, 42.0f, 0.05f, 0.4f};
	inline constexpr SUiSpringConfig TOGGLE_SPRING{1.0f, 620.0f, 44.0f, 0.01f, 0.05f};
	inline constexpr SUiSpringConfig CARD_REORDER{1.0f, 900.0f, 48.0f, 0.01f, 0.05f};
	// 离散滑条专用：档位切换时旋钮与填充带阻尼滑过去（ζ≈0.70），悬停/拖动只放大旋钮不改色。
	inline constexpr SUiSpringConfig SLIDER_SPRING{1.0f, 340.0f, 26.0f, 0.01f, 0.05f};
	inline constexpr SUiSpringConfig SLIDER_KNOB_SPRING{1.0f, 520.0f, 26.0f, 0.01f, 0.05f};

	// 弹窗开关缩放动画时长：入场 0.18s 展开渐显、出场 0.16s 收缩渐隐。
	// 曲线在弹窗绘制内实现（入场 ease-out 二次、出场 smoothstep），token 只承载时长。
	// 0.12/0.09 在实测中短到不可感知（用户反馈「关闭动画像没有」），放大到可读区间。
	inline constexpr float POPUP_OPEN_SEC = 0.18f;
	inline constexpr float POPUP_CLOSE_SEC = 0.16f;

	// 语义化动静节奏标尺（onetake 呼吸感与分级规约）：
	// 1. 竞技快击（Combat/Twitch）：≤80ms，临界阻尼，0 过冲，无拖泥带水
	inline constexpr SUiAnimTransition TWITCH_SNAP{.m_DurationSec = 0.06f, .m_Easing = EEasing::EASE_OUT_QUART};
	inline constexpr SUiSpringConfig TWITCH_SPRING{1.0f, 1000.0f, 63.0f, 0.01f, 0.05f}; // ζ≈1.0 临界阻尼

	// 2. 交互级（Interactive）：120ms~180ms，高响应轻微回弹
	inline constexpr SUiAnimTransition INTERACTION_POP{.m_DurationSec = 0.14f, .m_Easing = EEasing::EASE_OUT_QUART};

	// 3. 通告/展示级（Presentation）：~0.20s 入场 → 2.5s 静止定格 → 0.10s 快速收折
	inline constexpr float PRESENTATION_HOLD_TIME = 2.5f;
	inline constexpr SUiAnimTransition PRESENTATION_COLLAPSE{.m_DurationSec = 0.10f, .m_Easing = EEasing::EASE_OUT_QUART};

	// 4. Tooltip 气泡跨锚点移动：0.16s EASE_OUT_QUART（t80≈0.33）。
	// 原 CQmTooltipMotionState 手写插值的时长与曲线语义，2026-10-14 迁入引擎
	// tween 轨道（打断续接 + 动效等级自动响应），此处为唯一时长/曲线来源。
	inline constexpr SUiAnimTransition TOOLTIP_MOVE{.m_DurationSec = 0.16f, .m_Easing = EEasing::EASE_OUT_QUART};

	// 5. 列表逐行入场（wordRise 家族）：淡入与上浮同长、行间 cascading 铺满可视列。
	// 曲线语义：淡入 = smoothstep（EASE_IN_OUT）；上浮 = 临界阻尼闭式解
	// （QmCriticallyDampedRise），token 只承载时长与距离。求值器保持无状态
	// 流逝时间模型——虚拟化交错行若用每行引擎轨道会随滚动无限增删键；
	// 动效分级（Reduced：0.10s / 4px / 无错开）按分级规约在求值器内分支。
	inline constexpr SUiAnimTransition LIST_ENTRY_FADE{.m_DurationSec = 0.20f, .m_Easing = EEasing::EASE_IN_OUT};
	inline constexpr SUiAnimTransition LIST_ENTRY_FADE_REDUCED{.m_DurationSec = 0.10f, .m_Easing = EEasing::EASE_IN_OUT};
	inline constexpr float LIST_ENTRY_RISE_SEC = 0.20f;
	inline constexpr float LIST_ENTRY_RISE_SEC_REDUCED = 0.10f;
	inline constexpr float LIST_ENTRY_RISE_DISTANCE = 8.0f;
	inline constexpr float LIST_ENTRY_RISE_DISTANCE_REDUCED = 4.0f;
	inline constexpr float LIST_ENTRY_STAGGER_STEP_SEC = 0.014f;
	inline constexpr int LIST_ENTRY_STAGGER_MAX_ROWS = 28;

	inline constexpr const SUiAnimTransition &BTN_HOVER = HOVER_FADE;
	inline constexpr const SUiAnimTransition &BTN_PRESS = PRESS_SCALE;
	inline constexpr const SUiAnimTransition &MODAL_IN = MODAL_FADE_SCALE;
	inline constexpr const SUiSpringConfig &TOGGLE = TOGGLE_SPRING;
} // namespace ui_token::motion

#endif
