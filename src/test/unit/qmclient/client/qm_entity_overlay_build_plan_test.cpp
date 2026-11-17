#include <game/map/qm_entity_overlay_build_plan.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	using CPlan = CQmEntityOverlayBuildPlan;

	// 与生产层相同，只有计划回调被调用时才提交该类别的构建工作。
	std::vector<CPlan::ECategory> ScheduledCategories(const CPlan &Plan)
	{
		std::vector<CPlan::ECategory> vCategories;
		Plan.ForEachPresent([&](CPlan::ECategory Category) { vCategories.push_back(Category); });
		return vCategories;
	}
}

TEST(QmEntityOverlayBuildPlan, EmptyAndOrdinaryTilesDoNotScheduleSpecialLayers)
{
	CPlan Plan;
	EXPECT_TRUE(ScheduledCategories(Plan).empty());
	for(const int Index : {TILE_AIR, TILE_SOLID, TILE_NOHOOK, TILE_SWITCHTIMEDOPEN})
		Plan.Record(Index);
	EXPECT_TRUE(ScheduledCategories(Plan).empty());
}

TEST(QmEntityOverlayBuildPlan, LiveFreezeAliasesScheduleTheirMatchingLayers)
{
	CPlan Plan;
	Plan.Record(TILE_LFREEZE);
	Plan.Record(TILE_LUNFREEZE);
	EXPECT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::FREEZE, CPlan::UNFREEZE}));
}

TEST(QmEntityOverlayBuildPlan, RepeatedTilesAndAliasesBuildEachCategoryOnce)
{
	CPlan Plan;
	for(const int Index : {TILE_LFREEZE, TILE_FREEZE, TILE_FREEZE, TILE_LUNFREEZE, TILE_UNFREEZE})
		Plan.Record(Index);
	EXPECT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::FREEZE, CPlan::UNFREEZE}));
}

TEST(QmEntityOverlayBuildPlan, MixedTilesKeepTheExistingSpecialLayerOrder)
{
	CPlan Plan;
	for(const int Index : {TILE_DUNFREEZE, TILE_DFREEZE, TILE_UNFREEZE, TILE_FREEZE, TILE_DEATH, TILE_AIR})
		Plan.Record(Index);
	EXPECT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::DEATH, CPlan::FREEZE, CPlan::UNFREEZE, CPlan::DEEP_FREEZE, CPlan::DEEP_UNFREEZE}));
}

TEST(QmEntityOverlayBuildPlan, FreshPlanDoesNotRetainPreviousMapCategories)
{
	CPlan Plan;
	Plan.Record(TILE_DEATH);
	ASSERT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::DEATH}));
	Plan = CPlan{};
	Plan.Record(TILE_DUNFREEZE);
	EXPECT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::DEEP_UNFREEZE}));
}

TEST(QmEntityOverlayBuildPlan, FreezeCategoriesDoNotScheduleDeathOrOrdinaryTiles)
{
	CPlan Plan;
	for(const int Index : {TILE_DEATH, TILE_FREEZE, TILE_SWITCHTIMEDOPEN, TILE_LUNFREEZE})
		Plan.Record(Index, false);
	EXPECT_EQ(ScheduledCategories(Plan), (std::vector<CPlan::ECategory>{CPlan::FREEZE, CPlan::UNFREEZE}));
}
