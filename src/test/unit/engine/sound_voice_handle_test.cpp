#include <engine/client/sound.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>

// 只准备内存 PCM 和混音缓冲；播放、回收、控制和混音均使用生产实现。
class CSoundVoiceTestAccess
{
public:
	static int VoiceCount() { return CSound::NUM_VOICES; }

	static bool PrepareBuffer(CSound &Sound)
	{
		Sound.m_MaxFrames = 4;
		Sound.m_pMixBuffer = static_cast<int *>(std::calloc(8, sizeof(int)));
		return Sound.m_pMixBuffer != nullptr;
	}

	static int LoadPcm(CSound &Sound, int Frames = 8, int Channels = 2)
	{
		CSample *pSample = Sound.AllocSample();
		if(!pSample)
			return -1;
		pSample->m_pData = static_cast<short *>(std::calloc(Frames * Channels, sizeof(short)));
		if(!pSample->m_pData)
		{
			Sound.UnloadSample(pSample->m_Index);
			return -1;
		}
		std::fill_n(pSample->m_pData, Frames * Channels, short{1024});
		pSample->m_NumFrames = Frames;
		pSample->m_Rate = 48000;
		pSample->m_Channels = Channels;
		pSample->m_LoopStart = 0;
		pSample->m_PausedAt = 0;
		return pSample->m_Index;
	}

	static void FreeBuffer(CSound &Sound)
	{
		std::free(Sound.m_pMixBuffer);
		Sound.m_pMixBuffer = nullptr;
	}

	static void PrepareTiming(CSound &Sound, int SampleId, ISound::CVoiceHandle Voice, int Frames, int Tick, int LoopStart)
	{
		const CLockScope LockScope(Sound.m_SoundLock);
		// 时间同步只读元数据；这些用例不混音，不申请与逻辑帧数同规模的 PCM。
		Sound.m_aSamples[SampleId].m_NumFrames = Frames;
		Sound.m_aSamples[SampleId].m_Channels = 1;
		Sound.m_aSamples[SampleId].m_LoopStart = LoopStart;
		Sound.m_aVoices[Voice.Id()].m_Tick = Tick;
	}
};

namespace
{
	class SoundVoiceHandle : public ::testing::Test
	{
	protected:
		CSound m_Sound;
		int m_SampleId = -1;
		ISound::CVoiceHandle m_Old;
		ISound::CVoiceHandle m_Replacement;

		void SetUp() override
		{
			ASSERT_TRUE(CSoundVoiceTestAccess::PrepareBuffer(m_Sound));
			m_SampleId = CSoundVoiceTestAccess::LoadPcm(m_Sound);
			ASSERT_GE(m_SampleId, 0);
		}

		void TearDown() override
		{
			m_Sound.StopAll();
			m_Sound.UnloadSample(m_SampleId);
			CSoundVoiceTestAccess::FreeBuffer(m_Sound);
		}

		void FillVoices(int OldFlags = ISound::FLAG_LOOP)
		{
			m_Old = m_Sound.Play(0, m_SampleId, OldFlags, 1.0f);
			ASSERT_TRUE(m_Old.IsValid());
			for(int i = 1; i < CSoundVoiceTestAccess::VoiceCount(); ++i)
				ASSERT_TRUE(m_Sound.Play(0, m_SampleId, ISound::FLAG_LOOP, 0.0f).IsValid());
			ASSERT_FALSE(m_Sound.Play(0, m_SampleId, 0, 1.0f).IsValid());
		}

		void PlayReplacement(int Flags = ISound::FLAG_LOOP, vec2 Position = vec2(0.0f, 0.0f))
		{
			m_Replacement = m_Sound.PlayAt(0, m_SampleId, Flags, 1.0f, Position);
			ASSERT_TRUE(m_Replacement.IsValid());
			ASSERT_EQ(m_Replacement.Id(), m_Old.Id());
			EXPECT_FALSE(m_Replacement == m_Old);
		}

		void ReplaceStoppedVoice(int Flags = ISound::FLAG_LOOP, vec2 Position = vec2(0.0f, 0.0f))
		{
			FillVoices();
			m_Sound.StopAll();
			PlayReplacement(Flags, Position);
		}

		std::array<short, 8> Mix()
		{
			std::array<short, 8> aOutput{};
			m_Sound.Mix(aOutput.data(), 4);
			return aOutput;
		}

		void ExpectAudible()
		{
			for(short Sample : Mix())
				EXPECT_GT(Sample, 0);
		}

		void PrepareTiming(int Frames, int Tick, int Flags = ISound::FLAG_LOOP, int LoopStart = 0)
		{
			m_Old = m_Sound.Play(0, m_SampleId, Flags, 1.0f);
			ASSERT_TRUE(m_Old.IsValid());
			CSoundVoiceTestAccess::PrepareTiming(m_Sound, m_SampleId, m_Old, Frames, Tick, LoopStart);
		}
	};
}

TEST_F(SoundVoiceHandle, PausePreservesResumePositionAndRejectsOldHandle)
{
	FillVoices();
	ExpectAudible();
	m_Sound.Pause(m_SampleId);
	PlayReplacement();
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 4.0f / 48000.0f);
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, StopRejectsOldHandleAfterSlotReuse)
{
	FillVoices();
	m_Sound.Stop(m_SampleId);
	PlayReplacement();
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, StopAllRejectsOldHandleAfterSlotReuse)
{
	ReplaceStoppedVoice();
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, UnloadAndReloadDoNotReviveOldHandle)
{
	FillVoices();
	m_Sound.UnloadSample(m_SampleId);
	m_SampleId = CSoundVoiceTestAccess::LoadPcm(m_Sound);
	ASSERT_GE(m_SampleId, 0);
	PlayReplacement();
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, NaturalEndRejectsOldHandleAfterSlotReuse)
{
	FillVoices(0);
	ExpectAudible();
	ExpectAudible();
	PlayReplacement();
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, StopVoiceRejectsOldHandleAfterSlotReuse)
{
	FillVoices();
	m_Sound.StopVoice(m_Old);
	PlayReplacement();
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, OldVolumeCannotMuteReplacement)
{
	ReplaceStoppedVoice();
	m_Sound.SetVoiceVolume(m_Old, 0.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, OldPositionCannotMoveReplacementOutOfRange)
{
	ReplaceStoppedVoice();
	m_Sound.SetChannel(0, 1.0f, 1.0f);
	m_Sound.SetVoicePosition(m_Old, vec2(5000.0f, 0.0f));
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, OldFalloffCannotChangeReplacement)
{
	ReplaceStoppedVoice(ISound::FLAG_LOOP, vec2(750.0f, 0.0f));
	m_Sound.SetChannel(0, 1.0f, 1.0f);
	const auto aBefore = Mix();
	ASSERT_GT(aBefore[0], 0);
	m_Sound.SetVoiceFalloff(m_Old, 1.0f);
	EXPECT_EQ(Mix(), aBefore);
}

TEST_F(SoundVoiceHandle, OldCircleCannotMuteReplacement)
{
	ReplaceStoppedVoice();
	m_Sound.SetChannel(0, 1.0f, 1.0f);
	m_Sound.SetVoiceCircle(m_Old, 0.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, OldRectangleCannotMuteReplacement)
{
	ReplaceStoppedVoice();
	m_Sound.SetChannel(0, 1.0f, 1.0f);
	m_Sound.SetVoiceRectangle(m_Old, 0.0f, 0.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, OldTimeOffsetCannotSeekReplacement)
{
	ReplaceStoppedVoice(0);
	ExpectAudible();
	m_Sound.SetVoiceTimeOffset(m_Old, 1.0f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 4.0f / 48000.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, CurrentHandleStillChangesItsOwnVolume)
{
	ReplaceStoppedVoice();
	m_Sound.SetVoiceVolume(m_Replacement, 0.0f);
	EXPECT_EQ(Mix(), (std::array<short, 8>{}));
	m_Sound.SetVoiceVolume(m_Replacement, 1.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, CurrentHandleStillStopsItsOwnPlayback)
{
	ReplaceStoppedVoice();
	m_Sound.StopVoice(m_Replacement);
	EXPECT_EQ(Mix(), (std::array<short, 8>{}));
}

TEST_F(SoundVoiceHandle, RepeatedStopWithOldHandleKeepsReplacementPlaying)
{
	ReplaceStoppedVoice();
	m_Sound.StopVoice(m_Old);
	m_Sound.StopVoice(m_Old);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, GlobalStopThenOwnerCleanupKeepsLoopResumePosition)
{
	FillVoices();
	ExpectAudible();
	m_Sound.StopAll();
	m_Sound.StopVoice(m_Old);
	PlayReplacement();
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 4.0f / 48000.0f);
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, SingleStereoFrameIsMixedBeforePlaybackEnds)
{
	m_Sound.UnloadSample(m_SampleId);
	m_SampleId = CSoundVoiceTestAccess::LoadPcm(m_Sound, 1, 2);
	ASSERT_GE(m_SampleId, 0);
	ASSERT_TRUE(m_Sound.Play(0, m_SampleId, 0, 1.0f).IsValid());
	const auto aOutput = Mix();
	EXPECT_GT(aOutput[0], 0);
	EXPECT_EQ(aOutput[0], aOutput[1]);
	for(size_t i = 2; i < aOutput.size(); ++i)
		EXPECT_EQ(aOutput[i], 0);
	EXPECT_FALSE(m_Sound.IsPlaying(m_SampleId));
}

TEST_F(SoundVoiceHandle, SingleMonoFrameIsMixedToBothChannels)
{
	m_Sound.UnloadSample(m_SampleId);
	m_SampleId = CSoundVoiceTestAccess::LoadPcm(m_Sound, 1, 1);
	ASSERT_GE(m_SampleId, 0);
	ASSERT_TRUE(m_Sound.Play(0, m_SampleId, 0, 1.0f).IsValid());
	const auto aOutput = Mix();
	EXPECT_GT(aOutput[0], 0);
	EXPECT_EQ(aOutput[0], aOutput[1]);
	for(size_t i = 2; i < aOutput.size(); ++i)
		EXPECT_EQ(aOutput[i], 0);
	EXPECT_FALSE(m_Sound.IsPlaying(m_SampleId));
}

TEST_F(SoundVoiceHandle, SeekingToEndEndsPlaybackWithoutMixingSampleData)
{
	ASSERT_TRUE(m_Sound.Play(0, m_SampleId, 0, 1.0f).IsValid());
	m_Sound.SetSampleCurrentTime(m_SampleId, 1.0f);
	EXPECT_EQ(Mix(), (std::array<short, 8>{}));
	EXPECT_FALSE(m_Sound.IsPlaying(m_SampleId));
}

TEST_F(SoundVoiceHandle, LoopingSeekToEndResumesOnTheFollowingMix)
{
	ASSERT_TRUE(m_Sound.Play(0, m_SampleId, ISound::FLAG_LOOP, 1.0f).IsValid());
	m_Sound.SetSampleCurrentTime(m_SampleId, 1.0f);
	EXPECT_EQ(Mix(), (std::array<short, 8>{}));
	EXPECT_TRUE(m_Sound.IsPlaying(m_SampleId));
	ExpectAudible();
}

TEST_F(SoundVoiceHandle, TimeOffsetWithinThresholdKeepsTheCurrentTick)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(48000, 10000));
	m_Sound.SetVoiceTimeOffset(m_Old, 0.25f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 10000.0f / 48000.0f);
}

TEST_F(SoundVoiceHandle, TimeOffsetExactlyAtThresholdKeepsTheCurrentTick)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(96000, 26400));
	m_Sound.SetVoiceTimeOffset(m_Old, 0.75f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 26400.0f / 48000.0f);
}

TEST_F(SoundVoiceHandle, TimeOffsetBeyondThresholdMovesToTheRequestedTick)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(96000, 1000));
	m_Sound.SetVoiceTimeOffset(m_Old, 1.0f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 1.0f);
}

TEST_F(SoundVoiceHandle, LoopingTimeOffsetNearWrapKeepsTheCurrentTick)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(48000, 47000));
	m_Sound.SetVoiceTimeOffset(m_Old, 0.015625f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 47000.0f / 48000.0f);
}

TEST_F(SoundVoiceHandle, NonLoopingTimeOffsetNearWrapMovesToTheRequestedTick)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(48000, 47000, 0));
	m_Sound.SetVoiceTimeOffset(m_Old, 0.015625f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 0.015625f);
}

TEST_F(SoundVoiceHandle, TimeOffsetAfterFirstPassWrapsInsideTheLoopSection)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(48000, 0, ISound::FLAG_LOOP, 12000));
	m_Sound.SetVoiceTimeOffset(m_Old, 1.5f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 0.75f);
}

TEST_F(SoundVoiceHandle, LargeMonoFrameCountsSynchronizeWithoutSignedIntermediateOverflow)
{
	struct SCase
	{
		int m_Current;
		float m_Offset;
		int m_Expected;
	};
	for(const SCase &Case : {SCase{1500000000, 32768.0f, 1572864000}, SCase{1572864000, 31250.0f, 1500000000}})
	{
		SCOPED_TRACE(Case.m_Current);
		ASSERT_NO_FATAL_FAILURE(PrepareTiming(1800000000, Case.m_Current));
		m_Sound.SetVoiceTimeOffset(m_Old, Case.m_Offset);
		EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), Case.m_Expected / 48000.0f);
		m_Sound.StopVoice(m_Old);
	}
}

TEST_F(SoundVoiceHandle, NonLoopingTimeOffsetAfterTheSampleClampsToTheEnd)
{
	ASSERT_NO_FATAL_FAILURE(PrepareTiming(48000, 0, 0));
	m_Sound.SetVoiceTimeOffset(m_Old, 2.0f);
	EXPECT_FLOAT_EQ(m_Sound.GetSampleCurrentTime(m_SampleId), 1.0f);
}
