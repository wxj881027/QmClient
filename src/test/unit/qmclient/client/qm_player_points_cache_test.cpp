#include <game/client/components/qmclient/player_points_state.h>

#include <gtest/gtest.h>

TEST(QmPlayerPointsCache, PersistedPointsStayVisibleDuringInitialRefresh)
{
	CQmPlayerPointsCache Cache;
	Cache.Load("player", 42);
	EXPECT_TRUE(Cache.ShouldQuery("player", 1000, 1000));
	Cache.BeginRequest("player");
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(Cache.Get("player").m_Points, 42);
	EXPECT_FALSE(Cache.ShouldQuery("player", 1001, 1000));
}

TEST(QmPlayerPointsCache, NewServerRefreshesFreshPointsWithoutHidingThem)
{
	CQmPlayerPointsCache Cache;
	const auto Token = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteSuccess("player", Token, 42, 1000));
	EXPECT_FALSE(Cache.ShouldQuery("player", 1001, 1000));
	Cache.BeginServerSession();
	EXPECT_TRUE(Cache.ShouldQuery("player", 1001, 1000));
	Cache.BeginRequest("player");
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(Cache.Get("player").m_Points, 42);
}

TEST(QmPlayerPointsCache, FailedRefreshKeepsLastSuccessAndRetriesAfterBackoff)
{
	CQmPlayerPointsCache Cache;
	Cache.Load("player", 42);
	const auto Failed = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteFailure("player", Failed, 1000));
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::READY);
	EXPECT_EQ(Cache.Get("player").m_Points, 42);
	EXPECT_FALSE(Cache.ShouldQuery("player", 30999, 1000));
	EXPECT_TRUE(Cache.ShouldQuery("player", 31000, 1000));
	const auto Retry = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteSuccess("player", Retry, 53, 31000));
	EXPECT_EQ(Cache.Get("player").m_Points, 53);
	EXPECT_FALSE(Cache.ShouldQuery("player", 31001, 1000));
}

TEST(QmPlayerPointsCache, NewServerClearsPreviousFailureBackoff)
{
	CQmPlayerPointsCache Cache;
	const auto Token = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteFailure("player", Token, 1000));
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::FAILED);
	EXPECT_FALSE(Cache.ShouldQuery("player", 1001, 1000));
	Cache.BeginServerSession();
	EXPECT_TRUE(Cache.ShouldQuery("player", 1001, 1000));
}

TEST(QmPlayerPointsCache, PreviousServerCompletionCannotOverwriteNewRequest)
{
	CQmPlayerPointsCache Cache;
	Cache.Load("player", 42);
	const auto Old = Cache.BeginRequest("player");
	Cache.BeginServerSession();
	const auto Current = Cache.BeginRequest("player");
	EXPECT_FALSE(Cache.CompleteSuccess("player", Old, 99, 1000));
	EXPECT_FALSE(Cache.CompleteFailure("player", Old, 1001));
	EXPECT_EQ(Cache.Get("player").m_Points, 42);
	EXPECT_FALSE(Cache.ShouldQuery("player", 1002, 1000));
	ASSERT_TRUE(Cache.CompleteSuccess("player", Current, 53, 1002));
	EXPECT_EQ(Cache.Get("player").m_Points, 53);
}

TEST(QmPlayerPointsCache, CancelledInitialRequestCanRestartWithoutBackoff)
{
	CQmPlayerPointsCache Cache;
	const auto Old = Cache.BeginRequest("player");
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::FETCHING);
	Cache.CancelPendingRequests();
	EXPECT_EQ(Cache.Get("player").m_Status, EPointsStatus::NOT_REQUESTED);
	EXPECT_TRUE(Cache.ShouldQuery("player", 1000, 1000));
	const auto Current = Cache.BeginRequest("player");
	EXPECT_FALSE(Cache.CompleteSuccess("player", Old, 99, 1001));
	EXPECT_TRUE(Cache.CompleteSuccess("player", Current, 53, 1002));
}

TEST(QmPlayerPointsCache, CompletedRequestCannotPublishTwice)
{
	CQmPlayerPointsCache Cache;
	const auto Token = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteSuccess("player", Token, 42, 1000));
	EXPECT_FALSE(Cache.CompleteSuccess("player", Token, 99, 1001));
	EXPECT_FALSE(Cache.CompleteFailure("player", Token, 1001));
	EXPECT_EQ(Cache.Get("player").m_Points, 42);
}

TEST(QmPlayerPointsCache, RefreshStartsAtCacheExpiry)
{
	CQmPlayerPointsCache Cache;
	const auto Token = Cache.BeginRequest("player");
	ASSERT_TRUE(Cache.CompleteSuccess("player", Token, 42, 1000));
	EXPECT_FALSE(Cache.ShouldQuery("player", 7200999, 1000));
	EXPECT_TRUE(Cache.ShouldQuery("player", 7201000, 1000));
}
