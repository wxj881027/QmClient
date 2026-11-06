#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/axiom_scores.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class CResponse final : public IQmAxiomHttpRequest
	{
		std::string m_Body;
		bool m_Done = false;

	public:
		int m_Aborts = 0;
		bool Done() const override { return m_Done; }
		bool TransportSucceeded() const override { return true; }
		int StatusCode() const override { return 200; }
		void Result(const unsigned char **ppData, size_t *pSize) const override
		{
			*ppData = reinterpret_cast<const unsigned char *>(m_Body.data());
			*pSize = m_Body.size();
		}
		void Abort() override { ++m_Aborts; }
		void Complete(std::string Body)
		{
			m_Body = std::move(Body);
			m_Done = true;
		}
	};

	class CHttp final : public IQmAxiomHttp
	{
	public:
		bool m_Accept = false;
		std::string m_LastUrl;
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		std::shared_ptr<IQmAxiomHttpRequest> Get(const char *pUrl, int, int, int64_t) override
		{
			m_LastUrl = pUrl;
			if(!m_Accept)
				return nullptr;
			auto pRequest = std::make_shared<CResponse>();
			m_vRequests.push_back(pRequest);
			return pRequest;
		}
	};

	class CScores final : public CQmAxiomScores
	{
		int64_t CurrentTick() const noexcept override { return time_freq(); }
		const char *StatisticsPlayerName() const override { return g_Config.m_PlayerName; }

	public:
		explicit CScores(IQmAxiomHttp *pHttp) :
			CQmAxiomScores(pHttp) { SetDdStatsEnabled(false); }
	};

	std::string Name(int Index) { return "p" + std::to_string(Index); }

	std::string Player(const std::string &PlayerName, int Points = 9)
	{
		return "{\"name\":\"" + PlayerName + "\",\"user_id\":7,\"player_name\":\"" + PlayerName +
		       "\",\"modes\":[{\"mode\":\"Gores\",\"points\":" + std::to_string(Points) +
		       ",\"total_play_time\":2,\"total_maps_completed\":3,\"performance_points\":4,\"mileage\":5}]}";
	}

	std::string Search(const std::string &PlayerName)
	{
		return "{\"code\":200,\"data\":{\"results\":[{\"user_id\":7,\"player_name\":\"" + PlayerName + "\"}]}}";
	}

	class QmAxiomCacheBudget : public testing::Test
	{
		std::string m_OldPlayerName;

	protected:
		CHttp m_Http;
		CScores m_Scores{&m_Http};
		void SetUp() override
		{
			m_OldPlayerName = g_Config.m_PlayerName;
			str_copy(g_Config.m_PlayerName, "selected");
		}
		void TearDown() override
		{
			m_Scores.OnShutdown();
			str_copy(g_Config.m_PlayerName, m_OldPlayerName.c_str());
		}
		void Load(const std::string &Players)
		{
			const std::string Text = "{\"remote\":{\"axiom\":{\"players\":[" + Players + "]}}}";
			std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Text.data(), Text.size()), json_value_free);
			ASSERT_NE(pRoot, nullptr);
			m_Scores.LoadPersistentCache(pRoot.get());
		}
		std::string Players(int Count)
		{
			std::string Text;
			for(int Index = 0; Index < Count; ++Index)
			{
				if(Index)
					Text += ',';
				Text += Player(Name(Index));
			}
			return Text;
		}
		int CountKnownPlayers(int Count)
		{
			int Present = 0;
			for(int Index = 0; Index < Count; ++Index)
				Present += m_Scores.GetResult(Name(Index).c_str()) != nullptr;
			return Present;
		}
	};
}

TEST_F(QmAxiomCacheBudget, DiskRestoreKeepsFirst128UniquePlayers)
{
	Load(Players(160));
	EXPECT_EQ(CountKnownPlayers(160), 128);
	EXPECT_NE(m_Scores.GetResult("p0"), nullptr);
	EXPECT_NE(m_Scores.GetResult("p127"), nullptr);
	EXPECT_EQ(m_Scores.GetResult("p128"), nullptr);
}

TEST_F(QmAxiomCacheBudget, FullRestoreStillUpdatesExistingName)
{
	Load(Players(128));
	Load(Player("new") + ',' + Player("p0", 42));
	EXPECT_EQ(m_Scores.GetResult("new"), nullptr);
	const auto *pResult = m_Scores.GetResult("p0");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 42);
	EXPECT_EQ(CountKnownPlayers(128), 128);
}

TEST_F(QmAxiomCacheBudget, InvalidRestoredPlayersDoNotConsumeQuota)
{
	std::string Invalid;
	for(int Index = 0; Index < 160; ++Index)
		Invalid += "{\"name\":\"bad\",\"user_id\":0},";
	Load(Invalid + Players(128));
	EXPECT_EQ(CountKnownPlayers(128), 128);
	EXPECT_EQ(m_Scores.GetResult("bad"), nullptr);
}

TEST_F(QmAxiomCacheBudget, StatisticsQueriesStayBoundedAcrossPlayerChanges)
{
	for(int Index = 0; Index < 300; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	EXPECT_EQ(CountKnownPlayers(300), 128);
	EXPECT_EQ(m_Scores.GetResult("p0"), nullptr);
	EXPECT_NE(m_Scores.GetResult("p299"), nullptr);
}

TEST_F(QmAxiomCacheBudget, SameFrameAccessOrderProtectsMostRecentlyUsedEntry)
{
	for(int Index = 0; Index < 128; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	m_Scores.EnsureQueried("p0");
	m_Scores.EnsureQueried("new");
	EXPECT_NE(m_Scores.GetResult("p0"), nullptr);
	EXPECT_EQ(m_Scores.GetResult("p1"), nullptr);
	EXPECT_NE(m_Scores.GetResult("new"), nullptr);
}

TEST_F(QmAxiomCacheBudget, SelectedStatisticsPlayerRemainsAfterRequestHasFinished)
{
	m_Scores.EnsureQueried("selected");
	const auto *pSelected = m_Scores.GetResult("selected");
	ASSERT_NE(pSelected, nullptr);
	EXPECT_FALSE(m_Scores.IsFetchingPlayer("selected"));
	m_Scores.SetMode(EQmAxiomMode::GORES);
	for(int Index = 0; Index < 300; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	EXPECT_EQ(m_Scores.GetResult("selected"), pSelected);
	EXPECT_EQ(CountKnownPlayers(300), 127);
}

TEST_F(QmAxiomCacheBudget, AdmissionOwnsNameBorrowedFromEvictedEntry)
{
	std::string PlayersText = Players(128);
	const std::string BorrowedName = "new_player_name_over_small_string_limit";
	const std::string Original = "\"player_name\":\"p0\"";
	const size_t Position = PlayersText.find(Original);
	ASSERT_NE(Position, std::string::npos);
	PlayersText.replace(Position, Original.size(), "\"player_name\":\"" + BorrowedName + "\"");
	Load(PlayersText);
	const auto *pSource = m_Scores.GetResult("p0");
	ASSERT_NE(pSource, nullptr);
	m_Scores.EnsureQueried(pSource->m_Match.m_PlayerName.c_str());
	EXPECT_EQ(m_Scores.GetResult("p0"), nullptr);
	EXPECT_NE(m_Scores.GetResult(BorrowedName.c_str()), nullptr);
	EXPECT_NE(m_Http.m_LastUrl.find(BorrowedName), std::string::npos);
}

TEST_F(QmAxiomCacheBudget, ScoreboardAdmissionProtectsActiveStatisticsRequest)
{
	Load(Players(128));
	m_Http.m_Accept = true;
	m_Scores.EnsureQueried("p0");
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	const auto *pActive = m_Scores.GetResult("p0");
	m_Http.m_Accept = false;
	m_Scores.SetMode(EQmAxiomMode::GORES);
	for(int Index = 128; Index < 300; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	EXPECT_EQ(m_Scores.GetResult("p0"), pActive);
	EXPECT_FALSE(m_Http.m_vRequests.front()->Done());
	EXPECT_EQ(m_Http.m_vRequests.front()->m_Aborts, 0);
	EXPECT_EQ(CountKnownPlayers(300), 128);
}

TEST_F(QmAxiomCacheBudget, ScoreboardAdmissionProtectsPendingPrefetch)
{
	Load(Players(128));
	m_Scores.SetMode(EQmAxiomMode::GORES);
	m_Http.m_Accept = true;
	m_Scores.EnsureQueried("p0");
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
	const auto *pPending = m_Scores.GetResult("p0");
	m_Http.m_Accept = false;
	for(int Index = 128; Index < 300; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	EXPECT_EQ(m_Scores.GetResult("p0"), pPending);
	EXPECT_EQ(m_Http.m_vRequests.front()->m_Aborts, 0);
	EXPECT_EQ(CountKnownPlayers(300), 128);
}

TEST_F(QmAxiomCacheBudget, FullPinnedCacheRejectsNewNameUntilRequestReleasesEntry)
{
	m_Scores.SetMode(EQmAxiomMode::GORES);
	m_Http.m_Accept = true;
	for(int Index = 0; Index < 64; ++Index)
	{
		m_Scores.EnsureQueried(Name(Index).c_str());
		m_Scores.OnUpdate();
	}
	ASSERT_EQ(m_Http.m_vRequests.size(), 64u);
	for(int Index = 0; Index < 64; ++Index)
		m_Http.m_vRequests[Index]->Complete(Search(Name(Index)));
	m_Scores.OnUpdate();
	for(int Index = 0; Index < 64; ++Index)
		m_Scores.EnsureQueried(Name(Index).c_str());
	ASSERT_EQ(m_Http.m_vRequests.size(), 128u);
	for(int Index = 64; Index < 128; ++Index)
	{
		m_Scores.EnsureQueried(Name(Index).c_str());
		m_Scores.OnUpdate();
	}
	ASSERT_EQ(m_Http.m_vRequests.size(), 192u);
	m_Scores.EnsureQueried("blocked");
	EXPECT_EQ(m_Scores.GetResult("blocked"), nullptr);
	EXPECT_EQ(CountKnownPlayers(128), 128);
	EXPECT_EQ(m_Http.m_vRequests.size(), 192u);
	m_Http.m_vRequests[128]->Complete(R"({"code":200,"data":{"results":[]}})");
	m_Scores.OnUpdate();
	m_Scores.EnsureQueried("blocked");
	EXPECT_NE(m_Scores.GetResult("blocked"), nullptr);
	EXPECT_EQ(m_Scores.GetResult("p64"), nullptr);
}
