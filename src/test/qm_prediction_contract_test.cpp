#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmPredictionModeContract, UpdatePredictionDoesNotOverrideClientOptIn)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t FunctionStart = Source.find("void CGameClient::UpdatePrediction()");
	ASSERT_NE(FunctionStart, std::string::npos);
	const size_t FunctionEnd = Source.find("\nvoid CGameClient::", FunctionStart + 1);
	ASSERT_NE(FunctionEnd, std::string::npos);
	const std::string FunctionBody = Source.substr(FunctionStart, FunctionEnd - FunctionStart);
	const std::string Assignment = "m_GameWorld.m_WorldConfig.m_PredictEvents =";
	const size_t FirstAssignment = FunctionBody.find(Assignment);
	ASSERT_NE(FirstAssignment, std::string::npos);
	EXPECT_EQ(FunctionBody.find(Assignment, FirstAssignment + Assignment.size()), std::string::npos);
	EXPECT_NE(FunctionBody.find("m_GameWorld.m_WorldConfig.m_PredictEvents = g_Config.m_ClPredictEvents && m_GameInfo.m_PredictEvents;"), std::string::npos);
}
