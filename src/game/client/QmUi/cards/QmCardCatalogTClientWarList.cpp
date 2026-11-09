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
#include <game/client/QmUi/QmIconSwap.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
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

#include <SDL3/SDL_audio.h>

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

namespace qm_tclient_cards
{
	float TClientDeclaredRegion(const char *pId, float Width, const std::function<void(CUIRect &)> &Render, CUIRect *pRect);
}

namespace
{
	// 删除战争分组的确认：新确认窗是居中的，不会停掉卡片渲染，而列表渲染
	// 每帧开头会清空 m_pRemoveWarType。所以触发时先把名字存下来，
	// 确认后再按名字删，不依赖那只活一帧的指针。
	CUi::SConfirmPopupContext s_RemoveWarTypeConfirm;
	bool s_RemoveWarTypePending = false;
	char s_aRemoveWarTypeName[MAX_WARLIST_TYPE_LENGTH];
}

qm_card_catalog::STClientCardResult CMenus::RunTClientWarListCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CPerfTimer RenderTimer;
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	const SSettingsContentMetrics WarListMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float ListRowHeight = std::max(WarListMetrics.m_ListRowHeight, LineSize + 2.0f * MarginSmall);
	constexpr int WarListViewportRows = 8;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());

	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = nullptr;
	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ui_token::color::NEUTRAL_WHITE;
	static CLineInputBuffered<128> s_EntriesFilterInput;
	static CLineInputBuffered<128> s_PlayerSearchInput;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	static CLineInput s_ReasonInput;
	static CLineInput s_TypeNameInput;
	CWarEntry *pPreviewSelectedEntry = s_pSelectedEntry;
	CWarType *pPreviewSelectedType = s_pSelectedType;
	CWarEntry *&pSelectedEntry = ReadOnly || Ui()->RenderOnly() ? pPreviewSelectedEntry : s_pSelectedEntry;
	CWarType *&pSelectedType = ReadOnly || Ui()->RenderOnly() ? pPreviewSelectedType : s_pSelectedType;

	static uint64_t s_SelectionRevision = UINT64_MAX;
	if(s_SelectionRevision != GameClient()->m_WarList.Revision())
	{
		pSelectedEntry = nullptr;
		if(!ReadOnly && !Ui()->RenderOnly())
			s_SelectionRevision = GameClient()->m_WarList.Revision();
	}

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
		CUIRect Button, EntriesList;
		CSettingsContentLayoutFlow Flow(Column);
		static bool s_Reversed = true;
		// 排序箭头并进搜索行右侧：它只影响列表顺序，不值得单独占一行。
		if(Flow.Control("tclient-warlist-entries-search", LineSize, Button))
		{
			CUIRect SearchField, SortButton;
			Button.VSplitRight(WarListMetrics.m_ButtonHeight, &SearchField, &SortButton);
			SearchField.VSplitRight(MarginSmall, &SearchField, nullptr);
			ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, SearchField, FontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			static CButtonContainer s_ReverseEntries;
			// 排序箭头与设置卡片折叠箭头同一套过渡（0.26s EASE_IN_OUT 转 180°）：
			// 正序/倒序是持久状态，箭头必须跟着手指走，而不是下一帧直接翻面。
			IUiContext SortIconCtx = SettingsUiContext("settings_tclient_warlist_sort_icon", UiScale);
			ui_widget::SIconSwapBinding SortIcon = ui_widget::ResolveIconSwapBinding(
				SortIconCtx, EQmIconSwapPair::CHEVRON,
				EQmIcon::CHEVRON_DOWN, FONT_ICON_CHEVRON_DOWN,
				EQmIcon::CHEVRON_UP, FONT_ICON_CHEVRON_UP,
				s_Reversed ? 1.0f : 0.0f, str_quickhash("tclient-warlist-sort-icon"), &s_ReverseEntries);
			ui_widget::SIconButtonIconDraw SortIconDraw{&ui_widget::DrawIconSwapBinding, &SortIcon};
			if(Ui()->DoButton_QmIcon(&s_ReverseEntries, EQmIcon::COUNT, nullptr, 0, &SortButton, IGraphics::CORNER_ALL, IGraphics::CORNER_ALL, true, std::nullopt, false, false, &SortIconDraw) && !ReadOnly && !Ui()->RenderOnly())
				s_Reversed = !s_Reversed;
			GameClient()->m_Tooltips.DoToolTip(&s_ReverseEntries, &SortButton, Localize("Reverse order"));
		}
		// 列表与上方控件之间统一留间距，条目列表和在线玩家看起来才是同一套节奏。
		if(!Flow.Control("tclient-warlist-entries-list", std::clamp((int)GameClient()->m_WarList.m_vWarEntries.size(), 2, WarListViewportRows) * ListRowHeight, EntriesList, MarginSmall))
			return;
		static std::vector<CWarEntry *> s_vFilteredEntries;
		static char s_aCachedEntriesFilter[128] = "";
		static bool s_CachedReversed = false;
		static uint64_t s_CachedWarEntriesRevision = UINT64_MAX;
		if(str_comp(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString()) != 0 ||
			s_CachedReversed != s_Reversed ||
			s_CachedWarEntriesRevision != GameClient()->m_WarList.Revision())
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
			s_CachedWarEntriesRevision = GameClient()->m_WarList.Revision();
		}

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		static CListBox s_EntriesReadOnlyListBox;
		CListBox &EntriesListBox = ReadOnly ? s_EntriesReadOnlyListBox : s_EntriesListBox;
		EntriesListBox.SetActive(!ReadOnly);
		EntriesListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
		EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
		EntriesListBox.DoStart(ListRowHeight, s_vFilteredEntries.size(), 1, 2, SelectedOldEntry, &EntriesList);

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

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			// 删除放最右侧，和群组行同一套尺寸与位置：一行左手名字、右手操作。
			EntryRect.VSplitRight(20.0f, &EntryRect, &DeleteButton);
			DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
			DeleteButton.Margin(2.0f, &DeleteButton);
			if(Ui()->DoButton_QmIcon(&s_vDeleteButtons[i], EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL) && !ReadOnly && !Ui()->RenderOnly())
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
			EntryRect.VSplitLeft(ListRowHeight, &EntryTypeRect, &EntryRect);
			if(!ReadOnly || Ui()->BackgroundRender())
			{
				if(IsClan)
					RenderFontIcon_QmIcon(EntryTypeRect, EQmIcon::USERS, FONT_ICON_USERS, FontSize, TEXTALIGN_MC);
				else
					RenderDevSkin(EntryTypeRect.Center(), LineSize, "default", "default", false, 0, 0, 0, false, false);
			}

			// 备注改成整行悬浮提示：行尾那个聊天图标没人认识，白占宽度。
			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &Item.m_Rect, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			// 名称与分组各占一列，长文本省略，避免两行挤压和删除按钮缩成小点。
			EntryRect.VSplitRight(EntryRect.w * 0.32f, &EntryRect, &WarType);
			EntryRect.VSplitRight(MarginSmall, &EntryRect, nullptr);
			DoTClientLabel(Ui(), &EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			if(Flow.Drawing())
				WarType.Draw(pEntry->m_pWarType->m_Color.WithAlpha(0.16f), IGraphics::CORNER_ALL, ui_token::radius::TIGHT * UiScale);
			WarType.VMargin(MarginSmall, &WarType);
			DoTClientLabel(Ui(), &WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_MC);
		}

		const int NewSelectedEntry = EntriesListBox.DoEnd();
		// 删条目是破坏性操作：先弹确认弹窗，确认后才真删，点错还有反悔机会。
		static CWarEntry *s_pPendingRemoveEntry = nullptr;
		static CUi::SConfirmPopupContext s_RemoveWarEntryConfirm;
		static char s_aRemoveWarEntryLabel[64];
		if(!ReadOnly && !Ui()->RenderOnly() && pEntryToRemove != nullptr)
		{
			str_copy(s_aRemoveWarEntryLabel, pEntryToRemove->m_aClan[0] != '\0' ? pEntryToRemove->m_aClan : pEntryToRemove->m_aName, sizeof(s_aRemoveWarEntryLabel));
			s_pPendingRemoveEntry = pEntryToRemove;
			s_RemoveWarEntryConfirm.Reset();
			s_RemoveWarEntryConfirm.DefaultButtons();
			s_RemoveWarEntryConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
			str_format(s_RemoveWarEntryConfirm.m_aMessage, sizeof(s_RemoveWarEntryConfirm.m_aMessage),
				Localize("Are you sure that you want to remove '%s' from your war list?"), s_aRemoveWarEntryLabel);
			Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_RemoveWarEntryConfirm);
		}
		if(s_pPendingRemoveEntry != nullptr)
		{
			if(s_RemoveWarEntryConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
			{
				GameClient()->m_WarList.RemoveWarEntry(s_pPendingRemoveEntry);
				pSelectedEntry = nullptr;
				s_pPendingRemoveEntry = nullptr;
			}
			else if(s_RemoveWarEntryConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !Ui()->IsPopupOpen(&s_RemoveWarEntryConfirm))
			{
				s_pPendingRemoveEntry = nullptr;
			}
		}
		else if(!ReadOnly && !Ui()->RenderOnly() && NewSelectedEntry >= 0 && NewSelectedEntry < (int)s_vFilteredEntries.size() &&
			(SelectedOldEntry != NewSelectedEntry || (Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0))))
		{
			pSelectedEntry = s_vFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, pSelectedEntry->m_aReason);
				pSelectedType = pSelectedEntry->m_pWarType;
			}
		}

		// 列表空了只剩一个灰块，玩家不知道下一步做什么，补一句提示。
		if(s_vFilteredEntries.empty())
		{
			CUIRect Hint = EntriesList;
			TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&Hint, Localize("No entries yet"), StandardFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "entries=%d filtered=%d", (int)GameClient()->m_WarList.m_vWarEntries.size(), (int)s_vFilteredEntries.size());
		LogTClientPerfStageEx("tclient_warlist", "list", ETClientSettingsPerfStage::TEXT_CACHE, ListTimer.ElapsedMs(), false, aExtra);
	};

	auto RenderEditor = [&](CUIRect &Column) {
		CPerfTimer FilterTimer;
		CUIRect Button, ButtonL, ButtonR;
		static CButtonContainer s_AddButton, s_OverrideButton;
		CSettingsContentLayoutFlow Flow(Column);
		// 识别方式和输入框合成一行：左边选「按名字还是按战队」，右边就是被编辑的那个字段。
		// 原来两个输入框 + 一行胶囊分开画，玩家看不出胶囊到底在选什么。
		// 名称和战队各占一个输入框并排：两样都是条目的身份，没必要先选一个再输入。
		if(Flow.Control("tclient-warlist-entry-identity", LineSize, Button, MarginSmall))
		{
			CUIRect NameField, ClanField;
			Button.VSplitMid(&NameField, &ClanField, MarginSmall);
			s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
			s_NameInput.SetEmptyText(Localize("Name"));
			ui_widget::InputField(TClientWarListTextInputCtx, &s_NameInput, NameField, Localize("Name"), EditBoxFontSize);
			s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
			s_ClanInput.SetEmptyText(Localize("Clan"));
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ClanInput, ClanField, Localize("Clan"), EditBoxFontSize);
		}
		if(Flow.Control("tclient-warlist-selected-group", LineSize, Button, MarginSmall) && pSelectedType)
		{
			Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
			DoTClientLabel(Ui(), &ButtonL, Localize("War Groups"), FontSize, TEXTALIGN_ML);
			if(Flow.Drawing())
				ButtonR.Draw(pSelectedType->m_Color.WithAlpha(0.16f), IGraphics::CORNER_ALL, ui_token::radius::TIGHT * UiScale);
			ButtonR.VMargin(MarginSmall, &ButtonR);
			DoTClientLabel(Ui(), &ButtonR, pSelectedType->m_aWarName, FontSize, TEXTALIGN_MC);
		}
		if(Flow.Control("tclient-warlist-reason", LineSize, Button, MarginSmall))
		{
			s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
			s_ReasonInput.SetEmptyText(Localize("Reason"));
			ui_widget::InputField(TClientWarListTextInputCtx, &s_ReasonInput, Button, Localize("Reason"), EditBoxFontSize);
		}
		if(Flow.Control("tclient-warlist-entry-actions", WarListMetrics.m_ButtonHeight, Button, MarginSmall))
		{
			// 两个动作都用图标按钮：加号写入新条目，勾号把改动写回选中条目；说明走悬浮提示。
			CUIRect AddRect, OverrideRect;
			Button.VSplitRight(WarListMetrics.m_ButtonHeight, &Button, &OverrideRect);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
			Button.VSplitRight(WarListMetrics.m_ButtonHeight, &Button, &AddRect);
			static CButtonContainer s_AddButton, s_OverrideButton;
			if(Ui()->DoButton_QmIcon(&s_AddButton, EQmIcon::PLUS, FONT_ICON_PLUS, pSelectedType != nullptr ? 0 : -1, &AddRect, IGraphics::CORNER_ALL) && pSelectedType != nullptr && !ReadOnly && !Ui()->RenderOnly())
			{
				GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, pSelectedType->m_aWarName);
				pSelectedEntry = nullptr;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_AddButton, &AddRect, Localize("Add Entry"));
			if(Ui()->DoButton_QmIcon(&s_OverrideButton, EQmIcon::CHECK, "\xEE\x86\x82", pSelectedEntry != nullptr ? 0 : -1, &OverrideRect, IGraphics::CORNER_ALL) && pSelectedEntry != nullptr && pSelectedType != nullptr && !ReadOnly && !Ui()->RenderOnly() && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
			{
				const int Index = static_cast<int>(pSelectedEntry - GameClient()->m_WarList.m_vWarEntries.data());
				GameClient()->m_WarList.UpdateWarEntry(Index, s_aEntryName, s_aEntryClan, s_aEntryReason, pSelectedType);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_OverrideButton, &OverrideRect, Localize("Override Entry"));
		}
		LogTClientPerfStageEx("tclient_warlist", "filter", ETClientSettingsPerfStage::INTERACTIVE_LAYER, FilterTimer.ElapsedMs());
	};

	auto RenderSettings = [&](CUIRect &Column) {
		CUIRect CheckBoxRect, CaptionRect;
		CSettingsContentLayoutFlow Flow(Column);
		// 主开关关掉时其余选项不再接输入、胶囊也不高亮：一眼能看出这功能现在没开。
		const bool MasterOn = g_Config.m_QmWarList != 0;
		// 小节标题统一「次文本色 + 两端渐隐分割线」，不用整块背景板。
		const auto DrawCaption = [&](const char *pId, const char *pText, float SpacingBefore) {
			if(!Flow.Control(pId, WarListMetrics.m_LineHeight, CaptionRect, SpacingBefore))
				return;
			SLabelProperties Props;
			Props.m_MaxWidth = CaptionRect.w;
			Props.m_EnableWidthCheck = false;
			Props.SetColor(ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&CaptionRect, pText, StandardFontSize, TEXTALIGN_ML, Props);
		};
		// 主开关独占第一行：其余选项都从属于它。
		if(Flow.Control("tclient-warlist-enable", LineSize, CheckBoxRect))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarList, "tclient-warlist-enable", Localize("Enable warlist"), g_Config.m_QmWarList, &CheckBoxRect) && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarList ^= 1;
		}
		// 主开关与下面的分组之间画一条分割线，分组边界靠线不靠背景板。
		if(Flow.Control("tclient-warlist-settings-divider", ui_token::spacing::SM, CaptionRect, ui_token::spacing::XS))
			ui_widget::DrawFadeDivider(Ui(), CaptionRect);
		// 「颜色」：敌对颜色显示在哪些地方，四个并列胶囊，可多选。
		DrawCaption("tclient-warlist-colors-caption", Localize("Colors"), MarginSmall);
		if(Flow.Control("tclient-warlist-colors-divider", ui_token::spacing::SM, CaptionRect))
			ui_widget::DrawFadeDivider(Ui(), CaptionRect);
		// 显示位置回归开关行：和卡片里其他开关同一套排布，不再用胶囊。
		if(Flow.Control("tclient-warlist-colors-chat", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListChat, "tclient-warlist-colors-chat", Localize("Show enemy colors in chat"), g_Config.m_QmWarListChat, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListChat ^= 1;
		}
		if(Flow.Control("tclient-warlist-colors-scoreboard", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListScoreboard, "tclient-warlist-colors-scoreboard", Localize("Show enemy colors in scoreboard"), g_Config.m_QmWarListScoreboard, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListScoreboard ^= 1;
		}
		if(Flow.Control("tclient-warlist-colors-spectate", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListSpectate, "tclient-warlist-colors-spectate", Localize("Show enemy color in spectator menu"), g_Config.m_QmWarListSpectate, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListSpectate ^= 1;
		}
		if(Flow.Control("tclient-warlist-colors-clan", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListShowClan, "tclient-warlist-colors-clan", Localize("Show clan for enemies"), g_Config.m_QmWarListShowClan, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListShowClan ^= 1;
		}
		// 「其他」：不涉及显示位置的行为开关。
		DrawCaption("tclient-warlist-other-caption", Localize("Other"), MarginSmall * 2.0f);
		if(Flow.Control("tclient-warlist-other-divider", ui_token::spacing::SM, CaptionRect))
			ui_widget::DrawFadeDivider(Ui(), CaptionRect);
		if(Flow.Control("tclient-warlist-allow-duplicates", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListAllowDuplicates, "tclient-warlist-allow-duplicates", Localize("Allow Duplicate Entries"), g_Config.m_QmWarListAllowDuplicates, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListAllowDuplicates ^= 1;
		}
		if(Flow.Control("tclient-warlist-block-enemies", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListBlockEnemies, "tclient-warlist-block-enemies", Localize("Fully block enemy players"), g_Config.m_QmWarListBlockEnemies, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListBlockEnemies ^= 1;
		}
		if(Flow.Control("tclient-warlist-block-enemy-chat", LineSize, CheckBoxRect, MarginSmall))
		{
			if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &g_Config.m_QmWarListBlockEnemyChat, "tclient-warlist-block-enemy-chat", Localize("Block enemy chat"), g_Config.m_QmWarListBlockEnemyChat, &CheckBoxRect) && MasterOn && !ReadOnly && !Ui()->RenderOnly())
				g_Config.m_QmWarListBlockEnemyChat ^= 1;
		}
	};
	// 0 = 只看，1 = 新增中，2 = 重命名中。
	static int s_GroupEditMode = 0;
	auto RenderGroups = [&](CUIRect &Column) {
		CPerfTimer ActionsTimer;
		CUIRect WarTypeList, Button, ButtonL, ButtonR;
		CSettingsContentLayoutFlow Flow(Column);
		static CButtonContainer s_GroupColorPicker;
		if(Flow.Control("tclient-warlist-groups-list", std::clamp((int)GameClient()->m_WarList.m_WarTypes.size(), 2, WarListViewportRows) * ListRowHeight, WarTypeList, MarginSmall))
		{
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
				Item.m_Rect.Margin(MarginSmall, &TypeRect);
				if(pType->m_Removable)
				{
					TypeRect.VSplitRight(LineSize, &TypeRect, &DeleteButton);
					TypeRect.VSplitRight(MarginSmall, &TypeRect, nullptr);
					if(Ui()->DoButton_QmIcon(&s_vTypeDeleteButtons[i], EQmIcon::TRASH, FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL) && !ReadOnly && !Ui()->RenderOnly())
						m_pRemoveWarType = pType;
				}
				// 组色画成色块，名字回归正文色：纯色文字在色弱视角下等于没有信息，
				// 而且组色一旦选了深色，名字自己也读不出来。
				const float SwatchInset = std::min(ui_token::spacing::XS, TypeRect.h * 0.25f);
				const float SwatchSize = std::max(0.0f, TypeRect.h - 2.0f * SwatchInset);
				CUIRect Swatch, Label;
				TypeRect.VSplitLeft(SwatchInset, nullptr, &Swatch);
				Swatch.VSplitLeft(SwatchSize, &Swatch, &Label);
				if(SwatchSize > 0.0f)
				{
					Swatch.HMargin(SwatchInset, &Swatch);
					Swatch.Draw(pType->m_Color, IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
				}
				Label.VSplitLeft(SwatchInset, nullptr, &Label);
				DoTClientLabel(Ui(), &Label, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			}

			const int NewSelectedType = WarTypeListBox.DoEnd();
			const bool NewSelectedTypeValid = NewSelectedType >= 0 && NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size();
			if(!ReadOnly && !Ui()->RenderOnly() && ((SelectedOldType != NewSelectedType && NewSelectedTypeValid) || (NewSelectedTypeValid && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0))))
			{
				pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
				if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
				{
					str_copy(s_aTypeName, pSelectedType->m_aWarName);
					s_GroupColor = pSelectedType->m_Color;
				}
			}
			if(!ReadOnly && !Ui()->RenderOnly() && m_pRemoveWarType != nullptr)
			{
				if(m_pRemoveWarType == pSelectedType)
					pSelectedType = DefaultWarType();
				str_copy(s_aRemoveWarTypeName, m_pRemoveWarType->m_aWarName, sizeof(s_aRemoveWarTypeName));
				m_pRemoveWarType = nullptr;
				// 删分组是破坏性操作且会改到已有战争条目：Cancel/OK + 危险色。
				s_RemoveWarTypeConfirm.Reset();
				s_RemoveWarTypeConfirm.DefaultButtons();
				s_RemoveWarTypeConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
				str_format(s_RemoveWarTypeConfirm.m_aMessage, sizeof(s_RemoveWarTypeConfirm.m_aMessage),
					Localize("Are you sure that you want to remove '%s' from your war groups?"), s_aRemoveWarTypeName);
				str_append(s_RemoveWarTypeConfirm.m_aMessage, " ", sizeof(s_RemoveWarTypeConfirm.m_aMessage));
				str_append(s_RemoveWarTypeConfirm.m_aMessage, Localize("Entries in it will fall back to none."), sizeof(s_RemoveWarTypeConfirm.m_aMessage));
				Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_RemoveWarTypeConfirm);
				s_RemoveWarTypePending = true;
			}
		}
		// 结果下一帧轮询：确认后才按记下的名字删；取消或被切页收掉时只清状态。
		if(s_RemoveWarTypePending)
		{
			if(s_RemoveWarTypeConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
			{
				s_RemoveWarTypePending = false;
				PopupConfirmRemoveWarType();
			}
			else if(s_RemoveWarTypeConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !Ui()->IsPopupOpen(&s_RemoveWarTypeConfirm))
			{
				s_RemoveWarTypePending = false;
			}
		}
		// 头部做成「标题 + 动作」：新增、重命名在头上，颜色跟着选中项走，形态对齐皮肤队列的预设栏。
		if(Flow.Control("tclient-warlist-groups-header", LineSize, Button, MarginSmall))
		{
			CUIRect Label, MoveUpButton, MoveDownButton, AddButton, RenameButton;
			char aGroupLabel[96];
			str_format(aGroupLabel, sizeof(aGroupLabel), "%s (%d)", Localize("War Groups"), (int)GameClient()->m_WarList.m_WarTypes.size());
			// 图标按钮只放图标，说明交给悬浮提示：图标按钮里塞文字最容易看错。
			const float IconSide = WarListMetrics.m_ButtonHeight;
			Button.VSplitRight(IconSide * 4.0f + MarginSmall * 3.0f, &Label, &Button);
			Button.VSplitRight(IconSide, &Button, &RenameButton);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
			Button.VSplitRight(IconSide, &Button, &AddButton);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
			Button.VSplitRight(IconSide, &Button, &MoveDownButton);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
			MoveUpButton = Button;
			SLabelProperties LabelProps;
			LabelProps.m_MaxWidth = Label.w;
			LabelProps.m_EnableWidthCheck = false;
			Ui()->DoLabel(&Label, aGroupLabel, StandardFontSize, TEXTALIGN_ML, LabelProps);
			const auto &vTypes = GameClient()->m_WarList.m_WarTypes;
			const int TypeIndex = pSelectedType != nullptr ? static_cast<int>(std::find(vTypes.begin(), vTypes.end(), pSelectedType) - vTypes.begin()) : -1;
			const bool CanMoveUp = TypeIndex > 1;
			const bool CanMoveDown = TypeIndex >= 1 && TypeIndex + 1 < (int)vTypes.size();
			static CButtonContainer s_MoveUpTypeButton, s_MoveDownTypeButton, s_NewGroupButton, s_RenameGroupButton;
			if(Ui()->DoButton_QmIcon(&s_MoveUpTypeButton, EQmIcon::CHEVRON_UP, FONT_ICON_CHEVRON_UP, CanMoveUp ? 0 : -1, &MoveUpButton, IGraphics::CORNER_ALL) && CanMoveUp && !ReadOnly && !Ui()->RenderOnly())
				GameClient()->m_WarList.MoveWarType(TypeIndex, TypeIndex - 1);
			GameClient()->m_Tooltips.DoToolTip(&s_MoveUpTypeButton, &MoveUpButton, Localize("Move up"));
			if(Ui()->DoButton_QmIcon(&s_MoveDownTypeButton, EQmIcon::CHEVRON_DOWN, FONT_ICON_CHEVRON_DOWN, CanMoveDown ? 0 : -1, &MoveDownButton, IGraphics::CORNER_ALL) && CanMoveDown && !ReadOnly && !Ui()->RenderOnly())
				GameClient()->m_WarList.MoveWarType(TypeIndex, TypeIndex + 1);
			GameClient()->m_Tooltips.DoToolTip(&s_MoveDownTypeButton, &MoveDownButton, Localize("Move down"));
			if(Ui()->DoButton_QmIcon(&s_NewGroupButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &AddButton, IGraphics::CORNER_ALL) && !ReadOnly && !Ui()->RenderOnly())
			{
				s_GroupEditMode = 1;
				str_copy(s_aTypeName, "");
			}
			GameClient()->m_Tooltips.DoToolTip(&s_NewGroupButton, &AddButton, Localize("New group"));
			const bool CanRename = pSelectedType != nullptr && pSelectedType->m_Removable;
			if(Ui()->DoButton_QmIcon(&s_RenameGroupButton, EQmIcon::PENCIL, FONT_ICON_PENCIL, CanRename ? 0 : -1, &RenameButton, IGraphics::CORNER_ALL) && CanRename && !ReadOnly && !Ui()->RenderOnly())
			{
				s_GroupEditMode = 2;
				str_copy(s_aTypeName, pSelectedType->m_aWarName, sizeof(s_aTypeName));
				s_GroupColor = pSelectedType->m_Color;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_RenameGroupButton, &RenameButton, Localize("Rename group"));
		}
		// 只有新增或重命名时才出现输入行，确定后才落库，写错不会污染已有群组。
		if(s_GroupEditMode != 0 && Flow.Control("tclient-warlist-group-name", LineSize, Button, MarginSmall))
		{
			CUIRect Field, ConfirmButton, CancelButton;
			const float CommitWidth = WarListMetrics.m_ButtonHeight * 2.0f + MarginSmall;
			Button.VSplitRight(CommitWidth, &Field, &Button);
			Button.VSplitMid(&ConfirmButton, &CancelButton, MarginSmall);
			s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
			s_TypeNameInput.SetEmptyText(Localize("Group name"));
			ui_widget::InputField(TClientWarListTextInputCtx, &s_TypeNameInput, Field, Localize("Group name"), EditBoxFontSize);
			// 群组名有硬上限，提前说清楚，免得写完被静默截断。
			static unsigned char s_GroupNameHintId;
			GameClient()->m_Tooltips.DoToolTip(&s_GroupNameHintId, &Field, Localize("Group names are limited to 16 characters"));
			static CButtonContainer s_ConfirmGroupButton, s_CancelGroupButton;
			// 勾/叉是图标按钮，说明放悬浮提示里。
			GameClient()->m_Tooltips.DoToolTip(&s_ConfirmGroupButton, &ConfirmButton, Localize("Save"));
			GameClient()->m_Tooltips.DoToolTip(&s_CancelGroupButton, &CancelButton, Localize("Cancel"));
			if(Ui()->DoButton_QmIcon(&s_ConfirmGroupButton, EQmIcon::CHECK, "\xEE\x86\x82", 0, &ConfirmButton, IGraphics::CORNER_ALL) && !ReadOnly && !Ui()->RenderOnly() && str_comp(s_aTypeName, "") != 0)
			{
				if(s_GroupEditMode == 1)
				{
					GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
				}
				else if(pSelectedType != nullptr)
				{
					const auto &vpTypes = GameClient()->m_WarList.m_WarTypes;
					const int Index = static_cast<int>(std::find(vpTypes.begin(), vpTypes.end(), pSelectedType) - vpTypes.begin());
					GameClient()->m_WarList.UpsertWarType(Index, s_aTypeName, s_GroupColor);
				}
				s_GroupEditMode = 0;
			}
			if(Ui()->DoButton_QmIcon(&s_CancelGroupButton, EQmIcon::CLOSE, "\xEE\x93\xB6", 0, &CancelButton, IGraphics::CORNER_ALL) && !ReadOnly && !Ui()->RenderOnly())
				s_GroupEditMode = 0;
		}
		// 颜色跟着选中群组走：点开色板改完立刻生效，不用再去按「覆盖群组」。
		if(pSelectedType != nullptr && pSelectedType->m_Removable && Flow.Control("tclient-warlist-group-color", ColorPickerLineSize, Button, MarginSmall))
		{
			static unsigned int s_ColorValue = 0;
			s_ColorValue = color_cast<ColorHSLA>(pSelectedType->m_Color).Pack(false);
			const ColorRGBA Color = color_cast<ColorRGBA>(DoLine_ColorPicker(&s_GroupColorPicker, CurrentSettingsContentMetrics(), &Button, Localize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), false));
			if(Flow.Input() && !ReadOnly && !Ui()->RenderOnly() && Color != pSelectedType->m_Color)
			{
				s_GroupColor = Color;
				const auto &vpTypes = GameClient()->m_WarList.m_WarTypes;
				const int Index = static_cast<int>(std::find(vpTypes.begin(), vpTypes.end(), pSelectedType) - vpTypes.begin());
				GameClient()->m_WarList.UpsertWarType(Index, pSelectedType->m_aWarName, Color);
			}
		}
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "groups=%d", (int)GameClient()->m_WarList.m_WarTypes.size());
		LogTClientPerfStageEx("tclient_warlist", "actions", ETClientSettingsPerfStage::RESOURCE_PRETRIGGER, ActionsTimer.ElapsedMs(), false, aExtra);
	};

	// 0 在线 / 1 好友 / 2 本体 / 3 分身；默认在线。
	static int s_PlayerSource = 0;
	static unsigned char s_aPlayerSourceIds[4];
	auto RenderPlayers = [&](CUIRect &Column) {
		CPerfTimer PlayersTimer;
		CUIRect PlayerSearch, PlayerList, PlayerSourceRect;
		CSettingsContentLayoutFlow Flow(Column);
		// 页签式切换统一用胶囊滑块，轨道和滑块自己会画，不给四个独立按钮拼手感。
		if(Flow.Control("tclient-warlist-player-source", LineSize, PlayerSourceRect, MarginSmall))
		{
			static CButtonContainer s_aPlayerSourceButtons[4];
			const char *apSourceLabels[4] = {Localize("Online"), Localize("Friends"), Localize("Self"), Localize("Dummy")};
			const int NewSource = DoSegmentedChoice(s_aPlayerSourceButtons, apSourceLabels, 4, s_PlayerSource, PlayerSourceRect);
			if(NewSource != s_PlayerSource && !ReadOnly && !Ui()->RenderOnly())
				s_PlayerSource = NewSource;
		}
		// 好友、自身、分身都不依赖当前服务器：好友读好友列表，自身和分身是本地预设。
		if(s_PlayerSource != 0)
		{
			struct SPlayerPick
			{
				const char *m_pName;
				const char *m_pClan;
			};
			static std::vector<SPlayerPick> s_vPlayerPicks;
			s_vPlayerPicks.clear();
			if(s_PlayerSource == 1)
			{
				const IFriends *pFriends = GameClient()->Friends();
				const int FriendCount = pFriends != nullptr ? pFriends->NumFriends() : 0;
				for(int FriendIndex = 0; FriendIndex < FriendCount; ++FriendIndex)
				{
					const CFriendInfo *pFriend = pFriends->GetFriend(FriendIndex);
					if(pFriend != nullptr)
						s_vPlayerPicks.push_back({pFriend->m_aName, pFriend->m_aClan});
				}
			}
			else
			{
				// 自身和分身没进服务器也能选：名字优先取本地客户端，取不到就用配置里的昵称。
				const int LocalId = GameClient()->m_aLocalIds[s_PlayerSource == 3 ? 1 : 0];
				const char *pName = s_PlayerSource == 3 && g_Config.m_ClDummyName[0] != '\0' ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
				const char *pClan = "";
				if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[LocalId] != nullptr)
				{
					pName = GameClient()->m_aClients[LocalId].m_aName;
					pClan = GameClient()->m_aClients[LocalId].m_aClan;
				}
				s_vPlayerPicks.push_back({pName, pClan});
			}
			const int PickRows = std::clamp((int)s_vPlayerPicks.size(), 2, WarListViewportRows);
			if(!Flow.Control("tclient-warlist-player-picks", PickRows * ListRowHeight, PlayerList, MarginSmall))
				return;
			static CListBox s_PickListBox;
			static std::vector<unsigned char> s_vPickItemIds;
			s_vPickItemIds.resize(std::max<size_t>(1, s_vPlayerPicks.size()));
			s_PickListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			s_PickListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_PickListBox.DoStart(ListRowHeight, s_vPlayerPicks.size(), 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL);
			for(size_t PickIndex = 0; PickIndex < s_vPlayerPicks.size(); ++PickIndex)
			{
				const CListboxItem Item = s_PickListBox.DoNextItem(&s_vPickItemIds[PickIndex], false);
				if(!Item.m_Visible)
					continue;
				CUIRect RowRect = Item.m_Rect;
				RowRect.Margin(MarginSmall, &RowRect);
				CUIRect NameRect, ClanRect, AddEntryRect;
				RowRect.VSplitRight(WarListMetrics.m_ButtonHeight, &RowRect, &AddEntryRect);
				RowRect.VSplitRight(MarginSmall, &RowRect, nullptr);
				RowRect.VSplitMid(&NameRect, &ClanRect, MarginSmall);
				DoTClientLabel(Ui(), &NameRect, s_vPlayerPicks[PickIndex].m_pName, StandardFontSize, TEXTALIGN_ML);
				DoTClientLabel(Ui(), &ClanRect, s_vPlayerPicks[PickIndex].m_pClan, StandardFontSize, TEXTALIGN_ML);
				if(Ui()->DoButtonLogic(&s_vPickItemIds[PickIndex], 0, &Item.m_Rect, BUTTONFLAG_LEFT) && !ReadOnly && !Ui()->RenderOnly())
				{
					str_copy(s_aEntryName, s_vPlayerPicks[PickIndex].m_pName, sizeof(s_aEntryName));
					str_copy(s_aEntryClan, s_vPlayerPicks[PickIndex].m_pClan, sizeof(s_aEntryClan));
				}
				static std::vector<CButtonContainer> s_vPickAddButtons;
				s_vPickAddButtons.resize(std::max<size_t>(1, s_vPlayerPicks.size()));
				if(Ui()->DoButton_QmIcon(&s_vPickAddButtons[PickIndex], EQmIcon::PLUS, FONT_ICON_PLUS, pSelectedType != nullptr ? 0 : -1, &AddEntryRect, IGraphics::CORNER_ALL) && pSelectedType != nullptr && !ReadOnly && !Ui()->RenderOnly())
					GameClient()->m_WarList.AddWarEntry(s_vPlayerPicks[PickIndex].m_pName, s_vPlayerPicks[PickIndex].m_pClan, s_aEntryReason, pSelectedType->m_aWarName);
				GameClient()->m_Tooltips.DoToolTip(&s_vPickAddButtons[PickIndex], &AddEntryRect, Localize("Add Entry"));
			}
			s_PickListBox.DoEnd();
			if(s_vPlayerPicks.empty())
			{
				CUIRect Hint = PlayerList;
				TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
				Ui()->DoLabel(&Hint, Localize("No players match"), StandardFontSize, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			return;
		}
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
			// 本体 / 分身只留本地那两个客户端，其余照常在「在线」里。
			if(s_PlayerSource == 2 && ClientId != GameClient()->m_aLocalIds[0])
				continue;
			if(s_PlayerSource == 3 && ClientId != GameClient()->m_aLocalIds[1])
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) ||
				str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
				s_vFilteredPlayerIds.push_back(ClientId);
		}
		// 列表高度跟着实际人数收，空列表只占两行高，不铺满一屏空白。
		const int PlayerRows = std::clamp((int)s_vFilteredPlayerIds.size(), 2, WarListViewportRows);
		if(!Flow.Control("tclient-warlist-players-list", PlayerRows * ListRowHeight, PlayerList, MarginSmall))
			return;
		PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size(), 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL);
		if(s_vFilteredPlayerIds.empty())
		{
			CUIRect Hint = PlayerList;
			TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&Hint, Localize("No players match"), StandardFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

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

			CUIRect PlayerRect, TeeRect, NameRect, ClanRect, AddEntryRect;
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			Item.m_Rect.Margin(0.0f, &PlayerRect);
			// 每行末尾一个 +：作用对象就是这一行的人，不用先选中再去别处点添加。
			PlayerRect.VSplitRight(WarListMetrics.m_ButtonHeight, &PlayerRect, &AddEntryRect);
			AddEntryRect.VSplitRight(MarginSmall, &AddEntryRect, nullptr);
			PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);
			PlayerRect.VSplitMid(&NameRect, &ClanRect);
			PlayerRect = NameRect;
			PlayerRect.x = TeeRect.x;
			PlayerRect.w += TeeRect.w;
			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_NameColor);
			ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vNameButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(NameButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), PlayerRect, NameButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_L);
			DoTClientLabel(Ui(), &NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
			if(Ui()->DoButtonLogic(&s_vNameButtons[ClientId], false, &PlayerRect, BUTTONFLAG_LEFT) && !ReadOnly && !Ui()->RenderOnly())
			{
				str_copy(s_aEntryName, Client.m_aName);
			}

			TextRender()->TextColor(GameClient()->m_WarList.GetWarData(ClientId).m_ClanColor);
			ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[ClientId]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
												       (Ui()->HotItem() == &s_vClanButtons[ClientId] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			if(ClanButtonColor.a > 0.0f)
				DrawRoundedSurface(Ui(), ClanRect, ClanButtonColor, ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_R);
			DoTClientLabel(Ui(), &ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
			if(Ui()->DoButtonLogic(&s_vClanButtons[ClientId], false, &ClanRect, BUTTONFLAG_LEFT) && !ReadOnly && !Ui()->RenderOnly())
			{
				str_copy(s_aEntryClan, Client.m_aClan);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static std::vector<CButtonContainer> s_vPlayerAddButtons;
			s_vPlayerAddButtons.resize(MAX_CLIENTS);
			if(Ui()->DoButton_QmIcon(&s_vPlayerAddButtons[ClientId], EQmIcon::PLUS, FONT_ICON_PLUS, pSelectedType != nullptr ? 0 : -1, &AddEntryRect, IGraphics::CORNER_ALL) && pSelectedType != nullptr && !ReadOnly && !Ui()->RenderOnly())
				GameClient()->m_WarList.AddWarEntry(Client.m_aName, Client.m_aClan, s_aEntryReason, pSelectedType->m_aWarName);
			GameClient()->m_Tooltips.DoToolTip(&s_vPlayerAddButtons[ClientId], &AddEntryRect, Localize("Add Entry"));

			CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
			TeeInfo.m_Size = ListRowHeight;
			if(!ReadOnly || Ui()->BackgroundRender())
				RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
		}
		const int ClickedPlayer = PlayerListBox.DoEnd();
		// 整行点击兜底：只点行中间空白处也该把这个人填进编辑区。
		if(!ReadOnly && !Ui()->RenderOnly() && ClickedPlayer >= 0 && ClickedPlayer < (int)s_vFilteredPlayerIds.size())
		{
			const int ClickedId = s_vFilteredPlayerIds[ClickedPlayer];
			str_copy(s_aEntryName, GameClient()->m_aClients[ClickedId].m_aName, sizeof(s_aEntryName));
			str_copy(s_aEntryClan, GameClient()->m_aClients[ClickedId].m_aClan, sizeof(s_aEntryClan));
		}

		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "players=%d filtered=%d", MAX_CLIENTS, (int)s_vFilteredPlayerIds.size());
		LogTClientPerfStageEx("tclient_warlist", "players", ETClientSettingsPerfStage::STATIC_LAYER, PlayersTimer.ElapsedMs(), false, aExtra);
	};

	const float SectionGap = WarListMetrics.m_SectionGap;
	const float TwoColumnMinWidth = 2.0f * ResolveSettingsInlineRowMinimumWidth(WarListMetrics.m_LabelWidth + 2.0f * WarListMetrics.m_ButtonHeight, SectionGap, 1) + SectionGap;
	const char *apSectionIds[] = {"tclient-warlist-section-entries", "tclient-warlist-section-settings", "tclient-warlist-section-editor", "tclient-warlist-section-groups", "tclient-warlist-section-players"};
	const char *apTitles[] = {Localizable("War Entries"), Localizable("Settings"), Localizable("Edit Entry"), Localizable("War Groups"), Localizable("Online Players")};
	const std::function<void(CUIRect &)> aContents[] = {RenderEntries, RenderSettings, RenderEditor, RenderGroups, RenderPlayers};
	// 两列：左列只放设置开关，右列按办事顺序（条目 → 编辑 → 群组 → 在线玩家）往下叠，
	// 编辑条目紧挨着敌对群组，改归属不用在两个角落之间来回看。
	const int anColumnOf[] = {1, 0, 1, 1, 1};
	const auto RenderWarListCard = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		const auto *pScope = CSettingsContentLayoutScope::Current();
		const float Width = pScope && pScope->m_pLayout ? pScope->m_pLayout->Width() : Content.w;
		const int Columns = Width >= TwoColumnMinWidth ? 2 : 1;
		const float ColumnWidth = std::max(0.0f, (Width - (Columns - 1) * SectionGap) / Columns);
		std::array<std::function<void(CUIRect &)>, 5> aRenderSections;
		std::array<float, 5> aHeights{};
		for(size_t Index = 0; Index < aRenderSections.size(); ++Index)
		{
			aRenderSections[Index] = [&, Index](CUIRect &Col) {
				CSettingsContentLayoutFlow SectionFlow(Col);
				// 同一列里第二个及之后的分组，前面才画分割线：分组之间是分隔，不是装饰。
				const bool Divider = Columns == 1 ? Index != 0 : Index >= 2;
				if(Divider)
				{
					char aDividerId[96];
					str_format(aDividerId, sizeof(aDividerId), "%s-divider", apSectionIds[Index]);
					CUIRect Line;
					if(SectionFlow.Control(aDividerId, ui_token::spacing::SM, Line, ui_token::spacing::XS))
						ui_widget::DrawFadeDivider(Ui(), Line);
				}
				CUIRect Header;
				if(SectionFlow.Control(apSectionIds[Index], LineSize, Header))
					DoSettingsLabel(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, apSectionIds[Index], &Header, Localize(apTitles[Index]), HeadlineFontSize, TEXTALIGN_ML);
				SectionFlow.Space(MarginSmall);
				aContents[Index](Col);
			};
			aHeights[Index] = TClientDeclaredRegion(apSectionIds[Index], ColumnWidth, aRenderSections[Index], nullptr);
		}
		// 编辑区和分组管理连续排列；下方设置与在线玩家独立成行。
		std::array<float, 2> aColumnHeights{};
		for(size_t Index = 0; Index < aHeights.size(); ++Index)
			aColumnHeights[Columns == 1 ? 0 : anColumnOf[Index]] += aHeights[Index] + SectionGap;
		const float Height = std::max(0.0f, std::max(aColumnHeights[0], aColumnHeights[1]) - SectionGap);
		CUIRect Block;
		if(!Flow.Control("tclient-warlist-columns", Height, Block))
			return;
		std::array<float, 2> aColumnY = {Block.y, Block.y};
		for(size_t Index = 0; Index < aHeights.size(); ++Index)
		{
			const int ColumnIndex = Columns == 1 ? 0 : anColumnOf[Index];
			CUIRect View = {Block.x + ColumnIndex * (ColumnWidth + SectionGap), aColumnY[ColumnIndex], ColumnWidth, aHeights[Index]};
			aColumnY[ColumnIndex] += aHeights[Index] + SectionGap;
			TClientDeclaredRegion(apSectionIds[Index], ColumnWidth, aRenderSections[Index], &View);
		}
	};
	const uint64_t CardRevision = GameClient()->m_WarList.Revision();
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-warlist") == 0)
	{
		const float SavedY = Content.y;
		RenderWarListCard(Content);
		return {Content.y - SavedY, CardRevision};
	}
	return {0.0f, CardRevision};
}

void CMenus::PopupConfirmRemoveWarType()
{
	// 按名字删：确认窗显示期间列表会继续渲染，指针早已被清空。
	GameClient()->m_WarList.RemoveWarType(s_aRemoveWarTypeName);
	s_aRemoveWarTypeName[0] = '\0';
	m_pRemoveWarType = nullptr;
}
