// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITOKENS_H
#define GAME_CLIENT_QMUI_UITOKENS_H

#include "QmAnimCurves.h"

#include <base/color.h>

#include <game/client/ui_rect.h>

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
	// SURFACE_HIGHLIGHT 的语义档：数值等于原来的 WithMultipliedAlpha 结果，换名不改观感。
	// 按三族分开命名，调一档只改一处，也不会顺手带走别族的手感。
	// 一、面板/玻璃上的表面反馈（底本身已有 0.70–0.82 不透明度）
	inline constexpr ColorRGBA TINT_CONTROL_HOVER{1.0f, 1.0f, 1.0f, 0.030f}; // 5% ×0.6
	inline constexpr ColorRGBA TINT_CONTROL_SELECTED{1.0f, 1.0f, 1.0f, 0.050f}; // 5%
	inline constexpr ColorRGBA TINT_ZONE_HOVER{1.0f, 1.0f, 1.0f, 0.035f}; // 5% ×0.7
	inline constexpr ColorRGBA TINT_ZONE_SELECTED{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	inline constexpr ColorRGBA TINT_ROW_HOVER{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	// 绑定设置页那种整行可点的大行：悬停时比卡片内小行更亮。数值与
	// LIST_ITEM_SELECTED 相同但语义不同（这里是悬停，那边是选中），不要合并。
	inline constexpr ColorRGBA TINT_ROW_ACTIVE{1.0f, 1.0f, 1.0f, 0.140f}; // 5% ×2.8
	// 二、可辨识的立体部件（滚轮底座、倾斜板）
	inline constexpr ColorRGBA TINT_PART{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	inline constexpr ColorRGBA TINT_PART_ACTIVE{1.0f, 1.0f, 1.0f, 0.090f}; // 5% ×1.8
	// 三、游戏内叠层（直接压在画面上，5% 在亮地图几乎不可见）
	inline constexpr ColorRGBA TINT_HUD_BAR{1.0f, 1.0f, 1.0f, 0.100f}; // 5% ×2
	inline constexpr ColorRGBA TINT_HUD_CARD{1.0f, 1.0f, 1.0f, 0.050f}; // 5%
	inline constexpr ColorRGBA TINT_HUD_CARD_ACTIVE{1.0f, 1.0f, 1.0f, 0.200f}; // 5% ×4
	inline constexpr ColorRGBA PREVIEW_SURFACE{1.0f, 1.0f, 1.0f, 0.10f};
	inline constexpr ColorRGBA SURFACE_SHADOW{0.0f, 0.0f, 0.0f, 0.12f};
	inline constexpr ColorRGBA BORDER_SUBTLE{1.0f, 1.0f, 1.0f, 0.08f};
	inline constexpr ColorRGBA BORDER_FOCUS{0.4f, 0.753f, 0.957f, 0.85f};
	// 列表控件（ListBox）的整行悬停/选中：直接压在菜单底图上，比卡片内的
	// TINT_ROW_* 更亮，才压得住背板。两者不要互相替换。
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
	inline constexpr ColorRGBA SCREENSHOT_EDITOR_BACKDROP{0.06f, 0.07f, 0.09f, 0.95f};
	inline constexpr ColorRGBA SCREENSHOT_EDITOR_DISABLED_ACTION{1.0f, 1.0f, 1.0f, 0.08f};

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
	// 悬停 / 按下 / 选中统一使用的白色描边。按钮表面、动作组外框、键位行等
	// 所有「控件处于活动态」的描边都从这里取，禁止在调用文件重写 1,1,1,alpha。
	inline constexpr ColorRGBA ACTIVE_BORDER{1.0f, 1.0f, 1.0f, ICON_BORDER_ALPHA};
}

namespace ui_token::spacing
{
	inline constexpr float XS = 4.0f;
	inline constexpr float SM = 8.0f;
	inline constexpr float MD = 12.0f;
	inline constexpr float LG = 16.0f;
	inline constexpr float XL = 24.0f;
} // namespace ui_token::spacing

// 弹层条目（下拉框/选择弹窗）的排版事实：行背景占满整行，文字固定退还一段内边距，
// 否则文字会贴着胶囊圆角。随字号缩放，窄窗口不跟着一起挤。
namespace ui_token::dropdown
{
	inline constexpr float ENTRY_INSET_X_MIN = spacing::SM;
	inline constexpr float ENTRY_INSET_X_FONT_RATIO = 0.75f;

	inline float EntryInsetX(const float FontSize)
	{
		return std::max(ENTRY_INSET_X_MIN, FontSize * ENTRY_INSET_X_FONT_RATIO);
	}

	// 行高已知时按行高**夹取**：文字内退不该超过胶囊端帽半径（行高一半）。
	// 注意方向：这是上限，不是「行高越大内退越多」——早先这里写成 max(..., spacing::LG)，
	// 等于一路加到 16，再叠上轴向用错（见 EntryLabelRect），条目文字反而一直贴着胶囊左缘。
	inline float EntryInsetX(const float FontSize, const float RowHeight)
	{
		return std::min(EntryInsetX(FontSize), std::max(0.0f, RowHeight) * 0.5f);
	}

	// 条目文字矩形：行背景占满整行，文字只在**左右**方向退开。
	// CUIRect 的轴向名字反直觉，踩过一次：HMargin(Cut) == Margin(vec2(0, Cut)) 改的是 y/h
	// （上下），VMargin(Cut) == Margin(vec2(Cut, 0)) 才是左右。弹层条目曾写成 HMargin，
	// 于是横向零内退（文字贴着胶囊左缘）、纵向还被压掉两段行高。
	// 这里把轴向与档位一次算清，调用点（弹层条目、「发送给」弹层、二级面板提示条）共用。
	inline CUIRect EntryLabelRect(const CUIRect &Row, const float Padding, const float FontSize, const float InsetXOverride = 0.0f)
	{
		const float InsetX = InsetXOverride > 0.0f ? InsetXOverride : EntryInsetX(FontSize, Row.h);
		CUIRect Label = Row;
		Label.Margin(Padding, &Label);
		Label.VMargin(InsetX, &Label);
		return Label;
	}
} // namespace ui_token::dropdown

// 控件的内部排版事实：文字、图标、内边距三者只有一个真值来源。
// 约定：**图标字形 em = 控件文字字号**，图标盒子由 ICON_INK_RATIO 反推，
// 与 qm_icon.h 的 QmIconFontSize（盒 × 0.8）严格互逆，所以「盒子 → 字形」
// 走一圈回来正好等于文字字号，不会再出现图标比文字大/小一截。
// 入参一律是已经解析好的字号/行高（来自 SSettingsContentMetrics 或调用方自己的
// 面板字号），这里不重复定义任何绝对像素。
namespace ui_token::control
{
	inline constexpr float ICON_INK_RATIO = 0.8f;
	// 输入框尾部单位/后缀相对正文的比例，沿用既有观感，只留这一处。
	inline constexpr float SUFFIX_RATIO = 0.82f;

	inline constexpr float PadX(const float BodySize) { return std::max(spacing::XS, BodySize * 0.5f); }
	inline constexpr float PadY(const float LineHeight, const float BodySize) { return std::max(0.0f, (LineHeight - BodySize) * 0.5f); }
	inline constexpr float IconTextGap(const float BodySize) { return std::max(spacing::XS, BodySize * 0.33f); }
	inline constexpr float LabelControlGap(const float BodySize) { return std::max(spacing::SM, BodySize * 0.5f); }
	// 字形 → 盒子（与 QmIconFontSize 互逆）。
	inline constexpr float IconBox(const float GlyphSize) { return GlyphSize / ICON_INK_RATIO; }
	// 开关轨道最小宽度：2.75 倍正文，@12 得 33，等于原来 max(h*1.65, 30) 在 h=20 的结果。
	inline constexpr float SwitchMinWidth(const float BodySize) { return 2.75f * BodySize; }
	// 正文随视口缩放的唯一换算（与设置页度量同一条夹取）：控件拿不到
	// SSettingsContentMetrics 时用它，保证同一排控件不会一半缩放一半不缩放。
	inline constexpr float BodySizeFromScale(const float UiScale)
	{
		return std::clamp(12.0f * (UiScale > 0.0f ? UiScale : 1.0f), 10.0f, 12.0f);
	}
	inline constexpr float SwitchTrackAspect() { return 1.65f; }
} // namespace ui_token::control

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

	// 间距语义三档。以前各卡自己乘 0.35/0.4/0.5/0.6/0.75/1.15，或者写 +2.0/+5.0，
	// 结果同一张卡里相邻两行的距离和「行距」没有关系。现在只允许选档位：
	//   SubRowGap  —— 缩进后的子行（第二层内容），比常规行距紧一档；
	//   RowGap     —— 相邻两行（= 页面行距本身）；
	//   SectionGap —— 卡片内两个分区之间（= 原来的 m_SectionGap，2 倍行距）。
	// 参数一律传已经算好的 LineSpacing（含 UiScale），这里不再引入新的绝对像素。
	inline constexpr float SubRowGap(const float LineSpacing) { return LineSpacing * 0.5f; }
	inline constexpr float RowGap(const float LineSpacing) { return LineSpacing; }
	inline constexpr float SectionGap(const float LineSpacing) { return LineSpacing * 2.0f; }
	// 卡片底部动作区：比分区再重一档（例如配色列表之后、整卡级「全部设为…」按钮之前）。
	// 这个档位只该出现在「卡尾动作」，不要拿来当行间或普通分区。
	inline constexpr float ActionGap(const float LineSpacing) { return LineSpacing * 4.0f; }
} // namespace ui_token::settings

// 列表（CListBox）的整行外观与自适应高度：行高、行距、胶囊圆角、行内左右内边距、
// 可见行数上限、覆盖式滚动条尺寸都从这里取。数值全部由既有 settings/spacing 派生，
// 调用点不再各写一套字面量。
namespace ui_token::list
{
	// 行高：与设置页列表行同档（SSettingsContentMetrics::m_ListRowHeight 就是它）。
	inline constexpr float ROW_HEIGHT = settings::ROW_HEIGHT;
	// 行距只留半档：整行胶囊之间留一条读得出的缝，所有行列表统一这一档。
	inline constexpr float ROW_SPACING = settings::ROW_GAP * 0.5f;

	// 整行胶囊圆角：行高一半，超过卡片档才夹住。设置页列表行高 20~28 时正好是
	// 完整胶囊；更高的单列行（好友 36、国家旗 44 等旧列表）停在卡片档，
	// 不会被拉成体育场；绘制层仍按短边一半兜底。
	inline float RowRadius(const float RowHeight)
	{
		return std::min(std::max(0.0f, RowHeight) * 0.5f, radius::CARD);
	}

	// 可见行数：默认最多 3 行，显式放宽最多 8 行；皮肤队列单独 6 行，预设栏 2~3 行。
	inline constexpr int DEFAULT_MAX_VISIBLE_ROWS = 3;
	inline constexpr int MAX_VISIBLE_ROWS = 8;
	inline constexpr int SKIN_QUEUE_MAX_VISIBLE_ROWS = 6;
	inline constexpr int PRESET_MIN_VISIBLE_ROWS = 2;
	inline constexpr int PRESET_MAX_VISIBLE_ROWS = 3;

	// 行内左右内边距与弹层条目同一条公式（max(8, 字号 × 0.75)）：文字不再贴着
	// 胶囊左缘，窄窗口里也不会跟着一起挤没。
	inline float RowInsetX(const float FontSize) { return dropdown::EntryInsetX(FontSize); }

	// 容器圆角按容器长宽推导：上限是短边一半，档位随高度在 MENU→CARD 之间连续增长，
	// 所以 3 行以下偏菜单档、6 行以上完全到卡片档，列表增减一行也不会跳档。
	// 与文本按钮「按尺寸取圆角」是同一条思路，列表不再各自写死一个 5。
	inline float ContainerRadius(const float Width, const float Height)
	{
		const float ShortSide = std::max(0.0f, std::min(Width, Height));
		const float Growth = std::clamp(Height / (ROW_HEIGHT * 6.0f), 0.0f, 1.0f);
		const float Preferred = radius::MENU + (radius::CARD - radius::MENU) * Growth;
		return std::min(Preferred, ShortSide * 0.5f);
	}

	// 覆盖式滚动条：细轨压在内容上、不占内容宽度；出现/消失走同一档淡入 + 横向滑出。
	inline constexpr float SCROLLBAR_WIDTH = spacing::SM;
	inline constexpr float SCROLLBAR_MARGIN = spacing::XS;
	inline constexpr float SCROLLBAR_SLIDE = spacing::XS;

	// 条目数与每行条目数决定可见行数：0 表示空列表不占高度。
	inline int VisibleRows(const int ItemCount, const int ItemsPerRow, const int MinRows, const int MaxRows)
	{
		if(ItemCount <= 0 || MaxRows <= 0)
			return 0;
		const int PerRow = std::max(1, ItemsPerRow);
		const int RequiredRows = (ItemCount + PerRow - 1) / PerRow;
		return std::clamp(RequiredRows, std::clamp(MinRows, 0, MaxRows), MaxRows);
	}

	// 行数与行距决定的内容高度：首行之前、末行之后都不留行距。
	inline float RowsHeight(const int Rows, const float RowHeight, const float RowSpacing)
	{
		if(Rows <= 0)
			return 0.0f;
		return Rows * std::max(0.0f, RowHeight) + std::max(0, Rows - 1) * std::max(0.0f, RowSpacing);
	}
} // namespace ui_token::list

// 设置页行类型：每种行只描述「上间距 + 左缩进」两个尺寸事实，全部由既有 spacing
// token 派生。这里只增不删，调用点按类型逐步迁移，未迁移的行保持原样。
namespace ui_token::settings::row
{
	struct SRowMetrics
	{
		float m_Spacing; // 行上方间距
		float m_Indent; // 左侧缩进
	};

	inline constexpr SRowMetrics PRIMARY{spacing::SM, 0.0f};
	inline constexpr SRowMetrics GROUP_HEADER{spacing::LG, 0.0f};
	inline constexpr SRowMetrics SUB{spacing::XS, spacing::MD};
	inline constexpr SRowMetrics DIVIDER{spacing::SM, 0.0f};
	inline constexpr SRowMetrics FOLD{spacing::SM, 0.0f};
	inline constexpr SRowMetrics NOTE{spacing::XS, spacing::MD};

	// 分割线厚度：设置页统一 1px 细线，横向线不按圆角绘制。
	inline constexpr float DIVIDER_THICKNESS = 1.0f;
	// 折叠行分割线取卡片边框色压暗一档：亮度降到 0.7，透明度不动，
	// 线永远比卡片轮廓更弱，不会跟卡片描边抢视线。
	inline constexpr float FOLD_LINE_BRIGHTNESS = 0.7f;

	enum class EType
	{
		PRIMARY,
		GROUP_HEADER,
		SUB,
		DIVIDER,
		FOLD,
		NOTE,
	};

	// 按行类型取尺寸事实的唯一入口；未知类型退回 PRIMARY，调用点不用再判空。
	inline constexpr SRowMetrics Of(const EType Type)
	{
		switch(Type)
		{
		case EType::PRIMARY: return PRIMARY;
		case EType::GROUP_HEADER: return GROUP_HEADER;
		case EType::SUB: return SUB;
		case EType::DIVIDER: return DIVIDER;
		case EType::FOLD: return FOLD;
		case EType::NOTE: return NOTE;
		}
		return PRIMARY;
	}
} // namespace ui_token::settings::row

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

// 服务器信息页的键值行：三行信息（版本 / 游戏模式 / 延迟）与收藏 / 暴露 IP
// 开关行共用同一套行高与行距，步长只在这里定义，调用点不再各写 15 / 18 / 2。
// 右侧服务器筛选面板：行高、组间距与胶囊网格的度量集中在这里，
// 底部快捷条与筛选面板共用同一套密度，避免两处各写 18 / 24 / 26。
namespace ui_token::server_filter
{
	inline constexpr float LINE_HEIGHT = 16.0f;
	inline constexpr float LINE_SPACING = 3.0f;
	inline constexpr float BODY_FONT_SIZE = 12.0f;
	inline constexpr float SECTION_GAP = 6.0f;
	inline constexpr float SECTION_TITLE_SCALE = 1.3f;
	// 胶囊最小宽度按内容分档：星标档位短，模式名长。给错成 6 会让一行塞满 10 个胶囊，
	// 每个都只剩省略号。
	inline constexpr float CHIP_STAR_MIN_WIDTH = 40.0f;
	inline constexpr float CHIP_LABEL_MIN_WIDTH = 68.0f;
	inline constexpr float CHIP_HEIGHT = 24.0f;
	inline constexpr float CHIP_GAP = 4.0f;
	// FilterChip 是筛选胶囊与设置页二选一胶囊的唯一绘制入口，字号随该组统一。
	inline constexpr float CHIP_FONT_SIZE = 11.0f;
	inline constexpr float CHIP_MIN_FONT_SIZE = 9.0f;
	inline constexpr float DIVIDER_HEIGHT = 2.0f;
	inline constexpr float DIVIDER_THICKNESS = 1.0f;
	inline constexpr float DIVIDER_INSET_RATIO = 0.1f;
	inline constexpr float DIVIDER_INSET_MAX = 16.0f;
} // namespace ui_token::server_filter

namespace ui_token::server_info
{
	inline constexpr float ROW_HEIGHT = 15.0f;
	inline constexpr float ROW_GAP = 2.0f;
	inline constexpr float MIN_ROW_HEIGHT = 11.0f;
	inline constexpr float BLOCK_PADDING = 5.0f;
	inline constexpr float COPY_BUTTON_HEIGHT = 15.0f;
	inline constexpr float LABEL_COLUMN_WIDTH = 80.0f;
	inline constexpr float TOGGLE_WIDTH = 30.0f;
	inline constexpr float TOGGLE_LABEL_GAP = 6.0f;
	inline constexpr float LABEL_FONT_INSET = 4.0f;
	inline constexpr float EXPOSE_APPEAR_DURATION = 0.15f;
	inline constexpr float EXPOSE_SLIDE = 4.0f;
	inline constexpr float EXPOSE_SCALE_FROM = 0.85f;
} // namespace ui_token::server_info

namespace ui_token::confirm_dialog
{
	inline constexpr float MIN_WIDTH = 300.0f;
	inline constexpr float MAX_WIDTH = 380.0f;
	inline constexpr float TITLE_HEIGHT = 24.0f;
	inline constexpr float BUTTON_HEIGHT = 28.0f;
} // namespace ui_token::confirm_dialog

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

// 气泡（tooltip）的唯一尺寸来源。设置页的紧凑小字气泡就是全局标准，服务器列表、
// 好友列表等普通气泡不再各用一套更大的字号/内边距；行距由文字引擎按同一字号
// 推导，所以字号统一即行距统一，这里不另设行距。
namespace ui_token::tooltip
{
	// 阅读字号，与设置页小字同一档。
	inline constexpr float FONT_SIZE = font::SMALL;
	// qm_tooltip_font_size 的值就是字号本身：默认值等于 FONT_SIZE，缩放系数为 1。
	// 内边距、外边距、圆角、宽度上限都按 FONT_SIZE 的比例定义，所以配置调大时
	// 它们跟着等比放大。这里不做「配置 14 = 1 倍」的二次换算，否则设置页里的
	// 「气泡预览」用配置值直接画字，预览会比真实气泡大一号。
	inline constexpr float FONT_SIZE_CONFIG_REFERENCE = FONT_SIZE;
	// 相对于 FONT_SIZE 的比例：内边距 0.3 字高、外边距 0.4 字高、圆角 0.3 字高。
	inline constexpr float PADDING = 3.0f;
	inline constexpr float MARGIN = 4.0f;
	inline constexpr float RADIUS = 3.0f;
	// 默认阅读宽度上限；设置页与普通页面共用同一档，窄视口按比例收窄。
	inline constexpr float MAX_WIDTH = 300.0f;
} // namespace ui_token::tooltip

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
	// 导航滑块出现／消失：比普通悬停反馈长一档，配合由小到大的缩放才读得出「长出来」，
	// 0.12 秒的纯淡入在整页切换的同一帧里几乎看不见。
	inline constexpr SUiAnimTransition NAVIGATION_APPEAR{.m_DurationSec = 0.26f, .m_Easing = EEasing::EASE_OUT_QUART};
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
	// 交互控件驱动的显隐（折叠区展开、开关多出来的参数行）不能沿用列表入场的
	// smoothstep：那条曲线前后都慢，前两帧几乎不动，读作「点了没反应」。这里用
	// 更短时长 + ease-out（起步立刻见效、收尾平滑），手感才跟手。
	inline constexpr float INTERACTIVE_REVEAL_SEC = 0.14f;
	inline constexpr float INTERACTIVE_REVEAL_SEC_REDUCED = 0.09f;

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

// 覆盖式滚动条的出现/消失复用菜单悬停反馈的时长与曲线：同一档节奏，不再新开一套。
// 放在 motion 之后是因为 token 引用声明顺序，语义仍属于列表。
namespace ui_token::list
{
	inline constexpr const SUiAnimTransition &SCROLLBAR_FADE = motion::HOVER_FADE;
} // namespace ui_token::list

#endif
