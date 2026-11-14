#include "QmPlayersMenu.h"

#include "QmPlayerCard.h"
#include "QmTree.h"
#include "UiButtons.h"
#include "UiForms.h"
#include "UiOverlays.h"
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

	// 页面壳层：面板底、面板内边距与圆角全部取 token，页面不再自带一套 10/12。
	const ColorRGBA PanelColor = MenuPanelColor();
	MainView.Draw(PanelColor, MenuShellCorners(), ui_token::radius::MENU);
	CUiScopedSurfaceText SurfaceText(TextRender(), PanelColor);
	const CUIRect PageRect = MainView;
	MainView.Margin(ui_token::panel::PADDING, &MainView);

	// 整页共用一个 UI 上下文：标题、输入框、胶囊按钮与图标按钮都从它取 UI/文字/气泡。
	IUiContext Ctx;
	Ctx.m_pUi = Ui();
	Ctx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Ctx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Ctx.m_pTooltips = &GameClient()->m_Tooltips;
	Ctx.m_pTextRender = TextRender();
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_players_page");
	Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	Ctx.m_UiScale = 1.0f;
	// 只读绘制（回放定格、截图烘焙）不推进动画，也不登记需要生命周期的树节点。
	if(Ui()->RenderOnly())
	{
		Ctx.m_pAnim = nullptr;
		Ctx.m_pTree = nullptr;
	}

	// 文字只有一个入口：字号取 ui_token::font 的三段，弱化档只乘在表面前景上，
	// 不再各写 0.7 / 0.8 这类透明度。
	auto Label = [&](const CUIRect &Rect, const char *pText, float Size = ui_token::font::BODY, int Align = TEXTALIGN_ML, bool Muted = false) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Props.m_EllipsisAtEnd = true;
		const ColorRGBA Previous = TextRender()->GetTextColor();
		if(Muted)
			TextRender()->TextColor(Previous.WithMultipliedAlpha(ui_token::player_card::TEXT_MUTED_ALPHA));
		Ui()->DoLabel(&Rect, pText, Size, Align, Props);
		TextRender()->TextColor(Previous);
	};

	// 顶部：左边页面标题，右边本局在线人数（手绘稿的「35 在线」位置）。
	CUIRect Header, CountLabel;
	MainView.HSplitTop(ui_token::page_title::HEIGHT, &Header, &MainView);
	MainView.HSplitTop(ui_token::panel::GAP, nullptr, &MainView);
	Header.VSplitRight(Header.w * 0.5f, &Header, &CountLabel);
	ui_widget::PageTitle(Ctx, Header, Localize("Players"));
	char aCount[64];
	str_format(aCount, sizeof(aCount), Localize("%d players"), GameClient()->m_Snap.m_NumPlayers);
	Label(CountLabel, aCount, ui_token::font::SMALL, TEXTALIGN_MR, true);

	// 搜索与筛选并排一行：左边找玩家/氏族，右边是既有分段选择（全部/好友/被静音）。
	CUIRect SearchRow, SearchField, Filters;
	MainView.HSplitTop(ui_token::player_card::SEARCH_HEIGHT, &SearchRow, &MainView);
	MainView.HSplitTop(ui_token::panel::GAP, nullptr, &MainView);
	SearchRow.VSplitLeft(SearchRow.w * 0.5f, &SearchField, &Filters);
	Filters.VSplitLeft(std::min(ui_token::spacing::SM, Filters.w), nullptr, &Filters);
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_Clearable = true;
	SearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	SearchOptions.m_pPlaceholder = Localize("Search players or clans");
	SearchOptions.m_FontSize = ui_token::font::BODY;
	ui_widget::InputField(Ctx, &State.m_Search, SearchField, SearchOptions);
	static CButtonContainer s_aFilters[3];
	const char *apFilters[] = {Localize("All"), Localize("Friends"), Localize("Muted players")};
	const auto FilterStyle = CapsuleTabBarStyleFor(PanelColor);
	State.m_Filter = DoSegmentedChoice(s_aFilters, apFilters, 3, State.m_Filter, Filters, ui_token::radius::BASE, &FilterStyle);

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

	// 左列表 + 右信息卡：两栏之间的缝由 ui_token::player_card::LIST_CARD_GAP 决定。
	const auto Layout = QmPlayersUi::Panels(MainView);
	CUIRect List = Layout.m_List;
	static CListBox s_Players;
	// 选中/悬浮回落到 CListBox 的统一默认（选中 = 主题选中色、悬浮 = 列表 token），
	// 不再用本页的 Ink 另调一套；行距取列表 token 的半档，圆角按行高推导成胶囊。
	s_Players.DoAutoSpacing(ui_token::list::ROW_SPACING);
	s_Players.SetActive(!Ui()->IsPopupOpen() && !State.m_Search.IsActive());
	s_Players.DoStart(ui_token::player_card::LIST_ROW_HEIGHT, Count, 1, ui_token::list::DEFAULT_MAX_VISIBLE_ROWS, Selected, &List, false);
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
		// 行胶囊圆角按行高推导（与其它列表行同一档），列表容器另按容器尺寸推导。
		const CListboxItem Item = s_Players.DoNextItem(&Player, false, ui_token::list::RowRadius(s_Players.RowHeight()));
		if(!Item.m_Visible)
			continue;
		auto Blur = Item.SuppressGaussianBlur();
		// 行内容排在 CListBox 让出左右内退后的内容区，不再手写 VMargin 补偿；
		// 选中态由组件统一画（主题选中色胶囊、无描边、无左侧竖条）。
		CUIRect Row = Item.m_ContentRect;
		CUIRect Avatar, Text, Badge;
		Row.VSplitLeft(ui_token::player_card::LIST_AVATAR_SIZE, &Avatar, &Row);
		Row.VSplitRight(ui_token::player_card::LIST_BADGE_WIDTH, &Text, &Badge);
		// 头像按行高收敛成正方形，渲染尺寸永远等于短边（与方形图标按钮同一入口）。
		DrawTee(Id, QmUiSquareIconButtonRect(Avatar));
		CUIRect Name, Clan;
		Text.HSplitTop(ui_token::player_card::LIST_NAME_HEIGHT, &Name, &Clan);
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		Label(Name, aName, ui_token::font::HEADLINE);
		Label(Clan, aClan, ui_token::font::BODY, TEXTALIGN_ML, true);
		if(Player.m_Friend)
		{
			// 好友色是全局角色色（ui_token::color::FRIEND），不再在本页写一份粉红。
			const ColorRGBA Previous = TextRender()->GetTextColor();
			const CQmIconSemanticColorScope SemanticColor;
			TextRender()->TextColor(ResolveUiSurfaceIconColor(SurfaceText.Surface(), ui_token::color::FRIEND));
			Ui()->DoLabel_QmIcon(&Badge, EQmIcon::HEART, FontIcons::FONT_ICON_HEART, ui_token::font::BODY, TEXTALIGN_MC);
			TextRender()->TextColor(Previous);
		}
	}
	Selected = s_Players.DoEnd();
	if(Selected >= 0 && Selected < Count)
		State.m_Selection.Choose(Identity(aIds[Selected]));
	else if(Count == 0 || Selected < 0)
		State.m_Selection.Validate(nullptr);
	if(Count == 0)
		Label(List, Localize("No players match these filters"), ui_token::font::HEADLINE, TEXTALIGN_MC);

	// 右侧玩家信息卡：外壳只负责表面与描边，内容按 token 分区排版。
	CUIRect Card = Layout.m_Details;
	DrawRoundedSurface(Ui(), Card, ui_token::color::SURFACE_HIGHLIGHT, ui_token::color::BORDER_SUBTLE, ui_token::player_card::RADIUS, ui_token::feedback::ICON_BORDER_WIDTH);
	Card.Margin(ui_token::player_card::PADDING, &Card);
	const int Id = State.m_Selection.Id();
	if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_Snap.m_apPlayerInfos[Id])
	{
		Label(Card, Localize("Select a player to manage"), ui_token::font::HEADLINE, TEXTALIGN_MC);
		return;
	}
	auto &Player = GameClient()->m_aClients[Id];
	static CScrollRegion s_Card;
	static int s_CardPlayer = -1;
	if(s_CardPlayer != Id || PreviousId != Id)
	{
		s_Card.Reset();
		s_CardPlayer = Id;
	}
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const bool Sixup = Client()->IsSixup();
	char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
	GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
	GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
	const int DetailRowCount = 3 + (Sixup ? 1 : 0);

	vec2 ScrollOffset;
	s_Card.Begin(&Card, &ScrollOffset);
	Card.y += ScrollOffset.y;
	// 内容区按「自然高度」排版：卡片再矮也只是滚动，不会把分区压成负高度或互相叠住。
	const float NaturalHeight = QmPlayersUi::PlayerCardContentHeight((int)QmPlayersUi::EPlayerCardAction::COUNT, DetailRowCount);
	Card.h = std::max(Card.h, NaturalHeight);
	const auto CardLayout = QmPlayersUi::PlayerCardLayout(Card, (int)QmPlayersUi::EPlayerCardAction::COUNT);
	if(s_Card.AddRect(CardLayout.m_Header))
	{
		// 头部：皮肤 / 名字 / 氏族 / 国家旗。
		CUIRect HeaderRow = CardLayout.m_Header;
		CUIRect Avatar, Flag;
		HeaderRow.VSplitLeft(ui_token::player_card::HEADER_AVATAR_SIZE, &Avatar, &HeaderRow);
		HeaderRow.VSplitLeft(std::min(ui_token::spacing::SM, HeaderRow.w), nullptr, &HeaderRow);
		const float FlagHeight = std::min(ui_token::player_card::HEADER_FLAG_HEIGHT, HeaderRow.h);
		const float FlagWidth = FlagHeight * ui_token::player_card::HEADER_FLAG_ASPECT;
		HeaderRow.VSplitRight(std::min(FlagWidth, HeaderRow.w), &HeaderRow, &Flag);
		DrawTee(Id, QmUiSquareIconButtonRect(Avatar));
		CUIRect Name, Clan;
		HeaderRow.HSplitTop(std::min(ui_token::player_card::HEADER_NAME_HEIGHT, HeaderRow.h), &Name, &Clan);
		Label(Name, aName, ui_token::font::HEADLINE);
		Label(Clan, aClan, ui_token::font::BODY, TEXTALIGN_ML, true);
		const int Code = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : Player.m_Country;
		const ColorRGBA FlagColor = ui_token::color::NEUTRAL_WHITE.WithMultipliedAlpha(ui_token::player_card::FLAG_ALPHA);
		GameClient()->m_CountryFlags.Render(Code, FlagColor, Flag.x, Flag.y + (Flag.h - FlagHeight) * 0.5f, FlagWidth, FlagHeight);
	}

	// 操作区：胶囊按钮网格，全部走 ui_widget::SecondaryButton，选中态就是当前状态。
	static CButtonContainer s_aActionButtons[(int)QmPlayersUi::EPlayerCardAction::COUNT];
	const bool Following = GameClient()->m_PieMenu.IsFollowingPlayer(Player.m_aName, Player.m_aClan) ||
			       (m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Player.m_aName) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Player.m_aClan) == 0);
	const bool ForcedMute = g_Config.m_ClShowChatFriends && !Player.m_Friend;
	const bool ForcedChatMute = ForcedMute && !g_Config.m_QmChatRelatedOnly;
	const bool CanCopySkin = Online && !Sixup;
	const QmPlayersUi::SPlayerCardActionSpec aActionSpecs[] = {
		{Localize("Friends"), Player.m_Friend, true},
		{Following ? Localize("Stop following") : Localize("Follow server"), Following, Online && !GameClient()->IsLocalClientId(Id)},
		{Localize("Mute"), ForcedChatMute || Player.m_ChatIgnore, !ForcedChatMute},
		{Localize("Mute emoticons"), ForcedMute || Player.m_EmoticonIgnore, !ForcedMute},
		{Localize("Copy skin"), false, CanCopySkin},
	};
	static_assert((int)QmPlayersUi::EPlayerCardAction::COUNT == (int)(sizeof(aActionSpecs) / sizeof(aActionSpecs[0])), "玩家卡操作区的按钮数量必须与动作枚举一致");
	if(s_Card.AddRect(CardLayout.m_Actions))
	{
		switch(QmPlayersUi::PlayerCardActions(Ctx, s_aActionButtons, CardLayout.m_Actions, aActionSpecs, (int)QmPlayersUi::EPlayerCardAction::COUNT))
		{
		case QmPlayersUi::EPlayerCardAction::FRIEND:
			if(Player.m_Friend)
				GameClient()->Friends()->RemoveFriend(Player.m_aName, Player.m_aClan);
			else
				GameClient()->Friends()->AddFriend(Player.m_aName, Player.m_aClan);
			GameClient()->Client()->ServerBrowserUpdate();
			break;
		case QmPlayersUi::EPlayerCardAction::FOLLOW:
			StopFriendAutoFollow(m_FriendAutoFollowState);
			if(Following)
				GameClient()->m_PieMenu.CancelFollow();
			else
				GameClient()->m_PieMenu.ToggleFollowPlayer(Id);
			break;
		case QmPlayersUi::EPlayerCardAction::MUTE:
			Player.m_ChatIgnore ^= 1;
			break;
		case QmPlayersUi::EPlayerCardAction::MUTE_EMOTICONS:
			Player.m_EmoticonIgnore ^= 1;
			break;
		case QmPlayersUi::EPlayerCardAction::COPY_SKIN:
			if(QmCopyScoreboardSkin(g_Config, Sixup, Player.m_aSkinName, Player.m_UseCustomColor, Player.m_ColorBody, Player.m_ColorFeet))
			{
				if(g_Config.m_ClDummy)
					GameClient()->SendDummyInfo(false);
				else
					GameClient()->SendInfo(false);
				State.m_CopyToastUntil = Client()->GlobalTime() + ui_token::player_card::COPY_TOAST_SECONDS;
			}
			break;
		default:
			break;
		}
	}

	// 图标复制区：小方形图标按钮，悬停给 tooltip，点一下即写入剪贴板。
	static CButtonContainer s_aCopyButtons[(int)QmPlayersUi::EPlayerCardCopy::COUNT];
	// 皮肤名缓冲区跟着玩家记录走：既不做第二份长度常量，也不会截断长皮肤名。
	char aSkinName[sizeof(Player.m_aSkinName)];
	str_copy(aSkinName, GameClient()->ShouldHideStreamerSkin(Id) ? "default" : Player.m_aSkinName, sizeof(aSkinName));
	if(s_Card.AddRect(CardLayout.m_IconRow))
	{
		const QmPlayersUi::SPlayerCardCopySpec aCopySpecs[] = {
			{EQmIcon::USER, FontIcons::FONT_ICON_USER, aName, Localize("Copy name")},
			{EQmIcon::USERS, FontIcons::FONT_ICON_USERS, aClan, Localize("Copy clan")},
			// 皮肤 ID 复制的是玩家真实皮肤名（与计分板「Copy skin ID」同源）；
			// 流式模式只在展示（头部与细节行）里把它换成 default，复制路径保持原样。
			{EQmIcon::COPY, FontIcons::FONT_ICON_COPY, Player.m_aSkinName, Localize("Copy skin ID")},
		};
		static_assert((int)QmPlayersUi::EPlayerCardCopy::COUNT == (int)(sizeof(aCopySpecs) / sizeof(aCopySpecs[0])), "图标复制区的按钮数量必须与动作枚举一致");
		if(QmPlayersUi::PlayerCardCopies(Ctx, s_aCopyButtons, CardLayout.m_IconRow, aCopySpecs, (int)QmPlayersUi::EPlayerCardCopy::COUNT) != QmPlayersUi::EPlayerCardCopy::NONE)
			State.m_CopyToastUntil = Client()->GlobalTime() + ui_token::player_card::COPY_TOAST_SECONDS;
	}

	// 状态区：在线 / 旁观 / 加入。三者都是「当前事实」，活动项用主文本色，未成立项弱化。
	if(s_Card.AddRect(CardLayout.m_Status))
	{
		CUIRect Status = CardLayout.m_Status;
		const bool PlayerOnline = Online && GameClient()->m_Snap.m_apPlayerInfos[Id] != nullptr;
		const bool InGame = PlayerOnline && Player.m_Team != TEAM_SPECTATORS;
		auto StatusItem = [&](const char *pText, bool Active) {
			const float Width = std::min(Status.w, TextRender()->TextWidth(ui_token::font::SMALL, pText));
			CUIRect Cell;
			Status.VSplitLeft(Width, &Cell, &Status);
			Status.VSplitLeft(std::min(ui_token::player_card::STATUS_GAP, Status.w), nullptr, &Status);
			Label(Cell, pText, ui_token::font::SMALL, TEXTALIGN_ML, !Active);
		};
		StatusItem(PlayerOnline ? Localize("Online") : Localize("Offline"), PlayerOnline);
		StatusItem(Localize("Spectate"), PlayerOnline && !InGame);
		StatusItem(Localize("Join game"), InGame);
	}

	// 细节区：玩家信息（皮肤 / 氏族 / 分数），标签列宽与行高都取 token。
	if(s_Card.AddRect(CardLayout.m_Details))
	{
		CUIRect Details = CardLayout.m_Details;
		const int Score = GameClient()->m_Snap.m_apPlayerInfos[Id] != nullptr ? GameClient()->m_Snap.m_apPlayerInfos[Id]->m_Score : 0;
		char aScore[16];
		str_format(aScore, sizeof(aScore), "%d", Score);
		const char *apDetailLabels[] = {Localize("Skin"), Localize("Clan"), Localize("Score")};
		const char *apDetailValues[] = {aSkinName, aClan, aScore};
		for(int Detail = 0; Detail < 3; ++Detail)
		{
			CUIRect Row;
			Details.HSplitTop(std::min(ui_token::player_card::DETAIL_ROW_HEIGHT, Details.h), &Row, &Details);
			char aLabel[64];
			str_format(aLabel, sizeof(aLabel), "%s:", apDetailLabels[Detail]);
			CUIRect LabelCol, ValueCol;
			Row.VSplitLeft(std::min(ui_token::player_card::DETAIL_LABEL_WIDTH, Row.w), &LabelCol, &ValueCol);
			Label(LabelCol, aLabel, ui_token::player_card::DETAIL_FONT_SIZE, TEXTALIGN_ML, true);
			Label(ValueCol, apDetailValues[Detail], ui_token::player_card::DETAIL_FONT_SIZE);
		}
		if(Sixup)
		{
			// 0.7 皮肤不能写进 0.6 配置：这条说明与操作区「Copy skin」的可用性同源。
			CUIRect Hint;
			Details.HSplitTop(std::min(ui_token::player_card::DETAIL_ROW_HEIGHT, Details.h), &Hint, &Details);
			Label(Hint, Localize("Skin copying is only available for 0.6 skins"), ui_token::font::CAPTION, TEXTALIGN_ML, true);
		}
	}
	s_Card.End();

	// 复制成功的轻提示：统一一条文案，出现在页面底部，不占用卡片布局。
	const bool ToastVisible = Client()->GlobalTime() < State.m_CopyToastUntil;
	ui_widget::SToastProps ToastProps;
	ToastProps.m_pText = Localize("Copied");
	static ui_widget::SToastState s_CopyToast;
	ui_widget::Toast(Ctx, &s_CopyToast, &s_CopyToast, ToastVisible, PageRect, ToastProps);
}
