#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(QmProfileCard, LegacySplitLayoutLoadsOneFullWidthProfileCard)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("deck:tclient-profiles-actions|tclient-profiles|left|0;deck:tclient-profiles-options|tclient-profiles|right|0;deck:tclient-profiles-list|tclient-profiles|left|1;", qm_card_registry::BuildDefaultEntries());
	EXPECT_EQ(Model.StableIdOrder("deck:", "tclient-profiles", 0), (std::vector<std::string>{"deck:tclient-profiles"}));
	EXPECT_TRUE(Model.StableIdOrder("deck:", "tclient-profiles", 1).empty());
	EXPECT_TRUE(Model.StableIdOrder("deck:", "tclient-profiles", 2).empty());
	EXPECT_LT(Model.FindByStableId("deck:tclient-profiles-actions"), 0);
	EXPECT_LT(Model.FindByStableId("deck:tclient-profiles-options"), 0);
	EXPECT_LT(Model.FindByStableId("deck:tclient-profiles-list"), 0);
}

TEST(QmProfileCard, IdentityPhraseAndAssetSearchResolveTheUnifiedCard)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"身份", "语录", "贴图", "轮盘"})
	{
		SCOPED_TRACE(pQuery);
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto Result = std::find_if(Results.begin(), Results.end(), [](const auto &Item) { return std::string(Item.m_pStableId) == "deck:tclient-profiles"; });
		ASSERT_NE(Result, Results.end());
		EXPECT_STREQ(Result->m_Target.m_pStableId, "deck:tclient-profiles");
		EXPECT_STREQ(Result->m_Target.m_pTab, "tclient-profiles");
	}
}
