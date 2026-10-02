#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(SettingsCardDeck, EveryRegisteredCardResolvesANonEmptyDescriptionKey)
{
	ASSERT_FALSE(qm_card_registry::Defaults().empty());
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId != nullptr ? Default.m_pStableId : "<null>");
		ASSERT_NE(Default.m_pStableId, nullptr);
		EXPECT_NE(Default.m_pStableId[0], '\0');
		const char *pDescription = qm_card_registry::ResolveDescriptionKey(Default);
		ASSERT_NE(pDescription, nullptr);
		EXPECT_NE(pDescription[0], '\0');
	}
	EXPECT_EQ(qm_card_registry::ResolveLocalizedDescription(static_cast<const char *>(nullptr)), nullptr);
}

TEST(SettingsCardDeck, EveryCardDeclaresADistinctDescriptionWithinItsPage)
{
	std::unordered_map<std::string, std::unordered_set<std::string>> DescriptionsByTab;
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId);
		ASSERT_NE(Default.m_pDescription, nullptr);
		ASSERT_NE(Default.m_pDescription[0], '\0');
		const std::string Tab = Default.m_pDefaultTab != nullptr ? Default.m_pDefaultTab : "";
		EXPECT_TRUE(DescriptionsByTab[Tab].insert(Default.m_pDescription).second);
	}
}

TEST(SettingsCardDeck, DragPlacementUsesVisualOrderWithoutRendering)
{
	std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{},
		std::vector<int>{4, 7},
		std::vector<int>{9},
	};
	ApplySettingsCardDeckDragPlacement(aColumns, 7, 2, 1);
	EXPECT_EQ(aColumns[1], (std::vector<int>{4}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{9, 7}));

	// 意图：layout 已按每列视觉顺序收集 geometry，热路径无需再排序或分配。
	const std::vector<SSettingsCardDeckItemGeometry> vItems{
		{9, 2, {500.0f, 100.0f, 300.0f, 120.0f}},
		{7, 2, {500.0f, 168.0f, 300.0f, 52.0f}},
		{8, 2, {500.0f, 236.0f, 300.0f, 120.0f}},
	};
	EXPECT_EQ(ResolveSettingsCardDeckDropOrder(200.0f, 2, vItems, 7), 1);
}

TEST(SettingsCardDeck, SingleColumnDragPreservesCanonicalColumnCapacity)
{
	std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{2},
		std::vector<int>{4, 7},
		std::vector<int>{9, 11},
	};
	ApplySettingsCardDeckSingleColumnDragPlacement(aColumns, 11, 1);
	EXPECT_EQ(aColumns[0], (std::vector<int>{2}));
	EXPECT_EQ(aColumns[1], (std::vector<int>{4, 9}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{11, 7}));

	std::array<std::vector<int>, 3> aUnevenColumns{
		std::vector<int>{},
		std::vector<int>{10, 11},
		std::vector<int>{20},
	};
	ApplySettingsCardDeckSingleColumnDragPlacement(aUnevenColumns, 11, 1);
	EXPECT_EQ(aUnevenColumns[1], (std::vector<int>{10, 20}));
	EXPECT_EQ(aUnevenColumns[2], (std::vector<int>{11}));
	std::vector<int> vVisualOrder;
	ForEachSettingsCardDeckVisualOrder(aUnevenColumns, [&](int StateIndex, int Column) {
		if(Column != 0)
			vVisualOrder.push_back(StateIndex);
	});
	EXPECT_EQ(vVisualOrder, (std::vector<int>{10, 11, 20}));
}

TEST(SettingsCardDeck, SingleColumnVisualOrderPreservesWideReadingLayers)
{
	const std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{30},
		std::vector<int>{10, 11},
		std::vector<int>{20},
	};
	std::vector<std::pair<int, int>> vVisualOrder;
	ForEachSettingsCardDeckVisualOrder(aColumns, [&](int StateIndex, int Column) {
		vVisualOrder.emplace_back(StateIndex, Column);
	});
	EXPECT_EQ(vVisualOrder, (std::vector<std::pair<int, int>>{{10, 1}, {20, 2}, {30, 0}, {11, 1}}));
}

TEST(SettingsCardDeck, SingleColumnDropRestoresDeterministicWideLayout)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"full", "sound", 0, 0},
		{"left-a", "sound", 1, 0},
		{"left-b", "sound", 1, 1},
		{"right-a", "sound", 2, 0},
		{"right-b", "sound", 2, 1},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("full"),
		Model.StateIndexForStableId("left-a"),
		Model.StateIndexForStableId("left-b"),
		Model.StateIndexForStableId("right-a"),
		Model.StateIndexForStableId("right-b"),
	};

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "right-b", 1, vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 0), (std::vector<std::string>{"full"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), (std::vector<std::string>{"left-a", "right-a"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"right-b", "left-b"}));
}

TEST(SettingsCardDeck, SingleColumnDropPreservesHiddenCardsInCanonicalColumns)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"left-a", "sound", 1, 0},
		{"left-hidden", "sound", 1, 1},
		{"left-b", "sound", 1, 2},
		{"right-hidden", "sound", 2, 0},
		{"right-a", "sound", 2, 1},
		{"right-b", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("left-a"),
		Model.StateIndexForStableId("left-b"),
		Model.StateIndexForStableId("right-a"),
		Model.StateIndexForStableId("right-b"),
	};

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "right-b", 1, vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), (std::vector<std::string>{"left-a", "left-hidden", "right-a"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"right-hidden", "right-b", "left-b"}));

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "left-a", 3, vActiveStateIndices));
	const std::vector<std::string> vExpectedLeft{"right-b", "left-hidden", "left-b"};
	const std::vector<std::string> vExpectedRight{"right-hidden", "right-a", "left-a"};
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), vExpectedLeft);
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), vExpectedRight);

	char aSerialized[1024];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadExplicit(aSerialized, {
							       {"left-a", "sound", 1, 0},
							       {"left-hidden", "sound", 1, 1},
							       {"left-b", "sound", 1, 2},
							       {"right-hidden", "sound", 2, 0},
							       {"right-a", "sound", 2, 1},
							       {"right-b", "sound", 2, 2},
						       }));
	EXPECT_EQ(Reloaded.StableIdOrder("", "sound", 1), vExpectedLeft);
	EXPECT_EQ(Reloaded.StableIdOrder("", "sound", 2), vExpectedRight);
}

TEST(SettingsCardDeck, ColumnProjectionExcludesInactiveDefinitions)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("deck:graphics-visual"),
		Model.StateIndexForStableId("deck:graphics-modes"),
	};
	const auto aColumns = BuildSettingsCardDeckColumnOrder(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(aColumns[0], (std::vector<int>{}));
	EXPECT_EQ(aColumns[1], (std::vector<int>{Model.StateIndexForStableId("deck:graphics-visual")}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{Model.StateIndexForStableId("deck:graphics-modes")}));
}

TEST(SettingsCardDeck, ProductionPagePlacementsPreserveWideColumnsAndNarrowReadingOrder)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	const auto VerifyPage = [&Model](const char *pTab, const std::vector<const char *> &vStableIds, const std::array<std::vector<const char *>, 3> &aExpectedColumns, const std::vector<const char *> &vExpectedVisualOrder, int LeadingFullWidthCards = 0) {
		std::vector<int> vActiveStateIndices;
		vActiveStateIndices.reserve(vStableIds.size());
		for(const char *pStableId : vStableIds)
		{
			const int StateIndex = Model.StateIndexForStableId(pStableId);
			ASSERT_GE(StateIndex, 0) << pStableId;
			vActiveStateIndices.push_back(StateIndex);
		}

		const std::array<std::vector<int>, 3> aColumns = BuildSettingsCardDeckColumnOrder(Model, pTab, vActiveStateIndices);
		for(int Column = 0; Column < 3; ++Column)
		{
			ASSERT_EQ(aColumns[Column].size(), aExpectedColumns[Column].size());
			for(size_t Index = 0; Index < aColumns[Column].size(); ++Index)
				EXPECT_STREQ(Model.Entry(aColumns[Column][Index]).m_pStableId, aExpectedColumns[Column][Index]);
		}

		std::vector<const char *> vVisualOrder;
		ForEachSettingsCardDeckVisualOrder(aColumns, [&](const int StateIndex, int) {
			vVisualOrder.push_back(Model.Entry(StateIndex).m_pStableId);
		}, LeadingFullWidthCards);
		ASSERT_EQ(vVisualOrder.size(), vExpectedVisualOrder.size());
		for(size_t Index = 0; Index < vVisualOrder.size(); ++Index)
			EXPECT_STREQ(vVisualOrder[Index], vExpectedVisualOrder[Index]);
	};

	VerifyPage("tee",
		{"deck:tee-identity", "deck:tee-skin-options", "deck:tee-skin-list"},
		{{{"deck:tee-identity", "deck:tee-skin-list"}, {"deck:tee-skin-options"}, {}}},
		{"deck:tee-identity", "deck:tee-skin-list", "deck:tee-skin-options"}, 2);
	VerifyPage("appearance-chat",
		{"deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview"},
		{{{}, {"deck:appearance-chat-settings", "deck:appearance-chat-preview"}, {"deck:appearance-chat-messages"}}},
		{"deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview"});
	VerifyPage("tclient-status-bar",
		{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"},
		{{{}, {"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}, {}}},
		{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"});
}

TEST(SettingsCardDeck, ColumnProjectionCacheRebuildsOnlyForLayoutOrActiveDefinitionChanges)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const int Visual = Model.StateIndexForStableId("deck:graphics-visual");
	const int Modes = Model.StateIndexForStableId("deck:graphics-modes");
	const std::vector<int> vActiveStateIndices{Visual, Modes};
	settings_card_deck_logic::CProjectionCache Cache;

	const auto &aInitialColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);
	EXPECT_EQ(aInitialColumns[1], (std::vector<int>{Visual}));
	EXPECT_EQ(aInitialColumns[2], (std::vector<int>{Modes}));

	Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);

	const std::vector<int> vVisualOnly{Visual};
	const auto &aFilteredColumns = Cache.Resolve(Model, "graphics", vVisualOnly);
	EXPECT_EQ(Cache.RebuildCount(), 2u);
	EXPECT_EQ(aFilteredColumns[2], (std::vector<int>{}));

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "graphics", "deck:graphics-visual", 2, 0));
	const auto &aMovedColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 3u);
	EXPECT_EQ(aMovedColumns[2], (std::vector<int>{Visual, Modes}));
}
