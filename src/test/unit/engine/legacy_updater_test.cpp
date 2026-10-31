#include <engine/client/updater.h>
#include <engine/client/updater_file.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

TEST(LegacyUpdater, OnlyIdleOrTerminalStatesAllowANewAttempt)
{
	for(auto State : {IUpdater::CLEAN, IUpdater::FAIL, IUpdater::NEED_RESTART})
	{
		SCOPED_TRACE(State);
		EXPECT_TRUE(QmLegacyUpdater::CanInitiate(State, false));
	}
}

TEST(LegacyUpdater, ActivePhasesRejectDuplicateInitiation)
{
	for(auto State : {IUpdater::GETTING_MANIFEST, IUpdater::GOT_MANIFEST, IUpdater::PARSING_UPDATE, IUpdater::DOWNLOADING, IUpdater::MOVE_FILES})
	{
		SCOPED_TRACE(State);
		EXPECT_FALSE(QmLegacyUpdater::CanInitiate(State, false));
	}
}

TEST(LegacyUpdater, AStillActiveRequestPreventsRetryEvenAfterFailure)
{
	for(auto State : {IUpdater::CLEAN, IUpdater::FAIL, IUpdater::NEED_RESTART})
	{
		SCOPED_TRACE(State);
		EXPECT_FALSE(QmLegacyUpdater::CanInitiate(State, true));
	}
}

TEST(LegacyUpdater, QueuedAndRunningDownloadsContinueWaiting)
{
	EXPECT_EQ(QmLegacyUpdater::DownloadState(EHttpState::QUEUED), IUpdater::DOWNLOADING);
	EXPECT_EQ(QmLegacyUpdater::DownloadState(EHttpState::RUNNING), IUpdater::DOWNLOADING);
}

TEST(LegacyUpdater, FailedDownloadCannotAdvanceToFileReplacement)
{
	EXPECT_EQ(QmLegacyUpdater::DownloadState(EHttpState::ERROR), IUpdater::FAIL);
}

TEST(LegacyUpdater, AbortedDownloadCannotAdvanceToFileReplacement)
{
	EXPECT_EQ(QmLegacyUpdater::DownloadState(EHttpState::ABORTED), IUpdater::FAIL);
}

TEST(LegacyUpdater, OnlySuccessfulDownloadCanAdvanceToFileReplacement)
{
	EXPECT_EQ(QmLegacyUpdater::DownloadState(EHttpState::DONE), IUpdater::MOVE_FILES);
}

TEST(LegacyUpdater, PlatformLibraryNamePreservesTheExtensionAndDirectory)
{
	std::array<char, 128> aName{};
	ASSERT_TRUE(QmLegacyUpdater::FormatPlatformLibraryName("lib/path/example.dll", 4, "win64", aName.data(), aName.size()));
	EXPECT_STREQ(aName.data(), "lib/path/example-win64.dll");
	ASSERT_TRUE(QmLegacyUpdater::FormatPlatformLibraryName("lib/example.so", 3, "linux-x86_64", aName.data(), aName.size()));
	EXPECT_STREQ(aName.data(), "lib/example-linux-x86_64.so");
}

TEST(LegacyUpdater, OversizedLibraryPathFailsWithAnEmptyOutput)
{
	std::array<char, 32> aName{};
	const std::string Name(1000, 'a');
	EXPECT_FALSE(QmLegacyUpdater::FormatPlatformLibraryName(Name.c_str(), 4, "win64", aName.data(), aName.size()));
	EXPECT_STREQ(aName.data(), "");
}

TEST(LegacyUpdater, PlatformLibraryNameAcceptsTheExactTerminatedCapacity)
{
	std::array<char, sizeof("x-win64.dll")> aName{};
	ASSERT_TRUE(QmLegacyUpdater::FormatPlatformLibraryName("x.dll", 4, "win64", aName.data(), aName.size()));
	EXPECT_STREQ(aName.data(), "x-win64.dll");
	EXPECT_FALSE(QmLegacyUpdater::FormatPlatformLibraryName("x.dll", 4, "win64", aName.data(), aName.size() - 1));
	EXPECT_STREQ(aName.data(), "");
}

TEST(LegacyUpdater, InvalidExtensionLengthAndEmptyOutputCapacityAreRejected)
{
	char aName[32] = "old";
	EXPECT_FALSE(QmLegacyUpdater::FormatPlatformLibraryName("x", 4, "win64", aName, sizeof(aName)));
	EXPECT_STREQ(aName, "");
	EXPECT_FALSE(QmLegacyUpdater::FormatPlatformLibraryName("x.dll", 4, "win64", aName, 0));
}

TEST(LegacyUpdater, NewUpdaterHasAnEmptyStatusAndCleanStateBeforeInitialization)
{
	CUpdater Updater;
	char aStatus[64] = "previous";
	Updater.GetCurrentFile(aStatus, sizeof(aStatus));
	EXPECT_STREQ(aStatus, "");
	EXPECT_EQ(Updater.GetCurrentState(), IUpdater::CLEAN);
	EXPECT_EQ(Updater.GetCurrentPercent(), 0);
	Updater.Update();
	EXPECT_EQ(Updater.GetCurrentState(), IUpdater::CLEAN);
}

TEST(LegacyUpdater, InitiatingBeforeInitializationFailsWithoutStartingARequest)
{
	CUpdater Updater;
	Updater.InitiateUpdate();
	EXPECT_EQ(Updater.GetCurrentState(), IUpdater::FAIL);
	EXPECT_EQ(Updater.GetCurrentPercent(), 0);
	Updater.Update();
	EXPECT_EQ(Updater.GetCurrentState(), IUpdater::FAIL);
}
