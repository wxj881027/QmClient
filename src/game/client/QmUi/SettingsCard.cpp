/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SettingsCard.h"

#include "QmAnimResolve.h"
#include "SettingsCardInfo.h"
#include "SettingsCardSubtitle.h"
#include "SettingsIconFeedback.h"
#include "SettingsPageLayout.h"
#include "UiButtons.h"
#include "UiContext.h"
#include "UiSurface.h"
#include "UiSurfaceText.h"
#include "UiTheme.h"
#include "UiTokens.h"

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/qm_icon.h>
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

	void RenderSettingsCardLabel(const IUiContext &Ctx, const SSettingsCardSpec &Spec, bool Subtitle, const CUIRect &Rect, const char *pText, float Size, const SLabelProperties &Props)
	{
		ExecuteSettingsCardLabel(Rect, [&]() {
			if(Ctx.m_pMenus == nullptr || Spec.m_pStableId == nullptr || Spec.m_pStableId[0] == '\0')
			{
				Ctx.m_pUi->DoLabel(&Rect, pText, Size, TEXTALIGN_ML, Props);
				return;
			}

			// 卡片 stable ID 跨设置页唯一；公开的设置文字池入口同时处理预布局收集与渲染。
			char aTextId[256];
			str_format(aTextId, sizeof(aTextId), "settings-card-%s:%s", Subtitle ? "subtitle" : "title", Spec.m_pStableId);
			Ctx.m_pMenus->DoSettingsMenuLabel(-1, -1, -1, aTextId, &Rect, pText, Size, TEXTALIGN_ML, Props);
		});
	}

}

void RenderSettingsCardHeaderIcon(const IUiContext &Ctx, const CUIRect &Rect, const EQmIcon Icon, const char *pGlyph, const float DrawAlpha, const void *pId)
{
	if(Ctx.m_pUi == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	const bool Hovered = Ctx.m_pUi->MouseHovered(&Rect);
	const float Alpha = std::clamp(DrawAlpha, 0.0f, 1.0f);
	const CUIRect &ChromeRect = Rect;
	const float Radius = std::min(ui_token::radius::TIGHT * UiScale, std::min(ChromeRect.w, ChromeRect.h) * 0.25f);
	const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, (Hovered ? (Ctx.m_pUi->MouseButton(0) ? 0.36f : 0.28f) : 0.18f) * Alpha);
	if(!Ctx.m_pUi->MeasurementOnly())
		DrawRoundedSurface(Ctx, ChromeRect, ChromeColor, ChromeColor, Radius);
	const bool Pressed = Hovered && Ctx.m_pUi->MouseButton(0);
	const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ str_quickhash("card-icon-feedback"), reinterpret_cast<uint64_t>(pId));
	const float Scale = Ctx.m_pAnim != nullptr && pId != nullptr ? ResolveSettingsIconScale(*Ctx.m_pAnim, NodeKey, Hovered, Pressed, !Ctx.m_pUi->RenderOnly()) : 1.0f;
	const float IconSize = std::clamp(ui_token::font::BODY * UiScale, 10.0f, ui_token::font::BODY) * Scale;
	const ColorRGBA IconColor = ResolveUiSurfaceIconColor(ChromeColor, Ctx.m_pUi->TextRender()->GetTextColor().WithAlpha(Alpha));
	ITextRender *pTextRender = Ctx.m_pUi->TextRender();
	ExecuteSettingsCardHeaderIcon(*pTextRender, IconColor, [&]() {
		Ctx.m_pUi->DoLabel_QmIcon(&ChromeRect, Icon, pGlyph, IconSize, TEXTALIGN_MC);
	});
}

void RenderSettingsCardCollapseButton(const IUiContext &Ctx, const CUIRect &Rect, const bool Collapsed, const float DrawAlpha, const void *pId)
{
	RenderSettingsCardHeaderIcon(Ctx, Rect, Collapsed ? EQmIcon::CHEVRON_DOWN : EQmIcon::CHEVRON_UP, Collapsed ? FontIcons::FONT_ICON_CHEVRON_DOWN : FontIcons::FONT_ICON_CHEVRON_UP, DrawAlpha, pId);
}

SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const CUIRect &Slot, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardMeasure &Measure, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction, const FSettingsCardRenderMeasured &RenderMeasured, bool *pPointerInside, float *pObservedContentHeight)
{
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	const float ContentWidth = std::max(0.0f, Slot.w - 2.0f * ui_token::settings::CARD_PADDING * UiScale);
	const float ContentHeight = Measure ? std::max(0.0f, Measure(ContentWidth)) : 0.0f;
	const SSettingsCardFrame Frame = BuildSettingsCardFrame(Slot, Spec, ContentHeight, UiScale);
	return SettingsCard(Ctx, Frame, Spec, State, VisualOptions, Render, HeaderAction, RenderMeasured, pPointerInside, pObservedContentHeight);
}

SSettingsCardFrame SettingsCard(const IUiContext &Ctx, const SSettingsCardFrame &Frame, const SSettingsCardSpec &Spec, const SSettingsCardVisualState &State, const SSettingsCardDeckVisualOptions &VisualOptions, const FSettingsCardRender &Render, const FSettingsCardHeaderAction &HeaderAction, const FSettingsCardRenderMeasured &RenderMeasured, bool *pPointerInside, float *pObservedContentHeight)
{
	const CQmCardLabelHintScope LabelHints(Ctx.m_pUi);
	const float UiScale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	SSettingsCardVisualState DrawState = State;
	SSettingsCardFrame DrawFrame = ResolveSettingsCardDrawFrame(Frame, State.m_DrawOffsetX, State.m_DrawOffsetY);
	CUIRect InfoButton{};
	if(Spec.m_pInfo != nullptr && Spec.m_pInfo[0] != '\0')
	{
		InfoButton = ResolveSettingsCardInfoRect(DrawFrame);
		DrawFrame.m_TitleRect.w = std::min(DrawFrame.m_TitleRect.w, std::max(0.0f, InfoButton.x - DrawFrame.m_TitleRect.x - ui_token::spacing::XS));
	}
	DrawState.m_PointerInside = Ctx.m_pUi != nullptr && Ctx.m_pUi->MouseHovered(&DrawFrame.m_Rect);
	if(pPointerInside != nullptr)
		*pPointerInside = DrawState.m_PointerInside;
	DrawState.m_Hovered = State.m_HoverFeedbackEnabled && DrawState.m_PointerInside;

	SUiTheme Fallback;
	const SUiTheme &Theme = SettingsCardTheme(Ctx, Fallback);
	const bool DrawCardChrome = SettingsCardShouldDrawChrome(Ctx.m_pUi != nullptr && Ctx.m_pUi->MeasurementOnly());
	// 普通 hover 只显示卡片内副标题；重排与拖放反馈只改变边框，卡片背景透明度保持稳定，避免滚动或切页时闪烁。
	// 完成反馈只属于显式拖放。普通高度/布局变化不得改变卡片 chrome，
	// 否则半透明卡片在展开、折叠或首次布局时会表现为一次亮闪。
	// 普通卡片只是内容容器，指针进入不改变背景。拖放才需要轮廓反馈，
	// 且不能被“常驻边框”关闭选项一并隐藏。
	const bool DrawNormalBorder = VisualOptions.m_AlwaysShowBorders;
	const bool DrawAttentionBorder = SettingsCardInteractionBorderVisible(DrawState);
	ColorRGBA Border = DrawAttentionBorder ? Theme.m_BorderFocused : VisualOptions.m_BorderColor;
	Border.a *= DrawState.m_DrawAlpha;
	ColorRGBA Surface = ResolveSettingsCardSurfaceColor(VisualOptions.m_UseSurfaceColor ? VisualOptions.m_SurfaceColor : Theme.m_Surface, DrawState);
	const float PixelSize = Ctx.m_pUi != nullptr ? Ctx.m_pUi->PixelSize() : 0.0f;
	// 绘制和命中、内容使用同一份连续几何，避免滑动时边缘逐像素跳动。
	const CUIRect &ChromeRect = DrawFrame.m_Rect;
	const float CardRadius = std::min(ui_token::settings::CARD_RADIUS * UiScale, std::min(ChromeRect.w, ChromeRect.h) * 0.5f);
	// 拖放只能改变边框颜色，普通 hover 不参与 chrome；任何状态都不能改变
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
		CUiScopedSurfaceText HeaderSurfaceText(Ctx.m_pTextRender, Surface);
		// 卡片标题不按普通设置标签规则拆括号。
		const CQmCardLabelHintScope HeaderLabelHints(Ctx.m_pUi, false);
		Ctx.m_pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		ColorRGBA TitleColor = ResolveConfiguredTextColor(Surface);
		// 彩虹标题由「彩虹卡片标题」独立开关控制，优先于全局文本颜色模式；
		// 文本颜色模式只约束普通文本，不得覆盖卡片标题的彩虹流动效果。
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
		RenderSettingsCardLabel(Ctx, Spec, false, DrawFrame.m_TitleRect, Spec.m_pTitle != nullptr ? Spec.m_pTitle : "", ui_token::font::TITLE * UiScale, TitleProps);
		ColorRGBA SubtitleColor = ResolveConfiguredTextColor(Surface);
		SubtitleColor.a *= DrawState.m_DrawAlpha;
		Ctx.m_pTextRender->TextColor(SubtitleColor);
		RenderSettingsCardSubtitle(Ctx, DrawFrame, Spec, DrawState.m_PointerInside);
		// 标题只影响本卡片，不能把调用方的文本状态写死为默认白色。
		Ctx.m_pTextRender->SetRenderFlags(PreviousRenderFlags);
		Ctx.m_pTextRender->SetFontPreset(PreviousFontPreset);
		Ctx.m_pTextRender->TextOutlineColor(PreviousTextOutlineColor);
		Ctx.m_pTextRender->TextSelectionColor(PreviousTextSelectionColor);
		Ctx.m_pTextRender->TextColor(PreviousTextColor);
		if(DrawCardChrome && DrawState.m_ShowDefaultCollapseButton)
			RenderSettingsCardCollapseButton(Ctx, DrawFrame.m_HandleRect, DrawState.m_Collapsed, DrawState.m_DrawAlpha, Spec.m_pStableId);
	}

	if(DrawCardChrome)
		RenderSettingsCardInfo(Ctx, Spec, InfoButton, DrawState.m_DrawAlpha);

	if(HeaderAction && DrawCardChrome)
		HeaderAction(DrawFrame, DrawState.m_Collapsed);
	// 空内容变为非空的首帧也裁剪；随后消费高度反馈使 Deck 自动扩高。
	const bool ClipContent = DrawState.m_ClipContent && Ctx.m_pUi != nullptr && DrawFrame.m_ContentRect.w > 0.0f;
	if(ClipContent)
	{
		const CUIRect ClipRect = ResolveSettingsCardContentClipRect(DrawFrame.m_ContentRect, DrawFrame.m_Rect, UiScale);
		Ctx.m_pUi->ClipEnable(&ClipRect);
	}
	CUiScopedSurfaceText SurfaceText(Ctx.m_pTextRender, Surface);
	CUiScopedControlAlphaScale ContentAlpha(Ctx.m_pUi, DrawState.m_DrawAlpha);
	if(RenderMeasured)
	{
		CUIRect ContentRect = DrawFrame.m_ContentRect;
		const float ObservedHeight = RenderSettingsCardObservedContent(RenderMeasured, ContentRect);
		if(pObservedContentHeight != nullptr)
			*pObservedContentHeight = ObservedHeight;
	}
	else if(Render)
		Render(DrawFrame.m_ContentRect);
	if(ClipContent)
		Ctx.m_pUi->ClipDisable();
	return Frame;
}

// 作为 CMenus 的共享表单入口，沿用文字池、配置提示和临时接管策略。
bool CMenus::DoSettingsToggleGroup(int Page, int Tab, int Subtab, CUIRect &Content, std::initializer_list<SSettingsToggleEntry> Entries, const SSettingsContentMetrics &Metrics, bool ProcessInput, ESettingsToggleGroupPass Pass)
{
	if(Entries.size() == 0)
		return false;
	const float Gap = Metrics.m_LineSpacing;
	const float PaddingX = std::max(Metrics.m_LineSpacing, Metrics.m_BodySize * 0.5f);
	const float PaddingY = Metrics.m_LineSpacing * 0.5f;
	m_vSettingsToggleGroupSizes.resize(Entries.size());
	size_t Index = 0;
	for(const auto &Entry : Entries)
	{
		auto &Size = m_vSettingsToggleGroupSizes[Index++];
		const float IndicatorWidth = Entry.m_ShowCheckmark ? Metrics.m_BodySize + Gap : 0.0f;
		Size = ResolveSettingsToggleButtonSize(TextRender()->TextWidth(Metrics.m_BodySize, Entry.m_pLabel), Metrics.m_BodySize, Metrics.m_LineHeight, PaddingX, PaddingY, IndicatorWidth);
	}
	auto &Grid = m_SettingsToggleGroupGrid;
	ResolveSettingsToggleGrid(Content.w, m_vSettingsToggleGroupSizes, Gap, Grid);
	Index = 0;
	for(const auto &Entry : Entries)
	{
		const CUIRect Button{0, 0, Grid.m_aColumnWidths[Index % Grid.m_Columns], Metrics.m_LineHeight};
		const float IndicatorWidth = Entry.m_ShowCheckmark ? Metrics.m_BodySize + Gap : 0.0f;
		const CUIRect Label = ResolveSettingsToggleButtonLabel(Button, PaddingX, PaddingY, IndicatorWidth);
		const float LabelHeight = TextRender()->TextBoundingBox(Metrics.m_BodySize, Entry.m_pLabel, -1, std::max(1.0f, Label.w)).m_H;
		m_vSettingsToggleGroupSizes[Index++].m_Height = ResolveSettingsToggleButtonSize(0, LabelHeight, Metrics.m_LineHeight, PaddingX, PaddingY).m_Height;
	}
	Grid.SetItemHeights(m_vSettingsToggleGroupSizes);
	CUIRect Area;
	CSettingsContentLayoutFlow Flow(Content);
	// 分组作为一个稳定布局块发布；输入和绘制消费同一份卡片坐标。
	const char *pGroupId = Entries.begin()->m_pTextId;
	const bool HasArea = Flow.Control(pGroupId, Grid.Height(), Area);
	Flow.Space(Metrics.m_LineSpacing);
	if(!HasArea)
		return false;
	ProcessInput = ProcessInput && Flow.Input();
	if(Pass == ESettingsToggleGroupPass::LAYOUT)
		return false;

	bool Changed = false;
	Index = 0;
	for(const auto &Entry : Entries)
	{
		const CUIRect Cell = Grid.Cell(Area, static_cast<int>(Index++));
		Changed = DoSettingsToggleButton(Page, Tab, Subtab, Cell, Entry, Metrics, ProcessInput, Pass) || Changed;
	}
	return Changed;
}

bool CMenus::DoSettingsToggleButton(int Page, int Tab, int Subtab, const CUIRect &Button, const SSettingsToggleEntry &Entry, const SSettingsContentMetrics &Metrics, bool ProcessInput, ESettingsToggleGroupPass Pass)
{
	if(Pass == ESettingsToggleGroupPass::LAYOUT)
		return false;
	const float PaddingX = std::max(Metrics.m_LineSpacing, Metrics.m_BodySize * 0.5f);
	const float PaddingY = Metrics.m_LineSpacing * 0.5f;
	const float IndicatorWidth = Entry.m_ShowCheckmark ? Metrics.m_BodySize + Metrics.m_LineSpacing : 0.0f;
	const CUIRect Label = ResolveSettingsToggleButtonLabel(Button, PaddingX, PaddingY, IndicatorWidth);
	const IUiContext Context = SettingsUiContext("settings-toggle-button", Metrics.m_UiScale);
	const bool CanProcessInput = ProcessInput && !m_MenuTextPlanCollecting && !Ui()->RenderOnly();
	const void *pId = Entry.m_pId != nullptr ? Entry.m_pId : Entry.m_pValue;
	const char *pOverrideTooltip = TemporaryOverrideTooltip(Entry.m_pValue);
	bool Changed = false;
	if(!m_MenuTextPlanCollecting && !Ui()->RenderOnly())
	{
		Ui()->DoConfigTooltip(pId, &Button, Entry.m_pValue);
		const bool Clicked = CanProcessInput && Ui()->DoButtonLogic(pId, 0, &Button, pOverrideTooltip == nullptr ? BUTTONFLAG_LEFT : BUTTONFLAG_NONE);
		Changed = ApplySettingsToggleEntry(Entry, Clicked, CanProcessInput, pOverrideTooltip != nullptr);
	}
	if(Pass == ESettingsToggleGroupPass::INPUT)
		return Changed;
	CUIRect FlowRect = Button;
	if(!CSettingsContentLayoutFlow(FlowRect).Drawing())
		return Changed;
	ui_widget::SButtonSurfaceOptions Options;
	Options.m_Enabled = pOverrideTooltip == nullptr;
	Options.m_Selected = SettingsToggleEntryValue(Entry);
	Options.m_Radius *= Metrics.m_UiScale;
	// 与 F1 筛选相同，以选中底色表达启用状态，不再另画滑动开关。
	Options.m_Color = Options.m_Selected ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(1.0f) : ResolveConfiguredControlSurface();
	// 开关选中后保留浅色描边，不随浅色底翻成深色；文字也维持原前景色。
	if(Options.m_Selected)
		Options.m_BorderOverride = ui_token::color::TEXT_PRIMARY.WithAlpha(0.72f);
	else
		Options.m_BorderOverride.reset();
	ColorRGBA Fill = *Options.m_Color;
	if(!Options.m_Enabled)
		Fill.a *= 0.65f;
	if(!m_MenuTextPlanCollecting)
		Fill = ui_widget::DrawButtonSurface(Context, CanProcessInput ? pId : nullptr, Button, Options);
	const CUiScopedSurfaceText SurfaceText(TextRender(), Fill);
	if(Entry.m_ShowCheckmark && !m_MenuTextPlanCollecting)
	{
		CUIRect Indicator = ResolveSettingsToggleButtonLabel(Button, PaddingX, PaddingY);
		Indicator.w = std::min(Metrics.m_BodySize, Indicator.w);
		if(Options.m_Selected)
		{
			const EFontPreset PreviousFont = TextRender()->GetFontPreset();
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			char aCheck[8] = {};
			str_utf8_encode(aCheck, CQmIconRegistry::Codepoint(EQmIcon::CHECK));
			Ui()->DoLabel(&Indicator, aCheck, Metrics.m_BodySize, TEXTALIGN_MC);
			TextRender()->SetFontPreset(PreviousFont);
		}
		else
		{
			// 关闭态也给出空心圆提示，和选中态的勾选图标共用同一槽位。
			const float Diameter = std::min(Metrics.m_BodySize * 0.72f, std::min(Indicator.w, Indicator.h));
			CUIRect Ring;
			Ring.w = Ring.h = Diameter;
			Ring.x = Indicator.x + (Indicator.w - Diameter) * 0.5f;
			Ring.y = Indicator.y + (Indicator.h - Diameter) * 0.5f;
			DrawRoundedSurface(Ui(), Ring, ColorRGBA(), ui_token::color::TEXT_PRIMARY.WithAlpha(0.52f), Diameter * 0.5f, 1.0f);
		}
	}
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EnableWidthCheck = false;
	Props.m_MinimumFontSize = Metrics.m_BodySize;
	if(pOverrideTooltip != nullptr)
		Props.SetColor(ui_token::color::TEXT_DISABLED);
	else if(Options.m_Selected)
		Props.SetColor(ui_token::color::TEXT_PRIMARY);
	ExecuteSettingsCardLabel(Label, [&]() {
		DoSettingsMenuLabel(Page, Tab, Subtab, Entry.m_pTextId, &Label, Entry.m_pLabel, Metrics.m_BodySize, TEXTALIGN_ML, Props);
	});
	if(!m_MenuTextPlanCollecting && !Ui()->RenderOnly() && Context.m_pTooltips != nullptr)
	{
		if(pOverrideTooltip != nullptr)
			Context.m_pTooltips->DoToolTip(pId, &Button, pOverrideTooltip);
		else if(Entry.m_pTooltip != nullptr)
			Context.m_pTooltips->DoToolTip(pId, &Button, Entry.m_pTooltip);
		else
			Context.m_pTooltips->DoSettingsToolTipForConfig(pId, &Button, Entry.m_pValue, &Label);
	}
	return Changed;
}
