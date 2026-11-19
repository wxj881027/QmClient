#include <engine/shared/protocol.h>

#include <game/client/components/infomessages.h>

#include <gtest/gtest.h>

#include <limits>

TEST(InfoMessageLifetime, ExpiresOnlyAfterTenSeconds)
{
	EXPECT_FALSE(InfoMessageExpired(1000, 500, SERVER_TICK_SPEED));
	EXPECT_TRUE(InfoMessageExpired(1001, 500, SERVER_TICK_SPEED));
}

TEST(InfoMessageLifetime, RecentMessagesNearTheTickLimitStayVisible)
{
	constexpr int LAST_TICK = std::numeric_limits<int>::max();
	EXPECT_FALSE(InfoMessageExpired(LAST_TICK, LAST_TICK - 300, SERVER_TICK_SPEED));
	EXPECT_TRUE(InfoMessageExpired(LAST_TICK, LAST_TICK - 501, SERVER_TICK_SPEED));
}
