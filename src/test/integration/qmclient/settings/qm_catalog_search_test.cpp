#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(QmCatalogSearch, InternalLabelsAliasesAndConfigsResolveWithoutOpeningTheirPages)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pCard;
	};
	const SCase aCases[] = {
		{"inp_mousesens", "deck:controls-mouse"},
		{"Maximum cursor distance", "deck:controls-mouse"},
		{"inp_controller_tolerance", "deck:controls-controller"},
		{"player_name", "deck:player-identity"},
		{"gfx_fsaa_samples", "deck:graphics-display"},
		{"gfx_screen_refresh_rate", "deck:graphics-modes"},
		{"snd_volume", "deck:sound-volume"},
		{"Sound volume", "deck:sound-volume"},
		{"cl_predict_events", "deck:ddnet-gameplay"},
		{"Axiom dummy password", "qm:gores"},
		{"qm_ime_auto_manage", "qm:ime"},
		{"cl_replay_length", "deck:ddnet-demo"},
		{"预测余量", "deck:ddnet-gameplay"},
		{"cl_background_entities", "deck:ddnet-background"},
		{"cl_chat_size", "deck:appearance-chat-settings"},
		{"qm_custom_font_cjk", "tclient:font"},
		{"CJK font weight:", "tclient:font"},
		{"中日韩字体字重", "tclient:font"},
		{"qm_statusbar_height", "deck:tclient-status-bar-settings"},
		{"qm_cycle_tee_hue_dummy", "deck:tee7-editor"},
		{"地图点状调试路径", "qm:player_stats"},
		{"qm_player_stats_map_progress_dbg_route", "qm:player_stats"},
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

TEST(QmCatalogSearch, AliasesAndCardLabelsProduceOneEditableResult)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("激光", Model);
	EXPECT_EQ(std::count_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "deck:appearance-laser-enhanced";
	}),
		1);
	EXPECT_TRUE(std::none_of(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:laser";
	}));
}

TEST(QmCatalogSearch, AliasNavigationUsesTheSavedLocationOfItsEditableCard)
{
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	Model.SetEntries(Defaults);
	Model.MoveToTab("deck:appearance-laser-enhanced", "function", 2, 0);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	const auto *pAlias = qm_card_registry::FindByStableId("qm:laser");
	ASSERT_NE(pAlias, nullptr);
	const auto Target = qm_card_registry::ResolveCardNavigationTarget(*pAlias, Reloaded);
	EXPECT_STREQ(Target.m_pTab, "function");
	EXPECT_STREQ(Target.m_pStableId, "deck:appearance-laser-enhanced");
	const auto Results = qm_card_registry::SearchCards("激光", Reloaded);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "deck:appearance-laser-enhanced";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "function");
}

TEST(QmCatalogSearch, LyricsUseTheirOwnControlsAndNavigationTarget)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("歌词", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
	EXPECT_STREQ(It->m_Target.m_pStableId, "qm:lyrics");
}
