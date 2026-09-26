#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmAxiomScoresContract, KeepsTheExistingDdnetPointsColumn)
{
	const std::string Scoreboard = ReadTestSourceFile("src/game/client/components/scoreboard.cpp");
	EXPECT_NE(Scoreboard.find("m_PlayerPoints.GetPoints(ClientData.m_aName)"), std::string::npos);
	EXPECT_NE(Scoreboard.find("m_PlayerPoints.EnsureQueried(GameClient()->m_aClients[i].m_aName)"), std::string::npos);
}
