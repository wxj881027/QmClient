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
		bool m_AlignToPixel = true;
		int m_Corners = IGraphics::CORNER_ALL;
		float m_Radius = ui_token::radius::BASE;
		float m_BorderWidth = ui_token::feedback::ICON_BORDER_WIDTH;
		std::optional<ColorRGBA> m_Color;
		// 选中态可显式指定描边，避免浅色选中底把默认反馈描边压成深色。
		std::optional<ColorRGBA> m_BorderOverride;
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

	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners = IGraphics::CORNER_ALL, bool Enabled = true, std::optional<ColorRGBA> Color = std::nullopt, bool ShowSlash = false, bool TransparentInactive = false, const CUIRect *pHitRect = nullptr, const SIconButtonIconDraw *pIconDraw = nullptr);

	// 主按钮使用主题强调色；禁用时保留外形并停止交互。
	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL);

	// 次级按钮沿用控件背景配置，悬浮/按下时增加可辨识的背景和边框反馈。
	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, bool Selected = false);

	// 双操作共用外轮廓和分隔线，返回 0/1 表示被触发的半边，-1 表示无操作。
	int SplitActionButton(const IUiContext &Ctx, CButtonContainer *pLeft, const char *pLeftText, CButtonContainer *pRight, const char *pRightText, const CUIRect &Rect, bool LeftDisabled = false, bool RightDisabled = false, EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, const char *pLeftTooltip = nullptr, const char *pRightTooltip = nullptr);

	// 图标按钮与旧 CUi 入口共用字形、状态和点击处理。
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled = false);
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled = false);

} // namespace ui_widget

#endif
