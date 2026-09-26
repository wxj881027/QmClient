#include <engine/client/quad_rotation_cache.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// 四边形旋转缓存按角度取 (cos, sin)：重复角度可复用结果，
// 角度变化或无效输入后不能返回陈旧方向。
TEST(QmQuadRotationCache, AxisAlignedAnglesGiveUnitDirections)
{
	CQmQuadRotationCache Cache;
	const vec2 Zero = Cache.Get(0.0f);
	EXPECT_NEAR(Zero.x, 1.0f, 1e-6f);
	EXPECT_NEAR(Zero.y, 0.0f, 1e-6f);

	const vec2 Quarter = Cache.Get(pi / 2.0f);
	EXPECT_NEAR(Quarter.x, 0.0f, 1e-6f);
	EXPECT_NEAR(Quarter.y, 1.0f, 1e-6f);

	const vec2 Half = Cache.Get(pi);
	EXPECT_NEAR(Half.x, -1.0f, 1e-6f);
	EXPECT_NEAR(Half.y, 0.0f, 1e-6f);
}

TEST(QmQuadRotationCache, FreshInstanceMatchesZeroAngle)
{
	// 默认状态即角度 0、方向 (1, 0)：首次以 0 调用不需要重算，也不能给出别的方向。
	CQmQuadRotationCache Cache;
	const vec2 Direction = Cache.Get(0.0f);
	EXPECT_NEAR(Direction.x, 1.0f, 1e-6f);
	EXPECT_NEAR(Direction.y, 0.0f, 1e-6f);
}

TEST(QmQuadRotationCache, ReusesResultForRepeatedAngle)
{
	CQmQuadRotationCache Cache;
	const vec2 First = Cache.Get(0.7f);
	// 同一角度重复取用必须逐位相同（缓存命中路径）。
	for(int Repeat = 0; Repeat < 8; ++Repeat)
	{
		const vec2 Again = Cache.Get(0.7f);
		EXPECT_FLOAT_EQ(Again.x, First.x);
		EXPECT_FLOAT_EQ(Again.y, First.y);
	}
	// 与直接计算一致：缓存不得改变数值。
	EXPECT_NEAR(First.x, std::cos(0.7f), 1e-6f);
	EXPECT_NEAR(First.y, std::sin(0.7f), 1e-6f);
}

TEST(QmQuadRotationCache, RecomputesWhenAngleChanges)
{
	CQmQuadRotationCache Cache;
	const vec2 First = Cache.Get(0.0f);
	EXPECT_NEAR(First.x, 1.0f, 1e-6f);

	// 角度变化必须重算，否则会把上一个角度的方向用到新角度上。
	const vec2 Second = Cache.Get(2.5f);
	EXPECT_NEAR(Second.x, std::cos(2.5f), 1e-6f);
	EXPECT_NEAR(Second.y, std::sin(2.5f), 1e-6f);
	EXPECT_GT(std::fabs(Second.x - First.x) + std::fabs(Second.y - First.y), 0.5f);

	// 来回切换同样每次都正确（不量化角度，因此 0 与极小角度是不同的键）。
	const vec2 Back = Cache.Get(0.0f);
	EXPECT_NEAR(Back.x, 1.0f, 1e-6f);
	EXPECT_NEAR(Back.y, 0.0f, 1e-6f);

	const vec2 Tiny = Cache.Get(1e-4f);
	EXPECT_NEAR(Tiny.x, std::cos(1e-4f), 1e-6f);
	EXPECT_NEAR(Tiny.y, std::sin(1e-4f), 1e-6f);
	EXPECT_NE(Tiny.y, 0.0f);
}

TEST(QmQuadRotationCache, AdjacentFloatAnglesAndOtherInstancesDoNotReuseStaleDirections)
{
	CQmQuadRotationCache Cache;
	const float Adjacent = std::nextafter(0.75f, 1.0f);
	for(const float Angle : {0.75f, 0.75f, Adjacent, -0.75f, 8.0f * pi, 0.75f})
	{
		const vec2 Direction = Cache.Get(Angle);
		EXPECT_EQ(Direction.x, std::cos(Angle));
		EXPECT_EQ(Direction.y, std::sin(Angle));
	}
	CQmQuadRotationCache Other;
	Other.Get(-1.0f);
	EXPECT_EQ(Cache.Get(0.75f).x, std::cos(0.75f));
	EXPECT_EQ(Cache.Get(0.75f).y, std::sin(0.75f));
}

TEST(QmQuadRotationCache, NonFiniteAnglesDoNotContaminateLaterDraws)
{
	CQmQuadRotationCache Cache;
	for(const float Angle : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		const vec2 Invalid = Cache.Get(Angle);
		EXPECT_TRUE(std::isnan(Invalid.x));
		EXPECT_TRUE(std::isnan(Invalid.y));
		const vec2 Valid = Cache.Get(-0.5f);
		EXPECT_EQ(Valid.x, std::cos(-0.5f));
		EXPECT_EQ(Valid.y, std::sin(-0.5f));
	}
}
