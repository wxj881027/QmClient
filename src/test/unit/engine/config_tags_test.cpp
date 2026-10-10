#include <engine/shared/config_tags.h>

#include <gtest/gtest.h>

TEST(ConfigTags, MigratedNamesKeepPrefixSpecificCategories)
{
	const CConfigTagsManager Tags;
	const struct
	{
		const char *m_pName;
		EConfigTag m_Tag;
	} aCases[] = {
		{"qm_frozen_tees_text", EConfigTag::VISUAL},
		{"qm_custom_font", EConfigTag::VISUAL},
		{"qm_color_freeze", EConfigTag::VISUAL},
		{"qm_show_chat_client", EConfigTag::HUD},
		{"qm_show_local_time_seconds", EConfigTag::HUD},
		{"qm_prediction_margin_smooth", EConfigTag::GAMEPLAY},
		{"qm_hook_coll_cursor", EConfigTag::AUTOMATION},
	};
	for(const auto &Case : aCases)
		EXPECT_TRUE(Tags.HasTag(Case.m_pName, Case.m_Tag)) << Case.m_pName;
}

TEST(ConfigTags, MigratedNamesKeepMultipleCategoriesWithoutDuplicates)
{
	const CConfigTagsManager Tags;
	EXPECT_TRUE(Tags.HasTag("qm_antiping_improved", EConfigTag::VISUAL));
	EXPECT_TRUE(Tags.HasTag("qm_antiping_improved", EConfigTag::GAMEPLAY));
	EXPECT_EQ(Tags.GetTagsForVariable("qm_antiping_improved").size(), 2u);
	EXPECT_TRUE(Tags.HasTag("qm_show_chat_client", EConfigTag::HUD));
	EXPECT_TRUE(Tags.HasTag("qm_show_chat_client", EConfigTag::CHAT));
	EXPECT_EQ(Tags.GetTagsForVariable("qm_show_chat_client").size(), 2u);
}

TEST(ConfigTags, LegacyAliasesUseTheSameCategoriesAsCanonicalNames)
{
	const CConfigTagsManager Tags;
	const struct
	{
		const char *m_pLegacyName;
		const char *m_pCanonicalName;
	} aCases[] = {
		{"tc_frozen_tees_text", "qm_frozen_tees_text"},
		{"tc_show_chat_client", "qm_show_chat_client"},
		{"TC_ANTIPING_IMPROVED", "qm_antiping_improved"},
		{"tc_prediction_margin_smooth", "qm_prediction_margin_smooth"},
	};
	for(const auto &Case : aCases)
		EXPECT_EQ(Tags.GetTagsForVariable(Case.m_pLegacyName), Tags.GetTagsForVariable(Case.m_pCanonicalName)) << Case.m_pLegacyName;
}

TEST(ConfigTags, UpstreamAndExistingQmCategoriesRemainAvailable)
{
	const CConfigTagsManager Tags;
	EXPECT_TRUE(Tags.HasTag("cl_showhud", EConfigTag::HUD));
	EXPECT_TRUE(Tags.HasTag("cl_mouse_max_distance", EConfigTag::INPUT));
	EXPECT_TRUE(Tags.HasTag("qm_warlist", EConfigTag::VISUAL));
	EXPECT_TRUE(Tags.HasTag("qm_warlist", EConfigTag::SOCIAL));
	EXPECT_TRUE(Tags.GetTagsForVariable("qm_translate_tc_secret_id").empty());
	EXPECT_TRUE(Tags.GetTagsForVariable("tc_unknown_future_option").empty());
}
