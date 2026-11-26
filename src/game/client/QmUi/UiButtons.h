// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIBUTTONS_H
#define GAME_CLIENT_QMUI_UIBUTTONS_H

#include "SettingsPageLayout.h"
#include "UiButtonStyle.h"
#include "UiContext.h"

#include <engine/graphics.h>

#include <optional>

enum class EQmIcon;
class CButtonContainer;
class CUIRect;

namespace ui_widget
{
	struct SSettingsActionGroupState
	{
		CSettingsChoiceWidthCache m_Widths;
	};

	// 保留各按钮原来的业务和文字缓存入口，只统一真实译文测量及槽位。
	SSettingsActionGroupLayout PrepareSettingsActionGroup(CUi &Ui, SSettingsActionGroupState &State, const CUIRect &Row, const char *const *ppLabels, int Count, const SSettingsContentMetrics &Metrics, ESettingsActionGroupWidth WidthPolicy = ESettingsActionGroupWidth::FILL);

	struct SButtonSurfaceOptions
	{
		EUiButtonRole m_Role = EUiButtonRole::SECONDARY;
		bool m_Enabled = true;
		bool m_Selected = false;
		bool m_TransparentInactive = false;
		// 卡片形态的列表项：LIST_ENTRY 默认不描边，这里显式打开悬浮白边。
		// 只对 LIST_ENTRY 生效，其余角色本来就带统一描边。
		bool m_ListEntryBorder = false;
		bool m_AlignToPixel = true;
		// 分段按钮由调用方统一算出整条视觉矩形时，这里关闭逐半部分的缩放动画，
		// 避免填充缩进而共用外框停在原始尺寸。
		bool m_SuppressGeometryAnimation = false;
		int m_Corners = IGraphics::CORNER_ALL;
		float m_Radius = ui_token::radius::BASE;
		float m_BorderWidth = ui_token::feedback::ICON_BORDER_WIDTH;
		std::optional<ColorRGBA> m_Color;
		// 悬停/按下/选中的描边由 DrawButtonSurface 统一解析成同一条白色描边，
		// 不随表面前景色变化；需要额外语义时才用这里显式覆盖（优先于统一描边）。
		std::optional<ColorRGBA> m_BorderOverride;
		// 由调用方给出悬浮判定：需要按「成对色块」等自有几何判定的场景才覆盖，
		// 颜色仍走公共反馈 token。
		std::optional<bool> m_HoveredOverride;
		const CUIRect *m_pHitRect = nullptr;
	};

	// 旧菜单入口和新控件共用绘制、主题及动画；上下文由所属 CUi 提供。
	IUiContext ControlContext(CUi *pUi);
	ColorRGBA DrawButtonSurface(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, const SButtonSurfaceOptions &Options = {});

	// 图标按钮的自定义图标绘制：底色、hover 缩放、命中判定仍归 DoIconButton，图标本身
	// 交给回调画（形变帧条、双图标过渡等）。Rect 是按钮内部收敛后的图标方框，Color 是
	// 按钮已算好的前景色——与静止态 DrawQmIcon 拿到的两个入参完全一致。
	// 用函数指针而不是 std::function：这个回调每帧每按钮都要走一次，不能带分配。
	struct SIconButtonIconDraw
	{
		void (*m_pfnDraw)(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Color, const void *pUser) = nullptr;
		const void *m_pUser = nullptr;
	};

	// 卡头已在预布局处理点击，绘制时只复用图标按钮外观。
	void DrawIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, SButtonSurfaceOptions Options, const SIconButtonIconDraw *pIconDraw = nullptr, float IconSize = 0.0f, bool ShowSlash = false);

	// Radius < 0 使用默认控件圆角；传入短边一半得到圆形按钮。
	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners = IGraphics::CORNER_ALL, bool Enabled = true, std::optional<ColorRGBA> Color = std::nullopt, bool ShowSlash = false, bool TransparentInactive = false, const CUIRect *pHitRect = nullptr, const SIconButtonIconDraw *pIconDraw = nullptr, float Radius = -1.0f, std::optional<bool> HoveredOverride = std::nullopt);

	// 主按钮使用主题强调色；禁用时保留外形并停止交互。
	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL);

	// 次级按钮沿用控件背景配置，悬浮/按下时增加可辨识的背景和边框反馈。
	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, bool Selected = false);

	// 双操作共用外轮廓和分隔线，返回 0/1 表示被触发的半边，-1 表示无操作。
	// 分段按钮的可选渲染参数。默认值等价于「一直两段、中分、无插画」，
	// 既有调用点不用改；主菜单「运行服务器」用 Progress 做单段→两段的生长过渡。
	struct SSplitActionOptions
	{
		// 0 = 右段还没长出来（整条仍是左段，端头两侧都圆），1 = 正常分割；
		// 弹簧过冲允许略大于 1，右段会先冲过去再回稳。
		float m_Progress = 1.0f;
		// 左段目标宽度占整条的比例。贴图会额外吃掉右段宽度，由调用点算好再传。
		float m_LeftFraction = 0.5f;
		// 贴在整条右端的插画（两条纹理：静止 / 悬浮），m_ImageWidth 为 0 时按 4 倍按钮高。
		IGraphics::CTextureHandle m_ImageIdle;
		IGraphics::CTextureHandle m_ImageHover;
		float m_ImageWidth = 0.0f;
		// 两段各自的底色语义；不给就都走控制表面默认色。
		std::optional<ColorRGBA> m_LeftSurface;
		std::optional<ColorRGBA> m_RightSurface;
		// 文字字号；0 = 按控件正文尺寸（BodySizeFromScale）。主菜单行要跟带图菜单按钮
		// 同字号时由调用点算好传进来。
		float m_LabelSize = 0.0f;
	};

	int SplitActionButton(const IUiContext &Ctx, CButtonContainer *pLeft, const char *pLeftText, CButtonContainer *pRight, const char *pRightText, const CUIRect &Rect, bool LeftDisabled = false, bool RightDisabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, const char *pLeftTooltip = nullptr, const char *pRightTooltip = nullptr, const SSplitActionOptions &Options = {});

	// 图标按钮与旧 CUi 入口共用字形、状态和点击处理。
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled = false);
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled = false);

} // namespace ui_widget

#endif
