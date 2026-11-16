#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogSearch.h>
#include <game/client/components/qmclient/search_answers.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(QmSearchAnswerCards, EveryAnswerLinksToExistingEditableCards)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const auto &Answer : qm_search_answers::Answers())
	{
		SCOPED_TRACE(Answer.m_pId);
		std::vector<qm_card_registry::SCardSearchResult> vCards;
		qm_card_catalog::AddAnswerCards(qm_search_answers::Search(Answer.m_pQuestion), Model, vCards);
		ASSERT_EQ(vCards.size(), Answer.m_vRelatedCards.size());
		for(size_t Index = 0; Index < vCards.size(); ++Index)
		{
			EXPECT_TRUE(qm_card_catalog::HasCardModule(vCards[Index].m_pStableId));
			EXPECT_STREQ(vCards[Index].m_pStableId, Answer.m_vRelatedCards[Index]);
		}
	}
}

TEST(QmSearchAnswerCards, AnswerCardsPrecedeOtherResultsWithoutDuplicates)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	auto vCards = qm_card_registry::SearchCards("hitbox", Model);
	const auto Answer = qm_search_answers::Search("我身上的十字架怎么关");
	qm_card_catalog::AddAnswerCards(Answer, Model, vCards);
	qm_card_catalog::AddAnswerCards(Answer, Model, vCards);
	ASSERT_FALSE(vCards.empty());
	EXPECT_STREQ(vCards.front().m_pStableId, "qm:collision_hitbox");
	EXPECT_EQ(std::count_if(vCards.begin(), vCards.end(), [](const auto &Card) { return std::string(Card.m_pStableId) == "qm:collision_hitbox"; }), 1);
}

TEST(QmSearchAnswerCards, NavigationUsesTheSavedCardLocation)
{
	qm_card_order::CModel Model;
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	Model.SetEntries(Defaults);
	Model.MoveToTab("qm:collision_hitbox", "hud", 2, 0);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	std::vector<qm_card_registry::SCardSearchResult> vCards;
	qm_card_catalog::AddAnswerCards(qm_search_answers::Search("十字架"), Reloaded, vCards);
	ASSERT_EQ(vCards.size(), 1u);
	EXPECT_STREQ(vCards.front().m_Target.m_pTab, "hud");
	EXPECT_STREQ(vCards.front().m_Target.m_pStableId, "qm:collision_hitbox");
}

TEST(QmSearchAnswerCards, AmbiguousOrMissingAnswerKeepsOrdinarySearchResults)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuestion : {"没声音", "", "明天天气怎么样"})
	{
		SCOPED_TRACE(pQuestion);
		auto vCards = qm_card_registry::SearchCards("snd_volume", Model);
		ASSERT_FALSE(vCards.empty());
		const auto Count = vCards.size();
		const std::string FirstId = vCards.front().m_pStableId;
		qm_card_catalog::AddAnswerCards(qm_search_answers::Search(pQuestion), Model, vCards);
		EXPECT_EQ(vCards.size(), Count);
		EXPECT_EQ(vCards.front().m_pStableId, FirstId);
	}
}
