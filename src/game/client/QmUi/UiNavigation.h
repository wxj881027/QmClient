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

	// 滑块与 hover 胶囊共用的内缩量：hover 反馈必须与激活滑块同几何，不能大出一圈。
	inline constexpr float CAPSULE_TAB_INDICATOR_INSET = 2.0f;

	struct SCapsuleTabBarStyle
	{
		ColorRGBA m_CapsuleColor = ui_token::color::SURFACE_HIGHLIGHT; // 整排容器底色
		ColorRGBA m_IndicatorColor = ui_token::color::TEXT_PRIMARY; // 滑块胶囊底色
		ColorRGBA m_ActiveLabelColor = ui_token::color::TEXT_ON_ACCENT; // 滑块上的激活文字
		ColorRGBA m_InactiveLabelColor = ui_token::color::TEXT_PRIMARY; // 容器上的普通文字
		float m_CapsulePadding = 0.0f; // 容器相对 Tab 行的外扩（0 = 与 Tab 行同尺寸）
		float m_IndicatorInset = CAPSULE_TAB_INDICATOR_INSET; // 滑块相对 Tab 槽的内缩
	};

	// 胶囊 Tab 的纵向命中容差：胶囊只有一行高（SUB_TAB_HEIGHT），按在上下边缘、或按下后
	// 轻微移动几像素都会被判定成"点到了胶囊外面"。槽位只在纵向额外外扩这么多，横向不外扩，
	// 避免相邻页签互相抢点击。
	inline constexpr float CAPSULE_TAB_HIT_SLOP = 4.0f;

	// 纯几何命中判定：返回 (X, Y) 落在哪个槽位上，未命中返回 -1。X 用半开区间（与
	// CUIRect::Inside 一致），Y 额外给 Slop 的上下容差。
	inline int CapsuleTabBarSlotAtPoint(const CUIRect *pSlots, int Count, float X, float Y, float HitSlop = CAPSULE_TAB_HIT_SLOP)
	{
		if(pSlots == nullptr || Count <= 0)
			return -1;
		const float Slop = std::max(0.0f, HitSlop);
		for(int i = 0; i < Count; ++i)
		{
			const CUIRect &Slot = pSlots[i];
			if(X >= Slot.x && X < Slot.x + Slot.w && Y >= Slot.y - Slop && Y < Slot.y + Slot.h + Slop)
				return i;
		}
		return -1;
	}

	// 直接命中判定：本帧左键刚按下时返回被按下的页签下标，否则返回 -1。
	// DoButton_MenuTab 的 hot→active 两帧链路要求「上一帧已经 hot」且「抬起时仍在同一槽内」
	// 才提交选择，胶囊按在边缘或按下后轻微移动都会静默丢点击（页签有 hover 反馈却不切换）；
	// 这里按当前鼠标位置直接判定，可见性要求与按钮一致（非 RenderOnly、未被裁剪、无弹窗遮挡）。
	int CapsuleTabBarPressedIndex(const IUiContext &Ctx, const CUIRect *pSlots, int Count, float HitSlop = CAPSULE_TAB_HIT_SLOP);

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
	// Tints 为可选的单槽位自定义底色：画在容器之上、滑块之下，几何与滑块一致
	// （同 m_IndicatorInset 内缩），alpha 为 0 的槽位不画。
	struct SCapsuleTabBarTints
	{
		const CUIRect *m_pSlots = nullptr;
		const ColorRGBA *m_pColors = nullptr;
		int m_Count = 0;
	};

	void CapsuleTabBarChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect &RowRect, const CUIRect *pActiveSlot, const SCapsuleTabBarStyle &Style, const SCapsuleTabBarTints &Tints = {});

	// 直接给槽位表的重载：容器取槽位并集，激活项取 ActiveIndex（越界则只画容器）。
	// pSlotTints 为与槽位表等长的颜色数组（可为 nullptr），alpha 为 0 表示该槽位无自定义底色。
	inline void CapsuleTabBarChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect *pSlots, int Count, int ActiveIndex, const SCapsuleTabBarStyle &Style, const ColorRGBA *pSlotTints = nullptr)
	{
		SCapsuleTabBarTints Tints;
		if(pSlotTints != nullptr)
		{
			Tints.m_pSlots = pSlots;
			Tints.m_pColors = pSlotTints;
			Tints.m_Count = Count;
		}
		CapsuleTabBarChrome(Ctx, GroupId, CapsuleTabBarRowRect(pSlots, Count), pSlots != nullptr && ActiveIndex >= 0 && ActiveIndex < Count ? &pSlots[ActiveIndex] : nullptr, Style, Tints);
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

	// 两级分段选择器的外观：最外层容器胶囊 + 主滑块 + 次级滑块。
	// 绘制顺序是硬约束 —— 容器、主滑块、次级滑块必须先画，各段文字随后自己画，否则滑块
	// 滑动途中会盖住经过的文字。主滑块标记一级选项；一级项带子级时它整段盖住子级菜单，
	// 次级滑块再压在主滑块之上标出当前子项，所以子级文字色要按「主滑块底色」推导，
	// 不能按容器底色推导（容器上正常的字色压到主滑块上会看不见）。
	struct SNestedSegmentStyle
	{
		ColorRGBA m_ContainerColor = ui_token::color::SURFACE_HIGHLIGHT; // 最外层容器底色
		ColorRGBA m_MainIndicatorColor = ui_token::color::TEXT_PRIMARY; // 主滑块底色
		ColorRGBA m_SubIndicatorColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f); // 次级滑块底色（画在主滑块之上）
		ColorRGBA m_SubIndicatorBorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f); // 次级滑块描边
		ColorRGBA m_SubActiveLabelColor = ui_token::color::TEXT_ON_ACCENT; // 次级选中文字（压在主滑块上）
		ColorRGBA m_SubInactiveLabelColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f); // 次级未选中文字
		ColorRGBA m_SubHoverColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.08f); // 次级分段 hover 反馈
		float m_IndicatorInset = 2.0f; // 主滑块相对一级槽位的内缩
		float m_SubIndicatorInset = 3.0f; // 次级滑块相对二级槽位的内缩
	};

	// 画容器胶囊与两枚滑块胶囊。GroupId 标识同一排分段（不同排必须不同），滑块位置由
	// v2 动画运行时的弹簧轨道按帧求解，切换选项时带速度续接地滑过去。
	// pMainSlot / pSubSlot 为 nullptr 时对应滑块不绘制（例如一级项没有子级）。
	void NestedSegmentChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect &ContainerRect, const CUIRect *pMainSlot, const CUIRect *pSubSlot, const SNestedSegmentStyle &Style);

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
