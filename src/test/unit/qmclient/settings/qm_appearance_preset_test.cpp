#include <engine/shared/qm_default_profile.h>
#include <game/client/QmUi/cards/QmAppearancePreset.h>

#include <gtest/gtest.h>

#include <memory>

TEST(QmAppearancePreset, AppliesFixedAppearanceValuesWithoutChangingAccountOrGameplaySettings)
{
	auto pConfig = std::make_unique<CConfig>();
	str_copy(pConfig->m_PlayerName, "kept name");
	str_copy(pConfig->m_UiServerAddress, "localhost:8303");
	str_copy(pConfig->m_QmCustomFontIcons, "kept icons");
	pConfig->m_ClPredict = 1;
	pConfig->m_BrFilterLogin = 0;
	ASSERT_TRUE(QmAppearancePreset::Apply(*pConfig, "LXGW WenKai Regular"));
	EXPECT_STREQ(pConfig->m_QmCustomFont, "LXGW WenKai Regular");
	EXPECT_STREQ(pConfig->m_QmCustomFontCjk, "LXGW WenKai Regular");
	EXPECT_EQ(pConfig->m_QmCustomFontWeight, 400);
	EXPECT_EQ(pConfig->m_QmUiScale, 100);
	EXPECT_EQ(pConfig->m_UiColor, 0x4D000000u);
	EXPECT_EQ(pConfig->m_ClMenuPanelOpacity, 30);
	EXPECT_STREQ(pConfig->m_PlayerName, "kept name");
	EXPECT_STREQ(pConfig->m_UiServerAddress, "localhost:8303");
	EXPECT_STREQ(pConfig->m_QmCustomFontIcons, "kept icons");
	EXPECT_EQ(pConfig->m_ClPredict, 1);
	EXPECT_EQ(pConfig->m_BrFilterLogin, 0);
	ASSERT_TRUE(QmAppearancePreset::Apply(*pConfig, "LXGW WenKai Regular"));
	EXPECT_EQ(pConfig->m_QmUiScale, 100);
	EXPECT_STREQ(pConfig->m_PlayerName, "kept name");
}

TEST(QmAppearancePreset, MissingFontPreservesBothFacesAndWeightsWhileApplyingOtherAppearance)
{
	auto pConfig = std::make_unique<CConfig>();
	str_copy(pConfig->m_QmCustomFont, "existing Latin");
	str_copy(pConfig->m_QmCustomFontCjk, "existing CJK");
	pConfig->m_QmCustomFontWeight = 600;
	pConfig->m_QmCustomFontWeightCjk = 500;
	pConfig->m_QmUiScale = 150;
	EXPECT_FALSE(QmAppearancePreset::Apply(*pConfig, nullptr));
	EXPECT_STREQ(pConfig->m_QmCustomFont, "existing Latin");
	EXPECT_STREQ(pConfig->m_QmCustomFontCjk, "existing CJK");
	EXPECT_EQ(pConfig->m_QmCustomFontWeight, 600);
	EXPECT_EQ(pConfig->m_QmCustomFontWeightCjk, 500);
	EXPECT_EQ(pConfig->m_QmUiScale, 100);
	EXPECT_EQ(pConfig->m_QmGaussianBlur, 1);
}

TEST(QmDefaultProfile, FreshConfigKeepsDeclaredDefaultsAndDoesNotApplyRecommendedPreset)
{
	auto pConfig = std::make_unique<CConfig>();
	pConfig->m_UiColor = DefaultConfig::UiColor;
	pConfig->m_BrFilterLogin = DefaultConfig::BrFilterLogin;
	str_copy(pConfig->m_QmCustomFont, DefaultConfig::QmCustomFont);
	EXPECT_TRUE(QmInitializeDefaultProfile(*pConfig, false, [](const char *) { return false; }));
	EXPECT_EQ(pConfig->m_UiColor, 0xE4A046AFu);
	EXPECT_EQ(pConfig->m_BrFilterLogin, 1);
	EXPECT_STREQ(pConfig->m_QmCustomFont, "");
	EXPECT_EQ(pConfig->m_QmDefaultsProfileVersion, 1);
}

TEST(QmDefaultProfile, OldConfigKeepsOmittedPreviousDefaultsAndMigrationIsOneShot)
{
	auto pConfig = std::make_unique<CConfig>();
	pConfig->m_UiColor = DefaultConfig::UiColor;
	pConfig->m_BrFilterLogin = DefaultConfig::BrFilterLogin;
	EXPECT_TRUE(QmInitializeDefaultProfile(*pConfig, true, [](const char *) { return false; }));
	EXPECT_EQ(pConfig->m_UiColor, 0x4D000000u);
	EXPECT_EQ(pConfig->m_BrFilterLogin, 0);
	EXPECT_STREQ(pConfig->m_QmCustomFont, "Source Han Sans SC");
	pConfig->m_UiColor = 123;
	EXPECT_FALSE(QmInitializeDefaultProfile(*pConfig, true, [](const char *) { return false; }));
	EXPECT_EQ(pConfig->m_UiColor, 123u);
}

TEST(QmDefaultProfile, ExplicitOldUserValuesAreNeverReplacedByDefaultMigration)
{
	auto pConfig = std::make_unique<CConfig>();
	pConfig->m_UiColor = 123;
	pConfig->m_BrFilterLogin = 1;
	str_copy(pConfig->m_QmCustomFont, "chosen font");
	EXPECT_TRUE(QmInitializeDefaultProfile(*pConfig, true, [](const char *) { return true; }));
	EXPECT_EQ(pConfig->m_UiColor, 123u);
	EXPECT_EQ(pConfig->m_BrFilterLogin, 1);
	EXPECT_STREQ(pConfig->m_QmCustomFont, "chosen font");
}
