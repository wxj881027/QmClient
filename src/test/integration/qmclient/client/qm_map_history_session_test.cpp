#include <game/client/components/qmclient/map_history_session.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>

using namespace QmMapHistory;

TEST(MapHistorySession, RecordIsReadyBeforeReadingTheStartClockAndPublishingActiveState)
{
	CMapHistory History;
	CSession Session;
	std::string Id = "path:maps/map.map";
	int Calls = 0;
	bool RecordReady = false, StillInactive = false;
	ASSERT_TRUE(Session.TryStart(History, "Map", std::move(Id), 100, "2026-11-01", [&]() noexcept {
		++Calls;
		const auto *pRecord = History.Find("path:maps/map.map");
		RecordReady = pRecord && pRecord->m_LastEnteredAt == 100 && pRecord->m_LastPlayedDate == "2026-11-01";
		StillInactive = !Session.Active() && Session.StartTime() == 0 && Session.MapId().empty();
		return INT64_C(1234);
	}));
	EXPECT_EQ(Calls, 1);
	EXPECT_TRUE(RecordReady);
	EXPECT_TRUE(StillInactive);
	EXPECT_TRUE(Session.Active());
	EXPECT_EQ(Session.StartTime(), 1234);
	EXPECT_EQ(Session.MapId(), "path:maps/map.map");
}

TEST(MapHistorySession, AlreadyActiveSessionRejectsReplacementWithoutChangingEitherRecord)
{
	CMapHistory History;
	CSession Session;
	std::string First = "first";
	ASSERT_TRUE(Session.TryStart(History, "First", std::move(First), 100, "2026-11-01", []() noexcept { return INT64_C(10); }));
	const std::string Before = History.ToJson();
	std::string Second = "second";
	int Calls = 0;
	EXPECT_FALSE(Session.TryStart(History, "Second", std::move(Second), 200, "2026-11-02", [&]() noexcept { ++Calls; return INT64_C(20); }));
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(History.ToJson(), Before);
	EXPECT_EQ(Session.MapId(), "first");
	EXPECT_EQ(Session.StartTime(), 10);
	EXPECT_EQ(Second, "second");
}

TEST(MapHistorySession, ResetRetainsHistoryAndARepeatedVisitKeepsDeathsButResetsCompletion)
{
	CMapHistory History;
	CSession Session;
	std::string Id = "map";
	ASSERT_TRUE(Session.TryStart(History, "Map", std::move(Id), 100, "2026-11-01", []() noexcept { return INT64_C(10); }));
	History.AddDeath("map", 3);
	History.MarkFinished("map", 1000, 2000);
	const std::string Before = History.ToJson();
	Session.Reset();
	Session.Reset();
	EXPECT_FALSE(Session.Active());
	EXPECT_EQ(Session.StartTime(), 0);
	EXPECT_TRUE(Session.MapId().empty());
	EXPECT_EQ(History.ToJson(), Before);
	Id = "map";
	ASSERT_TRUE(Session.TryStart(History, "Map renamed", std::move(Id), 200, "2026-11-02", []() noexcept { return INT64_C(20); }));
	ASSERT_EQ(History.Size(), 1u);
	const auto *pRecord = History.Find("map");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_MapName, "Map renamed");
	EXPECT_EQ(pRecord->m_DeathCount, 3);
	EXPECT_FALSE(pRecord->m_Finished);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 0);
	EXPECT_EQ(pRecord->m_LastEnteredAt, 200);
}

TEST(MapHistorySession, MissingIdentityDoesNotCreateAnEntryOrStartTheClock)
{
	CMapHistory History;
	CSession Session;
	std::string Id;
	int Calls = 0;
	EXPECT_FALSE(Session.TryStart(History, "Map", std::move(Id), 100, "2026-11-01", [&]() noexcept { ++Calls; return INT64_C(10); }));
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(History.Size(), 0u);
	EXPECT_FALSE(Session.Active());
	EXPECT_EQ(Session.StartTime(), 0);
}
