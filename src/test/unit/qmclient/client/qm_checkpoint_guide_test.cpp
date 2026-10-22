#include <game/client/components/qmclient/checkpoint_guide_logic.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{
	struct SGuideMap
	{
		int m_Width;
		int m_Height;
		std::vector<QmMapProgress::STile> m_vTiles;

		SGuideMap(int Width, int Height) :
			m_Width(Width), m_Height(Height), m_vTiles((size_t)Width * Height, QmMapProgress::MakeTile(TILE_AIR)) {}

		void Set(int X, int Y, int Game, int TeleType = 0, int Number = 0, int Restrictions = 0)
		{
			m_vTiles[(size_t)(Y * m_Width + X)] = QmMapProgress::MakeTile(Game, TILE_AIR, TeleType, Number, Restrictions);
		}

		void Update(QmCheckpointGuide::CPlanner &Planner, int X, int Y) const
		{
			Planner.Update(m_Width, m_Height, Y * m_Width + X, [&](int Index) { return m_vTiles[(size_t)Index]; });
		}
	};
}

TEST(QmCheckpointGuide, SideBranchCheckpointDoesNotNeedConsecutiveNumber)
{
	SGuideMap Map(7, 3);
	Map.Set(3, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(27);
	Map.Update(Planner, 1, 1);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 2);
	ASSERT_FALSE(Planner.Result().m_vRoute.empty());
	EXPECT_EQ(Planner.Result().m_vRoute.front(), 8);
	EXPECT_EQ(Planner.Result().m_vRoute.back(), 3);
}

TEST(QmCheckpointGuide, RouteUsesTheOpeningInsteadOfCrossingTheWall)
{
	SGuideMap Map(5, 3);
	Map.Set(2, 0, TILE_SOLID);
	Map.Set(2, 1, TILE_NOHOOK);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 11);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	const auto &Route = Planner.Result().m_vRoute;
	ASSERT_FALSE(Route.empty());
	EXPECT_EQ(Route.size(), 9u);
	EXPECT_NE(std::find(Route.begin(), Route.end(), 12), Route.end());
	EXPECT_EQ(std::find(Route.begin(), Route.end(), 2), Route.end());
	EXPECT_EQ(std::find(Route.begin(), Route.end(), 7), Route.end());
}

TEST(QmCheckpointGuide, DeathTileBlocksASeparatedCheckpoint)
{
	SGuideMap Map(5, 1);
	Map.Set(2, 0, TILE_DEATH);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 1);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
	EXPECT_TRUE(Planner.Result().m_vRoute.empty());
}

TEST(QmCheckpointGuide, SafeDetourWinsOverTheShorterFrozenRoute)
{
	SGuideMap Map(5, 2);
	Map.Set(1, 0, TILE_FREEZE);
	Map.Set(2, 0, TILE_FREEZE);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 8);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	const auto &Route = Planner.Result().m_vRoute;
	ASSERT_FALSE(Route.empty());
	EXPECT_EQ(Route.size(), 7u);
	EXPECT_EQ(std::find(Route.begin(), Route.end(), 1), Route.end());
	EXPECT_EQ(std::find(Route.begin(), Route.end(), 2), Route.end());
}

TEST(QmCheckpointGuide, StopperPreventsGuidanceOnlyInItsBlockedDirection)
{
	SGuideMap Map(5, 1);
	Map.Set(2, 0, TILE_AIR, 0, 0, CANTMOVE_RIGHT);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 7);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
	Map.Set(2, 0, TILE_AIR, 0, 0, CANTMOVE_LEFT);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 7);
}

TEST(QmCheckpointGuide, ApproachingCheckpointReturnStrengthensTheReminder)
{
	SGuideMap Map(15, 1);
	Map.Set(0, 0, TILE_AIR, TILE_TELECHECK, 19);
	Map.Set(14, 0, TILE_AIR, TILE_TELECHECKINEVIL);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(1);
	Map.Update(Planner, 7, 0);
	ASSERT_EQ(Planner.Result().m_Checkpoint, 19);
	EXPECT_FALSE(Planner.Result().m_Warning);
	Map.Update(Planner, 8, 0);
	EXPECT_TRUE(Planner.Result().m_Warning);
}

TEST(QmCheckpointGuide, OrdinaryTeleporterDoesNotRaiseACheckpointWarning)
{
	SGuideMap Map(7, 1);
	Map.Set(0, 0, TILE_AIR, TILE_TELECHECK, 4);
	Map.Set(6, 0, TILE_AIR, TILE_TELEINEVIL, 10);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 4, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 4);
	EXPECT_FALSE(Planner.Result().m_Warning);
}

TEST(QmCheckpointGuide, CrossingTheTargetCheckpointFirstDoesNotWarnAboutThePortalBeyondIt)
{
	SGuideMap Map(7, 1);
	Map.Set(3, 0, TILE_AIR, TILE_TELECHECK, 5);
	Map.Set(5, 0, TILE_AIR, TILE_TELECHECKIN);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 5);
	EXPECT_FALSE(Planner.Result().m_Warning);
}

TEST(QmCheckpointGuide, LocalRouteCannotWalkThroughATeleportEntrance)
{
	for(const int Type : {TILE_TELEIN, TILE_TELEINEVIL, TILE_TELECHECKIN, TILE_TELECHECKINEVIL})
	{
		SCOPED_TRACE(Type);
		SGuideMap Map(5, 1);
		Map.Set(2, 0, TILE_AIR, Type, 3);
		Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 9);
		QmCheckpointGuide::CPlanner Planner;
		Planner.ObserveCheckpoint(0);
		Map.Update(Planner, 0, 0);
		EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
	}
}

TEST(QmCheckpointGuide, CurrentCheckpointIsNotATarget)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 9);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(9);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
}

TEST(QmCheckpointGuide, TouchingTargetKeepsGuidanceUntilCheckpointIsConfirmed)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 9);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(2);
	Map.Update(Planner, 4, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
	Planner.ObserveCheckpoint(9);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
	Map.Update(Planner, 4, 0);
	EXPECT_TRUE(Planner.Result().m_vRoute.empty());
}

TEST(QmCheckpointGuide, CollectedOldCheckpointIsNotSuggestedAgain)
{
	SGuideMap Map(7, 1);
	Map.Set(0, 0, TILE_AIR, TILE_TELECHECK, 2);
	Map.Set(6, 0, TILE_AIR, TILE_TELECHECK, 19);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(2);
	Planner.ObserveCheckpoint(7);
	Map.Update(Planner, 1, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 19);
}

TEST(QmCheckpointGuide, ResetAllowsTheSameCheckpointInANewLife)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(2);
	Planner.Reset();
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 2);
}

TEST(QmCheckpointGuide, MainAndDummyKeepIndependentCheckpointHistory)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Main;
	QmCheckpointGuide::CPlanner Dummy;
	Main.ObserveCheckpoint(2);
	Dummy.ObserveCheckpoint(0);
	Map.Update(Main, 0, 0);
	Map.Update(Dummy, 0, 0);
	EXPECT_EQ(Main.Result().m_TargetIndex, -1);
	EXPECT_EQ(Dummy.Result().m_Checkpoint, 2);
}

TEST(QmCheckpointGuide, UnknownCheckpointStateDoesNotGuessATarget)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Planner;
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
}

TEST(QmCheckpointGuide, LeavingTheRegionClearsTheOldRoute)
{
	SGuideMap Map(80, 1);
	Map.Set(2, 0, TILE_AIR, TILE_TELECHECK, 8);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	ASSERT_EQ(Planner.Result().m_Checkpoint, 8);
	Map.Update(Planner, 79, 0);
	EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
	EXPECT_FALSE(Planner.Result().m_Warning);
	EXPECT_TRUE(Planner.Result().m_vRoute.empty());
}

TEST(QmCheckpointGuide, LargeMapReadsOnlyTheBoundedLocalRegion)
{
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	constexpr int Width = 10000;
	constexpr int Start = Width * 500 + 500;
	int Reads = 0;
	Planner.Update(Width, 1000, Start, [&](int Index) {
		++Reads;
		return QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, Index == Start + 2 ? TILE_TELECHECK : 0, 7);
	});
	EXPECT_EQ(Planner.Result().m_Checkpoint, 7);
	EXPECT_LE(Reads, (QmCheckpointGuide::SEARCH_RADIUS * 2 + 1) * (QmCheckpointGuide::SEARCH_RADIUS * 2 + 1));
}

TEST(QmCheckpointGuide, InvalidMapAndStartDoNotReadTilesOrRetainRoute)
{
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	const auto Read = [](int) {
		ADD_FAILURE() << "invalid map must not be read";
		return QmMapProgress::MakeTile(TILE_AIR);
	};
	Planner.Update(0, 1, 0, Read);
	Planner.Update(1, 1, -1, Read);
	Planner.Update(1, 1, 1, Read);
	Planner.Update(100000, 100000, 0, Read);
	EXPECT_TRUE(Planner.Result().m_vRoute.empty());
}

TEST(QmCheckpointGuide, SimilarCostKeepsTheSelectedCheckpointStable)
{
	SGuideMap Map(11, 1);
	Map.Set(0, 0, TILE_AIR, TILE_TELECHECK, 2);
	Map.Set(10, 0, TILE_AIR, TILE_TELECHECK, 9);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 4, 0);
	ASSERT_EQ(Planner.Result().m_Checkpoint, 2);
	Map.Update(Planner, 6, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 2);
	Map.Update(Planner, 7, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
}

TEST(QmCheckpointGuide, ResetToNoCheckpointClearsPreviousCollectionHistory)
{
	SGuideMap Map(5, 1);
	Map.Set(4, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(2);
	Planner.ObserveCheckpoint(9);
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 2);
}

TEST(QmCheckpointGuide, ANewCheckpointStopsTheRouteBeforeALaterCheckpoint)
{
	SGuideMap Map(7, 1);
	Map.Set(2, 0, TILE_AIR, TILE_TELECHECK, 9);
	Map.Set(6, 0, TILE_AIR, TILE_TELECHECK, 2);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(0);
	Map.Update(Planner, 0, 0);
	ASSERT_EQ(Planner.Result().m_Checkpoint, 9);
	ASSERT_FALSE(Planner.Result().m_vRoute.empty());
	EXPECT_EQ(Planner.Result().m_vRoute.back(), 2);
	Planner.ObserveCheckpoint(9);
	Map.Update(Planner, 2, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 2);
}

TEST(QmCheckpointGuide, ReturningToAnOldCheckpointAllowsRestoringTheLaterCheckpoint)
{
	SGuideMap Map(7, 1);
	Map.Set(0, 0, TILE_AIR, TILE_TELECHECK, 2);
	Map.Set(6, 0, TILE_AIR, TILE_TELECHECK, 9);
	QmCheckpointGuide::CPlanner Planner;
	Planner.ObserveCheckpoint(2);
	Planner.ObserveCheckpoint(9);
	Map.Update(Planner, 3, 0);
	ASSERT_EQ(Planner.Result().m_TargetIndex, -1);
	Planner.ObserveCheckpoint(2);
	Map.Update(Planner, 3, 0);
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
}
