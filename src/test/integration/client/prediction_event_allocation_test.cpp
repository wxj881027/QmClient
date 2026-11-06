#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/collision.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_world.h>

#include <limits>

namespace
{
	class PredictionEventAllocation : public ::testing::Test
	{
		const int m_OldSound = g_Config.m_SndEnable;
		const int m_OldPrediction = g_Config.m_ClPredictEvents;
		const int m_OldDebug = g_Config.m_DbgPredictEvents;

	protected:
		CPredictionTestWorld m_World;

		void SetUp() override
		{
			g_Config.m_SndEnable = 1;
			g_Config.m_ClPredictEvents = 1;
			g_Config.m_DbgPredictEvents = 0;
			m_World.m_WorldConfig.m_PredictEvents = true;
		}

		void TearDown() override
		{
			g_Config.m_SndEnable = m_OldSound;
			g_Config.m_ClPredictEvents = m_OldPrediction;
			g_Config.m_DbgPredictEvents = m_OldDebug;
		}

		struct SFailureResult
		{
			bool m_Threw = false;
			bool m_Triggered = false;
		};

		template<typename F>
		SFailureResult FailAllocation(F Operation)
		{
			SFailureResult Result;
			{
				CPredictionAllocationFailure Failure;
				try
				{
					Operation();
				}
				catch(...)
				{
					Result.m_Threw = true;
				}
				Result.m_Triggered = Failure.Triggered();
			}
			return Result;
		}

		void ExpectFailureContained(const SFailureResult &Result)
		{
			EXPECT_TRUE(Result.m_Triggered);
			EXPECT_FALSE(Result.m_Threw);
		}
	};
}

TEST_F(PredictionEventAllocation, RawAppendPreservesConfirmedIdentityWithoutPredictionGates)
{
	g_Config.m_ClPredictEvents = 0;
	m_World.m_WorldConfig.m_PredictEvents = false;
	CGameWorld::CPredictedEvent Event(NETEVENTTYPE_HAMMERHIT, vec2(10, 20), 3, 80, 4);
	Event.m_Handled = true;
	Event.m_ServerConfirmed = true;
	ASSERT_TRUE(m_World.TryAppendPredictedEvent(Event));
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	const auto &Stored = m_World.m_PredictedEvents.front();
	EXPECT_EQ(Stored.m_EventId, NETEVENTTYPE_HAMMERHIT);
	EXPECT_EQ(Stored.m_Pos, vec2(10, 20));
	EXPECT_EQ(Stored.m_Id, 3);
	EXPECT_EQ(Stored.m_Tick, 80);
	EXPECT_EQ(Stored.m_ExtraInfo, 4);
	EXPECT_TRUE(Stored.m_Handled);
	EXPECT_TRUE(Stored.m_ServerConfirmed);
}

TEST_F(PredictionEventAllocation, RawAppendFailureCanRetryWithoutPublishingPartialRecord)
{
	const CGameWorld::CPredictedEvent Event(NETEVENTTYPE_EXPLOSION, vec2(10, 20), 3, 80);
	bool Accepted = true;
	const auto Result = FailAllocation([&] { Accepted = m_World.TryAppendPredictedEvent(Event); });
	ExpectFailureContained(Result);
	EXPECT_FALSE(Accepted);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	ASSERT_TRUE(m_World.TryAppendPredictedEvent(Event));
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_Id, 3);
}

TEST_F(PredictionEventAllocation, FailedGrowthKeepsExistingRecordsAndStorage)
{
	CGameWorld::CPredictedEvent Old(NETEVENTTYPE_SOUNDWORLD, vec2(10, 20), -1, 80, 7);
	Old.m_Handled = true;
	Old.m_ServerConfirmed = true;
	auto &Events = m_World.m_PredictedEvents;
	Events.reserve(1);
	while(Events.size() < Events.capacity())
		Events.push_back(Old);
	const auto Count = Events.size();
	const auto *pData = Events.data();
	bool Accepted = true;
	const CGameWorld::CPredictedEvent New(NETEVENTTYPE_EXPLOSION, vec2(30, 40), 9, 100);
	const auto Result = FailAllocation([&] { Accepted = m_World.TryAppendPredictedEvent(New); });
	ExpectFailureContained(Result);
	EXPECT_FALSE(Accepted);
	EXPECT_EQ(Events.size(), Count);
	EXPECT_EQ(Events.capacity(), Count);
	EXPECT_EQ(Events.data(), pData);
	for(const auto &Event : Events)
	{
		EXPECT_EQ(Event.m_EventId, NETEVENTTYPE_SOUNDWORLD);
		EXPECT_EQ(Event.m_Pos, vec2(10, 20));
		EXPECT_EQ(Event.m_Id, -1);
		EXPECT_EQ(Event.m_Tick, 80);
		EXPECT_EQ(Event.m_ExtraInfo, 7);
		EXPECT_TRUE(Event.m_Handled);
		EXPECT_TRUE(Event.m_ServerConfirmed);
	}
}

TEST_F(PredictionEventAllocation, InvalidPositionIsRejectedBeforeAllocation)
{
	const CGameWorld::CPredictedEvent Event(NETEVENTTYPE_EXPLOSION, vec2(std::numeric_limits<float>::infinity(), 0), 1, 100);
	bool Accepted = true;
	const auto Result = FailAllocation([&] { Accepted = m_World.TryAppendPredictedEvent(Event); });
	EXPECT_FALSE(Result.m_Threw);
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_FALSE(Accepted);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
}

TEST_F(PredictionEventAllocation, SoundFailureDoesNotPreventLaterReplayFromEnqueuing)
{
	const auto Result = FailAllocation([&] { m_World.CreatePredictedSound(vec2(10, 20), 7, 3); });
	ExpectFailureContained(Result);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	m_World.CreatePredictedSound(vec2(10, 20), 7, 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_FALSE(m_World.m_PredictedEvents.front().m_Handled);
}

TEST_F(PredictionEventAllocation, PendingSoundCorrectionDoesNotAllocateOrDuplicate)
{
	m_World.CreatePredictedSound(vec2(10, 20), 7, 3);
	const auto Result = FailAllocation([&] { m_World.CreatePredictedSound(vec2(30, 40), 7, 3); });
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Threw);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_Pos, vec2(30, 40));
}

TEST_F(PredictionEventAllocation, PlayedSoundKeepsOriginalPositionOnCorrection)
{
	m_World.CreatePredictedSound(vec2(10, 20), 7, 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	m_World.m_PredictedEvents.front().m_Handled = true;
	m_World.CreatePredictedSound(vec2(30, 40), 7, 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_Pos, vec2(10, 20));
}

TEST_F(PredictionEventAllocation, OrdinaryEventsKeepGlobalAndWorldPredictionGates)
{
	g_Config.m_ClPredictEvents = 0;
	m_World.CreatePredictedExplosionEvent(vec2(10, 20), 3);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	g_Config.m_ClPredictEvents = 1;
	m_World.m_WorldConfig.m_PredictEvents = false;
	m_World.CreatePredictedExplosionEvent(vec2(10, 20), 3);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	m_World.m_WorldConfig.m_PredictEvents = true;
	m_World.CreatePredictedExplosionEvent(vec2(10, 20), 3);
	EXPECT_EQ(m_World.m_PredictedEvents.size(), 1u);
}

TEST_F(PredictionEventAllocation, HandledSoundFailureCanRetryWithPredictionGatesDisabled)
{
	g_Config.m_ClPredictEvents = 0;
	m_World.m_WorldConfig.m_PredictEvents = false;
	const auto Result = FailAllocation([&] { m_World.CreateHandledPredictedSound(vec2(10, 20), 7, 3); });
	ExpectFailureContained(Result);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	m_World.CreateHandledPredictedSound(vec2(10, 20), 7, 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_TRUE(m_World.m_PredictedEvents.front().m_Handled);
	EXPECT_FALSE(m_World.m_PredictedEvents.front().m_ServerConfirmed);
}

TEST_F(PredictionEventAllocation, HandledSoundDuplicateDoesNotAllocate)
{
	m_World.CreateHandledPredictedSound(vec2(10, 20), 7, 3);
	const auto Result = FailAllocation([&] { m_World.CreateHandledPredictedSound(vec2(30, 40), 7, 3); });
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Threw);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_Pos, vec2(10, 20));
}

TEST_F(PredictionEventAllocation, DisabledSoundSkipsOrdinaryAndHandledRecords)
{
	g_Config.m_SndEnable = 0;
	const auto Result = FailAllocation([&] {
		m_World.CreatePredictedSound(vec2(10, 20), 7, 3);
		m_World.CreateHandledPredictedSound(vec2(10, 20), 7, 3);
	});
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Threw);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
}

TEST_F(PredictionEventAllocation, ExplosionFailureDoesNotEscape)
{
	const auto Result = FailAllocation([&] { m_World.CreatePredictedExplosionEvent(vec2(10, 20), 3); });
	ExpectFailureContained(Result);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	m_World.CreatePredictedExplosionEvent(vec2(10, 20), 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_EventId, NETEVENTTYPE_EXPLOSION);
}

TEST_F(PredictionEventAllocation, HammerFailureDoesNotBlockCorrectedReplay)
{
	const auto Result = FailAllocation([&] { m_World.CreatePredictedHammerHitEvent(vec2(10, 20), 3, 4); });
	ExpectFailureContained(Result);
	m_World.CreatePredictedHammerHitEvent(vec2(30, 40), 3, 4);
	m_World.CreatePredictedHammerHitEvent(vec2(50, 60), 3, 4);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_Pos, vec2(50, 60));
	EXPECT_EQ(m_World.m_PredictedEvents.front().m_ExtraInfo, 4);
}

TEST_F(PredictionEventAllocation, DamageIndicatorBatchContinuesAfterOneAllocationFailure)
{
	const auto Result = FailAllocation([&] { m_World.CreatePredictedDamageIndEvent(vec2(10, 20), 0.0f, 3, 3); });
	ExpectFailureContained(Result);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 2u);
	m_World.CreatePredictedDamageIndEvent(vec2(10, 20), 0.0f, 3, 3);
	ASSERT_EQ(m_World.m_PredictedEvents.size(), 3u);
	for(const auto &Event : m_World.m_PredictedEvents)
		EXPECT_EQ(Event.m_EventId, NETEVENTTYPE_DAMAGEIND);
}

TEST_F(PredictionEventAllocation, PickupSoundFailureKeepsGrantedWeaponAndFinishesTick)
{
	CCollision Collision;
	m_World.Init(&Collision, m_World.TuningList(), nullptr);
	m_World.m_WorldConfig.m_IsDDRace = true;
	m_World.m_WorldConfig.m_PredictDDRace = true;
	CNetObj_Character Input{};
	Input.m_HookedPlayer = -1;
	Input.m_Weapon = WEAPON_GUN;
	auto *pCharacter = new CCharacter(&m_World, 0, &Input);
	m_World.InsertEntity(pCharacter);
	pCharacter->SetWeaponGot(WEAPON_SHOTGUN, false);
	CPickupData Data{};
	Data.m_Type = POWERUP_WEAPON;
	Data.m_Subtype = WEAPON_SHOTGUN;
	CPickup Pickup(&m_World, 1, &Data);
	const auto Result = FailAllocation([&] { Pickup.Tick(); });
	ExpectFailureContained(Result);
	EXPECT_TRUE(pCharacter->GetWeaponGot(WEAPON_SHOTGUN));
	EXPECT_EQ(pCharacter->GetWeaponAmmo(WEAPON_SHOTGUN), -1);
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
	// 同一物品不会因为声音登记失败而重复授予，也不会补造一次拾取。
	Pickup.Tick();
	EXPECT_TRUE(m_World.m_PredictedEvents.empty());
}
