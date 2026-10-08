#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmUiPresentationSource, OverlaysUsePresentationState)
{
	const std::string OverlaySource = ReadTestSourceFile("src/game/client/QmUi/UiOverlays.h");

	EXPECT_NE(OverlaySource.find("ResolveUiPresentationStateValue"), std::string::npos);
	EXPECT_NE(OverlaySource.find("SetUiPresentationStateValue"), std::string::npos);
	EXPECT_EQ(OverlaySource.find("->SetValue("), std::string::npos);
}
