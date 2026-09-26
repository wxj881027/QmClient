// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

// 钩子倒计时环的纯逻辑：只覆盖寿命/进度换算与跟随位置、配色，
// 不覆盖 CHud 里的状态机（那一层要真实 GameClient 快照才能跑）。

TEST(QmHudHookCountdown, ProgressFollowsElapsedTimeAndFreezesAfterRelease)
{
	// 默认 tuning：m_HookDuration = 1.25 → 收回瞬间完成，寿命就是钩住玩家的 1.2 秒上限。
	EXPECT_FLOAT_EQ(QmHudHookCountdownLifespanSeconds(1.25f), 1.2f);
	// 地图把 hook_duration 调小 → 收回更慢 → 一轮动作更长（下限 1.25 收满 1.25 秒）。
	EXPECT_FLOAT_EQ(QmHudHookCountdownLifespanSeconds(0.0f), 2.45f);
	// 超过 1.25 的 tuning 引擎侧收回已经是瞬时，钳到最短 1.2 秒，不会得到负寿命。
	EXPECT_FLOAT_EQ(QmHudHookCountdownLifespanSeconds(100.0f), 1.2f);

	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(1.25f, 0.0f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(1.25f, 0.3f, 1.0f), 0.75f);
	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(1.25f, 1.2f, 1.0f), 0.0f);
	// 钩住时间超过一轮寿命也不会变成负进度。
	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(1.25f, 5.0f, 1.0f), 0.0f);
	// 同一个 tick 重复求值不能把进度推回去（环只能往下走）。
	EXPECT_LE(QmHudHookCountdownProgress(1.25f, 0.3f, 0.5f), 0.5f);
	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(1.25f, 0.3f, 0.5f), 0.5f);

	// 收回更慢的地图：同样的持钩时间还剩得更多。
	const float SlowLifespan = QmHudHookCountdownLifespanSeconds(0.25f);
	EXPECT_GT(SlowLifespan, 1.2f);
	EXPECT_FLOAT_EQ(QmHudHookCountdownProgress(0.25f, 1.2f, 1.0f), 5.0f / 11.0f);
}

TEST(QmHudHookCountdown, RingSitsRightAboveTheSwitchRingAndKeepsBlueColor)
{
	const vec2 TeePosition(100.0f, 100.0f);
	const vec2 SwitchTarget = QmHudSwitchCountdownFollowTarget(TeePosition, -1, 0, 0.0f);
	const vec2 HookTarget = QmHudHookCountdownFollowTarget(TeePosition, -1, 0.0f);
	// 正上方：x 与开关环完全对齐，y 更小（屏幕坐标向上）。
	EXPECT_FLOAT_EQ(HookTarget.x, SwitchTarget.x);
	EXPECT_LT(HookTarget.y, SwitchTarget.y);
	// 抬升量必须大于两个卫星半径之和（各约 10.25），否则两个环会叠在一起。
	EXPECT_FLOAT_EQ(SwitchTarget.y - HookTarget.y, QM_HUD_HOOK_COUNTDOWN_RISE);
	EXPECT_GT(QM_HUD_HOOK_COUNTDOWN_RISE, 2.0f * (9.0f + 2.5f * 0.5f) * 0.75f);
	// 换到另一侧同样是正上方。
	const vec2 RightHookTarget = QmHudHookCountdownFollowTarget(TeePosition, 1, 0.0f);
	EXPECT_FLOAT_EQ(RightHookTarget.x, QmHudSwitchCountdownFollowTarget(TeePosition, 1, 0, 0.0f).x);
	EXPECT_LT(RightHookTarget.y, SwitchTarget.y);

	const ColorRGBA Color = QmHudHookCountdownColor();
	EXPECT_GT(Color.b, Color.r);
	EXPECT_GT(Color.b, Color.g);
	EXPECT_FLOAT_EQ(Color.a, 1.0f);
}

// SDF 羽化宽度用的像素比例：开关环与钩子环共用，取 x/y 里较大的方向。
TEST(QmHudHookCountdown, ScreenPixelSizeUsesTheWiderAxisAndGuardsZeroSize)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 200.0f, 100.0f, 100, 100), 2.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 300.0f, 100, 100), 3.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 100.0f, 0, 0), 100.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 400.0f, 300.0f, 800, 600), 0.5f);
}
