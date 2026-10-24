#include <gtest/gtest.h>
#include <test/support/qm_netease_bridge_operations.h>

#include <string>

using NeteaseBridgeTest::COperations;
using QmNeteaseBridge::CFrontendLyricBridge;

namespace
{
	void PausedSong(COperations &Operations)
	{
		Operations.Emit(R"({"kind":"progress","songId":"42","positionMs":1500,"playing":false})");
		Operations.Emit(R"({"kind":"lyrics","songId":"42","rawLyrics":{"lrc":"[00:01.00]hello\n[00:02.00]world"}})");
	}
}

TEST(QmNeteaseBridgeLifecycleIntegration, StartupPublishesValidIdentityAndStopsBeforeClosingMapping)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	EXPECT_TRUE(Bridge.IsRunning());
	EXPECT_TRUE(Bridge.IsConnected());
	const auto Snapshot = Operations.Latest();
	EXPECT_EQ(Snapshot.m_CloudMusicPid, 42U);
	EXPECT_EQ(Snapshot.m_SongId, 0U);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshotV5(Snapshot));
	Bridge.Stop();
	EXPECT_EQ(Operations.m_TickExits, 1);
	EXPECT_TRUE(Operations.m_CloseAfterJoin);
	EXPECT_LT(Operations.m_LastStopOrder, Operations.m_LastCloseOrder);
}

TEST(QmNeteaseBridgeLifecycleIntegration, ReportsParserAndTickPublishTheSelectedPausedLyric)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	PausedSong(Operations);
	Operations.Advance();
	ASSERT_TRUE(Operations.Wait([&] { return std::string(Operations.Latest().m_aCurrentLyric) == "hello"; }));
	const auto Snapshot = Operations.Latest();
	EXPECT_EQ(Snapshot.m_SongId, 42U);
	EXPECT_EQ(Snapshot.m_PositionMs, 1500);
	EXPECT_EQ(Snapshot.m_LineStartMs, 1000);
	EXPECT_EQ(Snapshot.m_LineEndMs, 2000);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshotV5(Snapshot));
}

TEST(QmNeteaseBridgeLifecycleIntegration, MalformedReportKeepsThePreviouslyPublishedSong)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	PausedSong(Operations);
	const auto Previous = Operations.Latest();
	const int Published = Operations.m_Published;
	Operations.Emit("{broken");
	Operations.Emit(R"({"kind":7,"songId":"99"})");
	EXPECT_EQ(Operations.m_Published, Published);
	EXPECT_EQ(Operations.Latest().m_SongId, Previous.m_SongId);
	EXPECT_TRUE(Bridge.IsRunning());
}

TEST(QmNeteaseBridgeLifecycleIntegration, ReportPublishExceptionDoesNotEscapeAndLaterTickRecovers)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	Operations.m_PublishFailures = 1;
	EXPECT_NO_THROW(PausedSong(Operations));
	Operations.Advance();
	ASSERT_TRUE(Operations.Wait([&] { return std::string(Operations.Latest().m_aCurrentLyric) == "hello"; }));
	EXPECT_TRUE(Bridge.IsRunning());
}

TEST(QmNeteaseBridgeLifecycleIntegration, FailedTickPublishKeepsTheWorkerAvailableForTheNextCycle)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	PausedSong(Operations);
	const int Published = Operations.m_Published;
	Operations.m_PublishFailures = 1;
	Operations.Advance();
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 2; }));
	EXPECT_EQ(Operations.m_Published, Published);
	EXPECT_TRUE(Bridge.IsRunning());
	Operations.Advance();
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Published > Published; }));
	EXPECT_STREQ(Operations.Latest().m_aCurrentLyric, "hello");
}

TEST(QmNeteaseBridgeLifecycleIntegration, ReportPreparationFailureKeepsTheOldIdentityAndAcceptsANewReport)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	const int Published = Operations.m_Published;
	Operations.m_ClockFailures = 1;
	EXPECT_NO_THROW(Operations.Emit(R"({"kind":"progress","songId":"42","positionMs":1500})"));
	EXPECT_EQ(Operations.m_Published, Published);
	EXPECT_EQ(Operations.Latest().m_SongId, 0U);
	PausedSong(Operations);
	EXPECT_EQ(Operations.Latest().m_SongId, 42U);
}

TEST(QmNeteaseBridgeLifecycleIntegration, WaitFailureMarksTheOwnerUnavailableAndExplicitRestartAdvancesIdentity)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	const uint64_t Previous = Operations.Latest().m_Generation;
	Operations.m_WaitFailures = 1;
	Operations.Advance();
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_TickExits == 1; }));
	EXPECT_FALSE(Bridge.IsRunning());
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 2; }));
	EXPECT_GT(Operations.Latest().m_Generation, Previous);
	EXPECT_TRUE(Bridge.IsRunning());
}

TEST(QmNeteaseBridgeLifecycleIntegration, ReportWorkerExitIsVisibleToTheOwnerAndCanBeRestarted)
{
	COperations Operations;
	Operations.m_RealThread = true;
	CFrontendLyricBridge Bridge(Operations);
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	Operations.m_ReportsActive = false;
	EXPECT_FALSE(Bridge.IsRunning());
	ASSERT_TRUE(Bridge.Start(42, L""));
	EXPECT_TRUE(Bridge.IsRunning());
	EXPECT_TRUE(Operations.m_CloseAfterJoin);
}

TEST(QmNeteaseBridgeLifecycleIntegration, FailedThreadCreationCanRetryAfterCleaningThePreviousAttempt)
{
	COperations Operations;
	Operations.m_RealThread = true;
	Operations.m_Failure = COperations::EFailure::THREAD;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_FALSE(Operations.m_ReportsActive);
	EXPECT_FALSE(Operations.m_WriterOpen);
	const uint64_t Previous = Operations.Latest().m_Generation;
	Operations.m_Failure = COperations::EFailure::NONE;
	ASSERT_TRUE(Bridge.Start(42, L""));
	ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	EXPECT_GT(Operations.Latest().m_Generation, Previous);
	EXPECT_TRUE(Bridge.IsRunning());
}

TEST(QmNeteaseBridgeLifecycleIntegration, DestructionFencesReportsAndJoinsTickBeforeClosingMapping)
{
	COperations Operations;
	Operations.m_RealThread = true;
	{
		CFrontendLyricBridge Bridge(Operations);
		ASSERT_TRUE(Bridge.Start(42, L""));
		ASSERT_TRUE(Operations.Wait([&] { return Operations.m_Waits >= 1; }));
	}
	EXPECT_EQ(Operations.m_TickExits, 1);
	EXPECT_FALSE(Operations.m_ReportsActive);
	EXPECT_FALSE(Operations.m_WriterOpen);
	EXPECT_TRUE(Operations.m_CloseAfterJoin);
	const int Published = Operations.m_Published;
	Operations.Emit(R"({"kind":"progress","songId":"99","positionMs":1})");
	EXPECT_EQ(Operations.m_Published, Published);
}
