#include <game/client/components/players.h>
#include <game/client/components/qmclient/qm_hook_coll_candidates.h>
#include <game/client/components/qmclient/qm_hook_coll_intersection.h>
#include <game/client/components/qmclient/qm_hook_coll_spatial_index.h>
#include <game/client/components/qmclient/qm_hook_coll_visibility.h>
#include <game/client/components/qmclient/snapshot_entities.h>
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

TEST(QmHookCollCandidates, ReusesTargetsForEverySimulatedSegmentAndPreservesOrder)
{
	CQmHookCollCandidates Cache;
	int Checks = 0;
	const auto Eligible = [&](int Id) {
		++Checks;
		return Id == 1 || Id == 3 || Id == 7;
	};
	const std::vector<int> Expected = {1, 7};
	EXPECT_EQ(Cache.Get(3, Eligible), Expected);
	const int FirstChecks = Checks;
	for(int Tick = 0; Tick < 250; ++Tick)
		EXPECT_EQ(Cache.Get(3, Eligible), Expected);
	EXPECT_EQ(Checks, FirstChecks);
	EXPECT_EQ(Cache.Get(1, Eligible), (std::vector<int>{3, 7}));
	EXPECT_GT(Checks, FirstChecks);
	Cache.Reset();
	EXPECT_EQ(Cache.Get(3, [](int Id) { return Id == 8; }), (std::vector<int>{8}));
	Cache.Reset();
	EXPECT_TRUE(Cache.Get(3, [](int) { return false; }).empty());
}

TEST(QmHookCollGeometry, ReusedScratchPreservesSegmentAndCornerOrder)
{
	SQmHookCollLineScratch Scratch;
	Scratch.m_vLineSegments.emplace_back(vec2(1.0f, 2.0f), vec2(5.0f, 2.0f));
	Scratch.m_vLineSegments.emplace_back(vec2(20.0f, 3.0f), vec2(20.0f, 9.0f));
	Scratch.AppendQuad(Scratch.m_vLineSegments[0], vec2(0.0f, 2.0f), 0.5f);
	Scratch.AppendQuad(Scratch.m_vLineSegments[1], vec2(-2.0f, 0.0f), 0.25f);
	ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 2u);
	const auto &Horizontal = Scratch.m_vLineQuadSegments[0];
	EXPECT_EQ(vec2(Horizontal.m_X0, Horizontal.m_Y0), vec2(5.0f, 1.0f));
	EXPECT_EQ(vec2(Horizontal.m_X1, Horizontal.m_Y1), vec2(5.0f, 3.0f));
	EXPECT_EQ(vec2(Horizontal.m_X2, Horizontal.m_Y2), vec2(1.0f, 1.0f));
	EXPECT_EQ(vec2(Horizontal.m_X3, Horizontal.m_Y3), vec2(1.0f, 3.0f));
	const auto &Vertical = Scratch.m_vLineQuadSegments[1];
	EXPECT_EQ(vec2(Vertical.m_X0, Vertical.m_Y0), vec2(20.5f, 9.0f));
	EXPECT_EQ(vec2(Vertical.m_X1, Vertical.m_Y1), vec2(19.5f, 9.0f));
	EXPECT_EQ(vec2(Vertical.m_X2, Vertical.m_Y2), vec2(20.5f, 3.0f));
	EXPECT_EQ(vec2(Vertical.m_X3, Vertical.m_Y3), vec2(19.5f, 3.0f));
}

TEST(QmHookCollGeometry, NewPlayerAndTipReuseCapacityWithoutStaleGeometry)
{
	SQmHookCollLineScratch Scratch;
	for(int Segment = 0; Segment < 250; ++Segment)
	{
		Scratch.m_vLineSegments.emplace_back(vec2(Segment * 4.0f, 0.0f), vec2(Segment * 4.0f + 2.0f, 0.0f));
		Scratch.AppendQuad(Scratch.m_vLineSegments.back(), vec2(0.0f, 1.0f), 0.5f);
	}
	const auto *pLines = Scratch.m_vLineSegments.data();
	const auto *pQuads = Scratch.m_vLineQuadSegments.data();
	const auto LineCapacity = Scratch.m_vLineSegments.capacity();
	const auto QuadCapacity = Scratch.m_vLineQuadSegments.capacity();
	for(int Player = 0; Player < 64; ++Player)
	{
		Scratch.Reset();
		EXPECT_TRUE(Scratch.m_vLineSegments.empty());
		EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
		Scratch.m_vLineSegments.emplace_back(vec2(4.0f, 0.0f), vec2(8.0f, 0.0f));
		Scratch.m_vLineSegments.emplace_back(vec2(0.0f, 0.0f), vec2(4.0f, 0.0f));
		for(const auto &Line : Scratch.m_vLineSegments)
			Scratch.AppendQuad(Line, vec2(0.0f, 1.0f), 0.5f);
		ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 2u);
		EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[0].m_X0, 8.0f);
		EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[1].m_X0, 4.0f);
		Scratch.m_vLineQuadSegments.clear();
		Scratch.AppendQuad(IGraphics::CLineItem(vec2(8.0f, 0.0f), vec2(9.0f, 0.0f)), vec2(0.0f, 1.0f), 0.5f);
		ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 1u);
		EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[0].m_X0, 9.0f);
		EXPECT_EQ(Scratch.m_vLineSegments.data(), pLines);
		EXPECT_EQ(Scratch.m_vLineQuadSegments.data(), pQuads);
		EXPECT_EQ(Scratch.m_vLineSegments.capacity(), LineCapacity);
		EXPECT_EQ(Scratch.m_vLineQuadSegments.capacity(), QuadCapacity);
	}
}

// 保留原算法作为对照，验证加速后的命中目标、最近点及未命中输出完全一致。
static int ReferenceHookIntersection(vec2 Start, vec2 End, vec2 &Hit, const std::vector<int> &vIds, const std::array<vec2, MAX_CLIENTS> &aPositions, float Radius, vec2 *pPlayer)
{
	float Distance = 0.0f;
	int ClosestId = -1;
	for(int Id : vIds)
	{
		const vec2 Position = aPositions[Id];
		vec2 ClosestPoint;
		if(closest_point_on_line(Start, End, Position, ClosestPoint) && distance(Position, ClosestPoint) < Radius)
		{
			if(ClosestId == -1 || distance(Start, Position) < Distance)
			{
				Hit = ClosestPoint;
				ClosestId = Id;
				Distance = distance(Start, Position);
				if(pPlayer)
					*pPlayer = Position;
			}
		}
	}
	return ClosestId;
}

TEST(QmHookCollIntersection, MatchesOriginalForCrowdedAndSpreadPlayers)
{
	std::array<vec2, MAX_CLIENTS> aPositions;
	std::vector<int> vIds;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		vIds.push_back(Id);
	const std::array<vec2, 6> aStarts = {vec2(0, 0), vec2(400, -200), vec2(-1000, 800), vec2(1000000, -1000000), vec2(90, 30), vec2(40000, 40000)};
	const std::array<vec2, 6> aDeltas = {vec2(0, 0), vec2(80, 0), vec2(0, -80), vec2(-380, 290), vec2(800, 500), vec2(0.125f, -0.25f)};
	for(float Spacing : {1.0f, 32.0f, 512.0f})
	{
		for(vec2 Start : aStarts)
		{
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				aPositions[Id] = Start + vec2(((Id * 17) % 127 - 63) * Spacing, ((Id * 31) % 127 - 63) * Spacing);
			for(vec2 Delta : aDeltas)
			{
				const SQmHookCollSegment Segment(Start, Start + Delta, 30.0f);
				for(vec2 Position : aPositions)
				{
					vec2 OriginalClosest;
					if(closest_point_on_line(Start, Start + Delta, Position, OriginalClosest) && distance(Position, OriginalClosest) < 30.0f)
					{
						EXPECT_TRUE(Segment.MayIntersect(Position));
						EXPECT_EQ(Segment.ClosestPoint(Position), OriginalClosest);
					}
				}
				vec2 ExpectedHit(123, 456), ActualHit = ExpectedHit;
				vec2 ExpectedPlayer(789, 123), ActualPlayer = ExpectedPlayer;
				const int Expected = ReferenceHookIntersection(Start, Start + Delta, ExpectedHit, vIds, aPositions, 30.0f, &ExpectedPlayer);
				const int Actual = QmIntersectHookCollTargets(Start, Start + Delta, ActualHit, vIds, [&](int Id) { return aPositions[Id]; }, 30.0f, &ActualPlayer);
				EXPECT_EQ(Actual, Expected);
				EXPECT_EQ(ActualHit, ExpectedHit);
				EXPECT_EQ(ActualPlayer, ExpectedPlayer);
			}
		}
	}
}

TEST(QmHookCollIntersection, PreservesStrictRadiusTieOrderAndEndpoints)
{
	std::array<vec2, MAX_CLIENTS> aPositions = {};
	aPositions[1] = vec2(50, 30);
	aPositions[2] = vec2(50, -30);
	const std::vector<int> vIds = {2, 1};
	const auto PositionOf = [&](int Id) { return aPositions[Id]; };
	vec2 Hit(999, 999);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0, 0), vec2(100, 0), Hit, vIds, PositionOf, 30.0f), -1);
	EXPECT_EQ(Hit, vec2(999, 999));
	aPositions[1].y = 29.0f;
	aPositions[2].y = -29.0f;
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0, 0), vec2(100, 0), Hit, vIds, PositionOf, 30.0f), 2);
	EXPECT_EQ(Hit, vec2(50, 0));
	aPositions[2] = vec2(-29, 0);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(0, 0), vec2(100, 0), Hit, vIds, PositionOf, 30.0f), 2);
	EXPECT_EQ(Hit, vec2(0, 0));
	aPositions[2] = vec2(129, 0);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(100, 0), vec2(0, 0), Hit, vIds, PositionOf, 30.0f), 2);
	EXPECT_EQ(Hit, vec2(100, 0));
}

TEST(QmHookCollIntersection, RejectsDistantPlayersBeforeExactGeometry)
{
	const SQmHookCollSegment Segment(vec2(0, 0), vec2(80, 0), 30.0f);
	int ExactCandidates = 0;
	for(int Id = 0; Id < 128; ++Id)
	{
		const vec2 Position(Id == 0 ? vec2(40, 10) : vec2(Id * 512.0f, Id * 64.0f));
		if(Segment.MayIntersect(Position))
			++ExactCandidates;
	}
	EXPECT_EQ(ExactCandidates, 1);
	// 每段独立取实际起终点，传送后的远端玩家仍能进入精确判定。
	const SQmHookCollSegment Teleported(vec2(65536, -65536), vec2(65616, -65536), 30.0f);
	EXPECT_TRUE(Teleported.MayIntersect(vec2(65576, -65520)));
}

TEST(QmHookCollIntersection, EmptyAndZeroLengthSegmentsLeaveOutputsUntouched)
{
	vec2 Hit(10, 20), Player(30, 40);
	int PositionReads = 0;
	const auto PositionOf = [&](int) { ++PositionReads; return vec2(1, 1); };
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(1, 1), vec2(1, 1), Hit, std::vector<int>{0}, PositionOf, 30.0f, &Player), -1);
	EXPECT_EQ(QmIntersectHookCollTargets(vec2(1, 1), vec2(2, 2), Hit, std::vector<int>{}, PositionOf, 30.0f, &Player), -1);
	EXPECT_EQ(Hit, vec2(10, 20));
	EXPECT_EQ(Player, vec2(30, 40));
	EXPECT_EQ(PositionReads, 0);
}

TEST(QmHookCollVisibility, TracksHookAndLegacyTeleportInputsAcrossMaps)
{
	CQmHookCollVisibility Visibility;
	// 地图尚未初始化时必须保留模拟，不能假定没有钩子传送。
	EXPECT_TRUE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(-500, -300), vec2(500, 300), 1.0f, false));
	std::array<CTeleTile, 4> aTiles = {};
	aTiles[0].m_Type = TILE_TELEIN;
	aTiles[0].m_Number = 1;
	aTiles[1].m_Type = TILE_TELEINHOOK;
	// 编号为零不触发传送；普通传送只在旧钩子传送模式下生效。
	Visibility.OnMapLoad(aTiles.data(), aTiles.size());
	EXPECT_FALSE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(-500, -300), vec2(500, 300), 1.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(-500, -300), vec2(500, 300), 1.0f, true));
	aTiles[1].m_Number = 2;
	Visibility.OnMapLoad(aTiles.data(), aTiles.size());
	EXPECT_TRUE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(-500, -300), vec2(500, 300), 1.0f, false));
	Visibility.OnMapLoad(nullptr, 0);
	EXPECT_FALSE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(-500, -300), vec2(500, 300), 1.0f, true));
}

TEST(QmHookCollVisibility, PreservesIncomingLinesAndThickEdgesButSkipsDistantPlayers)
{
	CQmHookCollVisibility Visibility;
	Visibility.OnMapLoad(nullptr, 0);
	const vec2 ScreenMin(-500, -300), ScreenMax(500, 300);
	EXPECT_TRUE(Visibility.MayReachView(vec2(800, 0), 380.0f, 80.0f, ScreenMin, ScreenMax, 1.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(vec2(0, -600), 380.0f, 80.0f, ScreenMin, ScreenMax, 1.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(vec2(1200, 0), 380.0f, 80.0f, ScreenMin, ScreenMax, 200.0f, false));
	EXPECT_FALSE(Visibility.MayReachView(vec2(1200, 0), 380.0f, 80.0f, ScreenMin, ScreenMax, 1.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(vec2(10000, 0), 12000.0f, 80.0f, ScreenMin, ScreenMax, 1.0f, false));
	int SimulatedPlayers = 0;
	for(int Id = 0; Id < 128; ++Id)
		SimulatedPlayers += Visibility.MayReachView(vec2(Id * 1024.0f, 0), 380.0f, 80.0f, ScreenMin, ScreenMax, 1.0f, false) ? 1 : 0;
	EXPECT_EQ(SimulatedPlayers, 2);
	// 每个视口单独判定；另一小窗可看见的远端玩家不能复用主视口的裁剪结果。
	EXPECT_TRUE(Visibility.MayReachView(vec2(10000, 0), 380.0f, 80.0f, vec2(9500, -300), vec2(10500, 300), 1.0f, false));
}

TEST(QmHookCollVisibility, ConservativeBoundsContainQuantizedSimulationAndFallback)
{
	CQmHookCollVisibility Visibility;
	Visibility.OnMapLoad(nullptr, 0);
	const vec2 aOrigins[] = {vec2(0, 0), vec2(-4096, 4096), vec2(1000000, -1000000)};
	for(const vec2 Origin : aOrigins)
		for(const float HookLength : {42.0f, 380.0f, 5000.0f})
			for(const float FireSpeed : {0.25f, 80.0f, 1000.0f})
				for(int AngleIndex = 0; AngleIndex < 32; ++AngleIndex)
				{
					const vec2 Direction = direction(AngleIndex * (2.0f * pi / 32.0f));
					vec2 QuantizedDirection = Direction;
					vec2 Start = Origin + Direction * 42.0f;
					const auto CheckPoint = [&](vec2 Point) {
						// 将视口移到模拟的线端；任何可见的原始端点都必须保留。
						EXPECT_TRUE(Visibility.MayReachView(Origin, HookLength, FireSpeed, Point - vec2(1, 1), Point + vec2(1, 1), 1.0f, false));
					};
					CheckPoint(Origin);
					CheckPoint(Start);
					for(int Tick = 0; Tick < 250; ++Tick)
					{
						const vec2 End = Start + QuantizedDirection * FireSpeed;
						CheckPoint(End);
						if(distance(Origin, End) > HookLength)
						{
							CheckPoint(Origin + normalize(End - Origin) * HookLength);
							break;
						}
						Start = vec2(round_to_int(End.x), round_to_int(End.y));
						if(Tick == 0)
							QuantizedDirection = vec2(round_to_int(Direction.x * 256.0f) / 256.0f, round_to_int(Direction.y * 256.0f) / 256.0f);
					}
					CheckPoint(Origin + QuantizedDirection * HookLength);
				}
}

TEST(QmHookCollSpatialIndex, ShortSingleHookDoesNotBuildAnIndexEveryFrame)
{
	CQmHookCollSpatialIndex Index;
	std::vector<int> vAllowed;
	for(int Id = 1; Id < MAX_CLIENTS; ++Id)
		vAllowed.push_back(Id);
	int PositionReads = 0;
	const auto PositionOf = [&](int Id) { ++PositionReads; return vec2(Id * 512.0f, 0); };
	for(int Frame = 0; Frame < 128; ++Frame)
	{
		Index.Reset();
		for(int Step = 0; Step < 5; ++Step)
		{
			const SQmHookCollSegment Segment(vec2(Step * 80.0f, 0), vec2((Step + 1) * 80.0f, 0), 30.0f);
			EXPECT_EQ(&Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; }), &vAllowed);
		}
	}
	EXPECT_EQ(PositionReads, 0);
}

TEST(QmHookCollSpatialIndex, NarrowsSpreadPlayersAndReusesOneFrameOfPositions)
{
	CQmHookCollSpatialIndex Index;
	std::array<vec2, MAX_CLIENTS> aPositions;
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		aPositions[Id] = vec2(Id * 512.0f, Id * 64.0f);
		vAllowed.push_back(Id);
	}
	int PositionReads = 0;
	const auto PositionOf = [&](int Id) { ++PositionReads; return aPositions[Id]; };
	const auto Valid = [](int) { return true; };
	const SQmHookCollSegment First(vec2(0, 0), vec2(80, 0), 30.0f);
	for(int Segment = 0; Segment < 250; ++Segment)
	{
		const auto &vNearby = Index.GetCandidates(First, vAllowed, PositionOf, Valid);
		EXPECT_EQ(vNearby, PositionReads == 0 ? vAllowed : std::vector<int>{0});
	}
	EXPECT_EQ(PositionReads, MAX_CLIENTS);
	// 传送后的另一段按自己的端点查询，不能沿用传送前的近邻。
	const SQmHookCollSegment Teleported(aPositions[100], aPositions[100] + vec2(80, 0), 30.0f);
	EXPECT_EQ(Index.GetCandidates(Teleported, vAllowed, PositionOf, Valid), (std::vector<int>{100}));
	EXPECT_EQ(PositionReads, MAX_CLIENTS);
	// 下一轮绘制重建：移动、玩家离开和不同视口使用的新快照均不能遗留旧位置。
	aPositions[100] = vec2(40, 0);
	Index.Reset();
	// 当前资格名单也已移除离开的玩家。
	vAllowed.erase(vAllowed.begin());
	for(int Segment = 0; Segment < 250; ++Segment)
		Index.GetCandidates(First, vAllowed, PositionOf, [](int Id) { return Id != 0; });
	EXPECT_EQ(Index.GetCandidates(First, vAllowed, PositionOf, [](int Id) { return Id != 0; }), (std::vector<int>{100}));
}

TEST(QmHookCollSpatialIndex, DenseAndSmallSetsUseOriginalOrderWithoutExtraSorts)
{
	CQmHookCollSpatialIndex Index;
	std::array<vec2, MAX_CLIENTS> aPositions;
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		aPositions[Id] = vec2(40.0f, (Id % 5) - 2.0f);
		vAllowed.push_back(Id);
	}
	const SQmHookCollSegment Segment(vec2(0, 0), vec2(80, 0), 30.0f);
	const auto PositionOf = [&](int Id) { return aPositions[Id]; };
	for(int Query = 0; Query < 250; ++Query)
		EXPECT_EQ(&Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; }), &vAllowed);
	const std::vector<int> vSmall = {1, 3};
	Index.Reset();
	int Reads = 0;
	EXPECT_EQ(&Index.GetCandidates(Segment, vSmall, [&](int Id) { ++Reads; return aPositions[Id]; }, [](int) { return true; }), &vSmall);
	EXPECT_EQ(Reads, 0);
}

TEST(QmHookCollSpatialIndex, MatchesFullScanIncludingEligibilityBoundsAndTies)
{
	const std::array<vec2, 5> aDeltas = {vec2(80, 0), vec2(0, -80), vec2(-380, 290), vec2(800, 500), vec2(0.125f, -0.25f)};
	for(const float Spacing : {1.0f, 32.0f, 512.0f})
		for(const vec2 Origin : {vec2(0, 0), vec2(-4000, 2000), vec2(1000000, -1000000)})
		{
			CQmHookCollSpatialIndex Index;
			std::array<vec2, MAX_CLIENTS> aPositions;
			std::vector<int> vAllowed;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				aPositions[Id] = Origin + vec2(((Id * 17) % 127 - 63) * Spacing, ((Id * 31) % 127 - 63) * Spacing);
				if(Id % 3 != 0)
					vAllowed.push_back(Id);
			}
			// 对称目标强制等距；额外目标位于粗筛和精确半径边界。
			aPositions[1] = Origin + vec2(40, 10);
			aPositions[2] = Origin + vec2(40, -10);
			aPositions[4] = Origin + vec2(31, 30);
			aPositions[5] = Origin + vec2(-31, 0);
			const auto PositionOf = [&](int Id) { return aPositions[Id]; };
			for(int Query = 0; Query < 25; ++Query)
			{
				const vec2 Delta = aDeltas[Query % aDeltas.size()];
				const SQmHookCollSegment Segment(Origin, Origin + Delta, 30.0f);
				const auto &vNearby = Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; });
				// 不仅最终赢家：全扫描可命中的每位玩家都必须保留，且资格过滤不变。
				for(const int Id : vAllowed)
				{
					vec2 Closest;
					if(closest_point_on_line(Origin, Origin + Delta, aPositions[Id], Closest) && distance(aPositions[Id], Closest) < 30.0f)
						EXPECT_TRUE(std::binary_search(vNearby.begin(), vNearby.end(), Id));
				}
				for(const int Id : vNearby)
					EXPECT_TRUE(std::binary_search(vAllowed.begin(), vAllowed.end(), Id));
				vec2 ExpectedHit(123, 456), ActualHit = ExpectedHit;
				vec2 ExpectedPlayer(789, 123), ActualPlayer = ExpectedPlayer;
				const int Expected = ReferenceHookIntersection(Origin, Origin + Delta, ExpectedHit, vAllowed, aPositions, 30.0f, &ExpectedPlayer);
				const int Actual = QmIntersectHookCollTargets(Origin, Origin + Delta, ActualHit, vNearby, PositionOf, 30.0f, &ActualPlayer);
				EXPECT_EQ(Actual, Expected);
				EXPECT_EQ(ActualHit, ExpectedHit);
				EXPECT_EQ(ActualPlayer, ExpectedPlayer);
			}
		}
}

TEST(QmHookCollSpatialIndex, UsesEitherAxisForRowsAndColumnsOfPlayers)
{
	CQmHookCollSpatialIndex Index;
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		vAllowed.push_back(Id);
	const SQmHookCollSegment Horizontal(vec2(-10000, 0), vec2(10000, 0), 30.0f);
	for(int Query = 0; Query < 250; ++Query)
		Index.GetCandidates(Horizontal, vAllowed, [](int Id) { return vec2(0, Id * 512.0f); }, [](int) { return true; });
	EXPECT_EQ(Index.GetCandidates(Horizontal, vAllowed, [](int Id) { return vec2(0, Id * 512.0f); }, [](int) { return true; }), (std::vector<int>{0}));
	Index.Reset();
	const SQmHookCollSegment Vertical(vec2(0, -10000), vec2(0, 10000), 30.0f);
	for(int Query = 0; Query < 250; ++Query)
		Index.GetCandidates(Vertical, vAllowed, [](int Id) { return vec2(Id * 512.0f, 0); }, [](int) { return true; });
	EXPECT_EQ(Index.GetCandidates(Vertical, vAllowed, [](int Id) { return vec2(Id * 512.0f, 0); }, [](int) { return true; }), (std::vector<int>{0}));
}

TEST(QmSnapshotEntities, MatchesExtensionsByIdAndPreservesEntityPayloads)
{
	CNetObj_Laser Laser{};
	CNetObj_Projectile Projectile{};
	CNetObj_Pickup Pickup{};
	CNetObj_EntityEx Ex3{}, Ex7{}, Unmatched{};
	std::vector<CSnapEntities> vEntities = {
		{{NETOBJTYPE_LASER, 7, &Laser, sizeof(Laser)}, nullptr},
		{{NETOBJTYPE_PROJECTILE, 3, &Projectile, sizeof(Projectile)}, nullptr},
		{{NETOBJTYPE_PICKUP, 7, &Pickup, sizeof(Pickup)}, nullptr},
		{{NETOBJTYPE_LASER, 5, &Laser, sizeof(Laser)}, nullptr},
	};
	std::vector<CSnapEntities> vExtensions = {
		{{NETOBJTYPE_ENTITYEX, 7, &Ex7, sizeof(Ex7)}, nullptr},
		{{NETOBJTYPE_ENTITYEX, 1, &Unmatched, sizeof(Unmatched)}, nullptr},
		{{NETOBJTYPE_ENTITYEX, 3, &Ex3, sizeof(Ex3)}, nullptr},
	};
	const auto vOriginal = vEntities;
	QmAttachSnapshotEntityExtensions(vEntities, vExtensions);
	ASSERT_EQ(vEntities.size(), 4u);
	EXPECT_EQ(vEntities[0].m_Item.m_Id, 3);
	EXPECT_EQ(vEntities[1].m_Item.m_Id, 5);
	EXPECT_EQ(vEntities[2].m_Item.m_Id, 7);
	EXPECT_EQ(vEntities[3].m_Item.m_Id, 7);
	EXPECT_EQ(vEntities[0].m_pDataEx, &Ex3);
	EXPECT_EQ(vEntities[1].m_pDataEx, nullptr);
	EXPECT_EQ(vEntities[2].m_pDataEx, &Ex7);
	EXPECT_EQ(vEntities[3].m_pDataEx, &Ex7);
	for(const CSnapEntities &Entity : vEntities)
	{
		const auto It = std::find_if(vOriginal.begin(), vOriginal.end(), [&](const CSnapEntities &Original) {
			return Original.m_Item.m_Type == Entity.m_Item.m_Type && Original.m_Item.m_Id == Entity.m_Item.m_Id;
		});
		ASSERT_NE(It, vOriginal.end());
		EXPECT_EQ(Entity.m_Item.m_pData, It->m_Item.m_pData);
		EXPECT_EQ(Entity.m_Item.m_DataSize, It->m_Item.m_DataSize);
	}
	EXPECT_TRUE(vExtensions.empty());
}

TEST(QmSnapshotEntities, ReusesStorageAndDoesNotKeepPreviousSnapshotExtensions)
{
	CNetObj_EntityEx Ex{};
	std::vector<CSnapEntities> vEntities;
	std::vector<CSnapEntities> vExtensions;
	vEntities.reserve(8);
	vExtensions.reserve(8);
	vEntities.push_back({{NETOBJTYPE_PICKUP, 4, nullptr, 0}, nullptr});
	vExtensions.push_back({{NETOBJTYPE_ENTITYEX, 4, &Ex, sizeof(Ex)}, nullptr});
	const auto *pEntities = vEntities.data();
	const auto *pExtensions = vExtensions.data();
	const auto EntitiesCapacity = vEntities.capacity();
	const auto ExtensionsCapacity = vExtensions.capacity();
	QmAttachSnapshotEntityExtensions(vEntities, vExtensions);
	ASSERT_EQ(vEntities[0].m_pDataEx, &Ex);
	vEntities.clear();
	vEntities.push_back({{NETOBJTYPE_PICKUP, 4, nullptr, 0}, nullptr});
	QmAttachSnapshotEntityExtensions(vEntities, vExtensions);
	EXPECT_EQ(vEntities[0].m_pDataEx, nullptr);
	EXPECT_EQ(vEntities.data(), pEntities);
	EXPECT_EQ(vExtensions.data(), pExtensions);
	EXPECT_EQ(vEntities.capacity(), EntitiesCapacity);
	EXPECT_EQ(vExtensions.capacity(), ExtensionsCapacity);
	vEntities.clear();
	vExtensions.push_back({{NETOBJTYPE_ENTITYEX, 4, &Ex, sizeof(Ex)}, nullptr});
	QmAttachSnapshotEntityExtensions(vEntities, vExtensions);
	EXPECT_TRUE(vEntities.empty());
	EXPECT_TRUE(vExtensions.empty());
}
