#include <game/collision.h>
#include <game/gamecore.h>
#include <game/teamscore.h>

#include <gtest/gtest.h>

#include <limits>

TEST(CharacterCore, FreshCoreHasNoOwnerOrHookedPlayer)
{
	CCharacterCore Core{};

	EXPECT_EQ(Core.m_Id, -1);
	EXPECT_EQ(Core.HookedPlayer(), -1);
}

TEST(CharacterCore, ReadingHookForPlayerZeroUpdatesBacklink)
{
	CWorldCore World;
	CCollision Collision;
	CCharacterCore Core{};
	CCharacterCore Target{};
	Core.Init(&World, &Collision);
	Target.Init(&World, &Collision);
	Core.m_Id = 1;
	Target.m_Id = 0;
	World.m_apCharacters[1] = &Core;
	World.m_apCharacters[0] = &Target;
	CNetObj_CharacterCore Snapshot{};
	Snapshot.m_HookedPlayer = 0;

	Core.Read(&Snapshot);

	EXPECT_EQ(Core.HookedPlayer(), 0);
	EXPECT_EQ(Target.m_AttachedPlayers.count(1), 1u);

	Core.Reset();

	EXPECT_TRUE(Target.m_AttachedPlayers.empty());
}

TEST(CharacterCore, GrabbedHookTickSaturatesAtIntMax)
{
	CCollision Collision;
	CCharacterCore Core{};
	Core.Init(nullptr, &Collision);
	Core.Reset();
	CNetObj_CharacterCore Snapshot{};
	Snapshot.m_HookState = HOOK_GRABBED;
	Snapshot.m_HookedPlayer = -1;
	Snapshot.m_HookTick = std::numeric_limits<int>::max() - 1;
	Core.Read(&Snapshot);

	Core.Tick(false);
	EXPECT_EQ(Core.m_HookTick, std::numeric_limits<int>::max());

	Core.Tick(false);
	EXPECT_EQ(Core.m_HookTick, std::numeric_limits<int>::max());
	EXPECT_EQ(Core.m_HookState, HOOK_GRABBED);
}

TEST(CharacterCore, MoveKeepsVelocityWithInvalidRamp)
{
	CCollision Collision;
	CCharacterCore Core{};
	Core.Init(nullptr, &Collision);
	Core.Reset();
	Core.m_Tuning.m_VelrampRange = 0.0f;
	Core.m_Vel = vec2(20.0f, 0.0f);

	Core.Move();

	EXPECT_FLOAT_EQ(Core.m_Vel.x, 20.0f);
	EXPECT_NEAR(Core.m_Pos.x, 20.0f, 0.00001f);
}

TEST(CharacterCore, ZeroLengthHookLeavesPlayerVelocitiesUnchanged)
{
	CWorldCore World;
	CCollision Collision;
	CTeamsCore Teams;
	CCharacterCore Core{};
	CCharacterCore Target{};
	Core.Init(&World, &Collision, &Teams);
	Target.Init(&World, &Collision, &Teams);
	Core.Reset();
	Target.Reset();
	Core.m_Id = 0;
	Target.m_Id = 1;
	Teams.Team(0, TEAM_FLOCK);
	Teams.Team(1, TEAM_FLOCK);
	World.m_apCharacters[0] = &Core;
	World.m_apCharacters[1] = &Target;
	Target.m_Pos = vec2(0, 64);
	Core.m_HookState = HOOK_GRABBED;
	Core.m_Tuning.m_HookLength = 0.0f;
	Core.SetHookedPlayer(1);

	Core.TickDeferred();

	EXPECT_EQ(Core.m_Vel, vec2(0, 0));
	EXPECT_EQ(Target.m_Vel, vec2(0, 0));
}
