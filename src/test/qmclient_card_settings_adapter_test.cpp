#include <engine/shared/config.h>
#include <game/client/components/qmclient/presentation/qm_card_settings_adapter.h>
#include <game/client/ui/card_ui_model.h>

#include <gtest/gtest.h>

#include <memory>

TEST(CardSettingsAdapter, OfficialAndQmCardsUseTheSameConfigContract)
{
	CCardRegistry Registry;
	ASSERT_TRUE(RegisterQmSettingsAdapterCards(Registry));
	ASSERT_TRUE(Registry.RegisterCard({"qm.diagnostics", "Diagnostics", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle"}));
	Registry.Freeze();
	const auto pConfig = std::make_unique<CConfig>();
	pConfig->m_ClShowhud = 1;
	pConfig->m_QmDiagnostics = 0;
	CQmCardSettingsAdapter Adapter(*pConfig);
	ICardSettingsAdapter &Contract = Adapter;
	CCardUiModel Model(Registry);
	const auto Hud = Model.Snapshot("official", "ddnet.hud");
	const auto Diagnostics = Model.Snapshot("official", "qm.diagnostics");
	ASSERT_NE(Hud.m_pDescriptor, nullptr);
	ASSERT_NE(Diagnostics.m_pDescriptor, nullptr);
	EXPECT_EQ(Hud.m_pDescriptor->m_PresentationId, Diagnostics.m_pDescriptor->m_PresentationId);
	ASSERT_TRUE(Contract.Read(*Hud.m_pDescriptor).has_value());
	ASSERT_TRUE(Contract.Read(*Diagnostics.m_pDescriptor).has_value());
	EXPECT_FALSE(Contract.Read(*Hud.m_pDescriptor)->m_RestartRequired);
	EXPECT_TRUE(Contract.Read(*Diagnostics.m_pDescriptor)->m_RestartRequired);
	EXPECT_EQ(pConfig->m_ClShowhud, 1);
	EXPECT_EQ(pConfig->m_QmDiagnostics, 0);
	EXPECT_EQ(Contract.Apply(*Hud.m_pDescriptor, 0), ECardSettingResult::APPLIED);
	EXPECT_EQ(pConfig->m_ClShowhud, 0);
	pConfig->m_ClShowhud = 1;
	EXPECT_EQ(Contract.Read(*Hud.m_pDescriptor)->m_Value, 1);
	EXPECT_EQ(Contract.Apply(*Diagnostics.m_pDescriptor, 1), ECardSettingResult::APPLIED);
	EXPECT_EQ(pConfig->m_QmDiagnostics, 1);
}

TEST(CardSettingsAdapter, RejectsInvalidValuesAndUnrelatedDescriptors)
{
	const auto pConfig = std::make_unique<CConfig>();
	pConfig->m_ClShowhud = 1;
	CQmCardSettingsAdapter Adapter(*pConfig);
	SCardDescriptor Hud{"ddnet.hud", "HUD", {}, "icon", "", {}, ECardOwner::UPSTREAM, 0, true, "toggle"};
	EXPECT_EQ(Adapter.Apply(Hud, -1), ECardSettingResult::INVALID_VALUE);
	EXPECT_EQ(Adapter.Apply(Hud, 2), ECardSettingResult::INVALID_VALUE);
	EXPECT_EQ(pConfig->m_ClShowhud, 1);
	Hud.m_Owner = ECardOwner::QM;
	EXPECT_FALSE(Adapter.Read(Hud).has_value());
	EXPECT_EQ(Adapter.Apply(Hud, 0), ECardSettingResult::UNSUPPORTED);
	Hud.m_Owner = ECardOwner::UPSTREAM;
	Hud.m_PresentationId = "unknown";
	EXPECT_FALSE(Adapter.Read(Hud).has_value());
	EXPECT_EQ(pConfig->m_ClShowhud, 1);
}

TEST(CardSettingsAdapter, SnapshotDoesNotExposeMutableFeatureState)
{
	CCardRegistry Registry;
	SFeatureModel Feature{"qm.feature", "Feature", true, true};
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	ASSERT_TRUE(Registry.RegisterCard({"qm.feature", "Feature", {}, "icon", Feature.m_Id}));
	Registry.Freeze();
	CCardUiModel Model(Registry);
	const SCardModelSnapshot Before = Model.Snapshot("home", "qm.feature");
	Feature.m_Enabled = false;
	Feature.m_Available = false;
	EXPECT_TRUE(Before.m_Enabled);
	EXPECT_TRUE(Before.m_Available);
	EXPECT_FALSE(Model.Snapshot("home", "qm.feature").m_Enabled);
	EXPECT_FALSE(Model.Snapshot("home", "qm.feature").m_Available);
	EXPECT_EQ(Model.Snapshot("home", "qm.missing").m_pDescriptor, nullptr);
	EXPECT_FALSE(Model.Snapshot("home", "qm.missing").m_Available);
}

TEST(CardSettingsAdapter, SnapshotPreferencesFollowPlacementDefaults)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle", 0, true}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	Registry.Freeze();
	CCardUiModel Model(Registry);
	// 描述符默认折叠进入快照偏好；SetPreferences 按 (page, card) 生效。
	EXPECT_TRUE(Model.Snapshot("home", "qm.a").m_Preferences.m_Collapsed);
	EXPECT_TRUE(Model.Snapshot("home", "qm.a").m_Preferences.m_Visible);
	SCardUiPreferences Preferences = Model.Snapshot("home", "qm.a").m_Preferences;
	Preferences.m_Collapsed = false;
	ASSERT_TRUE(Model.SetPreferences("home", "qm.a", Preferences));
	EXPECT_FALSE(Model.Snapshot("home", "qm.a").m_Preferences.m_Collapsed);
}
