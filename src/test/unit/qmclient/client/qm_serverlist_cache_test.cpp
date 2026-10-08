#include <engine/client/qm_serverlist_cache.h>

#include <gtest/gtest.h>

TEST(QmServerListCache, FailedAndEmptyRefreshesKeepPreviousServersAndAge)
{
	CQmServerListCache<int> Cache;
	ASSERT_TRUE(Cache.Publish({1, 2}, 10, 100));
	Cache.RefreshFailed();
	EXPECT_EQ(Cache.Servers(), (std::vector<int>{1, 2}));
	EXPECT_TRUE(Cache.HasRefreshFailed());
	EXPECT_FALSE(Cache.IsStale(390));
	EXPECT_FALSE(Cache.Publish({}, 0, 390));
	EXPECT_TRUE(Cache.IsStale(391));
	EXPECT_EQ(Cache.Servers(), (std::vector<int>{1, 2}));
}

TEST(QmServerListCache, SuccessfulRefreshReplacesOldListAndClearsFailure)
{
	CQmServerListCache<int> Cache;
	Cache.Publish({1, 2}, 301, 100);
	Cache.RefreshFailed();
	ASSERT_TRUE(Cache.Publish({2, 3}, 0, 500));
	EXPECT_EQ(Cache.Servers(), (std::vector<int>{2, 3}));
	EXPECT_FALSE(Cache.HasRefreshFailed());
	EXPECT_FALSE(Cache.IsStale(500));
	EXPECT_TRUE(Cache.IsStale(801));
}

TEST(QmServerListCache, EmptyInitialCacheDoesNotClaimToBeAnOldList)
{
	CQmServerListCache<int> Cache;
	Cache.RefreshFailed();
	EXPECT_FALSE(Cache.IsStale(1000));
	EXPECT_TRUE(Cache.HasRefreshFailed());
	EXPECT_TRUE(Cache.Servers().empty());
}
