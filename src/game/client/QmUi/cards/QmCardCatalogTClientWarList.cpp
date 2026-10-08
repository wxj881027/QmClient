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

qm_card_catalog::STClientCardResult CMenus::RunTClientWarListCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CPerfTimer RenderTimer;
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	const SSettingsContentMetrics WarListMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float ListRowHeight = WarListMetrics.m_ListRowHeight;
	constexpr int WarListViewportRows = 8;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());

	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static bool s_IsClan = false;
	static bool s_IsName = true;
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = nullptr;
	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ColorRGBA(1, 1, 1, 1);
	static CLineInputBuffered<128> s_EntriesFilterInput;
	static CLineInputBuffered<128> s_PlayerSearchInput;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	static CLineInput s_ReasonInput;
	static CLineInput s_TypeNameInput;
	CWarEntry *pPreviewSelectedEntry = s_pSelectedEntry;
	CWarType *pPreviewSelectedType = s_pSelectedType;
	CWarEntry *&pSelectedEntry = ReadOnly ? pPreviewSelectedEntry : s_pSelectedEntry;
	CWarType *&pSelectedType = ReadOnly ? pPreviewSelectedType : s_pSelectedType;

	auto WarTypeExists = [&](const CWarType *pType) {
		return std::find(GameClient()->m_WarList.m_WarTypes.begin(), GameClient()->m_WarList.m_WarTypes.end(), pType) != GameClient()->m_WarList.m_WarTypes.end();
	};
	auto DefaultWarType = [&]() -> CWarType * {
		return GameClient()->m_WarList.m_WarTypes.empty() ? nullptr : GameClient()->m_WarList.m_WarTypes[0];
	};
	if(pSelectedType == nullptr || !WarTypeExists(pSelectedType))
		pSelectedType = DefaultWarType();
	auto WarEntryExists = [&](const CWarEntry *pEntry) {
		return pEntry != nullptr && std::find_if(GameClient()->m_WarList.m_vWarEntries.begin(), GameClient()->m_WarList.m_vWarEntries.end(),
						    [pEntry](const CWarEntry &Entry) { return &Entry == pEntry; }) != GameClient()->m_WarList.m_vWarEntries.end();
	};
	if(!WarEntryExists(pSelectedEntry))
		pSelectedEntry = nullptr;

	IUiContext TClientWarListTextInputCtx = SettingsUiContext("settings_tclient_warlist_text_inputs", UiScale);
	if(ReadOnly)
	{
		TClientWarListTextInputCtx.m_pAnim = nullptr;
		TClientWarListTextInputCtx.m_pTree = nullptr;
	}
	IUiContext TClientWarListEntriesSearchCtx = TClientWarListTextInputCtx;
	TClientWarListEntriesSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_warlist_entries_search");
	IUiContext TClientWarListPlayerSearchCtx = TClientWarListTextInputCtx;
	TClientWarListPlayerSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_warlist_player_search");

	auto RenderEntries = [&](CUIRect &Column) {
		CPerfTimer ListTimer;
		CUIRect Button, EntriesSearch;
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitRight(25.0f, nullptr, &Button);

		static CButtonContainer s_ReverseEntries;
		static bool s_Reversed = true;
		if(!ReadOnly && Ui()->DoButton_QmIcon(&s_ReverseEntries, s_Reversed ? EQmIcon::CHEVRON_UP : EQmIcon::CHEVRON_DOWN, s_Reversed ? FONT_ICON_CHEVRON_UP : FONT_ICON_CHEVRON_DOWN, 0, &Button, IGraphics::CORNER_ALL))
			s_Reversed = !s_Reversed;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &EntriesSearch, &Column);
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, EntriesSearch, FontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-entries-search", &EntriesSearch, s_EntriesFilterInput.GetString(), FontSize, TEXTALIGN_ML);
		static std::vector<CWarEntry *> s_vFilteredEntries;
		static char s_aCachedEntriesFilter[128] = "";
		static bool s_CachedReversed = false;
		static int s_CachedWarEntriesRevision = -1;
		if(str_comp(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString()) != 0 ||
			s_CachedReversed != s_Reversed ||
			s_CachedWarEntriesRevision != s_TClientWarListFilterRevision)
		{
			s_vFilteredEntries.clear();
			for(CWarEntry &Entry : GameClient()->m_WarList.m_vWarEntries)
			{
				if(str_find_nocase(Entry.m_aName, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
				else if(str_find_nocase(Entry.m_aClan, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
				else if(str_find_nocase(Entry.m_pWarType->m_aWarName, s_EntriesFilterInput.GetString()))
					s_vFilteredEntries.push_back(&Entry);
			}
			if(s_Reversed)
				std::reverse(s_vFilteredEntries.begin(), s_vFilteredEntries.end());
			str_copy(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString(), sizeof(s_aCachedEntriesFilter));
			s_CachedReversed = s_Reversed;
			s_CachedWarEntriesRevision = s_TClientWarListFilterRevision;
		}

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		static CListBox s_EntriesReadOnlyListBox;
		CListBox &EntriesListBox = ReadOnly ? s_EntriesReadOnlyListBox : s_EntriesListBox;
		EntriesListBox.SetActive(!ReadOnly);
		EntriesListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		EntriesListBox.DoStart(ListRowHeight, s_vFilteredEntries.size(), 1, 2, SelectedOldEntry, &Column);

		static std::vector<unsigned char> s_vItemIds;
		static std::vector<CButtonContainer> s_vDeleteButtons;
		const int MaxEntries = GameClient()->m_WarList.m_vWarEntries.size();
		s_vItemIds.resize(MaxEntries);
		s_vDeleteButtons.resize(MaxEntries);

		CWarEntry *pEntryToRemove = nullptr;
		for(size_t i = 0; i < s_vFilteredEntries.size(); i++)
		{
			CWarEntry *pEntry = s_vFilteredEntries[i];
			if(pSelectedEntry && pEntry == pSelectedEntry)
				SelectedOldEntry = (int)i;

			const CListboxItem Item = EntriesListBox.DoNextItem(&s_vItemIds[i], SelectedOldEntry >= 0 && (size_t)SelectedOldEntry == i);
			if(!Item.m_Visible)
				continue;

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType, ToolTip;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			EntryRect.VSplitLeft(26.0f, &DeleteButton, &EntryRect);
			DeleteButton.HMargin(7.5f, &DeleteButton);
			DeleteButton.VSplitLeft(MarginSmall, nullptr, &DeleteButton);
			DeleteButton.VSplitRight(MarginExtraSmall, &DeleteButton, nullptr);
			if(!ReadOnly && Ui()->DoButton_QmIcon(&s_vDeleteButtons[i], EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
			{
				pEntryToRemove = pEntry;
			}

			bool IsClan = false;
			char aBuf[32];
			if(str_comp(pEntry->m_aClan, "") != 0)
			{
				str_copy(aBuf, pEntry->m_aClan);
				IsClan = true;
			}
			else
			{
				str_copy(aBuf, pEntry->m_aName);
			}
			EntryRect.VSplitLeft(35.0f, &EntryTypeRect, &EntryRect);
			if(!ReadOnly)
			{
				if(IsClan)
					RenderFontIcon_QmIcon(EntryTypeRect, EQmIcon::USERS, FONT_ICON_USERS, 18.0f, TEXTALIGN_MC);
				else
					RenderDevSkin(EntryTypeRect.Center(), ListRowHeight, "default", "default", false, 0, 0, 0, false, false);
			}

			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				EntryRect.VSplitRight(20.0f, &EntryRect, &ToolTip);
				if(!ReadOnly)
					RenderFontIcon_QmIcon(ToolTip, EQmIcon::COMMENT, FONT_ICON_COMMENT, 18.0f, TEXTALIGN_MC);
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &ToolTip, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			EntryRect.HMargin(MarginExtraSmall, &EntryRect);
			EntryRect.HSplitMid(&EntryRect, &WarType, MarginSmall);
			DoTClientLabel(Ui(), &EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(pEntry->m_pWarType->m_Color);
			DoTClientLabel(Ui(), &WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		const int NewSelectedEntry = EntriesListBox.DoEnd();
		if(!ReadOnly && pEntryToRemove != nullptr)
		{
			GameClient()->m_WarList.RemoveWarEntry(pEntryToRemove);
			pSelectedEntry = nullptr;
			++s_TClientWarListFilterRevision;
		}
		else if(!ReadOnly && NewSelectedEntry >= 0 && NewSelectedEntry < (int)s_vFilteredEntries.size() &&
			(SelectedOldEntry != NewSelectedEntry || (Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0))))
		{
			pSelectedEntry = s_vFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, pSelectedEntry->m_aReason);
				if(str_comp(pSelectedEntry->m_aClan, "") != 0)
				{
					s_IsName = false;
					s_IsClan = true;
				}
				else
				{
					s_IsName = true;
					s_IsClan = false;
				}
				pSelectedType = pSelectedEntry->m_pWarType;
			}
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "entries=%d filtered=%d", (int)GameClient()->m_WarList.m_vWarEntries.size(), (int)s_vFilteredEntries.size());
		LogTClientPerfStageEx("tclient_warlist", "list", ETClientSettingsPerfStage::TEXT_CACHE, ListTimer.ElapsedMs(), false, aExtra);
	};

	auto RenderEditor = [&](CUIRect &Column) {
		CPerfTimer FilterTimer;
		CUIRect Button, ButtonL, ButtonR;
		Column.HSplitTop(LineSize, &Button, &Column);

		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
		s_NameInput.SetEmptyText(Localize("Name"));
		if(!ReadOnly && s_IsName)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_NameInput, ButtonL, Localize("Name"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-name", &ButtonL, s_aEntryName, FontSize, TEXTALIGN_ML);

		s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
		s_ClanInput.SetEmptyText(Localize("Clan"));
		if(!ReadOnly && s_IsClan)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ClanInput, ButtonR, Localize("Clan"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-clan", &ButtonR, s_aEntryClan, FontSize, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		static unsigned char s_NameRadio, s_ClanRadio;
		if(!ReadOnly && DoButton_CheckBox_Common(&s_NameRadio, Localize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
		{
			s_IsName = true;
			s_IsClan = false;
		}
		if(!ReadOnly && DoButton_CheckBox_Common(&s_ClanRadio, Localize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
		{
			s_IsName = false;
			s_IsClan = true;
		}
		if(!s_IsName)
			str_copy(s_aEntryName, "");
		if(!s_IsClan)
			str_copy(s_aEntryClan, "");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
		s_ReasonInput.SetEmptyText(Localize("Reason"));
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ReasonInput, Button, Localize("Reason"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-reason", &Button, s_aEntryReason, FontSize, TEXTALIGN_ML);

		static CButtonContainer s_AddButton, s_OverrideButton;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		if(!ReadOnly && DoButtonLineSize_Menu(&s_OverrideButton, Localize("Override Entry"), 0, &ButtonL, LineSize) && pSelectedEntry)
		{
			if(pSelectedEntry && pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
			{
				str_copy(pSelectedEntry->m_aName, s_aEntryName);
				str_copy(pSelectedEntry->m_aClan, s_aEntryClan);
				str_copy(pSelectedEntry->m_aReason, s_aEntryReason);
				pSelectedEntry->m_pWarType = pSelectedType;
				++s_TClientWarListFilterRevision;
			}
		}
		if(!ReadOnly && DoButtonLineSize_Menu(&s_AddButton, Localize("Add Entry"), 0, &ButtonR, LineSize))
		{
			if(pSelectedType)
			{
				GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, pSelectedType->m_aWarName);
				pSelectedEntry = nullptr;
				++s_TClientWarListFilterRevision;
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column);
		if(pSelectedType)
		{
			float Shade = 0.0f;
			if(!ReadOnly)
				Button.Draw(ColorRGBA(Shade, Shade, Shade, 0.25f), 15, 3.0f);
			TextRender()->TextColor(pSelectedType->m_Color);
			Ui()->DoLabel(&Button, pSelectedType->m_aWarName, HeadlineFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		LogTClientPerfStageEx("tclient_warlist", "filter", ETClientSettingsPerfStage::INTERACTIVE_LAYER, FilterTimer.ElapsedMs());
	};

	auto RenderSettings = [&](CUIRect &Column) {
		CUIRect CheckBoxRect;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListAllowDuplicates, "tclient-warlist-allow-duplicates", Localize("Allow Duplicate Entries"), g_Config.m_QmWarListAllowDuplicates, &CheckBoxRect))
			g_Config.m_QmWarListAllowDuplicates ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarList, "tclient-warlist-enable", Localize("Enable warlist"), g_Config.m_QmWarList, &CheckBoxRect))
			g_Config.m_QmWarList ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListBlockEnemyChat, "tclient-warlist-block-enemy-chat", Localize("Block enemy chat"), g_Config.m_QmWarListBlockEnemyChat, &CheckBoxRect))
			g_Config.m_QmWarListBlockEnemyChat ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListChat, "tclient-warlist-colors-chat", Localize("Colors in chat"), g_Config.m_QmWarListChat, &CheckBoxRect))
			g_Config.m_QmWarListChat ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListScoreboard, "tclient-warlist-colors-scoreboard", Localize("Colors in scoreboard"), g_Config.m_QmWarListScoreboard, &CheckBoxRect))
			g_Config.m_QmWarListScoreboard ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListSpectate, "tclient-warlist-colors-spectate", Localize("Show colors in spectator selection"), g_Config.m_QmWarListSpectate, &CheckBoxRect))
			g_Config.m_QmWarListSpectate ^= 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		if(!ReadOnly && DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListShowClan, "tclient-warlist-show-clan", Localize("Show clan if war"), g_Config.m_QmWarListShowClan, &CheckBoxRect))
			g_Config.m_QmWarListShowClan ^= 1;
	};

	auto RenderGroups = [&](CUIRect &Column) {
		CPerfTimer ActionsTimer;
		CUIRect WarTypeList, Button, ButtonL, ButtonR;
		Column.HSplitTop(WarListViewportRows * ListRowHeight, &WarTypeList, &Column);
		m_pRemoveWarType = nullptr;
		int SelectedOldType = -1;
		static CListBox s_WarTypeListBox;
		static CListBox s_WarTypeReadOnlyListBox;
		CListBox &WarTypeListBox = ReadOnly ? s_WarTypeReadOnlyListBox : s_WarTypeListBox;
		WarTypeListBox.SetActive(!ReadOnly);
		WarTypeListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		WarTypeListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		WarTypeListBox.DoStart(ListRowHeight, GameClient()->m_WarList.m_WarTypes.size(), 1, 2, SelectedOldType, &WarTypeList, true, IGraphics::CORNER_ALL);

		static std::vector<unsigned char> s_vTypeItemIds;
		static std::vector<CButtonContainer> s_vTypeDeleteButtons;
		const int MaxTypes = GameClient()->m_WarList.m_WarTypes.size();
		s_vTypeItemIds.resize(MaxTypes);
		s_vTypeDeleteButtons.resize(MaxTypes);

		for(int i = 0; i < (int)GameClient()->m_WarList.m_WarTypes.size(); i++)
		{
			CWarType *pType = GameClient()->m_WarList.m_WarTypes[i];
			if(!pType)
				continue;
			if(pSelectedType && pType == pSelectedType)
				SelectedOldType = i;

			const CListboxItem Item = WarTypeListBox.DoNextItem(&s_vTypeItemIds[i], SelectedOldType >= 0 && SelectedOldType == i);
			if(!Item.m_Visible)
				continue;

			CUIRect TypeRect, DeleteButton;
			Item.m_Rect.Margin(0.0f, &TypeRect);
			if(pType->m_Removable)
			{
				TypeRect.VSplitRight(20.0f, &TypeRect, &DeleteButton);
				DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
				DeleteButton.Margin(2.0f, &DeleteButton);
				if(!ReadOnly && DoButtonNoRect_QmIcon(&s_vTypeDeleteButtons[i], EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
					m_pRemoveWarType = pType;
			}
			TextRender()->TextColor(pType->m_Color);
			DoTClientLabel(Ui(), &TypeRect, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		const int NewSelectedType = WarTypeListBox.DoEnd();
		const bool NewSelectedTypeValid = NewSelectedType >= 0 && NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size();
		if(!ReadOnly && ((SelectedOldType != NewSelectedType && NewSelectedTypeValid) || (NewSelectedTypeValid && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0))))
		{
			pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aTypeName, pSelectedType->m_aWarName);
				s_GroupColor = pSelectedType->m_Color;
			}
		}
		if(!ReadOnly && m_pRemoveWarType != nullptr)
		{
			if(m_pRemoveWarType == pSelectedType)
				pSelectedType = DefaultWarType();
			char aMessage[256];
			str_format(aMessage, sizeof(aMessage),
				Localize("Are you sure that you want to remove '%s' from your war groups?"),
				m_pRemoveWarType->m_aWarName);
			PopupConfirm(Localize("Remove War Group"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmRemoveWarType);
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
		s_TypeNameInput.SetEmptyText(Localize("Group name"));
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListTextInputCtx, &s_TypeNameInput, Button, Localize("Group name"), EditBoxFontSize);
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-group-name", &Button, s_aTypeName, FontSize, TEXTALIGN_ML);
		static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static unsigned int s_ColorValue = 0;
		s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
		if(!ReadOnly)
			s_GroupColor = color_cast<ColorRGBA>(DoLine_ColorPicker(&s_GroupColorPicker, CurrentSettingsContentMetrics(), &Column, Localize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true));
		else
			Column.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, nullptr, &Column);

		Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
		Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
		bool OverrideDisabled = NewSelectedType == 0;
		if(!ReadOnly && DoButtonLineSize_Menu(&s_OverrideGroupButton, Localize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && pSelectedType)
		{
			if(pSelectedType && str_comp(s_aTypeName, "") != 0)
			{
				str_copy(pSelectedType->m_aWarName, s_aTypeName);
				pSelectedType->m_Color = s_GroupColor;
				++s_TClientWarListFilterRevision;
			}
		}
		bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
		if(!ReadOnly && DoButtonLineSize_Menu(&s_AddGroupButton, Localize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
		{
			GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
			++s_TClientWarListFilterRevision;
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "groups=%d", (int)GameClient()->m_WarList.m_WarTypes.size());
		LogTClientPerfStageEx("tclient_warlist", "actions", ETClientSettingsPerfStage::RESOURCE_PRETRIGGER, ActionsTimer.ElapsedMs(), false, aExtra);
	};

	auto RenderPlayers = [&](CUIRect &Column) {
		CPerfTimer PlayersTimer;
		CUIRect PlayerSearch, PlayerList;
		Column.HSplitTop(LineSize, &PlayerSearch, &Column);
		if(!ReadOnly)
			ui_widget::InputField(TClientWarListPlayerSearchCtx, &s_PlayerSearchInput, PlayerSearch, FontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		else
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, "tclient-warlist-player-search", &PlayerSearch, s_PlayerSearchInput.GetString(), FontSize, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		PlayerList = Column;
		static CListBox s_PlayerListBox;
		static CListBox s_PlayerReadOnlyListBox;
		CListBox &PlayerListBox = ReadOnly ? s_PlayerReadOnlyListBox : s_PlayerListBox;
		PlayerListBox.SetActive(!ReadOnly);
		PlayerListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		PlayerListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		static std::vector<int> s_vFilteredPlayerIds;
		s_vFilteredPlayerIds.clear();
		s_vFilteredPlayerIds.reserve(MAX_CLIENTS);
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId])
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) ||
				str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
				s_vFilteredPlayerIds.push_back(ClientId);
		}
		PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size(), 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL);

		static std::vector<unsigned char> s_vPlayerItemIds;
		static std::vector<CButtonContainer> s_vNameButtons;
		static std::vector<CButtonContainer> s_vClanButtons;
		s_vPlayerItemIds.resize(MAX_CLIENTS);
		s_vNameButtons.resize(MAX_CLIENTS);
		s_vClanButtons.resize(MAX_CLIENTS);

		for(const int ClientId : s_vFilteredPlayerIds)
		{
			const auto &Client = GameClient()->m_aClients[ClientId];

			const CListboxItem Item = PlayerListBox.DoNextItem(&s_vPlayerItemIds[ClientId], false);
			if(!Item.m_Visible)
				continue;

			CUIRect PlayerRect, TeeRect, NameRect, ClanRect;
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			Item.m_Rect.Margin(0.0f, &PlayerRect);
			PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);
			PlayerRect.VSplitMid(&NameRect, &ClanRect);
			PlayerRect = NameRect;
			PlayerRect.x = TeeRect.x;
			PlayerRect.w += TeeRect.w;
			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_NameColor);
			ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vNameButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(!ReadOnly && NameButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), PlayerRect, NameButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_L);
			Ui()->DoLabel(&NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
			if(!ReadOnly && Ui()->DoButtonLogic(&s_vNameButtons[ClientId], false, &PlayerRect, BUTTONFLAG_LEFT))
			{
				s_IsName = true;
				s_IsClan = false;
				str_copy(s_aEntryName, Client.m_aName);
			}

			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_ClanColor);
			ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vClanButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(!ReadOnly && ClanButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), ClanRect, ClanButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_R);
			Ui()->DoLabel(&ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
			if(!ReadOnly && Ui()->DoButtonLogic(&s_vClanButtons[ClientId], false, &ClanRect, BUTTONFLAG_LEFT))
			{
				s_IsName = false;
				s_IsClan = true;
				str_copy(s_aEntryClan, Client.m_aClan);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
			TeeInfo.m_Size = ListRowHeight;
			if(!ReadOnly)
				RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
		}
		PlayerListBox.DoEnd();

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "players=%d filtered=%d", MAX_CLIENTS, (int)s_vFilteredPlayerIds.size());
		LogTClientPerfStageEx("tclient_warlist", "players", ETClientSettingsPerfStage::STATIC_LAYER, PlayersTimer.ElapsedMs(), false, aExtra);
	};

	const float WarListColumnMinimum = ResolveSettingsInlineRowMinimumWidth(WarListMetrics.m_LabelWidth + 2.0f * WarListMetrics.m_ButtonHeight, WarListMetrics.m_SectionGap, 1);
	const float FourColumnMinWidth = 4.0f * WarListColumnMinimum + 3.0f * WarListMetrics.m_SectionGap;
	const float TwoColumnMinWidth = 2.0f * WarListColumnMinimum + WarListMetrics.m_SectionGap;
	const float EntriesHeight = LineSize * 2.0f + MarginSmall + WarListViewportRows * ListRowHeight;
	const float GroupsHeight = WarListViewportRows * ListRowHeight + MarginSmall * 2.0f + LineSize * 3.0f + ColorPickerLineSize + ColorPickerLineSpacing;
	const float PlayersHeight = LineSize + MarginSmall + WarListViewportRows * ListRowHeight;
	const float EditorHeight = LineSize * 5.0f + MarginSmall * 5.0f + HeadlineFontSize;
	const float SettingsHeight = LineSize * 7.0f + MarginSmall * 6.0f;
	const float SectionHeaderHeight = LineSize + MarginSmall;
	const float SectionGap = WarListMetrics.m_SectionGap;
	const char *pWarEntriesTitle = Localizable("War Entries");
	const char *pSettingsTitle = Localizable("Settings");
	const char *pEditEntryTitle = Localizable("Edit Entry");
	const char *pWarGroupsTitle = Localizable("War Groups");
	const char *pOnlinePlayersTitle = Localizable("Online Players");
	const auto SectionHeight = [SectionHeaderHeight](float ContentHeight) {
		return SectionHeaderHeight + ContentHeight;
	};
	const auto WarListContentHeight = [&](float ContentWidth) {
		const float EntriesSectionHeight = SectionHeight(EntriesHeight);
		const float EditorSectionHeight = SectionHeight(EditorHeight);
		const float SettingsSectionHeight = SectionHeight(SettingsHeight);
		const float GroupsSectionHeight = SectionHeight(GroupsHeight);
		const float PlayersSectionHeight = SectionHeight(PlayersHeight);
		if(ContentWidth >= FourColumnMinWidth)
			return maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight,
				maximum(EditorSectionHeight, maximum(GroupsSectionHeight, PlayersSectionHeight)));
		if(ContentWidth >= TwoColumnMinWidth)
			return maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight, EditorSectionHeight) + SectionGap +
			       maximum(GroupsSectionHeight, PlayersSectionHeight);
		return EntriesSectionHeight + SectionGap + SettingsSectionHeight + SectionGap + EditorSectionHeight +
		       SectionGap + GroupsSectionHeight + SectionGap + PlayersSectionHeight;
	};
	const auto RenderWarListLayout = [&](CUIRect ContentRect, bool Render) {
		const auto RenderSection = [&](CUIRect &Column, const char *pTextId, const char *pTitle, float ContentHeight, const auto &RenderContent) {
			CUIRect Section, Header, Body;
			Column.HSplitTop(SectionHeight(ContentHeight), &Section, &Column);
			Section.HSplitTop(LineSize, &Header, &Body);
			DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, pTextId, &Header, Localize(pTitle), HeadlineFontSize, TEXTALIGN_ML);
			Body.HSplitTop(MarginSmall, nullptr, &Body);
			if(Render)
				RenderContent(Body);
		};
		const auto AddSectionGap = [&](CUIRect &Column) {
			Column.HSplitTop(SectionGap, nullptr, &Column);
		};

		if(ContentRect.w >= FourColumnMinWidth)
		{
			CUIRect EntriesColumn, EditorColumn, GroupsColumn, PlayersColumn;
			const float ColumnGap = SectionGap;
			const float ColumnWidth = maximum(0.0f, (ContentRect.w - ColumnGap * 3.0f) / 4.0f);
			ContentRect.VSplitLeft(ColumnWidth, &EntriesColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			ContentRect.VSplitLeft(ColumnWidth, &EditorColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			ContentRect.VSplitLeft(ColumnWidth, &GroupsColumn, &ContentRect);
			ContentRect.VSplitLeft(ColumnGap, nullptr, &ContentRect);
			PlayersColumn = ContentRect;
			RenderSection(EntriesColumn, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(EntriesColumn);
			RenderSection(EntriesColumn, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			RenderSection(EditorColumn, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			RenderSection(GroupsColumn, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			RenderSection(PlayersColumn, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
		else if(ContentRect.w >= TwoColumnMinWidth)
		{
			const float FirstRowHeight = maximum(SectionHeight(EntriesHeight) + SectionGap + SectionHeight(SettingsHeight), SectionHeight(EditorHeight));
			CUIRect FirstRow, SecondRow, EntriesColumn, EditorColumn, GroupsColumn, PlayersColumn;
			ContentRect.HSplitTop(FirstRowHeight, &FirstRow, &SecondRow);
			SecondRow.HSplitTop(SectionGap, nullptr, &SecondRow);
			FirstRow.VSplitMid(&EntriesColumn, &EditorColumn, SectionGap);
			SecondRow.VSplitMid(&GroupsColumn, &PlayersColumn, SectionGap);
			RenderSection(EntriesColumn, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(EntriesColumn);
			RenderSection(EntriesColumn, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			RenderSection(EditorColumn, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			RenderSection(GroupsColumn, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			RenderSection(PlayersColumn, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
		else
		{
			RenderSection(ContentRect, "tclient-warlist-section-entries", pWarEntriesTitle, EntriesHeight, RenderEntries);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-settings", pSettingsTitle, SettingsHeight, RenderSettings);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-editor", pEditEntryTitle, EditorHeight, RenderEditor);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-groups", pWarGroupsTitle, GroupsHeight, RenderGroups);
			AddSectionGap(ContentRect);
			RenderSection(ContentRect, "tclient-warlist-section-players", pOnlinePlayersTitle, PlayersHeight, RenderPlayers);
		}
	};

	const auto RenderWarListCard = [&](CUIRect &ContentRect) { RenderWarListLayout(ContentRect, true); };
	const uint64_t CardRevision = ((uint64_t)GameClient()->m_WarList.m_vWarEntries.size() << 32) ^ (uint64_t)GameClient()->m_WarList.m_WarTypes.size();
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-warlist") == 0)
	{
		const float Height = WarListContentHeight(Content.w);
		if(Pass == qm_card_catalog::ETClientCardPass::RENDER)
			RenderWarListCard(Content);
		return {Height, CardRevision};
	}
	return {0.0f, CardRevision};
}
