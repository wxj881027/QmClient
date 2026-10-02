#include <engine/shared/config.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

TEST(QmNameplateCards, FourSettingsCardsHaveIndependentCatalogEntries)
{
	const auto &vIds = qm_card_catalog::NameplateCardStableIds();
	ASSERT_EQ(vIds.size(), 4u);
	const std::set<std::string> Expected = {
		"deck:appearance-name-plate-settings",
		"deck:appearance-name-plate-text",
		"deck:appearance-name-plate-hook-strength",
		"deck:appearance-name-plate-key-presses",
	};
	EXPECT_EQ((std::set<std::string>(vIds.begin(), vIds.end())), Expected);
	for(const char *pId : vIds)
	{
		SCOPED_TRACE(pId);
		const auto *pDefault = qm_card_registry::FindByStableId(pId);
		ASSERT_NE(pDefault, nullptr);
		EXPECT_STREQ(pDefault->m_pDefaultTab, "appearance-name-plate");
		EXPECT_TRUE(qm_card_catalog::HasCardModule(pId));
	}
	const auto *pPreview = qm_card_registry::FindByStableId("deck:appearance-name-plate-preview");
	ASSERT_NE(pPreview, nullptr);
	EXPECT_STREQ(pPreview->m_pDefaultTab, "appearance-name-plate");
	EXPECT_EQ(pPreview->m_DefaultColumn, qm_card_registry::ECardColumn::Right);
	EXPECT_EQ(pPreview->m_DefaultOrder, 0);
}

TEST(QmNameplateCards, SearchTargetsTheCardContainingTheSetting)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const struct
	{
		const char *m_pQuery;
		const char *m_pStableId;
	} aCases[] = {
		{"昵称", "deck:appearance-name-plate-settings"},
		{"辉光", "deck:appearance-name-plate-text"},
		{"强钩", "deck:appearance-name-plate-hook-strength"},
		{"按键显示", "deck:appearance-name-plate-key-presses"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto vResults = qm_card_registry::SearchCards(Case.m_pQuery, Model);
		const auto It = std::find_if(vResults.begin(), vResults.end(), [&](const auto &Result) {
			return std::string(Result.m_pStableId) == Case.m_pStableId;
		});
		ASSERT_NE(It, vResults.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "appearance-name-plate");
		EXPECT_STREQ(It->m_Target.m_pStableId, Case.m_pStableId);
	}
}

TEST(QmNameplateCards, LoadingOldLayoutKeepsPlacementAndAddsSplitCards)
{
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:appearance-name-plate-settings|appearance-name-plate|right|0;"
		"deck:appearance-name-plate-preview|appearance-name-plate|left|0;",
		vDefaults));
	char aSerialized[32768];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, vDefaults));
	const int General = Reloaded.FindByStableId("deck:appearance-name-plate-settings");
	const int Preview = Reloaded.FindByStableId("deck:appearance-name-plate-preview");
	ASSERT_GE(General, 0);
	ASSERT_GE(Preview, 0);
	EXPECT_EQ(Reloaded.Entry(General).m_Column, 2);
	EXPECT_EQ(Reloaded.Entry(Preview).m_Column, 1);
	EXPECT_EQ(Reloaded.Entry(General).m_OrderInColumn, 0);
	EXPECT_EQ(Reloaded.Entry(Preview).m_OrderInColumn, 0);
	for(const char *pId : qm_card_catalog::NameplateCardStableIds())
	{
		const int Index = Reloaded.FindByStableId(pId);
		ASSERT_GE(Index, 0) << pId;
		EXPECT_STREQ(Reloaded.Entry(Index).m_pDefaultTab, "appearance-name-plate");
	}
}

TEST(QmNameplateCards, CollapseStateRemainsIndependentAfterReload)
{
	qm_card_collapse::CState State;
	ASSERT_TRUE(State.Load("deck:appearance-name-plate-settings"));
	EXPECT_FALSE(State.IsCollapsed("deck:appearance-name-plate-text", false));
	EXPECT_FALSE(State.IsCollapsed("deck:appearance-name-plate-hook-strength", false));
	EXPECT_FALSE(State.IsCollapsed("deck:appearance-name-plate-key-presses", false));
	ASSERT_TRUE(State.SetCollapsed("deck:appearance-name-plate-text", true));
	ASSERT_TRUE(State.SetCollapsed("deck:appearance-name-plate-settings", false));
	char aSerialized[512];
	ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_collapse::CState Reloaded;
	ASSERT_TRUE(Reloaded.Load(aSerialized));
	EXPECT_FALSE(Reloaded.IsCollapsed("deck:appearance-name-plate-settings", true));
	EXPECT_TRUE(Reloaded.IsCollapsed("deck:appearance-name-plate-text", false));
	EXPECT_FALSE(Reloaded.IsCollapsed("deck:appearance-name-plate-hook-strength", false));
	EXPECT_FALSE(Reloaded.IsCollapsed("deck:appearance-name-plate-key-presses", false));
}

TEST(QmNameplateCards, ConditionalRowsInvalidatePageAndSearchMeasurements)
{
	int *apOptions[] = {
		&g_Config.m_ClNamePlatesClan,
		&g_Config.m_ClNamePlatesIds,
		&g_Config.m_ClNamePlatesIdsSeparateLine,
		&g_Config.m_ClNamePlatesStrong,
		&g_Config.m_ClShowDirection,
		&g_Config.m_QmNameplateEffectAutoLod,
		&g_Config.m_QmNameplateAdvanced,
	};
	for(size_t Index = 0; Index < std::size(apOptions); ++Index)
	{
		SCOPED_TRACE(Index);
		int &Option = *apOptions[Index];
		const int Original = Option;
		Option = 0;
		const uint64_t PageRevision = qm_card_catalog::NameplateMeasureContentRevision();
		const uint64_t SearchRevision = qm_card_catalog::MeasureContentRevision();
		Option = 1;
		EXPECT_NE(qm_card_catalog::NameplateMeasureContentRevision(), PageRevision);
		EXPECT_NE(qm_card_catalog::MeasureContentRevision(), SearchRevision);
		Option = Original;
	}
}
