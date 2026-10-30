#include <engine/client/map_download_file.h>

#include <gtest/gtest.h>

#include <limits>

TEST(MapDownloadProgress, CompleteOrPartialChunkFitsRemainingBytes)
{
	EXPECT_TRUE(map_download::CanAppend(0, 4, 10));
	EXPECT_TRUE(map_download::CanAppend(4, 6, 10));
}

TEST(MapDownloadProgress, InvalidOrExhaustedProgressRejectsAnotherChunk)
{
	EXPECT_FALSE(map_download::CanAppend(-1, 1, 10));
	EXPECT_FALSE(map_download::CanAppend(0, 1, -1));
	EXPECT_FALSE(map_download::CanAppend(11, 1, 10));
	EXPECT_FALSE(map_download::CanAppend(10, 1, 10));
}

TEST(MapDownloadProgress, EmptyNegativeOrExcessChunkIsRejected)
{
	EXPECT_FALSE(map_download::CanAppend(4, 0, 10));
	EXPECT_FALSE(map_download::CanAppend(4, -1, 10));
	EXPECT_FALSE(map_download::CanAppend(4, 7, 10));
}

TEST(MapDownloadProgress, LargeValuesUseRemainingBytesWithoutOverflowingSum)
{
	constexpr int Max = std::numeric_limits<int>::max();
	EXPECT_TRUE(map_download::CanAppend(Max - 1, 1, Max));
	EXPECT_FALSE(map_download::CanAppend(Max - 1, 2, Max));
	EXPECT_FALSE(map_download::CanAppend(Max, Max, Max));
}
