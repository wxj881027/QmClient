#include <base/system.h>

#include <engine/client/qm_hang_diagnostics.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

namespace
{
	QmHangDiagnostics::SSnapshot SnapshotForGeneration(int Generation)
	{
		QmHangDiagnostics::SSnapshot Snapshot;
		Snapshot.m_State = Generation;
		str_format(Snapshot.m_aCurrentMap, sizeof(Snapshot.m_aCurrentMap), "map-%d", Generation);
		str_format(Snapshot.m_aServerAddr, sizeof(Snapshot.m_aServerAddr), "server-%d", Generation);
		Snapshot.m_LastHeartbeat = Generation;
		return Snapshot;
	}
}

TEST(QmHangSnapshot, StartsWithoutAHeartbeat)
{
	QmHangDiagnostics::CSnapshotStore Store;
	const auto Snapshot = Store.Read();
	EXPECT_EQ(Store.LastHeartbeat(), 0);
	EXPECT_EQ(Snapshot.m_LastHeartbeat, 0);
	EXPECT_EQ(Snapshot.m_State, IClient::STATE_OFFLINE);
	EXPECT_STREQ(Snapshot.m_aCurrentMap, "");
	EXPECT_STREQ(Snapshot.m_aServerAddr, "");
}

TEST(QmHangSnapshot, PublicationOwnsItsCopyOfTheClientState)
{
	QmHangDiagnostics::CSnapshotStore Store;
	auto Input = SnapshotForGeneration(1);
	Store.Publish(Input);
	Input = SnapshotForGeneration(2);
	const auto Snapshot = Store.Read();
	EXPECT_EQ(Snapshot.m_State, 1);
	EXPECT_STREQ(Snapshot.m_aCurrentMap, "map-1");
	EXPECT_STREQ(Snapshot.m_aServerAddr, "server-1");
	EXPECT_EQ(Snapshot.m_LastHeartbeat, 1);
	EXPECT_EQ(Store.LastHeartbeat(), 1);
}

TEST(QmHangSnapshot, ReportCopySurvivesRepeatedPublications)
{
	QmHangDiagnostics::CSnapshotStore Store;
	Store.Publish(SnapshotForGeneration(1));
	auto Report = Store.Read();
	Store.Publish(SnapshotForGeneration(2));
	Store.Publish(SnapshotForGeneration(3));
	EXPECT_EQ(Report.m_State, 1);
	EXPECT_STREQ(Report.m_aCurrentMap, "map-1");
	EXPECT_STREQ(Report.m_aServerAddr, "server-1");
	EXPECT_EQ(Report.m_LastHeartbeat, 1);
	Report.m_State = 99;
	EXPECT_EQ(Store.Read().m_State, 3);
	EXPECT_EQ(Store.LastHeartbeat(), 3);
}

TEST(QmHangSnapshot, ConcurrentReadsKeepAllFieldsFromTheSamePublication)
{
	QmHangDiagnostics::CSnapshotStore Store;
	Store.Publish(SnapshotForGeneration(1));
	auto Writer = std::async(std::launch::async, [&Store]() {
		for(int Generation = 2; Generation <= 20000; ++Generation)
			Store.Publish(SnapshotForGeneration(Generation));
	});

	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	for(int Read = 0; Read < 20000 && std::chrono::steady_clock::now() < Deadline; ++Read)
	{
		const auto Snapshot = Store.Read();
		char aExpectedMap[IO_MAX_PATH_LENGTH];
		char aExpectedServer[NETADDR_MAXSTRSIZE];
		str_format(aExpectedMap, sizeof(aExpectedMap), "map-%d", Snapshot.m_State);
		str_format(aExpectedServer, sizeof(aExpectedServer), "server-%d", Snapshot.m_State);
		EXPECT_STREQ(Snapshot.m_aCurrentMap, aExpectedMap);
		EXPECT_STREQ(Snapshot.m_aServerAddr, aExpectedServer);
		EXPECT_EQ(Snapshot.m_LastHeartbeat, Snapshot.m_State);
	}
	ASSERT_EQ(Writer.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	Writer.get();
	EXPECT_EQ(Store.LastHeartbeat(), 20000);
}
