#include <base/color.h>

#include <engine/shared/config.h>

#include <game/client/components/qmclient/config_override.h>

#include <gtest/gtest.h>

static void ExpectColorNear(const ColorRGBA &Color, const ColorRGBA &Expected)
{
	EXPECT_NEAR(Color.r, Expected.r, 0.02f);
	EXPECT_NEAR(Color.g, Expected.g, 0.02f);
	EXPECT_NEAR(Color.b, Expected.b, 0.02f);
	EXPECT_NEAR(Color.a, Expected.a, 0.02f);
}

TEST(QmClientConfigOverride, ApplyingOverrideSavesOriginalAndForcesValue)
{
	bool Overridden = false;
	int SavedValue = -1;
	int Target = 0;

	UpdateConfigIntOverride(Target, 1, true, Overridden, SavedValue);

	EXPECT_TRUE(Overridden);
	EXPECT_EQ(SavedValue, 0);
	EXPECT_EQ(Target, 1);
}

TEST(QmClientConfigOverride, ActiveOverrideReappliesForcedValue)
{
	bool Overridden = true;
	int SavedValue = 0;
	int Target = 0;

	UpdateConfigIntOverride(Target, 1, true, Overridden, SavedValue);

	EXPECT_TRUE(Overridden);
	EXPECT_EQ(SavedValue, 0);
	EXPECT_EQ(Target, 1);
}

TEST(QmClientConfigOverride, DisablingOverrideRestoresSavedValue)
{
	bool Overridden = true;
	int SavedValue = 0;
	int Target = 1;

	UpdateConfigIntOverride(Target, 1, false, Overridden, SavedValue);

	EXPECT_FALSE(Overridden);
	EXPECT_EQ(Target, 0);
}

TEST(QmClientConfigOverride, UserChangesDuringOverrideRestoreToLatestDesiredValue)
{
	bool Overridden = false;
	int SavedValue = -1;
	int Target = 0;

	UpdateConfigIntOverride(Target, 1, true, Overridden, SavedValue);
	Target = 7;
	UpdateConfigIntOverride(Target, 1, true, Overridden, SavedValue);
	UpdateConfigIntOverride(Target, 1, false, Overridden, SavedValue);

	EXPECT_FALSE(Overridden);
	EXPECT_EQ(SavedValue, 7);
	EXPECT_EQ(Target, 7);
}

TEST(QmClientConfigOverride, BatchOverrideRestoresLatestDesiredValue)
{
	bool Overridden = false;
	int Target = 0;
	int SavedValue = -1;
	SConfigIntOverrideEntry aEntries[] = {
		{&Target, &SavedValue, 1},
	};

	UpdateConfigIntOverrides(aEntries, sizeof(aEntries) / sizeof(aEntries[0]), true, Overridden);
	Target = 9;
	UpdateConfigIntOverrides(aEntries, sizeof(aEntries) / sizeof(aEntries[0]), true, Overridden);
	UpdateConfigIntOverrides(aEntries, sizeof(aEntries) / sizeof(aEntries[0]), false, Overridden);

	EXPECT_FALSE(Overridden);
	EXPECT_EQ(SavedValue, 9);
	EXPECT_EQ(Target, 9);
}

TEST(QmClientConfigOverride, GoresAutoToggleDisablesFastInputWhenModeTurnsOff)
{
	EXPECT_EQ(DeriveGoresFastInputValue(true, true), 1);
	EXPECT_EQ(DeriveGoresFastInputValue(false, true), 0);
	EXPECT_EQ(DeriveGoresFastInputValue(true, false), 0);
}

TEST(QmClientConfigOverride, GoresAutoToggleDisablesFastInputOthersWhenModeTurnsOff)
{
	EXPECT_EQ(DeriveGoresFastInputOthersValue(true, true), 1);
	EXPECT_EQ(DeriveGoresFastInputOthersValue(false, true), 0);
	EXPECT_EQ(DeriveGoresFastInputOthersValue(true, false), 0);
}

TEST(QmClientConfigOverride, GoresLinkedConfigLeavesManualFastInputUntouchedWhenInactive)
{
	EXPECT_EQ(DeriveGoresLinkedConfigValue(false, false, true, 1), 1);
	EXPECT_EQ(DeriveGoresLinkedConfigValue(false, false, false, 1), 1);
}

TEST(QmClientConfigOverride, GoresLinkedConfigTurnsOnDuringActiveAndOffOnDisableTransition)
{
	EXPECT_EQ(DeriveGoresLinkedConfigValue(false, true, true, 0), 1);
	EXPECT_EQ(DeriveGoresLinkedConfigValue(true, true, true, 0), 1);
	EXPECT_EQ(DeriveGoresLinkedConfigValue(true, false, true, 1), 0);
	EXPECT_EQ(DeriveGoresLinkedConfigValue(true, false, false, 1), 1);
}

TEST(QmClientConfigOverride, ManualFastInputChangeWhileInactiveUpdatesGoresAutoTogglePreference)
{
	EXPECT_EQ(DeriveGoresAutoTogglePreference(false, false, 1, 1, 1, 0), 0);
	EXPECT_EQ(DeriveGoresAutoTogglePreference(false, false, 0, 0, 0, 1), 1);
}

TEST(QmClientConfigOverride, GoresDisableTransitionDoesNotClearAutoTogglePreference)
{
	EXPECT_EQ(DeriveGoresAutoTogglePreference(false, true, 1, 1, 1, 1), 1);
}

TEST(QmClientConfigOverride, FocusUiOverlayHidingDependsOnHideUiToggle)
{
	EXPECT_FALSE(ShouldHideFocusUiOverlays(true, false));
	EXPECT_TRUE(ShouldHideFocusUiOverlays(true, true));
	EXPECT_FALSE(ShouldHideFocusUiOverlays(false, true));
}

TEST(QmClientConfigOverride, FocusConfigGroupsKeepHudUiAndNamesSeparate)
{
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::CL_SHOWHUD), EFocusConfigGroup::HUD);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::CL_SHOWHUD_DDRACE), EFocusConfigGroup::HUD);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::TC_STATUS_BAR), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::TC_NOTIFY_WHEN_LAST), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::QM_PLAYER_STATS_MAP_PROGRESS), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::QM_SMTC_SHOW_HUD), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::QM_DUMMY_MINI_VIEW), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::QM_INPUT_OVERLAY), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::QM_VOICE_SHOW_OVERLAY), EFocusConfigGroup::UI);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::CL_NAME_PLATES), EFocusConfigGroup::NAMES);
	EXPECT_EQ(GetFocusConfigGroup(EFocusConfigTarget::CL_NAME_PLATES_OWN), EFocusConfigGroup::NAMES);
}

TEST(QmClientConfigOverride, FocusModeChatFilterKeepsForceVisibleClientLines)
{
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(true, true, true, true));
	EXPECT_FALSE(ShouldRenderAnyFocusFilteredChat(true, true, false));
	EXPECT_TRUE(ShouldRenderAnyFocusFilteredChat(true, true, true));
}

TEST(QmClientConfigOverride, FocusModeChatFilterStillHidesRegularLines)
{
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(true, false, false, false));
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(false, true, true, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, true, false, false));
}

TEST(QmClientConfigOverride, TranslateUiDefaultColorsMatchSettingsPreviewDefaults)
{
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTranslateBtnColorDisabled, true)), ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTranslateBtnColorEnabled, true)), ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTranslateMenuBgColor, true)), ColorRGBA(0.12f, 0.12f, 0.12f, 0.95f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTranslateMenuOptionSelected, true)), ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTranslateMenuOptionNormal, true)), ColorRGBA(0.20f, 0.20f, 0.20f, 0.90f));
}
