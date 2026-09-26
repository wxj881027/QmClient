#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmCardOrderModelContract, HeaderDocumentsPipeFormatAndLegacyCompatibility)
{
	const std::string Header = ReadTestSourceFile("src/game/client/QmUi/QmCardOrderModel.h");
	EXPECT_NE(Header.find("格式 \"stableId|tab|column|order;\""), std::string::npos);
	EXPECT_NE(Header.find("兼容旧 \"id:col:order\""), std::string::npos);
	EXPECT_EQ(Header.find("格式 \"id:col:order;\""), std::string::npos);
}
