#include "test.h"

#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/axiom_scores.h>
#include <game/client/components/qmclient/axiom_scores_data.h>
#include <game/client/components/qmclient/ddnet_player_stats_state.h>
#include <game/client/components/qmclient/statistics_file.h>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

IHttp *CComponentInterfaces::Http() const
{
	return nullptr;
}

namespace
{
	class CFakeAxiomHttpRequest final : public IQmAxiomHttpRequest
	{
		bool m_Done = false;
		bool m_TransportSucceeded = true;
		bool m_Aborted = false;
		int m_StatusCode = 200;
		std::vector<unsigned char> m_vBody;

	public:
		bool Done() const override { return m_Done; }
		bool TransportSucceeded() const override { return m_TransportSucceeded; }
		int StatusCode() const override { return m_StatusCode; }
		void Result(const unsigned char **ppData, size_t *pDataSize) const override
		{
			*ppData = m_vBody.empty() ? nullptr : m_vBody.data();
			*pDataSize = m_vBody.size();
		}
		void Abort() override { m_Aborted = true; }
		// 与真实适配器一致：失败时给出可读原因，成功时为空串。
		const char *ErrorDetail() const override
		{
			if(m_TransportSucceeded && m_StatusCode == 200)
				return "";
			if(!m_TransportSucceeded)
				return m_Aborted ? "aborted" : "network error";
			str_format(m_aErrorDetail, sizeof(m_aErrorDetail), "HTTP %d", m_StatusCode);
			return m_aErrorDetail;
		}

		void Complete(std::string Body, int StatusCode = 200, bool TransportSucceeded = true)
		{
			m_vBody.assign(Body.begin(), Body.end());
			m_StatusCode = StatusCode;
			m_TransportSucceeded = TransportSucceeded;
			m_Done = true;
		}

		void Fail(int StatusCode = 500)
		{
			m_StatusCode = StatusCode;
			m_TransportSucceeded = false;
			m_Done = true;
		}

		bool Aborted() const { return m_Aborted; }

	private:
		mutable char m_aErrorDetail[64] = "";
	};

	struct SRecordedAxiomRequest
	{
		std::string m_Url;
		int m_ConnectTimeoutMs;
		int m_TimeoutMs;
		int64_t m_MaxResponseBytes;
		std::shared_ptr<CFakeAxiomHttpRequest> m_pRequest;
	};

	class CFakeAxiomHttp final : public IQmAxiomHttp
	{
	public:
		std::vector<SRecordedAxiomRequest> m_vRequests;

		std::shared_ptr<IQmAxiomHttpRequest> Get(const char *pUrl, int ConnectTimeoutMs, int TimeoutMs, int64_t MaxResponseBytes) override
		{
			auto pRequest = std::make_shared<CFakeAxiomHttpRequest>();
			m_vRequests.push_back({pUrl, ConnectTimeoutMs, TimeoutMs, MaxResponseBytes, pRequest});
			return pRequest;
		}

		const SRecordedAxiomRequest &Request(size_t Index) const
		{
			return m_vRequests.at(Index);
		}

		std::shared_ptr<CFakeAxiomHttpRequest> LastRequest() const
		{
			return m_vRequests.empty() ? nullptr : m_vRequests.back().m_pRequest;
		}
	};

	class CTestAxiomScores final : public CQmAxiomScores
	{
		int64_t m_Now = time_freq();

	protected:
		int64_t CurrentTick() const override { return m_Now; }

	public:
		explicit CTestAxiomScores(IQmAxiomHttp *pHttp, bool EnableDdStats = false) :
			CQmAxiomScores(pHttp)
		{
			SetDdStatsEnabled(EnableDdStats);
		}

		void AdvanceMs(int64_t Milliseconds)
		{
			m_Now += Milliseconds * time_freq() / 1000;
		}
	};

	EQmAxiomParseResult ParseSearch(const char *pJson, const char *pPlayerName, SQmAxiomSearchMatch &Match)
	{
		return QmParseAxiomSearchResponse(pJson, std::strlen(pJson), pPlayerName, Match);
	}

	EQmAxiomParseResult ParseInfo(const char *pJson, SQmAxiomModeScore &Score)
	{
		return QmParseAxiomInfoResponse(pJson, std::strlen(pJson), Score);
	}

	EQmAxiomParseResult ParseDdStats(const char *pJson, const char *pPlayerName, std::vector<SQmDdStatsGameType> &OutGameTypes)
	{
		return QmParseDdStatsPlayerResponse(pJson, std::strlen(pJson), pPlayerName, OutGameTypes);
	}

	std::string SearchResponse(const char *pPlayerName, int UserId = 5528)
	{
		return std::string("{\"code\":200,\"data\":{\"results\":[{\"user_id\":") +
		       std::to_string(UserId) + ",\"player_name\":\"" + pPlayerName + "\",\"dummy_name\":\"\"}]}}";
	}

	std::string InfoResponse(const char *pPlayerName, int Points, int Playtime = 2)
	{
		return std::string("{\"code\":200,\"data\":{\"player\":{\"player_name\":\"") + pPlayerName +
		       "\",\"points\":" + std::to_string(Points) +
		       ",\"global_rank\":1,\"team_rank\":null,\"total_play_time\":" + std::to_string(Playtime) + ",\"total_maps_completed\":3,\"performance_points\":4,\"mileage\":5},\"difficultyData\":{}}}";
	}

	void CompleteSuccessfulQuery(CTestAxiomScores &Scores, CFakeAxiomHttp &Http, const char *pPlayerName, int UserId = 5528)
	{
		const size_t SearchIndex = Http.m_vRequests.size();
		Scores.EnsureQueried(pPlayerName);
		ASSERT_EQ(Http.m_vRequests.size(), SearchIndex + 1);
		Http.Request(SearchIndex).m_pRequest->Complete(SearchResponse(pPlayerName, UserId));
		Scores.OnUpdate();
		ASSERT_EQ(Http.m_vRequests.size(), SearchIndex + 3);
		Http.Request(SearchIndex + 1).m_pRequest->Complete(InfoResponse(pPlayerName, 10));
		Http.Request(SearchIndex + 2).m_pRequest->Complete(InfoResponse(pPlayerName, 20));
		Scores.OnUpdate();
	}

	void WriteStorageFile(IStorage *pStorage, const char *pFilename, const char *pContents)
	{
		IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_TRUE(File);
		ASSERT_EQ(io_write(File, pContents, str_length(pContents)), str_length(pContents));
		ASSERT_FALSE(io_close(File));
	}

	std::string ReadStorageFile(IStorage *pStorage, const char *pFilename)
	{
		IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
		EXPECT_TRUE(File);
		if(!File)
			return {};
		const int64_t Length = io_length(File);
		std::string Contents((size_t)Length, '\0');
		EXPECT_EQ(io_read(File, Contents.data(), Contents.size()), Length);
		EXPECT_FALSE(io_close(File));
		return Contents;
	}
}

TEST(QmAxiomScoresComponent, SuccessfulLocalQueryPublishesBothModes)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	CompleteSuccessfulQuery(Scores, Http, "wolf_test");
	const SQmAxiomPlayerResult *pResult = Scores.GetResult("wolf_test");
	ASSERT_NE(pResult, nullptr);
	EXPECT_EQ(pResult->m_SearchStatus, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::AXRACE).m_Status, EQmAxiomScoreStatus::READY);
	Scores.SetMode(EQmAxiomMode::GORES);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 10);
	Scores.SetMode(EQmAxiomMode::AXRACE);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 20);
}

TEST(QmAxiomScoresComponent, ScoreboardPrefetchDoesNotWaitForAnotherPlayer)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	for(int Index = 0; Index < 8; ++Index)
		Scores.EnsureQueried(("player_" + std::to_string(Index)).c_str());
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	Scores.OnUpdate();
	for(int Index = 0; Index < 8; ++Index)
		Scores.EnsureQueried(("player_" + std::to_string(Index)).c_str());
	ASSERT_EQ(Http.m_vRequests.size(), 4u);
	Http.Request(1).m_pRequest->Complete(SearchResponse("player_1", 1001));
	Scores.OnUpdate();
	Scores.EnsureQueried("player_1");
	ASSERT_EQ(Http.m_vRequests.size(), 5u);
	EXPECT_NE(Http.Request(4).m_Url.find("mode=Gores"), std::string::npos);
	Http.Request(4).m_pRequest->Complete(InfoResponse("player_1", 37));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("player_1").m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(Scores.GetLookup("player_1").m_Points, 37);
	EXPECT_FALSE(Http.Request(0).m_pRequest->Aborted());
}

TEST(QmAxiomScoresComponent, ModeSwitchCancelsPrefetchAndKeepsOtherModeCache)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	Scores.EnsureQueried("wolf_test");
	Http.Request(0).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	Scores.SetMode(EQmAxiomMode::AXRACE);
	EXPECT_TRUE(Http.Request(1).m_pRequest->Aborted());
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 3u);
	EXPECT_NE(Http.Request(2).m_Url.find("mode=AXRace"), std::string::npos);
	Http.Request(2).m_pRequest->Complete(InfoResponse("wolf_test", 22));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 22);
	Scores.SetMode(EQmAxiomMode::GORES);
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 4u);
	Http.Request(3).m_pRequest->Complete(InfoResponse("wolf_test", 11));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 11);
	Scores.SetMode(EQmAxiomMode::AXRACE);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 22);
}

TEST(QmAxiomScoresComponent, ResetCancelsPrefetchWithoutFailureBackoff)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Scores.OnReset();
	EXPECT_TRUE(Http.Request(0).m_pRequest->Aborted());
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	Scores.OnUpdate();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	Http.Request(0).m_pRequest->Complete(SearchResponse("wolf_test", 999));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::FETCHING);
}

TEST(QmAxiomScoresComponent, FailedPrefetchBacksOffAndRecovers)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	Scores.EnsureQueried("wolf_test");
	Http.Request(0).m_pRequest->Fail();
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::HTTP_ERROR);
	Scores.EnsureQueried("wolf_test");
	EXPECT_EQ(Http.m_vRequests.size(), 1u);
	Scores.AdvanceMs(31 * 1000);
	Scores.OnUpdate();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	Http.Request(1).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetResult("wolf_test")->m_SearchStatus, EQmAxiomScoreStatus::READY);
}

TEST(QmStatisticsFile, DistinguishesMissingInvalidAndValidDocuments)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::NOT_FOUND);
	EXPECT_EQ(pRoot, nullptr);

	const char *pInvalidJson = "{\"local\":";
	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", pInvalidJson);
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/statistics.json"), pInvalidJson);

	const char *pInvalidStructure = "{\"local\":{}}";
	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", pInvalidStructure);
	EXPECT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/statistics.json"), pInvalidStructure);

	WriteStorageFile(pStorage.get(), "qmclient/statistics.json", "{\"local\":{\"modes\":[]},\"remote\":{}}");
	ASSERT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::VALID);
	ASSERT_NE(pRoot, nullptr);
	EXPECT_EQ(json_object_get(json_object_get(pRoot, "local"), "modes")->type, json_array);
	json_value_free(pRoot);
}

TEST(QmStatisticsFile, ClearsOutputForInvalidArguments)
{
	json_value DummyRoot;
	json_value *pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);

	pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, nullptr, &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);

	pRoot = &DummyRoot;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "", &pRoot), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, "qmclient/statistics.json", nullptr), EQmStatisticsFileLoadResult::INVALID);
}

TEST(QmDdnetPlayerStatsState, PendingRefreshStartsImmediatelyAfterCompletedParse)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("DYL");
	ASSERT_TRUE(State.ShouldFetch(100, 60));
	State.BeginHttp("DYL");
	EXPECT_TRUE(State.IsFetching());
	State.CompleteHttp(true, 100, 0);
	ASSERT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::PARSING);
	EXPECT_TRUE(State.IsFetching());
	EXPECT_EQ(State.RequestRefresh(), EQmDdnetPlayerStatsRefreshAction::WAIT_FOR_PARSE);
	EXPECT_TRUE(State.RefreshPending());

	bool StartRefresh = false;
	EXPECT_TRUE(State.CompleteParse("DYL", true, 200, 1700000000, 30, StartRefresh));
	EXPECT_TRUE(StartRefresh);
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_EQ(State.LastSync(), 0);
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 1700000000);
	EXPECT_EQ(State.NextRetry(), 0);
	EXPECT_FALSE(State.LastRequestFailed());
	EXPECT_TRUE(State.ShouldFetch(200, 60));
}

TEST(QmDdnetPlayerStatsState, FailedResponsesWaitForRetryUnlessManuallyRefreshed)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("DYL");
	State.BeginHttp("DYL");
	State.CompleteHttp(false, 100, 30);
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_EQ(State.NextRetry(), 130);
	EXPECT_TRUE(State.LastRequestFailed());
	EXPECT_FALSE(State.ShouldFetch(129, 60));
	EXPECT_TRUE(State.ShouldFetch(130, 60));

	State.BeginHttp("DYL");
	State.CompleteHttp(false, 200, 30);
	EXPECT_EQ(State.RequestRefresh(), EQmDdnetPlayerStatsRefreshAction::START_REQUEST);
	EXPECT_EQ(State.NextRetry(), 0);
	EXPECT_TRUE(State.ShouldFetch(200, 60));
}

TEST(QmDdnetPlayerStatsState, FailedParseSchedulesRetry)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("DYL");
	State.BeginHttp("DYL");
	State.CompleteHttp(true, 100, 0);

	bool StartRefresh = true;
	EXPECT_TRUE(State.CompleteParse("DYL", false, 200, 1700000000, 30, StartRefresh));
	EXPECT_FALSE(StartRefresh);
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_EQ(State.LastSync(), 0);
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 0);
	EXPECT_EQ(State.NextRetry(), 230);
	EXPECT_FALSE(State.ShouldFetch(229, 60));
	EXPECT_TRUE(State.ShouldFetch(230, 60));
}

TEST(QmDdnetPlayerStatsState, KeepsLastSuccessfulTimestampAfterFailedRefresh)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("DYL");
	State.BeginHttp("DYL");
	State.CompleteHttp(true, 100, 0);

	bool StartRefresh = false;
	EXPECT_TRUE(State.CompleteParse("DYL", true, 200, 1700000000, 30, StartRefresh));
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 1700000000);

	EXPECT_EQ(State.RequestRefresh(), EQmDdnetPlayerStatsRefreshAction::START_REQUEST);
	State.BeginHttp("DYL");
	State.CompleteHttp(true, 300, 0);
	EXPECT_TRUE(State.CompleteParse("DYL", false, 400, 1700000100, 30, StartRefresh));
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 1700000000);

	State.SetPlayer("other_player");
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 0);
}

TEST(QmDdnetPlayerStatsState, RefreshDuringHttpClearsRetryAndKeepsRequestReplaceable)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("DYL");
	State.BeginHttp("DYL");

	EXPECT_EQ(State.RequestRefresh(), EQmDdnetPlayerStatsRefreshAction::START_REQUEST);
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::HTTP);
	EXPECT_EQ(State.LastSync(), 0);
	EXPECT_EQ(State.NextRetry(), 0);

	State.AbortHttp();
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_TRUE(State.ShouldFetch(100, 60));
	EXPECT_FALSE(State.LastRequestFailed());
}

TEST(QmDdnetPlayerStatsState, PlayerSwitchDuringHttpInvalidatesRequest)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("old_player");
	State.BeginHttp("old_player");
	State.SetPlayer("new_player");

	EXPECT_EQ(State.PlayerName(), "new_player");
	EXPECT_TRUE(State.RequestPlayerName().empty());
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_TRUE(State.ShouldFetch(100, 60));
}

TEST(QmDdnetPlayerStatsState, CompletesOldPlayerParseWithoutSelectingIt)
{
	CQmDdnetPlayerStatsState State;
	State.SetPlayer("old_player");
	State.BeginHttp("old_player");
	State.CompleteHttp(true, 100, 0);
	State.SetPlayer("new_player");

	bool StartRefresh = true;
	EXPECT_TRUE(State.CompleteParse("old_player", true, 200, 1700000000, 30, StartRefresh));
	EXPECT_FALSE(StartRefresh);
	EXPECT_EQ(State.PlayerName(), "new_player");
	EXPECT_EQ(State.Phase(), EQmDdnetPlayerStatsPhase::IDLE);
	EXPECT_EQ(State.LastSuccessfulSyncTimestamp(), 0);
	EXPECT_TRUE(State.ShouldFetch(200, 60));
}

TEST(QmAxiomScoresUrl, EncodesPlayerNameAndKeepsModesSeparate)
{
	const std::string SearchUrl = QmBuildAxiomSearchUrl("wolf test&dummy");
	EXPECT_EQ(SearchUrl.find("https://api.axiom.teeworlds.cn/v1/query/search?"), 0u);
	EXPECT_NE(SearchUrl.find("q=wolf%20test%26dummy"), std::string::npos);

	EXPECT_EQ(QmBuildAxiomInfoUrl(5528, EQmAxiomMode::GORES), "https://api.axiom.teeworlds.cn/v1/query/user/info?user_id=5528&mode=Gores");
	EXPECT_EQ(QmBuildAxiomInfoUrl(5528, EQmAxiomMode::AXRACE), "https://api.axiom.teeworlds.cn/v1/query/user/info?user_id=5528&mode=AXRace");
	EXPECT_EQ(QmBuildDdStatsPlayerUrl("wolf test&dummy"), "https://ddstats.tw/player/json?player=wolf%20test%26dummy");
}

TEST(QmAxiomScoresInfo, ParsesDdStatsGametypesAndValidatesPlayer)
{
	const char *pJson = R"({
		"profile": {"name": "DYL"},
		"most_played_gametypes": [
			{"key": "DDraceNetwork", "seconds_played": 10289485},
			{"key": "Gores", "seconds_played": 1169640},
			{"key": "AXRace", "seconds_played": 190385}
		]
	})";
	std::vector<SQmDdStatsGameType> vGameTypes;
	EXPECT_EQ(ParseDdStats(pJson, "dyl", vGameTypes), EQmAxiomParseResult::SUCCESS);
	ASSERT_EQ(vGameTypes.size(), 3u);
	EXPECT_EQ(vGameTypes[0].m_Name, "DDraceNetwork");
	EXPECT_EQ(vGameTypes[0].m_PlayTimeSeconds, 10289485);
	EXPECT_EQ(vGameTypes[1].m_Name, "Gores");
	EXPECT_EQ(vGameTypes[1].m_PlayTimeSeconds, 1169640);
	EXPECT_EQ(vGameTypes[2].m_Name, "AXRace");
	EXPECT_EQ(vGameTypes[2].m_PlayTimeSeconds, 190385);
	EXPECT_EQ(ParseDdStats(pJson, "other", vGameTypes), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(ParseDdStats(R"({"profile":{"name":"DYL"},"most_played_gametypes":{}})", "DYL", vGameTypes), EQmAxiomParseResult::INVALID_RESPONSE);
}

TEST(QmAxiomScoresMode, ResolvesCommunityTypeBeforeServerName)
{
	// Axiom 的两种社区分类正好对应两个积分模式。
	const SQmAxiomServerContext GoresCommunity{"Gores", "Axiom ⌬ 广州 ✦ 困难 - CHN9 钩累死"};
	const SQmAxiomServerContext OtherCommunity{"Other", "Axiom ⌬ 广州 ✦ 古典.简单 - CHN9 AXRace"};
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(GoresCommunity), EQmAxiomMode::GORES);
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(OtherCommunity), EQmAxiomMode::AXRACE);
	// 分类缺失时回落到服务器名标记。
	const SQmAxiomServerContext NoTypeAxRace{"", "Axiom ⌬ 广州 ✦ 活动 - CHN9 AXRace"};
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(NoTypeAxRace), EQmAxiomMode::AXRACE);
	// 名字里带 AXRace 的 Gores 服不会被误判：分类优先。
	const SQmAxiomServerContext GoresNamedAxRace{"Gores", "Axiom ⌬ 广州 ✦ 困难 - CHN9 AXRace"};
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(GoresNamedAxRace), EQmAxiomMode::GORES);
	// 都没有线索时按 Axiom 主力玩法 Gores 处理。
	const SQmAxiomServerContext NoHint{"", "Axiom ⌬ 北京 ✦ 普通 - CHN10 钩累死"};
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(NoHint), EQmAxiomMode::GORES);
	const SQmAxiomServerContext Empty{nullptr, nullptr};
	EXPECT_EQ(QmResolveAxiomModeFromServerContext(Empty), EQmAxiomMode::GORES);

	EXPECT_STREQ(QmAxiomModeName(EQmAxiomMode::GORES), "Gores");
	EXPECT_STREQ(QmAxiomModeName(EQmAxiomMode::AXRACE), "AXRace");
	EXPECT_STREQ(QmAxiomModeName(EQmAxiomMode::NONE), "");
}

TEST(QmAxiomScoresSearch, SelectsExactPlayerNameInsteadOfFirstFuzzyResult)
{
	const char *pJson = R"({"code":200,"data":{"results":[
		{"user_id":1,"player_name":"wolf_test2","dummy_name":""},
		{"user_id":5528,"player_name":"wolf_test","dummy_name":"dummy_test"}]}})";

	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(pJson, "wolf_test", Match), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Match.m_UserId, 5528);
	EXPECT_EQ(Match.m_PlayerName, "wolf_test");
	EXPECT_EQ(Match.m_DummyName, "dummy_test");
}

TEST(QmAxiomScoresSearch, AcceptsExactDummyName)
{
	const char *pJson = R"({"code":200,"data":{"results":[{"user_id":77,"player_name":"main_name","dummy_name":"dummy_name"}]}})";

	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(pJson, "dummy_name", Match), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Match.m_UserId, 77);
	EXPECT_EQ(Match.m_PlayerName, "main_name");
}

TEST(QmAxiomScoresSearch, RejectsAmbiguousExactMatches)
{
	const char *pJson = R"({"code":200,"data":{"results":[
		{"user_id":1,"player_name":"dup","dummy_name":""},
		{"user_id":2,"player_name":"dup","dummy_name":""}]}})";

	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(pJson, "dup", Match), EQmAxiomParseResult::AMBIGUOUS);
}

TEST(QmAxiomScoresSearch, AcceptsDuplicateExactMatchesForTheSameUser)
{
	const char *pJson = R"({"code":200,"data":{"results":[
		{"user_id":9,"player_name":"dup","dummy_name":""},
		{"user_id":9,"player_name":"dup","dummy_name":""}]}})";

	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(pJson, "dup", Match), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Match.m_UserId, 9);
}

TEST(QmAxiomScoresSearch, RejectsFuzzyOnlyAndMalformedResponses)
{
	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(R"({"code":200,"data":{"results":[{"user_id":1,"player_name":"other","dummy_name":""}]}})", "wolf_test", Match), EQmAxiomParseResult::NOT_FOUND);
	EXPECT_EQ(ParseSearch(R"({"code":404,"data":{"results":[]}})", "wolf_test", Match), EQmAxiomParseResult::API_ERROR);
	EXPECT_EQ(ParseSearch("{}", "wolf_test", Match), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(ParseSearch("not json", "wolf_test", Match), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(ParseSearch("", "wolf_test", Match), EQmAxiomParseResult::INVALID_RESPONSE);
	// 精确命中但缺少 user_id 时不能当成成功。
	EXPECT_EQ(ParseSearch(R"({"code":200,"data":{"results":[{"player_name":"wolf_test"}]}})", "wolf_test", Match), EQmAxiomParseResult::INVALID_RESPONSE);
}

TEST(QmAxiomScoresSearch, EnforcesSearchResultLimit)
{
	std::string Json = R"({"code":200,"data":{"results":[)";
	for(int Index = 0; Index < 64; ++Index)
	{
		if(Index > 0)
			Json += ',';
		Json += "{\"user_id\":" + std::to_string(Index + 1) + ",\"player_name\":\"" + (Index == 32 ? "wolf_test" : "other") + "\",\"dummy_name\":\"\"}";
	}
	Json += "]}}";
	SQmAxiomSearchMatch Match;
	EXPECT_EQ(ParseSearch(Json.c_str(), "wolf_test", Match), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Match.m_UserId, 33);

	Json.insert(Json.size() - 3, ",\"user_id\":65,\"player_name\":\"extra\",\"dummy_name\":\"\"");
	EXPECT_EQ(ParseSearch(Json.c_str(), "wolf_test", Match), EQmAxiomParseResult::INVALID_RESPONSE);
}

TEST(QmAxiomScoresInfo, ParsesNullableRanksAndDifficultyStats)
{
	const char *pJson = R"({"code":200,"data":{
		"player":{"player_name":"wolf_test","points":1234,"global_rank":56,"team_rank":null,
			"total_play_time":3600,"total_maps_completed":42,"performance_points":900,"mileage":1234},
		"difficultyData":{
			"Novice 简单":{"stats":{"points":100,"global_rank":7,"team_rank":null,"completed_maps":5,"remaining_maps":95,"total_points":4000,"total_maps":120}},
			"Race":{"stats":{"points":0,"global_rank":null,"team_rank":null,"completed_maps":0,"remaining_maps":50,"total_points":null,"total_maps":null}}}}})";

	SQmAxiomModeScore Score;
	ASSERT_EQ(ParseInfo(pJson, Score), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Score.m_PlayerName, "wolf_test");
	EXPECT_EQ(Score.m_Points, 1234);
	ASSERT_TRUE(Score.m_GlobalRank.has_value());
	EXPECT_EQ(*Score.m_GlobalRank, 56);
	EXPECT_FALSE(Score.m_TeamRank.has_value());
	EXPECT_EQ(Score.m_TotalMapsCompleted, 42);
	ASSERT_EQ(Score.m_vDifficulties.size(), 2u);
	EXPECT_EQ(Score.m_vDifficulties[0].m_Name, "Novice 简单");
	EXPECT_EQ(Score.m_vDifficulties[0].m_Points, 100);
	ASSERT_TRUE(Score.m_vDifficulties[0].m_TotalMaps.has_value());
	EXPECT_EQ(*Score.m_vDifficulties[0].m_TotalMaps, 120);
	EXPECT_FALSE(Score.m_vDifficulties[1].m_GlobalRank.has_value());
	EXPECT_FALSE(Score.m_vDifficulties[1].m_TotalMaps.has_value());
}

TEST(QmAxiomScoresInfo, AcceptsLiveGoresResponseShape)
{
	// 当前 Axiom Gores 响应的 difficultyData 同时包含 maps 和 stats；
	// 统计页依赖 player 聚合字段，不能因新增逐图数据而误判整包无效。
	const char *pJson = R"({
		"code": 200,
		"message": "获取成功",
		"error": null,
		"data": {
			"player": {
				"player_name": "DYL",
				"dummy_name": "YL",
				"points": 1620,
				"global_rank": 685,
				"team_rank": null,
				"total_play_time": 695,
				"total_maps_completed": 170,
				"performance_points": 170,
				"mileage": 1620
			},
			"difficultyData": {
				"Solo 单人": {
					"maps": [{"map": "003-solo", "points": 4, "global_rank": 404, "time": "10:31.28", "completions": 1}],
					"stats": {"total_points": 1786, "total_maps": 122, "completed_maps": 39, "remaining_maps": 83, "global_rank": 160, "points": 313, "team_rank": null}
				}
			}
		}
	})";

	SQmAxiomModeScore Score;
	ASSERT_EQ(ParseInfo(pJson, Score), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Score.m_PlayerName, "DYL");
	EXPECT_EQ(Score.m_Points, 1620);
	EXPECT_EQ(Score.m_PerformancePoints, 170);
	EXPECT_EQ(Score.m_TotalPlayTime, 695);
	EXPECT_EQ(Score.m_TotalMapsCompleted, 170);
	ASSERT_EQ(Score.m_vDifficulties.size(), 1u);
	EXPECT_EQ(Score.m_vDifficulties[0].m_CompletedMaps, 39);
}

TEST(QmAxiomScoresInfo, RejectsMissingOrInvalidRequiredFields)
{
	SQmAxiomModeScore Score;
	// 排名字段缺失或为 null 都表示未上榜，不算解析失败。
	EXPECT_EQ(ParseInfo(R"({"code":200,"data":{"player":{"player_name":"x","points":1,"total_play_time":0,"total_maps_completed":0,"performance_points":0,"mileage":0},"difficultyData":{}}})", Score), EQmAxiomParseResult::SUCCESS);
	// 排名字段只接受正整数或 null。
	EXPECT_EQ(ParseInfo(R"({"code":200,"data":{"player":{"player_name":"x","points":1,"global_rank":0,"total_play_time":0,"total_maps_completed":0,"performance_points":0,"mileage":0},"difficultyData":{}}})", Score), EQmAxiomParseResult::INVALID_RESPONSE);
	EXPECT_EQ(ParseInfo(R"({"code":500,"data":{}})", Score), EQmAxiomParseResult::API_ERROR);
	EXPECT_EQ(ParseInfo("{}", Score), EQmAxiomParseResult::INVALID_RESPONSE);
}

TEST(QmAxiomScoresInfo, EnforcesDifficultyLimit)
{
	std::string Json = R"({"code":200,"data":{"player":{"player_name":"wolf_test","points":1,"global_rank":null,"team_rank":null,"total_play_time":1,"total_maps_completed":1,"performance_points":1,"mileage":1},"difficultyData":{)";
	for(int Index = 0; Index < 128; ++Index)
	{
		if(Index > 0)
			Json += ',';
		Json += "\"difficulty_" + std::to_string(Index) + "\":{\"stats\":{\"points\":1,\"global_rank\":null,\"team_rank\":null,\"completed_maps\":1,\"remaining_maps\":1}}";
	}
	Json += "}}}";
	SQmAxiomModeScore Score;
	ASSERT_EQ(ParseInfo(Json.c_str(), Score), EQmAxiomParseResult::SUCCESS);
	EXPECT_EQ(Score.m_vDifficulties.size(), 128u);

	Json.insert(Json.size() - 3, ",\"difficulty_128\":{\"stats\":{\"points\":1,\"completed_maps\":1,\"remaining_maps\":1}}");
	EXPECT_EQ(ParseInfo(Json.c_str(), Score), EQmAxiomParseResult::INVALID_RESPONSE);
}

TEST(QmAxiomScoresPersistence, SkipsInvalidEntriesWithoutDiscardingValidPlayers)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	const char *pJson = R"({
		"remote": {"axiom": {"players": [
			{"name":"bad_points","user_id":1,"player_name":"bad_points","dummy_name":"","modes":[{"mode":"Gores","points":-1,"total_play_time":1,"total_maps_completed":1,"performance_points":1,"mileage":1,"difficulties":[]}]},
			{"name":"bad_id","user_id":0,"player_name":"bad_id","dummy_name":"","modes":[{"mode":"Gores","points":1,"total_play_time":1,"total_maps_completed":1,"performance_points":1,"mileage":1,"difficulties":[]}]},
			{"name":"valid_player","user_id":5528,"player_name":"valid_player","dummy_name":"","modes":[{"mode":"Gores","points":9,"total_play_time":1,"total_maps_completed":2,"performance_points":3,"mileage":4,"difficulties":[]}]}
		]}}
	})";
	json_value *pRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pRoot, nullptr);
	Scores.LoadPersistentCache(pRoot);
	json_value_free(pRoot);

	EXPECT_EQ(Scores.GetResult("bad_points"), nullptr);
	EXPECT_EQ(Scores.GetResult("bad_id"), nullptr);
	const SQmAxiomPlayerResult *pResult = Scores.GetResult("valid_player");
	ASSERT_NE(pResult, nullptr);
	EXPECT_TRUE(pResult->Mode(EQmAxiomMode::GORES).m_HasData);
	EXPECT_EQ(pResult->Mode(EQmAxiomMode::GORES).m_Score.m_Points, 9);
}

TEST(QmAxiomScoresPersistence, InvalidDifficultyDetailsDoNotDiscardAggregateMode)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	const char *pJson = R"({
		"remote": {"axiom": {"players": [{
			"name": "valid_player",
			"user_id": 5528,
			"player_name": "valid_player",
			"dummy_name": "",
			"modes": [{
				"mode": "Gores",
				"points": 9,
				"total_play_time": 1,
				"total_maps_completed": 2,
				"performance_points": 3,
				"mileage": 4,
				"difficulties": [{"name":"Broken","points":-1,"completed_maps":1,"remaining_maps":1}]
			}]
		}]}}
	})";
	json_value *pRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pRoot, nullptr);
	Scores.LoadPersistentCache(pRoot);
	json_value_free(pRoot);

	const SQmAxiomPlayerResult *pResult = Scores.GetResult("valid_player");
	ASSERT_NE(pResult, nullptr);
	const SQmAxiomModeResult &Mode = pResult->Mode(EQmAxiomMode::GORES);
	EXPECT_TRUE(Mode.m_HasData);
	EXPECT_EQ(Mode.m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(Mode.m_Score.m_Points, 9);
	EXPECT_TRUE(Mode.m_Score.m_vDifficulties.empty());
}

TEST(QmAxiomScoresLifecycle, OnlyCurrentGenerationCanPublish)
{
	EXPECT_TRUE(QmAxiomResponseIsCurrent(3, 3, std::string_view("player"), std::string_view("player")));
	EXPECT_FALSE(QmAxiomResponseIsCurrent(3, 2, std::string_view("player"), std::string_view("player")));
	EXPECT_FALSE(QmAxiomResponseIsCurrent(3, 3, std::string_view("player"), std::string_view("other")));
}

namespace
{
	// 异步请求要先在 OnUpdate 里收口，下一次 EnsureQueried 才会推进到下一步。
	void TickWithQuery(CTestAxiomScores &Scores, const char *pPlayerName)
	{
		Scores.OnUpdate();
		Scores.EnsureQueried(pPlayerName);
		Scores.OnUpdate();
	}

	void CompleteFullQuery(CTestAxiomScores &Scores, CFakeAxiomHttp &Http, const char *pPlayerName, int Points, int UserId = 5528)
	{
		const size_t Base = Http.m_vRequests.size();

		TickWithQuery(Scores, pPlayerName);
		ASSERT_EQ(Http.m_vRequests.size(), Base + 1);
		Http.Request(Base).m_pRequest->Complete(SearchResponse(pPlayerName, UserId));

		TickWithQuery(Scores, pPlayerName);
		ASSERT_EQ(Http.m_vRequests.size(), Base + 2);
		Http.Request(Base + 1).m_pRequest->Complete(InfoResponse(pPlayerName, Points));

		TickWithQuery(Scores, pPlayerName);
		ASSERT_EQ(Http.m_vRequests.size(), Base + 2);
	}
}

TEST(QmAxiomScoresComponent, PersistentCacheRoundTripsDifficultyDataInSharedDocument)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	const char *pJson = R"({
		"remote": {"axiom": {"players": [{
			"name": "wolf_test",
			"user_id": 5528,
			"player_name": "wolf_test",
			"dummy_name": "",
			"ddstats_gametypes": [
				{"name": "Gores", "play_time_seconds": 7200},
				{"name": "TestFortune", "play_time_seconds": 143950}
			],
			"modes": [{
				"mode": "Gores",
				"points": 38,
				"total_play_time": 7200,
				"axiom_play_time": 695,
				"total_maps_completed": 11,
				"performance_points": 17,
				"mileage": 42,
				"difficulties": [{
					"name": "Expert",
					"points": 30,
					"global_rank": 7,
					"team_rank": 3,
					"completed_maps": 9,
					"remaining_maps": 5,
					"total_points": 80,
					"total_maps": 14
				}]
			}]
		}]}}
	})";
	json_value *pInputRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pInputRoot, nullptr);
	Scores.LoadPersistentCache(pInputRoot);
	json_value_free(pInputRoot);

	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	IOHANDLE File = pStorage->OpenFile("qmclient/statistics.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_TRUE(File);
	CJsonFileWriter Writer(File);
	Writer.BeginObject();
	Writer.WriteAttribute("local");
	Writer.BeginObject();
	Writer.WriteAttribute("modes");
	Writer.BeginArray();
	Writer.BeginObject();
	Writer.WriteAttribute("mode");
	Writer.WriteStrValue("DDRace");
	Writer.WriteAttribute("maps");
	Writer.WriteIntValue(12);
	Writer.WriteAttribute("score");
	Writer.WriteIntValue(34);
	Writer.EndObject();
	Writer.EndArray();
	Writer.EndObject();
	Writer.WriteAttribute("remote");
	Writer.BeginObject();
	Scores.WritePersistentCache(Writer);
	Writer.EndObject();
	Writer.EndObject();
	ASSERT_TRUE(Writer.Finish());

	json_value *pRoot = nullptr;
	ASSERT_EQ(QmLoadStatisticsFile(pStorage.get(), "qmclient/statistics.json", &pRoot), EQmStatisticsFileLoadResult::VALID);
	ASSERT_NE(pRoot, nullptr);
	const json_value *pLocalModes = json_object_get(json_object_get(pRoot, "local"), "modes");
	ASSERT_EQ(pLocalModes->type, json_array);
	ASSERT_EQ(json_array_length(pLocalModes), 1u);
	const json_value *pPersistedAxiom = json_object_get(json_object_get(json_object_get(pRoot, "remote"), "axiom"), "players");
	ASSERT_EQ(pPersistedAxiom->type, json_array);
	ASSERT_EQ(json_array_length(pPersistedAxiom), 1u);
	const json_value *pPersistedModes = json_object_get(json_array_get(pPersistedAxiom, 0), "modes");
	ASSERT_EQ(pPersistedModes->type, json_array);
	EXPECT_STREQ(json_string_get(json_object_get(json_array_get(pPersistedModes, 0), "axiom_play_time")), "695");

	CTestAxiomScores Restored(&Http);
	Restored.LoadPersistentCache(pRoot);
	json_value_free(pRoot);
	const SQmAxiomPlayerResult *pResult = Restored.GetResult("wolf_test");
	ASSERT_NE(pResult, nullptr);
	const SQmAxiomModeScore &Score = pResult->Mode(EQmAxiomMode::GORES).m_Score;
	ASSERT_EQ(Score.m_vDifficulties.size(), 1u);
	EXPECT_EQ(Score.m_TotalPlayTime, 7200);
	EXPECT_EQ(Score.m_vDifficulties[0].m_Name, "Expert");
	EXPECT_EQ(Score.m_vDifficulties[0].m_CompletedMaps, 9);
	EXPECT_EQ(Score.m_vDifficulties[0].m_RemainingMaps, 5);
	ASSERT_TRUE(Score.m_vDifficulties[0].m_TotalPoints.has_value());
	EXPECT_EQ(*Score.m_vDifficulties[0].m_TotalPoints, 80);
	const std::vector<SQmDdStatsGameType> *pGameTypes = Restored.GetDdStatsGameTypes("wolf_test");
	ASSERT_NE(pGameTypes, nullptr);
	ASSERT_EQ(pGameTypes->size(), 2u);
	EXPECT_EQ((*pGameTypes)[1].m_Name, "TestFortune");
	EXPECT_EQ((*pGameTypes)[1].m_PlayTimeSeconds, 143950);
}

TEST(QmAxiomScoresPersistence, IgnoresDuplicatePersistedModes)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	const char *pJson = R"({
		"remote": {"axiom": {"players": [{
			"name": "wolf_test",
			"user_id": 5528,
			"player_name": "wolf_test",
			"dummy_name": "",
			"modes": [
				{"mode":"Gores","points":1,"total_play_time":2,"total_maps_completed":3,"performance_points":4,"mileage":5,"difficulties":[{"name":"Easy","points":1,"completed_maps":1,"remaining_maps":1}]},
				{"mode":"Gores","points":9,"total_play_time":10,"total_maps_completed":11,"performance_points":12,"mileage":13,"difficulties":[{"name":"Hard","points":9,"completed_maps":9,"remaining_maps":9}]}
			]
		}]}}
	})";
	json_value *pRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pRoot, nullptr);
	Scores.LoadPersistentCache(pRoot);
	json_value_free(pRoot);

	const SQmAxiomPlayerResult *pResult = Scores.GetResult("wolf_test");
	ASSERT_NE(pResult, nullptr);
	const SQmAxiomModeScore &Score = pResult->Mode(EQmAxiomMode::GORES).m_Score;
	EXPECT_EQ(Score.m_Points, 1);
	ASSERT_EQ(Score.m_vDifficulties.size(), 1u);
	EXPECT_EQ(Score.m_vDifficulties[0].m_Name, "Easy");
}

TEST(QmAxiomScoresPersistence, LoadsDdStatsOnlyPlayerEntries)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	const char *pJson = R"({
		"remote": {"axiom": {"players": [{
			"name": "DYL",
			"user_id": 5528,
			"player_name": "DYL",
			"dummy_name": "",
			"ddstats_gametypes": [{"name":"Gores","play_time_seconds":1169640}],
			"modes": []
		}]}}
	})";
	json_value *pRoot = JsonParse(pJson, std::strlen(pJson));
	ASSERT_NE(pRoot, nullptr);
	Scores.LoadPersistentCache(pRoot);
	json_value_free(pRoot);

	const std::vector<SQmDdStatsGameType> *pGameTypes = Scores.GetDdStatsGameTypes("DYL");
	ASSERT_NE(pGameTypes, nullptr);
	ASSERT_EQ(pGameTypes->size(), 1u);
	EXPECT_EQ((*pGameTypes)[0].m_PlayTimeSeconds, 1169640);
}

TEST(QmAxiomScoresLayout, ConstrainsPopupAtCommonUiScales)
{
	const SQmAxiomPopupSize Scale100 = QmAxiomPopupSize(1066.0f, 600.0f);
	EXPECT_FLOAT_EQ(Scale100.m_Width, 360.0f);
	EXPECT_FLOAT_EQ(Scale100.m_Height, 390.0f);

	const SQmAxiomPopupSize Scale150 = QmAxiomPopupSize(711.0f, 400.0f);
	EXPECT_FLOAT_EQ(Scale150.m_Width, 360.0f);
	EXPECT_FLOAT_EQ(Scale150.m_Height, 390.0f);

	const SQmAxiomPopupSize Scale200 = QmAxiomPopupSize(533.0f, 300.0f);
	EXPECT_FLOAT_EQ(Scale200.m_Width, 360.0f);
	EXPECT_FLOAT_EQ(Scale200.m_Height, 290.0f);
	EXPECT_LE(Scale200.m_Width, 533.0f - 10.0f);
	EXPECT_LE(Scale200.m_Height, 300.0f - 10.0f);
}
