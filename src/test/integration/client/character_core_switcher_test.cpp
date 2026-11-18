#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/teamscore.h>

#include <test/support/prediction_collision_map.h>

#include <gtest/gtest.h>

TEST(CharacterCoreSwitchers, SharedDoorOnlyRestrictsWorldsThatKnowItsSwitch)
{
	CPredictionCollisionMap Map;
	CLayers Layers;
	Layers.Init(&Map, false);
	CCollision Collision;
	Collision.Init(&Layers);
	ASSERT_EQ(Collision.m_HighestSwitchNumber, 3);
	Collision.SetDoorCollisionAt(80, 48, TILE_STOPA, 0, 3);
	CTeamsCore Teams;
	Teams.Team(0, TEAM_FLOCK);
	Teams.Team(1, TEAM_FLOCK);

	const auto HookVelocity = [&](int HighestSwitchNumber) {
		CWorldCore World;
		World.InitSwitchers(HighestSwitchNumber);
		CCharacterCore Core{};
		CCharacterCore Target{};
		Core.Init(&World, &Collision, &Teams);
		Target.Init(&World, &Collision, &Teams);
		Core.Reset();
		Target.Reset();
		Core.m_Id = 0;
		Target.m_Id = 1;
		World.m_apCharacters[0] = &Core;
		World.m_apCharacters[1] = &Target;
		Core.m_Pos = vec2(48, 48);
		Target.m_Pos = vec2(128, 48);
		Core.m_HookState = HOOK_GRABBED;
		Core.m_Input.m_Hook = 1;
		Core.SetHookedPlayer(1);

		Core.Tick(true);

		return Core.m_Vel.x;
	};

	EXPECT_GT(HookVelocity(2), 0.0f);
	EXPECT_FLOAT_EQ(HookVelocity(3), 0.0f);
}
