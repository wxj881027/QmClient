// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiForms.h"

#include "SettingsPageLayout.h"
#include "UiButtons.h"
#include "UiDiscreteSliderStyle.h"
#include "UiFormLogic.h"
#include "UiMotion.h"
#include "UiSurface.h"
#include "UiSurfaceText.h"
#include "UiTheme.h"

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/tooltips.h>
#include <game/client/lineinput.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui_widget
{

	namespace
	{
		// 表单外观共享父层显隐语义，不再把页面背景透明度乘入控件颜色。
		void DrawControlSurface(const IUiContext &Ctx, const CUIRect &Rect, ColorRGBA Fill, ColorRGBA Border, float Radius, float BorderWidth = 0.0f, int Corners = IGraphics::CORNER_ALL)
		{
			DrawRoundedSurface(Ctx, Rect, Ctx.m_pUi->ScaleControlAlpha(Fill), Ctx.m_pUi->ScaleControlAlpha(Border), Radius, BorderWidth, Corners);
		}

		void DrawTextFieldFocusBorder(const IUiContext &Ctx, const CUIRect &Rect, float Alpha);
		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect);
		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline);
		void DrawTextFieldShell(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, int Corners, float Radius, const ColorRGBA *pBorderColorOverride = nullptr, float BorderWidthOverride = 1.0f);
		bool InputFieldFocusActive(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline);

		SUiTheme ThemeFor(const IUiContext &Ctx)
		{
			return Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : Ctx.m_pUi != nullptr ? Ctx.m_pUi->QmControlTheme() :
												ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor);
		}

		SInputFieldResult BuildInputFieldResult(const IUiContext &Ctx, CLineInput *pInput, bool Changed, bool WasActive, bool WasEmpty, bool Clearable)
		{
			const bool SubmitPressed = Ctx.m_pUi != nullptr && (Ctx.m_pUi->Input()->KeyPress(KEY_RETURN) || Ctx.m_pUi->Input()->KeyPress(KEY_KP_ENTER));
			return ui_widget::BuildInputFieldResult(WasActive, pInput->IsActive(), Changed, SubmitPressed, WasEmpty, pInput->IsEmpty(), Clearable);
		}

		void DrawTextFieldShell(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const int Corners, const float Radius, const ColorRGBA *pBorderColorOverride, const float BorderWidthOverride)
		{
			const SUiTheme Theme = ThemeFor(Ctx);
			ColorRGBA Border = pBorderColorOverride != nullptr ? *pBorderColorOverride : Theme.m_Border;
			if(pBorderColorOverride == nullptr)
				Border.a = std::max(Border.a, 0.24f);
			DrawControlSurface(Ctx, Rect, Fill, Border, Radius, BorderWidthOverride, Corners);
		}

		bool NumericFieldTextIsInfinite(const char *pText)
		{
			const char *pTrimmed = str_utf8_skip_whitespaces(pText);
			char aTrimmed[32];
			str_copy(aTrimmed, pTrimmed);
			str_utf8_trim_right(aTrimmed);
			return str_comp(aTrimmed, "∞") == 0 || str_comp_nocase(aTrimmed, "inf") == 0 || str_comp_nocase(aTrimmed, "infinite") == 0;
		}

		void DrawTextFieldFocusBorder(const IUiContext &Ctx, const CUIRect &Rect, float Alpha)
		{
			if(Alpha <= 0.01f)
				return;

			const SUiTheme &Theme = ThemeFor(Ctx);
			ColorRGBA RingColor = Theme.m_FocusRing;
			RingColor.a *= Alpha;
			// 激活态直接把外壳边框画粗并换成强调色，不做外扩光圈。
			DrawControlSurface(Ctx, Rect, ResolveConfiguredInputSurface(), RingColor, ui_token::radius::BASE, Theme.m_FocusRingWidth);
		}

		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect)
		{
			DrawTextFieldFocusBorder(Ctx, pInput, Rect, false);
		}

		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline)
		{
			if(Ctx.m_pAnim == nullptr)
				return;
			const float TargetAlpha = InputFieldFocusActive(Ctx, pInput, Rect, Multiline) ? 1.0f : 0.0f;
			const float Alpha = AnimateStateValue(Ctx, pInput, EUiAnimProperty::ALPHA, TargetAlpha, ui_curve::DECELERATE);
			DrawTextFieldFocusBorder(Ctx, Rect, Alpha);
		}

		bool InputFieldFocusActive(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline)
		{
			if(Ctx.m_pUi == nullptr || pInput == nullptr)
				return false;

			bool Active = pInput->IsActive() || Ctx.m_pUi->IsActiveItem(pInput);
			const bool MousePressed = Ctx.m_pUi->MouseButton(0) || Ctx.m_pUi->MouseButton(1);
			if(!Multiline && MousePressed && !Ctx.m_pUi->MouseHovered(&Rect))
				Active = false;
			if(!Active && Ctx.m_pUi->HotItem() == pInput && Ctx.m_pUi->MouseButton(0))
				Active = true;
			if(!Multiline && (Ctx.m_pUi->Input()->KeyPress(KEY_RETURN) || Ctx.m_pUi->Input()->KeyPress(KEY_KP_ENTER)))
				Active = false;
			return Active;
		}

		void DrawInputFieldIcon(const IUiContext &Ctx, const CUIRect &Rect, const char *pIcon, const ColorRGBA &Color, const int QmIcon = -1, const void *pAnimationId = nullptr)
		{
			const bool HasQmIcon = QmIcon >= 0 && QmIcon < static_cast<int>(EQmIcon::COUNT);
			if((pIcon == nullptr && !HasQmIcon) || Rect.w <= 0.0f || Rect.h <= 0.0f)
				return;
			const float IconSide = minimum(Rect.w, Rect.h) * 0.58f;
			const bool IsEyeMorphIcon = QmIcon == static_cast<int>(EQmIcon::EYE) || QmIcon == static_cast<int>(EQmIcon::EYE_OFF);
			if(IsEyeMorphIcon && pAnimationId != nullptr && Ctx.m_pAnim != nullptr && Ctx.m_pUi != nullptr && g_Config.m_QmUiMotionLevel > 0 && !Ctx.m_pUi->AnimationsBlocked())
			{
				const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0xE1E0A11ull, reinterpret_cast<uint64_t>(pAnimationId));
				const float Target = QmIcon == static_cast<int>(EQmIcon::EYE_OFF) ? 1.0f : 0.0f;
				const float Progress = std::clamp(ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::SCALE, Target, ui_token::motion::TOGGLE, 2), 0.0f, 1.0f);
				if(Ctx.m_pAnim->HasActiveAnimation(NodeKey, EUiAnimProperty::SCALE))
				{
					const CUIRect IconRect{Rect.x + (Rect.w - IconSide) * 0.5f, Rect.y + (Rect.h - IconSide) * 0.5f, IconSide, IconSide};
					Ctx.m_pUi->DrawQmIcon(IconRect, EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Color.WithMultipliedAlpha(1.0f - Progress));
					Ctx.m_pUi->DrawQmIcon(IconRect, EQmIcon::EYE_OFF, FontIcons::FONT_ICON_EYE_SLASH, Color.WithMultipliedAlpha(Progress));
					return;
				}
			}
			if(pIcon == nullptr)
				return;
			ITextRender *pTextRender = Ctx.m_pUi->TextRender();
			const ColorRGBA PreviousColor = pTextRender->GetTextColor();
			const ColorRGBA PreviousOutlineColor = pTextRender->GetTextOutlineColor();
			const ColorRGBA PreviousSelectionColor = pTextRender->GetTextSelectionColor();
			const unsigned PreviousFlags = pTextRender->GetRenderFlags();
			const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
			pTextRender->TextColor(Color);
			pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
			pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			const float FallbackFontSize = HasQmIcon ? IconSide * 0.8f : std::min(Rect.w, Rect.h) * 0.65f;
			Ctx.m_pUi->DoLabel(&Rect, pIcon, FallbackFontSize, TEXTALIGN_MC);
			pTextRender->SetRenderFlags(PreviousFlags);
			pTextRender->SetFontPreset(PreviousPreset);
			pTextRender->TextOutlineColor(PreviousOutlineColor);
			pTextRender->TextSelectionColor(PreviousSelectionColor);
			pTextRender->TextColor(PreviousColor);
		}

	} // namespace

	SInputFieldResult InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const SInputFieldOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || pInput == nullptr)
			return {};
		Ctx.m_pUi->DoConfigTooltip(pInput, &Rect, pInput->GetString());

		const bool WasActive = pInput->IsActive();
		const bool WasEmpty = pInput->IsEmpty();
		const bool Search = Options.m_Mode == EInputFieldMode::SEARCH;
		const bool HasIcon = Options.m_pLeadingIcon != nullptr || Search;
		const bool HasTrailingAction = Options.m_pTrailingActionId != nullptr && Options.m_pTrailingActionIcon != nullptr;
		const bool InlineTrailingText = Options.m_InlineTrailingText && Options.m_pTrailingText != nullptr && Options.m_pTrailingText[0] != '\0';
		const float TrailingWidth = HasTrailingAction ? std::max(Options.m_TrailingWidth, Rect.h) : Options.m_TrailingWidth;
		const SInputFieldLayout Layout = ResolveInputFieldLayout(Rect, HasIcon, Options.m_Clearable, Ctx.m_UiScale, InlineTrailingText ? 0.0f : TrailingWidth);
		const float FontSize = std::min(Options.m_FontSize, Layout.m_ContentRect.h * CUi::ms_FontmodHeight * 0.8f);
		CUIRect TextRect = Layout.m_ContentRect;
		CUIRect TrailingRect = Layout.m_TrailingRect;
		if(InlineTrailingText)
		{
			const char *pDisplayText = pInput->GetDisplayedString();
			const float TextWidth = Ctx.m_pUi->TextRender()->TextWidth(FontSize, pDisplayText);
			const float TrailingTextWidth = Ctx.m_pUi->TextRender()->TextWidth(FontSize * 0.82f, Options.m_pTrailingText);
			const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout(Layout.m_ContentRect, TextWidth, TrailingTextWidth, Ctx.m_UiScale);
			TextRect = InlineLayout.m_TextRect;
			TrailingRect = InlineLayout.m_TrailingRect;
		}
		const int TextAlign = InlineTrailingText ? TEXTALIGN_MR : ResolveInputFieldTextAlign(Options);
		const bool RenderOnly = Ctx.m_pUi->RenderOnly();
		const bool ProcessInput = Options.m_ProcessInput && !RenderOnly;
		const ColorRGBA PlateColor = ResolveConfiguredInputSurface(Options.m_ProcessInput);
		CUiScopedSurfaceText SurfaceText(Ctx.m_pUi->TextRender(), PlateColor);
		// 激活态边框单层绘制：颜色与粗细随聚焦动画在常规边框与强调色环之间过渡，
		// 不再叠加第二层描边——半透明强调环叠在常规边框上会让灰边透出（叠色）。
		float FocusAlpha = 0.0f;
		if(ProcessInput && Ctx.m_pAnim != nullptr)
		{
			const bool FocusActive = InputFieldFocusActive(Ctx, pInput, Layout.m_ShellRect, Options.m_Mode == EInputFieldMode::MULTILINE);
			FocusAlpha = AnimateStateValue(Ctx, pInput, EUiAnimProperty::ALPHA, FocusActive ? 1.0f : 0.0f, ui_curve::DECELERATE);
		}
		if(!Ctx.m_pUi->MeasurementOnly() && FocusAlpha > 0.01f)
		{
			const SUiTheme Theme = ThemeFor(Ctx);
			ColorRGBA NormalBorder = Theme.m_Border;
			NormalBorder.a = std::max(NormalBorder.a, 0.24f);
			const float T = FocusAlpha;
			const ColorRGBA FocusBorder{
				NormalBorder.r + (Theme.m_FocusRing.r - NormalBorder.r) * T,
				NormalBorder.g + (Theme.m_FocusRing.g - NormalBorder.g) * T,
				NormalBorder.b + (Theme.m_FocusRing.b - NormalBorder.b) * T,
				NormalBorder.a + (Theme.m_FocusRing.a - NormalBorder.a) * T};
			const float BorderWidth = 1.0f + T * (Theme.m_FocusRingWidth - 1.0f);
			DrawTextFieldShell(Ctx, Layout.m_ShellRect, PlateColor, Options.m_Corners, ui_token::radius::BASE, &FocusBorder, BorderWidth);
		}
		else if(!Ctx.m_pUi->MeasurementOnly())
		{
			DrawTextFieldShell(Ctx, Layout.m_ShellRect, PlateColor, Options.m_Corners, ui_token::radius::BASE);
		}
		const char *pPreviousPlaceholder = pInput->GetEmptyText();
		pInput->SetEmptyText(Options.m_pPlaceholder != nullptr ? Options.m_pPlaceholder : (Search ? Localize("Search") : nullptr));
		if(!Options.m_ProcessInput && !RenderOnly)
			pInput->Deactivate();

		if(ProcessInput && Options.m_SearchHotkeyEnabled && Search && Ctx.m_pUi->Input()->ModifierIsPressed() && Ctx.m_pUi->Input()->KeyPress(KEY_F))
		{
			Ctx.m_pUi->SetActiveItem(pInput);
			pInput->SelectAll();
		}

		const ColorRGBA InputIconColor = ResolveUiSurfaceIconColor(PlateColor, Ctx.m_pUi->TextRender()->GetTextColor());
		const char *pLeadingIcon = Options.m_pLeadingIcon != nullptr ? Options.m_pLeadingIcon : (Search ? FontIcons::FONT_ICON_MAGNIFYING_GLASS : nullptr);
		const int LeadingQmIcon = Options.m_LeadingQmIcon >= 0 ? Options.m_LeadingQmIcon : (Search ? static_cast<int>(EQmIcon::SEARCH) : -1);
		if(!Ctx.m_pUi->MeasurementOnly())
			DrawInputFieldIcon(Ctx, Layout.m_IconRect, pLeadingIcon, InputIconColor, LeadingQmIcon);
		CUi::SEditBoxRenderOptions RenderOptions;
		RenderOptions.m_DrawBackground = false;
		RenderOptions.m_ProcessInput = ProcessInput;
		CUIRect InputHitRect = Layout.m_ShellRect;
		if(Options.m_Clearable && Layout.m_ClearRect.w > 0.0f)
			InputHitRect.VSplitRight(Layout.m_ClearRect.w, &InputHitRect, nullptr);
		if(HasTrailingAction && TrailingRect.w > 0.0f)
			InputHitRect.VSplitRight(TrailingRect.w, &InputHitRect, nullptr);
		RenderOptions.m_pHitRect = &InputHitRect;
		bool Changed = false;
		if(!pInput->IsHidden() || !Ctx.m_pUi->MeasurementOnly())
		{
			const CQmCardLabelHintScope PreserveInput(Ctx.m_pUi, !RenderOnly && Ctx.m_pUi->CardLabelHintsEnabled());
			if(Options.m_Mode == EInputFieldMode::MULTILINE)
				Changed = Ctx.m_pUi->DoEditBoxMultiLine(pInput, &TextRect, FontSize, Options.m_LineSpacing, TextAlign, RenderOptions);
			else
				Changed = Ctx.m_pUi->DoEditBox(pInput, &TextRect, FontSize, Options.m_Corners, {}, TextAlign, RenderOptions);
		}
	// 单行输入：Enter = 确认并退出编辑态（多行保留换行语义）。此前没有任何
	// 路径在回车时失焦，Result.m_Submitted（依赖 Deactivated+SubmitPressed）
	// 永远不成立——数值框的提交分支随之失效，输入后一直停在编辑态。
	// IME 组词中的回车是候选确认，不退出编辑态（见 QmEditorTextInput 合同）。
	// 必须走 ReleaseActiveTextInput 而非 Deactivate+SetActiveItem(nullptr)：
	// SetActiveItem 对空指针不清 m_pLastActiveItem，下一帧 DoEditBox 会凭
	// m_pLastActiveItem 重新 Activate，输入框复活（实测回归，见
	// QmNumericFieldEnter.EnterDeactivatesInputAndSubmits）。
	if(ProcessInput && Options.m_Mode != EInputFieldMode::MULTILINE && pInput->IsActive() && !Ctx.m_pUi->Input()->HasComposition() &&
		(Ctx.m_pUi->Input()->KeyPress(KEY_RETURN) || Ctx.m_pUi->Input()->KeyPress(KEY_KP_ENTER)))
	{
		Ctx.m_pUi->ReleaseActiveTextInput(pInput);
	}
		if(RenderOnly)
			pInput->SetEmptyText(pPreviousPlaceholder);

		if(Options.m_Clearable && !Ctx.m_pUi->MeasurementOnly())
		{
			const CUIRect &ClearRect = Layout.m_ClearRect;
			SButtonSurfaceOptions ActionOptions;
			ActionOptions.m_Role = EUiButtonRole::ICON;
			ActionOptions.m_TransparentInactive = true;
			ActionOptions.m_Corners = IGraphics::CORNER_R;
			DrawButtonSurface(Ctx, pInput->GetClearButtonId(), ClearRect, ActionOptions);
			DrawInputFieldIcon(Ctx, ClearRect, FontIcons::FONT_ICON_XMARK, InputIconColor, static_cast<int>(EQmIcon::CLOSE));
			if(ProcessInput && Ctx.m_pUi->DoButtonLogic(pInput->GetClearButtonId(), 0, &ClearRect, BUTTONFLAG_LEFT))
			{
				pInput->Clear();
				Ctx.m_pUi->SetActiveItem(pInput);
				Changed = true;
			}
		}
		bool TrailingAction = false;
		if(HasTrailingAction && TrailingRect.w > 0.0f && !Ctx.m_pUi->MeasurementOnly())
		{
			SButtonSurfaceOptions ActionOptions;
			ActionOptions.m_Role = EUiButtonRole::ICON;
			ActionOptions.m_TransparentInactive = true;
			ActionOptions.m_Corners = Options.m_Clearable ? IGraphics::CORNER_NONE : IGraphics::CORNER_R;
			DrawButtonSurface(Ctx, Options.m_pTrailingActionId, TrailingRect, ActionOptions);
			DrawInputFieldIcon(Ctx, TrailingRect, Options.m_pTrailingActionIcon, InputIconColor, Options.m_TrailingActionQmIcon);
			TrailingAction = ProcessInput && Ctx.m_pUi->DoButtonLogic(Options.m_pTrailingActionId, 0, &TrailingRect, BUTTONFLAG_LEFT) != 0;
		}
		if(Options.m_pTrailingText != nullptr && TrailingRect.w > 0.0f)
			Ctx.m_pUi->DoLabel(&TrailingRect, Options.m_pTrailingText, FontSize * 0.82f, TEXTALIGN_MC);

		if(!ProcessInput)
			return {};
		SInputFieldResult Result = BuildInputFieldResult(Ctx, pInput, Changed, WasActive, WasEmpty, Options.m_Clearable);
		Result.m_TrailingAction = TrailingAction;
		return Result;
	}
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder, float FontSize)
	{
		SInputFieldOptions Options;
		Options.m_pPlaceholder = pPlaceholder;
		Options.m_FontSize = FontSize;
		return InputField(Ctx, pInput, Rect, Options).m_Changed;
	}

	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, float FontSize, bool SearchHotkeyEnabled)
	{
		SInputFieldOptions Options;
		Options.m_Mode = EInputFieldMode::SEARCH;
		Options.m_Clearable = true;
		Options.m_SearchHotkeyEnabled = SearchHotkeyEnabled;
		Options.m_FontSize = FontSize;
		return InputField(Ctx, pInput, Rect, Options).m_Changed;
	}

	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder, float FontSize, const char *pIcon, bool Clearable)
	{
		SInputFieldOptions Options;
		Options.m_pPlaceholder = pPlaceholder;
		Options.m_pLeadingIcon = pIcon;
		Options.m_Clearable = Clearable;
		Options.m_FontSize = FontSize;
		return InputField(Ctx, pInput, Rect, Options).m_Changed;
	}
	bool InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const STextFieldOptions &Options)
	{
		SInputFieldOptions InputOptions;
		InputOptions.m_pPlaceholder = Options.m_pPlaceholder;
		InputOptions.m_FontSize = Options.m_FontSize;
		InputOptions.m_Corners = Options.m_Corners;
		InputOptions.m_TextAlign = Options.m_TextAlign;
		return InputField(Ctx, pInput, Rect, InputOptions).m_Changed;
	}
	void ReadOnlyTextField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const char *pPlaceholder, float FontSize)
	{
		if(Ctx.m_pUi == nullptr || pInput == nullptr)
			return;

		// 只读字段前后台共用文字引擎，保留占位字颜色、内边距和裁剪。
		const char *pPreviousPlaceholder = pInput->GetEmptyText();
		pInput->SetEmptyText(pPlaceholder);
		CUi::SEditBoxRenderOptions RenderOptions;
		RenderOptions.m_DrawBackground = false;
		RenderOptions.m_ProcessInput = false;
		Ctx.m_pUi->DoEditBox(pInput, &Rect, FontSize, IGraphics::CORNER_ALL, {}, TEXTALIGN_ML, RenderOptions);
		if(Ctx.m_pUi->RenderOnly())
			pInput->SetEmptyText(pPreviousPlaceholder);
		else
			DrawTextFieldFocusBorder(Ctx, pInput, Rect);
	}

	SInputFieldResult IntegerField(const IUiContext &Ctx, CLineInputNumber *pInput, int *pValue, int Min, int Max, const CUIRect &Rect, const STextFieldOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || pInput == nullptr || pValue == nullptr)
			return {};
		SInputFieldOptions FieldOptions;
		FieldOptions.m_pPlaceholder = Options.m_pPlaceholder;
		FieldOptions.m_FontSize = Options.m_FontSize;
		FieldOptions.m_Corners = Options.m_Corners;
		FieldOptions.m_TextAlign = Options.m_TextAlign;
		if(Ctx.m_pUi->RenderOnly())
		{
			CLineInputNumber PreviewInput;
			PreviewInput.SetInteger(std::clamp(*pValue, Min, Max));
			// 测量使用提交值；可见后台保留未提交文本，包括全选删除后的空文本。
			return InputField(Ctx, Ctx.m_pUi->MeasurementOnly() ? &PreviewInput : pInput, Rect, FieldOptions);
		}

		const int ClampedValue = std::clamp(*pValue, Min, Max);
		if(ClampedValue != *pValue)
			*pValue = ClampedValue;

		if(!pInput->IsActive() && (pInput->IsEmpty() || pInput->GetInteger() != *pValue))
		{
			pInput->SetInteger(*pValue);
			pInput->SelectAll();
		}

		const SInputFieldResult Result = InputField(Ctx, pInput, Rect, FieldOptions);
		if(!pInput->IsActive())
		{
			if(pInput->GetLength() > 0 && (Result.m_Changed || Result.m_Deactivated || pInput->GetInteger() != *pValue))
				*pValue = std::clamp(pInput->GetInteger(), Min, Max);
			pInput->SetInteger(*pValue);
			pInput->SelectAll();
		}

		return Result;
	}

	CUIRect SliderInputFieldLabelRect(const CUIRect &Rect, bool HasLabel, unsigned Flags)
	{
		if(!HasLabel)
			return {};

		CUIRect Label;
		if(Flags & CUi::SCROLLBAR_OPTION_MULTILINE)
		{
			CUIRect Header;
			Rect.HSplitMid(&Header, nullptr);
			Header.VSplitLeft(Header.w * 0.68f, &Label, nullptr);
			return Label;
		}

		return ResolveSettingsControlRowLayout(Rect, ResolveSettingsContentMetrics(Rect.w)).m_LabelRect;
	}

	bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || pState == nullptr || pValue == nullptr || Max <= Min)
			return false;
		Ctx.m_pUi->DoConfigTooltip(pId, &Rect, pValue);

		CLineInputNumber *pInput = &pState->m_Input;
		const IScrollbarScale *pScale = Options.m_pScale != nullptr ? Options.m_pScale : &CUi::ms_LinearScrollbarScale;
		const bool Infinite = Options.m_Flags & CUi::SCROLLBAR_OPTION_INFINITE;
		const bool NoClampValue = Options.m_Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;
		const bool MultiLine = Options.m_Flags & CUi::SCROLLBAR_OPTION_MULTILINE;
		const int Multiplier = std::max(1, Options.m_ValueMultiplier);
		const int ValueStep = std::max(1, Options.m_ValueStep);
		const int SliderMin = SliderInputStoredMinimum(Min, Multiplier);
		const int SliderMax = SliderInputStoredMaximum(Max, Multiplier) + (Infinite ? 1 : 0);
		const int FiniteSliderMax = SliderInputStoredMaximum(Max, Multiplier);
		const int InputMin = Options.m_InputMin >= 0 ? Options.m_InputMin : SliderMin;
		const int InputMax = Options.m_InputMax >= 0 ? std::max(InputMin, Options.m_InputMax) : FiniteSliderMax;
		const bool HasLabel = Options.m_pLabel != nullptr && Options.m_pLabel[0] != '\0';
		const bool RenderOnly = Ctx.m_pUi->RenderOnly();
		// 布局：label | scrollbar | input | suffix
		CUIRect Label{}, Controls{}, ValueRect{}, ScrollBar{}, InputField{};
		if(MultiLine)
		{
			CUIRect Header;
			Rect.HSplitMid(&Header, &ScrollBar);
			Header.VSplitLeft(Header.w * 0.68f, &Label, &ValueRect);
		}
		else if(HasLabel)
		{
			SplitSettingsControlRow(Rect, ResolveSettingsContentMetrics(Rect.w), &Label, &Controls);
		}
		else
		{
			Controls = Rect;
		}

		const bool HasSuffix = Options.m_pSuffix != nullptr && Options.m_pSuffix[0] != '\0';
		const float SuffixWidth = HasSuffix ? std::max(18.0f, Options.m_TrailingWidth) : 0.0f;
		const float ValueWidth = ResolveSettingsControlWidth(MultiLine ? ValueRect.w : Controls.w, ResolveSettingsContentMetrics(Rect.w), ESettingsControlWidth::NUMBER, SuffixWidth);
		const bool HasSlider = MultiLine || Controls.w > ValueWidth + 42.0f;
		if(!MultiLine && HasSlider)
			Controls.VSplitRight(ValueWidth, &ScrollBar, &InputField);
		else if(!MultiLine)
			InputField = Controls;
		if(InputField.w <= 0.0f)
			InputField = ValueRect;
		InputField.VMargin(std::min(5.0f, ValueWidth * 0.1f), &InputField);

		if(HasLabel)
		{
			const float LabelFontSize = MultiLine ? std::min(Options.m_FontSize, Label.h * CUi::ms_FontmodHeight * 0.8f) : Options.m_FontSize;
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w;
			Props.m_DisallowNewline = true;
			Props.m_StopAtEnd = true;
			Props.m_MinimumFontSize = 6.0f;
			if(Options.m_pLabelElement != nullptr)
			{
				Ctx.m_pUi->DoLabelStreamed(*Options.m_pLabelElement->Rect(0), &Label, Options.m_pLabel, LabelFontSize, Options.m_LabelAlign, Props, -1, nullptr, !Ctx.m_pUi->MeasurementOnly());
			}
			else
				Ctx.m_pUi->DoLabel(&Label, Options.m_pLabel, LabelFontSize, Options.m_LabelAlign, Props);
		}
		if(RenderOnly)
		{
			const int PreviewStoredValue = !Ctx.m_pUi->MeasurementOnly() && pState->m_HasPendingValue ? pState->m_PendingStoredValue : *pValue;
			const bool IsInfinite = SliderInputIsInfiniteValue(PreviewStoredValue, Infinite);
			int StoredValue = PreviewStoredValue;
			if(!IsInfinite && !NoClampValue)
				StoredValue = std::clamp(StoredValue, SliderMin, SliderInputStoredMaximum(Max, Multiplier));
			const int DisplayValue = IsInfinite ? Max : SliderInputDisplayValue(StoredValue, Multiplier);
			char aValue[64];
			if(IsInfinite)
				str_copy(aValue, "\xe2\x88\x9e");
			else if(Options.m_pMaxText != nullptr && DisplayValue == Max)
				str_copy(aValue, Options.m_pMaxText);
			else
				str_format(aValue, sizeof(aValue), "%d", DisplayValue);
			if(!Ctx.m_pUi->MeasurementOnly())
			{
				if(HasSlider)
					RenderHorizontalSlider(Ctx, pId, ScrollBar, IsInfinite ? 1.0f : std::clamp(pScale->ToRelative(StoredValue, SliderMin, FiniteSliderMax), 0.0f, 1.0f) * (Infinite ? NumericFieldInfiniteEndpointStart(ScrollBar.w, Ctx.m_UiScale) : 1.0f));
			}
			// 数字不能在后台改用 DoLabel：输入框的内边距、字体缩放、后缀排版
			// 和未提交文本都必须沿前台的同一条渲染路径。
			CLineInputNumber PreviewInput;
			PreviewInput.Set(aValue);
			char aNumericValue[32];
			str_format(aNumericValue, sizeof(aNumericValue), "%d", DisplayValue);
			const bool ShowMaxText = !pInput->IsActive() && !IsInfinite && Options.m_pMaxText != nullptr && DisplayValue == Max && str_comp(pInput->GetString(), aNumericValue) == 0;
			CLineInput *pVisibleInput = !Ctx.m_pUi->MeasurementOnly() && pState->m_HasSyncedValue && !ShowMaxText ? pInput : &PreviewInput;
			SInputFieldOptions FieldOptions;
			FieldOptions.m_FontSize = std::min(Options.m_FontSize, InputField.h * CUi::ms_FontmodHeight * 0.8f);
			FieldOptions.m_TextAlign = TEXTALIGN_MC;
			FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;
			FieldOptions.m_InlineTrailingText = HasSuffix;
			FieldOptions.m_ProcessInput = !Ctx.m_pUi->CheckActiveItem(pId);
			ui_widget::InputField(Ctx, pVisibleInput, InputField, FieldOptions);
			return false;
		}

		if(Ctx.m_pTooltips != nullptr)
			Ctx.m_pTooltips->DoSettingsToolTipForConfig(pId, &Rect, pValue, HasLabel ? &Label : nullptr);

		bool Changed = false;
		const int Increment = std::max(ValueStep, (SliderMax - SliderMin) / 35 / ValueStep * ValueStep);
		if(!RenderOnly && ValueStep > 1 && (!Infinite || *pValue != 0))
			*pValue = QuantizeNumericFieldStoredValue(*pValue, InputMin, InputMax, ValueStep);
		const bool WheelEligible = !RenderOnly && Ctx.m_pUi->Input()->ModifierIsPressed() && Ctx.m_pUi->MouseInside(&Rect);
		Ctx.m_pUi->RegisterWheelOwner(pState, EUiWheelOwnerPriority::COMPOSITE_CONTROL, Rect, WheelEligible);
		float WheelDelta = 0.0f;
		if(Ctx.m_pUi->TryConsumeWheel(pState, &WheelDelta))
		{
			const int CurrentValue = !Infinite || *pValue != 0 ? QuantizeNumericFieldStoredValue(*pValue, SliderMin, FiniteSliderMax, ValueStep) : *pValue;
			const int NewValue = SliderInputWheelStoredValue(CurrentValue, SliderMin, SliderMax, Infinite, WheelDelta > 0.0f ? Increment : -Increment);
			Changed = NewValue != *pValue;
			*pValue = NewValue;
		}

		if(!pState->m_HasSyncedValue || (!pState->m_HasPendingValue && pState->m_LastSyncedStoredValue != *pValue))
		{
			pState->m_LastSyncedStoredValue = *pValue;
			pState->m_HasSyncedValue = true;
		}

		const int PreviewStoredValue = pState->m_HasPendingValue ? pState->m_PendingStoredValue : *pValue;
		const bool IsInfinite = SliderInputIsInfiniteValue(PreviewStoredValue, Infinite);
		int StoredValue = PreviewStoredValue;
		if(!IsInfinite && !NoClampValue)
			StoredValue = std::clamp(StoredValue, SliderMin, SliderInputStoredMaximum(Max, Multiplier));

		int DisplayValue = SliderInputDisplayValue(StoredValue, Multiplier);
		if(IsInfinite)
			DisplayValue = Max;

		if(!pInput->IsActive() && IsInfinite)
		{
			if(str_comp(pInput->GetString(), "\xe2\x88\x9e") != 0)
				pInput->Set("\xe2\x88\x9e");
		}
		else if(!pInput->IsActive() && (pInput->IsEmpty() || pInput->GetInteger() != DisplayValue))
		{
			pInput->SetInteger(DisplayValue);
			pInput->SelectAll();
		}

		// 无限值使用末端专属区域，避免与有限最大值共享不足一个像素的命中区。
		int SliderValue = IsInfinite ? SliderMax : StoredValue;
		const float InfiniteEndpointStart = Infinite ? NumericFieldInfiniteEndpointStart(ScrollBar.w, Ctx.m_UiScale) : 1.0f;
		const float Normalized = IsInfinite ? 1.0f : std::clamp(pScale->ToRelative(SliderValue, SliderMin, FiniteSliderMax), 0.0f, 1.0f) * InfiniteEndpointStart;
		const bool SliderWasActive = pState->m_SliderWasActive;
		const bool SliderOwnsInput = Ctx.m_pUi->CheckActiveItem(pId);
		bool SliderActive = SliderOwnsInput;
		if(HasSlider && !RenderOnly && (!pInput->IsActive() || SliderOwnsInput))
		{
			const float NewNormalized = Ctx.m_pUi->DoScrollbarH(pId, &ScrollBar, Normalized);
			SliderActive = Ctx.m_pUi->CheckActiveItem(pId);
			const bool SliderReleased = SliderWasActive && !SliderActive;
			const bool NewInfiniteValue = Infinite && NewNormalized >= (1.0f + InfiniteEndpointStart) * 0.5f;
			const int NewSliderValue = NewInfiniteValue ? SliderMax : pScale->ToAbsolute(NewNormalized / InfiniteEndpointStart, SliderMin, FiniteSliderMax);
			if(NewSliderValue != SliderValue || SliderReleased)
			{
				int CandidateStored = QuantizeNumericFieldStoredValue(NewSliderValue, SliderMin, FiniteSliderMax, ValueStep);
				if(Infinite && NewSliderValue == SliderMax)
					CandidateStored = 0;

				if(NoClampValue && ((CandidateStored <= SliderMin && *pValue < SliderMin) || (CandidateStored >= SliderInputStoredMaximum(Max, Multiplier) && *pValue > SliderInputStoredMaximum(Max, Multiplier))))
				{
					// 保留越界值
					DisplayValue = SliderInputIsInfiniteValue(*pValue, Infinite) ? Max : SliderInputDisplayValue(*pValue, Multiplier);
					pInput->SetInteger(DisplayValue);
					pInput->SelectAll();
					pState->m_SliderWasActive = SliderActive;
				}
				else
				{
					Changed = UpdateNumericFieldSliderCommit(*pState, Options.m_CommitPolicy, SliderActive, SliderReleased, CandidateStored, pValue) || Changed;
					const int VisibleStoredValue = pState->m_HasPendingValue ? pState->m_PendingStoredValue : *pValue;
					DisplayValue = SliderInputIsInfiniteValue(VisibleStoredValue, Infinite) ? Max : SliderInputDisplayValue(VisibleStoredValue, Multiplier);
					pInput->SetInteger(DisplayValue);
					pInput->SelectAll();
					pState->m_LastSyncedStoredValue = *pValue;
					pState->m_HasSyncedValue = true;
				}
			}
			else
				pState->m_SliderWasActive = SliderActive;
		}
		else if(HasSlider)
		{
			// 编辑数值时只禁用交互，仍复用全局滑条的当前主题样式。
			Ctx.m_pUi->RenderScrollbarH(pId, &ScrollBar, Normalized);
		}

		// 输入框：特殊值（♾️ 或 pMaxText）在非编辑状态下显示为文本
		const bool bShowMaxText = !pInput->IsActive() && !SliderInputIsInfiniteValue(*pValue, Infinite) && Options.m_pMaxText != nullptr && DisplayValue == Max;
		char aSavedInput[32];
		if(bShowMaxText)
		{
			str_copy(aSavedInput, pInput->GetString(), sizeof(aSavedInput));
			pInput->Set(Options.m_pMaxText);
		}

		SInputFieldOptions FieldOptions;
		const float FieldFontSize = std::min(Options.m_FontSize, InputField.h * CUi::ms_FontmodHeight * 0.8f);
		FieldOptions.m_FontSize = FieldFontSize;
		FieldOptions.m_TextAlign = TEXTALIGN_MC;
		FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;
		FieldOptions.m_InlineTrailingText = HasSuffix;
		FieldOptions.m_ProcessInput = !SliderActive;
		SInputFieldResult Result = ui_widget::InputField(Ctx, pInput, InputField, FieldOptions);

		if(bShowMaxText)
			pInput->Set(aSavedInput);

		if(!RenderOnly && (Result.m_Deactivated || Result.m_Submitted))
		{
			const bool ParsedInfinite = Infinite && NumericFieldTextIsInfinite(pInput->GetString());
			int Parsed = ParsedInfinite ? 0 : SliderInputStoredValue(pInput->GetInteger(), Multiplier);
			if(!ParsedInfinite && (Options.m_InputMin >= 0 || Options.m_InputMax >= 0))
			{
				Parsed = NumericFieldTextInputStoredValue(pInput->GetInteger(), Multiplier, InputMin, FiniteSliderMax, InputMax, false);
				Parsed = QuantizeNumericFieldStoredValue(Parsed, InputMin, InputMax, ValueStep);
			}
			else if(!ParsedInfinite && NoClampValue && ((Parsed <= SliderMin && *pValue < SliderMin) || (Parsed >= SliderInputStoredMaximum(Max, Multiplier) && *pValue > SliderInputStoredMaximum(Max, Multiplier))))
			{
				// 保留越界值
			}
			else if(!ParsedInfinite)
			{
				Parsed = QuantizeNumericFieldStoredValue(Parsed, SliderMin, FiniteSliderMax, ValueStep);
			}
			if(*pValue != Parsed)
			{
				*pValue = Parsed;
				Result.m_Changed = true;
				Changed = true;
			}
			pState->m_HasPendingValue = false;
			pState->m_LastSyncedStoredValue = *pValue;
			pState->m_HasSyncedValue = true;
			DisplayValue = SliderInputIsInfiniteValue(*pValue, Infinite) ? Max : SliderInputDisplayValue(*pValue, Multiplier);
			pInput->SetInteger(DisplayValue);
			pInput->SelectAll();
		}

		return Result.m_Changed || Changed;
	}

	void DrawToggle(const IUiContext &Ctx, const void *pId, bool Value, const CUIRect &Rect, bool Enabled, bool Animate, const CUIRect *pHitRect)
	{
		if(Ctx.m_pUi == nullptr || Ctx.m_pUi->DrawingSuppressed())
			return;
		Ctx.m_pUi->DoConfigTooltip(pId, pHitRect != nullptr ? pHitRect : &Rect, pId);
		if(Ctx.m_pUi->MeasurementOnly())
			return;
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);
		const SUiTheme Theme = ThemeFor(Ctx);
		const ColorRGBA Backdrop = CUiScopedSurfaceText::CurrentSurface();
		const SUiToggleStyle Style = ResolveUiToggleStyle(Theme, ResolveConfiguredControlSurface(), Backdrop, Value, Enabled);
		ColorRGBA Track = Style.m_Track;
		float Progress = Value ? 1.0f : 0.0f;
		CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : Ctx.m_pUi->QmAnimationRuntime();
		if(Animate && Enabled && pAnim != nullptr && !Ctx.m_pUi->AnimationsBlocked())
		{
			const uint64_t TrackKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0xA5A5ull, reinterpret_cast<uint64_t>(pId));
			Track = ResolveUiAnimValueColor(*pAnim, TrackKey, Track, ui_token::motion::BTN_HOVER.m_DurationSec, ui_token::motion::BTN_HOVER.m_Easing);
			const uint64_t KnobKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0x5A5Aull, reinterpret_cast<uint64_t>(pId));
			Progress = ResolveUiAnimSpringValue(*pAnim, KnobKey, EUiAnimProperty::COLOR_MIX, Progress, ui_token::motion::TOGGLE);
		}
		const SToggleLayout Layout = ResolveToggleLayout(Rect, Progress);
		const bool Hovered = Enabled && Ctx.m_pUi->Enabled() && Ctx.m_pUi->VisualMouseHovered(pHitRect != nullptr ? pHitRect : &Rect);
		const bool Pressed = Hovered && Ctx.m_pUi->CheckActiveItem(pId) && Ctx.m_pUi->MouseButton(0);
		const auto Feedback = ResolveUiToggleFeedbackStyle(Style, Track, Backdrop, Enabled, Hovered, Pressed);
		DrawControlSurface(Ctx, Layout.m_Track, Feedback.m_Track, Feedback.m_Border, ui_token::radius::PILL, ui_token::feedback::ICON_BORDER_WIDTH);
		DrawControlSurface(Ctx, Layout.m_Knob, Feedback.m_Knob, ColorRGBA(), ui_token::radius::PILL);
	}

	void DrawMarkedControl(const IUiContext &Ctx, const void *pId, const char *pMark, const CUIRect &Rect, const CUIRect *pHitRect)
	{
		if(Ctx.m_pUi == nullptr)
			return;
		SButtonSurfaceOptions Options;
		Options.m_Selected = pMark != nullptr && pMark[0] != '\0';
		Options.m_Radius = ui_token::radius::TIGHT;
		Options.m_pHitRect = pHitRect;
		const ColorRGBA Fill = DrawButtonSurface(Ctx, pId, Rect, Options);
		CUiScopedSurfaceText SurfaceText(Ctx.m_pUi->TextRender(), Fill);
		if(pMark != nullptr && pMark[0] != '\0')
			Ctx.m_pUi->DoLabel(&Rect, pMark, Rect.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	}

	bool Toggle(const IUiContext &Ctx, const void *pId, bool *pValue, const CUIRect &Rect, bool ProcessInput, bool Animate)
	{
		if(Ctx.m_pUi == nullptr || pValue == nullptr)
			return false;
		const bool Clicked = !Ctx.m_pUi->RenderOnly() && ProcessInput && Ctx.m_pUi->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
		if(Clicked)
			*pValue = !*pValue;
		DrawToggle(Ctx, pId, *pValue, Rect, true, Animate);
		return Clicked;
	}

	void DrawScrollbarHandle(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, bool Enabled, const ColorRGBA *pColorInner, const ColorRGBA *pHandleColorOverride)
	{
		if(Ctx.m_pUi == nullptr || Ctx.m_pUi->MeasurementOnly())
			return;
		// 按压态只看活动项（拖动中热项可能被滚动区域接管）；悬浮态用视觉
		// 语义（内容 DRAW 回放放行），输入语义的 MouseHovered 不用于反馈。
		const bool Pressed = Enabled && Ctx.m_pUi->CheckActiveItem(pId);
		const bool Hot = Pressed || (Enabled && Ctx.m_pUi->VisualHotItem(pId));
		SUiSliderStyle Style = ResolveUiSliderStyle(ThemeFor(Ctx), CUiScopedSurfaceText::CurrentSurface(), Hot, Pressed, Enabled);
		// 宿主显式指定的滑块色优先于对比度兜底，供弹窗与设置页保持同一强调色。
		if(pHandleColorOverride != nullptr)
			Style.m_Handle = *pHandleColorOverride;
		// 悬浮/拖动的几何反馈：滑块向外扩 1~2px，在暗/亮表面上都可直接感知
		//（颜色推导反馈在高透明度卡片上不可靠，几何反馈不依赖底色）。
		CUIRect HandleRect = Rect;
		if(Hot)
			HandleRect.Margin(Pressed ? -2.0f : -1.0f, &HandleRect);
		// 滑块是前景反馈，背景透明度仅作用于轨道；保留强调色自身 alpha 和禁用态。
		DrawControlSurface(Ctx, HandleRect, Style.m_Handle, Style.m_Border, ui_token::radius::PILL, ui_token::feedback::ICON_BORDER_WIDTH);
		if(pColorInner != nullptr)
		{
			CUIRect Inner;
			HandleRect.Margin(std::min(2.0f, std::min(HandleRect.w, HandleRect.h) * 0.25f), &Inner);
			DrawControlSurface(Ctx, Inner, *pColorInner, ColorRGBA(), ui_token::radius::PILL);
		}
	}

	void RenderHorizontalSlider(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, float Current, const ColorRGBA *pColorInner)
	{
		if(Ctx.m_pUi == nullptr || Ctx.m_pUi->MeasurementOnly())
			return;
		const auto Layout = ResolveHorizontalSliderLayout(Rect, Current);
		const auto Style = ResolveUiSliderStyle(ThemeFor(Ctx), CUiScopedSurfaceText::CurrentSurface(), Ctx.m_pUi->VisualHotItem(pId), Ctx.m_pUi->CheckActiveItem(pId));
		DrawControlSurface(Ctx, Layout.m_Track, Ctx.m_pUi->ScaleControlAlpha(Style.m_Track), ColorRGBA(), ui_token::radius::PILL);
		DrawControlSurface(Ctx, Layout.m_Fill, Style.m_Fill, ColorRGBA(), ui_token::radius::PILL);
		DrawScrollbarHandle(Ctx, pId, Layout.m_Handle, true, pColorInner);
	}

	bool Slider(const IUiContext &Ctx, const void *pId, float *pValue, float Min, float Max, const CUIRect &Rect, const char *pSuffix)
	{
		if(Ctx.m_pUi == nullptr || pValue == nullptr || Max <= Min)
			return false;

		CUIRect Track, Label;
		Rect.VSplitRight(48.0f, &Track, &Label);
		Track.VSplitRight(ui_token::spacing::SM, &Track, nullptr);

		const float Normalized = std::clamp((*pValue - Min) / (Max - Min), 0.0f, 1.0f);
		if(Ctx.m_pUi->RenderOnly())
			RenderHorizontalSlider(Ctx, pId, Track, Normalized);
		const float NewNormalized = Ctx.m_pUi->RenderOnly() ? Normalized : Ctx.m_pUi->DoScrollbarH(pId, &Track, Normalized);
		const float NewValue = Min + NewNormalized * (Max - Min);
		const bool Changed = !Ctx.m_pUi->RenderOnly() && std::abs(NewValue - *pValue) > 1e-4f;
		if(!Ctx.m_pUi->RenderOnly())
			*pValue = NewValue;

		char aBuf[32];
		if(pSuffix != nullptr && pSuffix[0] != '\0')
			std::snprintf(aBuf, sizeof(aBuf), "%.2f%s", *pValue, pSuffix);
		else
			std::snprintf(aBuf, sizeof(aBuf), "%.2f", *pValue);
		Ctx.m_pUi->DoLabel(&Label, aBuf, ui_token::font::BODY, TEXTALIGN_MR);

		return Changed;
	}

} // namespace ui_widget
