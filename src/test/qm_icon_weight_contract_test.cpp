#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmIconWeightContract, UsesTheExistingContainerInvalidationPath)
{
	const std::string GameClient = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/gameclient.h");
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	EXPECT_NE(Header.find("void SyncQmUiIconWeight();"), std::string::npos);
	const size_t Sync = GameClient.find("void CGameClient::SyncQmUiIconWeight()");
	ASSERT_NE(Sync, std::string::npos);
	const size_t PrivacyRefresh = GameClient.find("void CGameClient::RefreshStreamerSkinPrivacyAfterStateChange", Sync);
	ASSERT_NE(PrivacyRefresh, std::string::npos);
	const std::string SyncBody = GameClient.substr(Sync, PrivacyRefresh - Sync);
	EXPECT_NE(SyncBody.find("TextRender()->SetIconFontWeight"), std::string::npos);
	EXPECT_NE(SyncBody.find("SetIconFontWeight(Weight)"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_QmIconManager.RefreshForCurrentDpi();"), std::string::npos);
	EXPECT_NE(SyncBody.find("OnWindowResize();"), std::string::npos);
	EXPECT_NE(Settings.find("GameClient()->SyncQmUiIconWeight();"), std::string::npos);
}
