#include <game/client/prediction/gameworld.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	using CEvent = CGameWorld::CPredictedEvent;
	constexpr int MinTick = std::numeric_limits<int>::min();
	constexpr int MaxTick = std::numeric_limits<int>::max();
	CEvent Sound(int Tick) { return CEvent(NETEVENTTYPE_SOUNDWORLD, vec2(10, 20), 1, Tick, 3); }
	CEvent Hammer(int Tick) { return CEvent(NETEVENTTYPE_HAMMERHIT, vec2(10, 20), 1, Tick, 2); }
}

TEST(QmPredictedEventMatching, GeneralConfirmationDoesNotConsumeExtremeOldTick)
{
	std::vector<CEvent> Events{Sound(MinTick)};
	bool Unplayed = false;
	EXPECT_FALSE(QmCheckPredictedEventHandled(Events, Sound(MaxTick), &Unplayed));
	EXPECT_FALSE(Unplayed);
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
}

TEST(QmPredictedEventMatching, GeneralConfirmationRejectsExtremeFutureTick)
{
	std::vector<CEvent> Events{Sound(MaxTick)};
	EXPECT_FALSE(QmCheckPredictedEventHandled(Events, Sound(MinTick)));
	EXPECT_FALSE(Events[0].m_Handled);
}

TEST(QmPredictedEventMatching, GeneralConfirmationKeepsSpatialPriorityBeforeRecency)
{
	auto Near = Sound(100);
	auto Recent = Sound(102);
	Recent.m_Pos.x += 80;
	std::vector<CEvent> Events{Recent, Near};
	EXPECT_TRUE(QmCheckPredictedEventHandled(Events, Sound(103)));
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_TRUE(Events[1].m_Handled);
	EXPECT_EQ(Events[1].m_Id, -1);
	EXPECT_EQ(Events[1].m_Tick, 103);
}

TEST(QmPredictedEventMatching, GeneralConfirmationKeepsInclusiveWindowAtBothIntegerEnds)
{
	for(const int ConfirmationTick : {MinTick + 3 * SERVER_TICK_SPEED, MaxTick})
	{
		SCOPED_TRACE(ConfirmationTick);
		std::vector<CEvent> Events{Sound(ConfirmationTick - 3 * SERVER_TICK_SPEED)};
		EXPECT_TRUE(QmCheckPredictedEventHandled(Events, Sound(ConfirmationTick)));
		EXPECT_TRUE(Events[0].m_Handled);
		EXPECT_TRUE(Events[0].m_ServerConfirmed);
		Events = {Sound(ConfirmationTick - 3 * SERVER_TICK_SPEED)};
		if(ConfirmationTick < MaxTick)
			EXPECT_FALSE(QmCheckPredictedEventHandled(Events, Sound(ConfirmationTick + 1)));
	}
}

TEST(QmPredictedEventMatching, ConfirmedBarrierStillExpiresAfterOneTickNearUpperBoundary)
{
	auto Existing = Sound(MaxTick - 2);
	Existing.m_Handled = true;
	Existing.m_ServerConfirmed = true;
	std::vector<CEvent> Events{Existing};
	EXPECT_TRUE(QmCheckPredictedEventHandled(Events, Sound(MaxTick - 1)));
	EXPECT_FALSE(QmCheckPredictedEventHandled(Events, Sound(MaxTick)));
}

TEST(QmPredictedEventMatching, StrictHammerDoesNotConsumeExtremeOldTick)
{
	std::vector<CEvent> Events{Hammer(MinTick)};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(Events, Hammer(MaxTick)));
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
}

TEST(QmPredictedEventMatching, StrictHammerKeepsInclusiveWindowAndOneTickConfirmationBarrier)
{
	std::vector<CEvent> Events{Hammer(MaxTick - SERVER_TICK_SPEED)};
	bool Unplayed = false;
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(Events, Hammer(MaxTick), &Unplayed));
	EXPECT_TRUE(Unplayed);
	EXPECT_TRUE(Events[0].m_Handled);
	EXPECT_EQ(Events[0].m_Tick, MaxTick);
	Events = {Hammer(MinTick)};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(Events, Hammer(MinTick + SERVER_TICK_SPEED + 1)));
	Events = {Hammer(MaxTick - 2)};
	Events[0].m_ServerConfirmed = true;
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(Events, Hammer(MaxTick)));
}

TEST(QmPredictedEventMatching, LooseHammerRejectsExtremeOldTickAndAcceptsRecentCandidate)
{
	std::vector<CEvent> Events{Hammer(MinTick), Hammer(MaxTick - 1)};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandledLoose(Events, 1, vec2(10, 20), MaxTick));
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
	EXPECT_TRUE(Events[1].m_ServerConfirmed);
	Events = {Hammer(MaxTick)};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandledLoose(Events, 1, vec2(10, 20), MinTick));
}

TEST(QmPredictedEventMatching, GeneralCreationDoesNotReuseExtremeOldConfirmation)
{
	auto Existing = Sound(MinTick);
	Existing.m_Handled = true;
	Existing.m_ServerConfirmed = true;
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Existing, Sound(MaxTick)));
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Sound(MaxTick), Sound(MinTick)));
}

TEST(QmPredictedEventMatching, GeneralCreationKeepsCorrectedSoundIdentityAndShortBarrier)
{
	auto Existing = Sound(MaxTick - 1);
	auto Corrected = Existing;
	Corrected.m_Pos = vec2(1000, 2000);
	EXPECT_TRUE(QmPredictedEventMatchesForCreation(Existing, Corrected));
	Existing.m_Id = -1;
	Existing.m_ServerConfirmed = true;
	EXPECT_TRUE(QmPredictedEventMatchesForCreation(Existing, Sound(MaxTick)));
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Existing, Sound(MaxTick - 2)));
}

TEST(QmPredictedEventMatching, HammerCreationKeepsAttackerTargetTickIdentityAcrossPositionCorrection)
{
	auto Existing = Hammer(100);
	auto Corrected = Existing;
	Corrected.m_Pos = vec2(500, 600);
	EXPECT_TRUE(QmPredictedHammerHitMatchesForCreation(Existing, Corrected));
	Corrected.m_ExtraInfo = 3;
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Corrected));
	Corrected = Existing;
	Corrected.m_Id = 2;
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Corrected));
}

TEST(QmPredictedEventMatching, HammerCreationHandledBarrierRemainsOneTickAndPositionBound)
{
	auto Existing = Hammer(MaxTick - 2);
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Hammer(MaxTick - 1)));
	Existing.m_Handled = true;
	EXPECT_TRUE(QmPredictedHammerHitMatchesForCreation(Existing, Hammer(MaxTick - 1)));
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Hammer(MaxTick)));
	auto Moved = Hammer(MaxTick - 1);
	Moved.m_Pos.x += 1;
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Moved));
}

TEST(QmPredictedEventMatching, HammerCreationRejectsExtremeTickAndInvalidPositionWithoutChangingEvents)
{
	auto Existing = Hammer(MinTick);
	Existing.m_Handled = true;
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Hammer(MaxTick)));
	auto Invalid = Existing;
	Invalid.m_Pos.x = std::numeric_limits<float>::infinity();
	EXPECT_FALSE(QmPredictedHammerHitMatchesForCreation(Existing, Invalid));
	EXPECT_EQ(Existing.m_Tick, MinTick);
	EXPECT_FALSE(Existing.m_ServerConfirmed);
}
