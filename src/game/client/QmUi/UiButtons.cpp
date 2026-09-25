// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiButtons.h"

#include "QmAnimResolve.h"
#include "UiSurface.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

namespace ui_widget
{

	namespace
	{
		SQmIconStyle ConfiguredIconStyle()
		{
			SQmIconStyle IconStyle;
			IconStyle.m_Normal = ConfiguredQmUiIconColor(IconStyle.m_Normal);
			IconStyle.m_Hover = ConfiguredQmUiIconColor(IconStyle.m_Hover);
			IconStyle.m_Active = ConfiguredQmUiIconColor(IconStyle.m_Active);
			IconStyle.m_Disabled = ConfiguredQmUiIconColor(IconStyle.m_Disabled);
			return IconStyle;
		}

		void RenderQmGlyphIcon(const IUiContext &Ctx, const CUIRect &Rect, const char *pIcon, const ColorRGBA &Color)
		{
			ITextRender *pTextRender = Ctx.m_pUi->TextRender();
			const ColorRGBA PreviousColor = pTextRender->GetTextColor();
			const unsigned PreviousFlags = pTextRender->GetRenderFlags();
			const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
			pTextRender->TextColor(Color);
			pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
			pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			Ctx.m_pUi->DoLabel(&Rect, pIcon, QmIconFallbackFontSize(Rect), TEXTALIGN_MC);
			pTextRender->SetRenderFlags(PreviousFlags);
			pTextRender->SetFontPreset(PreviousPreset);
			pTextRender->TextColor(PreviousColor);
		}

		bool DoStyledButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, const ColorRGBA &Idle, const ColorRGBA &Hover, bool DrawBorder)
		{
			if(Ctx.m_pUi == nullptr || pBtn == nullptr)
				return false;
			if(Ctx.m_pUi->RenderOnly())
			{
				Ctx.m_pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC);
				return false;
			}
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);

			if(Disabled)
			{
				DrawRoundedSurface(Ctx, Rect, ui_token::color::BORDER_SUBTLE, ui_token::color::BORDER_SUBTLE, ui_token::radius::BASE);
				SLabelProperties LabelProps;
				Ctx.m_pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC, LabelProps);
				return false;
			}

			// 强度并入动画目标，按下时使用较短的过渡。
			const bool HoverPrev = Ctx.m_pUi->HotItem() == static_cast<const void *>(pBtn);
			const bool Pressed = Ctx.m_pUi->CheckActiveItem(pBtn);
			ColorRGBA Target = HoverPrev || Pressed ? Hover : Idle;
			Target.a *= Ctx.m_pUi->ButtonColorMul(pBtn);
			ColorRGBA Resolved = Target;
			if(Ctx.m_pAnim != nullptr)
			{
				const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pBtn));
				const SUiAnimTransition &Transition = Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
				Resolved = ResolveUiAnimValueColor(*Ctx.m_pAnim, NodeKey, Target, Transition.m_DurationSec, Transition.m_Easing);
			}

			DrawRoundedSurface(Ctx, Rect, Resolved, ui_token::color::BORDER_SUBTLE, ui_token::radius::BASE, DrawBorder ? Ctx.m_pUi->PixelSize() : 0.0f);
			Ctx.m_pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC);
			const int Result = Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT);
			return Result != 0;
		}
	} // namespace

	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled)
	{
		const ColorRGBA Accent = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, Accent.WithAlpha(0.18f), Accent, false);
	}

	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled)
	{
		// Idle is fully transparent so only the border shows; on hover, tint
		// gently toward ACCENT_PRIMARY_DIM.
		const ColorRGBA Idle{0.0f, 0.0f, 0.0f, 0.0f};
		const ColorRGBA Accent = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, Idle, Accent.WithAlpha(0.18f), true);
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled)
	{
		if(Ctx.m_pUi == nullptr || pBtn == nullptr || Ctx.m_pUi->RenderOnly())
			return false;
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);

		const bool HoverPrev = !Disabled && Ctx.m_pUi->HotItem() == static_cast<const void *>(pBtn);
		const bool Pressed = !Disabled && Ctx.m_pUi->CheckActiveItem(pBtn);
		const ColorRGBA Accent = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		ColorRGBA Target = HoverPrev || Pressed ? Accent.WithAlpha(0.18f) : ColorRGBA{0.0f, 0.0f, 0.0f, 0.0f};
		if(!Disabled)
			Target.a *= Ctx.m_pUi->ButtonColorMul(pBtn);
		ColorRGBA BgColor = Target;
		if(Ctx.m_pAnim != nullptr && !Disabled)
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pBtn));
			const SUiAnimTransition &Transition = Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
			BgColor = ResolveUiAnimValueColor(*Ctx.m_pAnim, NodeKey, Target, Transition.m_DurationSec, Transition.m_Easing);
		}

		DrawRoundedSurface(Ctx, Rect, BgColor, BgColor, ui_token::radius::BASE);
		const SQmIconStyle IconStyle = ConfiguredIconStyle();
		const EQmIconState IconState = Disabled ? EQmIconState::DISABLED : (Pressed ? EQmIconState::ACTIVE : HoverPrev ? EQmIconState::HOVER :
																 EQmIconState::NORMAL);
		RenderQmGlyphIcon(Ctx, Rect, pIcon, IconStyle.Color(IconState));
		const int Result = Disabled ? 0 : Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT);
		return Result != 0;
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled)
	{
		if(Ctx.m_pUi == nullptr || pBtn == nullptr || Ctx.m_pUi->RenderOnly())
			return false;
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);

		const bool HoverPrev = !Disabled && Ctx.m_pUi->HotItem() == static_cast<const void *>(pBtn);
		const bool Pressed = !Disabled && Ctx.m_pUi->CheckActiveItem(pBtn);
		const ColorRGBA Accent = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		ColorRGBA Target = HoverPrev || Pressed ? Accent.WithAlpha(0.18f) : ColorRGBA{0.0f, 0.0f, 0.0f, 0.0f};
		if(!Disabled)
			Target.a *= Ctx.m_pUi->ButtonColorMul(pBtn);
		ColorRGBA BgColor = Target;
		if(Ctx.m_pAnim != nullptr && !Disabled)
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pBtn));
			const SUiAnimTransition &Transition = Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
			BgColor = ResolveUiAnimValueColor(*Ctx.m_pAnim, NodeKey, Target, Transition.m_DurationSec, Transition.m_Easing);
		}

		DrawRoundedSurface(Ctx, Rect, BgColor, BgColor, ui_token::radius::BASE);
		const int Result = Disabled ? 0 : Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT);

		const float IconSide = minimum(Rect.w, Rect.h) * 0.58f;
		CUIRect IconRect;
		IconRect.w = IconSide;
		IconRect.h = IconSide;
		IconRect.x = Rect.x + (Rect.w - IconSide) * 0.5f;
		IconRect.y = Rect.y + (Rect.h - IconSide) * 0.5f;
		const EQmIconState IconState = Disabled ? EQmIconState::DISABLED : (Pressed ? EQmIconState::ACTIVE : HoverPrev ? EQmIconState::HOVER :
																 EQmIconState::NORMAL);
		const SQmIconStyle IconStyle = ConfiguredIconStyle();
		if(Ctx.m_pIconManager == nullptr || (Ctx.m_pIconManager->PreferFontFallback() && pFallbackIcon != nullptr && pFallbackIcon[0] != '\0') || !Ctx.m_pIconManager->RenderIcon(Icon, IconRect, IconState, IconStyle))
		{
			RenderQmGlyphIcon(Ctx, IconRect, pFallbackIcon, IconStyle.Color(IconState));
		}

		return Result != 0;
	}

} // namespace ui_widget
