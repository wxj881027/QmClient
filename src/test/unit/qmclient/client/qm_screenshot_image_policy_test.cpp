// 缩略图调度与请求入队策略：通过生产接口检查边界和失败恢复，不依赖图形设备。
#include <game/client/components/qmclient/screenshot_image_job.h>

#include <gtest/gtest.h>

#include <memory_resource>
#include <new>
#include <string>
#include <vector>

namespace
{
	class CRequestMemory : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Reject)
				throw std::bad_alloc();
			return std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
		}
		void do_deallocate(void *pData, size_t Bytes, size_t Alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		bool m_Reject = false;
	};
}

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

TEST(QmScreenshotImagePolicy, QueuesRequestAndPublishesFlag)
{
	std::vector<std::string> Requests;
	bool Requested = false;
	ASSERT_TRUE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("image.png")));
	ASSERT_EQ(Requests.size(), 1u);
	EXPECT_EQ(Requests.front(), "image.png");
	EXPECT_TRUE(Requested);
}

TEST(QmScreenshotImagePolicy, AlreadyQueuedRequestDoesNotAllocateOrDuplicate)
{
	CRequestMemory Memory;
	std::pmr::vector<std::string> Requests(&Memory);
	bool Requested = false;
	ASSERT_TRUE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("image.png")));
	Memory.m_Reject = true;
	EXPECT_TRUE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("image.png")));
	ASSERT_EQ(Requests.size(), 1u);
	EXPECT_EQ(Requests.front(), "image.png");
}

TEST(QmScreenshotImagePolicy, FailedRequestKeepsFlagClearAndCanRetry)
{
	CRequestMemory Memory;
	std::pmr::vector<std::string> Requests(&Memory);
	bool Requested = false;
	Memory.m_Reject = true;
	EXPECT_FALSE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("image.png")));
	EXPECT_FALSE(Requested);
	EXPECT_TRUE(Requests.empty());

	Memory.m_Reject = false;
	ASSERT_TRUE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("image.png")));
	EXPECT_TRUE(Requested);
	ASSERT_EQ(Requests.size(), 1u);
	EXPECT_EQ(Requests.front(), "image.png");
}

TEST(QmScreenshotImagePolicy, FailedGrowthPreservesOtherQueuedRequests)
{
	CRequestMemory Memory;
	std::pmr::vector<std::string> Requests(&Memory);
	Requests.emplace_back("other.png");
	while(Requests.size() < Requests.capacity())
		Requests.emplace_back("other.png");
	const auto Before = std::vector<std::string>(Requests.begin(), Requests.end());
	Memory.m_Reject = true;
	bool Requested = false;
	EXPECT_FALSE(QmTryQueueScreenshotThumbnailRequest(Requests, Requested, std::string("new.png")));
	EXPECT_FALSE(Requested);
	EXPECT_EQ(std::vector<std::string>(Requests.begin(), Requests.end()), Before);
}
