#include <game/client/components/qmclient/media_volume_logic.h>
#include <gtest/gtest.h>

#include <limits>

TEST(QmMediaVolume, MatchesPackagedAndExecutablePlayerIdentitiesExactly)
{
	EXPECT_TRUE(QmMediaVolume::MatchesPlayer("Player.Package!App", "Player.Package!App", ""));
	EXPECT_TRUE(QmMediaVolume::MatchesPlayer("Music.EXE", "", "C:\\Apps\\music.exe"));
	EXPECT_FALSE(QmMediaVolume::MatchesPlayer("music", "", "C:\\Apps\\music.exe"));
	EXPECT_FALSE(QmMediaVolume::MatchesPlayer("music.exe", "", "C:\\Apps\\othermusic.exe"));
	EXPECT_FALSE(QmMediaVolume::MatchesPlayer("", "", ""));
}

TEST(QmMediaVolume, SliderUpdatesCoalesceAndClamp)
{
	QmMediaVolume::CPendingVolume Pending;
	Pending.Set(4, 0.2f);
	Pending.Set(4, 1.5f);
	const auto Request = Pending.Take(4);
	ASSERT_TRUE(Request);
	EXPECT_FLOAT_EQ(Request->m_Level, 1.0f);
	EXPECT_FALSE(Pending.Take(4));
	Pending.Set(4, -1.0f);
	EXPECT_FLOAT_EQ(Pending.Take(4)->m_Level, 0.0f);
}

TEST(QmMediaVolume, PlayerSwitchDropsOldVolumeRequest)
{
	QmMediaVolume::CPendingVolume Pending;
	Pending.Set(4, 0.9f);
	EXPECT_FALSE(Pending.Take(5));
	EXPECT_FALSE(Pending.Take(4));
	Pending.Set(5, 0.3f);
	Pending.Reset();
	EXPECT_FALSE(Pending.Take(5));
}

TEST(QmMediaVolume, InvalidVolumeDoesNotReplacePendingValue)
{
	QmMediaVolume::CPendingVolume Pending;
	Pending.Set(4, 0.4f);
	Pending.Set(4, std::numeric_limits<float>::quiet_NaN());
	Pending.Set(0, 0.8f);
	EXPECT_FLOAT_EQ(Pending.Take(4)->m_Level, 0.4f);
}
