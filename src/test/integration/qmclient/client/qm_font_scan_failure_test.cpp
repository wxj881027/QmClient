#include <engine/client/qm_font_restore.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <cstring>
#include <new>
#include <stdexcept>

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

	void RunFontScanJob(const std::shared_ptr<CQmFontScanJob> &pJob)
	{
		CJobPool Pool;
		Pool.Init(1);
		Pool.Add(pJob);
		EXPECT_TRUE(WaitForJobDone(*pJob));
		Pool.Shutdown();
	}
}

TEST(QmFontScanFailure, EnumerationExceptionDoesNotTerminateWorkerOrBlockNextJob)
{
	auto pFailed = std::make_shared<CQmFontScanJob>([](const auto &) -> std::vector<std::string> { throw std::runtime_error("enumeration failed"); }, ReadBytes);
	auto pNext = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"valid"}; }, ReadBytes);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pFailed);
	Pool.Add(pNext);
	EXPECT_TRUE(WaitForJobDone(*pFailed));
	EXPECT_TRUE(WaitForJobDone(*pNext));
	Pool.Shutdown();
	EXPECT_EQ(pFailed->State(), IJob::STATE_DONE);
	EXPECT_TRUE(pFailed->Failed());
	EXPECT_TRUE(pFailed->Exhausted());
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pFailed->TakeNext(File));
	ASSERT_TRUE(pNext->TakeNext(File));
	EXPECT_EQ(File.m_Path, "valid");
	EXPECT_FALSE(pNext->Failed());
}

TEST(QmFontScanFailure, ReadExceptionPublishesOnlyPreviouslyCompletedFiles)
{
	int Reads = 0;
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"first", "broken", "unread"}; }, [&](const char *pPath, void **ppData, unsigned *pSize) {
		++Reads;
		ReadBytes(pPath, ppData, pSize);
		if(std::strcmp(pPath, "broken") == 0)
			throw std::runtime_error("read failed after allocation");
		return true; });
	RunFontScanJob(pJob);
	EXPECT_TRUE(pJob->Failed());
	EXPECT_EQ(Reads, 2);
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(pJob->TakeNext(File));
	EXPECT_EQ(File.m_Path, "first");
	EXPECT_EQ(std::memcmp(File.m_pData.get(), "abc", 3), 0);
	EXPECT_FALSE(pJob->TakeNext(File));
	EXPECT_TRUE(pJob->Exhausted());
}

TEST(QmFontScanFailure, AllocationExceptionLeavesFinishedEmptyScan)
{
	auto pJob = std::make_shared<CQmFontScanJob>([](const auto &) { return std::vector<std::string>{"font"}; }, [](const char *, void **, unsigned *) -> bool { throw std::bad_alloc(); });
	RunFontScanJob(pJob);
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_TRUE(pJob->Failed());
	EXPECT_TRUE(pJob->Exhausted());
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(pJob->TakeNext(File));
}

TEST(QmFontScanFailure, FailedRestoreAllowsChangedSelectionToStartNewRequest)
{
	CQmFontRestore Restore;
	const auto Missing = [](const char *) { return false; };
	auto pFailed = Restore.Request({"Old", "", ""}, Missing, [](const auto &, const auto &) -> std::vector<std::string> { throw std::runtime_error("lookup failed"); }, ReadBytes);
	ASSERT_NE(pFailed, nullptr);
	RunFontScanJob(pFailed);
	EXPECT_TRUE(pFailed->Failed());
	EXPECT_TRUE(Restore.Finish({"Old", "", ""}, Missing, [](size_t, const char *) { ADD_FAILURE() << "missing font applied"; }));
	EXPECT_FALSE(Restore.Active());
	auto pNext = Restore.Request({"New", "", ""}, Missing, [](const auto &, const auto &) { return std::vector<std::string>{"new-font"}; }, ReadBytes);
	ASSERT_NE(pNext, nullptr);
	RunFontScanJob(pNext);
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(Restore.TakeNext(File));
	EXPECT_EQ(File.m_Path, "new-font");
	bool Applied = false;
	EXPECT_TRUE(Restore.Finish({"New", "", ""}, [](const char *pName) { return std::strcmp(pName, "New") == 0; }, [&](size_t Role, const char *pName) {
		EXPECT_EQ(Role, 0u);
		EXPECT_STREQ(pName, "New");
		Applied = true; }));
	EXPECT_TRUE(Applied);
}
