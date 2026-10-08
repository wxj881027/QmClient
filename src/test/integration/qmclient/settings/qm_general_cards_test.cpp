#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

TEST(QmGeneralCards, InternalSettingsCanFindTheirSharedCardBeforeOpeningThePage)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pCard;
	};
	const SCase aCases[] = {
		{"cl_dyncam", "deck:general-game"},
		{"Switch weapon when out of ammo", "deck:general-game"},
		{"动态镜头", "deck:general-game"},
		{"cl_languagefile", "deck:general-language"},
		{"cl_refresh_rate", "deck:general-client"},
		{"省电", "deck:general-client"},
		{"cl_auto_demo_max", "deck:general-recording"},
		{"Automatically take statboard screenshot", "deck:general-recording"},
		{"自动录像", "deck:general-recording"},
		{"Chat Binds", "deck:tclient-info-files"},
		{"配置文件", "deck:tclient-info-files"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Results = qm_card_registry::SearchCards(Case.m_pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [&](const auto &Result) {
			return std::string(Result.m_pStableId) == Case.m_pCard;
		});
		ASSERT_NE(It, Results.end());
		EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
		EXPECT_STREQ(It->m_Target.m_pStableId, Case.m_pCard);
	}
}

TEST(QmGeneralCards, SearchKeepsTheUsersSavedCardLocation)
{
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	Model.SetEntries(Defaults);
	Model.MoveToTab("deck:general-client", "function", 2, 0);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	const auto Results = qm_card_registry::SearchCards("cl_refresh_rate", Reloaded);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "deck:general-client";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "function");
	EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
}
