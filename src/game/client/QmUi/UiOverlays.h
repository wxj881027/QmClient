// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIOVERLAYS_H
#define GAME_CLIENT_QMUI_UIOVERLAYS_H

#include "QmAnimResolve.h"
#include "QmTree.h"
#include "UiContainers.h"
#include "UiContext.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/tooltips.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <optional>

namespace ui_widget
{

	// Thin shim over CTooltips::DoToolTip. Centralised here so dogfood/widget
	// callers can address tooltips without pulling tooltips.h directly.
	inline void Tooltip(const IUiContext &Ctx, const void *pId, const CUIRect &Anchor, const char *pText, float WidthHint = -1.0f)
	{
		if(Ctx.m_pTooltips != nullptr && pText != nullptr)
			Ctx.m_pTooltips->DoToolTip(pId, &Anchor, pText, WidthHint);
	}

	// 页面主标题：可选前置图标的单行标题。字号、图标尺寸、图标间距统一取
	// ui_token::page_title；设置页顶部标题与工作区标题都走本入口，不再各自写字号。
	// 图标与文字作为一个整体，按 Align 左/中/右对齐；文字超宽按省略号收尾。
	inline void PageTitle(const IUiContext &Ctx, const CUIRect &Rect, const char *pLabel,
		std::optional<EQmIcon> Icon = std::nullopt, const char *pFallbackIcon = nullptr, int Align = TEXTALIGN_ML)
	{
		if(Ctx.m_pUi == nullptr || pLabel == nullptr)
			return;
		ITextRender *pTextRender = Ctx.m_pUi->TextRender();
		const float FontSize = ui_token::page_title::FONT_SIZE;
		const bool HasIcon = Icon.has_value() && pFallbackIcon != nullptr && pFallbackIcon[0] != '\0';
		const float IconSize = HasIcon ? ui_token::page_title::ICON_SIZE : 0.0f;
		const float Gap = HasIcon ? ui_token::page_title::ICON_GAP : 0.0f;
		const float AvailableText = std::max(0.0f, Rect.w - IconSize - Gap);
		const float TextWidth = std::min(pTextRender->TextWidth(FontSize, pLabel), AvailableText);
		const float GroupWidth = IconSize + Gap + TextWidth;
		float GroupX = Rect.x;
		if(Align == TEXTALIGN_MC)
			GroupX = Rect.x + (Rect.w - GroupWidth) * 0.5f;
		else if(Align == TEXTALIGN_MR)
			GroupX = Rect.x + Rect.w - GroupWidth;
		if(HasIcon)
		{
			CUIRect IconRect{GroupX, Rect.y + (Rect.h - IconSize) * 0.5f, IconSize, IconSize};
			Ctx.m_pUi->DoLabel_QmIcon(&IconRect, *Icon, pFallbackIcon, IconSize, TEXTALIGN_MC);
		}
		CUIRect LabelRect{GroupX + IconSize + Gap, Rect.y, TextWidth, Rect.h};
		SLabelProperties Props;
		Props.m_MaxWidth = LabelRect.w;
		Props.m_EllipsisAtEnd = true;
		Props.SetColor(ui_token::color::TEXT_PRIMARY);
		Ctx.m_pUi->DoLabel(&LabelRect, pLabel, FontSize, Align, Props);
	}

	struct SAnimatePresenceResult
	{
		bool m_Render = false;
		float m_Alpha = 0.0f;
		uint64_t m_NodeKey = 0;
		bool m_FreshEnter = false;
	};

	// pPresenceTransition 非空时拆成两条轨道：显隐/生命周期按它的时钟推进，
	// 透明度用 Transition。为空时显隐仍由 ALPHA 轨道驱动（旧行为）。
	inline SAnimatePresenceResult AnimatePresence(const IUiContext &Ctx, const void *pId, bool Visible, const SUiAnimTransition &Transition, const SUiAnimTransition *pPresenceTransition = nullptr)
	{
		const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
		if(Ctx.m_pTree == nullptr || Ctx.m_pAnim == nullptr)
			return {Visible, Visible ? 1.0f : 0.0f, NodeKey, Visible};

		const SUiPresenceResult Presence = Ctx.m_pTree->ResolvePresence(*Ctx.m_pAnim, NodeKey, Visible, Transition, pPresenceTransition);
		return {Presence.m_Render, Presence.m_Alpha, NodeKey, Presence.m_FreshEnter};
	}

	inline float ResolveModalScale(const IUiContext &Ctx, const SAnimatePresenceResult &Presence, bool Open)
	{
		if(Ctx.m_pAnim == nullptr)
			return 1.0f;
		if(Ctx.m_pTree == nullptr || g_Config.m_QmUiMotionLevel < 2)
		{
			// 减少动态效果或缺少生命周期状态时保持原尺寸，并停止已有缩放。
			SetUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::SCALE, 1.0f);
			return 1.0f;
		}

		// 只在完整入场时设置起点；稳定绘制与退出中重开都延续当前状态。
		if(Open && Presence.m_FreshEnter)
			SetUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::SCALE, 0.98f);
		return ResolveUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::SCALE, Open ? 1.0f : 0.98f, ui_token::motion::MODAL_IN.m_Spring, 2, 0.004f);
	}

	struct SModalProps
	{
		float m_Width = 480.0f;
		float m_Height = 280.0f;
		const char *m_pTitle = nullptr;
		bool m_EscToClose = true;
	};

	struct SToastProps
	{
		float m_Width = 280.0f;
		float m_Height = 38.0f;
		float m_Margin = ui_token::spacing::LG;
		const char *m_pText = nullptr;
	};

	struct SToastState
	{
		bool m_WasVisible = false;
	};

	inline bool Toast(const IUiContext &Ctx, const void *pId, SToastState *pState, bool Visible, const CUIRect &ScreenRect, const SToastProps &Props)
	{
		if(Ctx.m_pUi == nullptr || Props.m_pText == nullptr || Props.m_pText[0] == '\0')
			return false;

		const SAnimatePresenceResult Presence = AnimatePresence(Ctx, pId, Visible, ui_token::motion::TOAST_SLIDE);
		if(!Presence.m_Render)
		{
			if(pState != nullptr)
				pState->m_WasVisible = false;
			return false;
		}

		CUIRect Target;
		Target.w = Props.m_Width;
		Target.h = Props.m_Height;
		Target.x = ScreenRect.x + (ScreenRect.w - Target.w) * 0.5f;
		Target.y = ScreenRect.y + ScreenRect.h - Props.m_Margin - Target.h;

		const float HiddenY = Target.y + Target.h + Props.m_Margin;
		float CurrentY = Visible ? Target.y : HiddenY;
		float Alpha = Presence.m_Alpha;

		if(Ctx.m_pAnim != nullptr)
		{
			if(g_Config.m_QmUiMotionLevel < 2)
			{
				// 减少动态效果只保留淡入淡出，中途切换也立即停止位移。
				CurrentY = Target.y;
				SetUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::POS_Y, CurrentY);
			}
			else
			{
				if(Visible && pState != nullptr && !pState->m_WasVisible)
					SetUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::POS_Y, HiddenY);
				CurrentY = ResolveUiPresentationStateValue(*Ctx.m_pAnim, Presence.m_NodeKey, EUiAnimProperty::POS_Y, Visible ? Target.y : HiddenY, ui_token::motion::TOAST_SLIDE.m_Spring, 2, 0.004f);
			}
		}
		else if(!Visible)
		{
			if(pState != nullptr)
				pState->m_WasVisible = false;
			return false;
		}

		if(pState != nullptr)
			pState->m_WasVisible = Presence.m_Render;

		CUIRect ToastRect = Target;
		ToastRect.y = CurrentY;

		ColorRGBA Shadow = ui_token::color::SURFACE_SHADOW;
		Shadow.a *= Alpha;
		CUIRect ShadowRect = ToastRect;
		ShadowRect.x += ui_token::elevation::SHADOW_X_HIGH;
		ShadowRect.y += ui_token::elevation::SHADOW_Y_HIGH;
		DrawRoundedSurface(Ctx, ShadowRect, Shadow, ColorRGBA(), ui_token::radius::BASE);

		ColorRGBA Bg = ui_token::color::SURFACE_ELEVATED;
		Bg.a *= Alpha;
		DrawRoundedSurface(Ctx, ToastRect, Bg, ColorRGBA(), ui_token::radius::BASE);

		ColorRGBA Text = ui_token::color::TEXT_PRIMARY;
		Text.a *= Alpha;
		if(Ctx.m_pTextRender != nullptr)
			Ctx.m_pTextRender->TextColor(Text);
		CUIRect Label;
		ToastRect.Margin(ui_token::spacing::MD, &Label);
		Ctx.m_pUi->DoLabel(&Label, Props.m_pText, ui_token::font::BODY, TEXTALIGN_ML);
		if(Ctx.m_pTextRender != nullptr)
			Ctx.m_pTextRender->TextColor(Ctx.m_pTextRender->DefaultTextColor());

		return true;
	}

	// Centered modal with overlay backdrop, scale-in animation and optional ESC
	// dismissal. Renders inline when *pOpen is true; Body is invoked with the
	// inner content rect (already inset). Caller owns the *pOpen flag.
	template<typename BodyFn>
	bool Modal(const IUiContext &Ctx, const void *pId, bool *pOpen, const CUIRect &ScreenRect, const SModalProps &Props, BodyFn &&Body)
	{
		if(Ctx.m_pUi == nullptr || pOpen == nullptr)
			return false;

		const SAnimatePresenceResult Presence = AnimatePresence(Ctx, pId, *pOpen, ui_token::motion::MODAL_IN, &ui_token::motion::MODAL_PRESENCE);
		if(!Presence.m_Render)
			return false;

		// Backdrop
		ColorRGBA Backdrop = ui_token::color::SURFACE_OVERLAY;
		Backdrop.a *= Presence.m_Alpha;
		ScreenRect.Draw(Backdrop, IGraphics::CORNER_NONE, 0.0f);

		// Compute centered rect at target size
		CUIRect Centered;
		Centered.w = Props.m_Width;
		Centered.h = Props.m_Height;
		Centered.x = ScreenRect.x + (ScreenRect.w - Centered.w) * 0.5f;
		Centered.y = ScreenRect.y + (ScreenRect.h - Centered.h) * 0.5f;

		const float Scale = ResolveModalScale(Ctx, Presence, *pOpen);

		CUIRect Scaled = Centered;
		const float DeltaW = Centered.w * (1.0f - Scale);
		const float DeltaH = Centered.h * (1.0f - Scale);
		Scaled.x += DeltaW * 0.5f;
		Scaled.y += DeltaH * 0.5f;
		Scaled.w -= DeltaW;
		Scaled.h -= DeltaH;

		SCardProps CardProps;
		CardProps.m_pTitle = Props.m_pTitle;
		CardProps.m_Elevation = 2;
		DrawCard(Ctx, Scaled, CardProps, [&](CUIRect &Content) {
			Body(Content);
		});

		if(Props.m_EscToClose && Ctx.m_pUi->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			*pOpen = false;

		return *pOpen;
	}

} // namespace ui_widget

#endif
