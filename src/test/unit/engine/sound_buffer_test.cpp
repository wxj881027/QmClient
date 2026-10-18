#include <engine/client/sound_buffer.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

TEST(SoundMixSample, ThreeLoudVoicesSaturateWithoutWrappingSign)
{
	EXPECT_EQ(QmSoundMixSample(3 * 32767 * 255, 100), 32767);
	EXPECT_EQ(QmSoundMixSample(3 * -32768 * 255, 100), -32768);
}

TEST(SoundMixSample, AllActiveVoicesRemainWithinPcmRange)
{
	EXPECT_EQ(QmSoundMixSample(256 * 32767 * 255, 100), 32767);
	EXPECT_EQ(QmSoundMixSample(256 * -32768 * 255, 100), -32768);
}

TEST(SoundMixSample, QuietSamplesKeepTheExistingVolumeScale)
{
	EXPECT_EQ(QmSoundMixSample(25856, 100), 100);
	EXPECT_EQ(QmSoundMixSample(25856, 50), 50);
	EXPECT_EQ(QmSoundMixSample(-25856, 50), -50);
}

TEST(SoundMixSample, MutingLoudSamplesProducesSilence)
{
	EXPECT_EQ(QmSoundMixSample(256 * 32767 * 255, 0), 0);
	EXPECT_EQ(QmSoundMixSample(256 * -32768 * 255, 0), 0);
}

TEST(SoundBuffer, AllocationFailureNeverCallsDecoder)
{
	bool Called = false;
	auto pBuffer = QmCreateSoundBuffer<short>(8, 2, [&](short *, int) {
		Called = true;
		return true; }, [](size_t, size_t) -> void * { return nullptr; });
	EXPECT_EQ(pBuffer, nullptr);
	EXPECT_FALSE(Called);
}

TEST(SoundBuffer, InvalidFrameAndChannelCountsNeverAllocateOrDecode)
{
	const auto NeverFill = [](short *, int) { ADD_FAILURE() << "invalid input reached decoder"; return true; };
	const auto NeverAllocate = [](size_t, size_t) -> void * { ADD_FAILURE() << "invalid input reached allocator"; return nullptr; };
	for(const int64_t Frames : std::array<int64_t, 4>{-1, 0, std::numeric_limits<int>::max(), std::numeric_limits<int64_t>::max()})
		EXPECT_EQ(QmCreateSoundBuffer<short>(Frames, 2, NeverFill, NeverAllocate), nullptr) << Frames;
	for(const int Channels : {-1, 0, 3})
		EXPECT_EQ(QmCreateSoundBuffer<short>(1, Channels, NeverFill, NeverAllocate), nullptr) << Channels;
}

TEST(SoundBuffer, StereoBufferPublishesEveryInterleavedSample)
{
	auto pBuffer = QmCreateSoundBuffer<short>(3, 2, [](short *pData, int Count) {
		EXPECT_EQ(Count, 6);
		for(int Index = 0; Index < Count; ++Index)
			pData[Index] = static_cast<short>(Index + 1);
		return true;
	});
	ASSERT_NE(pBuffer, nullptr);
	EXPECT_EQ(pBuffer.get()[0], 1);
	EXPECT_EQ(pBuffer.get()[5], 6);
}

TEST(SoundBuffer, DecoderFailureDoesNotPublishPartialSamples)
{
	auto pBuffer = QmCreateSoundBuffer<short>(4, 1, [](short *pData, int) {
		pData[0] = 123;
		return false;
	});
	EXPECT_EQ(pBuffer, nullptr);
}

TEST(SoundBuffer, SampleCountChecksInterleavedIntegerBoundary)
{
	size_t Count = 0;
	const int Limit = std::numeric_limits<int>::max() / 2;
	ASSERT_TRUE(QmSoundSampleCount(Limit, 2, Count));
	EXPECT_EQ(Count, static_cast<size_t>(Limit) * 2);
	EXPECT_FALSE(QmSoundSampleCount(static_cast<int64_t>(Limit) + 1, 2, Count));
	EXPECT_EQ(Count, 0u);
}

TEST(SoundBuffer, ResamplingRejectsUnrepresentableAndEmptyOutput)
{
	EXPECT_EQ(QmSoundResampledFrames(48000, 2, 48000, 24000), 24000);
	EXPECT_EQ(QmSoundResampledFrames(24000, 1, 24000, 48000), 48000);
	EXPECT_EQ(QmSoundResampledFrames(48000, 2, 0, 48000), 0);
	EXPECT_EQ(QmSoundResampledFrames(48000, 2, 48000, 0), 0);
	EXPECT_EQ(QmSoundResampledFrames(1, 1, 48000, 1), 0);
	EXPECT_EQ(QmSoundResampledFrames(std::numeric_limits<int>::max() / 2, 2, 1, 48000), 0);
}

TEST(SoundBuffer, DownsampledLoopAlwaysHasAtLeastOnePlayableFrame)
{
	const int Frames = QmSoundResampledFrames(3, 1, 48000, 16000);
	ASSERT_EQ(Frames, 1);
	EXPECT_EQ(QmSoundResampledLoopStart(2, 48000, 16000, Frames), 0);
	EXPECT_EQ(QmSoundResampledLoopStart(24000, 48000, 24000, 48000), 12000);
	EXPECT_EQ(QmSoundResampledLoopStart(0, 48000, 48000, 48000), 0);
}
