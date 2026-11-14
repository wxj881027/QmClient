// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITOKENS_H
#define GAME_CLIENT_QMUI_UITOKENS_H

#include "QmAnimCurves.h"

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <algorithm>

// QmUi 设计令牌（design token）的唯一来源：颜色、间距、圆角、字号、动效时长都从这里取，
// 调用点只选令牌，不再写字面量数值。令牌一律不含缩放，调用点按响应式 UiScale 再乘一档，
// 与 menus_qmclient.cpp 既有的 LG_* 约定一致；令牌自带缩放会造成双份缩放。

namespace ui_token::color
{
	// ── 表面（surface）──
	// 半透明前景解析无法采样场景时使用的背板参考色，不改变实际表面绘制。
	inline constexpr ColorRGBA SURFACE_BACKDROP{0.10f, 0.10f, 0.10f, 1.0f};
	// 通用玻璃底板：菜单页与悬浮面板的默认底，不透明度已写在 alpha 里（0.70）。
	inline constexpr ColorRGBA SURFACE_GLASS{0.08f, 0.09f, 0.12f, 0.70f};
	// 抬升一层的底板：压在玻璃之上的卡片与弹层，比 SURFACE_GLASS 更实一档（0.82）。
	inline constexpr ColorRGBA SURFACE_ELEVATED{0.10f, 0.11f, 0.14f, 0.82f};
	// 全屏遮罩：模态弹窗背后的压暗层。只压暗，不参与表面明暗推导。
	inline constexpr ColorRGBA SURFACE_OVERLAY{0.0f, 0.0f, 0.0f, 0.40f};
	// 白色 5% 提亮，是下面 TINT_* 三族的基准档；改这一档等于同时改三族的起点。
	inline constexpr ColorRGBA SURFACE_HIGHLIGHT{1.0f, 1.0f, 1.0f, 0.05f};
	// SURFACE_HIGHLIGHT 的语义档：数值等于原来的 WithMultipliedAlpha 结果，换名不改观感。
	// 按三族分开命名，调一档只改一处，也不会顺手带走别族的手感。
	// 一、面板/玻璃上的表面反馈（底本身已有 0.70–0.82 不透明度）
	// 控件悬停底；选中底比它再亮一档，两者的差就是「鼠标在上面」与「已选中」的区别。
	inline constexpr ColorRGBA TINT_CONTROL_HOVER{1.0f, 1.0f, 1.0f, 0.030f}; // 5% ×0.6
	inline constexpr ColorRGBA TINT_CONTROL_SELECTED{1.0f, 1.0f, 1.0f, 0.050f}; // 5%
	// 分区（zone）的悬停/选中：比控件档略亮，用来标记「这一整块可点」。
	inline constexpr ColorRGBA TINT_ZONE_HOVER{1.0f, 1.0f, 1.0f, 0.035f}; // 5% ×0.7
	inline constexpr ColorRGBA TINT_ZONE_SELECTED{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	// 列表行悬停底：整行是热区，比控件悬停亮一档才看得出「点到的是这行」。
	inline constexpr ColorRGBA TINT_ROW_HOVER{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	// 绑定设置页那种整行可点的大行：悬停时比卡片内小行更亮。数值与
	// LIST_ITEM_SELECTED 相同但语义不同（这里是悬停，那边是选中），不要合并。
	inline constexpr ColorRGBA TINT_ROW_ACTIVE{1.0f, 1.0f, 1.0f, 0.140f}; // 5% ×2.8
	// 二、可辨识的立体部件（滚轮底座、倾斜板）
	// 立体部件的常态底色与活动态底色：活动态再亮 1.5 倍，读作被按下去/被点亮。
	inline constexpr ColorRGBA TINT_PART{1.0f, 1.0f, 1.0f, 0.060f}; // 5% ×1.2
	inline constexpr ColorRGBA TINT_PART_ACTIVE{1.0f, 1.0f, 1.0f, 0.090f}; // 5% ×1.8
	// 三、游戏内叠层（直接压在画面上，5% 在亮地图几乎不可见）
	// HUD 条形底、HUD 卡片底、以及 HUD 卡片的活动态（活动态最亮，压得住地图细节）。
	inline constexpr ColorRGBA TINT_HUD_BAR{1.0f, 1.0f, 1.0f, 0.100f}; // 5% ×2
	inline constexpr ColorRGBA TINT_HUD_CARD{1.0f, 1.0f, 1.0f, 0.050f}; // 5%
	inline constexpr ColorRGBA TINT_HUD_CARD_ACTIVE{1.0f, 1.0f, 1.0f, 0.200f}; // 5% ×4
	// 卡片内预览区（皮肤/地图/激光演示）的浅色底板，比 PREVIEW_BACKDROP 亮一档。
	inline constexpr ColorRGBA PREVIEW_SURFACE{1.0f, 1.0f, 1.0f, 0.10f};
	// 表面投影：贴在外壳下沿的一条压暗，配合 ui_token::elevation 的偏移量使用。
	inline constexpr ColorRGBA SURFACE_SHADOW{0.0f, 0.0f, 0.0f, 0.12f};
	// 默认描边：卡片、容器、弹层普遍使用的 1px 白边（8%）。
	inline constexpr ColorRGBA BORDER_SUBTLE{1.0f, 1.0f, 1.0f, 0.08f};
	// 键盘/输入焦点的强调色描边，是「焦点在哪」的唯一固定反馈，不随鼠标悬停变化。
	inline constexpr ColorRGBA BORDER_FOCUS{0.4f, 0.753f, 0.957f, 0.85f};
	// 列表控件（ListBox）的整行悬停/选中：直接压在菜单底图上，比卡片内的
	// TINT_ROW_* 更亮，才压得住背板。两者不要互相替换。
	inline constexpr ColorRGBA LIST_ITEM_SELECTED{1.0f, 1.0f, 1.0f, 0.14f};
	inline constexpr ColorRGBA LIST_ITEM_HOVER{1.0f, 1.0f, 1.0f, 0.08f};

	// ── 强调色（accent）──
	// 强调色族的兜底值；用户配置了 qm_ui_accent_color 时由 UiTheme 覆盖，这里只做默认。
	// 主强调色：选中滑块、焦点环、主按钮描边、滑条填充都取这一档。
	inline constexpr ColorRGBA ACCENT_PRIMARY{0.4f, 0.753f, 0.957f, 1.0f};
	// 悬停档：比主色更亮，用于悬停反馈。
	inline constexpr ColorRGBA ACCENT_PRIMARY_HOVER{0.55f, 0.82f, 0.98f, 1.0f};
	// 按下档：比主色更暗，按下时压一档，避免与悬停混淆。
	inline constexpr ColorRGBA ACCENT_PRIMARY_PRESS{0.30f, 0.65f, 0.88f, 1.0f};
	// 弱化档（18%）：用于选中底、标签底、强调色的面填充。
	inline constexpr ColorRGBA ACCENT_PRIMARY_DIM{0.4f, 0.753f, 0.957f, 0.18f};
	// 离散滑块的空槽保持中性灰，填充色由档位样式单独决定。
	inline constexpr ColorRGBA SLIDER_TRACK{0.27f, 0.27f, 0.27f, 1.0f};

	// ── 文本（text）──
	// 正文主色：标题、正文、可读内容一律用它；纯白，不随界面透明度变淡。
	inline constexpr ColorRGBA TEXT_PRIMARY{1.0f, 1.0f, 1.0f, 1.0f};
	// 次级正文（说明、单位、辅助信息）：比正文暗一档，读得出主次。
	inline constexpr ColorRGBA TEXT_SECONDARY{1.0f, 1.0f, 1.0f, 0.72f};
	// 提示文本：气泡正文、脚注一类最弱但仍需可读的文字。
	inline constexpr ColorRGBA TEXT_TIP{1.0f, 1.0f, 1.0f, 0.65f};
	// 禁用态文本：控件不可交互时压到 38%，仍要能读出内容。
	inline constexpr ColorRGBA TEXT_DISABLED{1.0f, 1.0f, 1.0f, 0.38f};
	// 压在强调色填充上的文字：深色，保证浅强调色底上的对比度。
	inline constexpr ColorRGBA TEXT_ON_ACCENT{0.06f, 0.08f, 0.11f, 1.0f};
	// 卡片副标题（作者、来源、次级说明）：压在标题下面一行，比标题暗一档才读得出
	// 主次。数值当前与 TEXT_SECONDARY 同档，但语义独立——「正文的次级」和「卡片
	// 标题下的副标题」是两件事，以后调副标题只改这里，不会连带弱化别处次级正文。
	inline constexpr ColorRGBA TEXT_SUBTITLE{1.0f, 1.0f, 1.0f, 0.72f};

	// ── 语义色（semantic）──
	// 成功：保存成功、翻译完成一类的正向结果。
	inline constexpr ColorRGBA SUCCESS{0.42f, 0.85f, 0.52f, 1.0f};
	// 警告：可用但需注意，例如长度超限、配置冲突。
	inline constexpr ColorRGBA WARNING{0.98f, 0.78f, 0.30f, 1.0f};
	// 危险：删除、断连、错误一类的负向结果，与删除按钮同一档红。
	inline constexpr ColorRGBA DANGER{0.95f, 0.41f, 0.38f, 1.0f};
	// 角色色：好友与收藏在各列表里反复出现，集中一处避免各页各写一份金色/粉色。
	// 收藏用的金色同时承担"已标记"的软警告语义，不再单独复制一个 WARNING_SOFT。
	// 收藏星与好友名的颜色；两者不随主题强调色变，玩家靠颜色认人。
	inline constexpr ColorRGBA FAVORITE{1.0f, 0.85f, 0.0f, 1.0f};
	inline constexpr ColorRGBA FRIEND{0.95f, 0.45f, 0.62f, 1.0f};
	// 列表分组标题的底色层，比普通行更弱，只用来区分组与组。
	inline constexpr ColorRGBA SURFACE_SUBHEADER{0.0f, 0.0f, 0.0f, 0.15f};

	// 中性极值：只表达颜色本身，不承载角色；角色语义（正文/强调/危险）一律
	// 引用上面的 TEXT_*/ACCENT_*/DANGER token。顶点乘法直通（设白让纹理原色
	// 通过）与纯色基准、透明复位都引用这里，禁止再写裸 ColorRGBA 字面量。
	// 白：让纹理原色直通；黑：纯色基准；透明：复位与「不填充」的唯一写法。
	inline constexpr ColorRGBA NEUTRAL_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
	inline constexpr ColorRGBA NEUTRAL_BLACK{0.0f, 0.0f, 0.0f, 1.0f};
	inline constexpr ColorRGBA COLOR_TRANSPARENT{0.0f, 0.0f, 0.0f, 0.0f};
	// 手动文本模式的强制前景（ResolveUiTextColor 的模式 1/2），与自动对比推导无关。
	// 与 NEUTRAL_* 同值但语义不同：这里是「用户指定白/黑字」，那边是「颜色基准」。
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
	// 截图编辑器底板：接近不透明的深色画布，避免截图内容与界面底色混淆。
	inline constexpr ColorRGBA SCREENSHOT_EDITOR_BACKDROP{0.06f, 0.07f, 0.09f, 0.95f};
	// 截图编辑器里禁用动作的浅色层：只做压暗提示，不改变按钮文字前景。
	inline constexpr ColorRGBA SCREENSHOT_EDITOR_DISABLED_ACTION{1.0f, 1.0f, 1.0f, 0.08f};

	// 容器与滚动条的默认外观。与既有 token 的近似值（BORDER_SUBTLE 0.08 等）
	// 是当前视觉现状，统一视觉时改这里一处生效，不在调用点写死。
	// 容器填充：列表容器、分区容器的主底色（深蓝灰 0.72）。
	inline constexpr ColorRGBA CONTAINER_FILL{0.17f, 0.18f, 0.22f, 0.72f};
	// 容器顶部提亮：贴在容器上沿的一层浅光，让容器看起来有厚度。
	inline constexpr ColorRGBA CONTAINER_HIGHLIGHT{1.0f, 1.0f, 1.0f, 0.06f};
	// 容器描边：与 BORDER_SUBTLE 同档但语义独立，容器统一改视觉时只动这里。
	inline constexpr ColorRGBA CONTAINER_BORDER{1.0f, 1.0f, 1.0f, 0.10f};
	// 滚动条细轨：压在内容之下、不占内容宽度的那条浅槽。
	inline constexpr ColorRGBA SCROLLBAR_TRACK{1.0f, 1.0f, 1.0f, 0.12f};
	// 滚动条滑块：可拖动的那一段，比轨道亮一档才看得出可抓取。
	inline constexpr ColorRGBA SCROLLBAR_THUMB{1.0f, 1.0f, 1.0f, 0.42f};

	// 用户配置的界面表面色 → 实际表面色的唯一换算：ColorScale 调明暗、AlphaScale 调不透明。
	// 调用点需要「同一份配置色的不同明暗档」时用它，不要各自乘系数。
	inline ColorRGBA UiColorSurface(ColorRGBA UiColor, float AlphaScale, float ColorScale)
	{
		return ColorRGBA(
			std::clamp(UiColor.r * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.g * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.b * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.a * AlphaScale, 0.0f, 1.0f));
	}

	// 只缩放透明度、不改 RGB：用于「同一色调的弱化档」（悬停底、选中底）。
	inline ColorRGBA UiColorAccent(ColorRGBA UiColor, float AlphaScale)
	{
		return UiColor.WithAlpha(std::clamp(UiColor.a * AlphaScale, 0.0f, 1.0f));
	}
} // namespace ui_token::color

// 离散滑块档位色板与满档渐变：UiDiscreteSliderStyle.h 与 UiDiscreteSliderLogic.cpp
// 共享的唯一来源，禁止在调用文件复制。
namespace ui_token::discrete_slider
{
	// 四档色板，索引随档位升高：低档冷蓝 → 蓝紫 → 粉紫 → 高档亮粉。
	// 档位值就是数组下标，调用点直接取，不再按百分比插值。
	inline constexpr ColorRGBA TIER_COLORS[] = {
		ColorRGBA{0.48f, 0.67f, 0.91f, 1.0f}, // 第 1 档：冷蓝
		ColorRGBA{0.65f, 0.54f, 0.91f, 1.0f}, // 第 2 档：蓝紫
		ColorRGBA{0.84f, 0.48f, 0.78f, 1.0f}, // 第 3 档：粉紫
		ColorRGBA{0.92f, 0.45f, 0.69f, 1.0f}, // 第 4 档：亮粉
	};
	// 满档渐变的三段锚点：起点紫、中点亮紫、终点灰紫；三者顺序即渐变方向。
	inline constexpr ColorRGBA GRADIENT_START{0.51f, 0.16f, 0.52f, 1.0f};
	// 中点亮紫：满档时穿过这里，视觉上最亮的一段。
	inline constexpr ColorRGBA GRADIENT_MIDDLE{0.77f, 0.53f, 0.97f, 1.0f};
	// 终点灰紫：比中点亮紫收一档，避免满档边缘过曝。
	inline constexpr ColorRGBA GRADIENT_END{0.57f, 0.32f, 0.79f, 1.0f};
} // namespace ui_token::discrete_slider

// 图标按钮反馈统一强度；透明背景也保留可见的边界提示。
namespace ui_token::feedback
{
	// 悬停时压在按钮上的白色透明度。
	inline constexpr float ICON_HOVER_ALPHA = 0.16f;
	// 按下时再压深一档，与悬停区分得开。
	inline constexpr float ICON_PRESS_ALPHA = 0.30f;
	// 活动态描边的透明度（ACTIVE_BORDER 直接引用这里）。
	inline constexpr float ICON_BORDER_ALPHA = 0.70f;
	// 活动态描边宽度：固定 1 像素，不随 UiScale 变粗，否则悬停时会「胖一圈」。
	inline constexpr float ICON_BORDER_WIDTH = 1.0f;
	// 悬停 / 按下 / 选中统一使用的白色描边。按钮表面、动作组外框、键位行等
	// 所有「控件处于活动态」的描边都从这里取，禁止在调用文件重写 1,1,1,alpha。
	// 描边固定白色：它只表达「这里有反馈」，不参与强调色语义（强调色偏红时，
	// 若描边跟随强调色，每个按钮的悬停都会被读成错误态）。
	inline constexpr ColorRGBA ACTIVE_BORDER{1.0f, 1.0f, 1.0f, ICON_BORDER_ALPHA};
	// 滚动条滑块悬浮时向四周扩张的距离：公共滑块绘制入口（ui_widget::DrawScrollbarHandle）
	// 的几何反馈档，悬浮比静止大一档。
	inline constexpr float HANDLE_HOVER_GROW = 1.0f;
	// 滚动条滑块按下/拖动时再扩张一档：与悬浮拉开差距，按下有「抓得更实」的观感。
	// 宿主（聊天滚动条）按这一档在轨道两端预留余量，放大后的滑块不会顶出轨道。
	inline constexpr float HANDLE_PRESS_GROW = 2.0f;
}

// 输入框聚焦光晕：从外壳边缘向外铺开的衰减光带，与输入框模式无关——
// 文本、搜索、多行三种模式共用这一份令牌，哪个框亮只由焦点决定。
// 宽度跟着控件高度走并夹在上下限内——同一个令牌在 20 高的服务器筛选框和 32 高的
// 设置页搜索卡上都不会顶着一团比例失调的光。内外两层的分工：内层窄而亮、
// 贴着外壳收口；外层宽而淡，负责氤氲。只叠一层会看到「硬边」，只叠外层又不够亮。
// 强度刻意压得很低：参考设计里真正抓住眼睛的是那条强调色描边，光晕只是描边外面
// 的一圈淡光；光晕抢戏就会变成霓虹灯，浅色表面上尤其明显（见 LIGHT_SURFACE_DIM）。
namespace ui_token::glow
{
	// 光晕宽度相对外壳高度的比例：外壳越高，光带越宽。
	inline constexpr float INPUT_FOCUS_SPREAD_RATIO = 0.20f;
	// 宽度下限：再小的输入框也要留得住一圈可见的光。
	inline constexpr float INPUT_FOCUS_SPREAD_MIN = 2.5f;
	// 宽度上限：高输入框不让光带继续涨，否则会盖住相邻控件。
	inline constexpr float INPUT_FOCUS_SPREAD_MAX = 5.0f;
	// 内层宽度相对外层的比例；两层共用同一个衰减带宽基准，叠起来才是连续的晕。
	inline constexpr float INPUT_FOCUS_INNER_RATIO = 0.50f;
	// 衰减指数：越小越像一片氤氲，越大越贴着边缘收成一道细光。
	inline constexpr float INPUT_FOCUS_OUTER_FALLOFF = 3.2f;
	inline constexpr float INPUT_FOCUS_INNER_FALLOFF = 1.6f;
	// 强度取「聚焦进度 × 令牌」：聚焦淡入时整团光跟着亮起，不额外开一条动画轨道。
	// 这组值已经是「看得出有光、但不会盖过描边」的档位；再往上加会先亮成霓虹，
	// 再往外扩成一圈雾。要更明显请先调描边，不要只加倍光晕。
	inline constexpr float INPUT_FOCUS_OUTER_ALPHA = 0.12f;
	inline constexpr float INPUT_FOCUS_INNER_ALPHA = 0.22f;
	// 表面亮度对光晕的压暗比例：同一份 alpha 压在白卡上比压在深色卡上亮一档，
	// 最亮表面压到 55%。深色主题几乎不受影响。
	inline constexpr float LIGHT_SURFACE_DIM = 0.45f;
	// 焦点安全裁剪要为光晕放行的最大距离：卡片内容区裁剪按它外扩，
	// 否则光晕会在卡片内被切成一条直边；外扩后仍与卡片矩形取交集，永远不出卡。
	inline constexpr float FOCUS_CLIP_ALLOWANCE = INPUT_FOCUS_SPREAD_MAX;

	// 光晕宽度：按外壳高度取比例后夹取，调用点不再自己算像素。
	inline constexpr float InputFocusSpread(const float ShellHeight)
	{
		return std::clamp(std::max(0.0f, ShellHeight) * INPUT_FOCUS_SPREAD_RATIO, INPUT_FOCUS_SPREAD_MIN, INPUT_FOCUS_SPREAD_MAX);
	}
} // namespace ui_token::glow

// 间距标尺：只放「与字号无关」的绝对间距档，供下面各命名空间派生。
// 需要跟字号走的间距请用 ui_token::control / ui_token::settings 的派生函数，
// 不要在这里新增绝对像素，也不要复制这几个数字。
namespace ui_token::spacing
{
	inline constexpr float XS = 4.0f; // 最小缝：图标与文字、行内元素之间
	inline constexpr float SM = 8.0f; // 常规间距：控件之间、行内左右留白
	inline constexpr float MD = 12.0f; // 分区内的中等间距
	inline constexpr float LG = 16.0f; // 分区之间、卡片内边距
	inline constexpr float XL = 24.0f; // 页面级留白、卡片之间
} // namespace ui_token::spacing

// 弹层条目（下拉框/选择弹窗）的排版事实：行背景占满整行，文字固定退还一段内边距，
// 否则文字会贴着胶囊圆角。随字号缩放，窄窗口不跟着一起挤。
namespace ui_token::dropdown
{
	// 条目文字横向内退的下限，防止小字号时内退小到贴住圆角。
	inline constexpr float ENTRY_INSET_X_MIN = spacing::SM;
	// 横向内退相对字号的比例（0.75 字号）；与下限取大者。
	inline constexpr float ENTRY_INSET_X_FONT_RATIO = 0.75f;

	// 只按字号算内退：字号未知行高时用它（例如弹层尚未排版）。
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
	// InsetXOverride 用于调用点已有更准确的内退值（例如行内已排过图标）时覆盖默认值。
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
	// 图标字形占图标盒子的比例（0.8）；与 QmIconFontSize 互逆，改动需同步那边。
	inline constexpr float ICON_INK_RATIO = 0.8f;
	// 输入框尾部单位/后缀相对正文的比例，沿用既有观感，只留这一处。
	inline constexpr float SUFFIX_RATIO = 0.82f;

	// 控件左右内边距：至少一个 XS，随正文放大（0.5 字号）。
	inline constexpr float PadX(const float BodySize) { return std::max(spacing::XS, BodySize * 0.5f); }
	// 控件上下内边距：由「行高 - 正文字号」的一半得出，保证文字垂直居中。
	inline constexpr float PadY(const float LineHeight, const float BodySize) { return std::max(0.0f, (LineHeight - BodySize) * 0.5f); }
	// 图标与相邻文字的间距（0.33 字号，最小 XS）。
	inline constexpr float IconTextGap(const float BodySize) { return std::max(spacing::XS, BodySize * 0.33f); }
	// 左侧标签与右侧控件之间的最小间距（0.5 字号，最小 SM）。
	inline constexpr float LabelControlGap(const float BodySize) { return std::max(spacing::SM, BodySize * 0.5f); }
	// 字形 → 盒子（与 QmIconFontSize 互逆）。
	inline constexpr float IconBox(const float GlyphSize) { return GlyphSize / ICON_INK_RATIO; }
	// 开关轨道最小宽度：2.75 倍正文，@12 得 33，等于原来 max(h*1.65, 30) 在 h=20 的结果。
	inline constexpr float SwitchMinWidth(const float BodySize) { return 2.75f * BodySize; }
	// 正文随视口缩放的唯一换算（与设置页度量同一条夹取）：控件拿不到
	// SSettingsContentMetrics 时用它，保证同一排控件不会一半缩放一半不缩放。
	// 上下限夹在 10~12：再小读不清，再大挤压行高。
	inline constexpr float BodySizeFromScale(const float UiScale)
	{
		return std::clamp(12.0f * (UiScale > 0.0f ? UiScale : 1.0f), 10.0f, 12.0f);
	}
	// 开关轨道的宽高比：按行高反推轨道宽度时用（宽 = 高 × 1.65）。
	inline constexpr float SwitchTrackAspect() { return 1.65f; }
} // namespace ui_token::control

// 圆角标尺集中保存既有用途的数值；调用点选择用途，不再写字面量半径。
// 需要按控件尺寸自动取圆角的场合（按钮、胶囊、列表容器）请调用各自的
// Resolve*Radius 派生函数，而不是在这里挑一个固定档。
namespace ui_token::radius
{
	inline constexpr float NONE = 0.0f; // 无圆角：方块、分隔条、贴边面板
	inline constexpr float TIGHT = 6.0f; // 紧圆角：小徽标、标签、输入框内的小块
	inline constexpr float BASE = 8.0f; // 基础圆角：按钮、输入框、行背景的默认档
	inline constexpr float SMALL_MENU = 5.0f; // 小菜单圆角：弹出菜单、下拉条目
	inline constexpr float MENU = 10.0f; // 菜单圆角：菜单页、面板、容器
	inline constexpr float CARD = 14.0f; // 卡片圆角：设置卡片、大面板
	inline constexpr float PILL = 999.0f; // 由绘制层按短边一半裁剪
} // namespace ui_token::radius

// 设置页排版标尺：页面内边距、页签、卡片、行高与行距都在这里。
// 数值全部是「未缩放」的基准值，实际使用经过 SSettingsContentMetrics 换算。
namespace ui_token::settings
{
	// 内容区最大宽度：宽屏下设置卡片不再无限拉宽，保持可读行长。
	inline constexpr float MAX_CONTENT_WIDTH = 1000.0f;
	// 页面右侧给覆盖式滚动条让出的宽度槽。
	inline constexpr float OUTER_SCROLLBAR_SLOT = 20.0f;
	// 页面四周内边距：内容不贴着窗口边缘。
	inline constexpr float PAGE_INSET = 16.0f;
	// 一级页签高度。
	inline constexpr float TAB_HEIGHT = 26.0f;
	// 页签字号：设置页页签条与收藏地图工作区页签条共用同一档。
	// 早先工作区页签按行高反推（26-4）×0.7≈15.4，而设置页写 13，两处并排就能看出大小差别。
	// 现在两处都显式传本档值，行高反推只作为未指定时的兜底。
	// 一级页签之间的间距。
	inline constexpr float TAB_GAP = 10.0f;
	// 页签字号（未缩放）。
	inline constexpr float TAB_FONT_SIZE = 15.0f;
	// 区块标题档：导航、页签、卡片标题共用这一个字号，不再各自声明。
	inline constexpr float SECTION_TITLE = 15.0f;
	// ESC 顶部主导航与页面页签共用同一档：高度取胶囊行高，字号取标题档。
	// 单一入口的目的就是导航与页签不再一个高一个矮。
	inline constexpr float MENU_NAV_HEIGHT = TAB_HEIGHT;
	inline constexpr float MENU_NAV_FONT_SIZE = SECTION_TITLE;
	// 子页签高度：与一级页签同档，切换时布局不跳动。
	inline constexpr float SUB_TAB_HEIGHT = TAB_HEIGHT;
	// 子页签间距：同上，与一级页签一致。
	inline constexpr float SUB_TAB_GAP = TAB_GAP;
	// 卡片之间的水平/垂直间距。
	inline constexpr float CARD_GAP = 16.0f;
	// 标准行高：一行标签+控件的高度。
	inline constexpr float ROW_HEIGHT = 20.0f;
	// 控件在行内上下各压的内边距。
	inline constexpr float CONTROL_PADDING_Y = 2.0f;
	// 行高下限：再密的行也不低于它，保证仍可点中。
	inline constexpr float MIN_ROW_HEIGHT = 16.0f;
	// 徽标（badge）高度：与行高同档，不额外占高。
	inline constexpr float BADGE_HEIGHT = 16.0f;
	// 相邻两行之间的默认行距（未乘 UiScale）。
	inline constexpr float ROW_GAP = 5.0f;
	// 双列卡片布局的最小内容宽度：窄于此改单列，避免控件被压扁。
	inline constexpr float TWO_COLUMN_MIN_WIDTH = 760.0f;
	// 卡片四周内边距。
	inline constexpr float CARD_PADDING = 14.0f;
	// 卡片标题行高度（标题字号的容器高度）。
	inline constexpr float CARD_HEADER_TITLE_HEIGHT = 18.0f;
	// 卡片副标题行高度（小字号的容器高度）。
	inline constexpr float CARD_HEADER_SUBTITLE_HEIGHT = 12.0f;
	// 卡片摘要与标题之间的间距。
	inline constexpr float CARD_SUMMARY_GAP = spacing::XS;
	// 卡片标题与副标题之间的间距。
	inline constexpr float CARD_HEADER_GAP = 6.0f;
	// 卡片摘要文本的透明度：比正文弱一档，只作快速浏览用。
	inline constexpr float CARD_SUMMARY_ALPHA = 0.72f;
	// 卡片拖拽手柄的边长（正方形热区）。
	inline constexpr float CARD_HANDLE_SIZE = 24.0f;
	// 卡片圆角：引用 radius::CARD，卡片统一改圆角只动 radius。
	inline constexpr float CARD_RADIUS = radius::CARD;

	// 间距语义三档。以前各卡自己乘 0.35/0.4/0.5/0.6/0.75/1.15，或者写 +2.0/+5.0，
	// 结果同一张卡里相邻两行的距离和「行距」没有关系。现在只允许选档位：
	//   SubRowGap  —— 缩进后的子行（第二层内容），比常规行距紧一档；
	//   RowGap     —— 相邻两行（= 页面行距本身）；
	//   SectionGap —— 卡片内两个分区之间（= 原来的 m_SectionGap，2 倍行距）。
	// 参数一律传已经算好的 LineSpacing（含 UiScale），这里不再引入新的绝对像素。
	inline constexpr float SubRowGap(const float LineSpacing) { return LineSpacing * 0.5f; }
	inline constexpr float RowGap(const float LineSpacing) { return LineSpacing; }
	// 分区之间：两倍行距（没写进注释清单里的那一条，语义见上方说明）。
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

	// 行右侧给覆盖式滚动条让出的缝：行胶囊不能压在滚动条下面（组件内部用）。
	inline constexpr float SCROLLBAR_ROW_GAP = spacing::XS;
	// 整行胶囊圆角：行高一半，超过卡片档才夹住。设置页列表行高 20~28 时正好是
	// 完整胶囊；更高的单列行（好友 36、国家旗 44 等旧列表）停在卡片档，
	// 不会被拉成体育场；绘制层仍按短边一半兜底。
	inline float RowRadius(const float RowHeight)
	{
		return std::min(std::max(0.0f, RowHeight) * 0.5f, radius::CARD);
	}

	// 可见行数：默认最多 3 行，显式放宽最多 8 行；皮肤队列单独 6 行，预设栏 2~3 行。
	// 默认上限：不给参数时的可见行数，菜单里的短列表都停在这档。
	inline constexpr int DEFAULT_MAX_VISIBLE_ROWS = 3;
	// 硬上限：任何列表都不该一次显示超过 8 行，再多应交给滚动。
	inline constexpr int MAX_VISIBLE_ROWS = 8;
	// 皮肤队列：队列通常有 5~6 项，一次显示 6 行省一次滚动。
	inline constexpr int SKIN_QUEUE_MAX_VISIBLE_ROWS = 6;
	// 预设栏下限：至少 2 行，避免只有 1 项时卡片塌成一条。
	inline constexpr int PRESET_MIN_VISIBLE_ROWS = 2;
	// 预设栏上限：3 行封顶，预设再多也滚动而不是把卡片拉高。
	inline constexpr int PRESET_MAX_VISIBLE_ROWS = 3;
	// 主题（菜单中的背景图）列表：原来 DoStart 的 RowsPerScroll 是 3，内容区由外层卡片
	// 给多少吃多少，视图高度停不到整行上，最上/最下一行的胶囊会被切一半。现在最少 3 行
	// （沿用原 RowsPerScroll 的设计意图，主题至少 3 个内置项，卡片不会因此变矮）、最多 8 行；
	// 卡片测高与 CListBox 的自适应高度共用这两个数，视图正好停在整个行的边界。
	inline constexpr int THEME_MIN_VISIBLE_ROWS = 3;

	// 行内左右内边距与弹层条目同一条公式（max(8, 字号 × 0.75)）：文字不再贴着
	// 胶囊左缘，窄窗口里也不会跟着一起挤没。
	inline float RowInsetX(const float FontSize) { return dropdown::EntryInsetX(FontSize); }

	// 容器内边距：行胶囊不能贴着容器圆角，选中/悬浮填充才不会从圆角外侧探出去
	// （实机反馈：网格里第一行的高亮压在容器圆角上，看起来「超出列表」）。
	// 三档全部由行高派生，行高越大档位越高；调用点不再各写像素，也不再新增 token。
	inline float ContainerPadding(const float RowHeight)
	{
		if(RowHeight <= 24.0f)
			return spacing::XS;
		if(RowHeight <= 40.0f)
			return spacing::SM;
		return spacing::MD;
	}

	// 容器内容高度 = 可见行内容 + 上下内边距，定义见本节末尾（PaddedRowsHeight）：
	// 调用点测高与组件收敛高度共用同一个函数，加内边距不会把最后一行挤掉
	// （「该 8 行还是 8 行」），也不会让卡片按旧公式少算两档内边距。

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

	// 行高亮胶囊的上下内缩：胶囊比行高低两档，文字与胶囊边之间留出余量，
	// 整行才不是一条压满行距的色带（统计页表格悬浮行、概览行共用这一档）。
	inline constexpr float ROW_HIGHLIGHT_INSET_Y = ROW_SPACING;

	// 覆盖式滚动条：细轨压在内容上、不占内容宽度；出现/消失走同一档淡入 + 横向滑出。
	// 轨道厚度。
	inline constexpr float SCROLLBAR_WIDTH = spacing::SM;
	// 轨道与容器右缘的外边距。
	inline constexpr float SCROLLBAR_MARGIN = spacing::XS;
	// 滚动条出现/消失时横向滑出的距离。
	inline constexpr float SCROLLBAR_SLIDE = spacing::XS;

	// 覆盖式轨道的右侧让位：轨道厚度 + 外边距 + 一条缝。列表行、浏览器面板、弹层共用这一份，
	// 不要再在调用点写 8+4+4 或 12。
	inline constexpr float OverlayRailReserve() { return SCROLLBAR_WIDTH + SCROLLBAR_MARGIN + SCROLLBAR_ROW_GAP; }

	// 条目数与每行条目数决定可见行数：0 表示空列表不占高度。
	// MinRows 先夹到 [0, MaxRows]，再用它夹住实际所需行数，保证返回值不越界。
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

	// 容器内容高度 = 可见行内容 + 上下内边距（与上面的前向声明同一份定义）。
	inline float PaddedRowsHeight(const int Rows, const float RowHeight, const float RowSpacing, const float Padding)
	{
		return RowsHeight(Rows, RowHeight, RowSpacing) + 2.0f * std::max(0.0f, Padding);
	}
} // namespace ui_token::list

// 游戏内聊天框（滚动条 + 消息气泡）的尺寸。注意单位：聊天视口由
// Graphics()->MapScreen(0, 0, W, 300) 映射，这里的长度都是"聊天逻辑单位"
// （1 单位 ≈ 屏幕高度 / 300），不是 UI 像素。列表滚动条的
// ui_token::list::SCROLLBAR_WIDTH 是 UI 像素，同一个视觉量在两个坐标空间里
// 数值本来就不同，不要互相抄数字；需要跨空间换算时由绘制点按聊天像素尺寸折算。
namespace ui_token::chat
{
	// 滚动条轨道宽度：旧实现写死 3 单位，这里比它略宽一档，窄屏与高缩放下也抓得住。
	inline constexpr float SCROLLBAR_WIDTH = spacing::XS;
	// 轨道离聊天区边缘的外边距（与旧实现同值）；沿长度方向两端也各让出这一档，
	// 滑块端头不会顶到聊天区上下边缘，整条轨道也不会顶满历史区那么高。
	inline constexpr float SCROLLBAR_MARGIN = spacing::XS * 0.5f;
	// 滑块最短长度：再短就抓不住，与旧实现同一档。
	inline constexpr float SCROLLBAR_MIN_THUMB = spacing::MD;
	// 一档滚轮推进多少行：聊天偏移单位是行，所以这里就是行数；
	// 用户调 qm_ui_scroll_step 时与列表同一份系数等比缩放。
	inline constexpr float WHEEL_LINES_PER_STEP = 1.0f;
	// 滑块最长占轨道的比例：内容只比一屏多一点时，按「可见/内容」比例算出来的滑块
	// 轨道与滑块的圆角：胶囊端头，和列表覆盖式滚动条同一档（绘制层按短边一半兜底）。
	inline constexpr float SCROLLBAR_RADIUS = radius::PILL;
	// 滑块悬浮/按下放大的预留比例上限：轨道长度方向两端各最多让出这一比例，
	// 加上 ui_token::feedback::HANDLE_PRESS_GROW 的放大后仍完整落在轨道内；

	// 消息气泡单行判定的容差：行高不超过「一行行高」的 1.25 倍就算单行，
	// 留出行距与装饰的余量；超过即按多行处理。
	inline constexpr float BUBBLE_SINGLE_LINE_TOLERANCE = 0.25f;
	// 多行气泡圆角相对「一行行高」的比例：旧实现按字号一半算（≈行高的 0.43），
	// 提一档到 0.6 后比旧观感略圆，又随字号/行高一起缩放——固定像素会让大字号
	// 聊天反而显得更方。
	inline constexpr float BUBBLE_MULTI_LINE_RADIUS_RATIO = 0.6f;

	// 单行气泡圆角 = 行高一半（完整胶囊），不再另外夹取：绘制层本来就把半径
	// 夹在短边一半以内，夹在这里会让单行看起来仍是圆角矩形而不是胶囊。
	inline float BubbleSingleLineRadius(const float LineHeight)
	{
		return std::max(0.0f, LineHeight) * 0.5f;
	}

	// 多行气泡圆角 = 一行行高 × 比例；绘制层仍按短边一半兜底，所以再矮的多行行也不会自交。
	inline float BubbleMultiLineRadius(const float RowHeight)
	{
		return std::max(0.0f, RowHeight) * BUBBLE_MULTI_LINE_RADIUS_RATIO;
	}

	// 单行判定：调用点只回答「行高是多少」，判定阈值只有这一处。
	inline bool BubbleIsSingleLine(const float LineHeight, const float RowHeight)
	{
		return LineHeight <= std::max(0.0f, RowHeight) * (1.0f + BUBBLE_SINGLE_LINE_TOLERANCE);
	}

	// 消息气泡圆角的唯一入口：单行胶囊、多行圆角矩形。
	// LineHeight 是这一行实际占用的高度，RowHeight 是「一行文字」的高度。
	inline float BubbleRadius(const bool SingleLine, const float LineHeight, const float RowHeight)
	{
		return SingleLine ? BubbleSingleLineRadius(LineHeight) : BubbleMultiLineRadius(RowHeight);
	}

	// 聊天输入行：翻译按钮与「全体」前缀块共用同一份行高。单位同样是聊天逻辑单位
	// （1 单位 ≈ 屏幕高度 / 300），不要拿 ui_token::control 的控件高度当参照物。
	// 两条边必须同源：按钮与前缀块各算一次行高，换字号后就会一大一小，又回到
	// 「按钮比文字高半截」的老问题。
	namespace input
	{
		// 行高相对字号多出的一档：相当于上下各半个内边距，让按钮读作「一行控件」
		// 而不是贴着文字的圆点。取值与旧前缀块的「字号 + 4」一致，不改变既有手感。
		inline constexpr float ROW_HEIGHT_EXTRA = 4.0f;
		// 行高下限：小字号时不再跟着缩，保证按钮仍是可点、可辨认的一块。
		inline constexpr float ROW_HEIGHT_MIN = 16.0f;
		// 按钮与右侧文字之间的缝。
		inline constexpr float BUTTON_GAP = 4.0f;
		// 按钮描边宽度占直径的比例：圆形图标按钮只需要一圈勾边。
		// 固定 1 单位在 8 单位直径的小圆上就是八分之一，读起来是「粗框」而不是「描边」。
		inline constexpr float BUTTON_BORDER_RATIO = 0.05f;
	// 图标相对内径的内缩比例：图标与圆边之间必须有边距，
	// 否则大图标顶到描边上，看起来像「图标和圆一样高」。
	inline constexpr float ICON_INSET_RATIO = 0.18f;
	// 翻译按钮直径相对输入行行高的比例：外圈比行高小一档，文字行不被按钮撑高。
	inline constexpr float BUTTON_ROW_RATIO = 0.82f;

		// 输入行行高：按钮直径与「全体」前缀块高度都只从这里取。
		inline float RowHeight(const float FontSize)
		{
			return std::max(std::max(0.0f, FontSize) + ROW_HEIGHT_EXTRA, ROW_HEIGHT_MIN);
		}

		// 按钮描边宽度：按直径比例给出，圆变大时描边仍是一圈细边，不会跟着变粗。
		inline float ButtonBorderWidth(const float Diameter)
		{
			return std::max(0.0f, Diameter) * BUTTON_BORDER_RATIO;
		}
	} // namespace input
} // namespace ui_token::chat

// 设置页行类型：每种行只描述「上间距 + 左缩进」两个尺寸事实，全部由既有 spacing
// token 派生。这里只增不删，调用点按类型逐步迁移，未迁移的行保持原样。
namespace ui_token::settings::row
{
	// 一种行的两个尺寸事实：上方间距与左侧缩进；两者都由 spacing 档位派生。
	struct SRowMetrics
	{
		float m_Spacing; // 行上方间距
		float m_Indent; // 左侧缩进
	};

	// 主行：标签 + 控件，行上方一个 SM，不缩进。
	inline constexpr SRowMetrics PRIMARY{spacing::SM, 0.0f};
	// 分组标题行：上方留 LG，与上一组拉开一段，不缩进。
	inline constexpr SRowMetrics GROUP_HEADER{spacing::LG, 0.0f};
	// 子行（第二层内容）：上方只留 XS，左侧缩进 MD 表示从属关系。
	inline constexpr SRowMetrics SUB{spacing::XS, spacing::MD};
	// 分隔行：上下对称留 SM，画一条细分隔线。
	inline constexpr SRowMetrics DIVIDER{spacing::SM, 0.0f};
	// 折叠行（可展开分区标题）：与主行同档间距，不缩进。
	inline constexpr SRowMetrics FOLD{spacing::SM, 0.0f};
	// 注释行（说明、脚注）：上方 XS、左侧缩进 MD，紧贴它所说明的那一行。
	inline constexpr SRowMetrics NOTE{spacing::XS, spacing::MD};

	// 行的类型枚举：调用点用类型名表达意图，不直接挑尺寸，避免同一行在不同卡片里数值漂移。
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
// 二级面板是浮在设置页之上的一层小面板（滚动条、库面板、新闻、更新提示），
// 尺寸比设置页更紧凑：字号 7.5、边距 3，全部按二级面板自己的比例走，不跟页面对齐。
namespace ui_token::secondary_panel
{
	// 正文字号：二级面板的基准字，其余尺寸由它派生。
	inline constexpr float BODY_SIZE = 7.5f;
	// 面板四周内边距。
	inline constexpr float MARGIN = 3.0f;
	// 标题行高度。
	inline constexpr float TITLE_HEIGHT = 16.0f;
	// 普通行高度：标签 + 控件的容器高度。
	inline constexpr float ROW_HEIGHT = 16.0f;
	// 纯标签行高度：只有一行字、没有控件时用它。
	inline constexpr float LABEL_HEIGHT = 11.0f;
	// 下拉框高度：与行高同档，视觉上不凸出。
	inline constexpr float DROPDOWN_HEIGHT = 18.0f;
	// 相邻元素之间的通用间距。
	inline constexpr float GAP = 4.0f;
	// 开关轨道宽度（二级面板的紧凑档）。
	inline constexpr float SWITCH_WIDTH = 30.0f;
	// 开关轨道高度。
	inline constexpr float SWITCH_HEIGHT = 16.0f;
	// 库面板默认宽度。
	inline constexpr float LIBRARY_WIDTH = 780.0f;
	// 库面板默认高度。
	inline constexpr float LIBRARY_HEIGHT = 470.0f;
	// 库面板高度占视口的比例：小窗口下按比例收缩，不顶满屏幕。
	inline constexpr float LIBRARY_VIEWPORT_FRACTION = 0.74f;
	// 新闻面板宽度。
	inline constexpr float NEWS_WIDTH = 820.0f;
	// 新闻面板底部动作按钮高度。
	inline constexpr float NEWS_ACTION_HEIGHT = 26.0f;
	// 更新提示面板宽度。
	inline constexpr float UPDATE_WIDTH = 600.0f;
	// 更新提示动作行的行高系数：行高 = 正文 × 2.4，两行文字加按钮刚好放下。
	inline constexpr float UPDATE_ACTION_LINE_FACTOR = 2.4f;
} // namespace ui_token::secondary_panel

// 服务器信息页的键值行：三行信息（版本 / 游戏模式 / 延迟）与收藏 / 暴露 IP
// 开关行共用同一套行高与行距，步长只在这里定义，调用点不再各写 15 / 18 / 2。
// 右侧服务器筛选面板：行高、组间距与胶囊网格的度量集中在这里，
// 底部快捷条与筛选面板共用同一套密度，避免两处各写 18 / 24 / 26。
namespace ui_token::server_filter
{
	// 一行键值的高度（标签 + 值）。
	inline constexpr float LINE_HEIGHT = 16.0f;
	// 相邻键值行之间的间距。
	inline constexpr float LINE_SPACING = 3.0f;
	// 面板正文字号。
	inline constexpr float BODY_FONT_SIZE = 12.0f;
	// 两个筛选分区之间的间距。
	inline constexpr float SECTION_GAP = 6.0f;
	// 分区标题不再比正文大一档：面板里的层级由「主/次文本色 + 分区间距」承担，
	// 字号只保留正文一档（实机反馈：分区标题明显大于正文，看起来不像同一套排版）。
	// 需要更大号的场合请用 ui_token::font::TITLE（卡片标题），不要在这里造放大系数。
	inline constexpr float SECTION_TITLE_SCALE = 1.0f;
	// 胶囊最小宽度按内容分档：星标档位短，模式名长。给错成 6 会让一行塞满 10 个胶囊，
	// 每个都只剩省略号。
	// 星标一类短标签的最小宽度。
	inline constexpr float CHIP_STAR_MIN_WIDTH = 40.0f;
	// 模式名一类长标签的最小宽度（仅在未指定每行个数时参与反推）。
	inline constexpr float CHIP_LABEL_MIN_WIDTH = 68.0f;
	// 模式分组固定每行四格：两列时格子太宽、文字与胶囊两端之间留出一大片空，
	// 扫读时反而看不出这是一组可多选的标签。四格正好铺满右侧面板的宽度。
	inline constexpr int CHIP_LABEL_PER_ROW = 4;
	// 胶囊内文字左右内边距相对字号的比例：格子变窄后文字不再顶到胶囊端头，
	// 但比例压得比按钮小，四格一行时中文短标签仍能完整显示。
	inline constexpr float CHIP_TEXT_INSET_RATIO = 0.45f;
	// 筛选胶囊高度。
	inline constexpr float CHIP_HEIGHT = 24.0f;
	// 相邻胶囊之间的间距。
	inline constexpr float CHIP_GAP = 4.0f;
	// FilterChip 是筛选胶囊与设置页二选一胶囊的唯一绘制入口，字号随该组统一。
	inline constexpr float CHIP_FONT_SIZE = 11.0f;
	// 胶囊字号下限：窄窗口压缩时不再继续变小，否则胶囊里只剩省略号。
	inline constexpr float CHIP_MIN_FONT_SIZE = 9.0f;
	// 分区之间分隔条所占的高度。
	inline constexpr float DIVIDER_HEIGHT = 2.0f;
	// 分隔线本身的粗细。
	inline constexpr float DIVIDER_THICKNESS = 1.0f;
	// 分隔线左右内缩相对宽度的比例（10%）。
	inline constexpr float DIVIDER_INSET_RATIO = 0.1f;
	// 分隔线左右内缩的上限，宽面板下不再继续内缩。
	inline constexpr float DIVIDER_INSET_MAX = 16.0f;
} // namespace ui_token::server_filter

// 状态栏（TClient 状态栏卡片与其预览）的角色色：底轨、条目底、条目标签的常态与选中态。
// 这是一组「同一组件内部的角色」，不是给用户逐项自定义的配色；改外观只改这一处，
// 卡片与实机状态栏共用同一份值。数值保持既有观感，仅从卡片里的裸字面量搬进来。
namespace ui_token::status_bar
{
	// 底轨：状态条底板，压在卡片表面上的一层暗色。
	inline constexpr ColorRGBA TRACK{0.0f, 0.0f, 0.0f, 0.5f};
	// 条目底：每个状态项自己的浅色胶囊，比底轨亮一档才分得出来。
	inline constexpr ColorRGBA ITEM_FILL{1.0f, 1.0f, 1.0f, 0.15f};
	// 条目标签的常态色。
	inline constexpr ColorRGBA ITEM_LABEL{1.0f, 1.0f, 1.0f, 0.5f};
	// 条目标签的选中态：唯一一处红色，表示「这一项正在被拖动/选中」。
	inline constexpr ColorRGBA ITEM_LABEL_SELECTED{1.0f, 0.35f, 0.35f, 0.75f};
	// 底轨与条目共用的圆角：小菜单档，和列表行/胶囊同一族。
	inline constexpr float RADIUS = radius::SMALL_MENU;
} // namespace ui_token::status_bar

// 服务器信息页的字号曾经被删过一次（当时走设置页派生链），结果是标题 32、正文 20
// 又变成硬编码在渲染函数里，实机又大又密。现在恢复为「只放字号与列宽」的命名空间：
// 行距、段落缝仍来自设置页派生链，本命名空间只负责这一页自己的字号标尺。

// 确认弹窗（删除、覆盖、退出确认）的尺寸档：宽度按内容在上下限之间自适应，
// 标题与按钮各自固定高度，保证不同长度的提示文案排版一致。
namespace ui_token::confirm_dialog
{
	// 单行短提示时的最小宽度，低于它按钮会挤在一行挤不开。
	inline constexpr float MIN_WIDTH = 300.0f;
	// 最大宽度：再长的文案换行，不把弹窗拉成一条横贯屏幕的窄条。
	inline constexpr float MAX_WIDTH = 380.0f;
	// 标题区高度。
	inline constexpr float TITLE_HEIGHT = 24.0f;
	// 底部按钮高度（与设置页按钮同档）。
	inline constexpr float BUTTON_HEIGHT = 28.0f;
} // namespace ui_token::confirm_dialog

// 投影偏移标尺：三档高度对应三种「离页面多远」。
// 页面主标题：设置页顶部的「设置」与工作区标题（收藏地图 / 地图游玩历史 / 本地存档）
// 共用一档。字号单独成档，不再借用 ui_token::font 的正文/大标题档，
// 否则标题会随正文标尺一起被改小（曾经确实被改成 16 的「大标题」）。
namespace ui_token::page_title
{
	// 标题字号（未缩放）：比正文大一档以上，与卡片标题区分开。
	inline constexpr float FONT_SIZE = 20.0f;
	// 标题行高：标题条的高度，图标与文字都在这一行里居中。
	inline constexpr float HEIGHT = 38.0f;
	// 前置图标尺寸与图标到文字的间距。
	inline constexpr float ICON_SIZE = 20.0f;
	inline constexpr float ICON_GAP = 6.0f;
	// 图标的视觉下沉量：字形有内部留白，纯按矩形居中时图标会显得偏高，
	// 标题「图标 + 文字」的重心因此不稳。按字号比例给一点点下沉。
	inline constexpr float ICON_Y_OFFSET = 1.5f;
} // namespace ui_token::page_title

// 面板间距：页面级面板之间、面板与内容之间只用这两档。
// 服务器列表是基准（面板间距 8、面板内边距 10），回放/截图浏览器与设置页壳层跟它对齐。
namespace ui_token::panel
{
	// 两个面板之间的缝：列表与工具箱、内容与底部操作条、设置页左右两栏。
	inline constexpr float GAP = 8.0f;
	// 面板内边距：内容到面板四边。
	inline constexpr float PADDING = 10.0f;
} // namespace ui_token::panel

// 服务器信息页（游戏内「服务器信息」页）的排版档：标题、字段正文、标签列宽与行高。
// 原先标题 32、正文 20 全是硬编码并乘卡片缩放，实机上又大又密；收成一组后
// 「服务器信息」「游戏信息」两张卡与下方地图标签、公告共用同一套字号。
namespace ui_token::server_info
{
	// 卡片大标题。
	inline constexpr float TITLE_SIZE = 26.0f;
	// 字段正文（标签与值同一档）。
	inline constexpr float BODY_SIZE = 15.0f;
	// 标题与副标题之间的留白。
	inline constexpr float TITLE_GAP = 4.0f;
	// 副标题（服务器名）与第一条字段之间的留白：只留一档行距，不再额外空出一整行。
	inline constexpr float NAME_GAP = 6.0f;
	// 字段行高 = 正文字号 × 该比例：字号缩小后仍要留出行间呼吸，避免密排。
	inline constexpr float ROW_HEIGHT_RATIO = 1.5f;
	// 标签列宽（地址 / 延迟 / 版本 / 密码 / 社区）。
	inline constexpr float LABEL_WIDTH = 104.0f;
	// 备注行：与上一段的间隔、行高、标签列宽。
	inline constexpr float NOTE_GAP = 8.0f;
	inline constexpr float NOTE_ROW_HEIGHT = 22.0f;
	inline constexpr float NOTE_LABEL_WIDTH = 72.0f;
	// 卡片圆角：与设置卡片同一族。
	inline constexpr float CARD_RADIUS = radius::MENU;
	// 地图标签面板：标题行高、刷新图标边长、标题与列表的缝、底部分区高度、
	// 列表与底部之间的一档缝、建议标签说明文字占宽。
	inline constexpr float TAGS_HEADER_HEIGHT = 26.0f;
	inline constexpr float TAGS_ICON_SIZE = 22.0f;
	inline constexpr float TAGS_HEADER_GAP = 8.0f;
	inline constexpr float TAGS_FOOTER_HEIGHT = 90.0f;
	inline constexpr float TAGS_LIST_GAP = 5.0f;
	inline constexpr float TAGS_PRESET_LABEL_WIDTH = 78.0f;
	inline constexpr float TAGS_PRESET_ROW_HEIGHT = 22.0f;
	// 预置标签按钮宽与提交按钮宽（两者都是「短文字胶囊」，按内容上限给固定宽）。
	inline constexpr float TAGS_PRESET_BUTTON_WIDTH = 60.0f;
	inline constexpr float TAGS_SUBMIT_WIDTH = 70.0f;
} // namespace ui_token::server_info

// 服务器信息页公告（MOTD）的排版档：公告里常用 Unicode 框线字符画方框，
// 按普通文本排版时字距会把横线切成一节一节（实机看起来像竹子有节），
// 框线行因此改走「一格一字形、字间距为 0」的等宽推进。这些档位就是那条路径
// 的全部数值来源，格宽与行高由字体实测（见 QmMotdLayout.h），不在此写死。
namespace ui_token::motd
{
	// 排版设计基准尺寸：公告块按这个宽高比算缩放，尺寸改这里而不是散在渲染函数里。
	inline constexpr float DESIGN_WIDTH = 180.0f;
	inline constexpr float DESIGN_HEIGHT = 53.0f;
	// 段标题行高相对正文字号的倍率，以及标题与正文之间的一档缝。
	inline constexpr float TITLE_HEIGHT_RATIO = 2.0f;
	inline constexpr float TITLE_GAP = 5.0f;
	// 框线行每个排版 token 最多发多少个字形：整行一次发完会在长公告下形成单帧尖刺，
	// 这里给一个上限，剩余字形留到下一帧继续。
	inline constexpr int BOX_GLYPHS_PER_STEP = 12;
	// 公告正文字号（未缩放）：与「服务器信息 / 游戏信息」正文同一档，
	// 公告不再比上面两张卡更大。公告栏绘制时再乘卡片缩放。
	inline constexpr float FONT_SIZE = server_info::BODY_SIZE;
	// 行距相对行高的比例：公告是多行长文，行与行之间要留出比单行标签更松的空。
	inline constexpr float LINE_SPACING_RATIO = 1.28f;
	// 超宽框线行的推进压缩下限：低于该比例笔画会互相吞掉，宁可截断可见字数。
	inline constexpr float BOX_CELL_MIN_RATIO = 0.5f;
} // namespace ui_token::motd

// 只给偏移量，颜色用 color::SURFACE_SHADOW，绘制层按偏移方向铺一层模糊底。
namespace ui_token::elevation
{
	// 低层：贴合页面的卡片、行，几乎只有一道下沿阴影。
	inline constexpr float SHADOW_X_LOW = 1.0f;
	inline constexpr float SHADOW_Y_LOW = 1.0f;
	// 中层：悬浮菜单、下拉列表，阴影稍远一点。
	inline constexpr float SHADOW_X_MED = 1.5f;
	inline constexpr float SHADOW_Y_MED = 2.0f;
	// 高层：模态弹窗、拖拽中的卡片，阴影最远，读作浮在最上面。
	inline constexpr float SHADOW_X_HIGH = 3.0f;
	inline constexpr float SHADOW_Y_HIGH = 6.0f;
} // namespace ui_token::elevation

// 界面字号标尺（未缩放）。层级只有标题、正文、小字三段，其余都是派生档。
// 需要跟视口缩放的场合请用 ui_token::control::BodySizeFromScale 或
// SSettingsContentMetrics，不要在这里乘系数。
namespace ui_token::font
{
	// 大标题：页面主标题、空状态引导语。
	inline constexpr float HEADLINE_LG = 16.0f;
	// 标题：分区标题、卡片标题（设置卡片统一用这一档）。
	inline constexpr float HEADLINE = 14.0f;
	// 大号标题：卡片主标题、弹窗标题的放大档。
	inline constexpr float TITLE = 18.0f;
	// 正文：设置项标签、说明文字、输入内容。
	inline constexpr float BODY = 12.0f;
	// 小字：副标题、辅助信息、气泡正文。
	inline constexpr float SMALL = 10.0f;
	// 注脚：极短的状态提示，与 SMALL 同数值但语义独立，便于以后单独调整。
	inline constexpr float CAPTION = 10.0f;
	// 最小提示字：角标、单位、极次要信息。
	inline constexpr float TIP = 9.0f;
} // namespace ui_token::font

// 玩家页（ESC 菜单「玩家」== PAGE_PLAYERS）的左右两栏与右侧玩家信息卡。
// 卡内分区固定为「头部（皮肤/名字/国家）→ 操作胶囊 → 图标复制 → 状态 → 玩家信息细节」，
// 分区高度、分区之间的缝、图标按钮边长与列表行度量都在这里；调用点只选档位。
// 这是一组「同一张卡片内部的分区尺寸」，不新增玩家可配置项。
namespace ui_token::player_card
{
	// 左右两栏的最小宽度：窄于它就把信息卡堆到列表下方，避免两栏都挤成细条。
	inline constexpr float SIDE_BY_SIDE_MIN_WIDTH = 640.0f;
	// 右栏（信息卡）的宽度上限与占宽比例：卡片再宽也只是「一排操作」，不横向铺满整页。
	inline constexpr float CARD_MAX_WIDTH = 340.0f;
	inline constexpr float CARD_WIDTH_RATIO = 0.46f;
	// 堆叠布局时信息卡占内容区的高度比例。
	inline constexpr float STACK_CARD_HEIGHT_RATIO = 0.52f;
	// 列表与信息卡之间的缝：直接用页面级面板间距，两栏与其它页面读起来是同一条缝。
	inline constexpr float LIST_CARD_GAP = panel::GAP;
	// 玩家列表是网格卡片（与皮肤列表同一种读法）：每格最小宽度、格高、格间缝与格内头像边长。
	// 玩家瓦片按皮肤列表那种小卡：头像 + 两行文字 + 右侧/右上动作图标，
	// 因此比纯文字格宽一档，高度容纳两行文字。
	inline constexpr float TILE_MIN_WIDTH = 296.0f;
	inline constexpr float TILE_HEIGHT = 64.0f;
	// 瓦片内动作图标边长：比整行卡的小一档，五枚才排得下。
	inline constexpr float TILE_ICON_SIZE = 18.0f;
	inline constexpr float TILE_GAP = spacing::SM;
	inline constexpr float TILE_AVATAR_SIZE = 36.0f;
	// 顶部一行：搜索框与在线人数占的固定宽度。
	inline constexpr float SEARCH_WIDTH = 260.0f;
	inline constexpr float COUNT_WIDTH = 96.0f;

	// 信息卡四周内边距与圆角。
	inline constexpr float PADDING = spacing::SM;
	inline constexpr float RADIUS = radius::BASE;
	// 卡内两个分区之间的缝（头部、操作区、图标行、状态区、细节区之间）。
	inline constexpr float SECTION_GAP = spacing::SM;
	// 头部：头像方形、名字/战队两行的行高、右侧国家旗高度（宽度按 2:1 反推）。
	inline constexpr float HEADER_HEIGHT = 44.0f;
	inline constexpr float HEADER_AVATAR_SIZE = 36.0f;
	inline constexpr float HEADER_NAME_HEIGHT = 18.0f;
	inline constexpr float HEADER_FLAG_HEIGHT = 13.0f;
	inline constexpr float HEADER_FLAG_ASPECT = 2.0f;
	// 操作区：胶囊按钮两列网格，行高与行内缝隙。
	inline constexpr int ACTION_COLUMNS = 2;
	inline constexpr int ACTION_COUNT = 5;
	inline constexpr float ACTION_HEIGHT = 22.0f;
	inline constexpr float ACTION_GAP = spacing::XS;
	// 图标复制区：正方形图标按钮的边长、缝隙与个数。
	inline constexpr int ICON_COUNT = 3;
	inline constexpr float ICON_BUTTON_SIZE = 22.0f;
	inline constexpr float ICON_BUTTON_GAP = spacing::XS;
	// 卡片右上角动作图标行的行高：与图标边长同一档（好友列表卡片的动作图标也是这个尺寸）。
	inline constexpr float SHOWCASE_ICON_SIZE = ICON_BUTTON_SIZE;
	// 状态区（在线 / 旁观 / 加入）：行高与状态之间留出的空。
	inline constexpr float STATUS_HEIGHT = 16.0f;
	inline constexpr float STATUS_GAP = spacing::MD;
	// 细节区（玩家信息）：标签列宽、行高与字号。
	inline constexpr float DETAIL_LABEL_WIDTH = 56.0f;
	inline constexpr float DETAIL_ROW_HEIGHT = 16.0f;
	inline constexpr float DETAIL_FONT_SIZE = font::SMALL;
	// 次级文字相对表面前景的透明度：与 color::TEXT_SECONDARY 同档，
	// 但用于「由表面推导出的前景色」，所以只能乘在 Ink 上，不能直接换成固定白色。
	inline constexpr float TEXT_MUTED_ALPHA = 0.72f;
	// 国家旗相对表面前景的透明度：略淡一档，不抢名字的注意力。
	inline constexpr float FLAG_ALPHA = 0.8f;
	// 列表行：行高、头像方形边长、行尾好友标记占宽、名字行高。
	inline constexpr float LIST_ROW_HEIGHT = 36.0f;
	inline constexpr float LIST_AVATAR_SIZE = 32.0f;
	inline constexpr float LIST_BADGE_WIDTH = 22.0f;
	inline constexpr float LIST_NAME_HEIGHT = 19.0f;
	// 页面搜索框高度：与筛选胶囊同高，两条并排读作一行。
	inline constexpr float SEARCH_HEIGHT = 24.0f;
	// 复制成功的轻提示停留时长（秒）：足够读完「已复制」，又不挡下一步操作。
	inline constexpr float COPY_TOAST_SECONDS = 1.4f;
} // namespace ui_token::player_card

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
	// 气泡与锚点/屏幕边缘的外边距：一个气泡离目标多远。
	inline constexpr float MARGIN = 4.0f;
	// 气泡圆角：小圆角，读作贴在目标旁边而不是浮窗。
	inline constexpr float RADIUS = 3.0f;
	// 默认阅读宽度上限；设置页与普通页面共用同一档，窄视口按比例收窄。
	inline constexpr float MAX_WIDTH = 300.0f;
} // namespace ui_token::tooltip

// HUD 基础字号按 300 逻辑高度定义：辅助信息、正文、重点数值、关键提示。
namespace ui_token::hud::font
{
	// 辅助信息：坐标、FPS 一类角标，最小一档。
	inline constexpr float CAPTION = 6.0f;
	// 正文：HUD 面板里成行的说明与数值标签。
	inline constexpr float BODY = 8.0f;
	// 标题：HUD 面板标题、区域名。
	inline constexpr float TITLE = 10.0f;
	// 重点数值：需要一眼读到的数字（速度、时间、计数）。
	inline constexpr float EMPHASIS = 12.0f;
} // namespace ui_token::hud::font

// 输入法（IME）候选面板：跟随系统输入法浮在光标下方的小面板。
// 全部尺寸都是「基准值 × SCALE」，改整体大小只动 SCALE，不要逐项改数字。
// 面板有明暗两套配色：跟随界面（浅色界面用 LIGHT 族），无 _LIGHT/_DARK 后缀的别名
// 是当前的默认档，接入主题时改成另一族即可，调用点不用分支。
namespace ui_token::ime
{
	// 全局缩放：面板所有尺寸都乘它，改这一处等于整体放大/缩小。
	inline constexpr float SCALE = 0.68f;
	// 面板底（浅色界面）：接近不透明的白。
	inline constexpr ColorRGBA PANEL_BG_LIGHT{1.0f, 1.0f, 1.0f, 0.96f};
	// 面板底（深色界面）：接近不透明的深灰。
	inline constexpr ColorRGBA PANEL_BG_DARK{0.110f, 0.110f, 0.118f, 0.96f};
	// 面板描边（浅色）：极淡的黑边，只勾出边界。
	inline constexpr ColorRGBA PANEL_BORDER_LIGHT{0.0f, 0.0f, 0.0f, 0.08f};
	// 面板描边（深色）：淡白边。
	inline constexpr ColorRGBA PANEL_BORDER_DARK{1.0f, 1.0f, 1.0f, 0.10f};
	// 面板投影（浅色）：浅色面板需要阴影才离得开页面。
	inline constexpr ColorRGBA PANEL_SHADOW_LIGHT{0.0f, 0.0f, 0.0f, 0.16f};
	// 面板投影（深色）：深色面板本身够沉，不再加阴影。
	inline constexpr ColorRGBA PANEL_SHADOW_DARK{0.0f, 0.0f, 0.0f, 0.0f};
	// 编码中（未上屏）文字的背景（浅色）。
	inline constexpr ColorRGBA COMPOSITION_BG_LIGHT{1.0f, 1.0f, 1.0f, 0.18f};
	// 编码中文字的背景（深色）。
	inline constexpr ColorRGBA COMPOSITION_BG_DARK{1.0f, 1.0f, 1.0f, 0.03f};
	// 编码中文字里被选区的底色：系统蓝，两套主题共用。
	inline constexpr ColorRGBA COMPOSITION_SELECTION{0.0f, 0.478f, 1.0f, 0.18f};
	// 编码中文字的下划线（浅色）。
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE_LIGHT{0.05f, 0.06f, 0.08f, 0.18f};
	// 编码中文字的下划线（深色）。
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE_DARK{1.0f, 1.0f, 1.0f, 0.16f};
	// 候选词选中项的背景（浅色）：系统蓝压淡。
	inline constexpr ColorRGBA SELECTED_BG_LIGHT{0.0f, 0.478f, 1.0f, 0.22f};
	// 候选词选中项的背景（深色）：稍亮一档，避免在深底上糊掉。
	inline constexpr ColorRGBA SELECTED_BG_DARK{0.26f, 0.55f, 1.0f, 0.28f};
	// 正文文字（浅色界面）：近黑。
	inline constexpr ColorRGBA TEXT_LIGHT{0.055f, 0.065f, 0.08f, 0.98f};
	// 正文文字（深色界面）：近白。
	inline constexpr ColorRGBA TEXT_DARK{1.0f, 1.0f, 1.0f, 0.96f};
	// 弱化文字（浅色）：候选序号、页码一类。
	inline constexpr ColorRGBA TEXT_MUTED_LIGHT{0.08f, 0.09f, 0.11f, 0.52f};
	// 弱化文字（深色）。
	inline constexpr ColorRGBA TEXT_MUTED_DARK{0.557f, 0.557f, 0.580f, 0.56f};
	// 选中候选词的文字（浅色）。
	inline constexpr ColorRGBA TEXT_SELECTED_LIGHT{0.0f, 0.32f, 0.74f, 1.0f};
	// 选中候选词的文字（深色）。
	inline constexpr ColorRGBA TEXT_SELECTED_DARK{0.184f, 0.502f, 0.929f, 1.0f};
	// 当前默认档（浅色）：换成另一族时只改下面九个别名，调用点无需分支。
	inline constexpr ColorRGBA PANEL_BG = PANEL_BG_LIGHT;
	// 以下十二个别名成组切换明暗族，逐条含义见上面同名的 _LIGHT/_DARK 项。
	inline constexpr ColorRGBA PANEL_BORDER = PANEL_BORDER_LIGHT;
	inline constexpr ColorRGBA PANEL_SHADOW = PANEL_SHADOW_LIGHT;
	inline constexpr ColorRGBA COMPOSITION_BG = COMPOSITION_BG_LIGHT;
	inline constexpr ColorRGBA COMPOSITION_UNDERLINE = COMPOSITION_UNDERLINE_LIGHT;
	inline constexpr ColorRGBA SELECTED_BG = SELECTED_BG_LIGHT;
	inline constexpr ColorRGBA TEXT = TEXT_LIGHT;
	inline constexpr ColorRGBA TEXT_MUTED = TEXT_MUTED_LIGHT;
	inline constexpr ColorRGBA TEXT_SELECTED = TEXT_SELECTED_LIGHT;
	// 编码中（拼音串）的字号，比候选词小一档。
	inline constexpr float FONT_COMPOSITION = 7.2f * SCALE;
	// 候选词字号：面板里最大的一档字。
	inline constexpr float FONT_CANDIDATE = 8.1f * SCALE;
	// 面板左右内边距。
	inline constexpr float PADDING_X = 4.6f * SCALE;
	// 面板上下内边距。
	inline constexpr float PADDING_Y = 2.6f * SCALE;
	// 编码行与候选行之间的间距。
	inline constexpr float ROW_GAP = 1.4f * SCALE;
	// 候选行高度：一行候选词的容器高度。
	inline constexpr float ROW_HEIGHT = 14.0f * SCALE;
	// 编码行高度：比候选行矮一档。
	inline constexpr float COMPOSITION_ROW_HEIGHT = 11.8f * SCALE;
	// 候选序号占用的宽度。
	inline constexpr float NUM_WIDTH = 5.5f * SCALE;
	// 面板最小宽度：只有一两个单字候选时不缩成一小块。
	inline constexpr float MIN_WIDTH = 72.0f * SCALE;
	// 面板最大宽度：候选再多也不横向铺满屏幕，超出走换行。
	inline constexpr float MAX_WIDTH = 360.0f * SCALE;
	// 面板圆角。
	inline constexpr float RADIUS = 8.2f * SCALE;
	// 基准屏高：面板纵向定位按它算比例，不随实际窗口高变化。
	inline constexpr float SCREEN_HEIGHT = 300.0f;
	// 面板与光标/屏幕边缘的安全外边距。
	inline constexpr float SCREEN_MARGIN = 4.0f;
	// 投影横向偏移：IME 阴影只往下投，横向不偏。
	inline constexpr float SHADOW_X = 0.0f;
	// 投影纵向偏移：往下露一道，读作浮在文字之上。
	inline constexpr float SHADOW_Y = 2.0f * SCALE;
	// 描边内缩：描边画在面板内侧，不额外撑大面积。
	inline constexpr float BORDER_INSET = 0.35f * SCALE;
	// 编码行文字的左右内边距。
	inline constexpr float COMPOSITION_TEXT_PADDING_X = 4.6f * SCALE;
	// 候选序号与候选词之间的间距。
	inline constexpr float CANDIDATE_NUM_PADDING_X = 3.6f * SCALE;
	// 候选词之间的间距。
	inline constexpr float CANDIDATE_GAP = 7.0f * SCALE;
	// 单个候选词的左右内边距。
	inline constexpr float CANDIDATE_PADDING_X = 5.6f * SCALE;
	// 选中候选词比未选中再宽出的一档内边距：选中块看起来更「饱满」。
	inline constexpr float SELECTED_PADDING_X = 6.2f * SCALE;
	// 文字安全区的左右内缩：给描边与圆角让位，文字不会压到边上。
	inline constexpr float TEXT_SAFE_PADDING_X = 1.8f * SCALE;
	// 文字安全区的上下内缩。
	inline constexpr float TEXT_SAFE_PADDING_Y = 2.4f * SCALE;
	// 候选行尾部（翻页箭头、剩余标记）占用的宽度。
	inline constexpr float TRAILING_WIDTH = 20.0f * SCALE;
	// 单条候选词允许的最大文字宽度：超长候选词截断，避免一条撑满整行。
	inline constexpr float MAX_CANDIDATE_TEXT_WIDTH = 1000.0f;
} // namespace ui_token::ime

// 输入光标（caret）组件内部角色：竖线/下划线的线宽、方块宽度兜底比例与圆角。
// 这里是同一组件内部的形态档位，不是给玩家逐项自定义的配色：颜色取当前文字色与描边色。
// 线宽按物理像素给出，调用点只提供「一个物理像素对应多少世界单位」，换分辨率不改变粗细。
namespace ui_token::text_caret
{
	// 竖线与下划线的线宽（物理像素）：两像素，与既有光标观感一致。
	inline constexpr float THICKNESS_PX = 2.0f;
	// 竖线与下划线的圆角（物理像素）：半个线宽，端头读作胶囊。
	inline constexpr float RADIUS_PX = 1.0f;
	// 方块圆角（物理像素）：方形光标也保留一点圆角，压在深色底上不显生硬。
	inline constexpr float BLOCK_RADIUS_PX = 1.5f;
	// 方块与下划线在没有字符格宽时的兜底宽度：按字号的比例给出，三种样式都有确定宽度。
	inline constexpr float WIDTH_RATIO = 0.6f;
	// 光标外描边宽度（物理像素）：让光标压在任意底色和字形上都读得清。
	inline constexpr float OUTLINE_PX = 1.0f;
	// 描边相对笔画的最小比例：笔画细时光标仍是「一圈边」，不会只剩一条实心线。
	inline constexpr float OUTLINE_RATIO = 0.35f;
} // namespace ui_token::text_caret

// 动效令牌：界面所有过渡的时长、曲线与弹簧参数。按「节奏分级」组织——
// 同一档交互用同一套时长，跨页面拖动感受一致；不要为单个控件新造一条时长。
// 每个 SUiAnimTransition 同时带 duration 与 easing，需要弹簧时带 m_Spring（ζ 决定回弹）。
namespace ui_token::motion
{
	// 菜单反馈使用独立的语义时长，避免通用预设改变交互节奏。
	// 悬停淡入/淡出：最短的一档，鼠标扫过时不做拖尾。
	inline constexpr SUiAnimTransition HOVER_FADE{.m_DurationSec = 0.12f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 按下缩放：比悬停更短，按下要「立刻」有反馈。
	inline constexpr SUiAnimTransition PRESS_SCALE{.m_DurationSec = 0.08f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 模态弹窗入场：淡入 + 缩放同时进行，弹簧带一点回弹（ζ 由 m_Spring 决定）。
	inline constexpr SUiAnimTransition MODAL_FADE_SCALE{
		.m_DurationSec = 0.20f,
		.m_Easing = EEasing::EASE_OUT_QUART,
		.m_Spring = {1.0f, 400.0f, 40.0f, 0.001f, 0.01f},
	};
	// 页面切换横向滑入：整页位移，比局部反馈长一档才看得出方向。
	inline constexpr SUiAnimTransition PAGE_SLIDE{.m_DurationSec = 0.22f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 页签内容切换：只淡入内容区，不位移整页。
	inline constexpr SUiAnimTransition TAB_SWITCH{.m_DurationSec = 0.16f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 导航滑块出现／消失：比普通悬停反馈长一档，配合由小到大的缩放才读得出「长出来」，
	// 0.12 秒的纯淡入在整页切换的同一帧里几乎看不见。
	inline constexpr SUiAnimTransition NAVIGATION_APPEAR{.m_DurationSec = 0.26f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 输入框焦点环：与悬停同一档，焦点进出不做更慢的动画。
	inline constexpr const SUiAnimTransition &INPUT_FOCUS_RING = HOVER_FADE;
	// 提示条（toast）滑入：位移 + 淡入，弹簧沿用模态弹窗那一套。
	inline constexpr SUiAnimTransition TOAST_SLIDE{
		.m_DurationSec = 0.22f,
		.m_Easing = EEasing::EASE_OUT_QUART,
		.m_Spring = MODAL_FADE_SCALE.m_Spring,
	};
	// 气泡（tooltip）淡入淡出：与悬停同档，鼠标停住即出现。
	inline constexpr SUiAnimTransition TOOLTIP_FADE{.m_DurationSec = 0.12f, .m_Easing = EEasing::EASE_OUT_QUART};
	// 导航滑块专用：切换 Tab / 分段时带速度续接地滑过去（ζ≈0.93）。
	inline constexpr SUiSpringConfig NAVIGATION_SPRING{1.0f, 500.0f, 42.0f, 0.05f, 0.4f};
	// 开关（switch）拨动：旋钮滑到另一端，阻尼偏高、几乎不回弹。
	inline constexpr SUiSpringConfig TOGGLE_SPRING{1.0f, 620.0f, 44.0f, 0.01f, 0.05f};
	// 卡片重排：拖拽落位后卡片互相让位，刚度高一点让整列同时归位。
	inline constexpr SUiSpringConfig CARD_REORDER{1.0f, 900.0f, 48.0f, 0.01f, 0.05f};
	// 离散滑条专用：档位切换时旋钮与填充带阻尼滑过去（ζ≈0.70），悬停/拖动只放大旋钮不改色。
	inline constexpr SUiSpringConfig SLIDER_SPRING{1.0f, 340.0f, 26.0f, 0.01f, 0.05f};
	// 滑条旋钮自身的跟手弹簧：刚度比 SLIDER_SPRING 高，拖动时不掉队。
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
	// 折叠区展开时长。
	inline constexpr float INTERACTIVE_REVEAL_SEC = 0.14f;
	// 同上，动效等级为「减弱」时用的一档。
	inline constexpr float INTERACTIVE_REVEAL_SEC_REDUCED = 0.09f;

	// 列表行淡入：smoothstep，前后都慢，适合「整列一起出现」。
	inline constexpr SUiAnimTransition LIST_ENTRY_FADE{.m_DurationSec = 0.20f, .m_Easing = EEasing::EASE_IN_OUT};
	// 列表行淡入（减弱动效档）。
	inline constexpr SUiAnimTransition LIST_ENTRY_FADE_REDUCED{.m_DurationSec = 0.10f, .m_Easing = EEasing::EASE_IN_OUT};
	// 列表行上浮时长：与淡入等长，两个量一起做才是「升起」而不是「跳出来」。
	inline constexpr float LIST_ENTRY_RISE_SEC = 0.20f;
	// 上浮时长（减弱动效档）。
	inline constexpr float LIST_ENTRY_RISE_SEC_REDUCED = 0.10f;
	// 上浮距离：行从下方多少像素归位；越大越像「飞入」，越容易读成慢。
	inline constexpr float LIST_ENTRY_RISE_DISTANCE = 8.0f;
	// 上浮距离（减弱动效档）。
	inline constexpr float LIST_ENTRY_RISE_DISTANCE_REDUCED = 4.0f;
	// 相邻行的启动错开步长：让整列读起来是「依次」而不是「一起」。
	inline constexpr float LIST_ENTRY_STAGGER_STEP_SEC = 0.014f;
	// 错开生效的最大行数：超过之后一起启动，避免长列表末尾等太久。
	inline constexpr int LIST_ENTRY_STAGGER_MAX_ROWS = 28;

	// 输入光标闪烁：所有输入框共用同一条周期与占空比。相位只由绝对时间推导，
	// 组件不保存跨帧起点，所以并排的两个输入框不会各闪各的。
	inline constexpr float CARET_BLINK_PERIOD_SEC = 1.0f;
	// 一个周期内可见的比例：0.5 表示亮半周期、灭半周期。
	inline constexpr float CARET_BLINK_DUTY = 0.5f;

	// 语义别名：调用点按「哪个控件」取，不必记具体是哪条时长。
	// 按钮悬停：与 HOVER_FADE 同一条曲线，按控件命名方便调用点取用。
	inline constexpr const SUiAnimTransition &BTN_HOVER = HOVER_FADE;
	// 按钮按下：与 PRESS_SCALE 同一条曲线。
	inline constexpr const SUiAnimTransition &BTN_PRESS = PRESS_SCALE;
	// 模态弹窗入场：与 MODAL_FADE_SCALE 同一条曲线。
	inline constexpr const SUiAnimTransition &MODAL_IN = MODAL_FADE_SCALE;
	// 开关拨动：与 TOGGLE_SPRING 同一条弹簧。
	inline constexpr const SUiSpringConfig &TOGGLE = TOGGLE_SPRING;
} // namespace ui_token::motion

// 覆盖式滚动条的出现/消失复用菜单悬停反馈的时长与曲线：同一档节奏，不再新开一套。
// 放在 motion 之后是因为 token 引用声明顺序，语义仍属于列表。
namespace ui_token::list
{
	// 滚动条淡入/淡出的过渡：直接引用悬停反馈，滚动条与按钮同一档节奏。
	inline constexpr const SUiAnimTransition &SCROLLBAR_FADE = motion::HOVER_FADE;
} // namespace ui_token::list

#endif
