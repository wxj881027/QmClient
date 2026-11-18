#include <base/str.h>

#include <game/client/QmUi/QmCardRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	struct SSearchCase
	{
		const char *m_pConfigKey;
		const char *m_pLegacySearchTerm;
		const char *m_pCard;
	};

	constexpr SSearchCase s_aCases[] = {
		{"dummy_country", "cl_dummy_country", "deck:player-country"},
		{"dummy_clan", "cl_dummy_clan", "deck:player-identity"},
		{"dummy_name", "cl_dummy_name", "deck:player-identity"},
	};
}

TEST(QmCardSearchConfigKeys, ShowsRegisteredConfigNamesAndUsesThemAsMatchKeys)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const auto &Case : s_aCases)
	{
		SCOPED_TRACE(Case.m_pConfigKey);
		const auto vResults = qm_card_registry::SearchCards(Case.m_pConfigKey, Model);
		const auto Result = std::find_if(vResults.begin(), vResults.end(), [&Case](const auto &Candidate) {
			return str_comp(Candidate.m_pStableId, Case.m_pCard) == 0;
		});
		ASSERT_NE(Result, vResults.end());
		EXPECT_NE(std::find(Result->m_vMatches.begin(), Result->m_vMatches.end(), Case.m_pConfigKey), Result->m_vMatches.end());
		EXPECT_NE(std::find(Result->m_vMatchKeys.begin(), Result->m_vMatchKeys.end(), Case.m_pConfigKey), Result->m_vMatchKeys.end());
	}
}

TEST(QmCardSearchConfigKeys, FormerSearchTermsUseCurrentConfigNamesAsMatchKeys)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	for(const auto &Case : s_aCases)
	{
		SCOPED_TRACE(Case.m_pLegacySearchTerm);
		const auto vResults = qm_card_registry::SearchCards(Case.m_pLegacySearchTerm, Model);
		const auto Result = std::find_if(vResults.begin(), vResults.end(), [&Case](const auto &Candidate) {
			return str_comp(Candidate.m_pStableId, Case.m_pCard) == 0;
		});
		ASSERT_NE(Result, vResults.end());
		EXPECT_NE(std::find(Result->m_vMatches.begin(), Result->m_vMatches.end(), Case.m_pConfigKey), Result->m_vMatches.end());
		EXPECT_NE(std::find(Result->m_vMatchKeys.begin(), Result->m_vMatchKeys.end(), Case.m_pConfigKey), Result->m_vMatchKeys.end());
	}
}

TEST(QmCardSearchConfigKeys, RecordingCardOmitsExportEncodingOptions)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	constexpr const char *apQueries[] = {
		"Video quality CRF",
		"cl_video_crf",
		"Recorder FPS",
		"cl_video_recorder_fps",
		"Render preset",
		"cl_video_preset",
		"cl_video_x264_preset",
	};
	for(const char *pQuery : apQueries)
	{
		SCOPED_TRACE(pQuery);
		const auto vResults = qm_card_registry::SearchCards(pQuery, Model);
		EXPECT_TRUE(std::none_of(vResults.begin(), vResults.end(), [](const auto &Candidate) {
			return str_comp(Candidate.m_pStableId, "deck:general-recording") == 0;
		}));
	}
}
