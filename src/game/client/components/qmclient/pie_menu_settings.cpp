#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmPieMenuRender.h>
#include <game/client/QmUi/QmPieMenuSettingsControls.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsToggleGrid.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/components/pie_menu_logic.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

void CMenus::RenderQmFunctionPieMenuContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ButtonHeight, float CardPadding, float CornerRadius, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool CanCommit = Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly();
	CPerfTimer LayoutTimer;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_pie_menu_text_inputs", UiScale);
	char aLayoutExtra[96];
	str_copy(aLayoutExtra, "tab=function module=pie_menu", sizeof(aLayoutExtra));
	if(CanCommit && g_Config.m_QmPerfDebug)
		QmPerfLogStage("perf/qmclient", "pie_menu_layout", LayoutTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
	CPerfTimer ControlsTimer;
	CUIRect Row, LabelColumn, ControlColumn;
	if(Flow.Control("pie-enabled", LineHeight, Row))
		RenderQmFunctionCheckbox(&g_Config.m_QmPieMenuEnabled, "Enable pie menu", Localize("Enable pie menu"), &g_Config.m_QmPieMenuEnabled, &Row, !CanCommit);
	Flow.Space(LineSpacing);
	if(g_Config.m_QmPieFollowName[0] != '\0')
	{
		if(Flow.Control("pie-follow-status", LineHeight, Row))
		{
			CUIRect CancelButton;
			Row.VSplitRight(LineHeight, &Row, &CancelButton);
			char aStatus[192];
			str_format(aStatus, sizeof(aStatus), Localize("Following: %s"), g_Config.m_QmPieFollowName);
			SLabelProperties Props;
			Props.m_MaxWidth = maximum(0.0f, Row.w - LineSpacing);
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Row, aStatus, BodySize, TEXTALIGN_ML, Props);
			static CButtonContainer s_CancelFollowButton;
			if(Ui()->DoButton_QmIcon(&s_CancelFollowButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &CancelButton, BUTTONFLAG_LEFT) && CanCommit)
				GameClient()->m_PieMenu.CancelFollow();
			if(!PrewarmOnly)
				GameClient()->m_Tooltips.DoToolTip(&s_CancelFollowButton, &CancelButton, Localize("Stop following"));
		}
		Flow.Space(LineSpacing);
	}
	if(!g_Config.m_QmPieMenuEnabled)
	{
		if(CanCommit && g_Config.m_QmPerfDebug)
			QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
		return;
	}

	auto RenderSlider = [this, &Flow, &Row, &LabelColumn, &ControlColumn, LineHeight, BodySize, LineSpacing, CanCommit](const void *pId, const char *pTextId, const char *pText, int *pValue, int Min, int Max, const char *pSuffix = "") {
		if(Flow.Control(pTextId, LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, Min, Max, pSuffix, !CanCommit);
		}
		Flow.Space(LineSpacing);
	};
	static int s_PieMenuScaleInputId, s_PieMenuOpacityInputId, s_PieMenuMaxDistanceInputId;
	RenderSlider(&s_PieMenuScaleInputId, "qmclient-pie-menu-ui-scale", Localizable("UI scale"), &g_Config.m_QmPieMenuScale, 50, 200, "%");
	RenderSlider(&s_PieMenuOpacityInputId, "qmclient-pie-menu-opacity", Localizable("Opacity"), &g_Config.m_QmPieMenuOpacity, 0, 100, "%");
	RenderSlider(&s_PieMenuMaxDistanceInputId, "qmclient-pie-menu-detection-distance", Localizable("Detection distance"), &g_Config.m_QmPieMenuMaxDistance, 100, 2000);
	if(Flow.Control("pie-effects", LineHeight, Row))
		RenderQmFunctionCheckbox(&g_Config.m_QmPieMenuEffects, "Pie menu effects", Localize("Pie menu effects"), &g_Config.m_QmPieMenuEffects, &Row, !CanCommit);
	Flow.Space(LineSpacing);
	if(Flow.Control("pie-selected-color", ResolveSettingsColorRowLayout({0, 0, Content.w, LineHeight}, CurrentSettingsContentMetrics(), false).m_ConsumedHeight, Row))
	{
		static CButtonContainer s_SelectedColor;
		CSettingsContentLayoutScope ColorScope;
		DoLine_ColorPicker(&s_SelectedColor, CurrentSettingsContentMetrics(), &Row, Localize("Pie menu highlight tint"), &g_Config.m_QmPieMenuSelectedColor,
			color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmPieMenuSelectedColor, true)), false, nullptr, true);
	}
	static CButtonContainer s_ResetAppearance;
	if(Flow.Control("pie-reset-appearance", LineHeight, Row) && DoButton_Menu(&s_ResetAppearance, Localize("Reset pie menu appearance"), 0, &Row) && CanCommit)
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
	Flow.Space(LineSpacing);

	if(Flow.Control("pie-rename-queue", LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-rename-queue", &LabelColumn, Localize("Rename queue"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static CLineInput s_PieMenuRenameQueue(g_Config.m_QmPieMenuRenameQueue, sizeof(g_Config.m_QmPieMenuRenameQueue));
		if(CanCommit)
			s_PieMenuRenameQueue.SetEmptyText(Localize("Example: A|B|C,D|E (C: main, D: dummy)"));
		ui_widget::InputField(TextInputCtx, &s_PieMenuRenameQueue, ControlColumn, Localize("Example: A|B|C,D|E (C: main, D: dummy)"), BodySize);
		// 改名名单的说明是一整句不可断行的中文，走通用配置提示会因默认阅读宽度被截成省略号；
		// 这里按内容宽度给出完整提示，整句始终可见。
		if(!PrewarmOnly)
		{
			const char *pRenameQueueHelp = Localize("Pie menu rename list (| separates entries; main,dummy renames both)");
			const float HelpFontSize = CurrentSettingsContentMetrics().m_SmallSize;
			const float HelpWidth = TextRender()->TextWidth(HelpFontSize, pRenameQueueHelp) + HelpFontSize * 2.0f;
			GameClient()->m_Tooltips.DoSmallToolTip(&s_PieMenuRenameQueue, &ControlColumn, pRenameQueueHelp, HelpFontSize, HelpWidth);
		}
	}
	Flow.Space(LineSpacing * 2.0f);

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
	SSettingsContentMetrics ToggleMetrics = CurrentSettingsContentMetrics();
	ToggleMetrics.m_UiScale = UiScale;
	ToggleMetrics.m_LineHeight = LineHeight;
	ToggleMetrics.m_BodySize = BodySize;
	ToggleMetrics.m_LineSpacing = LineSpacing;
	const float PaddingX = std::max(LineSpacing, BodySize * 0.5f);
	const float PaddingY = LineSpacing * 0.5f;
	const auto OptionButtonRect = [LineHeight, LineSpacing](CUIRect Cell) {
		// 色块位于按钮右侧，两个命中区域始终分离。
		return QmPieOptionButtonRect(Cell, LineHeight, LineSpacing);
	};
	std::array<SSettingsToggleSize, qm_pie_menu::OPTION_COUNT> aItemSizes{};
	for(size_t Index = 0; Index < aColorEntries.size(); ++Index)
	{
		aItemSizes[Index] = ResolveSettingsToggleButtonSize(TextRender()->TextWidth(BodySize, aColorEntries[Index].m_pName), BodySize, LineHeight, PaddingX, PaddingY);
		aItemSizes[Index].m_Width += LineHeight + LineSpacing;
	}
	// 左右对调：左边是圆饼预览，中间是重置图标，右边是选项按钮网格。
	// 预览边长仍沿用行高公式（卡片高度估算 QmPieMenuContentHeight 用的是同一个式子），
	// 选项网格先占够两列，预览只使用剩下的宽度，右侧按钮不会被挤成一列一个。
	const float PreviewBaseSide = minimum(Content.w, std::clamp(Content.w * 0.88f, LineHeight * 10.0f, LineHeight * 13.5f));
	const float PreviewSide = PreviewBaseSide * 0.8f;
	const float ColumnGap = maximum(CardPadding * 0.8f, LineSpacing * 2.0f);
	// 重置图标按钮原来按 clamp(ButtonHeight, 1.5 倍行高, 2 倍行高) 出尺寸，比行高明显偏大；
	// 现在收到控件行高量级（约 1.2 倍行高），下限用 spacing token 托底。
	const float ResetButtonSide = maximum(ui_token::spacing::LG, minimum(ButtonHeight, LineHeight) * 1.2f);
	const float ResetColumnWidth = ResetButtonSide + ColumnGap;
	// 选项网格是「按钮 + 色块」成对的两列：按列取最长项得到两列各自的最小宽度，
	// 先保证右侧列宽放得下两列，再决定预览能用多少宽度。
	float aOptionColumnMinWidth[2] = {0.0f, 0.0f};
	for(size_t Index = 0; Index < aItemSizes.size(); ++Index)
	{
		const size_t Column = Index % 2;
		aOptionColumnMinWidth[Column] = maximum(aOptionColumnMinWidth[Column], aItemSizes[Index].m_Width);
	}
	const float OptionsMinWidth = aOptionColumnMinWidth[0] + aOptionColumnMinWidth[1] + LineSpacing;
	const float OptionsColumnWidth = minimum(maximum(OptionsMinWidth, Content.w - PreviewSide - ResetColumnWidth), maximum(0.0f, Content.w - ResetColumnWidth));
	// 预览尺寸按剩余宽度重算：只拿两栏分完剩下的那一列。
	const float PreviewColumnWidth = maximum(0.0f, Content.w - OptionsColumnWidth - ResetColumnWidth);
	auto Grid = ResolveSettingsToggleGrid(OptionsColumnWidth, aItemSizes, LineSpacing);
	for(size_t Index = 0; Index < aColorEntries.size(); ++Index)
	{
		const CUIRect Button = OptionButtonRect({0, 0, Grid.m_aColumnWidths[Index % Grid.m_Columns], LineHeight});
		const CUIRect Label = ResolveSettingsToggleButtonLabel(Button, PaddingX, PaddingY);
		const float LabelHeight = TextRender()->TextBoundingBox(BodySize, aColorEntries[Index].m_pName, -1, std::max(1.0f, Label.w)).m_H;
		aItemSizes[Index].m_Height = ResolveSettingsToggleButtonSize(0, LabelHeight, LineHeight, PaddingX, PaddingY).m_Height;
	}
	Grid.SetItemHeights(aItemSizes);
	std::array<const SPieMenuColorEntry *, qm_pie_menu::OPTION_COUNT> apVisibleEntries{};
	int VisibleCount = 0;
	for(const auto &Entry : aColorEntries)
		if(*Entry.m_pEnabled)
			apVisibleEntries[VisibleCount++] = &Entry;
	constexpr float PreviewStartAngle = -90.0f;
	const float PreviewSectorGap = VisibleCount == 1 ? 0.0f : 3.6f;
	constexpr float PreviewInnerRatio = 108.0f / 288.0f;
	// 行高同时容纳网格与预览：预览被剩余列宽夹住时，整行跟着夹住后的尺寸走。
	const float OptionsRowHeight = std::max(Grid.Height(), minimum(PreviewSide, PreviewColumnWidth));
	if(Flow.Control("pie-option-color-heading", BodySize, Row))
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-pie-menu-option-color", &Row, Localize("Option color"), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
	Flow.Space(LineSpacing);
	CUIRect OptionsRow;
	if(Flow.Control("pie-color-options-row", OptionsRowHeight, OptionsRow))
	{
		CUIRect PreviewColumn, ResetColumn, OptionsColumn;
		// 从左到右：圆饼预览、重置图标、选项按钮网格。
		OptionsRow.VSplitLeft(PreviewColumnWidth, &PreviewColumn, &OptionsRow);
		OptionsRow.VSplitLeft(ResetColumnWidth, &ResetColumn, &OptionsColumn);
		for(size_t Index = 0; Index < aColorEntries.size(); ++Index)
		{
			const auto &Entry = aColorEntries[Index];
			const CUIRect Cell = Grid.Cell(OptionsColumn, static_cast<int>(Index));
			// 末位参数打开悬浮明暗反馈：按钮和右侧色块要一起响应指针。
			DoSettingsToggleButton(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, OptionButtonRect(Cell),
				{Entry.m_pEnabled, Entry.m_pTextId, Entry.m_pName}, ToggleMetrics, CanCommit, Flow.Input() ? ESettingsToggleGroupPass::INPUT : ESettingsToggleGroupPass::RENDER, true);
			const CUIRect Swatch = QmPieOptionSwatchRect(Cell, LineHeight);
			if(QmDrawPieOptionColorControl(TextInputCtx, Entry.m_pColorValue,
				   color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), Swatch, CornerRadius * 0.5f, CanCommit))
				OpenColorPopup(Entry);
			if(!PrewarmOnly)
				GameClient()->m_Tooltips.DoToolTip(Entry.m_pColorValue, &Swatch, Localize("Set color"));
		}
		// 重置图标夹在两栏之间并垂直居中：一键把选项颜色恢复成默认值。
		CUIRect ResetButton;
		ResetButton.w = ResetButton.h = ResetButtonSide;
		ResetButton.x = ResetColumn.x + (ResetColumn.w - ResetButton.w) * 0.5f;
		ResetButton.y = ResetColumn.y + (ResetColumn.h - ResetButton.h) * 0.5f;
		static CButtonContainer s_ResetAllColorsButton;
		if(!PrewarmOnly && Ui()->DoButton_QmIcon(&s_ResetAllColorsButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ResetButton, BUTTONFLAG_LEFT) && CanCommit)
			for(const auto &Entry : aColorEntries)
				*Entry.m_pColorValue = color_cast<ColorHSLA>(Entry.m_DefaultColor).Pack(Entry.m_Alpha);
		if(!PrewarmOnly)
			GameClient()->m_Tooltips.DoToolTip(&s_ResetAllColorsButton, &ResetButton, Localize("Reset colors"));
		CUIRect PreviewRect = PreviewColumn;
		// 预览边长取剩余列宽与行高上限里的较小值，列比圆饼宽时左右居中。
		PreviewRect.w = minimum(PreviewRect.w, PreviewSide);
		PreviewRect.h = minimum(PreviewRect.h, PreviewSide);
		PreviewRect.x = PreviewColumn.x + (PreviewColumn.w - PreviewRect.w) * 0.5f;
		PreviewRect.y = PreviewColumn.y + (PreviewColumn.h - PreviewRect.h) * 0.5f;
		PreviewRect.Margin(LineSpacing * 0.5f, &PreviewRect);
		CUIRect PreviewFrame = PreviewRect;
		if(!PrewarmOnly)
		{
			if(Flow.Drawing())
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
			if(Ui()->DoButtonLogic(&s_ColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0 && CanCommit)
				OpenColorPopup(*apVisibleEntries[HoveredSector]);
			if(Flow.Drawing())
			{
				qm_pie_menu_ui::DrawDisc(Graphics(), PreviewCenter, CenterRadius, ColorRGBA(0.15f, 0.15f, 0.2f, 0.9f * PreviewAlpha));
				for(int i = 0; i < VisibleCount; ++i)
				{
					const auto &Entry = *apVisibleEntries[i];
					const bool Highlighted = (int)i == HoveredSector || (int)i == PopupSectorIndex;
					const float OuterRadius = BaseOuterRadius;
					const float StartAngle = PreviewStartAngle + AnglePerSector * i + PreviewSectorGap * 0.5f;
					const float EndAngle = StartAngle + AnglePerSector - PreviewSectorGap;
					const ColorRGBA Color = qm_pie_menu_ui::OptionColor(color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), Highlighted,
						color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmPieMenuSelectedColor, true)))
									.WithMultipliedAlpha(PreviewAlpha);
					qm_pie_menu_ui::DrawSector(Graphics(), PreviewCenter, InnerRadius, OuterRadius, StartAngle, EndAngle, PreviewSectorGap, Color);
					const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
					const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * ((InnerRadius + OuterRadius) * 0.5f);
					const float IconSize = BaseOuterRadius * 0.19f;
					QmDrawPieSettingsPreviewIcon(*Ui(), ItemPos, IconSize, Entry.m_pIcon, PreviewAlpha);
				}
				const int FocusedSector = HoveredSector >= 0 ? HoveredSector : PopupSectorIndex;
				const char *pCenterTitle = FocusedSector >= 0 ? apVisibleEntries[FocusedSector]->m_pName : Localize("Set color");
				const float CenterTitleSize = maximum(BodySize, BaseOuterRadius * 0.095f);
				QmDrawPieSettingsPreviewTitle(*TextRender(), PreviewCenter, CenterTitleSize, CenterRadius, pCenterTitle);
			}
		}
	}
	Flow.Space(LineSpacing);
	if(CanCommit && g_Config.m_QmPerfDebug)
		QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
}
