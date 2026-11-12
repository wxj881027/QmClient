// 只替换地图存储；起点采样、快照解包和落水统计均执行生产实现。
#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/gores_drown_observation.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_collision_map.h>

#include <cmath>
#include <limits>

namespace
{
	class QmGoresDrownObservation : public ::testing::Test
	{
	protected:
		CPredictionCollisionMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		CQmGoresDrownTracker m_Tracker;
		const vec2 m_Start{48.0f, 48.0f};
		const vec2 m_OutsideStart{80.0f, 48.0f};

		void SetUp() override
		{
			m_Map.SetGameTile(0, 1, TILE_START);
			m_Map.SetGameTile(1, 1, TILE_START);
			m_Map.SetGameTile(15, 1, TILE_START);
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetTileIndex(m_Collision.GetPureMapIndex(m_Start)), TILE_START);
		}
		bool Observe(vec2 Position, bool Frozen = false, bool Hooking = false, bool HasCharacter = true, bool Active = true)
		{
			return QmObserveGoresDrownPlayer(m_Tracker, m_Collision, 1, Active, "player", "clan", 1, HasCharacter, Frozen, Hooking, Position, true);
		}
		bool SeedOneFall()
		{
			return Observe(m_OutsideStart) && Observe(m_OutsideStart, true) && m_Tracker.Count(1) == 1;
		}
	};
}

TEST_F(QmGoresDrownObservation, NativeStartDepartureClearsOnlyAfterLeavingAndAllowsTheNewFirstFall)
{
	ASSERT_TRUE(SeedOneFall());
	ASSERT_TRUE(Observe(m_Start));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_Start));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_OutsideStart));
	EXPECT_EQ(m_Tracker.Count(1), 0);
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
}

TEST_F(QmGoresDrownObservation, AcceptedMaximumSnapshotOnEitherAxisKeepsTheArmedFallUntilAValidObservation)
{
	for(const bool LargeX : {false, true})
	{
		SCOPED_TRACE(LargeX);
		m_Tracker.Reset();
		ASSERT_TRUE(Observe(m_OutsideStart));
		CNetObj_Character Character{};
		Character.m_X = LargeX ? std::numeric_limits<int>::max() : 80;
		Character.m_Y = LargeX ? 48 : std::numeric_limits<int>::max();
		Character.m_HookedPlayer = -1;
		CUnpacker Unpacker;
		Unpacker.Reset(&Character, sizeof(Character));
		CNetObjHandler Handler;
		const auto *pCharacter = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
		ASSERT_NE(pCharacter, nullptr);
		EXPECT_FALSE(Observe(vec2(static_cast<float>(pCharacter->m_X), static_cast<float>(pCharacter->m_Y)), true));
		EXPECT_EQ(m_Tracker.Count(1), 0);
		ASSERT_TRUE(Observe(m_OutsideStart, true));
		EXPECT_EQ(m_Tracker.Count(1), 1);
	}
}

TEST_F(QmGoresDrownObservation, UnsafeObservationKeepsTheCountAndStartBaselineUntilAValidDeparture)
{
	ASSERT_TRUE(SeedOneFall());
	ASSERT_TRUE(Observe(m_Start));
	const vec2 Invalid(static_cast<float>(std::numeric_limits<int>::max()), 48.0f);
	EXPECT_FALSE(Observe(Invalid, false, true));
	EXPECT_FALSE(Observe(Invalid, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_Start));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_OutsideStart));
	EXPECT_EQ(m_Tracker.Count(1), 0);
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
}

TEST_F(QmGoresDrownObservation, UnsafeObservationCannotRearmARescuedPlayerWithoutANormalNewHook)
{
	ASSERT_TRUE(SeedOneFall());
	ASSERT_TRUE(Observe(m_OutsideStart));
	EXPECT_FALSE(Observe(vec2(80.0f, static_cast<float>(std::numeric_limits<int>::max())), false, true));
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_OutsideStart));
	ASSERT_TRUE(Observe(m_OutsideStart, false, true));
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 2);
}

TEST_F(QmGoresDrownObservation, RepresentableExtremePositionsKeepNativeEdgeClampingAndStartDetection)
{
	const float Inf = std::numeric_limits<float>::infinity();
	const float Maximum = std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), -Inf);
	for(const vec2 Position : {vec2(static_cast<float>(std::numeric_limits<int>::min()), 48.0f), vec2(Maximum, 48.0f)})
	{
		SCOPED_TRACE(Position.x);
		m_Tracker.Reset();
		ASSERT_TRUE(SeedOneFall());
		ASSERT_TRUE(Observe(Position));
		EXPECT_EQ(m_Tracker.Count(1), 1);
		ASSERT_TRUE(Observe(m_OutsideStart));
		EXPECT_EQ(m_Tracker.Count(1), 0);
		ASSERT_TRUE(Observe(m_OutsideStart, true));
		EXPECT_EQ(m_Tracker.Count(1), 1);
	}
}

TEST_F(QmGoresDrownObservation, FractionalPositionKeepsTheNativeRoundingAtTheStartTileBoundary)
{
	ASSERT_TRUE(SeedOneFall());
	ASSERT_TRUE(Observe(vec2(63.49f, 48.0f)));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(vec2(63.5f, 48.0f)));
	EXPECT_EQ(m_Tracker.Count(1), 0);
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
}

TEST_F(QmGoresDrownObservation, MissingCharacterStillPreservesTheCountAndRebuildsItsBaseline)
{
	ASSERT_TRUE(SeedOneFall());
	const vec2 UnusedPosition(static_cast<float>(std::numeric_limits<int>::max()), 48.0f);
	ASSERT_TRUE(Observe(UnusedPosition, false, false, false));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	ASSERT_TRUE(Observe(m_OutsideStart));
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 1);
	ASSERT_TRUE(Observe(m_OutsideStart));
	ASSERT_TRUE(Observe(m_OutsideStart, false, true));
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 2);
}

TEST_F(QmGoresDrownObservation, InactivePlayerStillClearsTheSlotWithoutSamplingUnusedCoordinates)
{
	ASSERT_TRUE(SeedOneFall());
	const vec2 UnusedPosition(80.0f, static_cast<float>(std::numeric_limits<int>::max()));
	ASSERT_TRUE(Observe(UnusedPosition, false, false, true, false));
	EXPECT_EQ(m_Tracker.Count(1), 0);
	ASSERT_TRUE(Observe(m_OutsideStart, true));
	EXPECT_EQ(m_Tracker.Count(1), 0);
}

TEST_F(QmGoresDrownObservation, UntrackedTeamsClearExistingCountsWithoutSamplingUnsafeCoordinates)
{
	struct SFilter
	{
		int m_Team;
		bool m_IncludeTeamZero;
	};
	const SFilter aFilters[] = {{TEAM_FLOCK, false}, {-1, true}, {NUM_DDRACE_TEAMS, true}};
	const vec2 UnusedPosition(static_cast<float>(std::numeric_limits<int>::max()), 48.0f);
	for(const auto &Filter : aFilters)
	{
		SCOPED_TRACE(Filter.m_Team);
		m_Tracker.Reset();
		ASSERT_TRUE(SeedOneFall());
		ASSERT_TRUE(QmObserveGoresDrownPlayer(m_Tracker, m_Collision, 1, true, "player", "clan", Filter.m_Team,
			true, true, false, UnusedPosition, Filter.m_IncludeTeamZero));
		EXPECT_EQ(m_Tracker.Count(1), 0);
		ASSERT_TRUE(Observe(m_OutsideStart, true));
		EXPECT_EQ(m_Tracker.Count(1), 0);
	}
}
