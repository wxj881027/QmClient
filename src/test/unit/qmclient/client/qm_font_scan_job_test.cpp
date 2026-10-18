#include <engine/client/qm_font_scan_job.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <chrono>
#include <cstring>
#include <future>

using namespace std::chrono_literals;

namespace
{
	bool ReadFontBytes(const char *pPath, void **ppData, unsigned *pSize)
	{
		if(std::strcmp(pPath, "missing") == 0)
			return false;
		*pSize = std::strcmp(pPath, "empty") == 0 ? 0 : 3;
		*ppData = malloc(3);
		std::memcpy(*ppData, "abc", 3);
		return true;
	}
}

TEST(QmFontScanJob, PendingScanDoesNotExposePartialFilesOrWaitOnCaller)
{
	std::promise<void> Started, Release;
	auto StartedFuture = Started.get_future();
	auto ReleaseFuture = Release.get_future();
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"first", "second"}; }, [&](const char *pPath, void **ppData, unsigned *pSize) {
		if(std::strcmp(pPath, "second") == 0)
		{
			Started.set_value();
			ReleaseFuture.wait_for(2s);
		}
		return ReadFontBytes(pPath, ppData, pSize); });
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_EQ(StartedFuture.wait_for(2s), std::future_status::ready);
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pJob->TakeNext(File));
	EXPECT_FALSE(pJob->Exhausted());
	Release.set_value();
	EXPECT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	ASSERT_TRUE(pJob->TakeNext(File));
	EXPECT_EQ(File.m_Path, "first");
}

TEST(QmFontScanJob, CompletedScanTransfersOneFileAndRetainsUnconsumedFiles)
{
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"first", "second"}; }, ReadFontBytes);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(pJob->TakeNext(File));
	EXPECT_EQ(File.m_Path, "first");
	EXPECT_EQ(File.m_Size, 3u);
	EXPECT_EQ(std::memcmp(File.m_pData.get(), "abc", 3), 0);
	EXPECT_EQ(File.m_Hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_FALSE(pJob->Exhausted());
	// 逐帧接收期间，尚未消费的结果仍由任务持有。
	CQmFontScanJob::SFile Next;
	ASSERT_TRUE(pJob->TakeNext(Next));
	EXPECT_EQ(Next.m_Path, "second");
	EXPECT_TRUE(pJob->Exhausted());
	EXPECT_FALSE(pJob->TakeNext(Next));
	EXPECT_EQ(std::memcmp(File.m_pData.get(), "abc", 3), 0);
}

TEST(QmFontScanJob, SkipsMissingAndEmptyFilesAndStillPublishesValidFile)
{
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"missing", "empty", "valid"}; }, ReadFontBytes);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(pJob->TakeNext(File));
	EXPECT_EQ(File.m_Path, "valid");
	EXPECT_TRUE(pJob->Exhausted());
}

TEST(QmFontScanJob, EmptyDirectoryCompletesWithoutFiles)
{
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{}; }, ReadFontBytes);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	CQmFontScanJob::SFile File;
	EXPECT_TRUE(pJob->Exhausted());
	EXPECT_FALSE(pJob->TakeNext(File));
}
