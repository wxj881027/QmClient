#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <string>

TEST(QmScreenshotExportCleanup, ReportsSuccessfulRemovalOfTheRequestedPath)
{
	const std::string Path = "owned-candidate.tmp";
	int Calls = 0;
	EXPECT_TRUE(CQmScreenshotExportControl::TryRemoveCandidate(Path, [&](const char *pPath) {
		++Calls;
		EXPECT_STREQ(pPath, Path.c_str());
		return 0;
	}));
	EXPECT_EQ(Calls, 1);
}

TEST(QmScreenshotExportCleanup, FailedRemovalIsReportedWithoutRetrying)
{
	int Calls = 0;
	EXPECT_FALSE(CQmScreenshotExportControl::TryRemoveCandidate("owned-candidate.tmp", [&](const char *) {
		++Calls;
		return 1;
	}));
	EXPECT_EQ(Calls, 1);
}

TEST(QmScreenshotExportCleanup, AllocationFailureDoesNotEscapeTheCleanupBoundary)
{
	const std::string Path = "owned-candidate.tmp";
	int Calls = 0;
	bool Removed = true;
	EXPECT_NO_THROW(Removed = CQmScreenshotExportControl::TryRemoveCandidate(Path, [&](const char *) -> int {
		++Calls;
		throw std::bad_alloc();
	}));
	EXPECT_FALSE(Removed);
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(Path, "owned-candidate.tmp");
}

TEST(QmScreenshotExportCleanup, DiagnosticExceptionDoesNotEscapeOrCauseAnotherRemoval)
{
	int Calls = 0;
	EXPECT_FALSE(CQmScreenshotExportControl::TryRemoveCandidate("owned-candidate.tmp", [&](const char *) -> int {
		++Calls;
		throw std::runtime_error("diagnostic allocation failed");
	}));
	EXPECT_EQ(Calls, 1);
}

TEST(QmScreenshotExportCleanup, NonStandardExceptionIsAlsoContained)
{
	int Calls = 0;
	EXPECT_FALSE(CQmScreenshotExportControl::TryRemoveCandidate("owned-candidate.tmp", [&](const char *) -> int {
		++Calls;
		throw 1;
	}));
	EXPECT_EQ(Calls, 1);
}
