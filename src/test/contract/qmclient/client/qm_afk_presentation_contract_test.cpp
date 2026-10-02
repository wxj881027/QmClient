#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmAfkPresentationContract, AfkStateDoesNotChangeTeeHookOrNameplateOpacity)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/qmclient/afk_presentation.h");
	const std::string Players = ReadTestSourceFile("src/game/client/components/players.cpp");
	const std::string Nameplates = ReadTestSourceFile("src/game/client/components/nameplates.cpp");
	EXPECT_EQ(Header.find("QM_AFK_PRESENTATION_ALPHA"), std::string::npos);
	EXPECT_EQ(Header.find("ApplyQmAfkPresentationAlpha"), std::string::npos);
	EXPECT_EQ(Players.find("ApplyQmAfkPresentationAlpha"), std::string::npos);
	EXPECT_EQ(Players.find("Afk ? Alpha : 1.0f"), std::string::npos);
	EXPECT_EQ(Nameplates.find("ApplyQmAfkPresentationAlpha"), std::string::npos);
}
