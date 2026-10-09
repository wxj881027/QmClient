// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiButtons.h"

#include "QmAnimResolve.h"
#include "UiSurface.h"
#include "UiSurfaceText.h"

#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

namespace ui_widget
{
	IUiContext ControlContext(CUi *pUi)
	{
		IUiContext Ctx;
		Ctx.m_pUi = pUi;
		Ctx.m_pAnim = pUi != nullptr ? pUi->QmAnimationRuntime() : nullptr;
		Ctx.m_pTheme = pUi != nullptr ? &pUi->QmControlTheme() : nullptr;
		Ctx.m_ScopeHash = MakeUiScopeHash("qm-controls");
		return Ctx;
	}

	ColorRGBA DrawButtonSurface(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, const SButtonSurfaceOptions &Options)
	{
		if(Ctx.m_pUi == nullptr)
			return ColorRGBA();
		CUi *pUi = Ctx.m_pUi;
		const SUiTheme &Theme = Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : pUi->QmControlTheme();
		const ColorRGBA Backdrop = CUiScopedSurfaceText::CurrentSurface();
		SUiButtonState State;
		State.m_Enabled = Options.m_Enabled;
		State.m_Selected = Options.m_Selected;
		State.m_Hovered = pId != nullptr && Options.m_Enabled && pUi->Enabled() && !pUi->RenderOnly() && pUi->MouseHovered(Options.m_pHitRect != nullptr ? Options.m_pHitRect : &Rect);
		State.m_Pressed = State.m_Hovered && pUi->CheckActiveItem(pId) && (pUi->MouseButton(0) || pUi->MouseButton(1) || pUi->MouseButton(2));
		const ColorRGBA Base = Options.m_Color.value_or(Options.m_TransparentInactive ? ColorRGBA() : ResolveConfiguredControlSurface());
		auto Style = ResolveUiButtonStyle(Options.m_Role, Base, Backdrop, Theme, State);
		if(Options.m_TransparentInactive && !State.m_Hovered)
			Style.m_Border = ColorRGBA();
		if(pUi->RenderOnly())
			return Style.m_Fill;

		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : pUi->QmAnimationRuntime();
		if(pId != nullptr && pAnim != nullptr && Options.m_Enabled)
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
			const SUiAnimTransition &Transition = State.m_Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
			Style.m_Fill = ResolveUiAnimValueColor(*pAnim, NodeKey, Style.m_Fill, Transition.m_DurationSec, Transition.m_Easing);
		}
		const ColorRGBA Fill = pUi->ScaleBackgroundAlpha(Style.m_Fill);
		DrawRoundedSurface(Ctx, Rect, Fill, pUi->ScaleBackgroundAlpha(Style.m_Border), Options.m_Radius, ui_token::feedback::ICON_BORDER_WIDTH, Options.m_Corners);
		return Fill;
	}

	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners, bool Enabled, std::optional<ColorRGBA> Color, bool ShowSlash, bool TransparentInactive)
	{
		if(Ctx.m_pUi == nullptr || pId == nullptr || Ctx.m_pUi->RenderOnly())
			return 0;
		CUi *pUi = Ctx.m_pUi;
		const CUIRect ButtonRect = QmUiSquareIconButtonRect(Rect);
		SButtonSurfaceOptions Options;
		Options.m_Role = EUiButtonRole::ICON;
		Options.m_Enabled = Enabled && Checked >= 0;
		Options.m_Selected = Checked > 0;
		Options.m_Corners = Corners;
		Options.m_Color = Color;
		Options.m_TransparentInactive = TransparentInactive;
		const ColorRGBA Fill = DrawButtonSurface(Ctx, pId, ButtonRect, Options);
		CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
		CUIRect Label;
		ButtonRect.Margin(std::min(2.0f, ButtonRect.h * 0.1f), &Label);
		ColorRGBA Foreground = ResolveUiSurfaceForeground(SurfaceText.Surface()).WithAlpha(pUi->TextRender()->GetTextColor().a);
		if(!Options.m_Enabled)
			Foreground.a *= 0.65f;
		pUi->DrawQmIcon(Label, Icon, pFallbackIcon, Foreground);
		if(ShowSlash || !Options.m_Enabled)
		{
			const CQmIconSemanticColorScope SemanticColorScope;
			pUi->DrawQmIcon(Label, EQmIcon::SLASH, FontIcons::FONT_ICON_SLASH, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		}
		return Options.m_Enabled ? pUi->DoButtonLogic(pId, Checked, &ButtonRect, Flags) : 0;
	}

	namespace
	{
		bool DoStyledButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonRole Role)
		{
			if(Ctx.m_pUi == nullptr || pBtn == nullptr)
				return false;
			SButtonSurfaceOptions Options;
			Options.m_Role = Role;
			Options.m_Enabled = !Disabled;
			const ColorRGBA Fill = DrawButtonSurface(Ctx, pBtn, Rect, Options);
			CUiScopedSurfaceText SurfaceText(Ctx.m_pUi->TextRender(), Fill);
			if(Disabled)
				Ctx.m_pUi->TextRender()->TextColor(Ctx.m_pUi->TextRender()->GetTextColor().WithMultipliedAlpha(0.65f));
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, Rect.w - ui_token::spacing::SM);
			Props.m_MinimumFontSize = ui_token::font::SMALL;
			Props.m_EllipsisAtEnd = true;
			Ctx.m_pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC, Props);
			return !Disabled && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT) != 0;
		}
	}

	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::PRIMARY);
	}

	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::SECONDARY);
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled)
	{
		return DoIconButton(Ctx, pBtn, EQmIcon::COUNT, pIcon, 0, Rect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Disabled) != 0;
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled)
	{
		return DoIconButton(Ctx, pBtn, Icon, pFallbackIcon, 0, Rect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Disabled) != 0;
	}
}
