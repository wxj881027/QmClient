#include <game/client/components/qmclient/music_lyrics/qm_spotify_progress.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
	constexpr int64_t Maximum = std::numeric_limits<int64_t>::max();
}

TEST(QmSpotifyProgress, ValidRateAdvancesFromTheObservedPosition)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 2500, 1000, 1.5), 4250);
}

TEST(QmSpotifyProgress, SubmillisecondElapsedTimeKeepsTheExistingTruncation)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 1001, 3000, 1.0), 2000);
}

TEST(QmSpotifyProgress, MissingAndFutureObservationsDoNotExtrapolate)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 0, 1000, 1000, 1.0), 2000);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, -1, 1000, 1000, 1.0), 2000);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 2500, 1000, 1000, 1.0), 2000);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 1000, 1000, 1.0), 2000);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1, std::numeric_limits<int64_t>::min(), 1000, 1.0), 2000);
}

TEST(QmSpotifyProgress, NonpositiveFrequencyKeepsTheObservedPosition)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 2000, 0, 1.0), 2000);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 2000, -1, 1.0), 2000);
}

TEST(QmSpotifyProgress, InvalidRatesUseNormalSpeed)
{
	const double aRates[] = {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
	for(const double Rate : aRates)
	{
		SCOPED_TRACE(Rate);
		EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 2000, 1000, Rate), 3000);
	}
}

TEST(QmSpotifyProgress, MaximumFiniteExternalRateSaturatesWithoutIntegerConversion)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(2000, 1000, 2000, 1000, std::numeric_limits<double>::max()), Maximum);
}

TEST(QmSpotifyProgress, RoundedSignedUpperBoundSaturatesBeforeConversion)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(0, 1000, 1001, 1000, std::ldexp(1.0, 63)), Maximum);
}

TEST(QmSpotifyProgress, LargestDoubleBelowTheSignedUpperBoundIsKept)
{
	const double Rate = std::nextafter(std::ldexp(1.0, 63), 0.0);
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(0, 1000, 1001, 1000, Rate), Maximum - 1023);
}

TEST(QmSpotifyProgress, PositionAdditionSaturatesInsteadOfWrapping)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(Maximum - 10, 1000, 1020, 1000, 1.0), Maximum);
}

TEST(QmSpotifyProgress, ExactFinalRepresentablePositionIsKept)
{
	EXPECT_EQ(QmSpotify::ExtrapolatePositionMs(Maximum - 10, 1000, 1010, 1000, 1.0), Maximum);
}
