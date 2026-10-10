#include "QmPlayersMenu.h"

#include "QmTree.h"
#include "UiButtons.h"
#include "UiForms.h"
#include "UiSurface.h"

#include <engine/client.h>
#include <engine/friends.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/scoreboard_skin.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>

void CMenus::RenderPlayers(CUIRect MainView)
{
	auto &State = m_QmPlayersMenu;
	auto Identity = [&](int Id) { return QmPlayersUi::SIdentity{Id, GameClient()->m_aClients[Id].m_aName, GameClient()->m_aClients[Id].m_aClan}; };
	const int PreviousId = State.m_Selection.Id();
	if(PreviousId >= 0 && PreviousId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[PreviousId])
	{
		const auto Current = Identity(PreviousId);
		State.m_Selection.Validate(&Current);
	}
	else
		State.m_Selection.Validate(nullptr);
	MainView.Draw(ms_ColorTabbarActive, MenuShellCorners(), 10.0f);
	CUiScopedSurfaceText SurfaceText(TextRender(), ms_ColorTabbarActive);
	const ColorRGBA Ink = ResolveUiSurfaceForeground(SurfaceText.Surface());
	MainView.Margin(12.0f, &MainView);
	CUIRect Header, Search, Filters, Summary;
	MainView.HSplitTop(28.0f, &Header, &MainView);
	MainView.HSplitTop(4.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Filters, &MainView);
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	auto Label = [&](const CUIRect &Rect, const char *pText, float Size = 13.0f, int Align = TEXTALIGN_ML, bool Muted = false) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Props.m_EllipsisAtEnd = true;
		const ColorRGBA Previous = TextRender()->GetTextColor();
		if(Muted)
			TextRender()->TextColor(Previous.WithMultipliedAlpha(0.7f));
		Ui()->DoLabel(&Rect, pText, Size, Align, Props);
		TextRender()->TextColor(Previous);
	};
	Header.VSplitRight(Header.w * 0.5f, &Header, &Search);
	Label(Header, Localize("Players"), 20.0f);
	IUiContext Ctx;
	Ctx.m_pUi = Ui();
	Ctx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Ctx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_players_search");
	Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_Clearable = true;
	SearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	SearchOptions.m_pPlaceholder = Localize("Search players or clans");
	SearchOptions.m_FontSize = 13.0f;
	ui_widget::InputField(Ctx, &State.m_Search, Search, SearchOptions);
	static CButtonContainer s_aFilters[3];
	const char *apFilters[] = {Localize("All"), Localize("Friends"), Localize("Muted players")};
	Filters.VSplitLeft(std::min(312.0f, Filters.w * 0.72f), &Filters, &Summary);
	const auto FilterStyle = CapsuleTabBarStyleFor(ms_ColorTabbarActive);
	State.m_Filter = DoSegmentedChoice(s_aFilters, apFilters, 3, State.m_Filter, Filters, 8.0f, &FilterStyle);
	std::array<int, MAX_CLIENTS> aIds{};
	int Count = 0;
	int Selected = -1;
	for(const auto *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(pInfo == nullptr || pInfo->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
			continue;
		const int Id = pInfo->m_ClientId;
		const auto &Player = GameClient()->m_aClients[Id];
		if(State.m_Filter == 1 && !Player.m_Friend)
			continue;
		if(State.m_Filter == 2 && !Player.m_ChatIgnore && !Player.m_EmoticonIgnore && !(g_Config.m_ClShowChatFriends && !Player.m_Friend))
			continue;
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		if(State.m_Search.GetString()[0] != '\0' && str_utf8_find_nocase(aName, State.m_Search.GetString()) == nullptr && str_utf8_find_nocase(aClan, State.m_Search.GetString()) == nullptr)
			continue;
		if(State.m_Selection.Id() == Id)
			Selected = Count;
		aIds[Count++] = Id;
	}
	const auto Layout = QmPlayersUi::Panels(MainView);
	CUIRect List = Layout.m_List;
	char aCount[64];
	str_format(aCount, sizeof(aCount), Localize("%d players"), Count);
	Label(Summary, aCount, 12.0f, TEXTALIGN_MR, true);
	static CListBox s_Players;
	s_Players.SetItemColors(Ink.WithAlpha(0.12f), Ink.WithAlpha(0.08f), Ink.WithAlpha(0.05f));
	s_Players.SetActive(!Ui()->IsPopupOpen() && !State.m_Search.IsActive());
	s_Players.DoStart(36.0f, Count, 1, 3, Selected, &List, false);
	auto DrawTee = [&](int Id, const CUIRect &Rect) {
		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
		TeeInfo.m_Size = Rect.h;
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, Offset);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), Rect.Center() + Offset);
	};
	for(int i = 0; i < Count; ++i)
	{
		const int Id = aIds[i];
		const auto &Player = GameClient()->m_aClients[Id];
		const CListboxItem Item = s_Players.DoNextItem(&Player);
		if(!Item.m_Visible)
			continue;
		auto Blur = Item.SuppressGaussianBlur();
		CUIRect Row = Item.m_Rect;
		if(Item.m_Selected)
			CUIRect{Row.x, Row.y + 4.0f, 2.0f, Row.h - 8.0f}.Draw(Ink.WithAlpha(0.55f), IGraphics::CORNER_ALL, 1.0f);
		Row.VMargin(8.0f, &Row);
		CUIRect Avatar, Flags, Text, Name, Clan;
		Row.VSplitLeft(38.0f, &Avatar, &Row);
		Row.VSplitRight(24.0f, &Text, &Flags);
		DrawTee(Id, Avatar);
		Text.HSplitTop(19.0f, &Name, &Clan);
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		Label(Name, aName, 14.0f);
		Label(Clan, aClan, 12.0f, TEXTALIGN_ML, true);
		if(Player.m_Friend)
		{
			const ColorRGBA Previous = TextRender()->GetTextColor();
			const CQmIconSemanticColorScope SemanticColor;
			TextRender()->TextColor(ResolveUiSurfaceIconColor(SurfaceText.Surface(), ColorRGBA(0.95f, 0.42f, 0.55f, 1.0f)));
			Ui()->DoLabel_QmIcon(&Flags, EQmIcon::HEART, FontIcons::FONT_ICON_HEART, 12.0f, TEXTALIGN_MC);
			TextRender()->TextColor(Previous);
		}
	}
	Selected = s_Players.DoEnd();
	if(Selected >= 0 && Selected < Count)
		State.m_Selection.Choose(Identity(aIds[Selected]));
	else if(Count == 0 || Selected < 0)
		State.m_Selection.Validate(nullptr);
	if(Count == 0)
		Label(List, Localize("No players match these filters"), 14.0f, TEXTALIGN_MC);

	CUIRect Panel = Layout.m_Details;
	DrawRoundedSurface(Ui(), Panel, Ink.WithAlpha(0.025f), Ink.WithAlpha(0.10f), 8.0f, 1.0f);
	Panel.Margin(8.0f, &Panel);
	const int Id = State.m_Selection.Id();
	if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_Snap.m_apPlayerInfos[Id])
	{
		Label(Panel, Localize("Select a player to manage"), 14.0f, TEXTALIGN_MC);
		return;
	}
	auto &Player = GameClient()->m_aClients[Id];
	static CScrollRegion s_Details;
	static int s_DetailsPlayer = -1;
	if(s_DetailsPlayer != Id || PreviousId != Id)
	{
		s_Details.Reset();
		s_DetailsPlayer = Id;
	}
	vec2 ScrollOffset;
	s_Details.Begin(&Panel, &ScrollOffset);
	Panel.y += ScrollOffset.y;
	auto NextRow = [&](float Height = 28.0f) {
		CUIRect Row;
		Panel.HSplitTop(Height, &Row, &Panel);
		Panel.HSplitTop(8.0f, nullptr, &Panel);
		return Row;
	};
	CUIRect Profile = NextRow(48.0f);
	if(s_Details.AddRect(Profile))
	{
		CUIRect Avatar, Name, Clan;
		Profile.VSplitLeft(50.0f, &Avatar, &Profile);
		DrawTee(Id, Avatar);
		Profile.HSplitTop(25.0f, &Name, &Clan);
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		Label(Name, aName, 15.0f);
		Label(Clan, aClan, 12.0f, TEXTALIGN_ML, true);
	}
	CUIRect Skin = NextRow(16.0f);
	if(s_Details.AddRect(Skin))
	{
		CUIRect Country;
		Skin.VSplitRight(30.0f, &Skin, &Country);
		Label(Skin, GameClient()->ShouldHideStreamerSkin(Id) ? "default" : Player.m_aSkinName, 12.0f, TEXTALIGN_ML, true);
		const int Code = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : Player.m_Country;
		GameClient()->m_CountryFlags.Render(Code, ColorRGBA(1, 1, 1, 0.8f), Country.x, Country.y, 27.0f, 13.5f);
	}
	const CUIRect Divider = NextRow(1.0f);
	if(s_Details.AddRect(Divider))
		Divider.Draw(Ink.WithAlpha(0.10f), IGraphics::CORNER_NONE, 0.0f);
	static CButtonContainer s_Mute, s_Emoticons, s_Friend, s_Follow, s_CopyName, s_CopySkin;
	auto Pair = [&]() {
		std::array<CUIRect, 2> aCells;
		if(Panel.w >= 232.0f)
			NextRow().VSplitMid(&aCells[0], &aCells[1], 4.0f);
		else
		{
			// 极窄详情栏退回单列，保留文字与开关的可用宽度。
			aCells[0] = NextRow();
			aCells[1] = NextRow();
		}
		return aCells;
	};
	auto Toggle = [&](CButtonContainer &Button, const char *pText, bool Value, bool Enabled, CUIRect Row) {
		if(!s_Details.AddRect(Row))
			return false;
		CUIRect Control;
		const CUIRect Hit = Row;
		Row.VSplitRight(28.0f, &Row, &Control);
		Row.VSplitRight(4.0f, &Row, nullptr);
		Label(Row, pText, 12.0f);
		GameClient()->m_Tooltips.DoToolTipForRect(&Button, &Hit, pText);
		return DoButton_Toggle(&Button, Value, &Control, Enabled) != 0;
	};
	auto Action = [&](CButtonContainer &Button, const char *pText, const CUIRect &Row, bool Enabled = true, bool Active = false) {
		if(!s_Details.AddRect(Row))
			return false;
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Enabled = Enabled;
		Options.m_Selected = Active;
		Options.m_Radius = 4.0f;
		const ColorRGBA Surface = ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), &Button, Row, Options);
		CUiScopedSurfaceText ButtonText(TextRender(), Surface);
		CUIRect Text;
		Row.VMargin(4.0f, &Text);
		Label(Text, pText, 12.0f, TEXTALIGN_MC, !Enabled);
		GameClient()->m_Tooltips.DoToolTip(&Button, &Row, pText);
		return Enabled && !Ui()->RenderOnly() && Ui()->DoButtonLogic(&Button, Active, &Row) != 0;
	};
	const auto aSocial = Pair();
	if(Toggle(s_Friend, Localize("Friends"), Player.m_Friend, true, aSocial[0]))
	{
		if(Player.m_Friend)
			GameClient()->Friends()->RemoveFriend(Player.m_aName, Player.m_aClan);
		else
			GameClient()->Friends()->AddFriend(Player.m_aName, Player.m_aClan);
		GameClient()->Client()->ServerBrowserUpdate();
	}
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const bool Following = GameClient()->m_PieMenu.IsFollowingPlayer(Player.m_aName, Player.m_aClan) ||
		(m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Player.m_aName) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Player.m_aClan) == 0);
	if(Action(s_Follow, Following ? Localize("Stop following") : Localize("Follow server"), aSocial[1], Online && !GameClient()->IsLocalClientId(Id), Following))
	{
		StopFriendAutoFollow(m_FriendAutoFollowState);
		if(Following)
			GameClient()->m_PieMenu.CancelFollow();
		else
			GameClient()->m_PieMenu.ToggleFollowPlayer(Id);
	}
	const auto aMute = Pair();
	const bool ForcedMute = g_Config.m_ClShowChatFriends && !Player.m_Friend;
	if(Toggle(s_Mute, Localize("Mute"), ForcedMute || Player.m_ChatIgnore, !ForcedMute, aMute[0]))
		Player.m_ChatIgnore ^= 1;
	if(Toggle(s_Emoticons, Localize("Mute emoticons"), ForcedMute || Player.m_EmoticonIgnore, !ForcedMute, aMute[1]))
		Player.m_EmoticonIgnore ^= 1;
	const auto aCopy = Pair();
	if(Action(s_CopyName, Localize("Copy name"), aCopy[0]))
		Input()->SetClipboardText(Player.m_aName);
	const bool CanCopySkin = Online && !Client()->IsSixup();
	if(Action(s_CopySkin, Localize("Copy skin"), aCopy[1], CanCopySkin) && QmCopyScoreboardSkin(g_Config, Client()->IsSixup(), Player.m_aSkinName, Player.m_UseCustomColor, Player.m_ColorBody, Player.m_ColorFeet))
	{
		if(g_Config.m_ClDummy)
			GameClient()->SendDummyInfo(false);
		else
			GameClient()->SendInfo(false);
	}
	if(Client()->IsSixup())
	{
		const CUIRect Hint = NextRow(36.0f);
		if(s_Details.AddRect(Hint))
			Label(Hint, Localize("Skin copying is only available for 0.6 skins"), 11.0f);
	}
	s_Details.End();
}
