#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmPieMenuRender.h>
#include <game/client/QmUi/SettingsToggleGrid.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/components/pie_menu_logic.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

void CMenus::RenderQmFunctionPieMenuContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CornerRadius, bool PrewarmOnly, int MaxGridColumns)
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
	RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, &g_Config.m_QmPieMenuEffects, "Pie menu effects", Localize("Pie menu effects"), &g_Config.m_QmPieMenuEffects, PrewarmOnly);
	static CButtonContainer s_SelectedColor;
	DoLine_ColorPicker(&s_SelectedColor, CurrentSettingsContentMetrics(), &Content, Localize("Pie menu highlight tint"), &g_Config.m_QmPieMenuSelectedColor,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmPieMenuSelectedColor, true)), false, nullptr, true);
	Content.HSplitTop(LineHeight, &Row, &Content);
	static CButtonContainer s_ResetAppearance;
	if(DoButton_Menu(&s_ResetAppearance, Localize("Reset pie menu appearance"), 0, &Row) && !PrewarmOnly && !Ui()->RenderOnly())
	{
		g_Config.m_QmPieMenuScale = DefaultConfig::QmPieMenuScale;
		g_Config.m_QmPieMenuOpacity = DefaultConfig::QmPieMenuOpacity;
		g_Config.m_QmPieMenuEffects = DefaultConfig::QmPieMenuEffects;
		g_Config.m_QmPieMenuSelectedColor = DefaultConfig::QmPieMenuSelectedColor;
		g_Config.m_QmPieMenuColorFriend = DefaultConfig::QmPieMenuColorFriend;
		g_Config.m_QmPieMenuColorWhisper = DefaultConfig::QmPieMenuColorWhisper;
		g_Config.m_QmPieMenuColorMention = DefaultConfig::QmPieMenuColorMention;
		g_Config.m_QmPieMenuColorCopySkin = DefaultConfig::QmPieMenuColorCopySkin;
		g_Config.m_QmPieMenuColorSwap = DefaultConfig::QmPieMenuColorSwap;
		g_Config.m_QmPieMenuColorSpectate = DefaultConfig::QmPieMenuColorSpectate;
		g_Config.m_QmPieMenuColorInviteTeam = DefaultConfig::QmPieMenuColorInviteTeam;
		g_Config.m_QmPieMenuColorJoinTeam = DefaultConfig::QmPieMenuColorJoinTeam;
		g_Config.m_QmPieMenuColorFollow = DefaultConfig::QmPieMenuColorFollow;
		g_Config.m_QmPieMenuColorScore = DefaultConfig::QmPieMenuColorScore;
		g_Config.m_QmPieMenuColorCopyName = DefaultConfig::QmPieMenuColorCopyName;
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);

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
	const std::array<SPieMenuColorEntry, qm_pie_menu::OPTION_COUNT> aColorEntries = {{
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
		{Localize("Copy name"), FontIcons::FONT_ICON_USER, (unsigned int *)&g_Config.m_QmPieMenuColorCopyName, ColorRGBA(0.4f, 0.8f, 0.8f, 0.75f), &g_Config.m_QmPieMenuCopyNameEnabled, "qmclient-pie-copy-name", true},
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
	float MinimumCellWidth = 72.0f * UiScale;
	for(const auto &Entry : aColorEntries)
		MinimumCellWidth = std::max(MinimumCellWidth, TextRender()->TextWidth(BodySize, Entry.m_pName) + LineSpacing * 2.0f);
	SSettingsToggleGrid Grid = ResolveSettingsToggleGrid(Content.w, MinimumCellWidth, 0.0f, LineSpacing * 2.0f, static_cast<int>(aColorEntries.size()), MaxGridColumns);
	float LabelHeight = LineHeight;
	for(const auto &Entry : aColorEntries)
		LabelHeight = std::max(LabelHeight, TextRender()->TextBoundingBox(BodySize, Entry.m_pName, -1, std::max(1.0f, Grid.m_CellWidth)).m_H);
	Grid.m_RowHeight = LabelHeight + LineSpacing * 0.5f + LineHeight;
	CUIRect GridArea;
	Content.HSplitTop(Grid.Height(), &GridArea, &Content);
	for(size_t Index = 0; Index < aColorEntries.size(); ++Index)
	{
		const auto &Entry = aColorEntries[Index];
		CUIRect Cell = Grid.Cell(GridArea, static_cast<int>(Index));
		CUIRect Label;
		Cell.HSplitTop(LabelHeight, &Label, &Cell);
		Cell.HSplitTop(LineSpacing * 0.5f, nullptr, &Cell);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Entry.m_pTextId, &Label, Entry.m_pName, BodySize, TEXTALIGN_MC, Props);
		if(!PrewarmOnly && !Ui()->RenderOnly())
		{
			const float SwatchSize = std::min(LineHeight, Cell.w);
			const float ControlGap = std::min(LineSpacing, std::max(0.0f, Cell.w - SwatchSize));
			const float ToggleWidth = std::min(LineHeight * 2.0f, std::max(0.0f, Cell.w - SwatchSize - ControlGap));
			const float GroupWidth = ToggleWidth + ControlGap + SwatchSize;
			const CUIRect Toggle{Cell.x + (Cell.w - GroupWidth) * 0.5f, Cell.y, ToggleWidth, LineHeight};
			const CUIRect Swatch{Toggle.x + ToggleWidth + ControlGap, Cell.y, SwatchSize, LineHeight};
			bool Enabled = *Entry.m_pEnabled != 0;
			if(ui_widget::Toggle(TextInputCtx, Entry.m_pEnabled, &Enabled, Toggle))
				*Entry.m_pEnabled = Enabled;
			Swatch.Draw(color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), IGraphics::CORNER_ALL, CornerRadius * 0.5f);
			if(Ui()->DoButtonLogic(Entry.m_pColorValue, 0, &Swatch, BUTTONFLAG_LEFT))
				OpenColorPopup(Entry);
			GameClient()->m_Tooltips.DoToolTip(Entry.m_pColorValue, &Swatch, Localize("Set color"));
		}
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(BodySize, &Row, &Content);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-option-color", &Row, Localize("Option color"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	std::array<const SPieMenuColorEntry *, qm_pie_menu::OPTION_COUNT> apVisibleEntries{};
	int VisibleCount = 0;
	for(const auto &Entry : aColorEntries)
		if(*Entry.m_pEnabled)
			apVisibleEntries[VisibleCount++] = &Entry;
	constexpr float PreviewStartAngle = -90.0f;
	const float PreviewSectorGap = VisibleCount == 1 ? 0.0f : 3.6f;
	constexpr float PreviewInnerRatio = 108.0f / 288.0f;
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
			HoveredSector = qm_pie_menu_ui::HoveredSector(Ui()->MousePos() - PreviewCenter, InnerRadius, BaseOuterRadius,
				PreviewStartAngle, VisibleCount, PreviewSectorGap, qm_pie_menu_ui::PixelSize(Graphics()));
		static CButtonContainer s_ColorPreviewButton;
		if(Ui()->DoButtonLogic(&s_ColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0)
			OpenColorPopup(*apVisibleEntries[HoveredSector]);
		qm_pie_menu_ui::DrawDisc(Graphics(), PreviewCenter, CenterRadius, ColorRGBA(0.15f, 0.15f, 0.2f, 0.9f * PreviewAlpha));
		for(int i = 0; i < VisibleCount; ++i)
		{
			const auto &Entry = *apVisibleEntries[i];
			const bool Highlighted = (int)i == HoveredSector || (int)i == PopupSectorIndex;
			const float OuterRadius = BaseOuterRadius;
			const float StartAngle = PreviewStartAngle + AnglePerSector * i + PreviewSectorGap * 0.5f;
			const float EndAngle = StartAngle + AnglePerSector - PreviewSectorGap;
			const ColorRGBA Color = qm_pie_menu_ui::OptionColor(color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), Highlighted,
				color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmPieMenuSelectedColor, true))).WithMultipliedAlpha(PreviewAlpha);
			qm_pie_menu_ui::DrawSector(Graphics(), PreviewCenter, InnerRadius, OuterRadius, StartAngle, EndAngle, PreviewSectorGap, Color);
			const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
			const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * ((InnerRadius + OuterRadius) * 0.5f);
			const float IconSize = BaseOuterRadius * 0.19f;
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, PreviewAlpha);
			const EFontPreset PreviousFont = TextRender()->GetFontPreset();
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const float IconWidth = TextRender()->TextWidth(IconSize, Entry.m_pIcon);
			const CUIRect IconRect{ItemPos.x - IconWidth * 0.5f, ItemPos.y - IconSize * 0.5f, IconWidth, IconSize};
			Ui()->DoLabel(&IconRect, Entry.m_pIcon, IconSize, TEXTALIGN_MC);
			TextRender()->SetFontPreset(PreviousFont);
		}
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
