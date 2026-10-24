#include <gtest/gtest.h>
#include <test/support/qm_netease_bridge_operations.h>

using NeteaseBridgeTest::COperations;
using QmNeteaseBridge::CFrontendLyricBridge;

TEST(QmNeteaseBridgeStartup, InvalidPidDoesNotPrepareExternalResources)
{
	COperations Operations;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(0, L""));
	EXPECT_EQ(Operations.m_Opened, 0);
	EXPECT_EQ(Operations.m_ThreadCalls, 0);
}

TEST(QmNeteaseBridgeStartup, DeclinedMappingClosesThePreparedCandidate)
{
	COperations Operations;
	Operations.m_OpenResult = false;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_FALSE(Operations.m_WriterOpen);
	EXPECT_EQ(Operations.m_Closed, 1);
	EXPECT_EQ(Operations.m_ReportStarts, 0);
}

TEST(QmNeteaseBridgeStartup, MappingExceptionsRemainInsideTheOwnerAndReleaseTheCandidate)
{
	for(int Kind : {1, 2, 3})
	{
		SCOPED_TRACE(Kind);
		COperations Operations;
		Operations.m_Failure = COperations::EFailure::OPEN;
		Operations.m_Exception = Kind;
		CFrontendLyricBridge Bridge(Operations);
		EXPECT_FALSE(Bridge.Start(42, L""));
		EXPECT_FALSE(Bridge.IsRunning());
		EXPECT_FALSE(Operations.m_WriterOpen);
		EXPECT_EQ(Operations.m_Closed, 1);
	}
}

TEST(QmNeteaseBridgeStartup, PreviousSnapshotFailureClosesMappingBeforeStartingReports)
{
	COperations Operations;
	Operations.m_Failure = COperations::EFailure::READ;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_EQ(Operations.m_Closed, 1);
	EXPECT_EQ(Operations.m_ReportStarts, 0);
}

TEST(QmNeteaseBridgeStartup, DeclinedReportStartupStopsPartialReportsBeforeClosingMapping)
{
	COperations Operations;
	Operations.m_ReportResult = false;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_EQ(Operations.m_ReportStops, 1);
	EXPECT_LT(Operations.m_LastStopOrder, Operations.m_LastCloseOrder);
	EXPECT_EQ(Operations.m_ThreadCalls, 0);
}

TEST(QmNeteaseBridgeStartup, ReportCreationExceptionRollsBackStartedResources)
{
	COperations Operations;
	Operations.m_Failure = COperations::EFailure::REPORTS;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_FALSE(Operations.m_ReportsActive);
	EXPECT_FALSE(Operations.m_WriterOpen);
	EXPECT_EQ(Operations.m_ReportStops, 1);
}

TEST(QmNeteaseBridgeStartup, InitialPublishExceptionStopsReportsWithoutCreatingTickThread)
{
	COperations Operations;
	Operations.m_Failure = COperations::EFailure::PUBLISH;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_EQ(Operations.m_ReportStops, 1);
	EXPECT_EQ(Operations.m_Closed, 1);
	EXPECT_EQ(Operations.m_ThreadCalls, 0);
}

TEST(QmNeteaseBridgeStartup, TickThreadCreationExceptionsReleaseReportsAndMapping)
{
	for(int Kind : {1, 2, 3})
	{
		SCOPED_TRACE(Kind);
		COperations Operations;
		Operations.m_Failure = COperations::EFailure::THREAD;
		Operations.m_Exception = Kind;
		CFrontendLyricBridge Bridge(Operations);
		EXPECT_FALSE(Bridge.Start(42, L""));
		EXPECT_FALSE(Bridge.IsRunning());
		EXPECT_EQ(Operations.m_ReportStops, 1);
		EXPECT_EQ(Operations.m_Closed, 1);
		EXPECT_LT(Operations.m_LastStopOrder, Operations.m_LastCloseOrder);
	}
}

TEST(QmNeteaseBridgeStartup, EmptyTickThreadIsRejectedAndAllPreparedResourcesAreClosed)
{
	COperations Operations;
	CFrontendLyricBridge Bridge(Operations);
	EXPECT_FALSE(Bridge.Start(42, L""));
	EXPECT_FALSE(Bridge.IsConnected());
	EXPECT_EQ(Operations.m_ReportStops, 1);
	EXPECT_EQ(Operations.m_Closed, 1);
}

TEST(QmNeteaseBridgeStartup, RepeatedStopDoesNotReleaseTheSameCandidateAgain)
{
	COperations Operations;
	{
		CFrontendLyricBridge Bridge(Operations);
		EXPECT_FALSE(Bridge.Start(42, L""));
		Bridge.Stop();
		Bridge.Stop();
	}
	EXPECT_EQ(Operations.m_ReportStops, 1);
	EXPECT_EQ(Operations.m_Closed, 1);
}
