#include <engine/shared/packer.h>

#include <game/client/components/hud_media_island_logic.h>
#include <game/collision.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_collision_map.h>

#include <limits>

namespace
{
	class QmSwitchCountdownTimingIntegration : public ::testing::Test
	{
	protected:
		CPredictionCollisionMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		SHudSwitchCountdownTracker m_Tracker;
		void SetUp() override
		{
			auto *pTiles = static_cast<CSwitchTile *>(m_Map.GetData(1));
			pTiles[17].m_Type = TILE_SWITCHTIMEDOPEN;
			pTiles[17].m_Number = 2;
			pTiles[17].m_Delay = 255;
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetWidth(), 16);
			m_Tracker.Reset();
		}
	};
}

TEST_F(QmSwitchCountdownTimingIntegration, NativeTimedTileKeepsItsFullDeadlineInSelectionAndProgress)
{
	const int Index = QmHudSwitchCountdownMapIndex(m_Collision, vec2(48, 48));
	ASSERT_EQ(Index, 17);
	ASSERT_EQ(m_Collision.GetSwitchType(Index), TILE_SWITCHTIMEDOPEN);
	const int Number = m_Collision.GetSwitchNumber(Index);
	ASSERT_EQ(Number, 2);
	constexpr int Now = std::numeric_limits<int>::max();
	ASSERT_TRUE(m_Tracker.Track(1, Number, Now, m_Collision.GetSwitchDelay(Index), 50, 7, 1));
	const int64_t End = m_Tracker.m_aaEndTick[1][Number];
	EXPECT_EQ(End, INT64_C(2147496398));
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(End, Now, 50), 256u);
	const SHudSwitchCountdownEntry Entry{1, Number, 7, 1, Now, End, Now};
	SHudSwitchCountdownEntry Selected{};
	ASSERT_EQ(QmHudSelectLatestSwitchCountdowns(&Entry, 1, &Selected, 1), 1);
	EXPECT_EQ(Selected.m_EndTick, End);
	const SHudMediaIslandCountdownInput Input{EHudMediaIslandCountdownType::SWITCH, Number, Now, End, End - Now};
	EXPECT_FLOAT_EQ(QmHudMediaIslandCountdownProgress(Input, Now), 1);
}

TEST_F(QmSwitchCountdownTimingIntegration, AcceptedExtremeSnapshotPositionIsRejectedBeforeNativeMapRounding)
{
	CNetObj_Character Character{};
	Character.m_Tick = 7;
	Character.m_X = std::numeric_limits<int>::max();
	Character.m_Y = 48;
	Character.m_HookedPlayer = -1;
	CUnpacker Unpacker;
	Unpacker.Reset(&Character, sizeof(Character));
	CNetObjHandler Handler;
	const auto *pCharacter = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pCharacter, nullptr);
	ASSERT_TRUE(m_Tracker.Track(1, 2, 100, 3, 50, 7, 0));
	EXPECT_EQ(QmHudSwitchCountdownMapIndex(m_Collision, vec2(pCharacter->m_X, pCharacter->m_Y)), -1);
	EXPECT_EQ(m_Tracker.m_aaEndTick[1][2], 251);
	EXPECT_EQ(m_Tracker.m_aaTouchTick[1][2], 100);
}

TEST_F(QmSwitchCountdownTimingIntegration, ResetAfterWideDeadlineAllowsANormalNativeTileTrigger)
{
	ASSERT_TRUE(m_Tracker.Track(1, 2, std::numeric_limits<int>::max(), 255, 50, 7, 0));
	m_Tracker.Reset();
	const int Index = QmHudSwitchCountdownMapIndex(m_Collision, vec2(48, 48));
	ASSERT_EQ(Index, 17);
	ASSERT_TRUE(m_Tracker.Track(1, m_Collision.GetSwitchNumber(Index), 100, m_Collision.GetSwitchDelay(Index), 50, 8, 1));
	EXPECT_EQ(m_Tracker.m_aaEndTick[1][2], 12851);
	EXPECT_EQ(m_Tracker.m_aaClientId[1][2], 8);
	EXPECT_EQ(m_Tracker.m_aaConnection[1][2], 1);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(m_Tracker.m_aaEndTick[1][2], 12851, 50), 0u);
}
