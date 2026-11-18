// Rank 回放缓存查找与真实隔离 IStorage 协作：验证“demo 浏览器已下载同一场次”
// 的判定按完整条目身份命中，而不是按地图 + 名次取最新文件。
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/rank_demo_cache.h>
#include <game/client/components/qmclient/rank_demo_manifest.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CQmRankDemoCache : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		// CConfig 体积很大，放堆上；值初始化保证 qm_demo_directory 为空（回落到默认 demos/）
		std::unique_ptr<CConfig> m_pConfig;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pConfig = std::make_unique<CConfig>();
			ASSERT_TRUE(m_pStorage->CreateFolder("demos", IStorage::TYPE_SAVE));
		}

		void WriteDemo(const std::string &Name)
		{
			const std::string Path = "demos/" + Name;
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			io_write(File, "TWDEMO\0", 7);
			io_close(File);
		}

		std::string ExpectedName(const qmclient::rank_demo::SEntry &Entry) const
		{
			return qmclient::rank_demo::BrowserDemoFileName(
				Entry.m_Map.c_str(), Entry.m_Demo.c_str(), Entry.m_Ts, Entry.m_Rank);
		}

		bool Find(const qmclient::rank_demo::SEntry &Entry, std::string *pPath)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			if(!qmclient::rank_demo::FindBrowserDownloadedDemo(m_pStorage.get(), *m_pConfig, Entry, aPath, sizeof(aPath)))
				return false;
			if(pPath != nullptr)
				*pPath = aPath;
			return true;
		}
	};

	qmclient::rank_demo::SEntry MakeEntry(const char *pDemo, int64_t Ts)
	{
		qmclient::rank_demo::SEntry Entry;
		Entry.m_Map = "Teleport 2";
		Entry.m_Rank = 1;
		Entry.m_Time = "105.00";
		Entry.m_Demo = pDemo;
		Entry.m_Ts = Ts;
		return Entry;
	}
} // namespace

TEST_F(CQmRankDemoCache, FindsTheExactEntryDownloadedByTheBrowser)
{
	const auto Entry = MakeEntry("uuid-a-hash.demo.gz", 1700);
	const std::string Name = ExpectedName(Entry);
	ASSERT_FALSE(Name.empty());
	WriteDemo(Name);

	std::string Path;
	ASSERT_TRUE(Find(Entry, &Path));
	EXPECT_NE(Path.find(Name), std::string::npos) << Path;
}

TEST_F(CQmRankDemoCache, DoesNotReuseAnotherEntryOfTheSameMap)
{
	// 同地图的另一场历史 Rank 1 已落盘：按地图 + 名次取最新文件的旧逻辑会误命中，
	// 这里必须判定为未下载，请求才会真的去下载玩家点的那一场。
	const auto Cached = MakeEntry("uuid-a-hash.demo.gz", 1700);
	WriteDemo(ExpectedName(Cached));

	const auto Requested = MakeEntry("uuid-b-hash.demo.gz", 1900);
	EXPECT_FALSE(Find(Requested, nullptr));
}

TEST(QmRankGhostDemoIdentity, UsesTheCompleteDemoNameIgnoringSuffixCase)
{
	qmclient::rank_demo::SEntry First;
	First.m_Map = "Kobra";
	First.m_Time = "176.96";
	First.m_Demo = "6227f2fe-84f9-467e-94f3-dc85612cdffd-a.demo.gz";
	qmclient::rank_demo::SEntry Second = First;
	Second.m_Demo = "31e0186c-521d-4024-939a-ac361f2f9ef2-b.demo.gz";

	EXPECT_EQ(qmclient::rank_demo::RankGhostDemoIdentity(First), "6227f2fe-84f9-467e-94f3-dc85612cdffd-a");
	EXPECT_NE(qmclient::rank_demo::RankGhostDemoIdentity(First), qmclient::rank_demo::RankGhostDemoIdentity(Second));

	Second.m_Demo = "6227f2fe-84f9-467e-94f3-dc85612cdffd-a.DEMO.GZ";
	EXPECT_EQ(qmclient::rank_demo::RankGhostDemoIdentity(First), qmclient::rank_demo::RankGhostDemoIdentity(Second));
}

TEST_F(CQmRankDemoCache, MatchesFileNameCaseInsensitively)
{
	const auto Entry = MakeEntry("uuid-a-hash.demo.gz", 1700);
	std::string Name = ExpectedName(Entry);
	ASSERT_FALSE(Name.empty());
	for(char &Char : Name)
	{
		if(Char >= 'a' && Char <= 'z')
			Char = (char)(Char - 'a' + 'A');
	}
	WriteDemo(Name);

	EXPECT_TRUE(Find(Entry, nullptr));
}
