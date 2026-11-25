#include "QmCardCatalog.h"
#include "QmCardCatalogTClientInternal.h"
#include "QmWarListCardModel.h"

#include <base/str.h>

#include <engine/friends.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmIconSwap.h>
#include <game/client/QmUi/SettingsPersonalization.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTag.h>
#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;
using namespace qm_warlist_card;

namespace qm_tclient_cards
{
	float TClientDeclaredRegion(const char *pId, float Width, const std::function<void(CUIRect &)> &Render, CUIRect *pRect);
}

namespace
{
	constexpr char RULES_ID[] = "tclient-warlist:rules";
	constexpr auto NEW_GROUP_COLOR = ui_token::color::DANGER;
	constexpr int PLAYER_ROWS = 7;
	constexpr int ENTRY_ROWS = 8;
	constexpr int GROUP_ROWS = 6;

	struct SListBoxes
	{
		CListBox m_List;
		CListBox m_PreviewList;

		CListBox &Start(bool ReadOnly, const CUIRect &Rect, const SSettingsContentMetrics &Metrics, float RowHeight, int Count, int Rows, int Selection)
		{
			CListBox &List = ReadOnly ? m_PreviewList : m_List;
			List.SetActive(!ReadOnly);
			List.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			List.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			List.SetCapsuleRows(Metrics.m_BodySize);
			List.SetContainerPadding(Count > 0 ? ui_token::list::ContainerPadding(RowHeight) : 0.0f);
			List.SetVisibleRowRange(Rows, Rows);
			List.DoStart(RowHeight, Count, 1, 2, Selection, &Rect);
			return List;
		}
	};

	SSettingsSkinListVisibleRange VisibleRows(CListBox &List, float RowHeight, int Count)
	{
		const auto Range = SettingsSkinListVisibleRangeForScroll(List.ScrollOffsetY(), List.ViewHeight(),
			RowHeight + ui_token::list::ROW_SPACING, 1, Count, 1);
		List.SkipItems(Range.m_FirstItem);
		return Range;
	}

	struct SListItem
	{
		char m_Id = 0;
		CButtonContainer m_Action;
	};

	struct SGroupItem
	{
		char m_Id = 0;
		std::array<CButtonContainer, 4> m_aActions;
	};

	struct SCandidate
	{
		const char *m_pName;
		const char *m_pClan;
		int m_ClientId;
		int m_SourceIndex;
	};

	struct SRemoval
	{
		CUi::SConfirmPopupContext m_Confirm;
		std::optional<SWarListRemoval> m_Pending;
		bool m_Group = false;

		void Open(CUi &Ui, const CQmWarListModel &Model, int Index, bool Group, const char *pLabel)
		{
			m_Group = Group;
			m_Pending = SWarListRemoval{Model.Revision(), static_cast<size_t>(Index)};
			m_Confirm.Reset();
			m_Confirm.DefaultButtons();
			m_Confirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
			str_format(m_Confirm.m_aMessage, sizeof(m_Confirm.m_aMessage),
				Group ? Localize("Are you sure that you want to remove '%s' from your war groups?") :
					Localize("Are you sure that you want to remove '%s' from your war list?"),
				pLabel);
			if(Group)
			{
				str_append(m_Confirm.m_aMessage, " ");
				str_append(m_Confirm.m_aMessage, Localize("Entries in it will fall back to none."));
			}
			Ui.ShowPopupConfirm(Ui.MouseX(), Ui.MouseY(), &m_Confirm);
		}
	};

	struct SEntryView
	{
		std::vector<int> m_vRows;
		uint64_t m_Revision = UINT64_MAX;
		CWarType *m_pGroup = nullptr;
		bool m_Reversed = false;
		char m_aSearch[128] = "";

		void Prepare(const CQmWarListModel &Model, const char *pSearch, CWarType *pGroup, bool Reversed)
		{
			if(m_Revision == Model.Revision() && m_pGroup == pGroup && m_Reversed == Reversed && str_comp(m_aSearch, pSearch) == 0)
				return;
			m_vRows.clear();
			for(int i = 0; i < static_cast<int>(Model.m_vWarEntries.size()); ++i)
				if(MatchesEntryFilter(Model.m_vWarEntries[i], pSearch, pGroup))
					m_vRows.push_back(i);
			if(Reversed)
				std::reverse(m_vRows.begin(), m_vRows.end());
			m_Revision = Model.Revision();
			m_pGroup = pGroup;
			m_Reversed = Reversed;
			str_copy(m_aSearch, pSearch);
		}
	};

	enum class EGroupEdit
	{
		NONE,
		NEW,
		RENAME,
	};

	template<typename T>
	void EnsureItems(std::deque<T> &Items, size_t Count)
	{
		// 动作 ID 由容器中的对象提供。增加条目时保留已有地址。
		if(Items.size() < Count)
			Items.resize(Count);
	}
} // namespace

qm_card_catalog::STClientCardResult CMenus::RunTClientWarListCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	if(str_comp(pStableId, "deck:tclient-warlist") != 0)
		return {};
	auto &WarList = GameClient()->m_WarList;
	static ESource s_Source = ESource::ONLINE;
	static EMatch s_QuickMatch = EMatch::PLAYER;
	static CWarType *s_pQuickGroup = nullptr;
	static CWarType *s_pGroupSelection = nullptr;
	static CWarType *s_pEntryFilterGroup = nullptr;
	static CWarType *s_pRenameGroup = nullptr;
	static EGroupEdit s_GroupEdit = EGroupEdit::NONE;
	static bool s_Reversed = true;
	static CEntryForm s_Form;
	static CLineInputBuffered<128> s_PlayerSearchInput;
	static CLineInputBuffered<128> s_EntriesFilterInput;
	const bool RulesExpanded = qm_card_catalog::SettingsPersonalizationExpanded(RULES_ID);
	uint64_t CardRevision = WarList.Revision();
	const auto Hash = [&](uint64_t Value) { CardRevision = CardRevision * 1099511628211ULL ^ Value; };
	Hash(RulesExpanded);
	Hash(static_cast<int>(s_Source));
	Hash(static_cast<int>(s_QuickMatch));
	Hash(static_cast<int>(s_GroupEdit));
	Hash(GroupIndex(WarList, s_pGroupSelection) + 1);
	Hash(s_Form.Selected(WarList) + 1);
	Hash(static_cast<int>(s_Form.Match()));
	Hash(GroupIndex(WarList, s_pEntryFilterGroup) + 1);
	Hash(str_quickhash(s_EntriesFilterInput.GetString()));
	Hash(str_quickhash(s_PlayerSearchInput.GetString()));
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};

	const auto &Metrics = Ctx.m_Metrics;
	const CTClientCardMetricsScope CardMetrics(Metrics);
	const float Gap = Metrics.m_LineSpacing;
	const float RowHeight = std::max(Metrics.m_ListRowHeight, Metrics.m_ButtonHeight);
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const auto *pScope = CSettingsContentLayoutScope::Current();
	const bool CanInput = !ReadOnly && !Ui()->RenderOnly() && (pScope == nullptr || pScope->m_Pass == ESettingsContentPass::INPUT);

	IUiContext InputCtx = SettingsUiContext("settings_tclient_warlist", Metrics.m_UiScale);
	if(ReadOnly)
	{
		InputCtx.m_pAnim = nullptr;
		InputCtx.m_pTree = nullptr;
	}
	const auto CollectPlanText = [&](const char *pTextId, const char *pText, const CUIRect &Rect, float Size, int Align, const SLabelProperties &Props, const char *pSourceTag) {
		if(!m_MenuTextPlanCollecting || pText == nullptr || pText[0] == '\0')
			return;
		uint32_t Identity = str_quickhash(pText);
		Identity ^= static_cast<uint32_t>(static_cast<int>(Size * 16.0f)) * 0x9e3779b9u;
		Identity ^= static_cast<uint32_t>(Align) * 0x85ebca6bu;
		char aTextId[64];
		str_format(aTextId, sizeof(aTextId), "tclient-warlist-%s-%08x", pTextId, Identity);
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, -1,
			aTextId, pText, &Rect, Size, Align, Props, BuildMenuTextStyleKey(&Rect, Size, Align, Props), pSourceTag);
	};
	const auto Label = [&](const CUIRect &Rect, const char *pText, float Size, ColorRGBA Color, int Align = TEXTALIGN_ML) {
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_EnableWidthCheck = false;
		Props.m_EllipsisAtEnd = true;
		Props.m_DisallowNewline = true;
		Props.SetColor(Color);
		CollectPlanText("label", pText, Rect, Size, Align, Props, "tclient-warlist-label");
		if(Rect.w > 0.0f)
			Ui()->DoLabel(&Rect, pText, Size, Align, Props);
	};
	const auto Body = [&](const CUIRect &Rect, const char *pText) {
		Label(Rect, pText, Metrics.m_BodySize, TextRender()->DefaultTextColor());
	};
	const auto Heading = [&](const CUIRect &Rect, const char *pText) {
		Label(Rect, pText, Metrics.m_HeadlineSize, TextRender()->DefaultTextColor());
	};
	const auto Tooltip = [&](const void *pId, const CUIRect &Rect, const char *pText) {
		GameClient()->m_Tooltips.DoToolTipForRect(pId, &Rect, pText);
	};
	const auto Icon = [&](CButtonContainer &Id, EQmIcon Type, const char *pGlyph, const CUIRect &Rect, const char *pHint, bool Enabled = true) {
		const bool Clicked = Ui()->DoButton_QmIcon(&Id, Type, pGlyph, 0, &Rect, IGraphics::CORNER_ALL, IGraphics::CORNER_ALL, Enabled);
		Tooltip(&Id, Rect, pHint);
		return CanInput && Enabled && Clicked;
	};
	const auto Button = [&](CButtonContainer &Id, const char *pId, const char *pText, CUIRect Rect, bool Enabled = true) {
		return DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST, &Id, pId, pText, Enabled ? 0 : -1, &Rect, Metrics) && CanInput && Enabled;
	};
	const auto Input = [&](CLineInput &Field, const CUIRect &Rect, const char *pPlaceholder, bool Search = false, bool Enabled = true) {
		ui_widget::SInputFieldOptions Options;
		Options.m_FontSize = Metrics.m_BodySize;
		Options.m_pPlaceholder = pPlaceholder;
		Options.m_Mode = Search ? ui_widget::EInputFieldMode::SEARCH : ui_widget::EInputFieldMode::TEXT;
		Options.m_Clearable = Search;
		Options.m_ProcessInput = !ReadOnly && Enabled && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_MinimumFontSize = Metrics.m_BodySize;
		Props.m_EllipsisAtEnd = true;
		CollectPlanText(Search ? "search-placeholder" : "input-placeholder", pPlaceholder, Rect, Metrics.m_BodySize, TEXTALIGN_ML, Props, "tclient-warlist-input");
		ui_widget::InputField(InputCtx, &Field, Rect, Options);
	};
	const auto Swatch = [&](const CUIRect &Rect, ColorRGBA Color) {
		DrawRoundedSurface(Ui(), Rect, Color, ui_token::color::COLOR_TRANSPARENT, ui_token::radius::TIGHT);
	};
	const auto SwatchRect = [&](CUIRect &Row) {
		CUIRect Chip;
		const float Side = std::min({Metrics.m_SmallSize, Row.h, Row.w});
		Row.VSplitLeft(Side, &Chip, &Row);
		Row.VSplitLeft(std::min(Row.w, Gap), nullptr, &Row);
		Chip.y += (Chip.h - Side) * 0.5f;
		Chip.h = Side;
		return Chip;
	};
	const auto LabelledControl = [&](CUIRect Row, const char *pLabel) {
		CUIRect Caption;
		Row.VSplitLeft(std::min(Metrics.m_LabelWidth, Row.w * 0.36f), &Caption, &Row);
		Row.VSplitLeft(std::min(Gap, Row.w), nullptr, &Row);
		Body(Caption, pLabel);
		return Row;
	};
	const auto GroupDropdown = [&](CUIRect Row, CUi::SDropDownState &DropDown, CWarType *pGroup, bool Filter = false) -> CWarType * {
		std::vector<const char *> vLabels;
		vLabels.reserve(WarList.m_WarTypes.size() + (Filter ? 1 : 0));
		if(Filter)
			vLabels.push_back(Localize("All groups"));
		for(const CWarType *pType : WarList.m_WarTypes)
			vLabels.push_back(pType->m_aWarName);
		if(vLabels.empty())
			return nullptr;

		if(pGroup != nullptr)
		{
			const CUIRect Chip = SwatchRect(Row);
			Swatch(Chip, pGroup->m_Color);
		}
		const int Current = GroupIndex(WarList, pGroup) + (Filter ? 1 : 0);
		CUi::SDropDownProperties Props;
		Props.m_FontSize = Metrics.m_BodySize;
		Props.m_Searchable = vLabels.size() > GROUP_ROWS;
		const int Selection = DoSettingsDropDown(&Row, Current, vLabels.data(), static_cast<int>(vLabels.size()), DropDown, Props);
		if(!CanInput || Selection == Current || Selection < 0 || Selection >= static_cast<int>(vLabels.size()))
			return pGroup;
		return Filter && Selection == 0 ? nullptr : WarList.m_WarTypes[Selection - (Filter ? 1 : 0)];
	};
	const auto MatchChoice = [&](CUIRect Row, CButtonContainer(&aButtons)[2], EMatch Match, const char *pPlayer, const char *pClan) {
		const char *apLabels[] = {pPlayer, pClan};
		const int Selection = DoSegmentedChoice(aButtons, apLabels, 2, static_cast<int>(Match), Row, ui_token::radius::BASE, nullptr, Metrics.m_BodySize);
		return CanInput && Selection >= 0 && Selection < 2 ? static_cast<EMatch>(Selection) : Match;
	};
	const auto ListHeight = [&](int Count, int Rows) {
		const float Padding = Count > 0 ? ui_token::list::ContainerPadding(RowHeight) : 0.0f;
		return ui_token::list::PaddedRowsHeight(Rows, RowHeight, ui_token::list::ROW_SPACING, Padding);
	};
	const auto EmptyList = [&](const CUIRect &Rect, const char *pMessage) {
		Label(Rect, pMessage, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY, TEXTALIGN_MC);
	};

	static CLineInputBuffered<MAX_NAME_LENGTH> s_NameInput;
	static CLineInputBuffered<MAX_CLAN_LENGTH> s_ClanInput;
	static CLineInputBuffered<MAX_WARLIST_REASON_LENGTH> s_ReasonInput;
	const auto RefreshEditorInputs = [&] {
		s_NameInput.Set(s_Form.m_aName);
		s_ClanInput.Set(s_Form.m_aClan);
		s_ReasonInput.Set(s_Form.m_aReason);
		(s_Form.Match() == EMatch::PLAYER ? static_cast<CLineInput &>(s_ClanInput) : static_cast<CLineInput &>(s_NameInput)).Deactivate();
	};
	const auto ExitEditor = [&] {
		s_Form.Exit();
		RefreshEditorInputs();
	};
	if(CanInput)
	{
		if(s_Form.Synchronize(WarList))
			RefreshEditorInputs();
		if(GroupIndex(WarList, s_pGroupSelection) < 0)
			s_pGroupSelection = nullptr;
		if(GroupIndex(WarList, s_pEntryFilterGroup) < 0)
			s_pEntryFilterGroup = nullptr;
		if(s_GroupEdit == EGroupEdit::RENAME && GroupIndex(WarList, s_pRenameGroup) < 0)
			s_GroupEdit = EGroupEdit::NONE;
		s_pQuickGroup = TargetGroup(WarList, s_pQuickGroup);
	}

	static SRemoval s_Removal;
	if(CanInput && s_Removal.m_Pending)
	{
		if(s_Removal.m_Confirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			const int Selected = s_Form.Selected(WarList);
			const auto Removal = *s_Removal.m_Pending;
			if(s_Removal.m_Group)
			{
				if(Removal.m_Revision == WarList.Revision() && Removal.m_Index < WarList.m_WarTypes.size())
				{
					const CWarType *pRemoved = WarList.m_WarTypes[Removal.m_Index];
					if(s_pGroupSelection == pRemoved)
						s_pGroupSelection = nullptr;
					if(s_pQuickGroup == pRemoved)
						s_pQuickGroup = nullptr;
					if(s_pEntryFilterGroup == pRemoved)
						s_pEntryFilterGroup = nullptr;
					if(s_pRenameGroup == pRemoved)
						s_GroupEdit = EGroupEdit::NONE;
					if(s_Form.m_pGroup == pRemoved)
						s_Form.m_pGroup = Selected >= 0 ? WarList.m_pWarTypeNone : nullptr;
					Removal.RemoveType(WarList);
					s_Form.AcceptRevision(WarList, Selected);
				}
			}
			else if(Removal.m_Revision == WarList.Revision() && Removal.m_Index < WarList.m_vWarEntries.size())
			{
				Removal.RemoveEntry(WarList);
				s_Form.AcceptRevision(WarList, Selected, static_cast<int>(Removal.m_Index));
				RefreshEditorInputs();
			}
			s_Removal.m_Pending.reset();
		}
		else if(s_Removal.m_Confirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !Ui()->IsPopupOpen(&s_Removal.m_Confirm))
			s_Removal.m_Pending.reset();
	}

	const auto RenderQuickAdd = [&](CUIRect &Column) {
		CSettingsContentLayoutFlow Flow(Column);
		CUIRect Row;
		if(Flow.Control("quick-title", Metrics.m_LineHeight, Row))
			Heading(Row, Localize("Quick add"));
		CWarType *pTarget = TargetGroup(WarList, s_pQuickGroup);
		if(Flow.Control("quick-group", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CUi::SDropDownState s_DropDown;
			pTarget = GroupDropdown(LabelledControl(Row, Localize("Target group")), s_DropDown, pTarget);
			if(CanInput)
				s_pQuickGroup = pTarget;
		}
		if(Flow.Control("quick-match", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CButtonContainer s_aMatchButtons[2];
			const auto Match = MatchChoice(LabelledControl(Row, Localize("Add by")), s_aMatchButtons, s_QuickMatch, Localize("By player"), Localize("By clan"));
			if(CanInput)
				s_QuickMatch = Match;
		}
		const ESource PreviousSource = s_Source;
		if(Flow.Control("quick-source", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CButtonContainer s_aSourceButtons[4];
			const char *apSources[] = {Localize("Online"), Localize("Friends"), Localize("Self"), Localize("Dummy")};
			const int Source = DoSegmentedChoice(s_aSourceButtons, apSources, 4, static_cast<int>(s_Source), Row, ui_token::radius::BASE, nullptr, Metrics.m_BodySize);
			if(CanInput && Source >= 0 && Source < 4)
				s_Source = static_cast<ESource>(Source);
		}
		if(HasPlayerSearch(s_Source) && Flow.Control("quick-search", Metrics.m_InputHeight, Row, Gap))
			Input(s_PlayerSearchInput, Row, Localize("Search players or clans"), true);

		static std::vector<SCandidate> s_vCandidates;
		s_vCandidates.clear();
		const auto Append = [&](const char *pName, const char *pClan, int ClientId, int SourceIndex) {
			if(MatchesCandidateFilter(s_Source, pName, pClan, s_PlayerSearchInput.GetString()))
				s_vCandidates.push_back({pName, pClan, ClientId, SourceIndex});
		};
		const auto AppendOnline = [&](int ClientId, int SourceIndex) {
			const auto &Client = GameClient()->m_aClients[ClientId];
			Append(Client.m_aName, Client.m_aClan, ClientId, SourceIndex);
		};
		if(s_Source == ESource::ONLINE)
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
				if(GameClient()->m_Snap.m_apPlayerInfos[i] != nullptr)
					AppendOnline(i, i);
		}
		else if(s_Source == ESource::FRIENDS)
		{
			const IFriends *pFriends = GameClient()->Friends();
			const int Count = pFriends != nullptr ? pFriends->NumFriends() : 0;
			for(int i = 0; i < Count; ++i)
			{
				const CFriendInfo *pFriend = pFriends->GetFriend(i);
				if(pFriend == nullptr)
					continue;
				int ClientId = -1;
				if(pFriend->m_aName[0] != '\0')
				{
					for(int j = 0; j < MAX_CLIENTS; ++j)
					{
						const auto &Client = GameClient()->m_aClients[j];
						if(GameClient()->m_Snap.m_apPlayerInfos[j] != nullptr && str_comp(Client.m_aName, pFriend->m_aName) == 0 &&
							(pFriend->m_aClan[0] == '\0' || str_comp(Client.m_aClan, pFriend->m_aClan) == 0))
						{
							ClientId = j;
							break;
						}
					}
				}
				if(ClientId >= 0)
					AppendOnline(ClientId, i);
				else
					Append(pFriend->m_aName, pFriend->m_aClan, -1, i);
			}
		}
		else
		{
			const bool Dummy = s_Source == ESource::DUMMY;
			const int ClientId = GameClient()->m_aLocalIds[Dummy ? 1 : 0];
			if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr)
				AppendOnline(ClientId, 0);
			else
				Append(Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName, Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan, -1, 0);
		}
		const int Count = static_cast<int>(s_vCandidates.size());
		const int Rows = std::clamp(Count, 1, PLAYER_ROWS);
		if(!Flow.Control("quick-list", ListHeight(Count, Rows), Row, Gap))
			return;
		if(Count == 0)
		{
			EmptyList(Row, Localize("No players match"));
			return;
		}
		static SListBoxes s_Lists;
		static std::array<std::deque<SListItem>, 4> s_aItems;
		if(CanInput && PreviousSource != s_Source)
			s_Lists.m_List.ResetScroll();
		std::deque<SListItem> &Items = s_aItems[static_cast<int>(s_Source)];
		auto &List = s_Lists.Start(ReadOnly, Row, Metrics, RowHeight, Count, Rows, -1);
		const auto Range = VisibleRows(List, RowHeight, Count);
		int AddIndex = -1;
		for(int i = Range.m_FirstItem; i < Range.m_EndItem; ++i)
		{
			const auto &Candidate = s_vCandidates[i];
			EnsureItems(Items, Candidate.m_SourceIndex + 1);
			auto &Controls = Items[Candidate.m_SourceIndex];
			const auto Item = List.DoNextItem(&Controls.m_Id);
			if(!Item.m_Visible)
				continue;
			const auto Blur = Item.SuppressGaussianBlur();
			CUIRect Detail, Add, Avatar;
			Item.m_ContentRect.VSplitRight(std::min(Metrics.m_ButtonHeight, Item.m_ContentRect.w), &Detail, &Add);
			Detail.VSplitLeft(std::min(RowHeight, Detail.w), &Avatar, &Detail);
			if(Flow.Drawing())
			{
				if(Candidate.m_ClientId >= 0)
				{
					CTeeRenderInfo Tee = GameClient()->m_aClients[Candidate.m_ClientId].m_RenderInfo;
					Tee.m_Size = RowHeight;
					RenderTeeCute(CAnimState::GetIdle(), &Tee, 0, vec2(1.0f, 0.0f), Avatar.Center(), true);
				}
				else
					RenderFontIcon_QmIcon(Avatar, EQmIcon::USER, nullptr, Metrics.m_BodySize, TEXTALIGN_MC);
			}
			const SIdentity Key = CandidateIdentity(s_QuickMatch, Candidate.m_pName, Candidate.m_pClan);
			const CWarType *pMatch = nullptr;
			WarList.VisitMatches(Candidate.m_pName, Candidate.m_pClan, [&](const CWarEntry &Entry, bool) {
				if(pMatch == nullptr || Entry.m_pWarType == pTarget)
					pMatch = Entry.m_pWarType;
				return pMatch != pTarget;
			});
			if(pMatch != nullptr)
			{
				const CUIRect Chip = SwatchRect(Detail);
				Swatch(Chip, pMatch->m_Color);
				Tooltip(&Controls.m_Id, Detail, pMatch->m_aWarName);
			}
			const char *pDisplayName = Candidate.m_pName[0] != '\0' ? Candidate.m_pName : Localize("Clan friend");
			char aClan[MAX_CLAN_LENGTH + 2];
			if(Candidate.m_pClan[0] != '\0')
			{
				str_format(aClan, sizeof(aClan), "[%s]", Candidate.m_pClan);
				const float NameWidth = TextRender()->TextWidth(Metrics.m_BodySize, pDisplayName);
				CUIRect Clan;
				Detail.VSplitLeft(std::min(NameWidth + Gap, Detail.w * 0.62f), &Detail, &Clan);
				Label(Clan, aClan, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
			}
			Body(Detail, pDisplayName);

			char aHint[256];
			if(!Key.Valid())
				str_copy(aHint, s_QuickMatch == EMatch::CLAN ?
							Localize("This player has no clan. Switch to By player to add them.") :
							Localize("This friend has no player name. Switch to By clan to add them."));
			else if(pTarget != nullptr)
				str_format(aHint, sizeof(aHint), s_QuickMatch == EMatch::CLAN ? Localize("Add the entire clan '%s' to '%s'") : Localize("Add player '%s' to '%s'"),
					Key.m_pText, pTarget->m_aWarName);
			else
				str_copy(aHint, Localize("Select a target group"));
			if(Icon(Controls.m_Action, EQmIcon::PLUS, FONT_ICON_PLUS, Add, aHint, Key.Valid() && pTarget != nullptr))
				AddIndex = i;
		}
		List.SkipItems(Count - Range.m_EndItem);
		const int Chosen = List.DoEnd();
		if(CanInput && AddIndex >= 0 && pTarget != nullptr)
		{
			const auto &Candidate = s_vCandidates[AddIndex];
			const SIdentity Key = CandidateIdentity(s_QuickMatch, Candidate.m_pName, Candidate.m_pClan);
			const int Selected = s_Form.Selected(WarList);
			int KeptIndex = Selected;
			if(Selected >= 0 && !g_Config.m_QmWarListAllowDuplicates)
			{
				for(int i = 0; i <= Selected; ++i)
				{
					const auto &Entry = WarList.m_vWarEntries[i];
					if(str_comp(Entry.m_aName, Key.Name()) == 0 && str_comp(Entry.m_aClan, Key.Clan()) == 0)
						KeptIndex = i == Selected ? -1 : KeptIndex - 1;
				}
			}
			if(WarList.AddWarEntry(Key.Name(), Key.Clan(), "", pTarget->m_aWarName))
			{
				if(Selected >= 0 && KeptIndex < 0)
					ExitEditor();
				else
					s_Form.AcceptRevision(WarList, KeptIndex);
			}
		}
		else if(CanInput && List.WasItemSelected() && Chosen >= 0 && Chosen < Count)
		{
			const auto &Candidate = s_vCandidates[Chosen];
			if(s_Form.Prefill(WarList, CandidateIdentity(s_QuickMatch, Candidate.m_pName, Candidate.m_pClan), pTarget))
				RefreshEditorInputs();
		}
	};

	static uint64_t s_GroupCountsRevision = UINT64_MAX;
	static std::unordered_map<const CWarType *, int> s_GroupCounts;
	static CLineInputBuffered<MAX_WARLIST_TYPE_LENGTH> s_NewGroupName, s_RenameGroupName;
	static unsigned s_NewColor = color_cast<ColorHSLA>(NEW_GROUP_COLOR).Pack(false);
	const auto RenderGroupName = [&](CUIRect Row, bool New, bool Editing) {
		const auto Layout = ResolveSettingsInputActionsLayout(Row, 2, Metrics);
		CUIRect Field = Layout.m_Input;
		if(New)
		{
			const CUIRect Chip = SwatchRect(Field);
			if(Editing)
			{
				const ColorHSLA Color = DoButton_ColorPicker(&Chip, &s_NewColor, false);
				if(CanInput)
					s_NewColor = Color.Pack(false);
				Tooltip(&s_NewColor, Chip, Localize("Choose the new group color"));
			}
			else
				Swatch(Chip, color_cast<ColorRGBA>(ColorHSLA(s_NewColor, false)));
		}
		auto &GroupName = New ? s_NewGroupName : s_RenameGroupName;
		Input(GroupName, Field, Localize("Enter group name"), false, Editing);
		Tooltip(&GroupName, Field, Localize("Group names can use up to 15 UTF-8 bytes"));
		const char *pName = GroupName.GetString();
		const CWarType *pExisting = WarList.FindWarType(pName);
		const bool Valid = !GroupName.IsEmpty() && str_comp_nocase(pName, "none") != 0;
		const bool Unique = pExisting == WarList.m_pWarTypeNone || (!New && pExisting == s_pRenameGroup);
		const char *pSaveHint = !Valid  ? Localize("Enter a valid group name (other than none)") :
					!Unique ? Localize("A war group already has this name") :
						  Localize("Save");
		static CButtonContainer s_aSave[2], s_aCancel[2];
		if(Icon(s_aSave[New], EQmIcon::CHECK, nullptr, Layout.m_Actions.m_aButtons[0], pSaveHint, Editing && Valid && Unique))
		{
			const int Selected = s_Form.Selected(WarList);
			if(New)
			{
				if(WarList.AddWarType(pName, color_cast<ColorRGBA>(ColorHSLA(s_NewColor, false))))
				{
					s_pGroupSelection = WarList.FindWarType(pName);
					s_pQuickGroup = s_pGroupSelection;
					s_GroupEdit = EGroupEdit::NONE;
				}
			}
			else if(GroupIndex(WarList, s_pRenameGroup) > 0 && s_pRenameGroup->m_Removable)
			{
				RenameEditableWarType(WarList, s_pRenameGroup, pName, s_pRenameGroup->m_Color);
				s_GroupEdit = EGroupEdit::NONE;
			}
			s_Form.AcceptRevision(WarList, Selected);
		}
		if(Icon(s_aCancel[New], EQmIcon::CLOSE, nullptr, Layout.m_Actions.m_aButtons[1], Localize("Cancel"), Editing))
			s_GroupEdit = EGroupEdit::NONE;
	};
	const auto RenderGroups = [&](CUIRect &Column) {
		CSettingsContentLayoutFlow Flow(Column);
		CUIRect Row;
		const int Count = static_cast<int>(WarList.m_WarTypes.size());
		if(Flow.Control("groups-title", Metrics.m_ButtonHeight, Row))
		{
			CUIRect Title, New;
			Row.VSplitRight(std::min(Row.w, Metrics.m_ButtonHeight), &Title, &New);
			char aTitle[96];
			str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize("War Groups"), Count);
			Heading(Title, aTitle);
			static CButtonContainer s_NewGroup;
			if(Icon(s_NewGroup, EQmIcon::PLUS, FONT_ICON_PLUS, New, Localize("New war group")))
			{
				s_GroupEdit = EGroupEdit::NEW;
				s_NewGroupName.Clear();
				s_NewColor = color_cast<ColorHSLA>(NEW_GROUP_COLOR).Pack(false);
			}
		}
		const float FrameDt = ReadOnly ? 0.0f : GameClient()->UiRuntimeV2()->FrameDt();
		const int Motion = ReadOnly ? 0 : g_Config.m_QmUiMotionLevel;
		static SettingsDependentRows s_New, s_Rename, s_Color;
		SettingsDependentRows PreviewNew, PreviewRename, PreviewColor;
		const bool Creating = s_GroupEdit == EGroupEdit::NEW;
		{
			const auto Reveal = (ReadOnly ? PreviewNew : s_New).Reveal(Ui(), Flow, Creating, FrameDt, Motion);
			if(Reveal && Flow.Control("groups-create", Metrics.m_InputHeight, Row, Gap))
				RenderGroupName(Row, true, Creating);
		}

		const int Rows = std::clamp(Count, 1, GROUP_ROWS);
		if(Flow.Control("groups-list", ListHeight(Count, Rows), Row, Gap))
		{
			static SListBoxes s_Lists;
			static std::unordered_map<const CWarType *, SGroupItem> s_Items;
			const int Selected = GroupIndex(WarList, s_pGroupSelection);
			auto &List = s_Lists.Start(ReadOnly, Row, Metrics, RowHeight, Count, Rows, Selected);
			if(s_GroupCountsRevision != WarList.Revision())
			{
				s_GroupCounts.clear();
				for(const auto &Entry : WarList.m_vWarEntries)
					++s_GroupCounts[Entry.m_pWarType];
				std::erase_if(s_Items, [&](const auto &Item) { return GroupIndex(WarList, Item.first) < 0; });
				s_GroupCountsRevision = WarList.Revision();
			}
			int Move = 0;
			int MoveIndex = -1;
			const auto Range = VisibleRows(List, RowHeight, Count);
			for(int i = Range.m_FirstItem; i < Range.m_EndItem; ++i)
			{
				CWarType *pType = WarList.m_WarTypes[i];
				auto &Controls = s_Items[pType];
				const auto Item = List.DoNextItem(&Controls.m_Id);
				if(!Item.m_Visible)
					continue;
				const auto Blur = Item.SuppressGaussianBlur();
				CUIRect Detail = Item.m_ContentRect;
				const CUIRect Chip = SwatchRect(Detail);
				Swatch(Chip, pType->m_Color);
				if(!pType->m_Removable)
				{
					CUIRect Hint;
					Detail.VSplitRight(std::min(Detail.w * 0.5f, Metrics.m_LabelWidth), &Detail, &Hint);
					DoSettingsInlineHint(Ui(), "warlist-fallback", Hint, 0.0f, Localize("Fallback group"), Metrics.m_SmallSize);
					Tooltip(&Controls.m_Id, Item.m_Rect, Localize("Unassigned entries fall back to this built-in group. It cannot be edited or deleted."));
				}
				else if(i == Selected || Ui()->VisualSurfaceHovered(&Item.m_Rect))
				{
					auto &aActions = Controls.m_aActions;
					const float Width = std::min(Metrics.m_ButtonHeight * aActions.size() + Gap * (aActions.size() - 1), Detail.w * 0.65f);
					CUIRect Actions;
					Detail.VSplitRight(Width, &Detail, &Actions);
					const auto Layout = ResolveSettingsActionGroupLayout(Actions, static_cast<int>(aActions.size()), Metrics, 0.0f);
					const auto &aButtons = Layout.m_aButtons;
					if(Icon(aActions[0], EQmIcon::CHEVRON_UP, FONT_ICON_CHEVRON_UP, aButtons[0],
						   i > 1 ? Localize("Move up (increase match color priority)") : Localize("Already first (none is the fixed fallback group)"), i > 1))
					{
						Move = -1;
						MoveIndex = i;
					}
					if(Icon(aActions[1], EQmIcon::CHEVRON_DOWN, FONT_ICON_CHEVRON_DOWN, aButtons[1],
						   i + 1 < Count ? Localize("Move down (decrease priority)") : Localize("Already last"), i + 1 < Count))
					{
						Move = 1;
						MoveIndex = i;
					}
					if(Icon(aActions[2], EQmIcon::PENCIL, FONT_ICON_PENCIL, aButtons[2], Localize("Rename group")))
					{
						s_pGroupSelection = pType;
						s_pRenameGroup = pType;
						s_GroupEdit = EGroupEdit::RENAME;
						s_RenameGroupName.Set(pType->m_aWarName);
					}
					if(Icon(aActions[3], EQmIcon::TRASH, FONT_ICON_TRASH, aButtons[3], Localize("Delete group")))
						s_Removal.Open(*Ui(), WarList, i, true, pType->m_aWarName);
				}
				const auto It = s_GroupCounts.find(pType);
				char aCount[48];
				str_format(aCount, sizeof(aCount), Localize("%d entries"), It == s_GroupCounts.end() ? 0 : It->second);
				CUIRect EntryCount;
				const float NameWidth = TextRender()->TextWidth(Metrics.m_BodySize, pType->m_aWarName);
				Detail.VSplitLeft(std::min(NameWidth + Gap, Detail.w * 0.66f), &Detail, &EntryCount);
				Body(Detail, pType->m_aWarName);
				Label(EntryCount, aCount, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
			}
			List.SkipItems(Count - Range.m_EndItem);
			const int Chosen = List.DoEnd();
			if(CanInput && MoveIndex >= 0)
			{
				s_pGroupSelection = WarList.m_WarTypes[MoveIndex];
				const int SelectedEntry = s_Form.Selected(WarList);
				WarList.MoveWarType(MoveIndex, MoveIndex + Move);
				s_Form.AcceptRevision(WarList, SelectedEntry);
			}
			else if(CanInput && (List.WasItemSelected() || Chosen != Selected) && Chosen >= 0 && Chosen < Count)
			{
				s_pGroupSelection = WarList.m_WarTypes[Chosen];
				if(Chosen > 0)
					s_pQuickGroup = s_pGroupSelection;
				if(s_GroupEdit == EGroupEdit::RENAME && s_pRenameGroup != s_pGroupSelection)
					s_GroupEdit = EGroupEdit::NONE;
			}
		}
		const bool Renaming = s_GroupEdit == EGroupEdit::RENAME && GroupIndex(WarList, s_pRenameGroup) > 0;
		{
			const auto Reveal = (ReadOnly ? PreviewRename : s_Rename).Reveal(Ui(), Flow, Renaming, FrameDt, Motion);
			if(Reveal && Flow.Control("groups-rename", Metrics.m_InputHeight, Row, Gap))
				RenderGroupName(Row, false, Renaming);
		}

		const int SelectedGroup = GroupIndex(WarList, s_pGroupSelection);
		const bool HasColor = SelectedGroup > 0 && s_pGroupSelection->m_Removable;
		const auto Reveal = (ReadOnly ? PreviewColor : s_Color).Reveal(Ui(), Flow, HasColor, FrameDt, Motion);
		if(Reveal && Flow.Control("groups-color", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CButtonContainer s_ResetColor;
			static unsigned s_PackedColor = color_cast<ColorHSLA>(NEW_GROUP_COLOR).Pack(false);
			const unsigned Previous = HasColor ? color_cast<ColorHSLA>(s_pGroupSelection->m_Color).Pack(false) : s_PackedColor;
			if(HasColor)
				s_PackedColor = Previous;
			std::optional<CUiScopedBackgroundRender> ClosingGuard;
			if(!HasColor)
				ClosingGuard.emplace(Ui(), true);
			const auto Color = DoLine_ColorPicker(&s_ResetColor, CurrentSettingsContentMetrics(), &Row, Localize("Group color"), &s_PackedColor, NEW_GROUP_COLOR, false, nullptr, false, false);
			const unsigned Picked = s_PackedColor != Previous ? s_PackedColor : Color.Pack(false);
			if(CanInput && HasColor && Picked != Previous)
			{
				const int SelectedEntry = s_Form.Selected(WarList);
				WarList.UpsertWarType(SelectedGroup, s_pGroupSelection->m_aWarName, color_cast<ColorRGBA>(ColorHSLA(Picked, false)));
				s_Form.AcceptRevision(WarList, SelectedEntry);
			}
		}
	};

	static int s_ScrollToEntry = -1;
	const auto RenderEntries = [&](CUIRect &Column) {
		CSettingsContentLayoutFlow Flow(Column);
		CUIRect Row;
		static SEntryView s_View;
		CWarType *pFilter = GroupIndex(WarList, s_pEntryFilterGroup) >= 0 ? s_pEntryFilterGroup : nullptr;
		s_View.Prepare(WarList, s_EntriesFilterInput.GetString(), pFilter, s_Reversed);
		const int Total = static_cast<int>(WarList.m_vWarEntries.size());
		if(Flow.Control("entries-title", Metrics.m_LineHeight, Row))
		{
			char aTitle[96];
			if(!s_EntriesFilterInput.IsEmpty() || pFilter != nullptr)
				str_format(aTitle, sizeof(aTitle), "%s (%d / %d)", Localize("War Entries"), static_cast<int>(s_View.m_vRows.size()), Total);
			else
				str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize("War Entries"), Total);
			Heading(Row, aTitle);
		}
		if(Flow.Control("entries-search", Metrics.m_InputHeight, Row, Gap))
		{
			CUIRect Sort;
			Row.VSplitRight(std::min(Metrics.m_ButtonHeight, Row.w), &Row, &Sort);
			Row.VSplitRight(std::min(Gap, Row.w), &Row, nullptr);
			Input(s_EntriesFilterInput, Row, Localize("Search entries or reasons"), true);
			static CButtonContainer s_Sort;
			const auto Swap = ui_widget::ResolveIconSwapBinding(InputCtx, EQmIconSwapPair::CHEVRON,
				EQmIcon::CHEVRON_DOWN, FONT_ICON_CHEVRON_DOWN, EQmIcon::CHEVRON_UP, FONT_ICON_CHEVRON_UP,
				s_Reversed ? 1.0f : 0.0f, str_quickhash("warlist-sort"), &s_Sort);
			const ui_widget::SIconButtonIconDraw Draw{&ui_widget::DrawIconSwapBinding, &Swap};
			if(Ui()->DoButton_QmIcon(&s_Sort, EQmIcon::COUNT, nullptr, 0, &Sort, IGraphics::CORNER_ALL, IGraphics::CORNER_ALL, true, std::nullopt, false, false, &Draw) && CanInput)
				s_Reversed = !s_Reversed;
			Tooltip(&s_Sort, Sort, Localize("Reverse order"));
		}
		if(Flow.Control("entries-filter", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CUi::SDropDownState s_DropDown;
			pFilter = GroupDropdown(LabelledControl(Row, Localize("Filter by group")), s_DropDown, pFilter, true);
			if(CanInput)
				s_pEntryFilterGroup = pFilter;
		}
		s_View.Prepare(WarList, s_EntriesFilterInput.GetString(), pFilter, s_Reversed);
		const int Count = static_cast<int>(s_View.m_vRows.size());
		const int Rows = std::clamp(Count, 1, ENTRY_ROWS);
		if(!Flow.Control("entries-list", ListHeight(Count, Rows), Row, Gap))
			return;
		if(Count == 0)
		{
			EmptyList(Row, Total == 0 ? Localize("No entries yet") : Localize("No entries match"));
			return;
		}

		static SListBoxes s_Lists;
		const int ModelSelection = s_Form.Selected(WarList);
		const int Index = CanInput && s_ScrollToEntry >= 0 ? s_ScrollToEntry : ModelSelection;
		const auto It = std::find(s_View.m_vRows.begin(), s_View.m_vRows.end(), Index);
		const int Selected = It == s_View.m_vRows.end() ? -1 : static_cast<int>(It - s_View.m_vRows.begin());
		auto &List = s_Lists.Start(ReadOnly, Row, Metrics, RowHeight, Count, Rows, Selected);
		if(CanInput && s_ScrollToEntry >= 0)
		{
			if(Selected >= 0)
				List.ScrollToSelected();
			s_ScrollToEntry = -1;
		}
		static std::deque<SListItem> s_Items;
		const auto Range = VisibleRows(List, RowHeight, Count);
		bool DeleteClicked = false;
		for(int i = Range.m_FirstItem; i < Range.m_EndItem; ++i)
		{
			const int EntryIndex = s_View.m_vRows[i];
			const auto &Entry = WarList.m_vWarEntries[EntryIndex];
			EnsureItems(s_Items, EntryIndex + 1);
			auto &Controls = s_Items[EntryIndex];
			const auto Item = List.DoNextItem(&Controls.m_Id);
			if(!Item.m_Visible)
				continue;
			const auto Blur = Item.SuppressGaussianBlur();
			const auto Identity = EntryIdentity(Entry);
			CUIRect Detail, Delete, Group, Avatar;
			Item.m_ContentRect.VSplitRight(std::min(Metrics.m_ButtonHeight, Item.m_ContentRect.w), &Detail, &Delete);
			Detail.VSplitRight(std::min(Metrics.m_LabelWidth, Detail.w * 0.28f), &Detail, &Group);
			Detail.VSplitRight(std::min(Gap, Detail.w), &Detail, nullptr);
			Detail.VSplitLeft(std::min(RowHeight, Detail.w), &Avatar, &Detail);
			if(Flow.Drawing())
				RenderFontIcon_QmIcon(Avatar, Identity.m_Match == EMatch::CLAN ? EQmIcon::USERS : EQmIcon::USER,
					Identity.m_Match == EMatch::CLAN ? FONT_ICON_USERS : nullptr, Metrics.m_BodySize, TEXTALIGN_MC);
			char aIdentity[96];
			if(Identity.m_Match == EMatch::CLAN)
				str_format(aIdentity, sizeof(aIdentity), Localize("[Clan] %s"), Identity.m_pText);
			else
				str_copy(aIdentity, Identity.m_pText);

			if(Entry.m_aReason[0] != '\0')
			{
				CUIRect Reason;
				const float NameWidth = TextRender()->TextWidth(Metrics.m_BodySize, aIdentity);
				Detail.VSplitLeft(std::min(NameWidth + Gap, Detail.w * 0.65f), &Detail, &Reason);
				Label(Reason, Entry.m_aReason, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
				Tooltip(&Controls.m_Id, Item.m_Rect, Entry.m_aReason);
			}
			Body(Detail, aIdentity);
			ui_tag::Draw(Ui(), Group, Entry.m_pWarType->m_aWarName, ui_tag::EColorMode::DERIVED, Entry.m_pWarType->m_Color);
			if(Icon(Controls.m_Action, EQmIcon::TRASH, FONT_ICON_TRASH, Delete, Localize("Delete entry")))
			{
				DeleteClicked = true;
				s_Removal.Open(*Ui(), WarList, EntryIndex, false, Identity.m_pText);
			}
		}
		List.SkipItems(Count - Range.m_EndItem);
		const int Chosen = List.DoEnd();
		if(CanInput && !DeleteClicked && (List.WasItemSelected() || Chosen != Selected) && Chosen >= 0 && Chosen < Count)
		{
			s_Form.ToggleSelection(WarList, s_View.m_vRows[Chosen]);
			RefreshEditorInputs();
		}
	};

	const auto RenderEditor = [&](CUIRect &Column) {
		CSettingsContentLayoutFlow Flow(Column);
		const int Selected = s_Form.Selected(WarList);
		const bool Editing = Selected >= 0;
		CUIRect Row;
		if(Flow.Control("editor-title", Metrics.m_ButtonHeight, Row))
		{
			if(Editing)
			{
				CUIRect Exit;
				Row.VSplitRight(std::min(Metrics.m_ButtonHeight, Row.w), &Row, &Exit);
				char aTitle[128];
				str_format(aTitle, sizeof(aTitle), Localize("Editing: %s"), EntryIdentity(WarList.m_vWarEntries[Selected]).m_pText);
				Heading(Row, aTitle);
				static CButtonContainer s_ClearSelection;
				if(Icon(s_ClearSelection, EQmIcon::CLOSE, nullptr, Exit, Localize("Exit editing")))
					ExitEditor();
			}
			else
				Heading(Row, Localize("Add an entry manually"));
		}
		if(Flow.Control("editor-match", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CButtonContainer s_aButtons[2];
			const auto Match = MatchChoice(LabelledControl(Row, Localize("Match by")), s_aButtons, s_Form.Match(), Localize("By player name"), Localize("By clan name"));
			if(CanInput && Match != s_Form.Match())
			{
				s_Form.ChangeMatch(Match);
				RefreshEditorInputs();
			}
		}
		const bool ByClan = s_Form.Match() == EMatch::CLAN;
		if(Flow.Control("editor-identity", Metrics.m_InputHeight, Row, Gap))
		{
			Input(ByClan ? static_cast<CLineInput &>(s_ClanInput) : static_cast<CLineInput &>(s_NameInput),
				LabelledControl(Row, ByClan ? Localize("Clan name") : Localize("Player name")),
				ByClan ? Localize("Enter clan name (matches all members)") : Localize("Enter player name"));
			if(CanInput)
			{
				str_copy(s_Form.m_aName, ByClan ? "" : s_NameInput.GetString());
				str_copy(s_Form.m_aClan, ByClan ? s_ClanInput.GetString() : "");
			}
		}
		if(Flow.Control("editor-group", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CUi::SDropDownState s_DropDown;
			auto *pGroup = GroupDropdown(LabelledControl(Row, Localize("Assigned group")), s_DropDown, TargetGroup(WarList, s_Form.m_pGroup));
			if(CanInput && s_Form.m_pGroup != pGroup)
				s_Form.m_pGroup = pGroup;
		}
		if(Flow.Control("editor-reason", Metrics.m_InputHeight, Row, Gap))
		{
			Input(s_ReasonInput, LabelledControl(Row, Localize("Reason")), Localize("Optional reason (can appear under the name plate)"));
			if(CanInput)
				str_copy(s_Form.m_aReason, s_ReasonInput.GetString());
		}
		if(Flow.Control("editor-actions", Metrics.m_ButtonHeight, Row, Gap))
		{
			static CButtonContainer s_Add, s_Save, s_Exit;
			const bool Valid = s_Form.Identity().Valid();
			const auto Actions = ResolveSettingsActionGroupLayout(Row, Editing ? 2 : 1, Metrics, 0.0f);
			const auto &Action = Actions.m_aButtons[0];
			const bool Clicked = Editing ?
						     Button(s_Save, "warlist-save-entry", Localize("Save changes"), Action, Valid) :
						     Button(s_Add, "warlist-add-entry", Localize("Add to war list"), Action, Valid);
			if(!Valid)
				Tooltip(Editing ? &s_Save : &s_Add, Action, Localize("Enter a player or clan name first"));
			if(Clicked && (!Editing || s_Form.Selected(WarList) >= 0) && s_Form.Submit(WarList))
			{
				RefreshEditorInputs();
				if(!Editing)
				{
					s_ScrollToEntry = static_cast<int>(WarList.m_vWarEntries.size()) - 1;
					if(!MatchesEntryFilter(WarList.m_vWarEntries.back(), s_EntriesFilterInput.GetString(), s_pEntryFilterGroup))
					{
						s_EntriesFilterInput.Clear();
						s_pEntryFilterGroup = nullptr;
					}
				}
			}
			if(Editing && Button(s_Exit, "warlist-exit-editor", Localize("Exit editing"), Actions.m_aButtons[1]))
				ExitEditor();
		}
	};

	const auto RenderRules = [&](CUIRect &Column) {
		CSettingsContentLayoutFlow Flow(Column);
		CUIRect Row;
		if(Flow.Control("rules-title", Metrics.m_LineHeight, Row))
			Heading(Row, Localize("Display and blocking rules"));
		const auto Toggle = [&](const char *pId, const char *pLabel, int &Value, bool Enabled) {
			if(!Flow.Control(pId, Metrics.m_LineHeight, Row, Gap))
				return;
			{
				const CUiScopedControlAlphaScale Alpha(Ui(), Enabled ? 1.0f : ui_token::color::TEXT_DISABLED.a);
				if(DoSettingsButton_CheckBox(SETTINGS_TCLIENT, TCLIENT_TAB_WARLIST, TCLIENT_TAB_WARLIST,
					   &Value, pId, pLabel, Value, &Row, SLabelProperties{}, CanInput && Enabled, Metrics.m_BodySize) &&
					CanInput && Enabled)
					Value ^= 1;
			}
			if(!Enabled && g_Config.m_QmWarList == 0)
				Tooltip(&Value, Row, Localize("Enable the war list to change this setting"));
		};
		Toggle("tclient-warlist-enable", Localize("Enable warlist"), g_Config.m_QmWarList, true);
		float Progress = 0.0f;
		if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Column, RULES_ID, Metrics, &Progress, ReadOnly, false, Localize("Display locations and blocking options")))
			return;

		const CSettingsContentRevealScope Reveal(Ui(), Flow, Progress);
		const bool Enabled = g_Config.m_QmWarList != 0 && qm_card_catalog::SettingsPersonalizationExpanded(RULES_ID);
		const auto Subheading = [&](const char *pId, const char *pLabel) {
			if(Flow.Control(pId, Metrics.m_LineHeight, Row, Gap))
				Label(Row, pLabel, Metrics.m_SmallSize, ui_token::color::TEXT_SECONDARY);
		};
		Subheading("rules-display-caption", Localize("Display"));
		Toggle("tclient-warlist-colors-chat", Localize("Show enemy colors in chat"), g_Config.m_QmWarListChat, Enabled);
		Toggle("tclient-warlist-colors-scoreboard", Localize("Show enemy colors in scoreboard"), g_Config.m_QmWarListScoreboard, Enabled);
		Toggle("tclient-warlist-colors-spectate", Localize("Show enemy color in spectator menu"), g_Config.m_QmWarListSpectate, Enabled);
		Toggle("tclient-warlist-colors-clan", Localize("Show clan for enemies"), g_Config.m_QmWarListShowClan, Enabled);
		Toggle("tclient-warlist-reason", Localize("Show the reason in name plates"), g_Config.m_QmWarListReason, Enabled);
		Subheading("rules-block-caption", Localize("Entries and blocking"));
		Toggle("tclient-warlist-allow-duplicates", Localize("Allow Duplicate Entries"), g_Config.m_QmWarListAllowDuplicates, Enabled);
		Toggle("tclient-warlist-block-enemies", Localize("Fully block enemy players"), g_Config.m_QmWarListBlockEnemies, Enabled);
		Toggle("tclient-warlist-block-enemy-chat", Localize("Block enemy chat"), g_Config.m_QmWarListBlockEnemyChat, Enabled);
	};

	const float Width = pScope && pScope->m_pLayout ? pScope->m_pLayout->Width() : Content.w;
	const float SectionGap = Metrics.m_SectionGap;
	const float TwoColumnMinWidth = 2.0f * ResolveSettingsInlineRowMinimumWidth(Metrics.m_LabelWidth + Metrics.m_ButtonHeight * 4.0f, Gap, 3) + SectionGap;
	const auto Columns = ResolveColumns(Width, TwoColumnMinWidth, SectionGap);
	const std::array<const char *, SECTION_COUNT> apRegionIds = {"warlist-quick-add", "warlist-groups", "warlist-entries", "warlist-editor", "warlist-rules"};
	const std::array<std::function<void(CUIRect &)>, SECTION_COUNT> aRender = {RenderQuickAdd, RenderGroups, RenderEntries, RenderEditor, RenderRules};
	std::array<float, SECTION_COUNT> aHeights{};
	for(int i = 0; i < SECTION_COUNT; ++i)
		aHeights[i] = TClientDeclaredRegion(apRegionIds[i], Columns.m_Width, aRender[i], nullptr);

	CSettingsContentLayoutFlow Flow(Content);
	std::array<CUIRect, SECTION_COUNT> aSections;
	const float Height = Columns.Place(Content, aHeights, aSections);
	CUIRect Block;
	if(Flow.Control("tclient-warlist-columns", Height, Block))
	{
		Columns.Place(Block, aHeights, aSections);
		for(int i = 0; i < SECTION_COUNT; ++i)
			TClientDeclaredRegion(apRegionIds[i], Columns.m_Width, aRender[i], &aSections[i]);
	}
	return {Height, CardRevision};
}
