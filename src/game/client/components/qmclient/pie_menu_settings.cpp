#include <game/client/components/menus.h>

#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

void CMenus::RenderQmFunctionPieMenuContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CornerRadius, bool PrewarmOnly)
{
	CPerfTimer LayoutTimer;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_pie_menu_text_inputs", UiScale);
	char aLayoutExtra[96];
	str_copy(aLayoutExtra, "tab=function module=pie_menu", sizeof(aLayoutExtra));
	if(g_Config.m_QmPerfDebug)
		QmPerfLogStage("perf/qmclient", "pie_menu_layout", LayoutTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
	CPerfTimer ControlsTimer;
	CUIRect Row, LabelColumn, ControlColumn;
	Content.HSplitTop(LineHeight, &Row, &Content);
	RenderQmFunctionCheckbox(&g_Config.m_QmPieMenuEnabled, "Enable pie menu", Localize("Enable pie menu"), &g_Config.m_QmPieMenuEnabled, &Row, PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmPieFollowName[0] != '\0')
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect CancelButton;
		Row.VSplitRight(LineHeight, &Row, &CancelButton);
		char aStatus[192];
		str_format(aStatus, sizeof(aStatus), Localize("Following: %s"), g_Config.m_QmPieFollowName);
		SLabelProperties Props;
		Props.m_MaxWidth = maximum(0.0f, Row.w - LineSpacing);
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Row, aStatus, BodySize, TEXTALIGN_ML, Props);
		static CButtonContainer s_CancelFollowButton;
		if(!PrewarmOnly && Ui()->DoButton_QmIcon(&s_CancelFollowButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &CancelButton, BUTTONFLAG_LEFT))
			GameClient()->m_PieMenu.CancelFollow();
		if(!PrewarmOnly)
			GameClient()->m_Tooltips.DoToolTip(&s_CancelFollowButton, &CancelButton, Localize("Stop following"));
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	if(!g_Config.m_QmPieMenuEnabled)
	{
		if(g_Config.m_QmPerfDebug)
			QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
		return;
	}

	auto RenderSlider = [this, &Content, &Row, &LabelColumn, &ControlColumn, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly](const void *pId, const char *pTextId, const char *pText, int *pValue, int Min, int Max, const char *pSuffix = "") {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, Min, Max, pSuffix, PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	};
	static int s_PieMenuScaleInputId, s_PieMenuOpacityInputId, s_PieMenuMaxDistanceInputId;
	RenderSlider(&s_PieMenuScaleInputId, "qmclient-pie-menu-ui-scale", Localizable("UI scale"), &g_Config.m_QmPieMenuScale, 50, 200, "%");
	RenderSlider(&s_PieMenuOpacityInputId, "qmclient-pie-menu-opacity", Localizable("Opacity"), &g_Config.m_QmPieMenuOpacity, 0, 100, "%");
	RenderSlider(&s_PieMenuMaxDistanceInputId, "qmclient-pie-menu-detection-distance", Localizable("Detection distance"), &g_Config.m_QmPieMenuMaxDistance, 100, 2000);

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-rename-queue", &LabelColumn, Localize("Rename queue"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	static CLineInput s_PieMenuRenameQueue(g_Config.m_QmPieMenuRenameQueue, sizeof(g_Config.m_QmPieMenuRenameQueue));
	s_PieMenuRenameQueue.SetEmptyText(Localize("Example: name1|name2|name3"));
	ui_widget::InputField(TextInputCtx, &s_PieMenuRenameQueue, ControlColumn, Localize("Example: name1|name2|name3"), BodySize);
	Content.HSplitTop(LineSpacing * 2.0f, nullptr, &Content);

	struct SPieMenuColorEntry
	{
		const char *m_pName;
		const char *m_pIcon;
		unsigned int *m_pColorValue;
		ColorRGBA m_DefaultColor;
		int *m_pEnabled;
		const char *m_pTextId;
		bool m_Alpha = false;
	};
	const std::array<SPieMenuColorEntry, 10> aColorEntries = {{
		{Localize("Friend"), FontIcons::FONT_ICON_HEART, (unsigned int *)&g_Config.m_QmPieMenuColorFriend, ColorRGBA(0.9f, 0.3f, 0.4f), &g_Config.m_QmPieMenuFriendEnabled, "qmclient-pie-friend"},
		{Localize("Whisper"), FontIcons::FONT_ICON_COMMENT, (unsigned int *)&g_Config.m_QmPieMenuColorWhisper, ColorRGBA(0.5f, 0.35f, 0.7f), &g_Config.m_QmPieMenuWhisperEnabled, "qmclient-pie-whisper"},
		{Localize("Mention"), FontIcons::FONT_ICON_CHEVRON_RIGHT, (unsigned int *)&g_Config.m_QmPieMenuColorMention, ColorRGBA(0.85f, 0.5f, 0.2f), &g_Config.m_QmPieMenuMentionEnabled, "qmclient-pie-mention"},
		{Localize("Copy skin"), FontIcons::FONT_ICON_COPY, (unsigned int *)&g_Config.m_QmPieMenuColorCopySkin, ColorRGBA(0.25f, 0.55f, 0.8f), &g_Config.m_QmPieMenuCopySkinEnabled, "qmclient-pie-copy-skin"},
		{Localize("Swap"), FontIcons::FONT_ICON_ARROWS_LEFT_RIGHT, (unsigned int *)&g_Config.m_QmPieMenuColorSwap, ColorRGBA(0.8f, 0.3f, 0.3f), &g_Config.m_QmPieMenuSwapEnabled, "qmclient-pie-swap"},
		{Localize("Spectate"), FontIcons::FONT_ICON_EYE, (unsigned int *)&g_Config.m_QmPieMenuColorSpectate, ColorRGBA(0.45f, 0.55f, 0.6f), &g_Config.m_QmPieMenuSpectateEnabled, "qmclient-pie-spectate"},
		{Localize("Invite to team"), FontIcons::FONT_ICON_USERS, (unsigned int *)&g_Config.m_QmPieMenuColorInviteTeam, ColorRGBA(0.9f, 0.48f, 0.3f, 0.75f), &g_Config.m_QmPieMenuInviteTeamEnabled, "qmclient-pie-invite-team", true},
		{Localize("Join team"), FontIcons::FONT_ICON_RIGHT_TO_BRACKET, (unsigned int *)&g_Config.m_QmPieMenuColorJoinTeam, ColorRGBA(0.3f, 0.61f, 0.9f, 0.75f), &g_Config.m_QmPieMenuJoinTeamEnabled, "qmclient-pie-join-team", true},
		{Localize("Follow server"), FontIcons::FONT_ICON_NETWORK_WIRED, (unsigned int *)&g_Config.m_QmPieMenuColorFollow, ColorRGBA(0.3f, 0.75f, 0.5f, 0.75f), &g_Config.m_QmPieMenuFollowEnabled, "qmclient-pie-follow", true},
		{Localize("View points"), FontIcons::FONT_ICON_MAGNIFYING_GLASS, (unsigned int *)&g_Config.m_QmPieMenuColorScore, ColorRGBA(0.69f, 0.42f, 0.9f, 0.75f), &g_Config.m_QmPieMenuScoreEnabled, "qmclient-pie-points", true},
	}};
	auto OpenColorPopup = [&](const SPieMenuColorEntry &Entry) {
		const ColorHSLA HslaColor = ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha);
		m_ColorPickerPopupContext.m_pHslaColor = Entry.m_pColorValue;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = Entry.m_Alpha;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	};
	for(const auto &Entry : aColorEntries)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect Swatch;
		Row.VSplitRight(LineHeight, &Row, &Swatch);
		Row.VSplitRight(LineSpacing, &Row, nullptr);
		RenderQmFunctionCheckbox(Entry.m_pEnabled, Entry.m_pTextId, Entry.m_pName, Entry.m_pEnabled, &Row, PrewarmOnly);
		if(!PrewarmOnly)
		{
			Swatch.Draw(color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), IGraphics::CORNER_ALL, CornerRadius * 0.5f);
			if(Ui()->DoButtonLogic(Entry.m_pColorValue, 0, &Swatch, BUTTONFLAG_LEFT))
				OpenColorPopup(Entry);
			GameClient()->m_Tooltips.DoToolTip(Entry.m_pColorValue, &Swatch, Localize("Set color"));
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
	}
	Content.HSplitTop(BodySize, &Row, &Content);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-option-color", &Row, Localize("Option color"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	std::array<const SPieMenuColorEntry *, 10> apVisibleEntries{};
	int VisibleCount = 0;
	for(const auto &Entry : aColorEntries)
		if(*Entry.m_pEnabled)
			apVisibleEntries[VisibleCount++] = &Entry;
	constexpr float PreviewStartAngle = -90.0f;
	constexpr float PreviewSectorGap = 3.6f;
	constexpr float PreviewInnerRatio = 108.0f / 288.0f;
	constexpr float PreviewHighlightScale = 1.12f;
	const float PreviewBaseSide = minimum(Content.w, std::clamp(Content.w * 0.88f, LineHeight * 10.0f, LineHeight * 13.5f));
	const float PreviewSide = PreviewBaseSide * 0.8f;
	CUIRect PreviewRow, PreviewRect, PreviewInfoRect;
	Content.HSplitTop(PreviewSide, &PreviewRow, &Content);
	PreviewRow.VSplitLeft(PreviewSide, &PreviewRect, &PreviewInfoRect);
	PreviewInfoRect.VSplitLeft(maximum(CardPadding * 0.8f, LineSpacing * 2.0f), nullptr, &PreviewInfoRect);
	PreviewRect.Margin(LineSpacing * 0.5f, &PreviewRect);
	CUIRect PreviewFrame = PreviewRect;
	const char *pHintText = Localize("Click to set color");
	if(!PrewarmOnly)
	{
		{
			CUiScopedGaussianBlurSuppression PreviewBlurSuppression(Ui());
			PreviewFrame.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, CornerRadius * 0.8f);
		}
		PreviewRect.Margin(maximum(4.0f, LineSpacing * 0.6f), &PreviewRect);
		const vec2 PreviewCenter = PreviewRect.Center();
		const float BaseOuterRadius = maximum(1.0f, minimum(PreviewRect.w, PreviewRect.h) * 0.5f - LineSpacing * 0.8f);
		const float InnerRadius = BaseOuterRadius * PreviewInnerRatio;
		const float CenterRadius = maximum(1.0f, InnerRadius - maximum(4.0f, BaseOuterRadius * 0.03f));
		const float AnglePerSector = VisibleCount > 0 ? 360.0f / VisibleCount : 0.0f;
		const float PreviewAlpha = std::clamp(g_Config.m_QmPieMenuOpacity / 100.0f, 0.2f, 1.0f);
		int PopupSectorIndex = -1;
		if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
		{
			for(int i = 0; i < VisibleCount; ++i)
				if(m_ColorPickerPopupContext.m_pHslaColor == apVisibleEntries[i]->m_pColorValue)
					PopupSectorIndex = (int)i;
		}
		int HoveredSector = -1;
		if(VisibleCount > 0 && Ui()->MouseInside(&PreviewFrame))
		{
			const vec2 MouseDir = Ui()->MousePos() - PreviewCenter;
			const float MouseDist = length(MouseDir);
			if(MouseDist >= InnerRadius && MouseDist <= BaseOuterRadius * PreviewHighlightScale)
			{
				float MouseAngle = atan2(MouseDir.y, MouseDir.x) * 180.0f / pi;
				while(MouseAngle < 0.0f)
					MouseAngle += 360.0f;
				const float AdjustedAngle = fmodf(MouseAngle - PreviewStartAngle + 360.0f, 360.0f);
				const int SectorIndex = (int)(AdjustedAngle / AnglePerSector);
				const float AngleInSector = AdjustedAngle - SectorIndex * AnglePerSector;
				if(SectorIndex >= 0 && SectorIndex < VisibleCount && AngleInSector >= PreviewSectorGap * 0.5f && AngleInSector <= AnglePerSector - PreviewSectorGap * 0.5f)
					HoveredSector = SectorIndex;
			}
		}
		static CButtonContainer s_ColorPreviewButton;
		if(Ui()->DoButtonLogic(&s_ColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0)
			OpenColorPopup(*apVisibleEntries[HoveredSector]);
		for(int i = 0; i < VisibleCount; ++i)
		{
			const auto &Entry = *apVisibleEntries[i];
			const bool Highlighted = (int)i == HoveredSector || (int)i == PopupSectorIndex;
			const float OuterRadius = BaseOuterRadius * (Highlighted ? PreviewHighlightScale : 1.0f);
			const float StartAngle = PreviewStartAngle + AnglePerSector * i + PreviewSectorGap * 0.5f;
			const float EndAngle = StartAngle + AnglePerSector - PreviewSectorGap;
			ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha));
			if(Highlighted)
			{
				Color.r = minimum(Color.r * 1.3f, 1.0f);
				Color.g = minimum(Color.g * 1.3f, 1.0f);
				Color.b = minimum(Color.b * 1.3f, 1.0f);
				Color.a = minimum(Color.a * 1.2f, 1.0f);
			}
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a * PreviewAlpha);
			for(int Segment = 0; Segment < 24; ++Segment)
			{
				const float Rad1 = (StartAngle + (EndAngle - StartAngle) * (Segment / 24.0f)) * pi / 180.0f;
				const float Rad2 = (StartAngle + (EndAngle - StartAngle) * ((Segment + 1) / 24.0f)) * pi / 180.0f;
				const vec2 Inner1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * InnerRadius;
				const vec2 Outer1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * OuterRadius;
				const vec2 Inner2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * InnerRadius;
				const vec2 Outer2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * OuterRadius;
				const IGraphics::CFreeformItem Freeform(Inner1.x, Inner1.y, Outer1.x, Outer1.y, Inner2.x, Inner2.y, Outer2.x, Outer2.y);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}
			Graphics()->QuadsEnd();
			const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
			const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * ((InnerRadius + OuterRadius) * 0.5f);
			const float IconSize = BaseOuterRadius * (Highlighted ? 0.22f : 0.19f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, PreviewAlpha);
			const EFontPreset PreviousFont = TextRender()->GetFontPreset();
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const float IconWidth = TextRender()->TextWidth(IconSize, Entry.m_pIcon);
			TextRender()->Text(ItemPos.x - IconWidth * 0.5f, ItemPos.y - IconSize * 0.5f, IconSize, Entry.m_pIcon);
			TextRender()->SetFontPreset(PreviousFont);
		}
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.15f, 0.15f, 0.2f, 0.9f * PreviewAlpha);
		Graphics()->DrawCircle(PreviewCenter.x, PreviewCenter.y, CenterRadius, 48);
		Graphics()->QuadsEnd();
		const int FocusedSector = HoveredSector >= 0 ? HoveredSector : PopupSectorIndex;
		const char *pCenterTitle = FocusedSector >= 0 ? apVisibleEntries[FocusedSector]->m_pName : Localize("Set color");
		pHintText = FocusedSector >= 0 ? apVisibleEntries[FocusedSector]->m_pName : Localize("Option color");
		float CenterTitleSize = maximum(BodySize, BaseOuterRadius * 0.095f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.98f);
		float CenterTitleWidth = TextRender()->TextWidth(CenterTitleSize, pCenterTitle);
		if(CenterTitleWidth > CenterRadius * 1.65f)
		{
			CenterTitleSize *= CenterRadius * 1.65f / CenterTitleWidth;
			CenterTitleWidth = TextRender()->TextWidth(CenterTitleSize, pCenterTitle);
		}
		TextRender()->Text(PreviewCenter.x - CenterTitleWidth * 0.5f, PreviewCenter.y - CenterTitleSize * 0.5f, CenterTitleSize, pCenterTitle);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect PreviewInfoContent = PreviewInfoRect;
	const float InfoSpacing = LineSpacing * 0.75f;
	const float InfoHeight = LineHeight + InfoSpacing + ButtonHeight;
	if(PreviewInfoContent.h > InfoHeight)
		PreviewInfoContent.HSplitTop((PreviewInfoContent.h - InfoHeight) * 0.5f, nullptr, &PreviewInfoContent);
	CUIRect HintRow, ResetRow;
	PreviewInfoContent.HSplitTop(LineHeight, &HintRow, &PreviewInfoContent);
	PreviewInfoContent.HSplitTop(InfoSpacing, nullptr, &PreviewInfoContent);
	PreviewInfoContent.HSplitTop(ButtonHeight, &ResetRow, &PreviewInfoContent);
	SLabelProperties HintProps;
	HintProps.m_MaxWidth = maximum(0.0f, HintRow.w);
	HintProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&HintRow, pHintText, BodySize * 0.9f, TEXTALIGN_MR, HintProps);
	static CButtonContainer s_ResetAllColorsButton;
	CUIRect ResetButton;
	const float ResetWidth = minimum(ButtonHeight, maximum(0.0f, ResetRow.w));
	ResetRow.VSplitRight(ResetWidth, nullptr, &ResetButton);
	if(!PrewarmOnly && Ui()->DoButton_QmIcon(&s_ResetAllColorsButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ResetButton, BUTTONFLAG_LEFT))
		for(const auto &Entry : aColorEntries)
			*Entry.m_pColorValue = color_cast<ColorHSLA>(Entry.m_DefaultColor).Pack(Entry.m_Alpha);
	if(!PrewarmOnly)
		GameClient()->m_Tooltips.DoToolTip(&s_ResetAllColorsButton, &ResetButton, Localize("Reset colors"));
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(g_Config.m_QmPerfDebug)
		QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
}
