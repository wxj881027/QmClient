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
	namespace
	{
		// 禁用/警示斜线的双色：暗遮罩衬底 + 纯警示红。语义是「不可用」标记，
		// 与通知语义的 ui_token::color::DANGER（柔和珊瑚红）刻意区分。
		constexpr ColorRGBA SlashMaskColor{0.0f, 0.0f, 0.0f, 0.65f};
		constexpr ColorRGBA SlashWarnColor{1.0f, 0.0f, 0.0f, 1.0f};
	}

	SSettingsActionGroupLayout PrepareSettingsActionGroup(CUi &Ui, SSettingsActionGroupState &State, const CUIRect &Row, const char *const *ppLabels, int Count, const SSettingsContentMetrics &Metrics, ESettingsActionGroupWidth WidthPolicy)
	{
		if(ppLabels == nullptr || Count <= 0 || Count > SSettingsChoiceLayout::MAX_SEGMENTS)
			return {};
		if(WidthPolicy == ESettingsActionGroupWidth::FILL)
			return ResolveSettingsActionGroupLayout(Row, Count, Metrics, 0.0f, WidthPolicy);
		const uint64_t FontRevision = Ui.TextRender()->GlyphAtlasRevision() * 31 + (uint64_t)Ui.TextRender()->GetFontPreset();
		const float Width = State.m_Widths.Resolve(ppLabels, Count, Metrics.m_BodySize, FontRevision, [&Ui](const char *pText, float Size) { return Ui.TextRender()->TextWidth(Size, pText); });
		return ResolveSettingsActionGroupLayout(Row, Count, Metrics, Width + 2.0f * ui_token::spacing::MD, WidthPolicy);
	}

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
		State.m_Hovered = pId != nullptr && Options.m_Enabled && pUi->Enabled() && pUi->VisualMouseHovered(Options.m_pHitRect != nullptr ? Options.m_pHitRect : &Rect);
		State.m_Pressed = State.m_Hovered && pUi->CheckActiveItem(pId) && (pUi->MouseButton(0) || pUi->MouseButton(1) || pUi->MouseButton(2));
		const ColorRGBA Base = Options.m_Color.value_or(Options.m_TransparentInactive ? ColorRGBA() : ResolveConfiguredControlSurface());
		auto Style = ResolveUiButtonStyle(Options.m_Role, Base, Backdrop, Theme, State);
		if(Options.m_TransparentInactive && !State.m_Hovered && !State.m_Selected)
			Style.m_Border = ColorRGBA();
		if(pUi->DrawingSuppressed())
			return Style.m_Fill;

		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : pUi->QmAnimationRuntime();
		CUIRect DrawRect = Rect;
		if(pId != nullptr && pAnim != nullptr && Options.m_Enabled && !pUi->AnimationsBlocked())
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
			const SUiAnimTransition &Transition = State.m_Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
			Style.m_Fill = ResolveUiAnimValueColor(*pAnim, NodeKey, Style.m_Fill, Transition.m_DurationSec, Transition.m_Easing);
			const float TargetScale = State.m_Pressed ? 0.97f : 1.0f;
			const float Scale = std::clamp(ResolveUiAnimSpringValue(*pAnim, NodeKey, EUiAnimProperty::SCALE, TargetScale, ui_token::motion::TWITCH_SPRING, 2), 0.90f, 1.05f);
			if(std::abs(Scale - 1.0f) > 0.001f)
			{
				DrawRect.x += DrawRect.w * (1.0f - Scale) * 0.5f;
				DrawRect.y += DrawRect.h * (1.0f - Scale) * 0.5f;
				DrawRect.w *= Scale;
				DrawRect.h *= Scale;
			}
		}
		const ColorRGBA Fill = pUi->ScaleControlAlpha(Style.m_Fill);
		DrawRoundedSurface(Ctx, DrawRect, Fill, pUi->ScaleControlAlpha(Style.m_Border), Options.m_Radius, Options.m_BorderWidth, Options.m_Corners);
		return Fill;
	}

	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners, bool Enabled, std::optional<ColorRGBA> Color, bool ShowSlash, bool TransparentInactive, const CUIRect *pHitRect)
	{
		if(Ctx.m_pUi == nullptr || pId == nullptr || (Ctx.m_pUi->DrawingSuppressed() && !Ctx.m_pUi->PreLayoutInput()))
			return 0;
		CUi *pUi = Ctx.m_pUi;
		const CUIRect ButtonRect = QmUiSquareIconButtonRect(Rect);
		const CUIRect &HitRect = pHitRect != nullptr ? *pHitRect : ButtonRect;
		if(pUi->PreLayoutInput())
			return Enabled && Checked >= 0 ? pUi->DoButtonLogic(pId, Checked, &HitRect, Flags) : 0;
		SButtonSurfaceOptions Options;
		Options.m_Role = EUiButtonRole::ICON;
		Options.m_Enabled = Enabled && Checked >= 0;
		Options.m_Selected = Checked > 0;
		Options.m_Corners = Corners;
		Options.m_Color = Color;
		Options.m_TransparentInactive = TransparentInactive;
		Options.m_pHitRect = &HitRect;
		const ColorRGBA SemanticColor = pUi->TextRender()->GetTextColor();
		const ColorRGBA Fill = DrawButtonSurface(Ctx, pId, ButtonRect, Options);
		CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
		CUIRect Label;
		ButtonRect.Margin(std::min(2.0f, ButtonRect.h * 0.1f), &Label);
		ColorRGBA Foreground = ResolveUiSurfaceForeground(SurfaceText.Surface()).WithAlpha(pUi->TextRender()->GetTextColor().a);
		if(CQmIconSemanticColorScope::Active())
			Foreground = ResolveUiSurfaceIconColor(SurfaceText.Surface(), SemanticColor);
		if(!Options.m_Enabled)
			Foreground.a *= 0.65f;
		pUi->DrawQmIcon(Label, Icon, pFallbackIcon, Foreground);
		if((ShowSlash || !Options.m_Enabled) && Label.w > 0.0f && Label.h > 0.0f)
		{
			// 禁用标记使用左上到右下的平头斜线，不依赖字体字形的方向和端点形状。
			const float Inset = Label.w * 0.16f;
			const vec2 Start(Label.x + Inset, Label.y + Inset);
			const vec2 End(Label.x + Label.w - Inset, Label.y + Label.h - Inset);
			const vec2 Normal = normalize(vec2(-(End.y - Start.y), End.x - Start.x));
			IGraphics *pGraphics = pUi->Graphics();
			pGraphics->TextureClear();
			pGraphics->QuadsBegin();
			const auto DrawSlash = [&](float Width, ColorRGBA SlashColor) {
				const vec2 Offset = Normal * Width * 0.5f;
				const IGraphics::CFreeformItem Quad(Start + Offset, Start - Offset, End + Offset, End - Offset);
				pGraphics->SetColor(SlashColor);
				pGraphics->QuadsDrawFreeform(&Quad, 1);
			};
			DrawSlash(Label.w * 0.18f, pUi->ScaleControlAlpha(SlashMaskColor));
			DrawSlash(Label.w * 0.10f, pUi->ScaleControlAlpha(SlashWarnColor));
			pGraphics->QuadsEnd();
		}
		return Options.m_Enabled && !pUi->RenderOnly() ? pUi->DoButtonLogic(pId, Checked, &HitRect, Flags) : 0;
	}

	namespace
	{
		bool DoStyledButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonRole Role, const int Corners = IGraphics::CORNER_ALL, const bool DrawBorder = true, const EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, const CUIRect *pSurfaceRect = nullptr, bool Selected = false)
		{
			if(Ctx.m_pUi == nullptr || pBtn == nullptr)
				return false;
			SButtonSurfaceOptions Options;
			Options.m_Role = Role;
			Options.m_Radius = ResolveUiTextButtonRadius(Geometry);
			if(!DrawBorder)
				Options.m_BorderWidth = 0.0f;
			Options.m_Corners = Corners;
			Options.m_Enabled = !Disabled;
			Options.m_Selected = Selected;
			Options.m_pHitRect = &Rect;
			const ColorRGBA Fill = DrawButtonSurface(Ctx, pBtn, pSurfaceRect != nullptr ? *pSurfaceRect : Rect, Options);
			CUiScopedSurfaceText SurfaceText(Ctx.m_pUi->TextRender(), Fill);
			if(Disabled)
				Ctx.m_pUi->TextRender()->TextColor(Ctx.m_pUi->TextRender()->GetTextColor().WithMultipliedAlpha(0.65f));
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, Rect.w - ui_token::spacing::SM);
			Props.m_MinimumFontSize = ui_token::font::SMALL;
			Props.m_EllipsisAtEnd = true;
			if(Props.m_MaxWidth > 0.0f && Rect.h > 0.0f)
				Ctx.m_pUi->DoLabel(&Rect, pText, ui_token::font::BODY, TEXTALIGN_MC, Props);
			if(Disabled && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->CheckActiveItem(pBtn))
				Ctx.m_pUi->SetActiveItem(nullptr);
			const bool Clicked = !Disabled && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT) != 0;
			if(Clicked)
				Ctx.m_pUi->SetKeyboardFocus(pBtn);
			return Clicked;
		}
	}

	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonGeometry Geometry)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::PRIMARY, IGraphics::CORNER_ALL, true, Geometry);
	}

	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonGeometry Geometry, bool Selected)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, true, Geometry, nullptr, Selected);
	}

	int SplitActionButton(const IUiContext &Ctx, CButtonContainer *pLeft, const char *pLeftText, CButtonContainer *pRight, const char *pRightText, const CUIRect &Rect, bool LeftDisabled, bool RightDisabled, EUiButtonGeometry Geometry, const char *pLeftTooltip, const char *pRightTooltip)
	{
		if(Ctx.m_pUi == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return -1;
		CUIRect Left, Right;
		Rect.VSplitMid(&Left, &Right);
		const void *apIds[] = {pLeft, pRight};
		const bool aEnabled[] = {!LeftDisabled, !RightDisabled};
		const int KeyboardAction = Ctx.m_pUi->DoButtonGroupKeyboard(apIds, aEnabled, 2);
		// 两半裁剪同一完整按钮表面，外框与普通按钮一致，中心没有重复描边。
		Ctx.m_pUi->ClipEnable(&Left);
		const bool LeftClicked = DoStyledButton(Ctx, pLeft, pLeftText, Left, LeftDisabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, true, Geometry, &Rect);
		Ctx.m_pUi->ClipDisable();
		Ctx.m_pUi->ClipEnable(&Right);
		const bool RightClicked = DoStyledButton(Ctx, pRight, pRightText, Right, RightDisabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, true, Geometry, &Rect);
		Ctx.m_pUi->ClipDisable();
		const float Inset = std::min(ui_token::spacing::XS, Rect.h * 0.25f);
		const CUIRect Divider{Right.x - 0.5f, Rect.y + Inset, 1.0f, std::max(0.0f, Rect.h - 2.0f * Inset)};
		if(!Ctx.m_pUi->DrawingSuppressed())
			Divider.Draw(Ctx.m_pUi->ScaleBackgroundAlpha(ui_token::color::BORDER_SUBTLE), IGraphics::CORNER_NONE, 0.0f);
		Ctx.m_pUi->DoButtonTooltip(pLeft, &Left, pLeftTooltip);
		Ctx.m_pUi->DoButtonTooltip(pRight, &Right, pRightTooltip);
		if(!Ctx.m_pUi->DrawingSuppressed())
		{
			const void *pFocus = Ctx.m_pUi->KeyboardFocusItem();
			if((pFocus == pLeft && !LeftDisabled) || (pFocus == pRight && !RightDisabled))
			{
				CUIRect FocusRect = pFocus == pLeft ? Left : Right;
				FocusRect.Margin(std::min(2.0f, std::min(FocusRect.w, FocusRect.h) * 0.25f), &FocusRect);
				const SUiTheme &Theme = Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : Ctx.m_pUi->QmControlTheme();
				DrawRoundedSurface(Ctx, FocusRect, ColorRGBA(), Theme.m_FocusRing, ResolveUiTextButtonRadius(Geometry), ui_token::feedback::ICON_BORDER_WIDTH);
			}
		}
		return LeftClicked ? 0 : RightClicked ? 1 :
							KeyboardAction;
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
