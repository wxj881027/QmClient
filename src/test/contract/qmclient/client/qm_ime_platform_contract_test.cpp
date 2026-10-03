#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmImePlatform, FlashGuardsAreWiredInInputAndManager)
{
	const std::string InputSource = ReadTestSourceFile("src/engine/client/input.cpp");
	const std::string ManagerSource = ReadTestSourceFile("src/game/client/qm_ime_manager.cpp");

	EXPECT_NE(InputSource.find("QmImeEmptyTextEditingShouldClearCandidates"), std::string::npos);
	EXPECT_NE(InputSource.find("QmImeResolveCandidateReloadAction"), std::string::npos);
	EXPECT_NE(InputSource.find("EQmImeCandidateReloadAction::KEEP_PREVIOUS"), std::string::npos);
	EXPECT_NE(InputSource.find("m_ImeSuppressStaleCandidateReload"), std::string::npos);
	EXPECT_NE(InputSource.find("QmImeShouldSuppressStaleCandidateReload"), std::string::npos);
	EXPECT_NE(ManagerSource.find("State.m_Visible = QmImePopupShouldBeVisible(CandidateCount);"), std::string::npos);
	EXPECT_EQ(ManagerSource.find("State.m_Visible = HasComposition && CandidateCount > 0;"), std::string::npos);
}
