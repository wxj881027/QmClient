// 缩略图并行与常驻上限的调度策略：只测纯决策函数，不依赖图形设备。
#include <game/client/components/qmclient/screenshot_image_job.h>

#include <gtest/gtest.h>

TEST(QmScreenshotImagePolicy, StartsJobsOnlyForVisibleRequests)
{
	EXPECT_EQ(QmScreenshotImageJobsToStart(0, 0, 4), 0);
	EXPECT_EQ(QmScreenshotImageJobsToStart(0, 6, 4), 4);
	EXPECT_EQ(QmScreenshotImageJobsToStart(3, 6, 4), 1);
	EXPECT_EQ(QmScreenshotImageJobsToStart(4, 6, 4), 0);
	EXPECT_EQ(QmScreenshotImageJobsToStart(2, 0, 4), 0);
}

TEST(QmScreenshotImagePolicy, IgnoresInvalidConcurrencyInput)
{
	EXPECT_EQ(QmScreenshotImageJobsToStart(-1, 3, 4), 0);
	EXPECT_EQ(QmScreenshotImageJobsToStart(0, 3, 0), 0);
	EXPECT_EQ(QmScreenshotImageJobsToStart(0, -3, 4), 0);
}

TEST(QmScreenshotImagePolicy, EvictsOnlyEntriesAboveResidentCap)
{
	EXPECT_EQ(QmScreenshotImageEvictionCount(0, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT), 0);
	EXPECT_EQ(QmScreenshotImageEvictionCount(10, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT), 0);
	EXPECT_EQ(QmScreenshotImageEvictionCount(QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT), 0);
	EXPECT_EQ(QmScreenshotImageEvictionCount(QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT + 1, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT), 1);
	EXPECT_EQ(QmScreenshotImageEvictionCount(QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT + 7, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT), 7);
	EXPECT_EQ(QmScreenshotImageEvictionCount(3, 0), 3);
	EXPECT_EQ(QmScreenshotImageEvictionCount(-1, 64), 0);
}
