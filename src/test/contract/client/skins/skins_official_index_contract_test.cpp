// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/skin_catalog_parse.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(SkinsOfficialIndexContract, OfficialCatalogUsesTypedJsonParser)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string CatalogSource = ReadTestSourceFile("src/game/client/components/qmclient/skin_catalog_parse.h");

	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_URL = \"https://ddnet.org/skins/skin/skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("OFFICIAL_SKIN_INDEX_CACHE_PATH = \"downloadedskins/official_skins.json\""), std::string::npos);
	EXPECT_NE(Source.find("qm_skin_catalog::EFormat::DDNET_JSON"), std::string::npos);
	EXPECT_NE(CatalogSource.find("DDNET_JSON"), std::string::npos);
}
