#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmCollisionHitboxContract, SemanticTogglesAndFreezeLaserVolumeAreIndependent)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menu = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Hitbox = ReadTestSourceFile("src/game/client/components/qmclient/collision_hitbox.cpp");
	const std::string Client = ReadTestSourceFile("src/engine/client/client.cpp");

	for(const char *pConfigName : {"QmHitboxShowMap", "QmHitboxShowTeeCollision", "QmHitboxShowTeeFreeze", "QmHitboxShowTeeDeath", "QmHitboxShowPickups", "QmHitboxShowHammer", "QmHitboxShowProjectiles", "QmHitboxShowLasers", "QmHitboxShowFreezeLasers", "QmHitboxShowHook"})
		EXPECT_NE(Config.find(std::string("MACRO_CONFIG_INT(") + pConfigName), std::string::npos) << pConfigName;

	for(const char *pState : {"g_Config.m_QmHitboxShowMap", "g_Config.m_QmHitboxShowTeeCollision", "g_Config.m_QmHitboxShowTeeFreeze", "g_Config.m_QmHitboxShowTeeDeath", "g_Config.m_QmHitboxShowPickups", "g_Config.m_QmHitboxShowHammer", "g_Config.m_QmHitboxShowProjectiles", "g_Config.m_QmHitboxShowLasers", "g_Config.m_QmHitboxShowFreezeLasers", "g_Config.m_QmHitboxShowHook"})
	{
		EXPECT_NE(Menu.find(pState), std::string::npos) << pState;
		EXPECT_NE(Hitbox.find(pState), std::string::npos) << pState;
	}

	for(const char *pToken : {"LASERTYPE_FREEZE", "LASERGUNTYPE_FREEZE", "LASERGUNTYPE_EXPFREEZE", "BuildHitboxCapsuleOutline", "CCharacterCore::PhysicalSize()", "RenderHammerHitboxes();", "RenderProjectileHitboxes();", "RenderLaserHitboxes();", "RenderHookHitboxes();"})
		EXPECT_NE(Hitbox.find(pToken), std::string::npos) << pToken;
	EXPECT_NE(Client.find("if(g_Config.m_ClConfigVersion < 4)"), std::string::npos);
	EXPECT_NE(Client.find("g_Config.m_QmHitboxShowTeeCollision = g_Config.m_QmHitboxShowTees"), std::string::npos);
	EXPECT_NE(Client.find("g_Config.m_QmHitboxShowFreezeLasers = g_Config.m_QmHitboxShowWeapons"), std::string::npos);
}
