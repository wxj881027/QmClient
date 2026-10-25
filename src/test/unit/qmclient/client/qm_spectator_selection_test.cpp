#include <game/client/components/qmclient/spectator_selection.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmClosestSpectator, EmptySelectionHasNoPlayer)
{
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), -1);
}

TEST(QmClosestSpectator, ChoosesNearestEuclideanPosition)
{
	QmSpectatorSelection::CClosest Closest(vec2(10.0f, 20.0f));
	Closest.Consider(3, vec2(13.0f, 24.0f));
	Closest.Consider(4, vec2(10.0f, 24.0f));
	EXPECT_EQ(Closest.SelectedId(), 4);
}

TEST(QmClosestSpectator, FractionalDistancesAreNotTruncatedToSameInteger)
{
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	Closest.Consider(1, vec2(10.9f, 0.0f));
	Closest.Consider(2, vec2(10.1f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), 2);
}

TEST(QmClosestSpectator, EqualDistanceKeepsFirstCandidate)
{
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	Closest.Consider(8, vec2(3.0f, 4.0f));
	Closest.Consider(2, vec2(-3.0f, -4.0f));
	EXPECT_EQ(Closest.SelectedId(), 8);
}

TEST(QmClosestSpectator, DistancesAboveIntMaximumStayOrdered)
{
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	Closest.Consider(1, vec2(4000000000.0f, 0.0f));
	Closest.Consider(2, vec2(3000000000.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), 2);
}

TEST(QmClosestSpectator, WidensBeforeOppositeExtremeCoordinatesAreSubtracted)
{
	const float Large = std::numeric_limits<float>::max();
	QmSpectatorSelection::CClosest Closest(vec2(-Large, -Large));
	Closest.Consider(1, vec2(Large, Large));
	Closest.Consider(2, vec2(0.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), 2);
}

TEST(QmClosestSpectator, InvalidCandidateDoesNotPreventLaterValidSelection)
{
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	Closest.Consider(1, vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f));
	Closest.Consider(2, vec2(std::numeric_limits<float>::infinity(), 0.0f));
	Closest.Consider(-1, vec2(0.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), -1);
	Closest.Consider(3, vec2(1.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), 3);
}

TEST(QmClosestSpectator, InvalidOriginDoesNotSelectAnArbitraryPlayer)
{
	QmSpectatorSelection::CClosest Closest(vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f));
	Closest.Consider(1, vec2(0.0f, 0.0f));
	EXPECT_EQ(Closest.SelectedId(), -1);
}

TEST(QmSpectatorRetarget, SelectedCurrentPlayerKeepsTarget)
{
	const bool aSelected[] = {true, false, true};
	EXPECT_FALSE(QmSpectatorSelection::NeedsRetarget(aSelected, 0));
	EXPECT_FALSE(QmSpectatorSelection::NeedsRetarget(aSelected, 2));
}

TEST(QmSpectatorRetarget, DeselectedCurrentPlayerRequestsNewTarget)
{
	const bool aSelected[] = {true, false, true};
	EXPECT_TRUE(QmSpectatorSelection::NeedsRetarget(aSelected, 1));
}

TEST(QmSpectatorRetarget, SentinelsAndOutOfRangeIdsRequestNewTarget)
{
	const bool aSelected[] = {true, true};
	for(int Id : {-1, -2, std::numeric_limits<int>::min(), 2, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Id);
		EXPECT_TRUE(QmSpectatorSelection::NeedsRetarget(aSelected, Id));
	}
}

TEST(QmSpectatorRetarget, EmptySelectionHasNoCurrentTarget)
{
	EXPECT_TRUE(QmSpectatorSelection::NeedsRetarget({}, 0));
}
