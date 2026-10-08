#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

qm_card_catalog::STClientCardResult CMenus::RunTClientStatusBarCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	IUiContext TClientStatusSchemeTextInputCtx = SettingsUiContext("settings_tclient_status_scheme_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientStatusSchemeTextInputCtx.m_pAnim = nullptr;
		TClientStatusSchemeTextInputCtx.m_pTree = nullptr;
	}

	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;
	static CLineInput s_StatusScheme(g_Config.m_QmStatusBarScheme, sizeof(g_Config.m_QmStatusBarScheme));
	const int StatusBarCodeCount = (int)GameClient()->m_StatusBar.m_vStatusItemTypes.size();
	const int StatusBarItemCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
	const float SettingsContentHeight = LineSize * 7.0f + HeadlineHeight * 2.0f + ColorPickerLineSize * 2.0f + MarginSmall * 10.0f;
	const float StatusBarPreviewHeight = LineSize + MarginSmall * 2.0f;

	auto GetStatusBarEditorLabel = [](const CStatusItem *pItem) {
		return str_comp(pItem->m_aName, "Space") == 0 ? pItem->m_aName : pItem->m_aDisplayName;
	};
	auto RenderStatusBarPreview = [&](CUIRect PreviewRect, int MaxItems = -1) {
		DrawRoundedSurface(Ui(), PreviewRect, ColorRGBA(0, 0, 0, 0.5f), ColorRGBA(), 5.0f);
		PreviewRect.VSplitLeft(MarginExtraSmall, nullptr, &PreviewRect);
		const int TotalCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
		const int PreviewCount = MaxItems > 0 ? minimum(TotalCount, MaxItems) : TotalCount;
		if(TotalCount <= 0 || PreviewCount <= 0)
		{
			PreviewRect.Margin(10.0f, &PreviewRect);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-empty-preview", &PreviewRect, Localize("No status bar items"), FontSize, TEXTALIGN_ML);
			return;
		}

		const float ItemWidth = (PreviewRect.w - MarginSmall) / (float)PreviewCount;
		CUIRect PreviewItem;
		for(int i = 0; i < PreviewCount; ++i)
		{
			PreviewRect.VSplitLeft(ItemWidth, &PreviewItem, &PreviewRect);
			PreviewItem.HMargin(MarginSmall, &PreviewItem);
			PreviewItem.VMargin(MarginExtraSmall, &PreviewItem);
			DrawRoundedSurface(Ui(), PreviewItem, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), ColorRGBA(), 5.0f);
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &PreviewItem, Localize(GetStatusBarEditorLabel(GameClient()->m_StatusBar.m_StatusBarItems[i])), FontSize, TEXTALIGN_MC);
		}
	};
	auto RenderStatusBarCodes = [&](CUIRect View) {
		static std::vector<std::string> s_vCodeStorage;
		static std::vector<const char *> s_vCodes;
		static char s_aCodeLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
		if(s_vCodeStorage.size() != GameClient()->m_StatusBar.m_vStatusItemTypes.size() || str_comp(s_aCodeLanguage, g_Config.m_ClLanguagefile) != 0)
		{
			s_vCodeStorage.clear();
			s_vCodes.clear();
			s_vCodeStorage.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			s_vCodes.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			for(const CStatusItem &Item : GameClient()->m_StatusBar.m_vStatusItemTypes)
			{
				char aCode[256];
				const char *pLetters = str_comp(Item.m_aName, "Space") == 0 ? "_ or ' '" : Item.m_aLetters;
				str_format(aCode, sizeof(aCode), "%s = %s", pLetters, Localize(GetStatusBarEditorLabel(&Item)));
				s_vCodeStorage.emplace_back(aCode);
			}
			for(const std::string &Code : s_vCodeStorage)
				s_vCodes.push_back(Code.c_str());
			str_copy(s_aCodeLanguage, g_Config.m_ClLanguagefile);
		}
		const char *const *apCodes = s_vCodes.data();
		CUIRect Label;
		if(View.w > 360.0f)
		{
			CUIRect LeftCodes, RightCodes;
			View.VSplitMid(&LeftCodes, &RightCodes, MarginSmall);
			const int LeftCount = (StatusBarCodeCount + 1) / 2;
			for(int i = 0; i < StatusBarCodeCount; ++i)
			{
				CUIRect &Column = i < LeftCount ? LeftCodes : RightCodes;
				Column.HSplitTop(LineSize, &Label, &Column);
				Ui()->DoLabel(&Label, apCodes[i], FontSize, TEXTALIGN_ML);
				if(i + 1 < (i < LeftCount ? LeftCount : StatusBarCodeCount))
					Column.HSplitTop(MarginSmall, nullptr, &Column);
			}
		}
		else
		{
			for(int i = 0; i < StatusBarCodeCount; ++i)
			{
				View.HSplitTop(LineSize, &Label, &View);
				Ui()->DoLabel(&Label, apCodes[i], FontSize, TEXTALIGN_ML);
				if(i + 1 < StatusBarCodeCount)
					View.HSplitTop(MarginSmall, nullptr, &View);
			}
		}
	};
	const auto MeasureItems = [StatusBarCodeCount](const float ContentWidth) {
		const int Rows = ResolveSettingsStatusCodeRows(StatusBarCodeCount, ContentWidth);
		return Rows * LineSize + maximum(0, Rows - 1) * MarginSmall;
	};

	const auto MeasureSettings = [SettingsContentHeight](float) { return SettingsContentHeight; };
	const auto RenderSettings = [this, ReadOnly](CUIRect &View) {
		CPerfTimer SectionsTimer;
		CUIRect CheckBoxRect, Button, Label;
		CTClientSettingsRowAllocator Rows(View);
		CheckBoxRect = Rows.Next();
		// 禅模式接管状态栏时：灰化、拒绝点击并提示接管来源（与 ReadOnly 同样只显示标签）。
		const char *pStatusBarOverrideTooltip = TemporaryOverrideTooltip(&g_Config.m_QmStatusBar);
		SLabelProperties StatusBarLabelProps;
		if(pStatusBarOverrideTooltip != nullptr)
		{
			StatusBarLabelProps.SetColor(ui_token::color::TEXT_DISABLED);
			if(!m_MenuTextPlanCollecting)
			{
				// 接管时该行只画标签、没有控件占 hover，先补一次只读按钮逻辑让提示能激活。
				Ui()->DoButtonLogic(&g_Config.m_QmStatusBar, 0, &CheckBoxRect, BUTTONFLAG_LEFT);
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmStatusBar, &CheckBoxRect, pStatusBarOverrideTooltip);
			}
		}
		if(!ReadOnly && pStatusBarOverrideTooltip == nullptr && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_QmStatusBar, "tclient-statusbar-show", Localize("Show status bar"), g_Config.m_QmStatusBar, &CheckBoxRect))
			g_Config.m_QmStatusBar ^= 1;
		else if(ReadOnly || pStatusBarOverrideTooltip != nullptr)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-show", &CheckBoxRect, Localize("Show status bar"), FontSize, TEXTALIGN_ML, StatusBarLabelProps);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_QmStatusBarLabels, "tclient-statusbar-show-labels", Localize("Show labels on status bar items"), g_Config.m_QmStatusBarLabels, &CheckBoxRect))
			g_Config.m_QmStatusBarLabels ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-show-labels", &CheckBoxRect, Localize("Show labels on status bar items"), FontSize, TEXTALIGN_ML);
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-height", &g_Config.m_QmStatusBarHeight, &g_Config.m_QmStatusBarHeight, &Button, Localize("Status bar height"), 1, 16);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-height", &Button, Localize("Status bar height"), FontSize, TEXTALIGN_ML);
		Label = Rows.Next(HeadlineHeight);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-local-time-title", &Label, Localize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_QmStatusBar12HourClock, "tclient-statusbar-12-hour-clock", Localize("Use 12 hour clock"), g_Config.m_QmStatusBar12HourClock, &CheckBoxRect))
			g_Config.m_QmStatusBar12HourClock ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-12-hour-clock", &CheckBoxRect, Localize("Use 12 hour clock"), FontSize, TEXTALIGN_ML);
		CheckBoxRect = Rows.Next();
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &g_Config.m_QmStatusBarLocalTimeSeconds, "tclient-statusbar-seconds", Localize("Show seconds on clock"), g_Config.m_QmStatusBarLocalTimeSeconds, &CheckBoxRect))
			g_Config.m_QmStatusBarLocalTimeSeconds ^= 1;
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-seconds", &CheckBoxRect, Localize("Show seconds on clock"), FontSize, TEXTALIGN_ML);
		Label = Rows.Next(HeadlineHeight);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-colors-title", &Label, Localize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
		if(!ReadOnly)
		{
			static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;
			CUIRect ColorRow = Rows.Next(ColorPickerLineSize);
			DoLine_ColorPicker(&s_StatusbarColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Status bar color"), &g_Config.m_QmStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
			ColorRow = Rows.Next(ColorPickerLineSize);
			DoLine_ColorPicker(&s_StatusbarTextColor, CurrentSettingsContentMetrics(), &ColorRow, Localize("Text color"), &g_Config.m_QmStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}
		else
		{
			Label = Rows.Next(ColorPickerLineSize);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-color", &Label, Localize("Status bar color"), FontSize, TEXTALIGN_ML);
			Label = Rows.Next(ColorPickerLineSize);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-color", &Label, Localize("Text color"), FontSize, TEXTALIGN_ML);
		}
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-alpha", &g_Config.m_QmStatusBarAlpha, &g_Config.m_QmStatusBarAlpha, &Button, Localize("Status bar alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-alpha", &Button, Localize("Status bar alpha"), FontSize, TEXTALIGN_ML);
		Button = Rows.Next();
		if(!ReadOnly)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-alpha", &g_Config.m_QmStatusBarTextAlpha, &g_Config.m_QmStatusBarTextAlpha, &Button, Localize("Text alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-text-alpha", &Button, Localize("Text alpha"), FontSize, TEXTALIGN_ML);
		LogTClientPerfStageEx("tclient_statusbar", "sections", ETClientSettingsPerfStage::INTERACTIVE_LAYER, SectionsTimer.ElapsedMs());
	};
	const auto MeasurePreview = [&MeasureItems, StatusBarCodeCount, StatusBarItemCount, StatusBarPreviewHeight](const float ContentWidth) {
		float Height = 0.0f;
		if(s_SelectedItem >= 0 && s_SelectedItem < StatusBarItemCount && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusBarCodeCount)
			Height += LineSize + MarginSmall;
		Height += StatusBarPreviewHeight + MarginSmall;
		Height += LineSize + MarginSmall;
		Height += LineSize + MarginSmall;
		Height += HeadlineHeight + MarginSmall;
		return Height + MeasureItems(ContentWidth);
	};
	const auto RenderPreview = [this, &TClientStatusSchemeTextInputCtx, &GetStatusBarEditorLabel, &RenderStatusBarCodes, &RenderStatusBarPreview, Page, ReadOnly, StatusBarPreviewHeight](CUIRect &Content) {
		CPerfTimer EditorTimer;
		const int StatusItemTypeCount = (int)GameClient()->m_StatusBar.m_vStatusItemTypes.size();
		if(s_TypeSelectedOld >= StatusItemTypeCount)
			s_TypeSelectedOld = -1;
		if(s_SelectedItem >= (int)GameClient()->m_StatusBar.m_StatusBarItems.size())
			s_SelectedItem = -1;
		CUIRect StatusScheme, StatusButtons, ItemLabel, CodesTitle, StatusBar;
		if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0)
		{
			Content.HSplitTop(LineSize, &ItemLabel, &Content);
			Ui()->DoLabel(&ItemLabel, Localize(GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld].m_aDesc), FontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}
		Content.HSplitTop(StatusBarPreviewHeight, &StatusBar, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &StatusButtons, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &StatusScheme, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		Content.HSplitTop(HeadlineHeight, &CodesTitle, &Content);
		Content.HSplitTop(MarginSmall, nullptr, &Content);

		CUIRect DropDownRect, AddButton, RemoveButton;
		StatusButtons.VSplitMid(&DropDownRect, &StatusButtons, MarginSmall);
		StatusButtons.VSplitMid(&AddButton, &RemoveButton, MarginSmall);
		static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
		CUIRect SchemeLabel, SchemeInput, ApplyButton;
		const float SchemeLabelWidth = std::min(StatusScheme.w * 0.28f, LineSize * 5.0f);
		StatusScheme.VSplitLeft(SchemeLabelWidth, &SchemeLabel, &StatusScheme);
		StatusScheme.VSplitLeft(MarginSmall, nullptr, &StatusScheme);
		const float ApplyWidth = std::min(StatusScheme.w * 0.30f, std::max(LineSize * 2.5f, StatusScheme.w * 0.18f));
		StatusScheme.VSplitRight(ApplyWidth, &SchemeInput, &ApplyButton);
		SchemeInput.VSplitRight(MarginSmall, &SchemeInput, nullptr);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_ApplyButton, "tclient-statusbar-apply-scheme", Localize("Apply"), 0, &ApplyButton))
		{
			GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_QmStatusBarScheme);
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_QmStatusBarScheme);
			s_SelectedItem = -1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-apply-scheme", &ApplyButton, Localize("Apply"), FontSize, TEXTALIGN_MC);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-scheme-label", &SchemeLabel, Localize("Status Scheme:"), FontSize, TEXTALIGN_MR);
		s_StatusScheme.SetEmptyText("");
		if(!ReadOnly)
			ui_widget::InputField(TClientStatusSchemeTextInputCtx, &s_StatusScheme, SchemeInput, nullptr, EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-scheme-value", &SchemeInput, g_Config.m_QmStatusBarScheme, FontSize, TEXTALIGN_ML);

		static std::vector<std::string> s_DropDownNameStorage;
		static std::vector<const char *> s_DropDownNames;
		static char s_aDropDownLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
		if(s_DropDownNameStorage.size() != GameClient()->m_StatusBar.m_vStatusItemTypes.size() || str_comp(s_aDropDownLanguage, g_Config.m_ClLanguagefile) != 0)
		{
			s_DropDownNameStorage.clear();
			s_DropDownNames.clear();
			s_DropDownNameStorage.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			s_DropDownNames.reserve(GameClient()->m_StatusBar.m_vStatusItemTypes.size());
			for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_vStatusItemTypes)
			{
				s_DropDownNameStorage.emplace_back(Localize(GetStatusBarEditorLabel(&StatusItemType)));
			}
			for(const std::string &Name : s_DropDownNameStorage)
				s_DropDownNames.push_back(Name.c_str());
			str_copy(s_aDropDownLanguage, g_Config.m_ClLanguagefile);
		}
		if(!ReadOnly)
		{
			static CUi::SDropDownState s_DropDownState;
			static CScrollRegion s_DropDownScrollRegion;
			s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
			CUi::SDropDownProperties DropDownProps;
			DropDownProps.m_pPopupViewport = &Page.m_ScrollViewport;
			const int TypeSelectedNew = DoSettingsDropDown(&DropDownRect, s_TypeSelectedOld, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState, DropDownProps);
			if(s_TypeSelectedOld != TypeSelectedNew)
			{
				s_TypeSelectedOld = TypeSelectedNew;
				if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusItemTypeCount)
				{
					GameClient()->m_StatusBar.m_StatusBarItems[s_SelectedItem] = &GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld];
					GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_QmStatusBarScheme);
				}
			}
		}
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-item-type", &DropDownRect, Localize("Item type"), FontSize, TEXTALIGN_MC);
		const size_t NumItems = GameClient()->m_StatusBar.m_StatusBarItems.size();
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_AddButton, "tclient-statusbar-add-item", Localize("Add Item"), 0, &AddButton) && s_TypeSelectedOld >= 0 && s_TypeSelectedOld < StatusItemTypeCount && NumItems < 128)
		{
			GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_vStatusItemTypes[s_TypeSelectedOld]);
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_QmStatusBarScheme);
			s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-add-item", &AddButton, Localize("Add Item"), FontSize, TEXTALIGN_MC);
		if(!ReadOnly && DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, &s_RemoveButton, "tclient-statusbar-remove-item", Localize("Remove Item"), 0, &RemoveButton) && s_SelectedItem >= 0)
		{
			if(s_SelectedItem < (int)GameClient()->m_StatusBar.m_StatusBarItems.size())
			{
				GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
				GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_QmStatusBarScheme);
			}
			s_SelectedItem = -1;
		}
		else if(ReadOnly)
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-remove-item", &RemoveButton, Localize("Remove Item"), FontSize, TEXTALIGN_MC);

		const int ItemCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
		if(ItemCount <= 0)
		{
			RenderStatusBarPreview(StatusBar);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-codes-title", &CodesTitle, Localize("Status Bar Codes"), HeadlineFontSize, TEXTALIGN_ML);
			RenderStatusBarCodes(Content);
			LogTClientPerfStageEx("tclient_statusbar", "editor", ETClientSettingsPerfStage::STATIC_LAYER, EditorTimer.ElapsedMs());
			return;
		}
		DrawRoundedSurface(Ui(), StatusBar, ColorRGBA(0, 0, 0, 0.5f), ColorRGBA(), 5.0f);
		const float ItemWidth = (StatusBar.w - MarginSmall) / (float)ItemCount;
		StatusBar.VSplitLeft(MarginExtraSmall, nullptr, &StatusBar);
		static std::vector<CButtonContainer *> s_pItemButtons;
		static std::vector<CButtonContainer> s_ItemButtons;
		static vec2 s_ActivePos = vec2(0.0f, 0.0f);
		class CSwapItem
		{
		public:
			vec2 m_InitialPosition = vec2(0.0f, 0.0f);
			float m_Duration = 0.0f;
		};
		static std::vector<CSwapItem> s_ItemSwaps;
		if((int)s_ItemButtons.size() != ItemCount)
		{
			s_ItemSwaps.resize(ItemCount);
			s_pItemButtons.resize(ItemCount);
			s_ItemButtons.resize(ItemCount);
			for(int i = 0; i < ItemCount; ++i)
				s_pItemButtons[i] = &s_ItemButtons[i];
		}
		bool StatusItemActive = false;
		int HotStatusIndex = 0;
		if(!ReadOnly)
		{
			for(int i = 0; i < ItemCount; ++i)
			{
				if(Ui()->ActiveItem() == s_pItemButtons[i])
				{
					StatusItemActive = true;
					HotStatusIndex = i;
				}
			}
		}
		CUIRect StatusItemButton;
		for(int i = 0; i < ItemCount; ++i)
		{
			StatusBar.VSplitLeft(ItemWidth, &StatusItemButton, &StatusBar);
			StatusItemButton.HMargin(MarginSmall, &StatusItemButton);
			StatusItemButton.VMargin(MarginExtraSmall, &StatusItemButton);
			CStatusItem *pStatusItem = GameClient()->m_StatusBar.m_StatusBarItems[i];
			const ColorRGBA Color = s_SelectedItem == i ? ColorRGBA(1.0f, 0.35f, 0.35f, 0.75f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
			CUIRect TempItemButton = StatusItemButton;
			if(!ReadOnly && StatusItemActive && Ui()->ActiveItem() != s_pItemButtons[i])
			{
				CUIRect FullHeightItemButton = StatusItemButton;
				FullHeightItemButton.y = 0.0f;
				FullHeightItemButton.h = 10000.0f;
				if(Ui()->MouseInside(&FullHeightItemButton))
				{
					std::swap(s_pItemButtons[i], s_pItemButtons[HotStatusIndex]);
					std::swap(GameClient()->m_StatusBar.m_StatusBarItems[i], GameClient()->m_StatusBar.m_StatusBarItems[HotStatusIndex]);
					s_SelectedItem = -2;
					s_ItemSwaps[HotStatusIndex].m_InitialPosition = vec2(StatusItemButton.x, StatusItemButton.y);
					s_ItemSwaps[HotStatusIndex].m_Duration = 0.15f;
					s_ItemSwaps[i].m_InitialPosition = vec2(s_ActivePos.x, s_ActivePos.y);
					s_ItemSwaps[i].m_Duration = 0.15f;
					GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_QmStatusBarScheme);
				}
			}
			if(!ReadOnly)
			{
				s_ItemSwaps[i].m_Duration = std::max(0.0f, s_ItemSwaps[i].m_Duration - Client()->RenderFrameTime());
				if(s_ItemSwaps[i].m_Duration > 0.0f)
				{
					const float Progress = std::pow(2.0, -5.0 * (1.0 - s_ItemSwaps[i].m_Duration / 0.15f));
					TempItemButton.x = mix(TempItemButton.x, s_ItemSwaps[i].m_InitialPosition.x, Progress);
				}
			}
			if(!ReadOnly && DoButtonLineSize_Menu(s_pItemButtons[i], Localize(GetStatusBarEditorLabel(pStatusItem)), 0, &TempItemButton, LineSize, false, 0, IGraphics::CORNER_ALL, ui_token::radius::BASE, 0.0f, Color))
			{
				if(s_SelectedItem == -2)
				{
					s_SelectedItem++;
				}
				else if(s_SelectedItem != i)
				{
					s_SelectedItem = i;
					for(int TypeIndex = 0; TypeIndex < StatusItemTypeCount; ++TypeIndex)
						if(str_comp(GameClient()->m_StatusBar.m_vStatusItemTypes[TypeIndex].m_aName, pStatusItem->m_aName) == 0)
							s_TypeSelectedOld = TypeIndex;
				}
				else
				{
					s_SelectedItem = -1;
					s_TypeSelectedOld = -1;
				}
			}
			else if(ReadOnly)
				DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-preview-item", &StatusItemButton, Localize(GetStatusBarEditorLabel(pStatusItem)), FontSize, TEXTALIGN_MC);
			if(!ReadOnly && Ui()->ActiveItem() == s_pItemButtons[i])
				s_ActivePos = vec2(StatusItemButton.x, StatusItemButton.y);
		}
		if(!ReadOnly && !StatusItemActive)
			s_SelectedItem = std::max(-1, s_SelectedItem);
		DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, "tclient-statusbar-codes-title", &CodesTitle, Localize("Status Bar Codes"), HeadlineFontSize, TEXTALIGN_ML);
		RenderStatusBarCodes(Content);
		LogTClientPerfStageEx("tclient_statusbar", "editor", ETClientSettingsPerfStage::STATIC_LAYER, EditorTimer.ElapsedMs());
	};
	const uint64_t StatusLayoutRevision = ((uint64_t)GameClient()->m_StatusBar.m_vStatusItemTypes.size() << 32) ^
					      (uint64_t)GameClient()->m_StatusBar.m_StatusBarItems.size() ^
					      ((uint64_t)(s_SelectedItem + 2) << 16) ^ (uint64_t)(s_TypeSelectedOld + 2);
	const uint64_t CardRevision = StatusLayoutRevision;
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-status-bar-settings") == 0)
	{
		const float Height = MeasureSettings(Content.w);
		if(Pass == qm_card_catalog::ETClientCardPass::RENDER)
			RenderSettings(Content);
		return {Height, CardRevision};
	}
	if(str_comp(pStableId, "deck:tclient-status-bar-preview") == 0)
	{
		const float Height = MeasurePreview(Content.w);
		if(Pass == qm_card_catalog::ETClientCardPass::RENDER)
			RenderPreview(Content);
		return {Height, CardRevision};
	}
	return {0.0f, CardRevision};
}
