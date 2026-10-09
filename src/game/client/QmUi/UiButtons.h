// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIBUTTONS_H
#define GAME_CLIENT_QMUI_UIBUTTONS_H

#include "UiButtonStyle.h"
#include "UiContext.h"

#include <engine/graphics.h>

#include <optional>

enum class EQmIcon;
class CButtonContainer;
class CUIRect;

namespace ui_widget
{
	struct SButtonSurfaceOptions
	{
		EUiButtonRole m_Role = EUiButtonRole::SECONDARY;
		bool m_Enabled = true;
		bool m_Selected = false;
		bool m_TransparentInactive = false;
		int m_Corners = IGraphics::CORNER_ALL;
		float m_Radius = ui_token::radius::BASE;
		std::optional<ColorRGBA> m_Color;
		const CUIRect *m_pHitRect = nullptr;
	};

	// 旧菜单入口和新控件共用绘制、主题及动画；上下文由所属 CUi 提供。
	IUiContext ControlContext(CUi *pUi);
	ColorRGBA DrawButtonSurface(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, const SButtonSurfaceOptions &Options = {});
	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners = IGraphics::CORNER_ALL, bool Enabled = true, std::optional<ColorRGBA> Color = std::nullopt, bool ShowSlash = false, bool TransparentInactive = false);

	// 主按钮使用主题强调色；禁用时保留外形并停止交互。
	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false);

	// 次级按钮沿用控件背景配置，悬浮/按下时增加可辨识的背景和边框反馈。
	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled = false);

	// 图标按钮与旧 CUi 入口共用字形、状态和点击处理。
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled = false);
	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled = false);

} // namespace ui_widget

#endif
