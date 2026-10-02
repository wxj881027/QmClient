#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

TEST(QmCreditsLayout, PartialLegacyLayoutRestoresQmCardsToVisibleColumns)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-community|qmclient-contributors|left|0;"
		"deck:qmclient-contributors-title|qmclient-contributors|left|1;"
		"deck:qmclient-contributors-sponsors|qmclient-contributors-ddnet|right|0;"
		"deck:tclient-profiles-actions|tclient-profiles|bad|0;",
		qm_card_registry::BuildDefaultEntries()));
	const std::vector<int> vActive{
		Model.FindByStableId("deck:qmclient-contributors-community"),
		Model.FindByStableId("deck:qmclient-contributors-title"),
		Model.FindByStableId("deck:qmclient-contributors-sponsors"),
	};
	for(const int Index : vActive)
		ASSERT_GE(Index, 0);
	const auto Before = BuildSettingsCardDeckColumnOrder(Model, "credits-qmclient", vActive);
	EXPECT_TRUE(Before[1].empty());
	EXPECT_TRUE(Before[2].empty());

	ASSERT_TRUE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	const auto After = BuildSettingsCardDeckColumnOrder(Model, "credits-qmclient", vActive);
	EXPECT_EQ(After[1], (std::vector<int>{vActive[1], vActive[0]}));
	EXPECT_EQ(After[2], (std::vector<int>{vActive[2]}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "credits-links", 1), (std::vector<std::string>{"deck:credits-friend-links", "deck:tclient-info-developers"}));
}

TEST(QmCreditsLayout, LegacyUpstreamCardsBecomeVisibleOnOtherTab)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-ddnet|qmclient-contributors-ddnet|full|0;"
		"deck:tclient-info-developers|tclient-info|right|0;",
		qm_card_registry::BuildDefaultEntries()));
	const std::vector<int> vActive{
		Model.FindByStableId("deck:qmclient-contributors-ddnet"),
		Model.FindByStableId("deck:tclient-info-developers"),
	};
	for(const int Index : vActive)
		ASSERT_GE(Index, 0);

	ASSERT_TRUE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	const auto Columns = BuildSettingsCardDeckColumnOrder(Model, "credits-links", vActive);
	EXPECT_EQ(Columns[2], (std::vector<int>{vActive[0]}));
	EXPECT_EQ(Columns[1], (std::vector<int>{vActive[1]}));
	for(const int Index : vActive)
	{
		const auto *pDefault = qm_card_registry::FindByStableId(Model.Entry(Index).m_pStableId);
		ASSERT_NE(pDefault, nullptr);
		EXPECT_STREQ(qm_card_registry::ResolveCardNavigationTarget(*pDefault, Model).m_pTab, "credits-links");
	}
}

TEST(QmCreditsLayout, UnifiedLegacyPageSplitsIntoCurrentTabs)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-community|qmclient-contributors|right|2;"
		"deck:qmclient-contributors-title|qmclient-contributors|left|3;"
		"deck:qmclient-contributors-sponsors|qmclient-contributors|right|1;"
		"deck:qmclient-contributors-ddnet|qmclient-contributors|left|0;"
		"deck:tclient-info-developers|qmclient-contributors|right|0;"
		"deck:tclient-info-files|qmclient-contributors|left|2;",
		qm_card_registry::BuildDefaultEntries()));

	ASSERT_TRUE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	EXPECT_EQ(Model.StableIdOrder("deck:", "credits-qmclient", 1), (std::vector<std::string>{"deck:qmclient-contributors-title", "deck:qmclient-contributors-community"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "credits-qmclient", 2), (std::vector<std::string>{"deck:qmclient-contributors-title-display", "deck:qmclient-contributors-sponsors"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "credits-links", 2), (std::vector<std::string>{"deck:qmclient-contributors-ddnet"}));
	EXPECT_EQ(Model.StableIdOrder("deck:", "credits-links", 1), (std::vector<std::string>{"deck:credits-friend-links", "deck:tclient-info-developers"}));
	const int FilesIndex = Model.FindByStableId("deck:tclient-info-files");
	ASSERT_GE(FilesIndex, 0);
	EXPECT_STREQ(Model.Entry(FilesIndex).m_pDefaultTab, "general");
}

TEST(QmCreditsLayout, CurrentCustomPlacementsAreNotReset)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:qmclient-contributors-community|credits-qmclient|right|0;"
		"deck:qmclient-contributors-title|credits-qmclient|right|1;"
		"deck:qmclient-contributors-sponsors|credits-qmclient|left|0;"
		"deck:qmclient-contributors-ddnet|credits-links|right|0;"
		"deck:tclient-info-developers|credits-links|left|0;"
		"deck:tclient-info-files|general|left|0;"
		"deck:general-game|hud|left|0;",
		qm_card_registry::BuildDefaultEntries()));
	char aBefore[sizeof(g_Config.m_QmGlobalCardOrder)];
	ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));
	const uint64_t Revision = Model.LayoutRevision();

	EXPECT_FALSE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	EXPECT_FALSE(Model.IsDirty());
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	char aAfter[sizeof(g_Config.m_QmGlobalCardOrder)];
	ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
	EXPECT_STREQ(aAfter, aBefore);
}

TEST(QmCreditsLayout, RepairedLayoutSurvivesReloadWithoutRepeatedMigration)
{
	qm_card_order::CModel Model;
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	ASSERT_TRUE(Model.LoadMerged("deck:qmclient-contributors-title|qmclient-contributors|left|0;", Defaults));
	ASSERT_TRUE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	EXPECT_TRUE(Model.IsDirty());
	char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));

	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, Defaults));
	EXPECT_FALSE(qm_card_registry::RepairLegacyCreditsTabs(Reloaded));
	EXPECT_FALSE(Reloaded.IsDirty());
	EXPECT_EQ(Reloaded.StableIdOrder("deck:", "credits-qmclient", 1), (std::vector<std::string>{"deck:qmclient-contributors-title", "deck:qmclient-contributors-community"}));
}

TEST(QmCreditsLayout, CompletedMigrationVersionSurvivesConfigReload)
{
	struct SConfigRestore
	{
		std::unique_ptr<CConfig> m_pConfig = std::make_unique<CConfig>(g_Config);
		~SConfigRestore() { g_Config = *m_pConfig; }
	} ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	for(int Session = 0; Session < 2; ++Session)
	{
		std::unique_ptr<IKernel> pKernel(IKernel::Create());
		pKernel->RegisterInterface(pStorage.get(), false);
		IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT).release();
		pKernel->RegisterInterface(pConsole);
		IConfigManager *pConfigManager = CreateConfigManager();
		pKernel->RegisterInterface(pConfigManager);
		pConsole->Init();
		pConfigManager->Init();
		if(Session == 0)
		{
			g_Config.m_ClSaveSettings = 1;
			g_Config.m_QmCardLayoutVersion = 13;
			ASSERT_TRUE(pConfigManager->Save());
		}
		else
		{
			ASSERT_TRUE(pConsole->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE));
			EXPECT_EQ(g_Config.m_QmCardLayoutVersion, 13);
		}
	}
}

TEST(QmCreditsLayout, PreviousOtherTabMigratesToVisibleLinksAndKeepsCurrentCustomPlacements)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged("deck:qmclient-contributors-ddnet|credits-other|left|0;deck:tclient-info-developers|credits-other|right|0;deck:credits-friend-links|credits-links|right|1;", qm_card_registry::BuildDefaultEntries()));
	ASSERT_TRUE(qm_card_registry::RepairLegacyCreditsTabs(Model));
	EXPECT_STREQ(Model.Entry(Model.FindByStableId("deck:qmclient-contributors-ddnet")).m_pDefaultTab, "credits-links");
	EXPECT_STREQ(Model.Entry(Model.FindByStableId("deck:tclient-info-developers")).m_pDefaultTab, "credits-links");
	EXPECT_EQ(Model.Entry(Model.FindByStableId("deck:credits-friend-links")).m_Column, 2);
	EXPECT_FALSE(qm_card_registry::RepairLegacyCreditsTabs(Model));
}
