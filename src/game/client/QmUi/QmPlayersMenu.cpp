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
	// 顶部一行与 ESC 顶部导航同高：筛胶囊、搜索框、在线人数都压在这一档里。
	MainView.HSplitTop(ui_token::settings::MENU_NAV_HEIGHT, &TopRow, &MainView);
	MainView.HSplitTop(ui_token::panel::GAP, nullptr, &MainView);
	const float RightWidth = std::min(TopRow.w * 0.62f,
		ui_token::player_card::SEARCH_WIDTH + ui_token::player_card::COUNT_WIDTH + ui_token::spacing::SM * 2.0f);
	TopRow.VSplitRight(RightWidth, &FilterRow, &RightSide);
	FilterRow.VSplitRight(std::min(ui_token::spacing::SM, FilterRow.w), &FilterRow, nullptr);
	RightSide.VSplitRight(std::min(ui_token::player_card::COUNT_WIDTH, RightSide.w * 0.4f), &RightSide, &CountLabel);
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
		aIds[Count++] = Id;
	}

	// ── 列表：与皮肤列表同一种多列小卡——每格一名玩家，左边头像、名字与战队两行、
	// 右侧一排动作图标。没有选中态，也没有右侧展示页。
	CUIRect ListArea = MainView;
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const bool Sixup = Client()->IsSixup();
	auto DrawTee = [&](int Id, const CUIRect &Rect) {
		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
		TeeInfo.m_Size = Rect.h;
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, Offset);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), Rect.Center() + Offset);
	};
	struct STileAction
	{
		EQmIcon m_Icon;
		const char *m_pFallback;
		const char *m_pTooltip;
		bool m_Active;
		bool m_Enabled;
	};

	const int Columns = QmPlayersUi::PlayerTileColumns(ListArea.w);
	static CListBox s_Players;
	s_Players.DoAutoSpacing(ui_token::list::ROW_SPACING);
	s_Players.SetActive(!Ui()->IsPopupOpen() && !State.m_Search.IsActive());
	// 行悬浮反馈由卡片自己按卡片矩形画：列表自带的那层按整格绘制，左右会超出卡片。
	s_Players.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ColorRGBA());
	s_Players.DoStart(ui_token::player_card::TILE_HEIGHT, Count, Columns, 1, -1, &ListArea, false);
	static CButtonContainer s_aTileButtons[MAX_CLIENTS][5];
	for(int i = 0; i < Count; ++i)
	{
		const int Id = aIds[i];
		auto &Player = GameClient()->m_aClients[Id];
		const CListboxItem Item = s_Players.DoNextItem(&Player, false, ui_token::list::RowRadius(s_Players.RowHeight()));
		if(!Item.m_Visible)
			continue;
		auto Blur = Item.SuppressGaussianBlur();
		const CUIRect Tile = Item.m_ContentRect;
		const auto Layout = QmPlayersUi::PlayerTileLayout(Tile, 5);
		// 卡片底与悬浮反馈都按卡片自身矩形画：悬停判定用 UI 坐标，
		// 不用原生鼠标坐标（两者在缩放/偏移下不是同一空间，会永远判不中）。
		const bool TileHovered = Ui()->MouseInside(&Tile);
		Tile.Draw(ui_token::color::SURFACE_HIGHLIGHT, IGraphics::CORNER_ALL, ui_token::list::RowRadius(Tile.h));
		if(TileHovered)
			Tile.Draw(ui_token::color::LIST_ITEM_HOVER, IGraphics::CORNER_ALL, ui_token::list::RowRadius(Tile.h));

		char aName[MAX_NAME_LENGTH], aClan[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Id, aName, sizeof(aName));
		GameClient()->FormatStreamerClan(Id, aClan, sizeof(aClan));
		DrawTee(Id, QmUiSquareIconButtonRect(Layout.m_Avatar));
		Label(Layout.m_Name, aName, ui_token::font::BODY);
		Label(Layout.m_Clan, aClan, ui_token::font::SMALL, TEXTALIGN_ML, true);

		const bool Following = GameClient()->m_PieMenu.IsFollowingPlayer(Player.m_aName, Player.m_aClan) ||
				       (m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Player.m_aName) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Player.m_aClan) == 0);
		const bool ForcedMute = g_Config.m_ClShowChatFriends && !Player.m_Friend;
		const bool ForcedChatMute = ForcedMute && !g_Config.m_QmChatRelatedOnly;
		const bool CanCopySkin = Online && !Sixup;
		const STileAction aActions[] = {
			{EQmIcon::HEART, FontIcons::FONT_ICON_HEART, Localize("Friends"), Player.m_Friend, true},
			{EQmIcon::BAN, FontIcons::FONT_ICON_BAN, Localize("Mute"), ForcedChatMute || Player.m_ChatIgnore, !ForcedChatMute},
			{EQmIcon::HEART_CRACK, FontIcons::FONT_ICON_HEART_CRACK, Localize("Mute emoticons"), ForcedMute || Player.m_EmoticonIgnore, !ForcedMute},
			{Following ? EQmIcon::STOP : EQmIcon::PLAY, Following ? FontIcons::FONT_ICON_STOP : FontIcons::FONT_ICON_PLAY,
				Following ? Localize("Stop following") : Localize("Follow server"), Following,
				Online && !GameClient()->IsLocalClientId(Id)},
			{EQmIcon::COPY, FontIcons::FONT_ICON_COPY, Localize("Copy skin"), false, CanCopySkin},
		};
		// 图标默认不占视线：只有悬浮到这张卡上才出现；已经生效的状态
		//（好友 / 静音 / 裂心 / 跟随中）常显，复制不属于状态，只在悬浮时出现。
		const auto Slots = QmPlayersUi::PlayerTileIconRow(Layout.m_Icons, (int)std::size(aActions));
		for(int Action = (int)std::size(aActions) - 1; Action >= 0; --Action)
		{
			const CUIRect Slot = Slots.m_aCells[Action];
			const auto &Spec = aActions[Action];
			// 悬浮时五个动作都出现；非悬浮只保留已生效的状态（好友 / 静音 / 裂心 / 跟随中）。
			if(!TileHovered && !(Action < 4 && Spec.m_Active))
				continue;
			const int Clicked = Ui()->DoButton_QmIcon(&s_aTileButtons[Id][Action], Spec.m_Icon, Spec.m_pFallback, Spec.m_Active ? 1 : 0, &Slot, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Spec.m_Enabled);
			ui_widget::Tooltip(Ctx, &s_aTileButtons[Id][Action], Slot, Spec.m_pTooltip);
			if(Clicked == 0 || !Spec.m_Enabled)
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
					GameClient()->m_PieMenu.ToggleFollowPlayer(Id);
				break;
			case 4:
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
	}
	s_Players.DoEnd();
	if(Count == 0)
		Label(ListArea, Localize("No players match these filters"), ui_token::font::HEADLINE, TEXTALIGN_MC);

	// 复制成功的轻提示统一一条文案，出现在页面底部。
	{
		const bool ToastVisible = Client()->GlobalTime() < State.m_CopyToastUntil;
		ui_widget::SToastProps ToastProps;
		ToastProps.m_pText = Localize("Copied");
		static ui_widget::SToastState s_CopyToast;
		ui_widget::Toast(Ctx, &s_CopyToast, &s_CopyToast, ToastVisible, PageRect, ToastProps);
	}
}
