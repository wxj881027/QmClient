#include <game/client/components/qmclient/practice_cursor.h>

#include <gtest/gtest.h>

TEST(QmPracticeCursor, UnitZoomPreservesTheCursorTarget)
{
	const vec2 Target(1120.0f, 440.0f);
	EXPECT_EQ(QmPracticeCursorWorldPosition(Target, vec2(1000.0f, 500.0f), 1.0f, false), Target);
}

TEST(QmPracticeCursor, ZoomingOutReachesTheWorldPointUnderTheCursor)
{
	// 地图缩小一半后，准心距屏幕中心 (120, -60) 对应世界偏移 (240, -120)。
	EXPECT_EQ(QmPracticeCursorWorldPosition(vec2(1120.0f, 440.0f), vec2(1000.0f, 500.0f), 2.0f, false),
		vec2(1240.0f, 380.0f));
}

TEST(QmPracticeCursor, ZoomingInReachesTheWorldPointUnderTheCursor)
{
	EXPECT_EQ(QmPracticeCursorWorldPosition(vec2(1120.0f, 440.0f), vec2(1000.0f, 500.0f), 0.5f, false),
		vec2(1060.0f, 470.0f));
}

TEST(QmPracticeCursor, OffsetCameraUsesItsRenderedCenter)
{
	// 动态镜头或漂移让镜头偏离角色时，换算仍以实际渲染中心为基准。
	EXPECT_EQ(QmPracticeCursorWorldPosition(vec2(1120.0f, 440.0f), vec2(1070.0f, 530.0f), 2.0f, false),
		vec2(1170.0f, 350.0f));
}

TEST(QmPracticeCursor, CenteredCursorStaysAtTheCameraCenterWhileZoomChanges)
{
	const vec2 Center(1000.0f, 500.0f);
	for(const float Zoom : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(Zoom);
		EXPECT_EQ(QmPracticeCursorWorldPosition(Center, Center, Zoom, false), Center);
	}
}

TEST(QmPracticeCursor, FreeviewUsesTheVisibleCenterBeforeCameraSmoothingReachesTheTarget)
{
	const vec2 Center(1000.0f, 500.0f);
	const vec2 MouseTarget(1400.0f, 800.0f);
	for(const float Zoom : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(Zoom);
		EXPECT_EQ(QmPracticeCursorWorldPosition(MouseTarget, Center, Zoom, true), Center);
	}
}

TEST(QmPracticeCursor, SpaceFollowsTheViewMode)
{
	EXPECT_EQ(QmPracticeCursorSpaceFor(false, false), EQmPracticeCursorSpace::PLAYER);
	EXPECT_EQ(QmPracticeCursorSpaceFor(false, true), EQmPracticeCursorSpace::PLAYER);
	EXPECT_EQ(QmPracticeCursorSpaceFor(true, true), EQmPracticeCursorSpace::FOLLOW);
	EXPECT_EQ(QmPracticeCursorSpaceFor(true, false), EQmPracticeCursorSpace::FREEVIEW);
}

TEST(QmPracticeCursor, FollowSpaceUsesTheSpectatedWorldPosition)
{
	// 跟踪旁观时 m_aTargetPos 以被跟随玩家的世界坐标为基准，镜头是否被动态
	// 镜头或漂移推开都不影响落点。
	const vec2 Target(1120.0f, 440.0f);
	const vec2 SpecPosition(1000.0f, 500.0f);
	const vec2 CameraCenter(1070.0f, 530.0f);
	EXPECT_EQ(QmPracticeCursorWorldPosition(Target, CameraCenter, SpecPosition, 2.0f, EQmPracticeCursorSpace::FOLLOW),
		vec2(1240.0f, 380.0f));
}

TEST(QmPracticeCursor, FollowSpaceIgnoresCameraOffsetAtUnitZoom)
{
	const vec2 Target(1120.0f, 440.0f);
	const vec2 SpecPosition(1000.0f, 500.0f);
	EXPECT_EQ(QmPracticeCursorWorldPosition(Target, vec2(1400.0f, 900.0f), SpecPosition, 1.0f, EQmPracticeCursorSpace::FOLLOW),
		Target);
}

TEST(QmPracticeCursor, HookIsReleasedOnlyForNonParticipantTargets)
{
	EXPECT_FALSE(QmPracticeHookShouldRelease(-1, 3, 7));
	EXPECT_FALSE(QmPracticeHookShouldRelease(3, 3, 7));
	EXPECT_FALSE(QmPracticeHookShouldRelease(7, 3, 7));
	EXPECT_TRUE(QmPracticeHookShouldRelease(5, 3, 7));
	// 没有分身时 B 为 -1，任何玩家目标都应释放。
	EXPECT_TRUE(QmPracticeHookShouldRelease(5, 3, -1));
	EXPECT_FALSE(QmPracticeHookShouldRelease(3, 3, -1));
}
