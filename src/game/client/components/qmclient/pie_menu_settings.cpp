#include <base/perf_timer.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmPieMenuRender.h>
#include <game/client/QmUi/QmPieOptionPool.h>
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
	const float RowGap = ui_token::settings::RowGap(LineSpacing);
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
	if(g_Config.m_QmPieFollowName[0] != '\0')
	{
		if(Flow.Control("pie-follow-status", LineHeight, Row, RowGap))
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
	}
	if(!g_Config.m_QmPieMenuEnabled)
	{
		if(CanCommit && g_Config.m_QmPerfDebug)
			QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
		return;
	}

	auto RenderSlider = [this, &Flow, &Row, &LabelColumn, &ControlColumn, LineHeight, BodySize, RowGap, CanCommit](const void *pId, const char *pTextId, const char *pText, int *pValue, int Min, int Max, const char *pSuffix = "") {
		if(Flow.Control(pTextId, LineHeight, Row, RowGap))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pTextId, &LabelColumn, Localize(pText), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, Min, Max, pSuffix, !CanCommit);
		}
	};
	static int s_PieMenuScaleInputId, s_PieMenuOpacityInputId, s_PieMenuMaxDistanceInputId;
	RenderSlider(&s_PieMenuScaleInputId, "qmclient-pie-menu-ui-scale", Localizable("UI scale"), &g_Config.m_QmPieMenuScale, 50, 200, "%");
	RenderSlider(&s_PieMenuOpacityInputId, "qmclient-pie-menu-opacity", Localizable("Opacity"), &g_Config.m_QmPieMenuOpacity, 0, 100, "%");
	RenderSlider(&s_PieMenuMaxDistanceInputId, "qmclient-pie-menu-detection-distance", Localizable("Detection distance"), &g_Config.m_QmPieMenuMaxDistance, 100, 2000);
	if(Flow.Control("pie-effects", LineHeight, Row, RowGap))
		RenderQmFunctionCheckbox(&g_Config.m_QmPieMenuEffects, "Pie menu effects", Localize("Pie menu effects"), &g_Config.m_QmPieMenuEffects, &Row, !CanCommit);
	if(Flow.Control("pie-selected-color", ResolveSettingsColorRowLayout({0, 0, Content.w, LineHeight}, CurrentSettingsContentMetrics(), false, false).m_ConsumedHeight, Row, RowGap))
	{
		static CButtonContainer s_SelectedColor;
		CSettingsContentLayoutScope ColorScope;
		DoLine_ColorPicker(&s_SelectedColor, CurrentSettingsContentMetrics(), &Row, Localize("Pie menu highlight tint"), &g_Config.m_QmPieMenuSelectedColor,
			color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmPieMenuSelectedColor, true)), false, nullptr, true, false);
	}
	static CButtonContainer s_ResetAppearance;
	if(Flow.Control("pie-reset-appearance", LineHeight, Row, RowGap) && DoSettingsButton_Menu(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, &s_ResetAppearance, "pie-reset-appearance", Localize("Reset pie menu appearance"), 0, &Row, CurrentSettingsContentMetrics()) && CanCommit)
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

	if(Flow.Control("pie-rename-queue", LineHeight, Row, RowGap))
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

	// 选项表：颜色 + 图标 + 「是否在饼菜单里」的配置项。逐项色块控件已删除——
	// 颜色统一在左侧预览的扇区上点选，卡片里不再并排一堆色块（同一件事只有一个入口）。
	struct SPieMenuOptionEntry
	{
		const char *m_pName;
		const char *m_pIcon;
		unsigned *m_pColorValue;
		ColorRGBA m_DefaultColor;
		int *m_pEnabled;
		const char *m_pTextId;
		bool m_Alpha = false;
	};
	const std::array<SPieMenuOptionEntry, qm_pie_menu::OPTION_COUNT> aOptions = {{
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
	auto OpenColorPopup = [&](const SPieMenuOptionEntry &Entry) {
		const ColorHSLA HslaColor = ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha);
		m_ColorPickerPopupContext.m_pHslaColor = Entry.m_pColorValue;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = Entry.m_Alpha;
		// 饼菜单颜色直接存 alpha 字节，A 通道沿用 0..255，不继承其它入口的百分比模式。
		m_ColorPickerPopupContext.m_AlphaPercent = false;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	};

	// 两个池的成员：配置项为 1 的进「饼菜单中」，否则进「未使用」。顺序即配置顺序，
	// 池内不做额外排序——玩家认的是自己排的那套顺序。
	std::array<int, qm_pie_menu::OPTION_COUNT> aEnabledOptions{}, aDisabledOptions{};
	int EnabledCount = 0;
	int DisabledCount = 0;
	for(int Index = 0; Index < qm_pie_menu::OPTION_COUNT; ++Index)
	{
		if(*aOptions[Index].m_pEnabled)
			aEnabledOptions[EnabledCount++] = Index;
		else
			aDisabledOptions[DisabledCount++] = Index;
	}

	// 预览容器：占满整卡宽，圆饼在容器里居中。背景就是这个父容器，
	// 不再让圆饼直接压在卡片底上——那样看不出预览的可点范围。
	const float PreviewSide = std::clamp(Content.w * 0.42f, LineHeight * 8.0f, LineHeight * 12.0f);
	const float PoolPadding = ui_token::list::ContainerPadding(LineHeight);
	const float PreviewBoxHeight = PreviewSide + PoolPadding * 2.0f;

	// 胶囊宽度先量好：池的高度要在声明趟就算出来，不能等画到那一行才知道要不要换行。
	float aEnabledChipWidths[qm_pie_menu::OPTION_COUNT] = {};
	float aDisabledChipWidths[qm_pie_menu::OPTION_COUNT] = {};
	for(int Slot = 0; Slot < EnabledCount; ++Slot)
		aEnabledChipWidths[Slot] = QmPieOptionChipWidth(TextRender()->TextWidth(BodySize, aOptions[aEnabledOptions[Slot]].m_pName), BodySize);
	for(int Slot = 0; Slot < DisabledCount; ++Slot)
		aDisabledChipWidths[Slot] = QmPieOptionChipWidth(TextRender()->TextWidth(BodySize, aOptions[aDisabledOptions[Slot]].m_pName), BodySize);
	const SQmPieOptionPoolLayout MeasuredEnabledPool = QmResolvePieOptionPool({0.0f, 0.0f, Content.w, 0.0f}, EnabledCount, aEnabledChipWidths, LineHeight, BodySize);
	const SQmPieOptionPoolLayout MeasuredDisabledPool = QmResolvePieOptionPool({0.0f, 0.0f, Content.w, 0.0f}, DisabledCount, aDisabledChipWidths, LineHeight, BodySize);

	SQmPieOptionPoolLayout EnabledPoolLayout{};
	SQmPieOptionPoolLayout DisabledPoolLayout{};

	// 拖动状态跨趟跨帧保留：只有交互趟推进它，绘制趟只读。
	static SQmPieOptionPoolDrag s_PieOptionPoolDrag;

	CUIRect PreviewRow;
	if(Flow.Control("pie-preview", PreviewBoxHeight, PreviewRow, RowGap))
	{
		const CUIRect PreviewFrame = PreviewRow;
		if(Flow.Drawing())
		{
			CUiScopedGaussianBlurSuppression PreviewBlurSuppression(Ui());
			DrawRoundedSurface(Ui(), PreviewFrame, ui_token::color::PREVIEW_BACKDROP, ui_token::color::BORDER_SUBTLE, CornerRadius * 0.8f, 0.0f);
		}
		CUIRect PreviewRect = PreviewFrame;
		PreviewRect.w = minimum(PreviewRect.w, PreviewSide);
		PreviewRect.h = minimum(PreviewRect.h, PreviewSide);
		PreviewRect.x = PreviewFrame.x + (PreviewFrame.w - PreviewRect.w) * 0.5f;
		PreviewRect.y = PreviewFrame.y + (PreviewFrame.h - PreviewRect.h) * 0.5f;
		if(!PrewarmOnly)
		{
			const vec2 PreviewCenter = PreviewRect.Center();
			const float BaseOuterRadius = maximum(1.0f, minimum(PreviewRect.w, PreviewRect.h) * 0.5f - LineSpacing);
			const float InnerRadius = BaseOuterRadius * (108.0f / 288.0f);
			const float CenterRadius = maximum(1.0f, InnerRadius - maximum(4.0f, BaseOuterRadius * 0.03f));
			constexpr float PreviewStartAngle = -90.0f;
			const int VisibleCount = EnabledCount;
			const float PreviewSectorGap = VisibleCount == 1 ? 0.0f : 3.6f;
			const float AnglePerSector = VisibleCount > 0 ? 360.0f / VisibleCount : 0.0f;
			const float PreviewAlpha = std::clamp(g_Config.m_QmPieMenuOpacity / 100.0f, 0.2f, 1.0f);
			int PopupSectorIndex = -1;
			if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
			{
				for(int i = 0; i < VisibleCount; ++i)
					if(m_ColorPickerPopupContext.m_pHslaColor == aOptions[aEnabledOptions[i]].m_pColorValue)
						PopupSectorIndex = i;
			}
			int HoveredSector = -1;
			if(VisibleCount > 0 && Ui()->MouseInside(&PreviewFrame))
				HoveredSector = qm_pie_menu_ui::HoveredSector(Ui()->MousePos() - PreviewCenter, InnerRadius, BaseOuterRadius,
					PreviewStartAngle, VisibleCount, PreviewSectorGap, qm_pie_menu_ui::PixelSize(Graphics()));
			static CButtonContainer s_ColorPreviewButton;
			if(Ui()->DoButtonLogic(&s_ColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0 && CanCommit)
				OpenColorPopup(aOptions[aEnabledOptions[HoveredSector]]);
			if(Flow.Drawing())
			{
				qm_pie_menu_ui::DrawDisc(Graphics(), PreviewCenter, CenterRadius, ui_token::color::TINT_HUD_CARD_ACTIVE.WithMultipliedAlpha(PreviewAlpha));
				for(int i = 0; i < VisibleCount; ++i)
				{
					const auto &Entry = aOptions[aEnabledOptions[i]];
					const bool Highlighted = i == HoveredSector || i == PopupSectorIndex;
					const float StartAngle = PreviewStartAngle + AnglePerSector * i + PreviewSectorGap * 0.5f;
					const float EndAngle = StartAngle + AnglePerSector - PreviewSectorGap;
					const ColorRGBA Color = qm_pie_menu_ui::OptionColor(color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), Highlighted,
						color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmPieMenuSelectedColor, true)))
									.WithMultipliedAlpha(PreviewAlpha);
					qm_pie_menu_ui::DrawSector(Graphics(), PreviewCenter, InnerRadius, BaseOuterRadius, StartAngle, EndAngle, PreviewSectorGap, Color);
					const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
					const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * ((InnerRadius + BaseOuterRadius) * 0.5f);
					const float IconSize = BaseOuterRadius * 0.19f;
					QmDrawPieSettingsPreviewIcon(*Ui(), ItemPos, IconSize, Entry.m_pIcon, PreviewAlpha);
				}
				const int FocusedSector = HoveredSector >= 0 ? HoveredSector : PopupSectorIndex;
				const char *pCenterTitle = FocusedSector >= 0 ? aOptions[aEnabledOptions[FocusedSector]].m_pName : (VisibleCount > 0 ? Localize("Click a slice to set color") : Localize("Add options below"));
				const float CenterTitleSize = maximum(BodySize, BaseOuterRadius * 0.095f);
				QmDrawPieSettingsPreviewTitle(*TextRender(), PreviewCenter, CenterTitleSize, CenterRadius, pCenterTitle);
			}
		}
	}

	const auto RenderOptionPool = [&](const char *pPoolId, const char *pHeadingText, const char *pEmptyText, float PoolHeight,
					      const std::array<int, qm_pie_menu::OPTION_COUNT> &Order, int Count, const float *pChipWidths, SQmPieOptionPoolLayout &OutLayout) {
		// 标题与池容器是两个独立布局行：标题用 <id>-heading，池本身用 <id>。
		char aHeadingId[96];
		str_format(aHeadingId, sizeof(aHeadingId), "%s-heading", pPoolId);
		if(Flow.Control(aHeadingId, BodySize, Row, RowGap))
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, aHeadingId, &Row, Localize(pHeadingText), BodySize, TEXTALIGN_ML, {}, (int)Row.w);
		// 说明文本与池容器之间的间距必须是常规行距：写 0 会让标题贴住池的上边框，
		// 两个池看起来像黏在一起的一整块。
		if(Flow.Control(pPoolId, PoolHeight, Row, RowGap))
		{
			OutLayout = QmResolvePieOptionPool(Row, Count, pChipWidths, LineHeight, BodySize);
			if(Flow.Drawing())
			{
				DrawRoundedSurface(Ui(), OutLayout.m_Container, ui_token::color::PREVIEW_BACKDROP, ui_token::color::BORDER_SUBTLE, ui_token::radius::MENU, 0.0f);
				if(OutLayout.m_Count == 0)
				{
					CUIRect EmptyRow = OutLayout.m_Container;
					SLabelProperties EmptyProps;
					EmptyProps.SetColor(ui_token::color::TEXT_DISABLED);
					Ui()->DoLabel(&EmptyRow, Localize(pEmptyText), BodySize, TEXTALIGN_MC, EmptyProps);
				}
			}
			for(int Slot = 0; Slot < OutLayout.m_Count; ++Slot)
			{
				const SPieMenuOptionEntry &Entry = aOptions[Order[Slot]];
				const bool Dragged = Ui()->IsActiveItem(&s_PieOptionPoolDrag) && s_PieOptionPoolDrag.m_Option == Order[Slot];
				if(Flow.Drawing())
					QmDrawPieOptionChip(SettingsUiContext("settings_qmclient_pie_option_pool"), OutLayout.m_aChips[Slot].m_Rect, Entry.m_pName, Entry.m_pIcon,
						color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), BodySize,
						Ui()->MouseHovered(&OutLayout.m_aChips[Slot].m_Rect), Dragged);
				// 色点/图标/名字都不是独立热区：整个胶囊只有一条 tooltip 说明「拖到另一池」。
				if(!PrewarmOnly)
					GameClient()->m_Tooltips.DoToolTip(Entry.m_pColorValue, &OutLayout.m_aChips[Slot].m_Rect, Localize("Drag to the other list to show or hide this option"));
			}
		}
	};
	RenderOptionPool("pie-pool-enabled", "In pie menu", "Drag options here to show them", MeasuredEnabledPool.m_Height, aEnabledOptions, EnabledCount, aEnabledChipWidths, EnabledPoolLayout);
	RenderOptionPool("pie-pool-disabled", "Not in pie menu", "Drag options here to hide them", MeasuredDisabledPool.m_Height, aDisabledOptions, DisabledCount, aDisabledChipWidths, DisabledPoolLayout);

	// 池间拖动：起手记录池与项，指针离开一档间距后才算拖动，松手按落点池切换配置项。
	// 只有交互趟推进状态；测量趟与只读趟保持原样，避免后台绘制把拖动「走」一遍。
	const bool HasOptionPools = EnabledPoolLayout.m_Container.w > 0.0f && DisabledPoolLayout.m_Container.w > 0.0f;
	if(CanCommit && s_PieOptionPoolDrag.Active() &&
		(!HasOptionPools || !Ui()->CheckActiveItem(&s_PieOptionPoolDrag) || Ui()->MouseButtonClicked(0)))
	{
		// 页面、弹窗或新的按下中断旧操作时，放弃旧选项。
		if(Ui()->IsActiveItem(&s_PieOptionPoolDrag))
			Ui()->SetActiveItem(nullptr);
		s_PieOptionPoolDrag.Reset();
	}
	if(CanCommit && HasOptionPools)
	{
		const auto HitTestPool = [&](const SQmPieOptionPoolLayout &Pool, const std::array<int, qm_pie_menu::OPTION_COUNT> &Order, int &Option) {
			for(int Slot = 0; Slot < Pool.m_Count; ++Slot)
			{
				if(Ui()->MouseHovered(&Pool.m_aChips[Slot].m_Rect))
				{
					Option = Order[Slot];
					return true;
				}
			}
			return false;
		};
		int HoverOption = -1;
		int HoverPool = -1;
		if(HitTestPool(EnabledPoolLayout, aEnabledOptions, HoverOption))
			HoverPool = 0;
		else if(HitTestPool(DisabledPoolLayout, aDisabledOptions, HoverOption))
			HoverPool = 1;
		int HoverContainer = -1;
		if(HoverPool < 0)
		{
			if(Ui()->MouseHovered(&EnabledPoolLayout.m_Container))
				HoverContainer = 0;
			else if(Ui()->MouseHovered(&DisabledPoolLayout.m_Container))
				HoverContainer = 1;
		}
		if(!s_PieOptionPoolDrag.Active() && Ui()->MouseButtonClicked(0) && HoverPool >= 0 && HoverOption >= 0)
		{
			Ui()->SetActiveItem(&s_PieOptionPoolDrag);
			s_PieOptionPoolDrag.m_Pool = HoverPool;
			s_PieOptionPoolDrag.m_Option = HoverOption;
			s_PieOptionPoolDrag.m_Start = Ui()->MousePos();
			const SQmPieOptionPoolLayout &SourcePool = HoverPool == 0 ? EnabledPoolLayout : DisabledPoolLayout;
			s_PieOptionPoolDrag.m_GhostRect = {0.0f, 0.0f, 0.0f, 0.0f};
			for(int Slot = 0; Slot < SourcePool.m_Count; ++Slot)
			{
				const int Option = HoverPool == 0 ? aEnabledOptions[Slot] : aDisabledOptions[Slot];
				if(Option == HoverOption)
				{
					s_PieOptionPoolDrag.m_GhostRect = SourcePool.m_aChips[Slot].m_Rect;
					s_PieOptionPoolDrag.m_GrabOffset = Ui()->MousePos() - vec2(SourcePool.m_aChips[Slot].m_Rect.x, SourcePool.m_aChips[Slot].m_Rect.y);
					break;
				}
			}
			s_PieOptionPoolDrag.m_Dragging = false;
		}
		if(s_PieOptionPoolDrag.Active() && Ui()->MouseButton(0))
		{
			if(!s_PieOptionPoolDrag.m_Dragging && distance(Ui()->MousePos(), s_PieOptionPoolDrag.m_Start) > ui_token::spacing::XS)
				s_PieOptionPoolDrag.m_Dragging = true;
		}
		else if(s_PieOptionPoolDrag.Active())
		{
			// 落点优先看胶囊，其次看池容器：拖到池的空白处也算那一池。
			int TargetPool = HoverPool >= 0 ? HoverPool : HoverContainer;
			if(TargetPool < 0)
				TargetPool = s_PieOptionPoolDrag.m_Pool;
			if(s_PieOptionPoolDrag.m_Option >= 0 && TargetPool != s_PieOptionPoolDrag.m_Pool)
				*aOptions[s_PieOptionPoolDrag.m_Option].m_pEnabled = TargetPool == 0 ? 1 : 0;
			else if(!s_PieOptionPoolDrag.m_Dragging && s_PieOptionPoolDrag.m_Option >= 0)
			{
				// 原地单击等价于切换归属：不拖也能用，键盘/触控板一样有路径。
				int *pEnabled = aOptions[s_PieOptionPoolDrag.m_Option].m_pEnabled;
				*pEnabled = *pEnabled ? 0 : 1;
			}
			Ui()->SetActiveItem(nullptr);
			s_PieOptionPoolDrag.Reset();
		}
	}

	// 拖动中的幽灵胶囊与落点池高亮：画在最后，压在两池之上。
	if(Flow.Drawing() && Ui()->IsActiveItem(&s_PieOptionPoolDrag) && s_PieOptionPoolDrag.m_Dragging)
	{
		int TargetPool = -1;
		if(Ui()->MouseHovered(&EnabledPoolLayout.m_Container))
			TargetPool = 0;
		else if(Ui()->MouseHovered(&DisabledPoolLayout.m_Container))
			TargetPool = 1;
		if(TargetPool >= 0)
		{
			const CUIRect &TargetRect = TargetPool == 0 ? EnabledPoolLayout.m_Container : DisabledPoolLayout.m_Container;
			DrawRoundedSurface(Ui(), TargetRect, ui_token::color::COLOR_TRANSPARENT, ui_token::feedback::ACTIVE_BORDER, ui_token::radius::MENU, ui_token::feedback::ICON_BORDER_WIDTH);
		}
		if(s_PieOptionPoolDrag.m_Option >= 0 && s_PieOptionPoolDrag.m_GhostRect.w > 0.0f)
		{
			const SPieMenuOptionEntry &Entry = aOptions[s_PieOptionPoolDrag.m_Option];
			CUIRect Ghost = s_PieOptionPoolDrag.m_GhostRect;
			Ghost.x = Ui()->MousePos().x - s_PieOptionPoolDrag.m_GrabOffset.x;
			Ghost.y = Ui()->MousePos().y - s_PieOptionPoolDrag.m_GrabOffset.y;
			CUIRect GhostShadow = Ghost;
			GhostShadow.x += ui_token::elevation::SHADOW_X_LOW;
			GhostShadow.y += ui_token::elevation::SHADOW_Y_MED;
			DrawRoundedSurface(Ui(), GhostShadow, ui_token::color::SURFACE_SHADOW, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::PILL);
			QmDrawPieOptionChip(SettingsUiContext("settings_qmclient_pie_option_pool"), Ghost, Entry.m_pName, Entry.m_pIcon,
				color_cast<ColorRGBA>(ColorHSLA(*Entry.m_pColorValue, Entry.m_Alpha)), BodySize, true, true);
		}
	}
	if(CanCommit && g_Config.m_QmPerfDebug)
		QmPerfLogStage("perf/qmclient", "pie_menu_controls", ControlsTimer.ElapsedMs(), false, Client(), nullptr, nullptr, aLayoutExtra);
}
