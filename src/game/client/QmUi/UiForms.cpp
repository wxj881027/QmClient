// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiForms.h"

#include "UiFormLogic.h"
#include "UiMotion.h"
#include "UiSurface.h"
#include "UiTheme.h"

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/lineinput.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdio>

namespace ui_widget
{

	namespace
	{
		void DrawTextFieldFocusBorder(const IUiContext &Ctx, const CUIRect &Rect, float Alpha);
		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect);
		void DrawTextFieldFocusBorder(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline);
		void DrawTextFieldShell(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, int Corners, float Radius);
		bool InputFieldFocusActive(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, bool Multiline);

		SUiTheme ThemeFor(const IUiContext &Ctx)
		{
			return Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor);
		}

		SInputFieldResult BuildInputFieldResult(const IUiContext &Ctx, CLineInput *pInput, bool Changed, bool WasActive, bool WasEmpty, bool Clearable)
		{
			const bool SubmitPressed = Ctx.m_pUi != nullptr && (Ctx.m_pUi->Input()->KeyPress(KEY_RETURN) || Ctx.m_pUi->Input()->KeyPress(KEY_KP_ENTER));
			return ui_widget::BuildInputFieldResult(WasActive, pInput->IsActive(), Changed, SubmitPressed, WasEmpty, pInput->IsEmpty(), Clearable);
		}

		void DrawTextFieldShell(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const int Corners, const float Radius)
		{
			const SUiTheme Theme = ThemeFor(Ctx);
			ColorRGBA Border = Theme.m_Border;
			Border.a = std::max(Border.a, 0.24f);
			DrawRoundedSurface(Ctx, Rect, Ctx.m_pUi->ScaleBackgroundAlpha(Fill), Ctx.m_pUi->ScaleBackgroundAlpha(Border), Radius, 1.0f, Corners);
		}

		bool NumericFieldTextIsInfinite(const char *pText)
		{
			const char *pTrimmed = str_utf8_skip_whitespaces(pText);
			char aTrimmed[32];
			str_copy(aTrimmed, pTrimmed);
			str_utf8_trim_right(aTrimmed);
			return str_comp(aTrimmed, "∞") == 0 || str_comp_nocase(aTrimmed, "inf") == 0 || str_comp_nocase(aTrimmed, "infinite") == 0;
		}

		void DrawTextFieldPlate(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const STextFieldOptions &Options)
		{
			const SUiTheme &Theme = ThemeFor(Ctx);
			const bool Active = pInput->IsActive();
			const bool Hovered = Ctx.m_pUi->HotItem() == pInput;
			const ColorRGBA PlateColor = Hovered && !Active ? Theme.m_SurfaceHovered : Theme.m_InputSurface;
			DrawTextFieldShell(Ctx, Rect, PlateColor, Options.m_Corners, Options.m_CornerRadius);
			if(Ctx.m_pAnim == nullptr)
				return;

			const float TargetAlpha = pInput->IsActive() ? 1.0f : 0.0f;
			const float Alpha = AnimateStateValue(Ctx, pInput, EUiAnimProperty::ALPHA, TargetAlpha, ui_curve::DECELERATE);
			DrawTextFieldFocusBorder(Ctx, Rect, Alpha);
		}

		void DrawTextFieldFocusBorder(const IUiContext &Ctx, const CUIRect &Rect, float Alpha)
		{
			if(Alpha <= 0.01f)
				return;

			const SUiTheme &Theme = ThemeFor(Ctx);
			ColorRGBA RingColor = Theme.m_FocusRing;
			RingColor.a *= Alpha;
			DrawRoundedSurface(Ctx, Rect, Ctx.m_pUi->ScaleBackgroundAlpha(Theme.m_InputSurface), Ctx.m_pUi->ScaleBackgroundAlpha(RingColor), ui_token::radius::BASE + Theme.m_FocusRingWidth, Theme.m_FocusRingWidth);
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

		void DrawInputFieldIcon(const IUiContext &Ctx, const CUIRect &Rect, const char *pIcon, const ColorRGBA &Color, const int QmIcon = -1)
		{
			const bool HasQmIcon = QmIcon >= 0 && QmIcon < static_cast<int>(EQmIcon::COUNT);
			if((pIcon == nullptr && !HasQmIcon) || Rect.w <= 0.0f || Rect.h <= 0.0f)
				return;
			// eye 与 eye-slash 在 Phosphor 里是同一套眼眶几何（斜线只是额外伸出眼框），
			// 图集也按 em 框归一化，所以两者以**同一尺寸**绘制即可。历史上给 eye-off
			// 乘过 1.15 / 1.25 的补偿，反而让眼睛看起来一大一小。
			const bool IsEyeMorphIcon = QmIcon == static_cast<int>(EQmIcon::EYE) || QmIcon == static_cast<int>(EQmIcon::EYE_OFF);
			const float BaseIconSide = minimum(Rect.w, Rect.h) * 0.58f;
			const float IconSide = BaseIconSide;
			if(HasQmIcon && Ctx.m_pIconManager != nullptr)
			{
				const CUIRect IconRect{Rect.x + (Rect.w - IconSide) * 0.5f, Rect.y + (Rect.h - IconSide) * 0.5f, IconSide, IconSide};
				if(IsEyeMorphIcon && pAnimationId != nullptr && Ctx.m_pAnim != nullptr && Ctx.m_pUi != nullptr && g_Config.m_QmUiMotionLevel > 0)
				{
					const uint64_t MorphNodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0xE1E0A11ull, reinterpret_cast<uint64_t>(pAnimationId));
					const float MorphTarget = QmIcon == static_cast<int>(EQmIcon::EYE_OFF) ? 1.0f : 0.0f;
					const float MorphProgress = ResolveUiAnimSpringValue(*Ctx.m_pAnim, MorphNodeKey, EUiAnimProperty::SCALE, MorphTarget, ui_token::motion::TOGGLE, 2);
					const bool MorphActive = Ctx.m_pAnim->HasActiveAnimation(MorphNodeKey, EUiAnimProperty::SCALE);
					// 眼睛切换优先用几何 morph（带状四边形插值）：样例来自随包
					// Phosphor-Bold.ttf，几何映射与历史 SVG 逐轮廓对齐过（含 y 翻转，
					// 见 qmclient_scripts/qm_build_icon_morph.py），同一表面的内外轮廓
					// 共享一套刚体参数。无样例（非 Bold 字重）时退回交叉淡化。
					if(MorphActive)
					{
						// 优先走预烘焙 MSDF 关键帧：形变与图标同一条抗锯齿路径，
						// 不依赖 FSAA，也不会有几何直出的亚像素散点；
						// 图集没有关键帧（非 Bold 或旧数据）时退回几何 morph。
						if(Ctx.m_pIconManager->RenderMorphFrames(IconRect, Color, MorphProgress))
							return;
						if(RenderQmEyeMorph(Ctx.m_pUi->Graphics(), g_Config.m_QmUiIconWeight, IconRect, Color, MorphProgress))
							return;
						const float CrossProgress = std::clamp(MorphProgress, 0.0f, 1.0f);
						const float AlphaEye = Color.a * (1.0f - CrossProgress);
						const float AlphaOff = Color.a * CrossProgress;
						bool Drawn = false;
						if(AlphaEye > 0.01f)
							Drawn = Ctx.m_pIconManager->RenderIcon(EQmIcon::EYE, IconRect, ColorRGBA(Color.r, Color.g, Color.b, AlphaEye));
						if(AlphaOff > 0.01f)
							Drawn = Ctx.m_pIconManager->RenderIcon(EQmIcon::EYE_OFF, IconRect, ColorRGBA(Color.r, Color.g, Color.b, AlphaOff)) || Drawn;
						if(Drawn)
							return;
					}
				}
				if(!Ctx.m_pIconManager->PreferFontFallback() && Ctx.m_pIconManager->RenderIcon(static_cast<EQmIcon>(QmIcon), IconRect, Color))
					return;
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
			pTextRender->SetFontPreset(QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight) ? EFontPreset::ICON_FONT_BOLD : EFontPreset::ICON_FONT);
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
		if(Ctx.m_pUi->RenderOnly())
		{
			// 文本计划只收集稳定文本，不绘制输入框 chrome，也不修改输入、焦点或动画状态。
			const char *pPlaceholder = Options.m_pPlaceholder != nullptr ? Options.m_pPlaceholder : (Search ? Localize("Search") : nullptr);
			if(WasEmpty && pPlaceholder != nullptr)
				Ctx.m_pUi->DoLabel(&TextRect, pPlaceholder, FontSize, TextAlign);
			else if(!WasEmpty && !pInput->IsHidden())
				Ctx.m_pUi->DoLabel(&TextRect, pInput->GetString(), FontSize, TextAlign);
			if(Options.m_pTrailingText != nullptr && TrailingRect.w > 0.0f)
				Ctx.m_pUi->DoLabel(&TrailingRect, Options.m_pTrailingText, FontSize * 0.82f, TEXTALIGN_MC);
			return {};
		}
		const SUiTheme &Theme = ThemeFor(Ctx);
		const auto ActionHoverColor = [&Theme](float State) {
			return Theme.m_BorderHovered.WithAlpha(std::clamp(Theme.m_BorderHovered.a * (State - 1.0f), 0.0f, 1.0f));
		};
		const bool Hovered = Ctx.m_pUi->HotItem() == pInput;
		const ColorRGBA PlateColor = Hovered && !pInput->IsActive() ? Theme.m_SurfaceHovered : Theme.m_InputSurface;
		DrawTextFieldShell(Ctx, Layout.m_ShellRect, PlateColor, Options.m_Corners, ui_token::radius::BASE);
		pInput->SetEmptyText(Options.m_pPlaceholder != nullptr ? Options.m_pPlaceholder : (Search ? Localize("Search") : nullptr));
		if(!Options.m_ProcessInput)
		{
			pInput->Deactivate();
			Ctx.m_pUi->ClipEnable(&TextRect);
			pInput->Render(&TextRect, FontSize, TextAlign, false, -1.0f, 0.0f);
			Ctx.m_pUi->ClipDisable();
			if(Options.m_pTrailingText != nullptr && TrailingRect.w > 0.0f)
				Ctx.m_pUi->DoLabel(&TrailingRect, Options.m_pTrailingText, FontSize * 0.82f, TEXTALIGN_MC);
			return {};
		}

		if(Options.m_SearchHotkeyEnabled && Search && Ctx.m_pUi->Input()->ModifierIsPressed() && Ctx.m_pUi->Input()->KeyPress(KEY_F))
		{
			Ctx.m_pUi->SetActiveItem(pInput);
			pInput->SelectAll();
		}
		DrawTextFieldFocusBorder(Ctx, pInput, Layout.m_FocusRingRect, Options.m_Mode == EInputFieldMode::MULTILINE);

		const ColorRGBA InputIconColor = ConfiguredQmUiIconColor(SQmIconStyle().Color(EQmIconState::NORMAL));
		const char *pLeadingIcon = Options.m_pLeadingIcon != nullptr ? Options.m_pLeadingIcon : (Search ? FontIcons::FONT_ICON_MAGNIFYING_GLASS : nullptr);
		const int LeadingQmIcon = Options.m_LeadingQmIcon >= 0 ? Options.m_LeadingQmIcon : (Search ? static_cast<int>(EQmIcon::SEARCH) : -1);
		DrawInputFieldIcon(Ctx, Layout.m_IconRect, pLeadingIcon, InputIconColor, LeadingQmIcon);
		CUi::SEditBoxRenderOptions RenderOptions;
		RenderOptions.m_DrawBackground = false;
		CUIRect InputHitRect = Layout.m_ShellRect;
		if(Options.m_Clearable && Layout.m_ClearRect.w > 0.0f)
			InputHitRect.VSplitRight(Layout.m_ClearRect.w, &InputHitRect, nullptr);
		if(HasTrailingAction && TrailingRect.w > 0.0f)
			InputHitRect.VSplitRight(TrailingRect.w, &InputHitRect, nullptr);
		RenderOptions.m_pHitRect = &InputHitRect;
		bool Changed = false;
		if(Options.m_Mode == EInputFieldMode::MULTILINE)
			Changed = Ctx.m_pUi->DoEditBoxMultiLine(pInput, &TextRect, FontSize, Options.m_LineSpacing, TextAlign, RenderOptions);
		else
			Changed = Ctx.m_pUi->DoEditBox(pInput, &TextRect, FontSize, Options.m_Corners, {}, TextAlign, RenderOptions);

		if(Options.m_Clearable)
		{
			const CUIRect &ClearRect = Layout.m_ClearRect;
			const float ClearState = Ctx.m_pUi->ButtonColorMul(pInput->GetClearButtonId());
			if(ClearState > 1.0f)
				DrawRoundedSurface(Ctx, ClearRect, Ctx.m_pUi->ScaleBackgroundAlpha(ActionHoverColor(ClearState)), ColorRGBA(), ui_token::radius::BASE, 0.0f, IGraphics::CORNER_R);
			DrawInputFieldIcon(Ctx, ClearRect, FontIcons::FONT_ICON_XMARK, InputIconColor, static_cast<int>(EQmIcon::CLOSE));
			if(Ctx.m_pUi->DoButtonLogic(pInput->GetClearButtonId(), 0, &ClearRect, BUTTONFLAG_LEFT))
			{
				pInput->Clear();
				Ctx.m_pUi->SetActiveItem(pInput);
				Changed = true;
			}
		}
		bool TrailingAction = false;
		if(HasTrailingAction && TrailingRect.w > 0.0f)
		{
			const float ActionState = Ctx.m_pUi->ButtonColorMul(Options.m_pTrailingActionId);
			if(ActionState > 1.0f)
				DrawRoundedSurface(Ctx, TrailingRect, Ctx.m_pUi->ScaleBackgroundAlpha(ActionHoverColor(ActionState)), ColorRGBA(), ui_token::radius::BASE, 0.0f, Options.m_Clearable ? IGraphics::CORNER_NONE : IGraphics::CORNER_R);
			DrawInputFieldIcon(Ctx, TrailingRect, Options.m_pTrailingActionIcon, InputIconColor, Options.m_TrailingActionQmIcon);
			TrailingAction = Ctx.m_pUi->DoButtonLogic(Options.m_pTrailingActionId, 0, &TrailingRect, BUTTONFLAG_LEFT) != 0;
		}
		if(Options.m_pTrailingText != nullptr && TrailingRect.w > 0.0f)
			Ctx.m_pUi->DoLabel(&TrailingRect, Options.m_pTrailingText, FontSize * 0.82f, TEXTALIGN_MC);

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

		pInput->SetEmptyText(pPlaceholder);
		CUIRect TextRect = Rect;
		TextRect.VMargin(2.0f, &TextRect);
		Ctx.m_pUi->ClipEnable(&Rect);
		pInput->Render(&TextRect, FontSize, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ctx.m_pUi->ClipDisable();
		DrawTextFieldFocusBorder(Ctx, pInput, Rect);
	}

	SInputFieldResult IntegerField(const IUiContext &Ctx, CLineInputNumber *pInput, int *pValue, int Min, int Max, const CUIRect &Rect, const STextFieldOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || pInput == nullptr || pValue == nullptr)
			return {};
		if(Ctx.m_pUi->RenderOnly())
		{
			char aValue[32];
			str_format(aValue, sizeof(aValue), "%d", std::clamp(*pValue, Min, Max));
			const float FontSize = std::min(Options.m_FontSize, Rect.h * CUi::ms_FontmodHeight * 0.8f);
			Ctx.m_pUi->DoLabel(&Rect, aValue, FontSize, Options.m_TextAlign);
			return {};
		}

		const int ClampedValue = std::clamp(*pValue, Min, Max);
		if(ClampedValue != *pValue)
			*pValue = ClampedValue;

		if(!pInput->IsActive() && (pInput->IsEmpty() || pInput->GetInteger() != *pValue))
		{
			pInput->SetInteger(*pValue);
			pInput->SelectAll();
		}

		SInputFieldOptions FieldOptions;
		FieldOptions.m_pPlaceholder = Options.m_pPlaceholder;
		FieldOptions.m_FontSize = Options.m_FontSize;
		FieldOptions.m_Corners = Options.m_Corners;
		FieldOptions.m_TextAlign = Options.m_TextAlign;
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

		const float LabelWidth = std::clamp(Rect.w * 0.25f, 108.0f, 180.0f);
		Rect.VSplitLeft(LabelWidth, &Label, nullptr);
		return Label;
	}

	bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || pState == nullptr || pValue == nullptr || Max <= Min)
			return false;

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
			Label = SliderInputFieldLabelRect(Rect, true, Options.m_Flags);
			Rect.VSplitLeft(Label.w, nullptr, &Controls);
			Controls.VMargin(std::min(10.0f, Controls.w * 0.025f), &Controls);
		}
		else
		{
			Controls = Rect;
		}

		const bool HasSuffix = Options.m_pSuffix != nullptr && Options.m_pSuffix[0] != '\0';
		const float SuffixWidth = HasSuffix ? std::max(18.0f, Options.m_TrailingWidth) : 0.0f;
		const float MinimumValueWidth = 52.0f + SuffixWidth + 22.0f;
		const float ValueWidth = std::clamp((MultiLine ? ValueRect.w : Controls.w) * 0.26f, MinimumValueWidth, 128.0f);
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
			if(Options.m_pLabelElement != nullptr)
			{
				SLabelProperties Props;
				Props.m_MaxWidth = Label.w;
				Ctx.m_pUi->DoLabelStreamed(*Options.m_pLabelElement->Rect(0), &Label, Options.m_pLabel, LabelFontSize, Options.m_LabelAlign, Props, -1, nullptr, !Ctx.m_pUi->RenderOnly());
			}
			else
				Ctx.m_pUi->DoLabel(&Label, Options.m_pLabel, LabelFontSize, Options.m_LabelAlign);
		}
		if(RenderOnly)
		{
			const int PreviewStoredValue = *pValue;
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
			const float FieldFontSize = std::min(Options.m_FontSize, InputField.h * CUi::ms_FontmodHeight * 0.8f);
			if(HasSuffix)
			{
				const SInputFieldLayout FieldLayout = ResolveInputFieldLayout(InputField, false, false, Ctx.m_UiScale);
				const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout(FieldLayout.m_ContentRect, Ctx.m_pUi->TextRender()->TextWidth(FieldFontSize, aValue), Ctx.m_pUi->TextRender()->TextWidth(FieldFontSize * 0.82f, Options.m_pSuffix), Ctx.m_UiScale);
				Ctx.m_pUi->DoLabel(&InlineLayout.m_TextRect, aValue, FieldFontSize, TEXTALIGN_MR);
				Ctx.m_pUi->DoLabel(&InlineLayout.m_TrailingRect, Options.m_pSuffix, FieldFontSize * 0.82f, TEXTALIGN_MC);
			}
			else
				Ctx.m_pUi->DoLabel(&InputField, aValue, FieldFontSize, TEXTALIGN_MC);
			return false;
		}

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

	bool Toggle(const IUiContext &Ctx, const void *pId, bool *pValue, const CUIRect &Rect, bool ProcessInput, bool Animate)
	{
		if(Ctx.m_pUi == nullptr || pValue == nullptr || Ctx.m_pUi->RenderOnly())
			return false;
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ctx.m_pUi);

		const int Result = ProcessInput ? Ctx.m_pUi->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) : 0;
		const bool Clicked = Result != 0;
		if(Clicked)
			*pValue = !*pValue;

		// Track
		const ColorRGBA TrackOn = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		const ColorRGBA TrackOff = ui_token::color::BORDER_SUBTLE;
		ColorRGBA Track = *pValue ? TrackOn : TrackOff;
		if(Animate && Ctx.m_pAnim != nullptr)
		{
			const uint64_t TrackKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0xA5A5ull, reinterpret_cast<uint64_t>(pId));
			Track = ResolveUiAnimValueColor(*Ctx.m_pAnim, TrackKey, Track, ui_token::motion::BTN_HOVER.m_DurationSec, ui_token::motion::BTN_HOVER.m_Easing);
		}
		DrawRoundedSurface(Ctx, Rect, Track, Track, Rect.h * 0.5f);

		// 弹簧只跟踪开关进度，控件滚动或布局移动不改变动画目标。
		const float Padding = std::min(Rect.h * 0.15f, 3.0f);
		const float KnobSize = Rect.h - Padding * 2.0f;
		const float LeftX = Rect.x + Padding;
		const float RightX = Rect.x + Rect.w - KnobSize - Padding;
		float KnobProgress = *pValue ? 1.0f : 0.0f;
		if(Animate && Ctx.m_pAnim != nullptr)
		{
			const uint64_t KnobKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ 0x5A5Aull, reinterpret_cast<uint64_t>(pId));
			KnobProgress = ResolveUiAnimSpringValue(*Ctx.m_pAnim, KnobKey, EUiAnimProperty::COLOR_MIX, KnobProgress, ui_token::motion::TOGGLE);
		}

		CUIRect Knob;
		Knob.x = LeftX + (RightX - LeftX) * std::clamp(KnobProgress, 0.0f, 1.0f);
		Knob.y = Rect.y + Padding;
		Knob.w = KnobSize;
		Knob.h = KnobSize;
		DrawRoundedSurface(Ctx, Knob, ui_token::color::TEXT_PRIMARY, ui_token::color::TEXT_PRIMARY, KnobSize * 0.5f);

		return Clicked;
	}

	bool Slider(const IUiContext &Ctx, const void *pId, float *pValue, float Min, float Max, const CUIRect &Rect, const char *pSuffix)
	{
		if(Ctx.m_pUi == nullptr || pValue == nullptr || Max <= Min)
			return false;

		CUIRect Track, Label;
		Rect.VSplitRight(48.0f, &Track, &Label);
		Track.VSplitRight(ui_token::spacing::SM, &Track, nullptr);

		const float Normalized = std::clamp((*pValue - Min) / (Max - Min), 0.0f, 1.0f);
		const ColorRGBA Inner = Ctx.m_pTheme != nullptr ? Ctx.m_pTheme->m_Accent : ui_token::color::ACCENT_PRIMARY;
		const float NewNormalized = Ctx.m_pUi->DoScrollbarH(pId, &Track, Normalized, &Inner);
		const float NewValue = Min + NewNormalized * (Max - Min);
		const bool Changed = std::abs(NewValue - *pValue) > 1e-4f;
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
