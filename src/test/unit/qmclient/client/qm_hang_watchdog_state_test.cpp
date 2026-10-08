#include <base/str.h>

#include <engine/client/qm_hang_diagnostics.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

namespace
{
	void PublishHeartbeat(QmHangDiagnostics::CSnapshotStore &Store, int64_t Now, int State, const char *pCurrentMap, const char *pServerAddr)
	{
		QmHangDiagnostics::SSnapshot Snapshot;
		Snapshot.m_LastHeartbeat = Now;
		Snapshot.m_State = State;
		str_copy(Snapshot.m_aCurrentMap, pCurrentMap);
		str_copy(Snapshot.m_aServerAddr, pServerAddr);
		Store.Publish(Snapshot);
	}
}

TEST(QmHangWatchdogState, DoesNotReportBeforeFirstHeartbeat)
{
	QmHangDiagnostics::CSnapshotStore State;
	EXPECT_FALSE(State.TryClaimReport(100, 10).has_value());
	State.StartWatchdog();
	EXPECT_FALSE(State.TryClaimReport(100, 10).has_value());
}

TEST(QmHangWatchdogState, ReportsAtTimeoutBoundaryWithMatchingClientInfo)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 20, IClient::STATE_OFFLINE, "", "unknown");
	EXPECT_FALSE(State.TryClaimReport(29, 10).has_value());
	const auto Snapshot = State.TryClaimReport(30, 10);
	ASSERT_TRUE(Snapshot.has_value());
	EXPECT_EQ(Snapshot->m_LastHeartbeat, 20);
	EXPECT_EQ(Snapshot->m_State, IClient::STATE_OFFLINE);
	EXPECT_STREQ(Snapshot->m_aCurrentMap, "");
	EXPECT_STREQ(Snapshot->m_aServerAddr, "unknown");
}

TEST(QmHangWatchdogState, FreshHeartbeatPreventsClaimingPreviousTimeout)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 10, IClient::STATE_OFFLINE, "", "unknown");
	// 恢复先于报告领取：不能继续用旧心跳的期限报告新状态。
	PublishHeartbeat(State, 21, IClient::STATE_ONLINE, "race", "127.0.0.1:8303");
	EXPECT_FALSE(State.TryClaimReport(22, 10).has_value());
	const auto Snapshot = State.TryClaimReport(31, 10);
	ASSERT_TRUE(Snapshot.has_value());
	EXPECT_EQ(Snapshot->m_LastHeartbeat, 21);
	EXPECT_EQ(Snapshot->m_State, IClient::STATE_ONLINE);
	EXPECT_STREQ(Snapshot->m_aCurrentMap, "race");
	EXPECT_STREQ(Snapshot->m_aServerAddr, "127.0.0.1:8303");
}

TEST(QmHangWatchdogState, ClaimedSnapshotSurvivesTwoLaterHeartbeatUpdates)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 10, IClient::STATE_LOADING, "old-map", "old-server");
	const auto Snapshot = State.TryClaimReport(20, 10);
	ASSERT_TRUE(Snapshot.has_value());
	PublishHeartbeat(State, 21, IClient::STATE_ONLINE, "new-map", "new-server");
	PublishHeartbeat(State, 22, IClient::STATE_OFFLINE, "", "unknown");
	EXPECT_EQ(Snapshot->m_LastHeartbeat, 10);
	EXPECT_EQ(Snapshot->m_State, IClient::STATE_LOADING);
	EXPECT_STREQ(Snapshot->m_aCurrentMap, "old-map");
	EXPECT_STREQ(Snapshot->m_aServerAddr, "old-server");
}

TEST(QmHangWatchdogState, ReportsOnlyOnceUntilNextStart)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 10, IClient::STATE_OFFLINE, "", "unknown");
	ASSERT_TRUE(State.TryClaimReport(20, 10).has_value());
	EXPECT_FALSE(State.TryClaimReport(30, 10).has_value());
	PublishHeartbeat(State, 31, IClient::STATE_ONLINE, "race", "server");
	EXPECT_FALSE(State.TryClaimReport(41, 10).has_value());
	State.StopWatchdog();
	State.StartWatchdog();
	EXPECT_FALSE(State.TryClaimReport(100, 10).has_value());
	PublishHeartbeat(State, 101, IClient::STATE_OFFLINE, "", "unknown");
	EXPECT_TRUE(State.TryClaimReport(111, 10).has_value());
}

TEST(QmHangWatchdogState, StopPreventsLateReportEvenAfterHeartbeatUpdate)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 10, IClient::STATE_OFFLINE, "", "unknown");
	State.StopWatchdog();
	PublishHeartbeat(State, 20, IClient::STATE_ONLINE, "race", "server");
	EXPECT_FALSE(State.TryClaimReport(100, 10).has_value());
	State.StopWatchdog();
	EXPECT_FALSE(State.TryClaimReport(200, 10).has_value());
}

TEST(QmHangWatchdogState, EarlierPollTimeDoesNotConsumeReport)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 20, IClient::STATE_OFFLINE, "", "unknown");
	EXPECT_FALSE(State.TryClaimReport(19, 10).has_value());
	EXPECT_TRUE(State.TryClaimReport(30, 10).has_value());
}

TEST(QmHangWatchdogState, ZeroTimestampIsAValidFirstHeartbeat)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 0, IClient::STATE_OFFLINE, "", "unknown");
	EXPECT_TRUE(State.TryClaimReport(10, 10).has_value());
}

TEST(QmHangWatchdogState, ConcurrentHeartbeatAndClaimKeepSnapshotFieldsTogether)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 0, IClient::STATE_OFFLINE, "menu", "unknown");
	std::promise<void> Start;
	const auto Ready = Start.get_future().share();
	const auto Timeout = std::chrono::seconds(2);
	auto Writer = std::async(std::launch::async, [&] {
		if(Ready.wait_for(Timeout) != std::future_status::ready)
			return false;
		for(int i = 1; i <= 2000; ++i)
		{
			if(i % 2 == 0)
				PublishHeartbeat(State, i, IClient::STATE_OFFLINE, "menu", "unknown");
			else
				PublishHeartbeat(State, i, IClient::STATE_ONLINE, "race", "127.0.0.1:8303");
		}
		return true;
	});
	auto Reader = std::async(std::launch::async, [&]() -> std::optional<QmHangDiagnostics::SSnapshot> {
		if(Ready.wait_for(Timeout) != std::future_status::ready)
			return std::nullopt;
		return State.TryClaimReport(10000, 10);
	});
	Start.set_value();
	ASSERT_EQ(Writer.wait_for(Timeout), std::future_status::ready);
	ASSERT_EQ(Reader.wait_for(Timeout), std::future_status::ready);
	EXPECT_TRUE(Writer.get());
	const auto Snapshot = Reader.get();
	ASSERT_TRUE(Snapshot.has_value());
	if(Snapshot->m_LastHeartbeat % 2 == 0)
	{
		EXPECT_EQ(Snapshot->m_State, IClient::STATE_OFFLINE);
		EXPECT_STREQ(Snapshot->m_aCurrentMap, "menu");
		EXPECT_STREQ(Snapshot->m_aServerAddr, "unknown");
	}
	else
	{
		EXPECT_EQ(Snapshot->m_State, IClient::STATE_ONLINE);
		EXPECT_STREQ(Snapshot->m_aCurrentMap, "race");
		EXPECT_STREQ(Snapshot->m_aServerAddr, "127.0.0.1:8303");
	}
}

TEST(QmHangWatchdogState, ConcurrentReadersClaimOnlyOneReport)
{
	QmHangDiagnostics::CSnapshotStore State;
	State.StartWatchdog();
	PublishHeartbeat(State, 10, IClient::STATE_OFFLINE, "", "unknown");
	auto First = std::async(std::launch::async, [&] { return State.TryClaimReport(20, 10).has_value(); });
	auto Second = std::async(std::launch::async, [&] { return State.TryClaimReport(20, 10).has_value(); });
	const auto Timeout = std::chrono::seconds(2);
	ASSERT_EQ(First.wait_for(Timeout), std::future_status::ready);
	ASSERT_EQ(Second.wait_for(Timeout), std::future_status::ready);
	EXPECT_NE(First.get(), Second.get());
}
