#include <engine/textrender.h>

#include <gtest/gtest.h>

TEST(TextCharTransform, DefaultScalePreservesExistingFloatingOffsets)
{
	const STextCharOffset Transform(3, 2.0f, -3.0f);
	const vec2 Position = Transform.TransformVertex(vec2(0.125f, -1.25f), vec2(1e20f, -1e20f));
	// 默认比例无需围绕中心做浮点往返，旧偏移效果保持相同坐标。
	EXPECT_FLOAT_EQ(Transform.m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Position.x, 2.125f);
	EXPECT_FLOAT_EQ(Position.y, -4.25f);
}

TEST(TextCharTransform, ShrinkingKeepsTheGlyphCenterFixed)
{
	const STextCharOffset Transform(0, 0.0f, 0.0f, 0.5f);
	const vec2 Center(13.0f, 22.0f);
	const vec2 TopLeft = Transform.TransformVertex(vec2(10.0f, 16.0f), Center);
	const vec2 BottomRight = Transform.TransformVertex(vec2(16.0f, 28.0f), Center);
	EXPECT_FLOAT_EQ(TopLeft.x, 11.5f);
	EXPECT_FLOAT_EQ(TopLeft.y, 19.0f);
	EXPECT_FLOAT_EQ(BottomRight.x, 14.5f);
	EXPECT_FLOAT_EQ(BottomRight.y, 25.0f);
	EXPECT_FLOAT_EQ((TopLeft.x + BottomRight.x) * 0.5f, Center.x);
	EXPECT_FLOAT_EQ((TopLeft.y + BottomRight.y) * 0.5f, Center.y);
}

TEST(TextCharTransform, OvershootScalesBothAxesBeforeApplyingOffset)
{
	const STextCharOffset Transform(0, 2.0f, -3.0f, 1.1f);
	const vec2 Center(13.0f, 22.0f);
	const vec2 TopLeft = Transform.TransformVertex(vec2(10.0f, 16.0f), Center);
	const vec2 BottomRight = Transform.TransformVertex(vec2(16.0f, 28.0f), Center);
	EXPECT_NEAR(TopLeft.x, 11.7f, 1e-5f);
	EXPECT_NEAR(TopLeft.y, 12.4f, 1e-5f);
	EXPECT_NEAR(BottomRight.x, 18.3f, 1e-5f);
	EXPECT_NEAR(BottomRight.y, 25.6f, 1e-5f);
	EXPECT_NEAR(BottomRight.x - TopLeft.x, 6.6f, 1e-5f);
	EXPECT_NEAR(BottomRight.y - TopLeft.y, 13.2f, 1e-5f);
}
