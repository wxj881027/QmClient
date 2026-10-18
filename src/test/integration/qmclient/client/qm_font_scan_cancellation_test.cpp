#include <engine/client/qm_font_restore.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <chrono>
#include <cstring>
#include <future>
#include <new>
#include <thread>

using namespace std::chrono_literals;

namespace
{
	bool ReadBytes(const char *, void **ppData, unsigned *pSize)
	{
		*ppData = malloc(3);
		if(*ppData == nullptr)
			throw std::bad_alloc();
		*pSize = 3;
		std::memcpy(*ppData, "abc", 3);
		return true;
	}

	// 单 worker 执行下一项后，才能检查已取消任务的回调结果。
	void WaitForWorker(CJobPool &Pool)
	{
		const auto pNext = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{}; }, ReadBytes);
		Pool.Add(pNext);
		EXPECT_TRUE(WaitForJobDone(*pNext));
		Pool.Shutdown();
	}
}

TEST(QmFontScanCancellation, AbortedQueuedJobDoesNotEnumerateOrRead)
{
	int Enumerations = 0, Reads = 0;
	const auto pJob = std::make_shared<CQmFontScanJob>([&](const auto &) {
		++Enumerations;
		return std::vector<std::string>{"font"}; }, [&](const char *, void **, unsigned *) {
		++Reads;
		return false; });
	EXPECT_TRUE(pJob->Abort());
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	WaitForWorker(Pool);
	EXPECT_EQ(Enumerations, 0);
	EXPECT_EQ(Reads, 0);
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pJob->TakeNext(File));
}

TEST(QmFontScanCancellation, AbortDuringReadDiscardsPrefixAndSkipsRemainingFiles)
{
	std::promise<void> Started, Release;
	auto StartedFuture = Started.get_future();
	auto ReleaseFuture = Release.get_future();
	int Reads = 0;
	const auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"first", "blocked", "unread"}; }, [&](const char *pPath, void **ppData, unsigned *pSize) {
		++Reads;
		ReadBytes(pPath, ppData, pSize);
		if(std::strcmp(pPath, "blocked") == 0)
		{
			Started.set_value();
			ReleaseFuture.wait_for(2s);
		}
		return true; });
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_EQ(StartedFuture.wait_for(2s), std::future_status::ready);
	EXPECT_TRUE(pJob->Abort());
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pJob->TakeNext(File));
	EXPECT_FALSE(pJob->Exhausted());
	Release.set_value();
	WaitForWorker(Pool);
	EXPECT_EQ(Reads, 2);
	EXPECT_FALSE(pJob->TakeNext(File));
}

TEST(QmFontScanCancellation, EnumerationObservesCancellationAndCannotPublishReturnedPaths)
{
	std::promise<void> Started, Release;
	auto StartedFuture = Started.get_future();
	auto ReleaseFuture = Release.get_future();
	bool SawCancellation = false;
	int Reads = 0;
	const auto pJob = std::make_shared<CQmFontScanJob>([&](const auto &Cancelled) {
		Started.set_value();
		ReleaseFuture.wait_for(2s);
		SawCancellation = Cancelled();
		return std::vector<std::string>{"font"}; }, [&](const char *, void **, unsigned *) {
		++Reads;
		return false; });
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_EQ(StartedFuture.wait_for(2s), std::future_status::ready);
	EXPECT_TRUE(pJob->Abort());
	Release.set_value();
	WaitForWorker(Pool);
	EXPECT_TRUE(SawCancellation);
	EXPECT_EQ(Reads, 0);
}

TEST(QmFontScanCancellation, ShutdownCancelsRunningEnumerationBeforeFullRead)
{
	std::promise<void> Started;
	auto StartedFuture = Started.get_future();
	bool SawCancellation = false;
	int Reads = 0;
	const auto pJob = std::make_shared<CQmFontScanJob>([&](const auto &Cancelled) {
		Started.set_value();
		const auto Deadline = std::chrono::steady_clock::now() + 2s;
		while(!Cancelled() && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::yield();
		SawCancellation = Cancelled();
		return std::vector<std::string>{"font"}; }, [&](const char *, void **, unsigned *) {
		++Reads;
		return false; });
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_EQ(StartedFuture.wait_for(2s), std::future_status::ready);
	Pool.Shutdown();
	EXPECT_TRUE(SawCancellation);
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
	EXPECT_EQ(Reads, 0);
}

TEST(QmFontScanCancellation, CancellingCompletedScanSuppressesUnconsumedResults)
{
	const auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"first", "second"}; }, ReadBytes);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	EXPECT_TRUE(WaitForJobDone(*pJob));
	Pool.Shutdown();
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(pJob->TakeNext(File));
	EXPECT_TRUE(pJob->Abort());
	CQmFontScanJob::SFile Next;
	EXPECT_FALSE(pJob->TakeNext(Next));
	EXPECT_EQ(std::memcmp(File.m_pData.get(), "abc", 3), 0);
}

TEST(QmFontScanCancellation, RestoreResetCancelsLookupAndNewSelectionCanFinish)
{
	CQmFontRestore Restore;
	const auto Missing = [](const char *) { return false; };
	std::promise<void> Started, Release;
	auto StartedFuture = Started.get_future();
	auto ReleaseFuture = Release.get_future();
	bool SawCancellation = false;
	const auto pOld = Restore.Request({"old", "", ""}, Missing, [&](const auto &vNames, const auto &Cancelled) {
		Started.set_value();
		ReleaseFuture.wait_for(2s);
		SawCancellation = Cancelled();
		return vNames; }, ReadBytes);
	ASSERT_NE(pOld, nullptr);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pOld);
	EXPECT_EQ(StartedFuture.wait_for(2s), std::future_status::ready);
	Restore.Reset();
	EXPECT_EQ(pOld->State(), IJob::STATE_ABORTED);
	const auto pNew = Restore.Request({"new", "", ""}, Missing, [](const auto &vNames, const auto &) { return vNames; }, ReadBytes);
	Release.set_value();
	ASSERT_NE(pNew, nullptr);
	Pool.Add(pNew);
	EXPECT_TRUE(WaitForJobDone(*pNew));
	Pool.Shutdown();
	EXPECT_TRUE(SawCancellation);
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pOld->TakeNext(File));
	ASSERT_TRUE(Restore.TakeNext(File));
	EXPECT_EQ(File.m_Path, "new");
	bool Applied = false;
	EXPECT_TRUE(Restore.Finish({"new", "", ""}, [](const char *pName) { return std::strcmp(pName, "new") == 0; }, [&](size_t Role, const char *pName) {
		EXPECT_EQ(Role, 0u);
		EXPECT_STREQ(pName, "new");
		Applied = true; }));
	EXPECT_TRUE(Applied);
}
