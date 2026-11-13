#include <game/client/components/qmclient/weapon_animation.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>

TEST(QmWeaponAnimationTuneZone, ExplicitOverrideWinsWithoutQueryingEvenWithAnInvalidPosition)
{
	int Queries = 0;
	auto Query = [&](vec2) {
		++Queries;
		return 3;
	};
	const vec2 Invalid(std::numeric_limits<float>::infinity(), 0);
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(true, 7, Invalid, Query), 7);
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(true, NUM_TUNEZONES + 10, Invalid, Query), NUM_TUNEZONES - 1);
	EXPECT_EQ(Queries, 0);
}

TEST(QmWeaponAnimationTuneZone, AbsentExtendedDataUsesTheMapInsteadOfTheUnusedOverride)
{
	int Queries = 0;
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(false, 7, vec2(10, 20), [&](vec2 Position) {
		++Queries;
		EXPECT_EQ(Position, vec2(10, 20));
		return 3;
	}),
		3);
	EXPECT_EQ(Queries, 1);
}

TEST(QmWeaponAnimationTuneZone, NegativeOverrideStillQueriesTheMap)
{
	int Queries = 0;
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(true, -1, vec2(10, 20), [&](vec2) {
		++Queries;
		return 8;
	}),
		8);
	EXPECT_EQ(Queries, 1);
}

TEST(QmWeaponAnimationTuneZone, InvalidMapPositionsReturnDefaultZoneWithoutQuerying)
{
	const float Max = static_cast<float>(std::numeric_limits<int>::max());
	for(vec2 Position : {vec2(Max, 0), vec2(0, Max), vec2(0, std::numeric_limits<float>::quiet_NaN()), vec2(std::numeric_limits<float>::infinity(), 0)})
	{
		int Queries = 0;
		EXPECT_EQ(QmResolveWeaponAnimationTuneZone(false, -1, Position, [&](vec2) {
			++Queries;
			return 8;
		}),
			0);
		EXPECT_EQ(Queries, 0);
	}
}

TEST(QmWeaponAnimationTuneZone, ValidMapQueryRetainsTheExistingZoneClamp)
{
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(false, -1, vec2(-100, -100), [](vec2) { return -1; }), 0);
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(false, -1, vec2(10, 20), [](vec2) { return NUM_TUNEZONES + 10; }), NUM_TUNEZONES - 1);
	EXPECT_EQ(QmResolveWeaponAnimationTuneZone(false, -1, vec2(10, 20), [](vec2) { return 9; }), 9);
}
