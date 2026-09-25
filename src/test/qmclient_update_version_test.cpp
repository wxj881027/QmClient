// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/update_version.h>

#include <gtest/gtest.h>

TEST(QmClientUpdateVersion, TreatsHigherRemoteVersionAsUpdate)
{
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("2.62.6", "2.62.5"));
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("v2.62.6", "2.62.5"));
}

TEST(QmClientUpdateVersion, DoesNotTreatEqualOrLowerRemoteVersionAsUpdate)
{
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("2.62.5", "2.62.5"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("2.62.4", "2.62.5"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("v2.62.4 ", " 2.62.5"));
}

TEST(QmClientUpdateVersion, SupportsCompactAndFourPartVersions)
{
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3", "3.0"));
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("v3.1", "3"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("2.62", "2.62.0"));
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("2.62.5.1", "2.62.5.0"));
}

TEST(QmClientUpdateVersion, DevelopmentBuildCanUpdateToMatchingStableBaseline)
{
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("v3", "3", true));
	EXPECT_TRUE(IsQmClientRemoteVersionNewer("3.1", "3", true));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3", "3", false));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3", "3.1", true));
}

TEST(QmClientUpdateVersion, IgnoresInvalidOrOverflowingRemoteVersions)
{
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("2.62-beta", "2.62.4"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3.0.0-rc1", "2.62.4"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3.0.", "2.62.4"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("999999999999", "2.62.4"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer("3.999999999999.0", "2.62.4"));
	EXPECT_FALSE(IsQmClientRemoteVersionNewer(nullptr, "2.62.4"));
}
