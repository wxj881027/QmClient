#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmFontIconsContract, FontIndexKeepsIconFacesInSyncWithCodepoints)
{
	const std::string Index = ReadTestSourceFile("data/fonts/index.json");
	EXPECT_NE(Index.find("\"icon\": \"Phosphor\""), std::string::npos);
	EXPECT_NE(Index.find("\"icon bold\": \"Phosphor-Bold\""), std::string::npos);
	EXPECT_NE(Index.find("\"Noto Emoji\",\n        \"Phosphor\""), std::string::npos);
	EXPECT_EQ(Index.find("Phosphor-Regular.ttf"), std::string::npos);
	EXPECT_EQ(Index.find("Phosphor-Bold.ttf"), std::string::npos);
	EXPECT_FALSE(ReadTestSourceFile("data/qmclient/fonts/Phosphor/Phosphor-Regular.ttf").empty());
	EXPECT_FALSE(ReadTestSourceFile("data/qmclient/fonts/Phosphor/Phosphor-Bold.ttf").empty());
}
