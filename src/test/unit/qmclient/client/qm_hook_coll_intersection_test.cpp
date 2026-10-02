#include <game/client/components/qmclient/qm_hook_coll_intersection.h>
#include <game/client/components/qmclient/qm_hook_coll_spatial_index.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{

	constexpr float HOOK_RADIUS = 30.0f;

	std::vector<int> AllowedOf(std::initializer_list<int> Ids)
	{
		std::vector<int> vAllowed(Ids);
		return vAllowed;
	}

} // namespace

TEST(QmHookCollIntersection, PicksTheCandidateNearestToTheSegmentStart)
{
	std::vector<vec2> vPositions(4);
	vPositions[1] = vec2(100.0f, 0.0f);
	vPositions[2] = vec2(50.0f, 0.0f);
	vPositions[3] = vec2(150.0f, 0.0f);
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };

	vec2 Hit;
	// 三个目标都压在线段上，取离起点最近的 2 号。
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({1, 2, 3}), PositionOf, HOOK_RADIUS), 2);
	EXPECT_FLOAT_EQ(Hit.x, 50.0f);
	EXPECT_FLOAT_EQ(Hit.y, 0.0f);

	// 命中位置是目标到线段的最近点，而不是目标自身。
	vPositions[2] = vec2(50.0f, 10.0f);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({1, 2, 3}), PositionOf, HOOK_RADIUS), 2);
	EXPECT_FLOAT_EQ(Hit.x, 50.0f);
	EXPECT_FLOAT_EQ(Hit.y, 0.0f);
}

TEST(QmHookCollIntersection, EqualDistanceKeepsTheEarlierCandidate)
{
	std::vector<vec2> vPositions(4);
	vPositions[1] = vec2(40.0f, 5.0f);
	vPositions[2] = vec2(40.0f, -5.0f);
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };

	vec2 Hit;
	// 与起点距离相同：保留名单里先出现的编号，不因遍历顺序或坐标细微差别换人。
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({1, 2}), PositionOf, HOOK_RADIUS), 1);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({2, 1}), PositionOf, HOOK_RADIUS), 2);
}

TEST(QmHookCollIntersection, MissLeavesTheOutputsUntouched)
{
	std::vector<vec2> vPositions(2);
	vPositions[1] = vec2(100.0f, 500.0f);
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };

	vec2 Hit(7.0f, 8.0f);
	vec2 PlayerPosition(9.0f, 10.0f);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({1}), PositionOf, HOOK_RADIUS, &PlayerPosition), -1);
	EXPECT_FLOAT_EQ(Hit.x, 7.0f);
	EXPECT_FLOAT_EQ(Hit.y, 8.0f);
	EXPECT_FLOAT_EQ(PlayerPosition.x, 9.0f);
	EXPECT_FLOAT_EQ(PlayerPosition.y, 10.0f);

	// 空名单同样不命中。
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, std::vector<int>{}, PositionOf, HOOK_RADIUS), -1);
}

TEST(QmHookCollIntersection, DegenerateSegmentNeverHits)
{
	std::vector<vec2> vPositions(2);
	vPositions[1] = vec2(0.0f, 0.0f);
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };

	vec2 Hit;
	// 零长度线段没有方向，与 closest_point_on_line 的既有语义一致：判定为不命中。
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(10.0f, 10.0f), vec2(10.0f, 10.0f), Hit, AllowedOf({1}), PositionOf, HOOK_RADIUS), -1);
}

TEST(QmHookCollIntersection, TargetsBeyondTheEndpointsAreStillReachableWithinRadius)
{
	std::vector<vec2> vPositions(3);
	// 落在线段延长线上、超出终点但在半径内：必须命中，粗筛不能把它滤掉。
	vPositions[1] = vec2(215.0f, 0.0f);
	// 起点外侧同理。
	vPositions[2] = vec2(-15.0f, 0.0f);
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };

	vec2 Hit;
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({1}), PositionOf, HOOK_RADIUS), 1);
	EXPECT_FLOAT_EQ(Hit.x, 200.0f);
	EXPECT_FLOAT_EQ(Hit.y, 0.0f);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0.0f, 0.0f), vec2(200.0f, 0.0f), Hit, AllowedOf({2}), PositionOf, HOOK_RADIUS), 2);
	EXPECT_FLOAT_EQ(Hit.x, 0.0f);
	EXPECT_FLOAT_EQ(Hit.y, 0.0f);
}

TEST(QmHookCollSpatialIndex, NarrowedCandidatesKeepTheSameHit)
{
	// 索引只允许缩小几何候选，不能改变命中者：与直接用完整名单的结果必须一致。
	const vec2 SegmentStart(0.0f, 0.0f);
	const vec2 SegmentEnd(600.0f, 0.0f);
	const SQmHookCollSegment Segment(SegmentStart, SegmentEnd, HOOK_RADIUS);

	std::vector<vec2> vPositions(MAX_CLIENTS);
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		// 铺开成两簇，确保索引需要做两轴二分而不是「全包住」早退。
		const float X = (Id % 2 == 0) ? (float)(Id * 3) : 500.0f + (float)Id;
		vPositions[Id] = vec2(X, (float)((Id % 7) - 3));
		if(Id != 0 && Id % 3 != 0)
			vAllowed.push_back(Id);
	}
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };
	const auto Valid = [](int) { return true; };

	vec2 ExpectedHit;
	const int Expected = QmIntersectHookCollTargets(Segment, ExpectedHit, vAllowed, PositionOf, HOOK_RADIUS);

	CQmHookCollSpatialIndex Index;
	vec2 ActualHit;
	int Actual = -1;
	// 前几次查询走直接粗筛路径，累计足够多次后切换为排序 + 二分路径，两次都要一致。
	for(int Query = 0; Query < 40; ++Query)
	{
		const auto &vNearby = Index.GetCandidates(Segment, vAllowed, PositionOf, Valid);
		for(const int Id : vNearby)
			ASSERT_TRUE(Id != 0 && Id % 3 != 0);
		Actual = QmIntersectHookCollTargets(Segment, ActualHit, vNearby, PositionOf, HOOK_RADIUS);
		EXPECT_EQ(Actual, Expected) << "Query=" << Query;
	}
	if(Expected != -1)
	{
		EXPECT_FLOAT_EQ(ActualHit.x, ExpectedHit.x);
		EXPECT_FLOAT_EQ(ActualHit.y, ExpectedHit.y);
	}
}

TEST(QmHookCollSpatialIndex, ResetDropsTheCachedIndexAndResamplesPositions)
{
	// 短线段 + 铺开的玩家：索引必须真的收窄，而不是走「全包住」早退。
	const SQmHookCollSegment Segment(vec2(0.0f, 0.0f), vec2(20.0f, 0.0f), HOOK_RADIUS);
	std::vector<vec2> vPositions(MAX_CLIENTS);
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		vPositions[Id] = vec2((float)(Id * 40), (float)(Id % 5));
		vAllowed.push_back(Id);
	}
	const auto PositionOf = [&](int Id) { return vPositions[Id]; };
	const auto Valid = [](int) { return true; };

	CQmHookCollSpatialIndex Index;
	// 累计足够多次查询后才会建立排序索引（见 GetCandidates 的摊销门槛）。
	for(int Query = 0; Query < 40; ++Query)
		Index.GetCandidates(Segment, vAllowed, PositionOf, Valid);
	EXPECT_LT(Index.GetCandidates(Segment, vAllowed, PositionOf, Valid).size(), vAllowed.size());

	// 位置整体搬走：Reset 之后必须按新位置重新采样，不能继续用旧索引。
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		vPositions[Id] = vec2(100000.0f + (float)Id, 100000.0f);
	Index.Reset();
	for(int Query = 0; Query < 40; ++Query)
		Index.GetCandidates(Segment, vAllowed, PositionOf, Valid);
	EXPECT_TRUE(Index.GetCandidates(Segment, vAllowed, PositionOf, Valid).empty());
}
