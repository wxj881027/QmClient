#include "test.h"

#include <base/system.h>

#include <game/client/components/qmclient/axiom_scores.h>
#include <game/client/components/qmclient/axiom_scores_data.h>

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
		explicit CTestAxiomScores(IQmAxiomHttp *pHttp) :
			CQmAxiomScores(pHttp)
		{
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

	std::string SearchResponse(const char *pPlayerName, int UserId = 5528)
	{
		return std::string("{\"code\":200,\"data\":{\"results\":[{\"user_id\":") +
		       std::to_string(UserId) + ",\"player_name\":\"" + pPlayerName + "\",\"dummy_name\":\"\"}]}}";
	}

	std::string InfoResponse(const char *pPlayerName, int Points)
	{
		return std::string("{\"code\":200,\"data\":{\"player\":{\"player_name\":\"") + pPlayerName +
		       "\",\"points\":" + std::to_string(Points) +
		       ",\"global_rank\":1,\"team_rank\":null,\"total_play_time\":2,\"total_maps_completed\":3,\"performance_points\":4,\"mileage\":5},\"difficultyData\":{}}}";
	}
}

TEST(QmAxiomScoresUrl, EncodesPlayerNameAndKeepsModesSeparate)
{
	const std::string SearchUrl = QmBuildAxiomSearchUrl("wolf test&dummy");
	EXPECT_EQ(SearchUrl.find("https://api.axiom.teeworlds.cn/v1/query/search?"), 0u);
	EXPECT_NE(SearchUrl.find("q=wolf%20test%26dummy"), std::string::npos);

	EXPECT_EQ(QmBuildAxiomInfoUrl(5528, EQmAxiomMode::GORES), "https://api.axiom.teeworlds.cn/v1/query/user/info?user_id=5528&mode=Gores");
	EXPECT_EQ(QmBuildAxiomInfoUrl(5528, EQmAxiomMode::AXRACE), "https://api.axiom.teeworlds.cn/v1/query/user/info?user_id=5528&mode=AXRace");
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

TEST(QmAxiomScoresLifecycle, OnlyCurrentModeCanPublish)
{
	EXPECT_TRUE(QmAxiomResponseIsCurrent(3, 3, EQmAxiomMode::GORES, EQmAxiomMode::GORES));
	EXPECT_FALSE(QmAxiomResponseIsCurrent(3, 2, EQmAxiomMode::GORES, EQmAxiomMode::GORES));
	EXPECT_FALSE(QmAxiomResponseIsCurrent(3, 3, EQmAxiomMode::AXRACE, EQmAxiomMode::GORES));
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

TEST(QmAxiomScoresComponent, QueriesOnlyTheCurrentMode)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	EXPECT_EQ(Scores.Mode(), EQmAxiomMode::GORES);

	CompleteFullQuery(Scores, Http, "wolf_test", 4321);
	// 只请求当前模式的分数，不会顺带拉另一个模式。
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	EXPECT_NE(Http.Request(0).m_Url.find("/v1/query/search?"), std::string::npos);
	EXPECT_NE(Http.Request(1).m_Url.find("mode=Gores"), std::string::npos);
	EXPECT_EQ(Http.Request(1).m_Url.find("AXRace"), std::string::npos);

	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 4321);
}

TEST(QmAxiomScoresComponent, ReportsNotRequestedWhileOnlyTheSearchIsDone)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	TickWithQuery(Scores, "wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Http.Request(0).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	// 搜索命中但分数还没回来，对外仍应是「查询中」。
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
}

TEST(QmAxiomScoresComponent, DoesNothingWithoutAModeAndRejectsInvalidNames)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);

	TickWithQuery(Scores, "wolf_test");
	EXPECT_TRUE(Http.m_vRequests.empty());
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);

	Scores.SetMode(EQmAxiomMode::AXRACE);
	Scores.EnsureQueried(nullptr);
	Scores.EnsureQueried("");
	std::string TooLong(300, 'a');
	Scores.EnsureQueried(TooLong.c_str());
	const char aInvalidUtf8[] = {(char)0xff, (char)0xfe, '\0'};
	Scores.EnsureQueried(aInvalidUtf8);
	EXPECT_TRUE(Http.m_vRequests.empty());
}

TEST(QmAxiomScoresComponent, SwitchingModeDiscardsTheOldCacheAndRequestsTheNewMode)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	CompleteFullQuery(Scores, Http, "wolf_test", 111);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 111);

	Scores.SetMode(EQmAxiomMode::AXRACE);
	// 换模式后旧分数立刻作废，并重新走一遍搜索 + AXRace 查询。
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);

	const size_t Base = Http.m_vRequests.size();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), Base + 1);
	EXPECT_NE(Http.Request(Base).m_Url.find("/v1/query/search?"), std::string::npos);
	Http.Request(Base).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), Base + 2);
	EXPECT_NE(Http.Request(Base + 1).m_Url.find("mode=AXRace"), std::string::npos);
}

TEST(QmAxiomScoresComponent, ModeResponseFromAnotherModeIsIgnored)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Http.Request(0).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 2u);

	// 模式响应还在路上时切服换模式：旧响应不能再写进缓存。
	Scores.SetMode(EQmAxiomMode::AXRACE);
	EXPECT_TRUE(Http.Request(1).m_pRequest->Aborted());
	Http.Request(1).m_pRequest->Complete(InfoResponse("wolf_test", 999));
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
}

TEST(QmAxiomScoresComponent, ThrottlesNewSearchesPerFrame)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	for(int i = 0; i < 10; i++)
		Scores.EnsureQueried(("player_" + std::to_string(i)).c_str());
	// 同一帧内只允许少量搜索请求起步。
	EXPECT_EQ(Http.m_vRequests.size(), 2u);

	Scores.OnUpdate();
	for(int i = 0; i < 10; i++)
		Scores.EnsureQueried(("player_" + std::to_string(i)).c_str());
	EXPECT_EQ(Http.m_vRequests.size(), 4u);
}

TEST(QmAxiomScoresComponent, ResetCancelsAndShutdownClearsCache)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	Scores.EnsureQueried("wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Scores.OnReset();
	EXPECT_TRUE(Http.Request(0).m_pRequest->Aborted());
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);

	// 复位后能重新起查询。
	Http.Request(0).m_pRequest->Complete(SearchResponse("wolf_test"));
	Scores.OnUpdate();
	const size_t Base = Http.m_vRequests.size();
	CompleteFullQuery(Scores, Http, "wolf_test", 5);
	EXPECT_GT(Http.m_vRequests.size(), Base);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::READY);
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Points, 5);

	Scores.OnShutdown();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
}

TEST(QmAxiomScoresComponent, SearchFailureUsesThirtySecondBackoff)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	TickWithQuery(Scores, "wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Http.Request(0).m_pRequest->Fail();
	Scores.OnUpdate();
	EXPECT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::HTTP_ERROR);

	// 退避期内不重试。
	TickWithQuery(Scores, "wolf_test");
	EXPECT_EQ(Http.m_vRequests.size(), 1u);

	Scores.AdvanceMs(31 * 1000);
	TickWithQuery(Scores, "wolf_test");
	EXPECT_EQ(Http.m_vRequests.size(), 2u);
}

TEST(QmAxiomScoresComponent, ModeAndSearchCacheUseIndependentTtl)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);
	CompleteFullQuery(Scores, Http, "wolf_test", 10);
	ASSERT_EQ(Scores.GetLookup("wolf_test").m_Status, EQmAxiomScoreStatus::READY);

	// 31 分钟后只刷新模式分数，搜索结果仍在 2 小时 TTL 内。
	Scores.AdvanceMs(31 * 60 * 1000);
	TickWithQuery(Scores, "wolf_test");
	ASSERT_EQ(Http.m_vRequests.size(), 3u);
	EXPECT_NE(Http.Request(2).m_Url.find("mode=Gores"), std::string::npos);
	EXPECT_EQ(Http.Request(2).m_Url.find("/v1/query/search?"), std::string::npos);
}

TEST(QmAxiomScoresComponent, EvictsLeastRecentlyUsedEntryAtCapacity)
{
	CFakeAxiomHttp Http;
	CTestAxiomScores Scores(&Http);
	Scores.SetMode(EQmAxiomMode::GORES);

	// 记分板最多 64 行，这里刻意查 140 个名字把 128 条容量填满。
	// 每次查询立刻让搜索响应返回（真实环境里请求也总会结束），
	// 这样在途槽位不会成为限制，缓存容量才是唯一的约束。
	for(int i = 0; i < 140; i++)
	{
		Scores.OnUpdate();
		Scores.AdvanceMs(1000);
		for(int k = 0; k <= i; k++)
		{
			const std::string Name = "player_" + std::to_string(k);
			Scores.EnsureQueried(Name.c_str());
		}
		for(const SRecordedAxiomRequest &Request : Http.m_vRequests)
		{
			if(Request.m_pRequest->Done())
				continue;
			// 搜索响应必须匹配被查询的名字，否则解析会判成查无此人。
			const size_t QueryPos = Request.m_Url.find("q=");
			const size_t QueryEnd = Request.m_Url.find('&', QueryPos);
			Request.m_pRequest->Complete(SearchResponse(Request.m_Url.substr(QueryPos + 2, QueryEnd - QueryPos - 2).c_str()));
		}
	}

	// 最新的一批名字触发了查询，最旧的两条已经被挤掉。
	ASSERT_GE(Http.m_vRequests.size(), 130u);
	EXPECT_NE(Scores.GetLookup("player_139").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	EXPECT_NE(Scores.GetLookup("player_138").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	EXPECT_EQ(Scores.GetLookup("player_0").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
	EXPECT_EQ(Scores.GetLookup("player_1").m_Status, EQmAxiomScoreStatus::NOT_REQUESTED);
}

TEST(QmAxiomScoresIntegration, ScoreboardShowsTheCurrentModeInline)
{
	const std::string Scoreboard = ReadTestSourceFile("src/game/client/components/scoreboard.cpp");
	const std::string Component = ReadTestSourceFile("src/game/client/components/qmclient/axiom_scores.cpp");

	EXPECT_NE(Scoreboard.find("QmResolveAxiomModeFromServerContext"), std::string::npos);
	EXPECT_NE(Scoreboard.find("m_QmAxiomScores.SetMode("), std::string::npos);
	EXPECT_NE(Scoreboard.find("m_QmAxiomScores.EnsureQueried(GameClient()->m_aClients[i].m_aName)"), std::string::npos);
	EXPECT_NE(Scoreboard.find("GameClient()->m_QmAxiomScores.GetLookup("), std::string::npos);
	// Axiom 积分服上这一列取代 DDNet 点数列。
	EXPECT_NE(Scoreboard.find("AxiomScoreColumn = HasQmAxiomScoreMode()"), std::string::npos);
	EXPECT_NE(Scoreboard.find("if(AxiomScoreColumn)"), std::string::npos);

	EXPECT_NE(Component.find("AbortActiveRequests(true);"), std::string::npos);
	EXPECT_NE(Component.find("QmAxiomResponseIsCurrent"), std::string::npos);
	EXPECT_NE(Component.find("MaxResponseSize(AXIOM_MAX_RESPONSE_BYTES)"), std::string::npos);
	EXPECT_NE(Component.find("AXIOM_INFO_TIMEOUT_MS = 45000"), std::string::npos);
}

TEST(QmAxiomScoresIntegration, RemovesTheDedicatedAxiomPopup)
{
	const std::string Scoreboard = ReadTestSourceFile("src/game/client/components/scoreboard.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/scoreboard.h");

	EXPECT_EQ(Scoreboard.find("m_AxiomScrollRegion"), std::string::npos);
	EXPECT_EQ(Scoreboard.find("m_ShowAxiomScores"), std::string::npos);
	EXPECT_EQ(Scoreboard.find("QmAxiomPopupSize"), std::string::npos);
	EXPECT_EQ(Header.find("m_ShowAxiomScores"), std::string::npos);
	EXPECT_EQ(Header.find("m_aAxiomPlayerName"), std::string::npos);
}

TEST(QmAxiomScoresIntegration, KeepsTheExistingDdnetPointsColumn)
{
	const std::string Scoreboard = ReadTestSourceFile("src/game/client/components/scoreboard.cpp");
	EXPECT_NE(Scoreboard.find("m_PlayerPoints.GetPoints(ClientData.m_aName)"), std::string::npos);
	EXPECT_NE(Scoreboard.find("m_PlayerPoints.EnsureQueried(GameClient()->m_aClients[i].m_aName)"), std::string::npos);
}
