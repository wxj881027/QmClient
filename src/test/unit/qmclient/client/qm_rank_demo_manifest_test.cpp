#include <game/client/components/qmclient/rank_demo_manifest.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(QmRankDemoManifest, SelectsLatestRankOneEntryCaseInsensitively)
{
	const std::string Manifest =
		std::string(R"({"status":"ok","map":"Teleport 2","rank":1,"ts":1700,"time":"105.00","demo":"old.demo.gz","cid":1,"names":["old"]})") +
		"\n" + R"({"status":"ok","map":"Teleport 2","rank":1,"ts":1800,"time":"104.14","demo":"26f401bb-dee0-4874-8c58-ea4dd24a66b7-bc9084494e74caeb.demo.gz","cid":0,"names":["Markus777777","Namzar"]})" +
		"\n" + R"({"status":"ok","map":"Teleport 2","rank":2,"ts":1900,"demo":"rank2.demo.gz"})";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	ASSERT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));
	ASSERT_EQ(Entries.size(), 3U);
	const auto *pEntry = qmclient::rank_demo::FindLatest(Entries, "teleport 2", 1);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Demo, "26f401bb-dee0-4874-8c58-ea4dd24a66b7-bc9084494e74caeb.demo.gz");
	EXPECT_EQ(pEntry->m_Names, "Markus777777, Namzar");
}

TEST(QmRankDemoManifest, RejectsNonOkEntriesAndUnsafeDemoNames)
{
	const std::string Manifest =
		R"({"status":"failed","map":"Map","rank":1,"ts":4,"demo":"failed.demo.gz"})
{"status":"ok","map":"Map","rank":1,"ts":5,"demo":"../escape.demo.gz"}
{"status":"ok","map":"Map","rank":1,"ts":6,"demo":"safe.demo.gz"})";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	ASSERT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));
	ASSERT_EQ(Entries.size(), 1U);
	EXPECT_EQ(Entries[0].m_Demo, "safe.demo.gz");
}

TEST(QmRankDemoManifest, AcceptsManifestWithoutTrailingNewline)
{
	const std::string Manifest = R"({"status":"ok","map":"Map","rank":1,"ts":1,"demo":"map.demo.gz"})";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	EXPECT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));
	ASSERT_EQ(Entries.size(), 1U);
	EXPECT_EQ(Entries[0].m_Map, "Map");
}

TEST(QmRankDemoManifest, AcceptsLeadingBomAndWhitespace)
{
	const std::string Manifest = "\xef\xbb\xbf  \t{\"status\":\"ok\",\"map\":\"Map\",\"rank\":1,\"ts\":1,\"demo\":\"map.demo.gz\"} \r\n";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	EXPECT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));
	ASSERT_EQ(Entries.size(), 1U);
	EXPECT_EQ(Entries[0].m_Demo, "map.demo.gz");
}

TEST(QmRankDemoManifest, ParsesKindTeamFinishersAndUuid)
{
	const std::string Manifest = std::string(R"({"kind":"team","map":"Map","names":["a","b"],"time":"120.5","ts":1700,"uuid":"4830765d-9d84-49ee-8dfe-5672e924c6ab","rank":1,"status":"ok","cid":8,"team":7,"finishers":[4,8],"demo":"team.demo.gz","rev":"13a314d0099f"})") +
				     "\n" + R"({"kind":"solo","map":"Map","names":["c"],"time":"80.5","ts":1800,"rank":1,"status":"ok","cid":0,"team":0,"finishers":[0],"demo":"solo.demo.gz"})";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	ASSERT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));
	ASSERT_EQ(Entries.size(), 2U);

	EXPECT_TRUE(qmclient::rank_demo::IsTeamEntry(Entries[0]));
	EXPECT_EQ(Entries[0].m_Team, 7);
	EXPECT_EQ(Entries[0].m_Uuid, "4830765d-9d84-49ee-8dfe-5672e924c6ab");
	EXPECT_EQ(Entries[0].m_Rev, "13a314d0099f");
	ASSERT_EQ(Entries[0].m_vFinishers.size(), 2U);
	EXPECT_EQ(Entries[0].m_vFinishers[0], 4);
	EXPECT_EQ(Entries[0].m_vFinishers[1], 8);

	EXPECT_FALSE(qmclient::rank_demo::IsTeamEntry(Entries[1]));
}

TEST(QmRankDemoManifest, CollectRankEntriesMergesMembersAndSortsSoloFirst)
{
	const std::string Manifest =
		std::string(R"({"kind":"team","map":"Map","names":["alice"],"time":"200.0","ts":1700,"uuid":"uuid-team","rank":1,"status":"ok","cid":1,"team":3,"finishers":[1,2],"demo":"team.demo.gz"})") +
		"\n" + R"({"kind":"team","map":"Map","names":["bob"],"time":"200.0","ts":1700,"uuid":"uuid-team","rank":1,"status":"ok","cid":2,"team":3,"finishers":[1,2],"demo":"team.demo.gz"})" +
		"\n" + R"({"kind":"solo","map":"Map","names":["carol"],"time":"100.0","ts":1600,"uuid":"uuid-solo","rank":1,"status":"ok","cid":0,"team":0,"finishers":[0],"demo":"solo.demo.gz"})" +
		"\n" + R"({"kind":"solo","map":"Map","names":["dave"],"time":"90.0","ts":1500,"uuid":"uuid-solo","rank":1,"status":"ok","cid":3,"team":0,"finishers":[3],"demo":"solo.demo.gz"})" +
		"\n" + R"({"kind":"solo","map":"Other","names":["eve"],"time":"1.0","ts":1400,"rank":1,"status":"ok","cid":4,"finishers":[4],"demo":"other.demo.gz"})" +
		"\n" + R"({"kind":"solo","map":"Map","names":["frank"],"time":"99.0","ts":1499,"rank":2,"status":"ok","cid":5,"finishers":[5],"demo":"rank2.demo.gz"})";
	std::vector<qmclient::rank_demo::SEntry> Entries;
	ASSERT_TRUE(qmclient::rank_demo::ParseManifest(reinterpret_cast<const unsigned char *>(Manifest.data()), Manifest.size(), Entries));

	std::vector<qmclient::rank_demo::SEntry> Collected;
	qmclient::rank_demo::CollectRankEntries(Entries, "map", 1, Collected);
	ASSERT_EQ(Collected.size(), 2U);

	// solo 在前，且同一 demo 的成员记录已合并
	ASSERT_FALSE(qmclient::rank_demo::IsTeamEntry(Collected[0]));
	EXPECT_EQ(Collected[0].m_Demo, "solo.demo.gz");
	EXPECT_EQ(Collected[0].m_Names, "carol, dave");
	EXPECT_EQ(Collected[0].m_Ts, 1600);

	ASSERT_TRUE(qmclient::rank_demo::IsTeamEntry(Collected[1]));
	EXPECT_EQ(Collected[1].m_Demo, "team.demo.gz");
	EXPECT_EQ(Collected[1].m_Names, "alice, bob");
	ASSERT_EQ(Collected[1].m_vFinishers.size(), 2U);

	// 其他地图与名次的记录不混入
	EXPECT_EQ(std::count_if(Collected.begin(), Collected.end(), [](const qmclient::rank_demo::SEntry &Entry) { return Entry.m_Demo == "other.demo.gz" || Entry.m_Demo == "rank2.demo.gz"; }), 0);
}

TEST(QmRankDemoManifest, CollectRankEntriesHandlesEmptyInputs)
{
	std::vector<qmclient::rank_demo::SEntry> Collected;
	qmclient::rank_demo::CollectRankEntries({}, "Map", 1, Collected);
	EXPECT_TRUE(Collected.empty());

	std::vector<qmclient::rank_demo::SEntry> Entries;
	Entries.push_back({"Map", 1, "1.0", "demo.gz", "a", 0, 1});
	qmclient::rank_demo::CollectRankEntries(Entries, nullptr, 1, Collected);
	EXPECT_TRUE(Collected.empty());
	qmclient::rank_demo::CollectRankEntries(Entries, "", 1, Collected);
	EXPECT_TRUE(Collected.empty());
}
