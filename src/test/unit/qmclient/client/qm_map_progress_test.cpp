#include <game/client/components/qmclient/map_progress.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	QmMapProgress::CMap MakeProgressLine(int Width)
	{
		QmMapProgress::CMap Map(Width, 1);
		for(int Index = 0; Index < Width; ++Index)
			Map.SetTile(Index, QmMapProgress::MakeTile(Index == 0 ? TILE_START : (Index == Width - 1 ? TILE_FINISH : TILE_AIR)));
		return Map;
	}

	QmMapProgress::SEstimate MoveProgressPlayer(QmMapProgress::CPlayer &Player, const QmMapProgress::CMap &Map, int Index, int TeleCheckpoint = 0)
	{
		Player.Observe(Map.Tile(Index), Index);
		// 小地图也分多次推进，以覆盖换段和回传后重建距离场的行为。
		for(int Step = 0; Step < 128; ++Step)
			Player.Update(Map, Index, TeleCheckpoint, 128);
		return Player.Estimate();
	}
}

TEST(QmMapProgress, DragThroughFreezeUsesRouteLengthAndCanGoBack)
{
	auto Map = MakeProgressLine(11);
	for(int Index = 4; Index <= 6; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 0).m_Progress, 0.0f);
	const auto InWater = MoveProgressPlayer(Player, Map, 5);
	ASSERT_TRUE(InWater.m_Valid);
	EXPECT_FLOAT_EQ(InWater.m_Progress, 0.5f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 7).m_Progress, 0.7f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 3).m_Progress, 0.3f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 10).m_Progress, 1.0f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 0).m_Progress, 0.0f);
}

TEST(QmMapProgress, SafeDetourStillWinsButPenaltyDoesNotBecomeLength)
{
	QmMapProgress::CMap Map(5, 2);
	for(int Index = 0; Index < 10; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(TILE_AIR));
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(4, QmMapProgress::MakeTile(TILE_FINISH));
	Map.SetTile(1, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), 6);
	EXPECT_EQ(Field.Next(0), 5);
}

TEST(QmMapProgress, OneWetTileNeverWinsAgainstAnEntirelyDryDetour)
{
	QmMapProgress::CMap Map(3, 7);
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(1, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_FINISH));
	for(int Row = 1; Row < 6; ++Row)
		Map.SetTile(3 * Row + 1, QmMapProgress::MakeTile(TILE_SOLID));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Cost(0).m_Risk, 0);
	EXPECT_EQ(Field.Length(0), 14);
	EXPECT_EQ(Field.Next(0), 3);
}

TEST(QmMapProgress, StandingCorridorUnderDeathRemainsReachableAndRiskDoesNotStack)
{
	QmMapProgress::CMap Map(5, 3);
	for(int X = 0; X < 5; ++X)
	{
		Map.SetTile(X, QmMapProgress::MakeTile(TILE_AIR, TILE_DEATH));
		Map.SetTile(10 + X, QmMapProgress::MakeTile(TILE_NOHOOK));
	}
	Map.SetTile(5, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(7, QmMapProgress::MakeTile(TILE_AIR, TILE_FREEZE));
	Map.SetTile(9, QmMapProgress::MakeTile(TILE_FINISH));
	Map.Finalize();
	EXPECT_TRUE(Map.Tile(6).m_Grounded);
	EXPECT_TRUE(Map.Tile(6).m_NearDeath);
	EXPECT_FALSE(Map.Tile(7).m_Grounded);
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Cost(5).m_Risk, 4);
	EXPECT_EQ(Field.Cost(5).m_Water, 1);
	EXPECT_EQ(Field.Length(5), 4);
	EXPECT_EQ(Field.Next(5), 6);
	EXPECT_EQ(Field.Length(0), -1);
}

TEST(QmMapProgress, EqualExposurePrefersDryAndThenGroundedRoutes)
{
	for(bool Hazards : {false, true})
	{
		SCOPED_TRACE(Hazards);
		QmMapProgress::CMap Map(5, 6);
		for(int Index = 0; Index < Map.Size(); ++Index)
			Map.SetTile(Index, QmMapProgress::MakeTile(TILE_SOLID));
		for(int Index : {6, 7, 8, 16, 17, 18})
			Map.SetTile(Index, QmMapProgress::MakeTile(TILE_AIR));
		Map.SetTile(11, QmMapProgress::MakeTile(TILE_START));
		Map.SetTile(13, QmMapProgress::MakeTile(TILE_FINISH));
		if(Hazards)
		{
			// 同为一格风险时，让干路多绕两步，且下面没有地面，以区分各项优先级。
			Map.SetTile(7, QmMapProgress::MakeTile(TILE_FREEZE));
			Map.SetTile(17, QmMapProgress::MakeTile(TILE_SOLID));
			Map.SetTile(25, QmMapProgress::MakeTile(TILE_DEATH));
			for(int Index : {21, 22, 23, 26, 27, 28})
				Map.SetTile(Index, QmMapProgress::MakeTile(TILE_AIR));
		}
		Map.Finalize();
		QmMapProgress::CField Field;
		Field.Start(Map, 0);
		while(!Field.Complete())
			Field.Step(Map, 1);
		EXPECT_EQ(Field.Cost(11).m_Risk, Hazards ? 1 : 0);
		EXPECT_EQ(Field.Cost(11).m_Water, 0);
		EXPECT_EQ(Field.Length(11), Hazards ? 6 : 4);
		EXPECT_EQ(Field.Next(11), 16);
	}
}

TEST(QmMapProgress, StandingOnStopperAndDeathProximityAtMapEdge)
{
	QmMapProgress::CMap Map(3, 2);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_DEATH));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, 0, 0, CANTMOVE_DOWN));
	Map.Finalize();
	EXPECT_FALSE(Map.Tile(3).m_NearDeath);
	EXPECT_TRUE(Map.Tile(4).m_NearDeath);
	EXPECT_TRUE(Map.Tile(3).m_Grounded);
	EXPECT_FALSE(Map.Tile(4).m_Grounded);
}

TEST(QmMapProgress, DeepFreezeIsPassableButDeathAndSolidAreNot)
{
	for(const int Tile : {TILE_DFREEZE, TILE_LFREEZE, TILE_DEATH, TILE_SOLID, TILE_NOHOOK})
	{
		auto Map = MakeProgressLine(5);
		Map.SetTile(2, QmMapProgress::MakeTile(Tile));
		Map.Finalize();
		QmMapProgress::CField Field;
		Field.Start(Map, 0);
		while(!Field.Complete())
			Field.Step(Map, 1);
		EXPECT_EQ(Field.Length(0), Tile == TILE_DFREEZE || Tile == TILE_LFREEZE ? 4 : -1);
	}
}

TEST(QmMapProgress, DirectTeleportLinksRoomsWithoutCountingItsWorldDistance)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELEINEVIL, 17));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELEOUT, 17));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), 4);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(2), Field.Length(6));
}

TEST(QmMapProgress, TeleportLandingCountsRiskButNotDistance)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELEIN, 17));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_FREEZE, TILE_TELEOUT, 17));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Cost(0).m_Risk, 1);
	EXPECT_EQ(Field.Cost(2).m_Water, 1);
	EXPECT_EQ(Field.Length(2), Field.Length(6));
	EXPECT_EQ(Field.Next(2), 6);
}

TEST(QmMapProgress, CheckpointReturnUsesRecordedNumberAndFallsBackToEarlierExit)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(1, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKINEVIL, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 2));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 4);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(0), 4);
	Field.Start(Map, 1);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), -1);
}

TEST(QmMapProgress, CheckpointReturnWithoutExitGoesToSpawn)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(0), 4);
}

TEST(QmMapProgress, TimeCheckpointsDivideStagesAndBacktrackingDecreasesProgress)
{
	auto Map = MakeProgressLine(11);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_TIME_CHECKPOINT_FIRST));
	Map.SetTile(8, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST + 1));
	Map.Finalize();
	ASSERT_EQ(Map.CheckpointCount(), 2);
	QmMapProgress::CPlayer Player;
	MoveProgressPlayer(Player, Map, 0);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 2).m_Progress, 1.0f / 3.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 5).m_Progress, 0.5f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 8).m_Progress, 2.0f / 3.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 9).m_Progress, 5.0f / 6.0f, 0.001f);
	MoveProgressPlayer(Player, Map, 8);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 7).m_Progress, 11.0f / 18.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 3).m_Progress, 7.0f / 18.0f, 0.001f);
	MoveProgressPlayer(Player, Map, 2);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 1).m_Progress, 1.0f / 6.0f, 0.001f);
}

TEST(QmMapProgress, MissingAndReversedCheckpointOrderFallBackToWholeMap)
{
	for(const bool Reversed : {false, true})
	{
		auto Map = MakeProgressLine(11);
		Map.SetTile(2, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST + 1));
		if(Reversed)
			Map.SetTile(8, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST));
		Map.Finalize();
		QmMapProgress::CPlayer Player;
		MoveProgressPlayer(Player, Map, 0);
		const auto Estimate = MoveProgressPlayer(Player, Map, 5);
		ASSERT_TRUE(Estimate.m_Valid);
		EXPECT_FLOAT_EQ(Estimate.m_Progress, 0.5f);
	}
}

TEST(QmMapProgress, MidRunEnableCanEstimateWithoutHavingSeenStart)
{
	auto Map = MakeProgressLine(11);
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	const auto Estimate = MoveProgressPlayer(Player, Map, 6);
	ASSERT_TRUE(Estimate.m_Valid);
	EXPECT_FLOAT_EQ(Estimate.m_Progress, 0.6f);
}

TEST(QmMapProgress, MainAndDummyCheckpointStatesAreIndependent)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(1, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 2));
	Map.Finalize();
	QmMapProgress::CPlayer Main;
	QmMapProgress::CPlayer Dummy;
	EXPECT_TRUE(MoveProgressPlayer(Main, Map, 1, 2).m_Valid);
	EXPECT_FALSE(MoveProgressPlayer(Dummy, Map, 1, 0).m_Valid);
	EXPECT_FALSE(MoveProgressPlayer(Main, Map, 1, 0).m_Valid);
	EXPECT_TRUE(MoveProgressPlayer(Dummy, Map, 1, 2).m_Valid);
}

TEST(QmMapProgress, OneWayRestrictionsAreRespectedAndEmptyMapsStayUnknown)
{
	auto Map = MakeProgressLine(5);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, 0, 0, CANTMOVE_RIGHT));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), -1);
	QmMapProgress::CMap Empty(1, 1);
	Empty.Finalize();
	QmMapProgress::CPlayer Player;
	EXPECT_FALSE(MoveProgressPlayer(Player, Empty, 0).m_Valid);
}
