#include <game/client/components/qmclient/predicted_event_queue.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	CGameWorld::CPredictedEvent MakeEvent(int Id, int Tick, bool Handled = false)
	{
		CGameWorld::CPredictedEvent Event(NETEVENTTYPE_SOUNDWORLD, vec2(64.0f, 128.0f), Id, Tick, 0);
		Event.m_Handled = Handled;
		return Event;
	}
}

TEST(QmPredictedEventQueue, SnapshotPruningRemovesExpiredHandledHistoryWithoutPrediction)
{
	auto Confirmed = MakeEvent(1, 100, true);
	Confirmed.m_ServerConfirmed = true;
	std::vector<CGameWorld::CPredictedEvent> vEvents = {
		Confirmed,
		MakeEvent(2, 100, true),
		MakeEvent(3, 100),
		MakeEvent(4, 1000, true),
		MakeEvent(5, 1001, true)};

	QmPruneHandledPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED);

	ASSERT_EQ(vEvents.size(), 3u);
	EXPECT_EQ(vEvents[0].m_Id, 3);
	EXPECT_FALSE(vEvents[0].m_Handled);
	EXPECT_EQ(vEvents[1].m_Id, 4);
	EXPECT_EQ(vEvents[2].m_Id, 5);
}

TEST(QmPredictedEventQueue, SnapshotPruningRetainsTheThreeSecondBoundary)
{
	const int Tick = 1000;
	const int BoundaryTick = Tick - 3 * SERVER_TICK_SPEED;
	std::vector<CGameWorld::CPredictedEvent> vEvents = {
		MakeEvent(1, BoundaryTick - 1, true),
		MakeEvent(2, BoundaryTick, true),
		MakeEvent(3, BoundaryTick + 1, true)};

	QmPruneHandledPredictedEvents(vEvents, Tick, SERVER_TICK_SPEED);

	ASSERT_EQ(vEvents.size(), 2u);
	EXPECT_EQ(vEvents[0].m_Id, 2);
	EXPECT_EQ(vEvents[1].m_Id, 3);

	QmPruneHandledPredictedEvents(vEvents, Tick, SERVER_TICK_SPEED);
	ASSERT_EQ(vEvents.size(), 2u);

	QmPruneHandledPredictedEvents(vEvents, Tick + 1, SERVER_TICK_SPEED);
	ASSERT_EQ(vEvents.size(), 1u);
	EXPECT_EQ(vEvents[0].m_Id, 3);
}

TEST(QmPredictedEventQueue, RepeatedSnapshotsKeepConfirmedHistoryBoundedWithoutPrediction)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents;
	for(int Tick = 1; Tick <= 60 * SERVER_TICK_SPEED; ++Tick)
	{
		QmPruneHandledPredictedEvents(vEvents, Tick, SERVER_TICK_SPEED);
		auto Event = MakeEvent(Tick, Tick, true);
		Event.m_ServerConfirmed = true;
		vEvents.push_back(Event);
		ASSERT_LE(vEvents.size(), static_cast<size_t>(3 * SERVER_TICK_SPEED + 1));
	}

	ASSERT_EQ(vEvents.size(), static_cast<size_t>(3 * SERVER_TICK_SPEED + 1));
	EXPECT_EQ(vEvents.front().m_Tick, 57 * SERVER_TICK_SPEED);
	EXPECT_EQ(vEvents.back().m_Tick, 60 * SERVER_TICK_SPEED);
}

TEST(QmPredictedEventQueue, ProcessingPlaysDueEventsOnceInQueueOrder)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents = {
		MakeEvent(1, 99),
		MakeEvent(2, 100, true),
		MakeEvent(3, 101),
		MakeEvent(4, 100)};
	std::vector<int> vPlayed;
	auto Play = [&vPlayed](const CGameWorld::CPredictedEvent &Event) {
		vPlayed.push_back(Event.m_Id);
		return true;
	};

	QmProcessPredictedEvents(vEvents, 100, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(vPlayed, (std::vector<int>{1, 4}));
	ASSERT_EQ(vEvents.size(), 4u);
	EXPECT_TRUE(vEvents[0].m_Handled);
	EXPECT_FALSE(vEvents[2].m_Handled);
	EXPECT_TRUE(vEvents[3].m_Handled);

	QmProcessPredictedEvents(vEvents, 100, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(vPlayed, (std::vector<int>{1, 4}));

	QmProcessPredictedEvents(vEvents, 101, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(vPlayed, (std::vector<int>{1, 4, 3}));
}

TEST(QmPredictedEventQueue, ProcessingKeepsNewlyHandledOldEventsUntilTheNextPass)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents = {MakeEvent(1, 1)};
	int Played = 0;
	auto Play = [&Played](const CGameWorld::CPredictedEvent &) {
		++Played;
		return true;
	};

	QmProcessPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(Played, 1);
	ASSERT_EQ(vEvents.size(), 1u);
	EXPECT_TRUE(vEvents.front().m_Handled);

	QmProcessPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(Played, 1);
	EXPECT_TRUE(vEvents.empty());
}

TEST(QmPredictedEventQueue, ProcessingDiscardsRejectedEventsAndKeepsSurvivorsInOrder)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents = {
		MakeEvent(1, 100),
		MakeEvent(2, 101),
		MakeEvent(3, 100),
		MakeEvent(4, 100, true),
		MakeEvent(5, 100)};
	std::vector<int> vVisited;

	QmProcessPredictedEvents(vEvents, 100, SERVER_TICK_SPEED, [&vVisited](const CGameWorld::CPredictedEvent &Event) {
		vVisited.push_back(Event.m_Id);
		return Event.m_Id == 3;
	});

	EXPECT_EQ(vVisited, (std::vector<int>{1, 3, 5}));
	ASSERT_EQ(vEvents.size(), 3u);
	EXPECT_EQ(vEvents[0].m_Id, 2);
	EXPECT_FALSE(vEvents[0].m_Handled);
	EXPECT_EQ(vEvents[1].m_Id, 3);
	EXPECT_TRUE(vEvents[1].m_Handled);
	EXPECT_EQ(vEvents[2].m_Id, 4);
}

TEST(QmPredictedEventQueue, ProcessingCompactsLargeExpiredBacklogsWithoutReplayingHistory)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents;
	for(int Id = 0; Id < 100000; ++Id)
		vEvents.push_back(MakeEvent(Id, 1, true));
	vEvents.push_back(MakeEvent(100000, 1000, true));
	vEvents.push_back(MakeEvent(100001, 1000));
	vEvents.push_back(MakeEvent(100002, 1001));
	std::vector<int> vPlayed;

	QmProcessPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED, [&vPlayed](const CGameWorld::CPredictedEvent &Event) {
		vPlayed.push_back(Event.m_Id);
		return true;
	});

	EXPECT_EQ(vPlayed, (std::vector<int>{100001}));
	ASSERT_EQ(vEvents.size(), 3u);
	EXPECT_EQ(vEvents[0].m_Id, 100000);
	EXPECT_EQ(vEvents[1].m_Id, 100001);
	EXPECT_TRUE(vEvents[1].m_Handled);
	EXPECT_EQ(vEvents[2].m_Id, 100002);
	EXPECT_FALSE(vEvents[2].m_Handled);
}

TEST(QmPredictedEventQueue, EmptyAndExpiredQueuesAcceptNewEvents)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents;
	int Played = 0;
	auto Play = [&Played](const CGameWorld::CPredictedEvent &) {
		++Played;
		return true;
	};

	QmPruneHandledPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED);
	QmProcessPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(Played, 0);

	vEvents.push_back(MakeEvent(1, 1, true));
	QmPruneHandledPredictedEvents(vEvents, 1000, SERVER_TICK_SPEED);
	EXPECT_TRUE(vEvents.empty());

	vEvents.push_back(MakeEvent(2, 1001));
	QmProcessPredictedEvents(vEvents, 1001, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(Played, 1);
	ASSERT_EQ(vEvents.size(), 1u);
	EXPECT_TRUE(vEvents.front().m_Handled);
}
