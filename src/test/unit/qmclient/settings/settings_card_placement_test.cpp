#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

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

TEST(SettingsCardPacking, HalfWidthCardsFillSpaceAboveFullWidthWithoutChangingSides)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	const auto Full = Plan.Append(0, 50.0f);
	const auto Left = Plan.Append(1, 80.0f);
	const auto Right = Plan.Append(2, 30.0f);

	EXPECT_FLOAT_EQ(Full.m_Y, 350.0f);
	EXPECT_FLOAT_EQ(Left.m_Y, 150.0f);
	EXPECT_LE(Left.m_NextY, Full.m_Y);
	EXPECT_FLOAT_EQ(Right.m_Y, Full.m_NextY);
}

TEST(SettingsCardPacking, WithoutFullWidthCardsBothColumnsKeepIndependentFlow)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	const auto FirstLeft = Plan.Append(1, 40.0f);
	const auto SecondLeft = Plan.Append(1, 80.0f);
	const auto FirstRight = Plan.Append(2, 240.0f);
	const auto SecondRight = Plan.Append(2, 30.0f);

	EXPECT_FLOAT_EQ(FirstLeft.m_Y, 100.0f);
	EXPECT_FLOAT_EQ(FirstRight.m_Y, 100.0f);
	EXPECT_FLOAT_EQ(SecondLeft.m_Y, FirstLeft.m_NextY);
	EXPECT_FLOAT_EQ(SecondRight.m_Y, FirstRight.m_NextY);
}

TEST(SettingsCardPacking, ExactFitKeepsGapBeforeFullWidthCard)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	const auto Full = Plan.Append(0, 50.0f);
	const auto Left = Plan.Append(1, 190.0f);

	EXPECT_FLOAT_EQ(Left.m_Y, 150.0f);
	EXPECT_FLOAT_EQ(Left.m_NextY, Full.m_Y);
	EXPECT_FLOAT_EQ(Full.m_Y - (Left.m_Y + Left.m_Height), 10.0f);
}

TEST(SettingsCardPacking, CardThatExceedsGapContinuesBelowFullWidth)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	const auto Full = Plan.Append(0, 50.0f);
	const auto Left = Plan.Append(1, 190.01f);

	EXPECT_FLOAT_EQ(Left.m_Y, Full.m_NextY);
	EXPECT_FLOAT_EQ(Left.m_Y - (Full.m_Y + Full.m_Height), 10.0f);
}

TEST(SettingsCardPacking, SmallerFollowingCardDoesNotOvertakeItsColumnPredecessor)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	const auto Full = Plan.Append(0, 50.0f);
	const auto Tall = Plan.Append(1, 200.0f);
	const auto Short = Plan.Append(1, 20.0f);

	EXPECT_FLOAT_EQ(Tall.m_Y, Full.m_NextY);
	EXPECT_FLOAT_EQ(Short.m_Y, Tall.m_NextY);
}

TEST(SettingsCardPacking, HalfWidthCardsCanFillSuccessiveFullWidthGaps)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(0.0f, 10.0f);
	Plan.Append(1, 10.0f);
	Plan.Append(2, 100.0f);
	const auto FirstFull = Plan.Append(0, 20.0f);
	Plan.Append(2, 100.0f);
	const auto SecondFull = Plan.Append(0, 20.0f);
	const auto FirstLeft = Plan.Append(1, 60.0f);
	const auto SecondLeft = Plan.Append(1, 60.0f);
	const auto ThirdLeft = Plan.Append(1, 100.0f);

	EXPECT_FLOAT_EQ(FirstLeft.m_Y, 20.0f);
	EXPECT_LE(FirstLeft.m_NextY, FirstFull.m_Y);
	EXPECT_FLOAT_EQ(SecondLeft.m_Y, FirstFull.m_NextY);
	EXPECT_LE(SecondLeft.m_NextY, SecondFull.m_Y);
	EXPECT_FLOAT_EQ(ThirdLeft.m_Y, SecondFull.m_NextY);
}

TEST(SettingsCardPacking, ConsecutiveFullWidthCardsRemainOrderedWhenColumnsAreShorter)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(0.0f, 10.0f);
	Plan.Append(1, 10.0f);
	Plan.Append(2, 100.0f);
	const auto FirstFull = Plan.Append(0, 20.0f);
	const auto SecondFull = Plan.Append(0, 30.0f);
	const auto Left = Plan.Append(1, 100.0f);

	EXPECT_FLOAT_EQ(SecondFull.m_Y, FirstFull.m_NextY);
	EXPECT_FLOAT_EQ(Left.m_Y, SecondFull.m_NextY);
}

TEST(SettingsCardPacking, LeadingFullWidthCardsKeepBothColumnsBelowThem)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	const auto FirstFull = Plan.Append(0, 30.0f, true);
	const auto SecondFull = Plan.Append(0, 20.0f, true);
	const auto Left = Plan.Append(1, 20.0f);
	const auto Right = Plan.Append(2, 50.0f);

	EXPECT_FLOAT_EQ(FirstFull.m_Y, 100.0f);
	EXPECT_FLOAT_EQ(SecondFull.m_Y, FirstFull.m_NextY);
	EXPECT_FLOAT_EQ(Left.m_Y, SecondFull.m_NextY);
	EXPECT_FLOAT_EQ(Right.m_Y, SecondFull.m_NextY);
}

TEST(SettingsCardPacking, RebuildAfterCollapseAndReopenUsesOnlyCurrentHeights)
{
	CSettingsCardDeckPackingPlan Plan;
	for(const float RightHeight : {240.0f, 10.0f, 240.0f})
	{
		SCOPED_TRACE(RightHeight);
		Plan.Reset(100.0f, 10.0f);
		Plan.Append(1, 40.0f);
		Plan.Append(2, RightHeight);
		const auto Full = Plan.Append(0, 50.0f);
		const auto Left = Plan.Append(1, 80.0f);

		EXPECT_FLOAT_EQ(Full.m_Y, RightHeight == 240.0f ? 350.0f : 150.0f);
		EXPECT_FLOAT_EQ(Left.m_Y, RightHeight == 240.0f ? 150.0f : 210.0f);
		EXPECT_TRUE(Left.m_NextY <= Full.m_Y || Left.m_Y >= Full.m_NextY);
	}
}

TEST(SettingsCardPacking, RebuildWithoutFullWidthCardRemovesItsObstacle)
{
	CSettingsCardDeckPackingPlan Plan;
	Plan.Reset(100.0f, 10.0f);
	Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	Plan.Append(0, 50.0f);

	Plan.Reset(100.0f, 10.0f);
	const auto FirstLeft = Plan.Append(1, 40.0f);
	Plan.Append(2, 240.0f);
	const auto SecondLeft = Plan.Append(1, 200.0f);
	EXPECT_FLOAT_EQ(SecondLeft.m_Y, FirstLeft.m_NextY);
}

TEST(SettingsCardPacking, ChangingAnimatedHeightsNeverIntersectsFullWidthCard)
{
	CSettingsCardDeckPackingPlan Plan;
	for(const float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		Plan.Reset(100.0f, 10.0f);
		const auto FirstLeft = Plan.Append(1, 40.0f);
		const auto FirstRight = Plan.Append(2, 240.0f - 180.0f * Progress);
		const auto Full = Plan.Append(0, 50.0f + 80.0f * Progress);
		const auto Left = Plan.Append(1, 80.0f + 100.0f * Progress);
		const auto Right = Plan.Append(2, 30.0f);

		EXPECT_GE(Left.m_Y, FirstLeft.m_NextY);
		EXPECT_GE(Right.m_Y, FirstRight.m_NextY);
		EXPECT_TRUE(Left.m_NextY <= Full.m_Y || Left.m_Y >= Full.m_NextY);
		EXPECT_TRUE(Right.m_NextY <= Full.m_Y || Right.m_Y >= Full.m_NextY);
	}
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
	// 两张仍在「显示选项」页的卡片：图标与动效已迁到「视觉效果」，这里改用显示与显示模式。
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("deck:graphics-display"),
		Model.StateIndexForStableId("deck:graphics-modes"),
	};
	const auto aColumns = BuildSettingsCardDeckColumnOrder(Model, "graphics", vActiveStateIndices);
	// 投影只保留活动卡片，并按各自 model 列分组；默认列由注册表瀑布分配，不在这里写死。
	int ProjectedCards = 0;
	for(int Column = 0; Column < 3; ++Column)
	{
		for(const int Index : aColumns[Column])
		{
			ASSERT_GE(Index, 0);
			EXPECT_EQ(Model.Entry(Index).m_Column, Column);
			EXPECT_NE(std::find(vActiveStateIndices.begin(), vActiveStateIndices.end(), Index), vActiveStateIndices.end());
			++ProjectedCards;
		}
	}
	EXPECT_EQ(ProjectedCards, static_cast<int>(vActiveStateIndices.size()));
	// 未激活的同页卡片不得占用任何列槽位。
	for(int Column = 0; Column < 3; ++Column)
	{
		for(const int Index : Model.ColumnIndices("graphics", Column))
		{
			if(std::find(vActiveStateIndices.begin(), vActiveStateIndices.end(), Index) != vActiveStateIndices.end())
				continue;
			EXPECT_EQ(std::count(aColumns[Column].begin(), aColumns[Column].end(), Index), 0);
		}
	}
}

// 意图：生产页面的「卡片清单 → deck 列投影 → 宽屏阅读序」必须覆盖每张活动卡一次：
// 加卡或调整默认列时，投影不能丢卡、重复放卡，也不能把半宽卡塞进全宽列。
TEST(SettingsCardDeck, ProductionPageProjectionsCoverEveryActiveCardOnce)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	// 页面清单来源有两类：栖梦分类页用各自的 *CardStableIds()，
	// 标准目录页用 CatalogPageStableIds()（两者都从注册表派生）。
	const std::vector<const char *> vCatalogGraphics = qm_card_catalog::CatalogPageStableIds("graphics");
	const std::vector<const char *> vCatalogSound = qm_card_catalog::CatalogPageStableIds("sound");
	const std::vector<const char *> vCatalogDdnet = qm_card_catalog::CatalogPageStableIds("ddnet");
	const std::vector<const char *> vCatalogPlayer = qm_card_catalog::CatalogPageStableIds("player");
	const std::vector<const char *> vCatalogChat = qm_card_catalog::CatalogPageStableIds("appearance-chat");
	const std::vector<const char *> vCatalogHud = qm_card_catalog::CatalogPageStableIds("appearance-hud");
	const std::vector<const char *> vCatalogNamePlate = qm_card_catalog::CatalogPageStableIds("appearance-name-plate");
	const std::vector<const char *> vCatalogLaser = qm_card_catalog::CatalogPageStableIds("appearance-laser");
	const std::vector<const char *> vCatalogTClient = qm_card_catalog::CatalogPageStableIds("tclient");
	struct SPageCards
	{
		const char *m_pTab;
		const std::vector<const char *> *m_pIds;
	};
	const SPageCards aPages[] = {
		{"visual", &qm_card_catalog::VisualCardStableIds()},
		{"function", &qm_card_catalog::FunctionCardStableIds()},
		{"hud", &qm_card_catalog::HudCardStableIds()},
		{"tee", &qm_card_catalog::TeeCardStableIds()},
		{"controls", &qm_card_catalog::ControlsCardStableIds()},
		{"general", &qm_card_catalog::GeneralCardStableIds()},
		{"graphics", &vCatalogGraphics},
		{"sound", &vCatalogSound},
		{"ddnet", &vCatalogDdnet},
		{"player", &vCatalogPlayer},
		{"appearance-chat", &vCatalogChat},
		{"appearance-hud", &vCatalogHud},
		{"appearance-name-plate", &vCatalogNamePlate},
		{"appearance-laser", &vCatalogLaser},
		{"tclient", &vCatalogTClient},
	};
	for(const SPageCards &Page : aPages)
	{
		SCOPED_TRACE(Page.m_pTab);
		ASSERT_NE(Page.m_pIds, nullptr);
		ASSERT_FALSE(Page.m_pIds->empty()) << Page.m_pTab;
		std::vector<int> vActiveStateIndices;
		for(const char *pStableId : *Page.m_pIds)
		{
			const int Index = Model.StateIndexForStableId(pStableId);
			ASSERT_GE(Index, 0) << pStableId;
			// 目录页可以把卡片"呈现"在别的页（例如字体卡），deck 投影只认模型 tab。
			const char *pModelTab = Model.Entry(Index).m_pDefaultTab;
			if(pModelTab == nullptr || std::string(pModelTab) != Page.m_pTab)
				continue;
			vActiveStateIndices.push_back(Index);
		}
		ASSERT_FALSE(vActiveStateIndices.empty()) << Page.m_pTab;

		const std::array<std::vector<int>, 3> aColumns = BuildSettingsCardDeckColumnOrder(Model, Page.m_pTab, vActiveStateIndices);
		std::vector<int> vVisualOrder;
		ForEachSettingsCardDeckVisualOrder(aColumns, [&](const int StateIndex, int) { vVisualOrder.push_back(StateIndex); });
		ASSERT_EQ(vVisualOrder.size(), vActiveStateIndices.size()) << Page.m_pTab;
		for(const int Index : vActiveStateIndices)
			EXPECT_EQ(std::count(vVisualOrder.begin(), vVisualOrder.end(), Index), 1) << Page.m_pTab;

		// 全宽卡只在 0 列，其余卡片只在 1/2 列。
		for(int Column = 0; Column < 3; ++Column)
		{
			for(const int Index : aColumns[Column])
				EXPECT_EQ(Model.Entry(Index).m_Column == 0, Column == 0) << Page.m_pTab;
		}
	}
}

TEST(SettingsCardDeck, ColumnProjectionCacheRebuildsOnlyForLayoutOrActiveDefinitionChanges)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const int Display = Model.StateIndexForStableId("deck:graphics-display");
	const int Modes = Model.StateIndexForStableId("deck:graphics-modes");
	const std::vector<int> vActiveStateIndices{Display, Modes};
	settings_card_deck_logic::CProjectionCache Cache;

	// 列号由默认布局（对称瀑布）决定，两张卡也可能落在同一列，所以这里只锁
	// 「每张卡在自己的默认列里且只出现一次」，不把具体列或同列/分列写死。
	const int DisplayColumn = Model.Entry(Display).m_Column;
	const int ModesColumn = Model.Entry(Modes).m_Column;
	const auto CountIn = [](const std::vector<int> &vRows, const int Id) {
		return (int)std::count(vRows.begin(), vRows.end(), Id);
	};
	const auto &aInitialColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);
	EXPECT_EQ(CountIn(aInitialColumns[DisplayColumn], Display), 1);
	EXPECT_EQ(CountIn(aInitialColumns[ModesColumn], Modes), 1);

	Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);

	const std::vector<int> vDisplayOnly{Display};
	const auto &aFilteredColumns = Cache.Resolve(Model, "graphics", vDisplayOnly);
	EXPECT_EQ(Cache.RebuildCount(), 2u);
	EXPECT_EQ(CountIn(aFilteredColumns[ModesColumn], Modes), 0);

	// 换到另一列的首位：该列按读序把这张卡排在最前，且这次落位不是空操作。
	const int OtherColumn = DisplayColumn == 1 ? 2 : 1;
	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "graphics", "deck:graphics-display", OtherColumn, 0));
	const auto &aMovedColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 3u);
	ASSERT_FALSE(aMovedColumns[OtherColumn].empty());
	EXPECT_EQ(aMovedColumns[OtherColumn].front(), Display);
}
