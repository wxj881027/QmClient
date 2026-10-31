#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/predicted_event_queue.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

namespace
{
	using CEvent = CGameWorld::CPredictedEvent;
	template<typename T>
	std::optional<CEvent> SecureEvent(int Type, const T &Input, int Tick = 100, int ExtraInfo = -1)
	{
		CUnpacker Unpacker;
		Unpacker.Reset(&Input, sizeof(Input));
		CNetObjHandler Handler;
		const auto *pEvent = static_cast<const CNetEvent_Common *>(Handler.SecureUnpackObj(Type, &Unpacker));
		if(!pEvent)
			return std::nullopt;
		return CEvent(Type, vec2(pEvent->m_X, pEvent->m_Y), -1, Tick, ExtraInfo);
	}
}

TEST(PredictedEventInputs, SecureExplosionMaximumIsInvalidAndNeverReachesPlayback)
{
	CNetEvent_Explosion Input{};
	Input.m_X = std::numeric_limits<int>::max();
	auto Event = SecureEvent(NETEVENTTYPE_EXPLOSION, Input);
	ASSERT_TRUE(Event.has_value());
	EXPECT_FALSE(Event->HasValidPosition());
	std::vector<CEvent> Events{*Event};
	int Played = 0;
	QmProcessPredictedEvents(Events, 100, SERVER_TICK_SPEED, [&](const CEvent &) {
		++Played;
		return true;
	});
	EXPECT_EQ(Played, 0);
	EXPECT_TRUE(Events.empty());
}

TEST(PredictedEventInputs, SecureSoundKeepsRepresentableBoundaryAndPlaysOnce)
{
	CNetEvent_SoundWorld Input{};
	Input.m_X = std::numeric_limits<int>::min();
	Input.m_Y = static_cast<int>(std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), 0.0f));
	Input.m_SoundId = 0;
	auto Event = SecureEvent(NETEVENTTYPE_SOUNDWORLD, Input, 100, Input.m_SoundId);
	ASSERT_TRUE(Event.has_value());
	ASSERT_TRUE(Event->HasValidPosition());
	std::vector<CEvent> Events{*Event};
	int Played = 0;
	auto Play = [&](const CEvent &Observed) {
		++Played;
		EXPECT_EQ(Observed.m_Pos, vec2(Input.m_X, Input.m_Y));
		EXPECT_EQ(Observed.m_ExtraInfo, 0);
		return true;
	};
	QmProcessPredictedEvents(Events, 100, SERVER_TICK_SPEED, Play);
	QmProcessPredictedEvents(Events, 100, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(Played, 1);
	ASSERT_EQ(Events.size(), 1u);
	EXPECT_TRUE(Events[0].m_Handled);
}

TEST(PredictedEventInputs, SecureDamageEventPreservesOffMapPositionAndAngleIdentity)
{
	CNetEvent_DamageInd Input{};
	Input.m_X = -100000;
	Input.m_Y = 100000;
	Input.m_Angle = std::numeric_limits<int>::min();
	auto Event = SecureEvent(NETEVENTTYPE_DAMAGEIND, Input, 100, Input.m_Angle);
	ASSERT_TRUE(Event.has_value());
	ASSERT_TRUE(Event->HasValidPosition());
	EXPECT_EQ(Event->m_Pos, vec2(Input.m_X, Input.m_Y));
	EXPECT_EQ(Event->m_ExtraInfo, Input.m_Angle);
	std::vector<CEvent> Events{*Event};
	bool Unplayed = false;
	EXPECT_TRUE(QmCheckPredictedEventHandled(Events, *Event, &Unplayed));
	EXPECT_TRUE(Unplayed);
}

TEST(PredictedEventInputs, SecureHammerUsesActualMatchingWithoutConsumingAncientCandidate)
{
	CNetEvent_HammerHit Input{};
	Input.m_X = 30;
	Input.m_Y = 40;
	auto Confirmation = SecureEvent(NETEVENTTYPE_HAMMERHIT, Input, std::numeric_limits<int>::max(), 2);
	ASSERT_TRUE(Confirmation.has_value());
	Confirmation->m_Id = 1;
	std::vector<CEvent> Events{CEvent(NETEVENTTYPE_HAMMERHIT, vec2(30, 40), 1, std::numeric_limits<int>::min(), 2)};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(Events, *Confirmation));
	EXPECT_FALSE(Events[0].m_Handled);
	Events.push_back(CEvent(NETEVENTTYPE_HAMMERHIT, vec2(30, 40), 1, std::numeric_limits<int>::max() - 1, 2));
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(Events, *Confirmation));
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_TRUE(Events[1].m_Handled);
}

TEST(PredictedEventInputs, SecureCelebrationEventsRetainTheSamePositionBoundary)
{
	CNetEvent_Birthday Birthday{};
	Birthday.m_Y = std::numeric_limits<int>::max();
	auto Invalid = SecureEvent(NETEVENTTYPE_BIRTHDAY, Birthday);
	ASSERT_TRUE(Invalid.has_value());
	EXPECT_FALSE(Invalid->HasValidPosition());
	CNetEvent_Finish Finish{};
	Finish.m_X = -1234;
	auto Valid = SecureEvent(NETEVENTTYPE_FINISH, Finish);
	ASSERT_TRUE(Valid.has_value());
	EXPECT_TRUE(Valid->HasValidPosition());
	EXPECT_EQ(Valid->m_Pos, vec2(-1234, 0));
}

TEST(PredictedEventInputs, SecureMapSoundMaximumDoesNotBecomeAValidConfirmation)
{
	CNetEvent_MapSoundWorld Input{};
	Input.m_X = std::numeric_limits<int>::max();
	Input.m_SoundId = 0;
	auto Event = SecureEvent(NETEVENTTYPE_MAPSOUNDWORLD, Input, 100, 0);
	ASSERT_TRUE(Event.has_value());
	EXPECT_FALSE(Event->HasValidPosition());
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(*Event, *Event));
}
