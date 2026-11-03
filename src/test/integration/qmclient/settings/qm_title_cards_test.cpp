#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{
	constexpr const char *SPONSOR_CARD = "deck:qmclient-contributors-title";
	constexpr const char *DISPLAY_CARD = "deck:qmclient-contributors-title-display";
}

TEST(QmTitleCards, SponsorStaysInCreditsAndDisplayMovesToNameplateSettings)
{
	const auto &vIds = qm_card_catalog::TitleCardStableIds();
	ASSERT_EQ(vIds.size(), 2u);
	for(const char *pId : {SPONSOR_CARD, DISPLAY_CARD})
	{
		SCOPED_TRACE(pId);
		EXPECT_TRUE(std::any_of(vIds.begin(), vIds.end(), [pId](const char *pCandidate) { return std::string(pCandidate) == pId; }));
		EXPECT_TRUE(qm_card_catalog::HasCardModule(pId));
		const auto *pDefault = qm_card_registry::FindByStableId(pId);
		ASSERT_NE(pDefault, nullptr);
		const bool Display = std::string(pId) == DISPLAY_CARD;
		EXPECT_STREQ(pDefault->m_pDefaultTab, Display ? "appearance-name-plate" : "credits-qmclient");
		EXPECT_EQ(pDefault->m_DefaultOrder, Display ? 2 : 0);
	}
	EXPECT_EQ(qm_card_registry::FindByStableId(SPONSOR_CARD)->m_DefaultColumn, qm_card_registry::ECardColumn::Left);
	EXPECT_EQ(qm_card_registry::FindByStableId(DISPLAY_CARD)->m_DefaultColumn, qm_card_registry::ECardColumn::Left);
}

TEST(QmTitleCards, MissingSettingNamesFindTheInteractiveDisplayCard)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"头衔显示", "qm_nameplate_title_scale", "头衔大小", "qm_show_nameplate_title", "qm_nameplate_title_above_name", "qm_show_main_title", "qm_show_dummy_title", "qm_title_badge_style", "qm_title_badge_color", "qm_title_opacity", "Moon", "Sun", "月亮", "太阳", "星轨"})
	{
		SCOPED_TRACE(pQuery);
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [](const auto &Result) { return std::string(Result.m_pStableId) == DISPLAY_CARD; });
		ASSERT_NE(It, vResults.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "appearance-name-plate");
		EXPECT_STREQ(It->m_Target.m_pStableId, DISPLAY_CARD);
		EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
	}
}

TEST(QmTitleCards, OldDefaultLayoutMovesDisplayToNameplatesAndSurvivesReload)
{
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-community|credits-qmclient|left|0;"
		"deck:qmclient-contributors-title|credits-qmclient|left|1;"
		"deck:qmclient-contributors-title-display|credits-qmclient|right|1;"
		"deck:qmclient-contributors-sponsors|credits-qmclient|right|0;",
		vDefaults));
	ASSERT_TRUE(qm_card_registry::RepairLegacyTitleLayout(Model));
	ASSERT_TRUE(qm_card_registry::RepairLegacyNameplateTitleLayout(Model));
	char aSerialized[32768];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, vDefaults));
	EXPECT_FALSE(qm_card_registry::RepairLegacyTitleLayout(Reloaded));
	EXPECT_FALSE(qm_card_registry::RepairLegacyNameplateTitleLayout(Reloaded));
	for(const char *pId : {SPONSOR_CARD, DISPLAY_CARD})
	{
		const int Index = Reloaded.FindByStableId(pId);
		ASSERT_GE(Index, 0);
		const auto &Entry = Reloaded.Entry(Index);
		const bool Display = std::string(pId) == DISPLAY_CARD;
		EXPECT_STREQ(Entry.m_pDefaultTab, Display ? "appearance-name-plate" : "credits-qmclient");
		EXPECT_EQ(Entry.m_OrderInColumn, Display ? 2 : 0);
		EXPECT_EQ(Entry.m_Column, 1);
	}
}

TEST(QmTitleCards, BothLegacyDisplayPositionsMigrateWithoutRequiringALayoutVersion)
{
	for(const int OldOrder : {0, 1})
	{
		SCOPED_TRACE(OldOrder);
		qm_card_order::CModel Model;
		Model.SetEntries(qm_card_registry::BuildDefaultEntries());
		Model.MoveToTab(DISPLAY_CARD, "credits-qmclient", 2, OldOrder);
		ASSERT_TRUE(qm_card_registry::RepairLegacyNameplateTitleLayout(Model));
		const auto &Entry = Model.Entry(Model.FindByStableId(DISPLAY_CARD));
		EXPECT_STREQ(Entry.m_pDefaultTab, "appearance-name-plate");
		EXPECT_EQ(Entry.m_Column, 1);
		EXPECT_EQ(Entry.m_OrderInColumn, 2);
		EXPECT_FALSE(qm_card_registry::RepairLegacyNameplateTitleLayout(Model));
	}
}

TEST(QmTitleCards, DisplayMigrationPreservesCustomizedPlacement)
{
	for(const char *pTab : {"hud", "credits-qmclient"})
	{
		SCOPED_TRACE(pTab);
		qm_card_order::CModel Model;
		Model.SetEntries(qm_card_registry::BuildDefaultEntries());
		Model.MoveToTab(DISPLAY_CARD, pTab, 0, 0);
		char aBefore[32768], aAfter[32768];
		ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));
		EXPECT_FALSE(qm_card_registry::RepairLegacyNameplateTitleLayout(Model));
		ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
		EXPECT_STREQ(aBefore, aAfter);
	}
}

TEST(QmTitleCards, CustomizedSponsorLayoutIsPreserved)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-community|credits-qmclient|left|0;"
		"deck:qmclient-contributors-title|credits-qmclient|full|0;"
		"deck:qmclient-contributors-sponsors|credits-qmclient|right|0;",
		qm_card_registry::BuildDefaultEntries()));
	char aBefore[32768], aAfter[32768];
	ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));
	EXPECT_FALSE(qm_card_registry::RepairLegacyTitleLayout(Model));
	ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
	EXPECT_STREQ(aBefore, aAfter);
	EXPECT_GE(Model.FindByStableId(DISPLAY_CARD), 0);
}

TEST(QmTitleCards, CollapseStateIsIndependentAfterReload)
{
	const auto &vIds = qm_card_catalog::TitleCardStableIds();
	ASSERT_EQ(vIds.size(), 2u);
	qm_card_collapse::CState State;
	ASSERT_TRUE(State.Load(vIds[0]));
	EXPECT_FALSE(State.IsCollapsed(vIds[1], false));
	ASSERT_TRUE(State.SetCollapsed(vIds[1], true));
	ASSERT_TRUE(State.SetCollapsed(vIds[0], false));
	char aSerialized[512];
	ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_collapse::CState Reloaded;
	ASSERT_TRUE(Reloaded.Load(aSerialized));
	EXPECT_FALSE(Reloaded.IsCollapsed(SPONSOR_CARD, true));
	EXPECT_TRUE(Reloaded.IsCollapsed(DISPLAY_CARD, false));
}

TEST(QmTitleCards, FluidSearchFindsTheSponsorCard)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"Fluid", "流体", "液体"})
	{
		SCOPED_TRACE(pQuery);
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		EXPECT_TRUE(std::any_of(vResults.begin(), vResults.end(), [](const auto &Result) { return std::string(Result.m_pStableId) == SPONSOR_CARD; }));
	}
}
