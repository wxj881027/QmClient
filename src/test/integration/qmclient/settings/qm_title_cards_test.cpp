#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{
	constexpr const char *SPONSOR_CARD = "deck:qmclient-contributors-title";
	constexpr const char *DISPLAY_CARD = "deck:qmclient-contributors-title-display";
}

TEST(QmTitleCards, SinglePlayerTitleCardBelongsWithSponsors)
{
	const auto &vIds = qm_card_catalog::TitleCardStableIds();
	ASSERT_EQ(vIds.size(), 1u);
	EXPECT_STREQ(vIds.front(), SPONSOR_CARD);
	EXPECT_TRUE(qm_card_catalog::HasCardModule(SPONSOR_CARD));
	EXPECT_FALSE(qm_card_catalog::HasCardModule(DISPLAY_CARD));
	const auto *pCard = qm_card_registry::FindByStableId(SPONSOR_CARD);
	ASSERT_NE(pCard, nullptr);
	EXPECT_STREQ(pCard->m_pTitle, "Player titles");
	EXPECT_STREQ(pCard->m_pDefaultTab, "credits-qmclient");
	EXPECT_EQ(qm_card_registry::FindByStableId(DISPLAY_CARD), nullptr);
}

TEST(QmTitleCards, ProfileAndStyleSearchesFindTheSameInteractiveCard)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const char *apQueries[] = {
		"玩家头衔", "Player titles", "赞助头衔", "Sponsor title", "头衔样式", "头衔显示", "Title display",
		"Fluid", "流体", "液体", "qm_sponsor_chat_style", "qm_nameplate_title_scale", "头衔大小",
		"qm_show_nameplate_title", "qm_show_main_title", "qm_show_dummy_title", "qm_title_badge_style",
		"qm_title_badge_color", "qm_title_opacity", "Moon", "Sun", "月亮", "太阳", "星轨",
		"qm_title_text_style", "qm_title_text_wave", "文字1", "Text 3", "文字5", "逐字波动",
	};
	for(const char *pQuery : apQueries)
	{
		SCOPED_TRACE(pQuery);
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [](const auto &Result) { return std::string(Result.m_pStableId) == SPONSOR_CARD; });
		ASSERT_NE(It, vResults.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "credits-qmclient");
		EXPECT_STREQ(It->m_Target.m_pStableId, SPONSOR_CARD);
		EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
	}
}

TEST(QmTitleCards, SavedTwoCardLayoutKeepsTheCustomSponsorPlacementAfterReload)
{
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-title|hud|full|0;"
		"deck:qmclient-contributors-title-display|appearance-name-plate|right|1;",
		vDefaults));
	char aSerialized[32768];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, vDefaults));
	const int Index = Reloaded.FindByStableId(SPONSOR_CARD);
	ASSERT_GE(Index, 0);
	EXPECT_STREQ(Reloaded.Entry(Index).m_pDefaultTab, "hud");
	EXPECT_EQ(Reloaded.Entry(Index).m_Column, 0);
	EXPECT_LT(Reloaded.FindByStableId(DISPLAY_CARD), 0);
}
