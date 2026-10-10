#include "QmVoteMenu.h"

#include "QmTree.h"
#include "UiForms.h"
#include "UiSurface.h"

#include <base/math.h>
#include <base/secure.h>

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	const char *const s_apCategories[] = {Localizable("Novice"), Localizable("Moderate"), Localizable("Brutal"), Localizable("Insane"), Localizable("Dummy"), "DDmaX", "DDmaX Easy", "DDmaX Next", "DDmaX Pro", "DDmaX Nut", Localizable("Oldschool"), Localizable("Solo"), Localizable("Race"), Localizable("Fun"), Localizable("Event")};

	const char *CategoryLabel(const char *pKey)
	{
		if(str_comp(pKey, "DDmaX Easy") == 0)
			return Localize("Classic Easy");
		if(str_comp(pKey, "DDmaX Next") == 0)
			return Localize("Classic Next");
		if(str_comp(pKey, "DDmaX Pro") == 0)
			return Localize("Classic Pro");
		if(str_comp(pKey, "DDmaX Nut") == 0)
			return Localize("Classic Nut");
		return Localize(pKey);
	}

	QmVoteMaps::ECompletion CompletionFor(const CCommunity *pCommunity, const char *pName)
	{
		if(!g_Config.m_BrIndicateFinished || pCommunity == nullptr || !pCommunity->HasRanks())
			return QmVoteMaps::ECompletion::UNKNOWN;
		const auto Rank = pCommunity->HasRank(pName);
		if(Rank == CServerInfo::RANK_RANKED)
			return QmVoteMaps::ECompletion::FINISHED;
		if(Rank == CServerInfo::RANK_UNRANKED)
			return QmVoteMaps::ECompletion::UNFINISHED;
		return QmVoteMaps::ECompletion::UNKNOWN;
	}
}

void CMenus::RenderServerControl(CUIRect MainView)
{
	CUIRect Navigation, Button;
	MainView.HSplitTop(26.0f, &Navigation, &MainView);
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	Navigation.VSplitLeft(std::min(180.0f, Navigation.w * 0.5f), &Button, &Navigation);
	static CButtonContainer s_Maps, s_Control;
	if(DoButton_Menu(&s_Maps, Localize("DDNet map library"), m_QmVoteMenu.m_ShowLibrary, &Button))
		m_QmVoteMenu.m_ShowLibrary = true;
	Navigation.VSplitLeft(6.0f, nullptr, &Navigation);
	Navigation.VSplitLeft(std::min(180.0f, Navigation.w), &Button, nullptr);
	if(DoButton_Menu(&s_Control, Localize("Server control"), !m_QmVoteMenu.m_ShowLibrary, &Button))
		m_QmVoteMenu.m_ShowLibrary = false;
	if(m_QmVoteMenu.m_ShowLibrary)
		RenderVoteMapLibrary(MainView);
	else
	{
		m_QmVoteMenu.m_Loader.Suspend();
		RenderServerControlLegacy(MainView);
	}
}

void CMenus::RenderVoteMapLibrary(CUIRect MainView)
{
	auto &State = m_QmVoteMenu;
	auto &Voting = GameClient()->m_Voting;
	const CServerInfo &Server = Client()->ServerInfo();
	const CCommunity *pCommunity = ServerBrowser()->Community(Server.m_aCommunityId);
	// DDNet 元数据不能套在 Gores 等同名地图上；这些服仍保留原始投票入口。
	const bool UseCatalog = (str_comp_nocase(Server.m_aGameType, "DDNet") == 0 || str_comp_nocase(Server.m_aGameType, "DDRaceNetwork") == 0 || str_comp_nocase(Server.m_aGameType, "DDRace") == 0) &&
		str_comp_nocase(Server.m_aCommunityId, "kog") != 0 && str_find_nocase(Server.m_aCommunityType, "gores") == nullptr;
	if(UseCatalog)
		State.m_Loader.Update(Http(), Engine(), Client()->LocalTime());
	else
		State.m_Loader.Suspend();
	if((State.m_OptionsRevision != Voting.OptionsRevision() && !Voting.IsReceivingOptions()) || State.m_CatalogRevision != State.m_Loader.Revision() || State.m_UseCatalog != UseCatalog)
	{
		// 仅清理即将失效的行 ID，后台刷新不能打断搜索框输入。
		for(const auto &Map : State.m_vMaps)
		{
			if(Ui()->HotItem() == &Map || Ui()->HotItem() == &Map.m_Points)
				Ui()->SetHotItem(nullptr);
			if(Ui()->ActiveItem() == &Map || Ui()->ActiveItem() == &Map.m_Points)
				Ui()->SetActiveItem(nullptr);
		}
		const std::vector<QmVoteMaps::SMap> vEmpty;
		State.m_vMaps = QmVoteMaps::MergeCatalog(UseCatalog ? State.m_Loader.Maps() : vEmpty, Voting.FirstOption());
		State.m_OptionsRevision = Voting.OptionsRevision();
		State.m_CatalogRevision = State.m_Loader.Revision();
		State.m_UseCatalog = UseCatalog;
		State.m_Dirty = true;
	}
	if(State.m_Favorites != GameClient()->m_TClient.GetFavoriteMaps())
	{
		State.m_Favorites = GameClient()->m_TClient.GetFavoriteMaps();
		State.m_Dirty = true;
	}
	const int FinishedCount = pCommunity != nullptr && g_Config.m_BrIndicateFinished && pCommunity->HasRanks() ? pCommunity->NumFinishedMaps() : -1;
	if(State.m_FinishedCount != FinishedCount || State.m_RankPlayer != Client()->PlayerName() || State.m_Community != Server.m_aCommunityId)
	{
		State.m_FinishedCount = FinishedCount;
		State.m_RankPlayer = Client()->PlayerName();
		State.m_Community = Server.m_aCommunityId;
		State.m_Dirty = true;
	}
	if(FinishedCount < 0 && State.m_Filter.m_Completion != QmVoteMaps::ECompletion::UNKNOWN)
	{
		State.m_Filter.m_Completion = QmVoteMaps::ECompletion::UNKNOWN;
		State.m_Dirty = true;
	}

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_ALL, 10.0f);
	CUiScopedSurfaceText SurfaceText(TextRender(), ms_ColorTabbarActive);
	MainView.Margin(10.0f, &MainView);
	const bool CompactFilters = MainView.w < 460.0f;
	CUIRect Header, SearchRow, FilterRow, Bottom, Details, Sidebar, List;
	MainView.HSplitTop(26.0f, &Header, &MainView);
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(26.0f, &SearchRow, &MainView);
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(CompactFilters ? 54.0f : 24.0f, &FilterRow, &MainView);
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.HSplitBottom(30.0f, &MainView, &Bottom);
	MainView.HSplitBottom(6.0f, &MainView, nullptr);
	const bool ShowDetails = MainView.h >= 176.0f;
	if(ShowDetails)
	{
		MainView.HSplitBottom(88.0f, &MainView, &Details);
		MainView.HSplitBottom(6.0f, &MainView, nullptr);
	}

	auto Label = [&](CUIRect Rect, const char *pText, float FontSize = 12.0f, int Align = TEXTALIGN_ML) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Rect, pText, FontSize, Align, Props);
	};
	CUIRect Refresh, Title;
	Header.VSplitRight(82.0f, &Title, &Refresh);
	static CButtonContainer s_Refresh;
	if(DoButton_Menu(&s_Refresh, State.m_Loader.Loading() ? Localize("Loading…") : Localize("Refresh"), UseCatalog && !State.m_Loader.Loading() ? 0 : -1, &Refresh))
		State.m_Loader.Refresh(Http());
	Label(Title, State.m_Loader.Error() ? Localize("Map data unavailable; server votes remain available") : Localize("Browse, choose, vote"), 16.0f);
	CUIRect Search, Exclude;
	SearchRow.VSplitMid(&Search, &Exclude, 6.0f);
	IUiContext Ctx;
	Ctx.m_pUi = Ui();
	Ctx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Ctx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_vote_map_library_search");
	Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	InputOptions.m_Clearable = true;
	InputOptions.m_FontSize = 13.0f;
	InputOptions.m_pPlaceholder = Localize("Search maps or mappers");
	InputOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	if(m_ControlPageOpening)
	{
		m_ControlPageOpening = false;
		Ui()->SetActiveItem(&m_FilterInput);
		m_FilterInput.SelectAll();
	}
	State.m_Dirty |= ui_widget::InputField(Ctx, &m_FilterInput, Search, InputOptions).m_Changed;
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_vote_map_library_exclude");
	InputOptions.m_pPlaceholder = Localize("Exclude");
	InputOptions.m_SearchHotkeyEnabled = false;
	State.m_Dirty |= ui_widget::InputField(Ctx, &m_ExcludeInput, Exclude, InputOptions).m_Changed;
	// 与原始投票页共用搜索字段，切换入口后仍保持同一组条件。
	if(State.m_Filter.m_Search != m_FilterInput.GetString() || State.m_Filter.m_Exclude != m_ExcludeInput.GetString())
		State.m_Dirty = true;
	State.m_Filter.m_Search = m_FilterInput.GetString();
	State.m_Filter.m_Exclude = m_ExcludeInput.GetString();

	CUIRect SortButton, CompletionButton, FavoriteButton, Stars = FilterRow, Controls = FilterRow;
	if(CompactFilters)
	{
		FilterRow.HSplitTop(24.0f, &Stars, &Controls);
		Controls.HSplitTop(6.0f, nullptr, &Controls);
	}
	const float FilterButtonWidth = std::min(104.0f, FilterRow.w * (CompactFilters ? 0.32f : 0.19f));
	Controls.VSplitRight(FilterButtonWidth, &Controls, &SortButton);
	Controls.VSplitRight(5.0f, &Controls, nullptr);
	Controls.VSplitRight(FilterButtonWidth, &Controls, &CompletionButton);
	Controls.VSplitRight(5.0f, &Controls, nullptr);
	Controls.VSplitRight(FilterButtonWidth, &Controls, &FavoriteButton);
	Controls.VSplitRight(5.0f, &Controls, nullptr);
	if(!CompactFilters)
		Stars = Controls;
	static CButtonContainer s_aStars[7], s_Favorites, s_Completion, s_Sort;
	const float StarWidth = Stars.w / 7.0f;
	for(int Star = -1; Star <= 5; ++Star)
	{
		CUIRect Chip;
		Stars.VSplitLeft(StarWidth, &Chip, &Stars);
		Chip.VMargin(1.0f, &Chip);
		char aStar[16];
		str_format(aStar, sizeof(aStar), "%d★", Star);
		const int Mask = Star < 0 ? 0 : 1 << Star;
		const bool Active = Star < 0 ? State.m_Filter.m_StarMask == 0 : (State.m_Filter.m_StarMask & Mask) != 0;
		if(DoButton_Menu(&s_aStars[Star + 1], Star < 0 ? Localize("All") : aStar, Active, &Chip))
		{
			State.m_Filter.m_StarMask = Star < 0 ? 0 : State.m_Filter.m_StarMask ^ Mask;
			State.m_Dirty = true;
		}
	}
	if(DoButton_Menu(&s_Favorites, Localize("Favorites"), State.m_Filter.m_FavoritesOnly, &FavoriteButton))
	{
		State.m_Filter.m_FavoritesOnly = !State.m_Filter.m_FavoritesOnly;
		State.m_Dirty = true;
	}
	const char *apCompletion[] = {Localize("All"), Localize("Unfinished"), Localize("Finished")};
	if(DoButton_Menu(&s_Completion, apCompletion[(int)State.m_Filter.m_Completion], FinishedCount < 0 ? -1 : State.m_Filter.m_Completion != QmVoteMaps::ECompletion::UNKNOWN, &CompletionButton))
	{
		State.m_Filter.m_Completion = (QmVoteMaps::ECompletion)(((int)State.m_Filter.m_Completion + 1) % 3);
		State.m_Dirty = true;
	}
	if(FinishedCount < 0)
		GameClient()->m_Tooltips.DoToolTip(&s_Completion, &CompletionButton, Localize("Enable the finished indicator and wait for player data"));
	const char *apSort[] = {Localize("Name"), Localize("Difficulty"), Localize("Newest first")};
	if(DoButton_Menu(&s_Sort, apSort[(int)State.m_Sort], 0, &SortButton))
	{
		State.m_Sort = (QmVoteMaps::ESort)(((int)State.m_Sort + 1) % 3);
		State.m_Dirty = true;
	}

	const bool ShowSidebar = MainView.w >= 500.0f;
	List = MainView;
	if(ShowSidebar)
	{
		MainView.VSplitLeft(112.0f, &Sidebar, &List);
		List.VSplitLeft(8.0f, nullptr, &List);
		Sidebar.Draw(ColorRGBA(0, 0, 0, 0.15f), IGraphics::CORNER_ALL, 8.0f);
		Sidebar.Margin(5.0f, &Sidebar);
		static CListBox s_Categories;
		int SelectedCategory = 0;
		for(size_t i = 0; i < std::size(s_apCategories); ++i)
			if(State.m_Filter.m_Category == s_apCategories[i])
				SelectedCategory = (int)i + 1;
		s_Categories.SetActive(!Ui()->IsPopupOpen() && Ui()->MouseInside(&Sidebar) && !m_FilterInput.IsActive() && !m_ExcludeInput.IsActive() && !m_CallvoteReasonInput.IsActive());
		s_Categories.DoStart(22.0f, (int)std::size(s_apCategories) + 1, 1, 3, SelectedCategory, &Sidebar, false);
		static int s_AllCategory;
		for(size_t i = 0; i <= std::size(s_apCategories); ++i)
		{
			const CListboxItem Item = s_Categories.DoNextItem(i == 0 ? (const void *)&s_AllCategory : (const void *)&s_apCategories[i - 1]);
			if(Item.m_Visible)
			{
				CUIRect Text = Item.m_Rect;
				Text.VMargin(5.0f, &Text);
				Label(Text, i == 0 ? Localize("All maps") : CategoryLabel(s_apCategories[i - 1]), 12.0f);
			}
		}
		const int NewCategory = s_Categories.DoEnd();
		if(NewCategory >= 0 && NewCategory != SelectedCategory)
		{
			State.m_Filter.m_Category = NewCategory == 0 ? "" : s_apCategories[NewCategory - 1];
			State.m_Dirty = true;
		}
	}
	else
	{
		// 窄视口把分类放到单行选择器，不挤压地图名。
		CUIRect CategoryRow;
		List.HSplitTop(24.0f, &CategoryRow, &List);
		List.HSplitTop(5.0f, nullptr, &List);
		static CButtonContainer s_Category;
		if(DoButton_Menu(&s_Category, State.m_Filter.m_Category.empty() ? Localize("All maps") : CategoryLabel(State.m_Filter.m_Category.c_str()), 0, &CategoryRow))
		{
			const auto It = std::find_if(std::begin(s_apCategories), std::end(s_apCategories), [&](const char *pCategory) { return State.m_Filter.m_Category == pCategory; });
			State.m_Filter.m_Category = It == std::end(s_apCategories) ? s_apCategories[0] : (It + 1 == std::end(s_apCategories) ? "" : *(It + 1));
			State.m_Dirty = true;
		}
	}
	if(State.m_Dirty)
	{
		State.m_vVisible.clear();
		for(size_t i = 0; i < State.m_vMaps.size(); ++i)
		{
			const auto &Map = State.m_vMaps[i];
			if(QmVoteMaps::Matches(Map, State.m_Filter, GameClient()->m_TClient.IsFavoriteMap(Map.m_Name.c_str()), CompletionFor(pCommunity, Map.m_Name.c_str())))
				State.m_vVisible.push_back((int)i);
		}
		QmVoteMaps::Sort(State.m_vVisible, State.m_vMaps, State.m_Sort);
		State.m_Dirty = false;
	}
	CUIRect Count, Random;
	List.HSplitTop(24.0f, &Count, &List);
	Count.VSplitRight(112.0f, &Count, &Random);
	char aCount[128];
	str_format(aCount, sizeof(aCount), Localize("%d / %d maps"), (int)State.m_vVisible.size(), (int)State.m_vMaps.size());
	Label(Count, aCount);
	static CButtonContainer s_Random;
	bool ScrollToSelection = false;
	if(DoButton_Menu(&s_Random, Localize("Random pick"), State.m_vVisible.empty() ? -1 : 0, &Random))
	{
		const int Index = QmVoteMaps::Pick(State.m_vVisible, secure_rand_below((int)State.m_vVisible.size()));
		if(Index >= 0)
			State.m_SelectedName = State.m_vMaps[Index].m_Name;
		State.m_Status.clear();
		ScrollToSelection = true;
	}
	List.HSplitTop(5.0f, nullptr, &List);
	const bool Wide = List.w >= 540.0f;
	const bool HasMapper = List.w >= 340.0f;
	auto Columns = [&](CUIRect Row) {
		std::array<CUIRect, 5> aRects{};
		Row.VMargin(5.0f, &Row);
		if(Wide)
		{
			Row.VSplitRight(86.0f, &Row, &aRects[4]);
			Row.VSplitRight(46.0f, &Row, &aRects[3]);
		}
		Row.VSplitRight(62.0f, &Row, &aRects[2]);
		if(HasMapper)
			Row.VSplitRight(Row.w * 0.38f, &Row, &aRects[1]);
		aRects[0] = Row;
		return aRects;
	};
	CUIRect TableHeader;
	List.HSplitTop(22.0f, &TableHeader, &List);
	TableHeader.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 5.0f);
	const auto aHead = Columns(TableHeader);
	Label(aHead[0], Localize("Map"));
	if(HasMapper)
		Label(aHead[1], Localize("Mapper"));
	Label(aHead[2], Localize("Difficulty"));
	if(Wide)
	{
		Label(aHead[3], Localize("Points"));
		Label(aHead[4], Localize("Release date"));
	}
	int Selected = -1;
	for(size_t i = 0; i < State.m_vVisible.size(); ++i)
		if(State.m_vMaps[State.m_vVisible[i]].m_Name == State.m_SelectedName)
			Selected = (int)i;
	static CListBox s_Maps;
	s_Maps.SetActive(!Ui()->IsPopupOpen() && !m_FilterInput.IsActive() && !m_ExcludeInput.IsActive() && !m_CallvoteReasonInput.IsActive());
	if(ScrollToSelection)
		s_Maps.ScrollToSelected();
	s_Maps.DoStart(25.0f, (int)State.m_vVisible.size(), 1, 3, Selected, &List, false);
	const int ItemCount = (int)State.m_vVisible.size();
	const float ScrollY = std::max(0.0f, s_Maps.ScrollOffsetY());
	const int FirstVisible = ScrollToSelection ? 0 : std::clamp((int)std::floor(ScrollY / 25.0f) - 1, 0, ItemCount);
	const int EndVisible = ScrollToSelection ? ItemCount : std::clamp((int)std::ceil((ScrollY + s_Maps.ViewHeight()) / 25.0f) + 1, FirstVisible, ItemCount);
	// 全目录仍参与滚动几何，只给可见行做命中测试和文字布局。
	s_Maps.SkipItems(FirstVisible);
	for(int Row = FirstVisible; Row < EndVisible; ++Row)
	{
		const int Index = State.m_vVisible[Row];
		const auto &Map = State.m_vMaps[Index];
		const CListboxItem Item = s_Maps.DoNextItem(&Map);
		if(!Item.m_Visible)
			continue;
		const auto aRow = Columns(Item.m_Rect);
		CUIRect MapLabel = aRow[0];
		CUIRect Marker;
		MapLabel.VSplitLeft(18.0f, &Marker, &MapLabel);
		const bool Favorite = GameClient()->m_TClient.IsFavoriteMap(Map.m_Name.c_str());
		const auto Completion = CompletionFor(pCommunity, Map.m_Name.c_str());
		Label(Marker, Favorite ? "★" : "☆");
		if(Ui()->DoButtonLogic(&Map.m_Points, 0, &Marker))
		{
			if(Favorite)
				GameClient()->m_TClient.RemoveFavoriteMap(Map.m_Name.c_str());
			else
				GameClient()->m_TClient.AddFavoriteMap(Map.m_Name.c_str());
			State.m_Dirty = true;
		}
		GameClient()->m_Tooltips.DoToolTip(&Map.m_Points, &Marker, Localize("Favorite map"));
		if(Completion == QmVoteMaps::ECompletion::FINISHED)
		{
			MapLabel.VSplitLeft(14.0f, &Marker, &MapLabel);
			Label(Marker, "✓");
		}
		Label(MapLabel, Map.m_Name.c_str(), 13.0f);
		if(HasMapper)
			Label(aRow[1], Map.m_Mapper.empty() ? "—" : Map.m_Mapper.c_str());
		char aValue[32];
		str_format(aValue, sizeof(aValue), "%d/5 ★", Map.m_Stars);
		Label(aRow[2], Map.m_Stars < 0 ? "—" : aValue);
		if(Wide)
		{
			str_format(aValue, sizeof(aValue), "%d", Map.m_Points);
			Label(aRow[3], Map.m_Points < 0 ? "—" : aValue);
			Label(aRow[4], Map.m_Release.empty() ? "—" : Map.m_Release.substr(0, 10).c_str());
		}
	}
	s_Maps.SkipItems(ItemCount - EndVisible);
	Selected = s_Maps.DoEnd();
	const bool Activated = s_Maps.WasItemActivated();
	const QmVoteMaps::SMap *pSelected = Selected >= 0 && Selected < (int)State.m_vVisible.size() ? &State.m_vMaps[State.m_vVisible[Selected]] : nullptr;
	const char *pNewSelectedName = pSelected != nullptr ? pSelected->m_Name.c_str() : "";
	if(State.m_SelectedName != pNewSelectedName)
	{
		State.m_Status.clear();
		State.m_SelectedName = pNewSelectedName;
	}
	State.m_Loader.Select(UseCatalog ? pSelected : nullptr, Client()->LocalTime());
	if(State.m_vVisible.empty())
		Label(List, State.m_Loader.Loading() ? Localize("Loading…") : Localize("No maps match these filters"), 14.0f, TEXTALIGN_MC);
	if(ShowDetails)
	{
		Details.Draw(ColorRGBA(0, 0, 0, 0.18f), IGraphics::CORNER_ALL, 8.0f);
		Details.Margin(8.0f, &Details);
		CUIRect Name, Metadata, Statistics, Favorite;
		Details.HSplitTop(22.0f, &Name, &Details);
		Name.VSplitRight(100.0f, &Name, &Favorite);
		Label(Name, pSelected != nullptr ? pSelected->m_Name.c_str() : Localize("Select a map"), 16.0f);
		static CButtonContainer s_Favorite;
		const bool IsFavorite = pSelected != nullptr && GameClient()->m_TClient.IsFavoriteMap(pSelected->m_Name.c_str());
		if(DoButton_Menu(&s_Favorite, Localize("Favorite map"), pSelected == nullptr ? -1 : (int)IsFavorite, &Favorite))
		{
			if(IsFavorite)
				GameClient()->m_TClient.RemoveFavoriteMap(pSelected->m_Name.c_str());
			else
				GameClient()->m_TClient.AddFavoriteMap(pSelected->m_Name.c_str());
			State.m_Dirty = true;
		}
		Details.HSplitTop(4.0f, nullptr, &Details);
		Details.HSplitTop(18.0f, &Metadata, &Details);
		Details.HSplitTop(4.0f, nullptr, &Statistics);
		if(pSelected != nullptr)
		{
			char aInfo[768];
			str_format(aInfo, sizeof(aInfo), "%s · %s · %s", pSelected->m_Category.empty() ? Localize("Unknown") : CategoryLabel(pSelected->m_Category.c_str()), pSelected->m_Mapper.c_str(), pSelected->m_Release.c_str());
			Label(Metadata, aInfo);
			str_format(aInfo, sizeof(aInfo), "%s: %s", Localize("Points"), pSelected->m_Points < 0 ? "—" : std::to_string(pSelected->m_Points).c_str());
			if(const auto *pData = State.m_Loader.Details())
			{
				char aStats[160];
				if(pData->m_Finishers >= 0 && pData->m_AverageSeconds >= 0)
				{
					const int Seconds = (int)pData->m_AverageSeconds;
					str_format(aStats, sizeof(aStats), Localize(" · %d finishers · average %d:%02d:%02d"), pData->m_Finishers, Seconds / 3600, Seconds / 60 % 60, Seconds % 60);
					str_append(aInfo, aStats, sizeof(aInfo));
				}
			}
			if(State.m_Loader.DetailsError())
			{
				CUIRect Retry;
				Statistics.VSplitRight(100.0f, &Statistics, &Retry);
				static CButtonContainer s_Retry;
				if(DoButton_Menu(&s_Retry, Localize("Retry details"), 0, &Retry))
					State.m_Loader.RetryDetails();
			}
			Label(Statistics, aInfo);
		}
	}

	CUIRect CallButton, Reason;
	Bottom.VSplitRight(std::min(130.0f, Bottom.w * 0.3f), &Bottom, &CallButton);
	Bottom.VSplitRight(8.0f, &Bottom, nullptr);
	Bottom.VSplitRight(Bottom.w * 0.45f, &Bottom, &Reason);
	InputOptions = {};
	InputOptions.m_pPlaceholder = Localize("Reason (optional)");
	InputOptions.m_FontSize = 13.0f;
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_vote_map_library_reason");
	if(Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed())
	{
		Ui()->SetActiveItem(&m_CallvoteReasonInput);
		m_CallvoteReasonInput.SelectAll();
	}
	ui_widget::InputField(Ctx, &m_CallvoteReasonInput, Reason, InputOptions);
	const bool HasPending = Voting.PendingMapVote()[0] != '\0';
	const bool CanCall = pSelected != nullptr && !HasPending && !Voting.IsVoting() && !Voting.IsReceivingOptions();
	if(HasPending)
	{
		CUIRect Cancel;
		Bottom.VSplitRight(std::min(65.0f, Bottom.w * 0.4f), &Bottom, &Cancel);
		static CButtonContainer s_Cancel;
		if(DoButton_Menu(&s_Cancel, Localize("Cancel"), 0, &Cancel))
			Voting.ClearUnfinishedMapVoteChain();
		Label(Bottom, Voting.PendingMapVote());
	}
	else
		Label(Bottom, State.m_Status.empty() ? (pSelected != nullptr ? pSelected->m_Name.c_str() : Localize("Select a map")) : Localize(State.m_Status.c_str()));
	static CButtonContainer s_Call;
	if((DoButton_Menu(&s_Call, Localize("Call vote"), CanCall ? 1 : -1, &CallButton) || Activated) && CanCall)
	{
		// 每次提交重新解析当前服务器列表，不能沿用刷新前的索引。
		const int Option = QmVoteMaps::FindOption(Voting.FirstOption(), pSelected->m_Name.c_str());
		if(Option >= 0)
		{
			Voting.ClearUnfinishedMapVoteChain();
			Voting.CallvoteOption(Option, m_CallvoteReasonInput.GetString());
			State.m_Status.clear();
			m_CallvoteReasonInput.Clear();
			if(g_Config.m_UiCloseWindowAfterChangingSetting)
				SetActive(false);
		}
		else
		{
			const auto Action = Voting.StartUnfinishedMapVoteChain(pSelected->m_Name.c_str(), pSelected->m_Category.c_str(), CategoryLabel(pSelected->m_Category.c_str()), m_CallvoteReasonInput.GetString());
			State.m_Status = Action == CVoting::EUnfinishedMapVoteAction::NO_OPTION ? Localizable("No corresponding vote option found") : Localizable("Type switch vote started automatically");
			if(Action != CVoting::EUnfinishedMapVoteAction::NO_OPTION)
				m_CallvoteReasonInput.Clear();
		}
	}
}
