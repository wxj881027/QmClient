#include <game/client/components/qmclient/collision_hitbox_logic.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(CollisionHitboxLogic, CapsuleOutlineHandlesDegenerateAndAxisAlignedLasers)
{
	const auto Circle = BuildHitboxCapsuleOutline({10.0f, 20.0f}, {10.0f, 20.0f}, 5.0f, 4);
	ASSERT_EQ(Circle.size(), 8u);
	for(const auto &Line : Circle)
	{
		EXPECT_NEAR(distance(Line.m_From, vec2(10.0f, 20.0f)), 5.0f, 0.0001f);
		EXPECT_NEAR(distance(Line.m_To, vec2(10.0f, 20.0f)), 5.0f, 0.0001f);
	}

	const auto Horizontal = BuildHitboxCapsuleOutline({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, 4);
	ASSERT_EQ(Horizontal.size(), 10u);
	EXPECT_NEAR(Horizontal[0].m_From.y, 2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[0].m_To.y, 2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[1].m_From.y, -2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[1].m_To.y, -2.0f, 0.0001f);

	const auto Vertical = BuildHitboxCapsuleOutline({0.0f, 0.0f}, {0.0f, 10.0f}, 2.0f, 4);
	ASSERT_EQ(Vertical.size(), 10u);
	EXPECT_NEAR(Vertical[0].m_From.x, -2.0f, 0.0001f);
	EXPECT_NEAR(Vertical[0].m_To.x, -2.0f, 0.0001f);
	EXPECT_TRUE(BuildHitboxCapsuleOutline({0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f).empty());
	const float MaxFloat = std::numeric_limits<float>::max();
	EXPECT_TRUE(BuildHitboxCapsuleOutline({-MaxFloat, 0.0f}, {MaxFloat, 0.0f}, 2.0f).empty());
}

TEST(CollisionHitboxLogic, CapsuleEmitOverloadProducesTheSameOutlineAsTheVectorOverload)
{
	// 渲染热路径改用直接输出重载（栈上定长缓冲），两者必须逐点一致，
	// 否则渲染结果会与既有行为发生偏移。
	const vec2 Cases[][2] = {
		{{10.0f, 20.0f}, {10.0f, 20.0f}}, // 退化为圆
		{{0.0f, 0.0f}, {10.0f, 0.0f}}, // 水平
		{{0.0f, 0.0f}, {0.0f, 10.0f}}, // 垂直
		{{-3.0f, 5.0f}, {7.0f, -11.0f}}, // 斜向
	};
	for(const auto &Case : Cases)
	{
		for(const int ArcSegments : {2, 3, 4, 16, 64})
		{
			const auto Expected = BuildHitboxCapsuleOutline(Case[0], Case[1], 2.5f, ArcSegments);
			std::vector<SCollisionHitboxLine> Emitted;
			BuildHitboxCapsuleOutline(Case[0], Case[1], 2.5f, ArcSegments, [&](vec2 From, vec2 To) {
				Emitted.push_back({From, To});
			});
			ASSERT_EQ(Emitted.size(), Expected.size()) << "ArcSegments=" << ArcSegments;
			for(size_t Index = 0; Index < Emitted.size(); ++Index)
			{
				EXPECT_EQ(Emitted[Index].m_From.x, Expected[Index].m_From.x) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_From.y, Expected[Index].m_From.y) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_To.x, Expected[Index].m_To.x) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_To.y, Expected[Index].m_To.y) << "ArcSegments=" << ArcSegments << " Index=" << Index;
			}
		}
	}

	// 被拒绝的输入不能输出任何线段；合法输入不能超过栈上定长缓冲的容量。
	std::vector<SCollisionHitboxLine> Rejected;
	BuildHitboxCapsuleOutline({0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f, 16, [&](vec2 From, vec2 To) { Rejected.push_back({From, To}); });
	EXPECT_TRUE(Rejected.empty());
	std::vector<SCollisionHitboxLine> Overflowed;
	const float MaxFloat = std::numeric_limits<float>::max();
	BuildHitboxCapsuleOutline({-MaxFloat, 0.0f}, {MaxFloat, 0.0f}, 2.0f, 16, [&](vec2 From, vec2 To) { Overflowed.push_back({From, To}); });
	EXPECT_TRUE(Overflowed.empty());
	std::vector<SCollisionHitboxLine> MaxSegments;
	BuildHitboxCapsuleOutline({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, 4096, [&](vec2 From, vec2 To) { MaxSegments.push_back({From, To}); });
	EXPECT_LE(MaxSegments.size(), (size_t)COLLISION_HITBOX_CAPSULE_MAX_LINES);
}

TEST(CollisionHitboxLogic, CachedCircleDirectionsKeepIncreasingAnglesPerSegmentCount)
{
	CQmHitboxCircleDirections Directions;
	for(const int Segments : {CQmHitboxCircleDirections::MIN_SEGMENTS, 32, CQmHitboxCircleDirections::MAX_SEGMENTS})
	{
		const vec2 *pFirst = Directions.Get(Segments);
		const vec2 *pSecond = Directions.Get(Segments);
		// 同一档位重复取用必须命中缓存，返回同一块表。
		EXPECT_EQ(pFirst, pSecond);
		// 起点固定为角度 0，末端保留原角度运算的浮点值（不吸附回起点）。
		EXPECT_FLOAT_EQ(pFirst[0].x, 1.0f);
		EXPECT_FLOAT_EQ(pFirst[0].y, 0.0f);
		const float Step = 2.0f * pi / Segments;
		for(int Index = 0; Index <= Segments; ++Index)
		{
			EXPECT_FLOAT_EQ(pFirst[Index].x, std::cos(Step * Index));
			EXPECT_FLOAT_EQ(pFirst[Index].y, std::sin(Step * Index));
			EXPECT_NEAR(length(pFirst[Index]), 1.0f, 0.0001f);
		}
	}
	// 不同档位各自独立，不互相覆盖。
	const vec2 *pSmall = Directions.Get(CQmHitboxCircleDirections::MIN_SEGMENTS);
	const vec2 *pLarge = Directions.Get(CQmHitboxCircleDirections::MAX_SEGMENTS);
	EXPECT_NE(pSmall, pLarge);
	EXPECT_NEAR(length(pSmall[CQmHitboxCircleDirections::MIN_SEGMENTS]), 1.0f, 0.0001f);
	EXPECT_NEAR(length(pLarge[CQmHitboxCircleDirections::MAX_SEGMENTS]), 1.0f, 0.0001f);
}
