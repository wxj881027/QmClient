// QmMediaCoverSpin 行为测试：圆形封面的唱片自转只跟播放状态走。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/math.h>

#include <game/client/components/qmclient/media_cover_spin.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmMediaCoverSpin, PlayingAdvancesOneTurnPerPeriod)
{
	const float PerSecond = 2.0f * pi / QM_MEDIA_COVER_SPIN_PERIOD_SECONDS;
	const float Frame = 1.0f / 60.0f;
	SQmMediaCoverSpin Spin;
	float Now = 0.0f;
	Spin.Advance(true, true, Now);
	// Advance 是「每帧一次」的推进：单帧增量上限 0.1s，所以按 60fps 累积一秒钟。
	for(int i = 0; i < 60; ++i)
		Spin.Advance(true, true, Now += Frame);
	EXPECT_NEAR(Spin.m_Angle, PerSecond, 0.01f);
	// 一整圈之后回到起点而不是无限累加：角度始终在 [0, 2π)。
	const int FramesPerTurn = static_cast<int>(std::lround(QM_MEDIA_COVER_SPIN_PERIOD_SECONDS / Frame));
	for(int i = 0; i < FramesPerTurn - 60; ++i)
		Spin.Advance(true, true, Now += Frame);
	EXPECT_NEAR(Spin.m_Angle, 0.0f, 0.01f);
	EXPECT_GE(Spin.m_Angle, 0.0f);
	EXPECT_LT(Spin.m_Angle, 2.0f * pi);
}

TEST(QmMediaCoverSpin, PausedKeepsCurrentAngle)
{
	SQmMediaCoverSpin Spin;
	Spin.Advance(true, true, 0.0f);
	const float Angle = Spin.Advance(true, true, 1.0f);
	// 暂停 10 秒：角度必须定格在暂停那一刻，恢复播放再从这个角度继续。
	EXPECT_FLOAT_EQ(Spin.Advance(false, true, 11.0f), Angle);
	EXPECT_GT(Spin.Advance(true, true, 12.0f), Angle);
}

TEST(QmMediaCoverSpin, MotionDisabledNeverRotates)
{
	SQmMediaCoverSpin Spin;
	for(int Step = 0; Step <= 10; ++Step)
		EXPECT_FLOAT_EQ(Spin.Advance(true, false, Step * 0.5f), 0.0f);
}

TEST(QmMediaCoverSpin, LongGapOnlyAdvancesClampedSlice)
{
	SQmMediaCoverSpin Spin;
	Spin.Advance(true, true, 0.0f);
	// 界面隐藏十秒后回来：只补 0.1 秒，不要让封面突然跳几十度。
	const float ExpectedAngle = 0.1f * (2.0f * pi / QM_MEDIA_COVER_SPIN_PERIOD_SECONDS);
	EXPECT_NEAR(Spin.Advance(true, true, 10.0f), ExpectedAngle, 0.001f);
	// Reset 后回到零角，并从下一次调用重新计时。
	Spin.Reset();
	EXPECT_FLOAT_EQ(Spin.m_Angle, 0.0f);
	EXPECT_FALSE(Spin.m_HasLastTime);
}
