// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiNavigation.h"

#include "QmAnimResolve.h"
#include "UiMotion.h"
#include "UiSurface.h"
#include "UiTokens.h"

#include <engine/graphics.h>

#include <game/client/components/menus.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ui_widget
{

	int TabBar(const IUiContext &Ctx, const char *const *ppLabels, int Count, int *pActive, const CUIRect &Rect)
	{
		if(Ctx.m_pUi == nullptr || Ctx.m_pMenus == nullptr || pActive == nullptr || Count <= 0)
			return pActive != nullptr ? *pActive : 0;

		// Static identity pool — caller-side need not allocate CButtonContainer
		// per tab. We index by (TabBar address, tab index) which is stable as long
		// as the caller's TabBar invocation site is stable.
		static std::unordered_map<std::uintptr_t, std::vector<CButtonContainer>> s_vButtonPools;
		std::vector<CButtonContainer> &ButtonPool = s_vButtonPools[reinterpret_cast<std::uintptr_t>(pActive)];
		const std::size_t Needed = static_cast<std::size_t>(Count);
		if(ButtonPool.size() < Needed)
			ButtonPool.resize(Needed);

		const float TabWidth = Rect.w / static_cast<float>(Count);
		std::vector<CUIRect> vTabSlots(static_cast<std::size_t>(Count));
		CUIRect Tabs = Rect;
		CUIRect TabsRow;
		Tabs.HSplitBottom(2.0f, &TabsRow, nullptr);

		for(int i = 0; i < Count; ++i)
		{
			CUIRect &TabRect = vTabSlots[static_cast<std::size_t>(i)];
			TabRect.x = Rect.x + TabWidth * static_cast<float>(i);
			TabRect.y = TabsRow.y;
			TabRect.w = TabWidth;
			TabRect.h = TabsRow.h;
		}

		// 胶囊 Tabbar：容器与滑块先画，页签文字随后 —— 滑块压在文字之下，
		// 激活位置不再用页签下方的下划线小块表达。
		SCapsuleTabBarStyle Style;
		Style.m_IndicatorColor = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		CapsuleTabBarChrome(Ctx, BuildUiAnimNodeKey(MakeUiScopeHash("ui_widget_tabbar_capsule"), reinterpret_cast<uint64_t>(pActive)), vTabSlots.data(), Count, *pActive, Style);

		for(int i = 0; i < Count; ++i)
		{
			const int Checked = (*pActive == i) ? 1 : 0;
			if(Ctx.m_pMenus->DoButton_MenuTab(&ButtonPool[i], ppLabels[i], Checked, &vTabSlots[static_cast<std::size_t>(i)], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true) != 0)
				*pActive = i;
		}

		return *pActive;
	}

	void CapsuleTabBarChrome(const IUiContext &Ctx, const uint64_t GroupId, const CUIRect &RowRect, const CUIRect *pActiveSlot, const SCapsuleTabBarStyle &Style)
	{
		if(Ctx.m_pUi == nullptr || RowRect.w <= 0.0f || RowRect.h <= 0.0f)
			return;
		// 预热 / 文字计划收集帧只跑逻辑不落绘制，也不推进滑块弹簧，否则预热帧会把滑块
		// 直接推到目标位置，下一帧切换就看不到滑动。
		if(Ctx.m_pUi->RenderOnly())
			return;

		CUIRect Capsule;
		RowRect.Margin(-Style.m_CapsulePadding, &Capsule);
		DrawRoundedSurface(Ctx, Capsule, Style.m_CapsuleColor, ColorRGBA(), ui_token::radius::PILL);

		if(pActiveSlot == nullptr)
			return;
		CUIRect Target;
		pActiveSlot->Margin(Style.m_IndicatorInset, &Target);
		if(Target.w <= 0.0f || Target.h <= 0.0f)
			return;

		CUIRect Indicator = Target;
		if(Ctx.m_pAnim != nullptr)
		{
			// 滑块弹簧：欠阻尼一点点（ζ≈0.93），切换 Tab 时带速度续接地滑过去，
			// 落到目标附近再收住，不会来回弹。
			static constexpr SUiSpringConfig s_IndicatorSpring{1.0f, 420.0f, 38.0f, 0.05f, 0.4f};
			const uint64_t NodeKey = BuildUiAnimNodeKey(GroupId, 0);
			Indicator.x = ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::POS_X, Target.x, s_IndicatorSpring, 2);
			Indicator.y = ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::POS_Y, Target.y, s_IndicatorSpring, 2);
			Indicator.w = ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::WIDTH, Target.w, s_IndicatorSpring, 2);
			Indicator.h = ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::HEIGHT, Target.h, s_IndicatorSpring, 2);
		}
		DrawRoundedSurface(Ctx, Indicator, Style.m_IndicatorColor, ColorRGBA(), ui_token::radius::PILL);
	}

	bool ListItem(const IUiContext &Ctx, const void *pId, const char *pText, const CUIRect &Rect, const SListItemProps &Props)
	{
		if(Ctx.m_pUi == nullptr)
			return false;
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);

		// Background: selected first, then hover blend on top.
		if(Props.m_Selected)
			Rect.Draw(Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Selected : ui_token::color::ACCENT_PRIMARY_DIM, IGraphics::CORNER_ALL, ui_token::radius::TIGHT);

		if(Ctx.m_pAnim != nullptr)
		{
			const bool HoverPrev = Ctx.m_pUi->HotItem() == pId;
			const float TargetAlpha = HoverPrev ? 1.0f : 0.0f;
			const float Alpha = AnimateStateValue(Ctx, pId, EUiAnimProperty::ALPHA, TargetAlpha, ui_curve::DECELERATE);
			if(Alpha > 0.01f)
			{
				ColorRGBA HoverBg = ui_token::color::SURFACE_HIGHLIGHT;
				HoverBg.a *= Alpha;
				Rect.Draw(HoverBg, IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
			}
		}

		const int Result = Props.m_Disabled ? 0 : Ctx.m_pUi->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);

		// Content layout
		CUIRect Content;
		Rect.VMargin(ui_token::spacing::SM, &Content);

		if(Props.m_pLeadingIcon != nullptr)
		{
			CUIRect Icon;
			Content.VSplitLeft(Rect.h, &Icon, &Content);
			Ctx.m_pUi->DoLabel(&Icon, Props.m_pLeadingIcon, ui_token::font::BODY, TEXTALIGN_MC);
			Content.VSplitLeft(ui_token::spacing::XS, nullptr, &Content);
		}

		if(Props.m_pTrailingText != nullptr)
		{
			CUIRect Trailing;
			Content.VSplitRight(64.0f, &Content, &Trailing);
			Ctx.m_pUi->DoLabel(&Trailing, Props.m_pTrailingText, ui_token::font::CAPTION, TEXTALIGN_MR);
		}

		Ctx.m_pUi->DoLabel(&Content, pText, ui_token::font::BODY, TEXTALIGN_ML);

		return Result != 0;
	}

} // namespace ui_widget
