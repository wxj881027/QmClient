// QmNewUi 菜单源码合同：cards 战争列表卡组。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuCardsWarListContract, UsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_WarListPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("str_startswith(m_SettingsCardFocusStableId.c_str(), \"deck:tclient-warlist\")"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\tui_widget::InputField"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("LeftView.VSplitMid(&Column1, &Column2, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("RightView.VSplitMid(&Column3, &Column4, Margin);"), std::string::npos);

	EXPECT_NE(Registry.find("{\"deck:tclient-warlist\", \"tclient-warlist\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_NE(Body.find("const float FourColumnMinWidth = 4.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("const float TwoColumnMinWidth = 2.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("WarListMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("constexpr int WarListViewportRows = 8;"), std::string::npos);
	EXPECT_NE(Body.find("const float EntriesHeight = LineSize * 2.0f + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("Column.HSplitTop(WarListViewportRows * ListRowHeight, &WarTypeList, &Column);"), std::string::npos);
	EXPECT_NE(Body.find("const float PlayersHeight = LineSize + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\t\tRenderTeeCute"), std::string::npos);
	EXPECT_NE(Body.find("RenderWarListLayout(ContentRect, true);"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Entries\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Groups\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Edit Entry\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Online Players\")"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-entries\", pWarEntriesTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EditorColumn, \"tclient-warlist-section-editor\", pEditEntryTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(GroupsColumn, \"tclient-warlist-section-groups\", pWarGroupsTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(PlayersColumn, \"tclient-warlist-section-players\", pOnlinePlayersTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-settings\", pSettingsTitle"), std::string::npos);
	EXPECT_NE(Body.find("PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size()"), std::string::npos);
	EXPECT_EQ(Body.find("PlayerListBox.DoStart(ListRowHeight, MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(Body.find("maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight, EditorSectionHeight)"), std::string::npos);
	EXPECT_NE(Body.find("SecondRow.VSplitMid(&GroupsColumn, &PlayersColumn, SectionGap);"), std::string::npos);
	const size_t TwoColumnBranch = Body.find("else if(ContentRect.w >= TwoColumnMinWidth)");
	ASSERT_NE(TwoColumnBranch, std::string::npos);
	const size_t SingleColumnBranch = Body.find("\t\telse\n\t\t{", TwoColumnBranch);
	ASSERT_NE(SingleColumnBranch, std::string::npos);
	const size_t SingleEntries = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-entries\"", SingleColumnBranch);
	const size_t SingleSettings = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-settings\"", SingleColumnBranch);
	const size_t SingleEditor = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-editor\"", SingleColumnBranch);
	const size_t SingleGroups = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-groups\"", SingleColumnBranch);
	const size_t SinglePlayers = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-players\"", SingleColumnBranch);
	ASSERT_NE(SingleEntries, std::string::npos);
	ASSERT_NE(SingleSettings, std::string::npos);
	ASSERT_NE(SingleEditor, std::string::npos);
	ASSERT_NE(SingleGroups, std::string::npos);
	ASSERT_NE(SinglePlayers, std::string::npos);
	EXPECT_LT(SingleEntries, SingleSettings);
	EXPECT_LT(SingleSettings, SingleEditor);
	EXPECT_LT(SingleEditor, SingleGroups);
	EXPECT_LT(SingleGroups, SinglePlayers);
	EXPECT_EQ(Body.find("deck:tclient-warlist-entries"), std::string::npos);

	const size_t EntriesPriority = Body.find("EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t EntriesStart = Body.find("EntriesListBox.DoStart(");
	const size_t GroupsPriority = Body.find("WarTypeListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GroupsStart = Body.find("WarTypeListBox.DoStart(");
	const size_t PlayersPriority = Body.find("PlayerListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t PlayersStart = Body.find("PlayerListBox.DoStart(");
	ASSERT_NE(EntriesPriority, std::string::npos);
	ASSERT_NE(EntriesStart, std::string::npos);
	ASSERT_NE(GroupsPriority, std::string::npos);
	ASSERT_NE(GroupsStart, std::string::npos);
	ASSERT_NE(PlayersPriority, std::string::npos);
	ASSERT_NE(PlayersStart, std::string::npos);
	EXPECT_LT(EntriesPriority, EntriesStart);
	EXPECT_LT(GroupsPriority, GroupsStart);
	EXPECT_LT(PlayersPriority, PlayersStart);
}
