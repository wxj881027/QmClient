#include <engine/client/video_encoder_probe_cache.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>
#include <test/support/video_encoder_probe.h>

#include <chrono>
#include <future>
#include <limits>
#include <stdexcept>

using namespace std::chrono_literals;

TEST(VideoEncoderProbeCache, SuccessfulProbeIsPublishedOnlyOnce)
{
	CVideoEncoderProbeCache<> Cache;
	int Calls = 0;
	const auto Probe = [&](auto &vResults) {
		++Calls;
		vResults.push_back(MakeVideoEncoderProbe("libx264", "software"));
		vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "hardware"));
	};
	const auto &First = Cache.Get(Probe);
	ASSERT_EQ(First.size(), 2u);
	EXPECT_EQ(&Cache.Get(Probe), &First);
	EXPECT_EQ(Calls, 1);
	EXPECT_STREQ(First[1].m_aName, "av1_qsv");
}

TEST(VideoEncoderProbeCache, FailedAllocationSnapshotStaysEmptyAfterSuccessfulRetry)
{
	CTrackedAllocationResource Resource;
	{
		CVideoEncoderProbeCache<CBorrowedAllocator<video_encoder::SProbeResult>> Cache{&Resource};
		Resource.m_FailAt = 1;
		int Calls = 0;
		const auto Probe = [&](auto &vResults) {
			++Calls;
			vResults.reserve(2);
			vResults.push_back(MakeVideoEncoderProbe("libx264", "software"));
		};
		const auto &Unavailable = Cache.Get(Probe);
		EXPECT_TRUE(Unavailable.empty());
		const auto &Ready = Cache.Get(Probe);
		ASSERT_EQ(Ready.size(), 1u);
		EXPECT_TRUE(Unavailable.empty());
		EXPECT_STREQ(Ready[0].m_aName, "libx264");
		EXPECT_EQ(Calls, 2);
	}
	EXPECT_EQ(Resource.m_Live, 0);
}

TEST(VideoEncoderProbeCache, PartialProbeIsNotPublishedWhenProducerRunsOutOfMemory)
{
	CVideoEncoderProbeCache<> Cache;
	const auto &Unavailable = Cache.Get([](auto &vResults) {
		vResults.push_back(MakeVideoEncoderProbe("libx264", "partial"));
		throw std::bad_alloc();
	});
	EXPECT_TRUE(Unavailable.empty());
	const auto &Ready = Cache.Get([](auto &vResults) { vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "complete")); });
	ASSERT_EQ(Ready.size(), 1u);
	EXPECT_STREQ(Ready[0].m_aName, "av1_qsv");
	EXPECT_TRUE(Unavailable.empty());
}

TEST(VideoEncoderProbeCache, ContainerLengthFailureAllowsLaterProbe)
{
	CVideoEncoderProbeCache<> Cache;
	EXPECT_TRUE(Cache.Get([](auto &vResults) { vResults.reserve(std::numeric_limits<size_t>::max()); }).empty());
	const auto &Ready = Cache.Get([](auto &vResults) { vResults.push_back(MakeVideoEncoderProbe("libx264", "software")); });
	ASSERT_EQ(Ready.size(), 1u);
}

TEST(VideoEncoderProbeCache, UnknownAllocatorExceptionPropagatesAndDiscardsPartialCandidate)
{
	CTrackedAllocationResource Resource;
	CVideoEncoderProbeCache<CBorrowedAllocator<video_encoder::SProbeResult>> Cache{&Resource};
	EXPECT_THROW(Cache.Get([&](auto &vResults) {
		vResults.push_back(MakeVideoEncoderProbe("libx264", "partial"));
		Resource.m_FailAt = Resource.m_Allocations + 1;
		Resource.m_ThrowUnknown = true;
		vResults.reserve(vResults.capacity() + 1);
	}),
		int);
	EXPECT_EQ(Resource.m_Live, 0);
	const auto &Ready = Cache.Get([](auto &vResults) { vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "complete")); });
	ASSERT_EQ(Ready.size(), 1u);
	EXPECT_STREQ(Ready[0].m_aName, "av1_qsv");
}

TEST(VideoEncoderProbeCache, ConcurrentCallersReceiveTheSameCompletedSnapshot)
{
	CVideoEncoderProbeCache<> Cache;
	std::promise<void> Started;
	auto StartedFuture = Started.get_future();
	std::promise<void> Release;
	const auto Released = Release.get_future().share();
	auto First = std::async(std::launch::async, [&]() {
		return &Cache.Get([&](auto &vResults) {
			Started.set_value();
			if(Released.wait_for(2s) != std::future_status::ready)
				throw std::runtime_error("probe release timed out");
			vResults.push_back(MakeVideoEncoderProbe("libx264", "complete"));
		});
	});
	const auto StartStatus = StartedFuture.wait_for(2s);
	auto Second = std::async(std::launch::async, [&]() {
		return &Cache.Get([](auto &) { throw std::runtime_error("successful probe was repeated"); });
	});
	Release.set_value();
	ASSERT_EQ(StartStatus, std::future_status::ready);
	ASSERT_EQ(First.wait_for(2s), std::future_status::ready);
	ASSERT_EQ(Second.wait_for(2s), std::future_status::ready);
	const auto *pFirst = First.get();
	const auto *pSecond = Second.get();
	EXPECT_EQ(pFirst, pSecond);
	ASSERT_EQ(pSecond->size(), 1u);
	EXPECT_STREQ((*pSecond)[0].m_aName, "libx264");
}

TEST(VideoEncoderProbeCache, ConcurrentRetryCannotModifyAFailedCallersSnapshot)
{
	CVideoEncoderProbeCache<> Cache;
	std::promise<void> Started;
	auto StartedFuture = Started.get_future();
	std::promise<void> Release;
	const auto Released = Release.get_future().share();
	auto First = std::async(std::launch::async, [&]() {
		return &Cache.Get([&](auto &vResults) {
			Started.set_value();
			if(Released.wait_for(2s) != std::future_status::ready)
				throw std::runtime_error("probe release timed out");
			vResults.push_back(MakeVideoEncoderProbe("libx264", "partial"));
			throw std::bad_alloc();
		});
	});
	const auto StartStatus = StartedFuture.wait_for(2s);
	auto Second = std::async(std::launch::async, [&]() {
		return &Cache.Get([](auto &vResults) { vResults.push_back(MakeVideoEncoderProbe("av1_qsv", "complete")); });
	});
	Release.set_value();
	ASSERT_EQ(StartStatus, std::future_status::ready);
	ASSERT_EQ(First.wait_for(2s), std::future_status::ready);
	ASSERT_EQ(Second.wait_for(2s), std::future_status::ready);
	const auto *pFailedSnapshot = First.get();
	const auto *pReadySnapshot = Second.get();
	EXPECT_TRUE(pFailedSnapshot->empty());
	ASSERT_EQ(pReadySnapshot->size(), 1u);
	EXPECT_STREQ((*pReadySnapshot)[0].m_aName, "av1_qsv");
}
