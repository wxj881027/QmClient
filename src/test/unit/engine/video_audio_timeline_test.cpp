#include <engine/client/video_audio.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
	// 模拟 NextAudioFrameTimeline 逐帧推进，观察跨帧累计后的时间轴总量。
	void AdvanceFrames(int64_t &Samples, uint64_t &Remainder, int SampleRate, int FPS, int Frames)
	{
		for(int Frame = 0; Frame < Frames; ++Frame)
			AdvanceVideoAudioTimeline(Samples, Remainder, SampleRate, FPS);
	}
}

TEST(VideoAudioTimeline, IntegerFrameRateAddsExactSamples)
{
	int64_t Samples = 0;
	uint64_t Remainder = 0;
	AdvanceFrames(Samples, Remainder, 48000, 60, 60);
	EXPECT_EQ(Samples, 48000);
	EXPECT_EQ(Remainder, 0u);
}

// 48 kHz/144 FPS 每帧 333.33 样本；整帧截断会让一小时录像的音频少 3.6 秒。
TEST(VideoAudioTimeline, FractionalFrameRateKeepsLongRecordingInSync)
{
	int64_t Samples = 0;
	uint64_t Remainder = 0;
	AdvanceFrames(Samples, Remainder, 48000, 144, 144 * 3600);
	EXPECT_EQ(Samples, 48000 * 3600);
	EXPECT_EQ(Remainder, 0u);
}

// 59 FPS 的余数不会在 3600 秒内自行归零，验证余数跨帧保留而不是按秒重置。
TEST(VideoAudioTimeline, RemainderCarriesAcrossSecondsAtOddFrameRate)
{
	int64_t Samples = 0;
	uint64_t Remainder = 0;
	for(int Second = 0; Second < 3600; ++Second)
	{
		AdvanceFrames(Samples, Remainder, 48000, 59, 59);
		EXPECT_LT(Remainder, 59u);
	}
	EXPECT_EQ(Samples, 48000 * 3600);
}

TEST(VideoAudioTimeline, InvalidFrameRateDoesNotMutateState)
{
	int64_t Samples = 17;
	uint64_t Remainder = 5;
	AdvanceVideoAudioTimeline(Samples, Remainder, 48000, 0);
	EXPECT_EQ(Samples, 17);
	EXPECT_EQ(Remainder, 5u);
	AdvanceVideoAudioTimeline(Samples, Remainder, 0, 60);
	EXPECT_EQ(Samples, 17);
	EXPECT_EQ(Remainder, 5u);
}
