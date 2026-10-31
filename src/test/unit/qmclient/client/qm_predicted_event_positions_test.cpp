#include <game/client/prediction/gameworld.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(QmPredictedEventPositions, ValidCoordinatesKeepTruncationAndIdentity)
{
	CGameWorld::CPredictedEvent Event(NETEVENTTYPE_SOUNDWORLD, vec2(-3.75f, 2.5f), 7, 31, 5);
	EXPECT_TRUE(Event.HasValidPosition());
	EXPECT_EQ(Event.m_Pos, vec2(-3.0f, 2.0f));
	EXPECT_EQ(Event.m_EventId, NETEVENTTYPE_SOUNDWORLD);
	EXPECT_EQ(Event.m_Id, 7);
	EXPECT_EQ(Event.m_Tick, 31);
	EXPECT_EQ(Event.m_ExtraInfo, 5);
	EXPECT_FALSE(Event.m_Handled);
	EXPECT_FALSE(Event.m_ServerConfirmed);
}

TEST(QmPredictedEventPositions, IntegerBoundaryFloatsAreValidatedBeforeConversion)
{
	const float Lower = static_cast<float>(std::numeric_limits<int>::min());
	const float RoundedUpper = static_cast<float>(std::numeric_limits<int>::max());
	const float Upper = std::nextafter(RoundedUpper, 0.0f);
	CGameWorld::CPredictedEvent Valid(NETEVENTTYPE_EXPLOSION, vec2(Lower, Upper), 1, 0);
	EXPECT_TRUE(Valid.HasValidPosition());
	EXPECT_EQ(Valid.m_Pos, vec2(Lower, Upper));
	for(const vec2 Pos : {vec2(RoundedUpper, 0), vec2(0, RoundedUpper), vec2(std::nextafter(Lower, -INFINITY), 0)})
	{
		const CGameWorld::CPredictedEvent Invalid(NETEVENTTYPE_EXPLOSION, Pos, 1, 0);
		EXPECT_FALSE(Invalid.HasValidPosition());
		EXPECT_EQ(Invalid.m_Pos, Pos);
	}
}

TEST(QmPredictedEventPositions, NonFiniteCoordinatesStayInvalid)
{
	for(float Value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		const CGameWorld::CPredictedEvent X(NETEVENTTYPE_HAMMERHIT, vec2(Value, 1), 2, 10, 3);
		const CGameWorld::CPredictedEvent Y(NETEVENTTYPE_HAMMERHIT, vec2(1, Value), 2, 10, 3);
		EXPECT_FALSE(X.HasValidPosition());
		EXPECT_FALSE(Y.HasValidPosition());
		EXPECT_FALSE(std::isfinite(X.m_Pos.x));
		EXPECT_FALSE(std::isfinite(Y.m_Pos.y));
	}
}

TEST(QmPredictedEventPositions, InvalidSoundCannotClaimSameTickIdentity)
{
	CGameWorld::CPredictedEvent Valid(NETEVENTTYPE_SOUNDWORLD, vec2(10, 20), 1, 100, 3);
	auto Invalid = Valid;
	Invalid.m_Pos.x = std::numeric_limits<float>::infinity();
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Valid, Invalid));
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Invalid, Valid));
	EXPECT_FALSE(QmPredictedEventMatchesForCreation(Invalid, Invalid));
}

TEST(QmPredictedEventPositions, InvalidConfirmationDoesNotConsumeValidEvent)
{
	CGameWorld::CPredictedEvent Existing(NETEVENTTYPE_EXPLOSION, vec2(10, 20), 1, 100);
	std::vector<CGameWorld::CPredictedEvent> Events{Existing};
	auto Confirmation = Existing;
	Confirmation.m_Pos.x = std::numeric_limits<float>::quiet_NaN();
	bool Unplayed = false;
	EXPECT_FALSE(QmCheckPredictedEventHandled(Events, Confirmation, &Unplayed));
	EXPECT_FALSE(Unplayed);
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
	EXPECT_TRUE(QmCheckPredictedEventHandled(Events, Existing, &Unplayed));
	EXPECT_TRUE(Unplayed);
}

TEST(QmPredictedEventPositions, HammerMatchingSkipsInvalidCandidateAndKeepsValidCandidate)
{
	CGameWorld::CPredictedEvent Valid(NETEVENTTYPE_HAMMERHIT, vec2(10, 20), 1, 100, 2);
	auto Invalid = Valid;
	Invalid.m_Pos.x = std::numeric_limits<float>::quiet_NaN();
	std::vector<CGameWorld::CPredictedEvent> Events{Invalid, Valid};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(Events, Valid));
	EXPECT_FALSE(Events[0].m_Handled);
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
	EXPECT_TRUE(Events[1].m_Handled);
	EXPECT_TRUE(Events[1].m_ServerConfirmed);
}

TEST(QmPredictedEventPositions, LooseHammerRejectsInvalidPositionWithoutConsumingCandidate)
{
	CGameWorld::CPredictedEvent Event(NETEVENTTYPE_HAMMERHIT, vec2(10, 20), 1, 100, 2);
	std::vector<CGameWorld::CPredictedEvent> Events{Event};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandledLoose(Events, 1, vec2(NAN, 20), 100));
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
	Events[0].m_Pos.x = NAN;
	EXPECT_FALSE(QmCheckPredictedHammerHitHandledLoose(Events, 1, Event.m_Pos, 100));
	EXPECT_FALSE(Events[0].m_ServerConfirmed);
}

TEST(QmPredictedEventPositions, DirectPositionMatchingRejectsInvalidPositionsAndKeepsNormalWindows)
{
	EXPECT_TRUE(QmPredictedEventPositionsMatch(NETEVENTTYPE_SOUNDWORLD, vec2(0, 0), vec2(96, 0)));
	EXPECT_FALSE(QmPredictedEventPositionsMatch(NETEVENTTYPE_SOUNDWORLD, vec2(0, 0), vec2(97, 0)));
	EXPECT_TRUE(QmPredictedEventPositionsMatch(NETEVENTTYPE_EXPLOSION, vec2(-100000, 100000), vec2(-100000, 100000)));
	EXPECT_FALSE(QmPredictedEventPositionsMatch(NETEVENTTYPE_HAMMERHIT, vec2(NAN, 0), vec2(NAN, 0)));
	EXPECT_FALSE(QmPredictedEventPositionsMatch(NETEVENTTYPE_DAMAGEIND, vec2(0, 0), vec2(INFINITY, 0)));
}
