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

	// ── 顶部一行：左边胶囊筛选（全体 / 好友 / 已屏蔽），右边搜索框，最右在线人数。
	// 页面标题删掉：顶部导航已经高亮「玩家」，再写一次大标题是重复占位。
	CUIRect TopRow, FilterRow, RightSide, CountLabel, SearchField;
	MainView.HSplitTop(ui_token::settings::TAB_HEIGHT, &TopRow, &MainView);
	MainView.HSplitTop(ui_token::panel::GAP, nullptr, &MainView);
	const float RightWidth = std::min(TopRow.w * 0.62f,
		ui_token::player_card::SEARCH_WIDTH + ui_token::player_card::COUNT_WIDTH + ui_token::spacing::SM * 2.0f);
	TopRow.VSplitRight(RightWidth, &FilterRow, &RightSide);
	FilterRow.VSplitRight(std::min(ui_token::spacing::SM, FilterRow.w), &FilterRow, nullptr);
	RightSide.VSplitRight(ui_token::player_card::COUNT_WIDTH, &RightSide, &CountLabel);
	RightSide.VSplitRight(std::min(ui_token::spacing::XS, RightSide.w), &RightSide, nullptr);
	SearchField = RightSide;

	static CButtonContainer s_aFilters[3];
	const char *apFilters[] = {Localize("All"), Localize("Friends"), Localize("Muted players")};
	const auto FilterStyle = CapsuleTabBarStyleFor(PanelColor);
	State.m_Filter = DoSegmentedChoice(s_aFilters, apFilters, 3, State.m_Filter, FilterRow, ui_token::radius::PILL, &FilterStyle);
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_Clearable = true;
	SearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	SearchOptions.m_pPlaceholder = Localize("Search players or clans");
	SearchOptions.m_FontSize = ui_token::font::BODY;
	ui_widget::InputField(Ctx, &State.m_Search, SearchField, SearchOptions);
	char aCount[64];
	str_format(aCount, sizeof(aCount), Localize("%d players"), GameClient()->m_Snap.m_NumPlayers);
	Label(CountLabel, aCount, ui_token::font::SMALL, TEXTALIGN_MR, true);

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

	// ── 列表：网格卡片（与皮肤列表同一种读法）。未选中玩家时占满全宽，
	// 选中后才让出右侧信息卡；列数由可用宽度除以格宽推导，窗口变窄自动减列。
	const int SelectedId = State.m_Selection.Id();
	CUIRect ListArea = MainView;
	CUIRect CardArea{};
	if(SelectedId >= 0 && SelectedId < MAX_CLIENTS && GameClient()->m_Snap.m_apPlayerInfos[SelectedId] != nullptr)
	{
		const float CardWidth = std::min(ui_token::player_card::CARD_MAX_WIDTH, MainView.w * ui_token::player_card::CARD_WIDTH_RATIO);
		MainView.VSplitRight(CardWidth, &ListArea, &CardArea);
		ListArea.VSplitRight(ui_token::player_card::LIST_CARD_GAP, &ListArea, nullptr);
	}
	const int Columns = QmPlayersUi::PlayerTileColumns(ListArea.w);
	static CListBox s_Players;
	s_Players.DoAutoSpacing(ui_token::list::ROW_SPACING);
	s_Players.SetActive(!Ui()->IsPopupOpen() && !State.m_Search.IsActive());
	s_Players.DoStart(ui_token::player_card::TILE_HEIGHT, Count, Columns, ui_token::list::DEFAULT_MAX_VISIBLE_ROWS, Selected, &ListArea, false);
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
		const CListboxItem Item = s_Players.DoNextItem(&Player, false, ui_token::list::RowRadius(s_Players.RowHeight()));
		if(!Item.m_Visible)
			continue;
		auto Blur = Item.SuppressGaussianBlur();
		CUIRect Tile = Item.m_ContentRect;
		CUIRect Avatar, Text, Badge;
		Tile.VSplitLeft(ui_token::player_card::TILE_AVATAR_SIZE, &Avatar, &Tile);
		Tile.VSplitLeft(std::min(ui_token::spacing::XS, Tile.w), nullptr, &Tile);
		Tile.VSplitRight(ui_token::player_card::LIST_BADGE_WIDTH, &Text, &Badge);
		DrawTee(Id, QmUiSquareIconButtonRect(Avatar));
		CUIRect Name, Clan;
		Text.HSplitTop(std::min(ui_token::player_card::LIST_NAME_HEIGHT, Text.h), &Name, &Clan);
		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		Label(Name, aName, ui_token::font::HEADLINE);
		Label(Clan, aClan, ui_token::font::BODY, TEXTALIGN_ML, true);
		if(Player.m_Friend)
		{
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
		Label(ListArea, Localize("No players match these filters"), ui_token::font::HEADLINE, TEXTALIGN_MC);

	// ── 右侧信息卡：上半是「英雄展示」——角色立绘居中，名字与战队压在下沿，
	// 操作全部是展示区内的圆形图标按钮；下半只留皮肤名与复制按钮。
	if(CardArea.w <= 0.0f || SelectedId < 0)
	{
		return;
	}
	CUIRect Card = CardArea;
	DrawRoundedSurface(Ui(), Card, ui_token::color::SURFACE_HIGHLIGHT, ui_token::color::BORDER_SUBTLE, ui_token::player_card::RADIUS, ui_token::feedback::ICON_BORDER_WIDTH);
	Card.Margin(ui_token::player_card::PADDING, &Card);
	auto &Player = GameClient()->m_aClients[SelectedId];
	char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
	GameClient()->FormatStreamerName(SelectedId, aName, sizeof(aName));
	GameClient()->FormatStreamerClan(SelectedId, aClan, sizeof(aClan));
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const bool Sixup = Client()->IsSixup();
	char aSkinName[sizeof(Player.m_aSkinName)];
	str_copy(aSkinName, GameClient()->ShouldHideStreamerSkin(SelectedId) ? "default" : Player.m_aSkinName, sizeof(aSkinName));

	// 展示区右下角：四个圆形图标按钮——好友（心）、静音（禁）、跨服跟随（眼）、屏蔽表情（裂心）。
	// 图标沿用仓库既有语义色与既有图集条目，不再出现「复制战队」这种 DDNet 里没有的动作。
	const bool Following = GameClient()->m_PieMenu.IsFollowingPlayer(Player.m_aName, Player.m_aClan) ||
			       (m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Player.m_aName) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Player.m_aClan) == 0);
	const bool ForcedMute = g_Config.m_ClShowChatFriends && !Player.m_Friend;
	const bool ForcedChatMute = ForcedMute && !g_Config.m_QmChatRelatedOnly;
	struct SIconAction
	{
		EQmIcon m_Icon;
		const char *m_pFallback;
		const char *m_pTooltip;
		bool m_Active;
		bool m_Enabled;
	};
	const SIconAction aIconActions[] = {
		{EQmIcon::HEART, FontIcons::FONT_ICON_HEART, Localize("Friends"), Player.m_Friend, true},
		{EQmIcon::BAN, FontIcons::FONT_ICON_BAN, Localize("Mute"), ForcedChatMute || Player.m_ChatIgnore, !ForcedChatMute},
		{EQmIcon::HEART_CRACK, FontIcons::FONT_ICON_HEART_CRACK, Localize("Mute emoticons"), ForcedMute || Player.m_EmoticonIgnore, !ForcedMute},
		{EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Following ? Localize("Stop following") : Localize("Follow server"), Following,
			Online && !GameClient()->IsLocalClientId(SelectedId)},
	};
	// 分区几何只有一个来源：PlayerShowcaseLayout（展示区 / 立绘 / 名字 / 战队 / 图标行 / 皮肤行 / 状态行）。
	const auto ShowcaseLayout = QmPlayersUi::PlayerShowcaseLayout(Card, (int)std::size(aIconActions));
	const CUIRect Showcase = ShowcaseLayout.m_Showcase;
	CUIRect SkinRow = ShowcaseLayout.m_SkinRow;
	const CUIRect InfoRow = ShowcaseLayout.m_Info;
	DrawRoundedSurface(Ui(), Showcase, ui_token::color::SURFACE_OVERLAY, ui_token::color::BORDER_SUBTLE, ui_token::player_card::RADIUS, ui_token::feedback::ICON_BORDER_WIDTH);
	DrawTee(SelectedId, ShowcaseLayout.m_Figure);
	Label(ShowcaseLayout.m_Name, aName, ui_token::font::HEADLINE, TEXTALIGN_MC);
	if(aClan[0] != '\0')
		Label(ShowcaseLayout.m_Clan, aClan, ui_token::font::BODY, TEXTALIGN_MC, true);

	static CButtonContainer s_aIconButtons[std::size(aIconActions)];
	// 槽位由 PlayerShowcaseIconRow 统一算：从右往左排，窄卡等比缩小而不是把按钮挤出行外。
	const auto IconSlots = QmPlayersUi::PlayerShowcaseIconRow(ShowcaseLayout.m_Icons, (int)std::size(aIconActions));
	for(int Action = (int)std::size(aIconActions) - 1; Action >= 0; --Action)
	{
		const CUIRect Slot = IconSlots.m_aCells[Action];
		const auto &Spec = aIconActions[Action];
		if(!Spec.m_Enabled)
			continue;
		const int Clicked = Ui()->DoButton_QmIcon(&s_aIconButtons[Action], Spec.m_Icon, Spec.m_pFallback, Spec.m_Active ? 1 : 0, &Slot, BUTTONFLAG_LEFT);
		ui_widget::Tooltip(Ctx, &s_aIconButtons[Action], Slot, Spec.m_pTooltip);
		if(Clicked == 0)
			continue;
		switch(Action)
		{
		case 0:
			if(Player.m_Friend)
				GameClient()->Friends()->RemoveFriend(Player.m_aName, Player.m_aClan);
			else
				GameClient()->Friends()->AddFriend(Player.m_aName, Player.m_aClan);
			GameClient()->Client()->ServerBrowserUpdate();
			break;
		case 1:
			Player.m_ChatIgnore ^= 1;
			break;
		case 2:
			Player.m_EmoticonIgnore ^= 1;
			break;
		case 3:
			StopFriendAutoFollow(m_FriendAutoFollowState);
			if(Following)
				GameClient()->m_PieMenu.CancelFollow();
			else
				GameClient()->m_PieMenu.ToggleFollowPlayer(SelectedId);
			break;
		default:
			break;
		}
	}

	// 展示区下方：皮肤名 + 复制按钮（复制的是玩家真实皮肤名，与计分板「Copy skin ID」同源）。
	CUIRect SkinLabel, CopyButton;
	SkinRow.VSplitRight(ui_token::player_card::SHOWCASE_ICON_SIZE, &SkinLabel, &CopyButton);
	SkinRow.VSplitRight(std::min(ui_token::spacing::XS, SkinLabel.w), &SkinLabel, nullptr);
	Label(SkinLabel, aSkinName, ui_token::font::BODY, TEXTALIGN_ML, true);
	static CButtonContainer s_SkinCopyButton;
	const bool CanCopySkin = Online && !Sixup;
	if(Ui()->DoButton_QmIcon(&s_SkinCopyButton, EQmIcon::COPY, FontIcons::FONT_ICON_COPY, 0, &CopyButton, CanCopySkin ? BUTTONFLAG_LEFT : 0) != 0 && CanCopySkin)
	{
		if(QmCopyScoreboardSkin(g_Config, Sixup, Player.m_aSkinName, Player.m_UseCustomColor, Player.m_ColorBody, Player.m_ColorFeet))
		{
			if(g_Config.m_ClDummy)
				GameClient()->SendDummyInfo(false);
			else
				GameClient()->SendInfo(false);
			State.m_CopyToastUntil = Client()->GlobalTime() + ui_token::player_card::COPY_TOAST_SECONDS;
		}
	}
	if(!CanCopySkin)
		Label(InfoRow, Localize("Skin copying is only available for 0.6 skins"), ui_token::font::CAPTION, TEXTALIGN_ML, true);
	else
	{
		// 状态与分数压成一行短信息，不再用五行「标签: 值」的表格。
		char aStatus[96];
		str_format(aStatus, sizeof(aStatus), "%s · %s · %d",
			Online ? Localize("Online") : Localize("Offline"),
			Player.m_Team != TEAM_SPECTATORS ? Localize("Join game") : Localize("Spectate"),
			GameClient()->m_Snap.m_apPlayerInfos[SelectedId] != nullptr ? GameClient()->m_Snap.m_apPlayerInfos[SelectedId]->m_Score : 0);
		Label(InfoRow, aStatus, ui_token::player_card::DETAIL_FONT_SIZE, TEXTALIGN_ML, true);
	}

	// 复制成功的轻提示：统一一条文案，出现在页面底部，不占用卡片布局。
	const bool ToastVisible = Client()->GlobalTime() < State.m_CopyToastUntil;
	ui_widget::SToastProps ToastProps;
	ToastProps.m_pText = Localize("Copied");
	static ui_widget::SToastState s_CopyToast;
	ui_widget::Toast(Ctx, &s_CopyToast, &s_CopyToast, ToastVisible, PageRect, ToastProps);
}
