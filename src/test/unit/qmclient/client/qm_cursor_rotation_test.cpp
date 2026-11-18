#include <game/client/components/qmclient/cursor_rotation.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmCursorRotation, ZeroSpeedKeepsCursorStatic)
{
	EXPECT_FLOAT_EQ(QmCursorRotationRadians(0, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmCursorRotationRadians(0, 1234.5f), 0.0f);
}

TEST(QmCursorRotation, AngleFollowsSpeedInDegreesPerSecond)
{
	EXPECT_FLOAT_EQ(QmCursorRotationRadians(90, 1.0f), pi / 2.0f);
	EXPECT_FLOAT_EQ(QmCursorRotationRadians(180, 0.5f), pi / 2.0f);
	EXPECT_NEAR(QmCursorRotationRadians(45, 8.0f), 0.0f, 1e-5f);
}

TEST(QmCursorRotation, AngleWrapsPerTurnInsteadOfGrowing)
{
	// 90 度/秒转满四圈回到 0，不会把角度累加到 4π 之外的量级。
	EXPECT_NEAR(QmCursorRotationRadians(90, 4.0f), 0.0f, 1e-5f);
	EXPECT_NEAR(QmCursorRotationRadians(90, 4.25f), pi / 8.0f, 1e-5f);
	// 长时间运行（1 小时）仍然只落在 0..2π 内。
	const float Angle = QmCursorRotationRadians(720, 3600.0f);
	EXPECT_GE(Angle, 0.0f);
	EXPECT_LT(Angle, pi * 2.0f);
}

TEST(QmCursorRotation, NegativeSpeedRotatesBackwards)
{
	const float Reverse = QmCursorRotationRadians(-90, 1.0f);
	EXPECT_FLOAT_EQ(Reverse, -pi / 2.0f);
	const float Wrapped = QmCursorRotationRadians(-45, 8.0f);
	EXPECT_NEAR(Wrapped, 0.0f, 1e-5f);
}
