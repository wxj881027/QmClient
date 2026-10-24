#include <game/client/components/qmclient/friend_last_seen_data.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	constexpr int64_t NOW = 1792800000;
}

TEST(QmFriendLastSeenParser, UsesLatestDateAcrossSkinAndClanRecords)
{
	const auto Date = QmParseDdStatsLastSeen(R"({"recent_player_info":[
		{"name":"Alice","clan":"Old","last_seen":"2026-10-20"},
		{"name":"Alice","clan":"New","last_seen":"2026-10-23"},
		{"name":"Alice","last_seen":"2026-10-21"}]})",
		"Alice");
	ASSERT_TRUE(Date.has_value());
	EXPECT_EQ(*Date, "2026-10-23");
}

TEST(QmFriendLastSeenParser, EmptyHistoryHasNoDate)
{
	const auto Date = QmParseDdStatsLastSeen(R"({"recent_player_info":[]})", "Alice");
	ASSERT_TRUE(Date.has_value());
	EXPECT_TRUE(Date->empty());
}

TEST(QmFriendLastSeenParser, FinishTimestampDoesNotStandInForOnlineDate)
{
	EXPECT_FALSE(QmParseDdStatsLastSeen(R"({"recent_finishes":[{"timestamp":"2026-10-23T12:30:00"}]})", "Alice"));
}

TEST(QmFriendLastSeenParser, RejectsHtmlAndUnexpectedHistoryShape)
{
	for(const char *pBody : {"<html>Access denied</html>", "null", "[]", R"({"recent_player_info":{}})", R"({"recent_player_info":[null]})"})
	{
		SCOPED_TRACE(pBody);
		EXPECT_FALSE(QmParseDdStatsLastSeen(pBody, "Alice"));
	}
}

TEST(QmFriendLastSeenParser, RejectsAnotherPlayerAndEmbeddedNameSuffix)
{
	EXPECT_FALSE(QmParseDdStatsLastSeen(R"({"recent_player_info":[{"name":"Bob","last_seen":"2026-10-23"}]})", "Alice"));
	EXPECT_FALSE(QmParseDdStatsLastSeen(R"({"recent_player_info":[{"name":"Alice\u0000Bob","last_seen":"2026-10-23"}]})", "Alice"));
}

TEST(QmFriendLastSeenParser, ValidatesCalendarDatesIncludingLeapYears)
{
	struct SCase
	{
		const char *m_pDate;
		bool m_Valid;
	};
	const SCase aCases[] = {
		{"2024-02-29", true},
		{"2000-02-29", true},
		{"2026-02-29", false},
		{"2100-02-29", false},
		{"2026-04-31", false},
		{"2026-00-01", false},
		{"2026-12-00", false},
		{"2026-1-23", false},
		{"2026-10-23T12:30:00", false},
	};
	for(const SCase &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pDate);
		const std::string Body = std::string(R"({"recent_player_info":[{"name":"Alice","last_seen":")") + Case.m_pDate + R"("}]})";
		EXPECT_EQ(QmParseDdStatsLastSeen(Body, "Alice").has_value(), Case.m_Valid);
	}
}

TEST(QmFriendLastSeenCache, RepeatedVisibleLookupsShareOneRequestAndCacheForADay)
{
	CQmFriendLastSeenCache Cache;
	const auto *pEntry = Cache.Get("Alice", NOW);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(Cache.Get("Alice", NOW), pEntry);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	EXPECT_FALSE(Cache.NextRequest(NOW + 3));
	Cache.Get("Alice", NOW + 3);
	Cache.Complete(Request->m_Id, "2026-10-23", NOW + 3);
	EXPECT_EQ(pEntry->m_Date, "2026-10-23");
	EXPECT_EQ(pEntry->m_Status, EQmFriendLastSeenStatus::READY);
	Cache.Get("Alice", NOW + CQmFriendLastSeenCache::CACHE_SECONDS + 2);
	EXPECT_FALSE(Cache.NextRequest(NOW + CQmFriendLastSeenCache::CACHE_SECONDS + 2));
	Cache.Get("Alice", NOW + CQmFriendLastSeenCache::CACHE_SECONDS + 3);
	EXPECT_TRUE(Cache.NextRequest(NOW + CQmFriendLastSeenCache::CACHE_SECONDS + 3));
}

TEST(QmFriendLastSeenCache, MissingHistoryIsAlsoCachedForADay)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, std::string{}, NOW);
	EXPECT_EQ(Cache.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
	Cache.Get("Alice", NOW + 300);
	EXPECT_FALSE(Cache.NextRequest(NOW + 300));
}

TEST(QmFriendLastSeenCache, FailedRefreshPreservesDateAndWaitsBeforeRetrying)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, "2026-10-23", NOW);
	const int64_t Expired = NOW + CQmFriendLastSeenCache::CACHE_SECONDS;
	Cache.Get("Alice", Expired);
	Request = Cache.NextRequest(Expired);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, std::nullopt, Expired);
	EXPECT_EQ(Cache.Find("Alice")->m_Date, "2026-10-23");
	Cache.Get("Alice", Expired + CQmFriendLastSeenCache::RETRY_SECONDS - 1);
	EXPECT_FALSE(Cache.NextRequest(Expired + CQmFriendLastSeenCache::RETRY_SECONDS - 1));
	Cache.Get("Alice", Expired + CQmFriendLastSeenCache::RETRY_SECONDS);
	EXPECT_TRUE(Cache.NextRequest(Expired + CQmFriendLastSeenCache::RETRY_SECONDS));
}

TEST(QmFriendLastSeenCache, FailureWithoutHistoryIsUnavailableRatherThanNoData)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, std::nullopt, NOW);
	EXPECT_EQ(Cache.Find("Alice")->m_Status, EQmFriendLastSeenStatus::ERROR);
	EXPECT_EQ(Cache.Find("Alice")->m_CheckedAt, 0);
}

TEST(QmFriendLastSeenCache, SerialRequestsRespectMinimumSpacing)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	Cache.Get("Bob", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, "2026-10-23", NOW);
	EXPECT_FALSE(Cache.NextRequest(NOW + 1));
	const auto Next = Cache.NextRequest(NOW + 2);
	ASSERT_TRUE(Next);
	EXPECT_EQ(Next->m_Name, "Bob");
}

TEST(QmFriendLastSeenCache, RetryAfterPausesRequestsForOtherFriends)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, std::nullopt, NOW, 300);
	Cache.Get("Bob", NOW + 299);
	EXPECT_FALSE(Cache.NextRequest(NOW + 299));
	Cache.Get("Bob", NOW + 300);
	EXPECT_TRUE(Cache.NextRequest(NOW + 300));
}

TEST(QmFriendLastSeenCache, ScrolledAwayEntriesWaitUntilVisibleAgain)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	EXPECT_FALSE(Cache.NextRequest(NOW + 3));
	Cache.Get("Alice", NOW + 10);
	EXPECT_TRUE(Cache.NextRequest(NOW + 10));
}

TEST(QmFriendLastSeenCache, CancelledRequestCannotOverwriteNewRequestAfterReenable)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	const auto Old = Cache.NextRequest(NOW);
	ASSERT_TRUE(Old);
	Cache.CancelRequests();
	Cache.Get("Alice", NOW + 2);
	const auto Current = Cache.NextRequest(NOW + 2);
	ASSERT_TRUE(Current);
	Cache.Complete(Old->m_Id, "2026-10-20", NOW + 2);
	EXPECT_TRUE(Cache.IsCurrent(Current->m_Id));
	EXPECT_TRUE(Cache.Find("Alice")->m_Date.empty());
	Cache.Complete(Current->m_Id, "2026-10-23", NOW + 2);
	EXPECT_EQ(Cache.Find("Alice")->m_Date, "2026-10-23");
}

TEST(QmFriendLastSeenCache, RemovingFriendDiscardsActiveAndQueuedRequests)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	Cache.Get("Bob", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.RetainNames({});
	Cache.Complete(Request->m_Id, "2026-10-23", NOW);
	EXPECT_EQ(Cache.Find("Alice"), nullptr);
	EXPECT_EQ(Cache.Find("Bob"), nullptr);
	EXPECT_FALSE(Cache.NextRequest(NOW + 2));
}

TEST(QmFriendLastSeenCache, PersistenceRestoresUnicodeNamesDatesAndNoDataWithoutRequests)
{
	CQmFriendLastSeenCache Cache;
	int64_t Now = NOW;
	for(const char *pName : {"好友\\\"", "Bob"})
	{
		Cache.Get(pName, Now);
		const auto Request = Cache.NextRequest(Now);
		ASSERT_TRUE(Request);
		Cache.Complete(Request->m_Id, std::string(pName) == "Bob" ? "" : "2026-10-23", Now);
		Now += 2;
	}
	CQmFriendLastSeenCache Loaded;
	ASSERT_TRUE(Loaded.Load(Cache.Serialize(), Now));
	EXPECT_EQ(Loaded.Get("好友\\\"", Now)->m_Date, "2026-10-23");
	EXPECT_EQ(Loaded.Get("Bob", Now)->m_Status, EQmFriendLastSeenStatus::NO_DATA);
	EXPECT_FALSE(Loaded.NextRequest(Now));
	EXPECT_FALSE(Loaded.Dirty());
}

TEST(QmFriendLastSeenCache, InvalidCacheDoesNotReplaceExistingHistory)
{
	CQmFriendLastSeenCache Cache;
	Cache.Get("Alice", NOW);
	const auto Request = Cache.NextRequest(NOW);
	ASSERT_TRUE(Request);
	Cache.Complete(Request->m_Id, "2026-10-23", NOW);
	for(const char *pBody : {"<html>error</html>", R"({"version":"1","players":[]})", R"({"version":1,"players":[{"name":"Bob","date":"2026-02-30","checked_at":"100"}]})", R"({"version":1,"players":[{"name":"Bob","date":"2026-10-23","checked_at":"9999999999"}]})"})
	{
		SCOPED_TRACE(pBody);
		EXPECT_FALSE(Cache.Load(pBody, NOW));
		EXPECT_EQ(Cache.Find("Alice")->m_Date, "2026-10-23");
	}
}
