/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SettingsCard.h"

#include "SettingsPageLayout.h"
#include "UiContext.h"
#include "UiSurface.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace
{
	const SUiTheme &SettingsCardTheme(const IUiContext &Ctx, SUiTheme &Fallback)
	{
		Fallback = ResolveUiTheme(ColorHSLA(0.0f, 0.0f, 0.29f, 1.0f), 1.0f);
		return Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : Fallback;
	}

}

void RenderSettingsCardCollapseButton(const IUiContext &Ctx, const CUIRect &Rect, const bool Collapsed, const float DrawAlpha)
{
	if(Ctx.m_pUi == nullptr)
		return;
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	const bool Hovered = Ctx.m_pUi->MouseHovered(&Rect);
	const float Alpha = std::clamp(DrawAlpha, 0.0f, 1.0f);
	const float PixelSize = Ctx.m_pUi->PixelSize();
	const CUIRect ChromeRect = ResolveSettingsCardChromeRect(Rect, PixelSize);
	const float Radius = AlignSettingsCardValueToPixels(std::min(4.0f * UiScale, std::min(ChromeRect.w, ChromeRect.h) * 0.25f), PixelSize);
	const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, (Hovered ? 0.28f : 0.18f) * Alpha);
	DrawRoundedSurface(Ctx, ChromeRect, ChromeColor, ChromeColor, Radius);
	const float IconSize = std::clamp(ui_token::font::BODY * UiScale, 10.0f, ui_token::font::BODY);
	const ColorRGBA IconColor = ConfiguredQmUiIconColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	ITextRender *pTextRender = Ctx.m_pUi->TextRender();
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	const ColorRGBA PreviousOutlineColor = pTextRender->GetTextOutlineColor();
	const ColorRGBA PreviousSelectionColor = pTextRender->GetTextSelectionColor();
	const unsigned PreviousFlags = pTextRender->GetRenderFlags();
	const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
	pTextRender->TextColor(IconColor);
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT_BOLD);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	Ctx.m_pUi->DoLabel(&ChromeRect, Collapsed ? FontIcons::FONT_ICON_CHEVRON_DOWN : FontIcons::FONT_ICON_CHEVRON_UP, IconSize, TEXTALIGN_MC);
	pTextRender->SetRenderFlags(PreviousFlags);
	pTextRender->SetFontPreset(PreviousPreset);
	pTextRender->TextOutlineColor(PreviousOutlineColor);
	pTextRender->TextSelectionColor(PreviousSelectionColor);
	pTextRender->TextColor(PreviousColor);
}

SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const CUIRect &Slot, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardMeasure &Measure, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction, const FSettingsCardRenderMeasured &RenderMeasured, bool *pPointerInside)
{
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	const float ContentWidth = std::max(0.0f, Slot.w - 2.0f * ui_token::settings::CARD_PADDING * UiScale);
	const float ContentHeight = Measure ? std::max(0.0f, Measure(ContentWidth)) : 0.0f;
	const SSettingsCardFrame Frame = BuildSettingsCardFrame(Slot, Spec, ContentHeight, UiScale);
	return SettingsCard(Ctx, Frame, Spec, State, VisualOptions, Render, HeaderAction, RenderMeasured, pPointerInside);
}

SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const SSettingsCardFrame &Frame, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction, const FSettingsCardRenderMeasured &RenderMeasured, bool *pPointerInside)
{
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	SSettingsCardVisualState DrawState = State;
	const SSettingsCardFrame DrawFrame = ResolveSettingsCardDrawFrame(Frame, State.m_DrawOffsetX, State.m_DrawOffsetY);
	DrawState.m_PointerInside = Ctx.m_pUi != nullptr && Ctx.m_pUi->MouseHovered(&DrawFrame.m_Rect);
	if(pPointerInside != nullptr)
		*pPointerInside = DrawState.m_PointerInside;
	DrawState.m_Hovered = State.m_HoverFeedbackEnabled && DrawState.m_PointerInside;

	SUiTheme Fallback;
	const SUiTheme &Theme = SettingsCardTheme(Ctx, Fallback);
	const bool DrawCardChrome = SettingsCardShouldDrawChrome(Ctx.m_pUi != nullptr && Ctx.m_pUi->RenderOnly());
	// 普通 hover 只暴露副标题；重排与拖放反馈只改变边框，卡片背景透明度保持稳定，避免滚动或切页时闪烁。
	// 完成反馈只属于显式拖放。普通高度/布局变化不得改变卡片 chrome，
	// 否则半透明卡片在展开、折叠或首次布局时会表现为一次亮闪。
	const bool InteractionComplete = DrawState.m_DropFeedback;
	// 普通卡片只是内容容器，指针进入只显示副标题。焦点和拖放才需要轮廓反馈，
	// 且不能被“常驻边框”关闭选项一并隐藏。
	const bool DrawNormalBorder = VisualOptions.m_AlwaysShowBorders;
	const bool DrawAttentionBorder = DrawState.m_Focused || DrawState.m_Dragged || InteractionComplete;
	ColorRGBA Border = DrawAttentionBorder ? Theme.m_BorderFocused : VisualOptions.m_BorderColor;
	Border.a *= DrawState.m_DrawAlpha;
	ColorRGBA Surface = ResolveSettingsCardSurfaceColor(VisualOptions.m_UseSurfaceColor ? VisualOptions.m_SurfaceColor : Theme.m_Surface, DrawState);
	const float PixelSize = Ctx.m_pUi != nullptr ? Ctx.m_pUi->PixelSize() : 0.0f;
	const CUIRect ChromeRect = ResolveSettingsCardChromeRect(DrawFrame.m_Rect, PixelSize);
	const float CardRadius = AlignSettingsCardValueToPixels(std::min(ui_token::settings::CARD_RADIUS * UiScale, std::min(ChromeRect.w, ChromeRect.h) * 0.5f), PixelSize);
	// 焦点与拖放只能改变边框颜色，普通 hover 不参与 chrome；任何状态都不能改变
	// Surface 的几何，否则边框获得焦点时会产生一次内缩跳变并重新触发卡片闪动。
	const float BorderWidth = ResolveSettingsCardBorderWidth(UiScale, PixelSize);
	if(DrawCardChrome)
		DrawRoundedSurface(Ctx, ChromeRect, Surface, Border, CardRadius, DrawNormalBorder || DrawAttentionBorder ? BorderWidth : 0.0f);

	if(Ctx.m_pUi != nullptr && Ctx.m_pTextRender != nullptr)
	{
		const ColorRGBA PreviousTextColor = Ctx.m_pTextRender->GetTextColor();
		const ColorRGBA PreviousTextOutlineColor = Ctx.m_pTextRender->GetTextOutlineColor();
		const ColorRGBA PreviousTextSelectionColor = Ctx.m_pTextRender->GetTextSelectionColor();
		const unsigned PreviousRenderFlags = Ctx.m_pTextRender->GetRenderFlags();
		const EFontPreset PreviousFontPreset = Ctx.m_pTextRender->GetFontPreset();
		ColorRGBA TitleColor = Theme.m_TextTitle;
		if(VisualOptions.m_RainbowTitles)
		{
			const float TimePhase = (float)time_get() / (float)time_freq() * 0.08f;
			const float IdPhase = Spec.m_pStableId != nullptr ? (float)(str_quickhash(Spec.m_pStableId) & 0xffff) / 65535.0f : 0.0f;
			TitleColor = color_cast<ColorRGBA>(ColorHSLA(std::fmod(TimePhase + IdPhase, 1.0f), 0.75f, 0.65f, 1.0f));
		}
		TitleColor.a *= DrawState.m_DrawAlpha;
		Ctx.m_pTextRender->TextColor(TitleColor);
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = DrawFrame.m_TitleRect.w;
		TitleProps.m_EllipsisAtEnd = true;
		Ctx.m_pUi->DoLabel(&DrawFrame.m_TitleRect, Spec.m_pTitle != nullptr ? Spec.m_pTitle : "", ui_token::font::TITLE * UiScale, TEXTALIGN_ML, TitleProps);
		const char *pSubtitle = Spec.m_pSubtitle;
		if(pSubtitle != nullptr && SettingsCardSubtitleVisible(DrawState.m_Hovered, DrawState.m_SubtitleVisibleDuringMotion, DrawState.m_Focused))
		{
			ColorRGBA SubtitleColor = Theme.m_TextSmall;
			SubtitleColor.a *= DrawState.m_DrawAlpha;
			Ctx.m_pTextRender->TextColor(SubtitleColor);
			SLabelProperties SubtitleProps;
			SubtitleProps.m_MaxWidth = DrawFrame.m_SubtitleRect.w;
			SubtitleProps.m_EllipsisAtEnd = true;
			const float SubtitleSize = ResolveSettingsSmallFontSize(UiScale);
			Ctx.m_pUi->DoLabel(&DrawFrame.m_SubtitleRect, pSubtitle, SubtitleSize, TEXTALIGN_ML, SubtitleProps);
		}
		// 标题和副标题只影响本卡片，不能把调用方的文本状态写死为默认白色。
		Ctx.m_pTextRender->SetRenderFlags(PreviousRenderFlags);
		Ctx.m_pTextRender->SetFontPreset(PreviousFontPreset);
		Ctx.m_pTextRender->TextOutlineColor(PreviousTextOutlineColor);
		Ctx.m_pTextRender->TextSelectionColor(PreviousTextSelectionColor);
		Ctx.m_pTextRender->TextColor(PreviousTextColor);
		if(DrawCardChrome && DrawState.m_ShowDefaultCollapseButton)
			RenderSettingsCardCollapseButton(Ctx, DrawFrame.m_HandleRect, DrawState.m_Collapsed, DrawState.m_DrawAlpha);
	}

	if(HeaderAction && DrawCardChrome)
		HeaderAction(DrawFrame, DrawState.m_Collapsed);
	const bool ClipContent = DrawState.m_ClipContent && Ctx.m_pUi != nullptr && DrawFrame.m_ContentRect.w > 0.0f && DrawFrame.m_ContentRect.h > 0.0f;
	if(ClipContent)
	{
		const CUIRect ClipRect = ResolveSettingsCardContentClipRect(DrawFrame.m_ContentRect, DrawFrame.m_Rect, UiScale);
		Ctx.m_pUi->ClipEnable(&ClipRect);
	}
	if(RenderMeasured)
	{
		CUIRect ContentRect = DrawFrame.m_ContentRect;
		RenderMeasured(ContentRect);
	}
	else if(Render)
		Render(DrawFrame.m_ContentRect);
	if(ClipContent)
		Ctx.m_pUi->ClipDisable();
	return Frame;
}
