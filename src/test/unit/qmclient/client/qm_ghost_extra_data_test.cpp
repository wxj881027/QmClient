#include <game/client/components/qmclient/ghost_extra_data.h>

#include <gtest/gtest.h>

TEST(QmGhostExtraData, LegacyDirectionsRemainReadable)
{
	EXPECT_EQ(QmDecodeGhostDirection(-1), -1);
	EXPECT_EQ(QmDecodeGhostDirection(0), 0);
	EXPECT_EQ(QmDecodeGhostDirection(1), 1);
	EXPECT_EQ(QmDecodeGhostStrongWeak(-1), -1);
	EXPECT_EQ(QmDecodeGhostStrongWeak(0), -1);
	EXPECT_EQ(QmDecodeGhostStrongWeak(1), -1);
}

TEST(QmGhostExtraData, DirectionAndStrongWeakRoundTrip)
{
	for(const int Direction : {-1, 0, 1})
	{
		for(const int StrongWeakId : {0, 7, 63})
		{
			const int Packed = QmEncodeGhostDirection(Direction, StrongWeakId);
			EXPECT_EQ(QmDecodeGhostDirection(Packed), Direction);
			EXPECT_EQ(QmDecodeGhostStrongWeak(Packed), StrongWeakId);
		}
	}
}

TEST(QmGhostExtraData, DirectionPackMasksOutOfRangeValues)
{
	const int Packed = QmEncodeGhostDirection(9, 999);
	EXPECT_EQ(QmDecodeGhostDirection(Packed), 1);
	EXPECT_EQ(QmDecodeGhostStrongWeak(Packed), 254);
}
