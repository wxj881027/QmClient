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

// 配置注册合同：保留本轮卡片迁移之前的保存键与默认值。
TEST(QmNewUiMenuBranches, WeaponTrajectoryConfigDefaultsRemainRegistered)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryGun, qm_weapon_trajectory_gun, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryNinja, qm_weapon_trajectory_ninja, 0, 0, 1"), std::string::npos);
}
