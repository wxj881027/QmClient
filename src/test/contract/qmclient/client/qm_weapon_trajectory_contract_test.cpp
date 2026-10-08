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
