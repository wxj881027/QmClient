#include <game/client/components/qmclient/finish_summary_data.h>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>

using namespace QmFinishSummary;

namespace
{
	SSnapshot Finish(ECommunity Community = ECommunity::DDNET)
	{
		SSnapshot Snapshot;
		Snapshot.m_Community = Community;
		Snapshot.m_Map = "Map A";
		Snapshot.m_Player = "Alpha";
		Snapshot.m_TimeMs = 100000;
		return Snapshot;
	}
}

TEST(QmFinishSummaryData, DdnetMapRequiresExactNameAndRetainsOutputOnFailure)
{
	constexpr const char *pJson = R"({"name":"Map A","mapper":"Mapper","type":"Moderate","difficulty":3,"points":11})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetMap(pJson, std::strlen(pJson), "Map A", Data));
	EXPECT_EQ(Data.m_Mapper, "Mapper");
	EXPECT_EQ(Data.m_Stars, 3);
	EXPECT_EQ(Data.m_MapPoints, 11);
	EXPECT_FALSE(ParseDdnetMap(pJson, std::strlen(pJson), "Map B", Data));
	EXPECT_EQ(Data.m_Map, "Map A");
}

TEST(QmFinishSummaryCoverData, OfficialThumbnailSurvivesPartialAfterFinishData)
{
	constexpr const char *pJson = R"({"name":"Map A","thumbnail":"https://ddnet.org/ranks/maps/Map_A.png"})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetMap(pJson, std::strlen(pJson), "Map A", Data));
	EXPECT_EQ(Data.m_Thumbnail, "https://ddnet.org/ranks/maps/Map_A.png");
	auto Snapshot = Finish();
	ApplyRemote(Snapshot, Data, {}, false);
	EXPECT_EQ(Snapshot.m_Thumbnail, Data.m_Thumbnail);
}

TEST(QmFinishSummaryCoverData, InvalidThumbnailIsIgnoredWithoutRejectingMap)
{
	for(const char *pJson : {
		    R"({"name":"Map A","thumbnail":"https://example.com/Map_A.png"})",
		    R"({"name":"Map A","thumbnail":"https://ddnet.org/ranks/maps/Map_A.png\u0000suffix"})",
		    R"({"name":"Map A","thumbnail":5})",
		    R"({"name":"Map A"})"})
	{
		SCOPED_TRACE(pJson);
		SRemoteData Data;
		Data.m_Thumbnail = "previous";
		ASSERT_TRUE(ParseDdnetMap(pJson, std::strlen(pJson), "Map A", Data));
		EXPECT_TRUE(Data.m_Thumbnail.empty());
	}
}

TEST(QmFinishSummaryCoverData, AxiomSnapshotDoesNotUseDdnetCover)
{
	auto Snapshot = Finish(ECommunity::AXIOM_GORES);
	SRemoteData Data;
	Data.m_Thumbnail = "https://ddnet.org/ranks/maps/Map_A.png";
	ApplyRemote(Snapshot, {}, Data, false);
	EXPECT_TRUE(Snapshot.m_Thumbnail.empty());
}

TEST(QmFinishSummaryData, DdnetMapRejectsEmbeddedNullAndInvalidJson)
{
	constexpr const char *pJson = R"({"name":"Map A\u0000extra","mapper":"Mapper"})";
	SRemoteData Data;
	EXPECT_FALSE(ParseDdnetMap(pJson, std::strlen(pJson), "Map A", Data));
	EXPECT_FALSE(ParseDdnetMap("{", 1, "Map A", Data));
	EXPECT_FALSE(ParseDdnetMap(nullptr, 5, "Map A", Data));
	EXPECT_FALSE(ParseDdnetMap("{}", 8 * 1024 * 1024 + 1, "Map A", Data));
}

TEST(QmFinishSummaryData, PlayerDataCountsDistinctMapsAndFindsLatestMatchingFinish)
{
	constexpr const char *pJson = R"({"player":"Alpha","points":{"points":1234},"types":{"Moderate":{"maps":{
		"Map A":{"points":11,"finishes":9,"rank":125,"time":100.123},
		"Map B":{"points":7,"finishes":0},"Map C":{"points":9,"finishes":2,"time":90}}}},
		"last_finishes":[{"map":"Map B","time":80,"timestamp":150},
		{"map":"Map A","time":100.123,"timestamp":120},{"map":"Map A","time":115,"timestamp":100}]})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetPlayer(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_EQ(Data.m_Points, 1234);
	EXPECT_EQ(Data.m_Rank, 125);
	EXPECT_EQ(Data.m_BestTimeMs, 100123);
	EXPECT_TRUE(Data.m_HistoryKnown);
	EXPECT_EQ(Data.m_CompletedMaps, 2);
	EXPECT_EQ(Data.m_TotalMaps, 3);
	EXPECT_EQ(Data.m_LatestTimestamp, 120);
	EXPECT_EQ(Data.m_LatestTimeMs, 100123);
	EXPECT_FALSE(ParseDdnetPlayer(pJson, std::strlen(pJson), "alpha", "Map A", Data));
}

TEST(QmFinishSummaryData, IndividualRankPointsDistinguishUnrankedFromAnUnknownBaseline)
{
	SRemoteData Data;
	const auto ParseProfile = [&](const char *pStandings) {
		const std::string Json = std::string(R"({"player":"Alpha","types":{},"points":{"points":500},"team_rank":{"points":300},)") + pStandings + "}";
		return ParseDdnetPlayer(Json.c_str(), Json.size(), "Alpha", "Map A", Data);
	};
	ASSERT_TRUE(ParseProfile(R"("rank":{"rank":4,"points":12})"));
	EXPECT_EQ(Data.m_RankPoints, 12);
	ASSERT_TRUE(ParseProfile(R"("rank":{"rank":null})"));
	EXPECT_EQ(Data.m_RankPoints, 0);
	ASSERT_TRUE(ParseProfile(R"("rank":{"rank":4})"));
	EXPECT_FALSE(Data.m_RankPoints);
	ASSERT_TRUE(ParseProfile(R"("rank":{"points":-1})"));
	EXPECT_FALSE(Data.m_RankPoints);
}

TEST(QmFinishSummaryData, ExplicitZeroFinishesEstablishesFirstFinishBaseline)
{
	constexpr const char *pJson = R"({"player":"Alpha","points":{"points":0},"types":{"Solo":{"maps":{"Map A":{"points":4,"finishes":0}}}}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetPlayer(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_TRUE(Data.m_HistoryKnown);
	EXPECT_FALSE(Data.m_BestTimeMs);
	EXPECT_EQ(Data.m_CompletedMaps, 0);
	EXPECT_EQ(Data.m_Points, 0);
}

TEST(QmFinishSummaryData, MissingOrMalformedHistoryDoesNotEstablishFirstFinish)
{
	constexpr const char *pJson = R"({"player":"Alpha","types":{"Solo":{"maps":{"Map A":{"time":-3},"Map B":{"finishes":1}}}}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetPlayer(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_FALSE(Data.m_HistoryKnown);
	EXPECT_FALSE(Data.m_BestTimeMs);
	EXPECT_FALSE(Data.m_CompletedMaps);
	EXPECT_FALSE(Data.m_Points);
}

TEST(QmFinishSummaryData, OmittedFinishCountInKnownUnfinishedMapCountsAsZero)
{
	constexpr const char *pJson = R"({"player":"Alpha","points":{"points":4},"types":{"Solo":{"maps":{
		"Map A":{"points":4,"total_finishes":100},
		"Map B":{"points":4,"finishes":2,"time":80,"total_finishes":99}}}}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetPlayer(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_TRUE(Data.m_HistoryKnown);
	EXPECT_FALSE(Data.m_BestTimeMs);
	EXPECT_EQ(Data.m_CompletedMaps, 1);
	EXPECT_EQ(Data.m_TotalMaps, 2);
}

TEST(QmFinishSummaryData, ChinaRankMatchesPlayerAndRetainsBestTime)
{
	constexpr const char *pJson = R"({"name":"Map A","ranks":[{"player":"Beta","rank":1,"time":80},{"player":"Alpha","rank":2,"time":95.12}]})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetChinaRank(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_EQ(Data.m_ChinaRank, 2);
	EXPECT_EQ(Data.m_ChinaBestTimeMs, 95120);
	EXPECT_TRUE(Data.m_ChinaRankKnown);
	ASSERT_TRUE(ParseDdnetChinaRank(pJson, std::strlen(pJson), "Outside", "Map A", Data));
	EXPECT_TRUE(Data.m_ChinaRankKnown);
	EXPECT_FALSE(Data.m_ChinaRank);
}

TEST(QmFinishSummaryData, AxiomResolvesExactDummyNameToAccount)
{
	constexpr const char *pJson = R"({"code":200,"data":{"items":[{"user":{"user_id":42,"player":{"name":"Other"},"dummy":{"name":"Alpha"}}}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomSearch(pJson, std::strlen(pJson), "Alpha", Data));
	EXPECT_EQ(Data.m_UserId, 42);
	EXPECT_FALSE(ParseAxiomSearch(pJson, std::strlen(pJson), "Alp", Data));
}

TEST(QmFinishSummaryData, AxiomUserInfoCanResolveExactName)
{
	constexpr const char *pJson = R"({"code":200,"data":{"user":{"user_id":42,"player":{"name":"Alpha"},"dummy":{"name":null}}}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomSearch(pJson, std::strlen(pJson), "Alpha", Data));
	EXPECT_EQ(Data.m_UserId, 42);
}

TEST(QmFinishSummaryData, AmbiguousAxiomNameAndErrorEnvelopeAreRejected)
{
	constexpr const char *pJson = R"({"code":200,"data":{"items":[{"user":{"user_id":42,"player":{"name":"Alpha"}}},{"user":{"user_id":43,"player":{"name":"Alpha"}}}]}})";
	SRemoteData Data;
	EXPECT_FALSE(ParseAxiomSearch(pJson, std::strlen(pJson), "Alpha", Data));
	constexpr const char *pError = R"({"code":429,"data":{"user":{"user_id":42,"player":{"name":"Alpha"}}}})";
	EXPECT_FALSE(ParseAxiomSearch(pError, std::strlen(pError), "Alpha", Data));
}

TEST(QmFinishSummaryData, AxiomMapNullBestDoesNotProveUnfinished)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","mapper":"Author","difficulty":"Hard","stars":3,"points":8,"player_best_time":null}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomMap(pJson, std::strlen(pJson), "Map A", Data));
	EXPECT_EQ(Data.m_Category, "Hard");
	EXPECT_EQ(Data.m_MapPoints, 8);
	EXPECT_FALSE(Data.m_HistoryKnown);
	EXPECT_FALSE(ParseAxiomMap(pJson, std::strlen(pJson), "Map", Data));
}

TEST(QmFinishSummaryData, AxiomTopValidatesMatchingAccountTimeAndRank)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","items":[{"user":{"user_id":42},"rank":8,"time":93.44}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTop(pJson, std::strlen(pJson), "Map A", 42, Data));
	EXPECT_TRUE(Data.m_AccountHistoryKnown);
	EXPECT_EQ(Data.m_AccountBestTimeMs, 93440);
	EXPECT_EQ(Data.m_Rank, 8);
	constexpr const char *pInvalidTime = R"({"code":200,"data":{"map":"Map A","items":[{"user":{"user_id":42},"rank":8,"time":-1}]}})";
	EXPECT_FALSE(ParseAxiomTop(pInvalidTime, std::strlen(pInvalidTime), "Map A", 42, Data));
	constexpr const char *pInvalidRank = R"({"code":200,"data":{"map":"Map A","items":[{"user":{"user_id":42},"rank":0,"time":93.44}]}})";
	EXPECT_FALSE(ParseAxiomTop(pInvalidRank, std::strlen(pInvalidRank), "Map A", 42, Data));
}

TEST(QmFinishSummaryData, AxiomTopRejectsAnotherModesSameMapAndAccount)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","mode":"gores","items":[{"user":{"user_id":42},"rank":8,"time":93.44}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTop(pJson, std::strlen(pJson), "Map A", 42, Data, "gores"));
	EXPECT_FALSE(ParseAxiomTop(pJson, std::strlen(pJson), "Map A", 42, Data, "axrace"));
	EXPECT_EQ(Data.m_Rank, 8);
}

TEST(QmFinishSummaryData, AxiomMapAllowsOmittedModeButRejectsExplicitMismatch)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","difficulty":"Hard","points":8}})";
	SRemoteData Data;
	EXPECT_TRUE(ParseAxiomMap(pJson, std::strlen(pJson), "Map A", Data, "gores"));
	constexpr const char *pWrongMode = R"({"code":200,"data":{"map":"Map A","mode":"axrace","difficulty":"Hard","points":8}})";
	EXPECT_FALSE(ParseAxiomMap(pWrongMode, std::strlen(pWrongMode), "Map A", Data, "gores"));
}

TEST(QmFinishSummaryData, AxiomTopOutsideRangeDoesNotProveAccountUnfinished)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","items":[{"rank":2,"time":90,"user":{"user_id":42}}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTop(pJson, std::strlen(pJson), "Map A", 42, Data));
	EXPECT_EQ(Data.m_Rank, 2);
	EXPECT_TRUE(Data.m_AccountHistoryKnown);
	ASSERT_TRUE(ParseAxiomTop(pJson, std::strlen(pJson), "Map A", 43, Data));
	EXPECT_FALSE(Data.m_Rank);
	EXPECT_FALSE(Data.m_AccountHistoryKnown);
}

TEST(QmFinishSummaryData, AxiomProgressUsesRequestedCategoryAndRejectsImpossibleTotals)
{
	constexpr const char *pJson = R"({"code":200,"data":{"user":{"user_id":42},"items":[{"difficulty":"Easy","completed_maps":9,"total_maps":10},{"difficulty":"Hard","completed_maps":2,"total_maps":7}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomProgress(pJson, std::strlen(pJson), 42, "Hard", Data));
	EXPECT_EQ(Data.m_CompletedMaps, 2);
	EXPECT_EQ(Data.m_TotalMaps, 7);
	EXPECT_FALSE(ParseAxiomProgress(pJson, std::strlen(pJson), 43, "Hard", Data));
	constexpr const char *pInvalid = R"({"code":200,"data":{"user":{"user_id":42},"items":[{"difficulty":"Hard","completed_maps":8,"total_maps":7}]}})";
	EXPECT_FALSE(ParseAxiomProgress(pInvalid, std::strlen(pInvalid), 42, "Hard", Data));
}

TEST(QmFinishSummaryData, AxiomPointsAcceptsZeroAndRequiresCorrectAccount)
{
	constexpr const char *pJson = R"({"code":200,"data":{"user":{"user_id":42},"points":0}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomPoints(pJson, std::strlen(pJson), 42, Data));
	EXPECT_EQ(Data.m_Points, 0);
	EXPECT_FALSE(ParseAxiomPoints(pJson, std::strlen(pJson), 43, Data));
}

TEST(QmFinishSummaryData, AxiomLatestRecordsMatchAccountAndConvertExplicitTimezone)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","items":[
		{"user":{"user_id":43},"time":80,"recorded_at":"2026-11-16T09:00:00Z"},
		{"user":{"user_id":42},"time":100,"recorded_at":"2026-11-16T16:00:00+08:00"},
		{"user":{"user_id":42},"time":110,"recorded_at":"2026-11-16T07:59:00.123Z"}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTimes(pJson, std::strlen(pJson), "Map A", 42, Data));
	EXPECT_EQ(Data.m_LatestTimeMs, 100000);
	EXPECT_EQ(Data.m_LatestTimestamp, 1794816000);
}

TEST(QmFinishSummaryData, InvalidCalendarDateCannotConfirmAxiomFinish)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","items":[{"user":{"user_id":42},"time":100,"recorded_at":"2026-02-30T08:00:00Z"}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTimes(pJson, std::strlen(pJson), "Map A", 42, Data));
	EXPECT_FALSE(Data.m_LatestTimestamp);
}

TEST(QmFinishSummaryData, FractionalTimestampWithoutTimezoneCannotConfirmAxiomFinish)
{
	constexpr const char *pJson = R"({"code":200,"data":{"map":"Map A","items":[{"user":{"user_id":42},"time":100,"recorded_at":"2026-11-16T08:00:00.123"}]}})";
	SRemoteData Data;
	ASSERT_TRUE(ParseAxiomTimes(pJson, std::strlen(pJson), "Map A", 42, Data));
	EXPECT_FALSE(Data.m_LatestTimestamp);
}

TEST(QmFinishSummaryData, OutOfRangeIntegerTimestampCannotConfirmDdnetFinish)
{
	constexpr const char *pJson = R"({"player":"Alpha","types":{},"last_finishes":[{"map":"Map A","time":100,"timestamp":9223372036854775807}]})";
	SRemoteData Data;
	ASSERT_TRUE(ParseDdnetPlayer(pJson, std::strlen(pJson), "Alpha", "Map A", Data));
	EXPECT_FALSE(Data.m_LatestTimestamp);
}

TEST(QmFinishSummaryComparison, KnownFirstFinishHasNoArtificialDifference)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, std::nullopt, true, 0, true);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::FIRST);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 0);
}

TEST(QmFinishSummaryComparison, ImprovedTimeShowsMagnitudeOfImprovement)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, 112340, true, std::nullopt, true);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::IMPROVED);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 12340);
}

TEST(QmFinishSummaryComparison, SlowerTimeComparesAgainstBest)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, 80000, true, std::nullopt, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::SLOWER);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 20000);
}

TEST(QmFinishSummaryComparison, MatchingBestIsTie)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, 100000, true, std::nullopt, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::TIED);
}

TEST(QmFinishSummaryComparison, ServerDifferenceTakesPrecedenceOverCachedBest)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, 150000, true, -5000, true);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 5000);
}

TEST(QmFinishSummaryComparison, UnknownHistoryCanStillRecognizeServerPersonalRecord)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, std::nullopt, false, std::nullopt, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::UNKNOWN);
	ResolveComparison(Snapshot, std::nullopt, false, 0, true);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::BEST);
}

TEST(QmFinishSummaryComparison, InvalidDifferenceCannotOverflow)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, std::nullopt, false, std::numeric_limits<int64_t>::min(), false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::UNKNOWN);
}

TEST(QmFinishSummaryComparison, CachedBestRoundingDifferenceDoesNotShowZeroRegression)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100020;
	ResolveComparison(Snapshot, 100019, true, std::nullopt, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::TIED);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 0);
}

TEST(QmFinishSummaryComparison, FullCentisecondDifferenceSurvivesTruncatedFinish)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100019;
	ResolveComparison(Snapshot, 100010, true, std::nullopt, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::SLOWER);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 10);
}

TEST(QmFinishSummaryComparison, ServerSubCentisecondImprovementStillShowsPersonalBest)
{
	auto Snapshot = Finish();
	ResolveComparison(Snapshot, std::nullopt, false, -1, true);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::BEST);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 0);
}

TEST(QmFinishSummaryComparison, TruncatedServerDifferenceRetainsObservableCentisecondRegression)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100019;
	ResolveComparison(Snapshot, 100010, true, 9, false);
	EXPECT_EQ(Snapshot.m_Comparison, EComparison::SLOWER);
	EXPECT_EQ(Snapshot.m_DifferenceMs, 10);
}

TEST(QmFinishSummaryLocalRank, TiedPlayersUseCompetitionRankingAndNamesAreDeduplicated)
{
	auto Snapshot = Finish();
	const std::vector<SOnlineTime> vTimes = {{"Alpha", 120000, true}, {"Beta", 90000, true}, {"Beta", 95000, true}, {"Gamma", 90000, true}, {"Delta", 100000, true}, {"Unfinished", std::nullopt, true}};
	ResolveLocalRank(Snapshot, vTimes, 120000, true);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Value, 3);
	EXPECT_EQ(Snapshot.m_LocalRankTotal.m_Value, 4);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Status, EStatus::READY);
}

TEST(QmFinishSummaryLocalRank, SlowerRunUsesExistingPersonalBest)
{
	auto Snapshot = Finish();
	ResolveLocalRank(Snapshot, {{"Beta", 90000, true}}, 80000, true);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Value, 1);
}

TEST(QmFinishSummaryLocalRank, HiddenOrImpreciseScoreKeepsRankingUnavailable)
{
	auto Snapshot = Finish();
	ResolveLocalRank(Snapshot, {{"Beta", std::nullopt, false}}, 80000, true);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Status, EStatus::UNAVAILABLE);
	ResolveLocalRank(Snapshot, {{"Beta", 90000, true}}, 80000, false);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Status, EStatus::UNAVAILABLE);
}

TEST(QmFinishSummaryLocalRank, RoundingDifferenceAtSameCentisecondKeepsTie)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100020;
	ResolveLocalRank(Snapshot, {{"Beta", 100019, true}}, std::nullopt, true);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Value, 1);
	EXPECT_EQ(Snapshot.m_LocalRankTotal.m_Value, 2);
}

TEST(QmFinishSummaryLocalRank, FullCentisecondFasterPlayerRetainsBetterRank)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100019;
	ResolveLocalRank(Snapshot, {{"Beta", 100010, true}}, std::nullopt, true);
	EXPECT_EQ(Snapshot.m_LocalRank.m_Value, 2);
}

TEST(QmFinishSummaryRemote, UnknownFirstFinishKeepsGainPending)
{
	auto Snapshot = Finish();
	SRemoteData After;
	After.m_BestTimeMs = Snapshot.m_TimeMs;
	After.m_HistoryKnown = true;
	After.m_MapPoints = 5;
	After.m_Points = 100;
	ApplyRemote(Snapshot, {}, After, true);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Status, EStatus::PENDING);
}

TEST(QmFinishSummaryRemote, ConfirmedRankPointGainIsRecordedOnceForTheFinish)
{
	SSnapshot Snapshot;
	Snapshot.m_Community = ECommunity::DDNET;
	Snapshot.m_Map = "Map A";
	Snapshot.m_TimeMs = 93000;
	SRemoteData Before, After;
	Before.m_RankPoints = 0;
	Before.m_Rank = 42;
	After.m_Rank = 9;
	After.m_BestTimeMs = 93000;
	After.m_RankPoints = 2;
	ApplyRemote(Snapshot, Before, After, false);
	EXPECT_EQ(Snapshot.m_RankPointsGained, 0);
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_RankPointsGained, 2);
	ApplyRemote(Snapshot, Before, {}, false);
	EXPECT_EQ(Snapshot.m_RankPointsGained, 2);
}

TEST(QmFinishSummaryRemote, OrdinaryPointsOrAMissingRankBaselineDoNotTriggerCelebration)
{
	SSnapshot Snapshot;
	Snapshot.m_Community = ECommunity::DDNET;
	Snapshot.m_TimeMs = 93000;
	SRemoteData Before, After;
	Before.m_Points = 100;
	After.m_Points = 110;
	Before.m_Rank = 10;
	After.m_Rank = 9;
	After.m_BestTimeMs = 93000;
	After.m_RankPoints = 12;
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_RankPointsGained, 0);
	for(const int Previous : {12, 15})
	{
		Before.m_RankPoints = Previous;
		ApplyRemote(Snapshot, Before, After, true);
		EXPECT_EQ(Snapshot.m_RankPointsGained, 0);
	}
	Before.m_RankPoints = 2;
	After.m_BestTimeMs = 80000;
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_RankPointsGained, 0);
}

TEST(QmFinishSummaryRemote, ConfirmedFirstFinishAwardsOnlyThisMapsPoints)
{
	auto Snapshot = Finish();
	SRemoteData Before;
	Before.m_HistoryKnown = true;
	Before.m_MapPoints = 5;
	Before.m_Points = 100;
	Before.m_CompletedMaps = 10;
	SRemoteData After = Before;
	After.m_BestTimeMs = Snapshot.m_TimeMs;
	After.m_Points = 112;
	After.m_CompletedMaps = 12;
	ApplyRemote(Snapshot, Before, After, false);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Status, EStatus::PENDING);
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Value, 5);
	EXPECT_EQ(Snapshot.m_Points.m_Value, 112);
	EXPECT_EQ(Snapshot.m_Points.m_Status, EStatus::READY);
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Value, 5);
}

TEST(QmFinishSummaryRemote, AxiomPreviouslyCompletedAccountDoesNotAwardRoleFirstFinish)
{
	auto Snapshot = Finish(ECommunity::AXIOM_GORES);
	SRemoteData Before;
	Before.m_HistoryKnown = true;
	Before.m_AccountHistoryKnown = true;
	Before.m_AccountBestTimeMs = 110000;
	Before.m_MapPoints = 5;
	ApplyRemote(Snapshot, Before, {}, false);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Value, 0);
	EXPECT_EQ(Snapshot.m_PointsGained.m_Status, EStatus::READY);
}

TEST(QmFinishSummaryRemote, OldRankAndTotalsRemainMarkedStale)
{
	auto Snapshot = Finish();
	SRemoteData Before;
	Before.m_HistoryKnown = true;
	Before.m_BestTimeMs = 110000;
	Before.m_Rank = 20;
	Before.m_Points = 100;
	ApplyRemote(Snapshot, Before, {}, false);
	EXPECT_EQ(Snapshot.m_Rank.m_Value, 20);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::STALE);
	EXPECT_EQ(Snapshot.m_Points.m_Status, EStatus::STALE);
	SRemoteData After = Before;
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::STALE);
	After.m_BestTimeMs = Snapshot.m_TimeMs;
	After.m_Rank = 18;
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_Rank.m_Value, 18);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::READY);
}

TEST(QmFinishSummaryRemote, ConfirmedFinishAcceptsRoundedRankTimeInSameCentisecond)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100019;
	SRemoteData After;
	After.m_BestTimeMs = 100020;
	After.m_Rank = 20;
	After.m_ChinaBestTimeMs = 100020;
	After.m_ChinaRank = 5;
	ApplyRemote(Snapshot, {}, After, true);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::READY);
	EXPECT_EQ(Snapshot.m_ChinaRank.m_Status, EStatus::READY);
}

TEST(QmFinishSummaryRemote, UnconfirmedRoundedRankRemainsStale)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100019;
	SRemoteData After;
	After.m_BestTimeMs = 100020;
	After.m_Rank = 20;
	After.m_ChinaBestTimeMs = 100020;
	After.m_ChinaRank = 5;
	ApplyRemote(Snapshot, {}, After, false);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::STALE);
	EXPECT_EQ(Snapshot.m_ChinaRank.m_Status, EStatus::STALE);
}

TEST(QmFinishSummaryRemote, FullCentisecondSlowerRankRemainsStaleAfterFinishConfirmation)
{
	auto Snapshot = Finish();
	Snapshot.m_TimeMs = 100020;
	SRemoteData After;
	After.m_BestTimeMs = 100030;
	After.m_Rank = 20;
	After.m_ChinaBestTimeMs = 100030;
	After.m_ChinaRank = 5;
	ApplyRemote(Snapshot, {}, After, true);
	EXPECT_EQ(Snapshot.m_Rank.m_Status, EStatus::STALE);
	EXPECT_EQ(Snapshot.m_ChinaRank.m_Status, EStatus::STALE);
}

TEST(QmFinishSummaryRemote, ChinaTopAbsenceClearsPreviousRank)
{
	auto Snapshot = Finish();
	SRemoteData Before;
	Before.m_ChinaRank = 20;
	SRemoteData After;
	After.m_ChinaRankKnown = true;
	ApplyRemote(Snapshot, Before, After, false);
	EXPECT_EQ(Snapshot.m_ChinaRank.m_Status, EStatus::UNAVAILABLE);
	EXPECT_EQ(Snapshot.m_ChinaRank.m_Value, -1);
}

TEST(QmFinishSummaryRemote, CurrentMapMismatchCannotOverwriteSnapshot)
{
	auto Snapshot = Finish();
	Snapshot.m_Mapper = "Original";
	SRemoteData Other;
	Other.m_Map = "Map B";
	Other.m_Mapper = "Wrong";
	ApplyRemote(Snapshot, {}, Other, true);
	EXPECT_EQ(Snapshot.m_Mapper, "Original");
}
TEST(QmFinishSummaryRemote, DelayedAggregateDoesNotBecomeCurrentWithOnlyRecentFinish)
{
	auto Snapshot = Finish();
	SRemoteData Before;
	Before.m_HistoryKnown = true;
	Before.m_MapPoints = 5;
	Before.m_Points = 100;
	Before.m_CompletedMaps = 10;
	SRemoteData After = Before;
	After.m_BestTimeMs = Snapshot.m_TimeMs;
	ApplyRemote(Snapshot, Before, After, true);
	EXPECT_EQ(Snapshot.m_Points.m_Status, EStatus::STALE);
	EXPECT_EQ(Snapshot.m_CompletedMaps.m_Status, EStatus::STALE);
}
