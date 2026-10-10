#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{
	constexpr const char *SPECTATOR_CARD = "qm:spectator_mode";
}

TEST(QmSpectatorCards, VisualPageAndModuleRegistryExposeTheSameSpectatorCard)
{
	const auto &Ids = qm_card_catalog::VisualCardStableIds();
	EXPECT_TRUE(std::any_of(Ids.begin(), Ids.end(), [](const char *pId) { return std::string(pId) == SPECTATOR_CARD; }));
	ASSERT_TRUE(qm_card_catalog::HasCardModule(SPECTATOR_CARD));
	const auto *pDefault = qm_card_registry::FindByStableId(SPECTATOR_CARD);
	ASSERT_NE(pDefault, nullptr);
	EXPECT_STREQ(pDefault->m_pTitle, "Spectate mode");
	EXPECT_STREQ(pDefault->m_pDefaultTab, "visual");
	qm_module::EQmModuleId Id;
	ASSERT_TRUE(qm_module::QmModuleIdFromStableId(SPECTATOR_CARD, &Id));
	EXPECT_EQ(Id, qm_module::EQmModuleId::SpectatorMode);
	EXPECT_STREQ(qm_module::QmModuleStableId(Id), SPECTATOR_CARD);
}

TEST(QmSpectatorCards, FreeviewAndDemoSearchTermsNavigateToTheSpectatorSettings)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const char *pQuery : {"Spectate mode", "观战模式", "freeview", "demo", "Smooth free spectator camera", "Cinematic smoothness", "qm_cinematic_camera", "qm_cinematic_camera_smoothness"})
	{
		SCOPED_TRACE(pQuery);
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) { return std::string(Result.m_pStableId) == SPECTATOR_CARD; });
		ASSERT_NE(It, Results.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "visual");
		EXPECT_STREQ(It->m_Target.m_pStableId, SPECTATOR_CARD);
	}
	const auto Results = qm_card_registry::SearchCards("qm_cinematic_camera_smoothness", Model);
	EXPECT_FALSE(std::any_of(Results.begin(), Results.end(), [](const auto &Result) { return std::string(Result.m_pStableId) == "qm:camera_view"; }));
}

TEST(QmSpectatorCards, ExistingLayoutGainsSpectatorCardAndPersistsAfterReload)
{
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm:camera_view|visual|right|0;qm:weapon_animation|visual|right|1;", Defaults));
	EXPECT_GE(Model.StateIndexForStableId(SPECTATOR_CARD), 0);
	char aSerialized[32768];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSerialized, Defaults));
	EXPECT_GE(Reloaded.StateIndexForStableId(SPECTATOR_CARD), 0);
	EXPECT_GE(Reloaded.StateIndexForStableId("qm:camera_view"), 0);
	EXPECT_GE(Reloaded.StateIndexForStableId("qm:weapon_animation"), 0);
}
