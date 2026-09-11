// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UINAVIGATION_H
#define GAME_CLIENT_QMUI_UINAVIGATION_H

#include "UiContext.h"
#include "UiTokens.h"

#include <game/client/ui_rect.h>

#include <algorithm>

namespace ui_widget
{

	// Horizontal tab bar. Splits Rect into Count equal columns, renders each with
	// CMenus::DoButton_MenuTab and animates a 2px accent underline to the active
	// tab via the v2 animation runtime. Updates *pActive on click; returns the
	// new value (same as *pActive after this call).
	int TabBar(const IUiContext &Ctx, const char *const *ppLabels, int Count, int *pActive, const CUIRect &Rect);

	// 胶囊 Tabbar：整排 Tab 共用一个胶囊容器，激活位置由一枚弹簧驱动的滑块胶囊标记。
	// 绘制顺序是硬约束 —— 先 CapsuleTabBarChrome，再画各 Tab 的文字/图标，滑块必须压在
	// 文字之下，否则滑动途中会盖住经过的 Tab 文字。
	struct SCapsuleTabBarStyle
	{
		ColorRGBA m_CapsuleColor = ui_token::color::SURFACE_HIGHLIGHT; // 整排容器底色
		ColorRGBA m_IndicatorColor = ui_token::color::TEXT_PRIMARY; // 滑块胶囊底色
		ColorRGBA m_ActiveLabelColor = ui_token::color::TEXT_ON_ACCENT; // 滑块上的激活文字
		ColorRGBA m_InactiveLabelColor = ui_token::color::TEXT_PRIMARY; // 容器上的普通文字
		float m_CapsulePadding = 0.0f; // 容器相对 Tab 行的外扩（0 = 与 Tab 行同尺寸）
		float m_IndicatorInset = 2.0f; // 滑块相对 Tab 槽的内缩
	};

	// 槽位并集：胶囊容器覆盖整排 Tab（含 Tab 之间的间隙）。
	inline CUIRect CapsuleTabBarRowRect(const CUIRect *pSlots, int Count)
	{
		CUIRect Row = {0.0f, 0.0f, 0.0f, 0.0f};
		if(pSlots == nullptr || Count <= 0)
			return Row;
		float Left = pSlots[0].x;
		float Top = pSlots[0].y;
		float Right = pSlots[0].x + pSlots[0].w;
		float Bottom = pSlots[0].y + pSlots[0].h;
		for(int i = 1; i < Count; ++i)
		{
			Left = std::min(Left, pSlots[i].x);
			Top = std::min(Top, pSlots[i].y);
			Right = std::max(Right, pSlots[i].x + pSlots[i].w);
			Bottom = std::max(Bottom, pSlots[i].y + pSlots[i].h);
		}
		Row = {Left, Top, Right - Left, Bottom - Top};
		return Row;
	}

	// 画容器胶囊与滑块胶囊。GroupId 标识同一排 Tab（不同排必须不同），滑块位置由
	// v2 动画运行时的弹簧轨道按帧求解，切换 Tab 时带速度续接地滑过去。
	// RowRect 为 CapsuleTabBarRowRect 的结果，pActiveSlot 为当前激活 Tab 槽
	// （nullptr 表示本帧没有激活项，此时只画容器）。
	void CapsuleTabBarChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect &RowRect, const CUIRect *pActiveSlot, const SCapsuleTabBarStyle &Style);

	// 直接给槽位表的重载：容器取槽位并集，激活项取 ActiveIndex（越界则只画容器）。
	inline void CapsuleTabBarChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect *pSlots, int Count, int ActiveIndex, const SCapsuleTabBarStyle &Style)
	{
		CapsuleTabBarChrome(Ctx, GroupId, CapsuleTabBarRowRect(pSlots, Count), pSlots != nullptr && ActiveIndex >= 0 && ActiveIndex < Count ? &pSlots[ActiveIndex] : nullptr, Style);
	}

	// 胶囊配色推导：滑块与文字色由容器表面色明暗自适应 —— 容器偏暗用亮滑块 + 深字，
	// 容器偏亮反过来，避免"白底白滑块"或"深底深字"。各 Tabbar 传入自己的容器表面色。
	inline bool CapsuleTabBarSurfaceIsLight(const ColorRGBA &SurfaceColor)
	{
		return SurfaceColor.r * 0.299f + SurfaceColor.g * 0.587f + SurfaceColor.b * 0.114f >= 0.5f;
	}

	inline ColorRGBA CapsuleTabBarIndicatorColor(const ColorRGBA &SurfaceColor)
	{
		return CapsuleTabBarSurfaceIsLight(SurfaceColor) ? ColorRGBA(0.09f, 0.10f, 0.12f, 0.92f) : ColorRGBA(0.97f, 0.98f, 1.0f, 0.94f);
	}

	inline ColorRGBA CapsuleTabBarActiveLabelColor(const ColorRGBA &SurfaceColor)
	{
		return CapsuleTabBarSurfaceIsLight(SurfaceColor) ? ui_token::color::TEXT_PRIMARY : ui_token::color::TEXT_ON_ACCENT;
	}

	inline ColorRGBA CapsuleTabBarInactiveLabelColor(const ColorRGBA &SurfaceColor)
	{
		return CapsuleTabBarSurfaceIsLight(SurfaceColor) ? ColorRGBA(0.09f, 0.10f, 0.12f, 0.86f) : ui_token::color::TEXT_PRIMARY;
	}

	inline ColorRGBA CapsuleTabBarHoverColor(const ColorRGBA &SurfaceColor)
	{
		return CapsuleTabBarSurfaceIsLight(SurfaceColor) ? ColorRGBA(0.0f, 0.0f, 0.0f, 0.06f) : ui_token::color::SURFACE_HIGHLIGHT;
	}

	struct SListItemProps
	{
		const char *m_pLeadingIcon = nullptr;
		const char *m_pTrailingText = nullptr;
		bool m_Selected = false;
		bool m_Disabled = false;
	};

	// Row entry for lists. Hover state cross-fades a background tint; the
	// selected state paints ACCENT_PRIMARY_DIM as a persistent background.
	bool ListItem(const IUiContext &Ctx, const void *pId, const char *pText, const CUIRect &Rect, const SListItemProps &Props = {});

} // namespace ui_widget

#endif
