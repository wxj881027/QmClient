#include <game/client/components/qmclient/map_download_index.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(QmMapDownloadIndexSelection, KeepsTheExactMapWhenAnotherMapHasTheSamePrefix)
{
	const std::vector<std::string> vHrefs = {
		"Map_50c7e9ad.map",
		"Map_Extended_50c7e9ad_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.map",
	};

	EXPECT_EQ(QmSelectMapDownloadHref(vHrefs, "Map_"), "Map_50c7e9ad.map");
}

TEST(QmMapDownloadIndexSelection, MissingExactMapDoesNotReturnAnotherMap)
{
	const std::vector<std::string> vHrefs = {"Map_Extended_50c7e9ad.map"};

	EXPECT_TRUE(QmSelectMapDownloadHref(vHrefs, "Map_").empty());
}

TEST(QmMapDownloadIndexSelection, PrefersTheLongestSupportedHashSuffix)
{
	const std::string Name = "Map%20One_";
	const std::string Sha = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	const std::vector<std::string> vHrefs = {Name + "50c7e9ad.map", Name + "50c7e9ad_" + Sha + ".map", Name + Sha + ".map"};

	EXPECT_EQ(QmSelectMapDownloadHref(vHrefs, Name), Name + "50c7e9ad_" + Sha + ".map");
}
