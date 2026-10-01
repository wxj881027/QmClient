#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	qm_card_order::CModel LegacyTeeModel()
	{
		qm_card_order::CModel Model;
		Model.LoadMerged(
			"deck:tee-identity|tee|left|0;"
			"deck:tee-skin-options|tee|right|0;"
			"deck:tee-skin-list|tee|full|0;"
			"deck:tee-skin-queue|tee|left|1;"
			"deck:tee-glow|tee|right|1;"
			"qm:skin_appearance|visual|left|0;",
			qm_card_registry::BuildDefaultEntries());
		return Model;
	}
}

TEST(QmTeeCards, DefaultReadingOrderStartsWithSharedEditorAndLibrary)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	std::vector<int> vActive;
	for(const char *pId : qm_card_catalog::TeeCardStableIds())
	{
		EXPECT_TRUE(qm_card_catalog::HasCardModule(pId)) << pId;
		const int Index = Model.StateIndexForStableId(pId);
		ASSERT_GE(Index, 0) << pId;
		vActive.push_back(Index);
	}
	ASSERT_EQ(vActive.size(), 6u);
	const auto Columns = BuildSettingsCardDeckColumnOrder(Model, "tee", vActive);
	std::vector<std::string> vOrder;
	ForEachSettingsCardDeckVisualOrder(Columns, [&](int Index, int) { vOrder.emplace_back(Model.Entry(Index).m_pStableId); }, 2);
	EXPECT_EQ(vOrder, (std::vector<std::string>{"deck:tee-identity", "deck:tee-skin-list", "deck:tee-skin-options", "deck:tee-skin-queue", "qm:skin_appearance", "deck:tee-glow"}));
	const auto &Visual = qm_card_catalog::VisualCardStableIds();
	EXPECT_TRUE(std::none_of(Visual.begin(), Visual.end(), [](const char *pId) { return std::string(pId) == "qm:skin_appearance"; }));
}

TEST(QmTeeCards, SearchReturnsTheCardThatOwnsTheEditedSetting)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const auto &[pQuery, pExpectedId] : {std::pair{"colors", "deck:tee-identity"}, std::pair{"eyes", "deck:tee-identity"}, std::pair{"recent", "deck:tee-skin-list"}, std::pair{"prefix", "deck:tee-skin-options"}, std::pair{"outline", "qm:skin_appearance"}})
	{
		SCOPED_TRACE(pQuery);
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [&](const auto &Result) { return std::string(Result.m_pStableId) == pExpectedId; });
		ASSERT_NE(It, Results.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "tee");
	}
}

TEST(QmTeeCards, PreviousDefaultLayoutMigratesAndSurvivesReload)
{
	auto Model = LegacyTeeModel();
	ASSERT_TRUE(qm_card_registry::RepairLegacyTeeLayout(Model));
	EXPECT_EQ(Model.StableIdOrder("", "tee", 0), (std::vector<std::string>{"deck:tee-identity", "deck:tee-skin-list"}));
	EXPECT_EQ(Model.StableIdOrder("", "tee", 1), (std::vector<std::string>{"deck:tee-skin-options", "qm:skin_appearance"}));
	EXPECT_EQ(Model.StableIdOrder("", "tee", 2), (std::vector<std::string>{"deck:tee-skin-queue", "deck:tee-glow"}));
	EXPECT_FALSE(qm_card_registry::RepairLegacyTeeLayout(Model));
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, qm_card_registry::BuildDefaultEntries()));
	EXPECT_FALSE(qm_card_registry::RepairLegacyTeeLayout(Reloaded));
	for(int Column = 0; Column < 3; ++Column)
		EXPECT_EQ(Reloaded.StableIdOrder("", "tee", Column), Model.StableIdOrder("", "tee", Column));
}

TEST(QmTeeCards, CustomCardPlacementSurvivesTheAppearanceCardMove)
{
	auto Model = LegacyTeeModel();
	Model.Move("deck:tee-identity", 2, 1);
	const auto FullBefore = Model.StableIdOrder("deck:", "tee", 0);
	const auto LeftBefore = Model.StableIdOrder("deck:", "tee", 1);
	const auto RightBefore = Model.StableIdOrder("deck:", "tee", 2);
	ASSERT_TRUE(qm_card_registry::RepairLegacyTeeLayout(Model));
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 0), FullBefore);
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 1), LeftBefore);
	EXPECT_EQ(Model.StableIdOrder("deck:", "tee", 2), RightBefore);
	const int Appearance = Model.FindByStableId("qm:skin_appearance");
	ASSERT_GE(Appearance, 0);
	EXPECT_STREQ(Model.Entry(Appearance).m_pDefaultTab, "tee");
	EXPECT_FALSE(qm_card_registry::RepairLegacyTeeLayout(Model));
}

TEST(QmTeeCards, ExistingCustomizationWithinTeeIsNotMovedAgain)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	Model.Move("qm:skin_appearance", 0, 0);
	const auto FullBefore = Model.StableIdOrder("", "tee", 0);
	EXPECT_FALSE(qm_card_registry::RepairLegacyTeeLayout(Model));
	EXPECT_EQ(Model.StableIdOrder("", "tee", 0), FullBefore);
}
