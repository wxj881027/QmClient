#include <engine/client/audio_sdl3.h>
#include <engine/shared/config.h>
#include <engine/shared/video.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
	// 只替换录像端，不启动编码器或声卡；声音入口和 SDL 转换流使用生产实现。
	class CRecordingForAudio : public IVideo
	{
		const bool m_HasAudio;
		bool m_Recording = true;

	public:
		explicit CRecordingForAudio(bool HasAudio) :
			m_HasAudio(HasAudio) { ms_pCurrentVideo = this; }
		~CRecordingForAudio() override { ms_pCurrentVideo = nullptr; }
		bool Start() override
		{
			ADD_FAILURE() << "unexpected encoder start";
			return false;
		}
		void Stop() override { ADD_FAILURE() << "unexpected encoder stop"; }
		void Pause(bool Paused) override { m_Recording = !Paused; }
		bool IsRecording() const override { return m_Recording; }
		bool HasError() const override { return false; }
		bool HasAudio() const override { return m_HasAudio; }
		void NextVideoFrame() override { ADD_FAILURE() << "unexpected video frame"; }
		void NextVideoFrameThread() override { ADD_FAILURE() << "unexpected video worker"; }
		void NextAudioFrame(ISoundMixFunc) override { ADD_FAILURE() << "unexpected recorder mix"; }
		void NextAudioFrameTimeline(ISoundMixFunc) override { ADD_FAILURE() << "unexpected audio timeline"; }
	};

	class AudioRecordingRouting : public ::testing::Test
	{
	protected:
		int m_PreviousVideoSound = 0;
		int m_MixCalls = 0;
		std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> m_pStream{nullptr, SDL_DestroyAudioStream};

		void SetUp() override
		{
			m_PreviousVideoSound = g_Config.m_ClVideoSndEnable;
			g_Config.m_ClVideoSndEnable = 1;
			ASSERT_EQ(IVideo::Current(), nullptr);
			SDL_AudioSpec Spec{SDL_AUDIO_S16, 2, 48000};
			m_pStream.reset(SDL_CreateAudioStream(&Spec, &Spec));
			ASSERT_NE(m_pStream, nullptr) << SDL_GetError();
		}

		void TearDown() override
		{
			EXPECT_EQ(IVideo::Current(), nullptr);
			g_Config.m_ClVideoSndEnable = m_PreviousVideoSound;
		}

		std::array<int16_t, 4> RequestAudio()
		{
			std::array<int16_t, 4> aBuffer{1, 2, 3, 4};
			EXPECT_TRUE(FillSdlPlaybackAudioStream(m_pStream.get(), sizeof(aBuffer), 2, aBuffer.data(), 2, [&](int16_t *pSamples, int Frames) {
				++m_MixCalls;
				std::fill_n(pSamples, Frames * 2, int16_t{123});
			}));
			std::array<int16_t, 4> aOutput{};
			EXPECT_EQ(SDL_GetAudioStreamData(m_pStream.get(), aOutput.data(), sizeof(aOutput)), sizeof(aOutput));
			return aOutput;
		}
	};
}

TEST_F(AudioRecordingRouting, WithoutARecorderTheDeviceContinuesMixing)
{
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{123, 123, 123, 123}));
	EXPECT_EQ(m_MixCalls, 1);
}

TEST_F(AudioRecordingRouting, DisablingTheOptionDoesNotGiveTheDeviceASecondMixOwner)
{
	CRecordingForAudio Recording(true);
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{}));
	g_Config.m_ClVideoSndEnable = 0;
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{}));
	EXPECT_EQ(m_MixCalls, 0);
}

TEST_F(AudioRecordingRouting, EnablingTheOptionDoesNotSilenceAnExistingSilentRecording)
{
	g_Config.m_ClVideoSndEnable = 0;
	CRecordingForAudio Recording(false);
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{123, 123, 123, 123}));
	g_Config.m_ClVideoSndEnable = 1;
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{123, 123, 123, 123}));
	EXPECT_EQ(m_MixCalls, 2);
}

TEST_F(AudioRecordingRouting, PausedRecordingKeepsTheDeviceFromAdvancingItsSounds)
{
	CRecordingForAudio Recording(true);
	Recording.Pause(true);
	ASSERT_FALSE(Recording.IsRecording());
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{}));
	EXPECT_EQ(m_MixCalls, 0);
}

TEST_F(AudioRecordingRouting, DestroyingTheRecorderReturnsMixingToTheDevice)
{
	{
		CRecordingForAudio Recording(true);
		EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{}));
	}
	EXPECT_EQ(RequestAudio(), (std::array<int16_t, 4>{123, 123, 123, 123}));
	EXPECT_EQ(m_MixCalls, 1);
}
