#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmWeaponTrajectoryContract, ReusesRenderScratchBuffers)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.cpp");
	EXPECT_NE(Header.find("m_vPoints"), std::string::npos);
	EXPECT_NE(Header.find("m_vLineSegments"), std::string::npos);
	EXPECT_NE(Header.find("m_vLineQuadSegments"), std::string::npos);
	EXPECT_EQ(Source.find("std::vector<vec2> vPoints"), std::string::npos);
	EXPECT_EQ(Source.find("std::vector<IGraphics::CLineItem> vLineSegments"), std::string::npos);
}

TEST(QmWeaponTrajectorySource, PredictsNinjaEndpointUsingCharacterCollision)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/qmclient/weapon_trajectory.cpp");

	EXPECT_NE(Source.find("g_pData->m_Weapons.m_Ninja.m_Movetime"), std::string::npos);
	EXPECT_NE(Source.find("g_pData->m_Weapons.m_Ninja.m_Velocity"), std::string::npos);
	EXPECT_NE(Source.find("Collision()->MoveBox"), std::string::npos);
	EXPECT_NE(Source.find("CCharacterCore::PhysicalSizeVec2()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponTrajectoryExposesDefaultOnPistolGuideToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CardRegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string WeaponTrajectoryBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionWeaponTrajectoryContent(");
	const std::string FunctionDeck = FunctionBody(MenusSource, "void CMenus::RenderSettingsQmClientFunctionDeck(");

	ASSERT_FALSE(WeaponTrajectoryBody.empty());
	ASSERT_FALSE(FunctionDeck.empty());
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryGun, qm_weapon_trajectory_gun, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryNinja, qm_weapon_trajectory_ninja, 0, 0, 1"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryGun, \"qmclient-weapon-trajectory-gun\", Localize(\"Pistol guide line\")"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryNinja, \"qmclient-weapon-trajectory-ninja\", Localize(\"Predict ninja path\")"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory == 0 ? Row() : Row() * 6.0f;"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("手枪辅助线 shouqiang fuzhuxian pistol guide line"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("预测忍者路径 yuce renzhe lujing predict ninja path"), std::string::npos);
}
