#include <engine/client/video_audio.h>

extern "C" {
#include <libavutil/opt.h>
}

#include <gtest/gtest.h>

#include <array>
#include <cerrno>

namespace
{
	class CVideoAudioConversion : public testing::Test
	{
	protected:
		AVFrame *m_pInput = av_frame_alloc();
		AVFrame *m_pOutput = av_frame_alloc();
		SwrContext *m_pContext = swr_alloc();
		std::array<int16_t, 2048> m_aSamples{};

		void SetUp() override
		{
			ASSERT_NE(m_pInput, nullptr);
			ASSERT_NE(m_pOutput, nullptr);
			ASSERT_NE(m_pContext, nullptr);
			m_pInput->nb_samples = 1024;
			m_pOutput->nb_samples = 1024;
			m_pOutput->format = AV_SAMPLE_FMT_FLTP;
			m_pOutput->sample_rate = 48000;
#if LIBAVUTIL_VERSION_MAJOR >= 57
			av_channel_layout_default(&m_pOutput->ch_layout, 2);
			ASSERT_EQ(av_opt_set_chlayout(m_pContext, "in_chlayout", &m_pOutput->ch_layout, 0), 0);
			ASSERT_EQ(av_opt_set_chlayout(m_pContext, "out_chlayout", &m_pOutput->ch_layout, 0), 0);
#else
			m_pOutput->channel_layout = AV_CH_LAYOUT_STEREO;
			ASSERT_EQ(av_opt_set_int(m_pContext, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0), 0);
			ASSERT_EQ(av_opt_set_int(m_pContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0), 0);
#endif
			ASSERT_EQ(av_frame_get_buffer(m_pOutput, 0), 0);
			ASSERT_EQ(av_opt_set_int(m_pContext, "in_sample_rate", 48000, 0), 0);
			ASSERT_EQ(av_opt_set_int(m_pContext, "out_sample_rate", 48000, 0), 0);
			ASSERT_EQ(av_opt_set_sample_fmt(m_pContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0), 0);
			ASSERT_EQ(av_opt_set_sample_fmt(m_pContext, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0), 0);
			ASSERT_EQ(swr_init(m_pContext), 0);
		}

		void TearDown() override
		{
			av_frame_free(&m_pInput);
			av_frame_free(&m_pOutput);
			swr_free(&m_pContext);
		}

		int Convert(const SVideoAudioConversionApi &Api = {})
		{
			return ConvertVideoAudioFrame(m_pInput, m_pOutput, m_pContext, m_aSamples.data(), Api);
		}
	};
}

TEST_F(CVideoAudioConversion, ConvertsStereoSamplesAndReplacesPreviousFrame)
{
	m_aSamples.fill(16384);
	ASSERT_EQ(Convert(), 0);
	EXPECT_FLOAT_EQ(reinterpret_cast<float *>(m_pOutput->data[0])[0], 0.5f);
	EXPECT_FLOAT_EQ(reinterpret_cast<float *>(m_pOutput->data[1])[1023], 0.5f);
	m_aSamples.fill(-16384);
	ASSERT_EQ(Convert(), 0);
	EXPECT_FLOAT_EQ(reinterpret_cast<float *>(m_pOutput->data[0])[1023], -0.5f);
}

TEST_F(CVideoAudioConversion, FillFailureIsReturnedBeforeOutputIsTouched)
{
	SVideoAudioConversionApi Api;
	Api.m_pFill = [](uint8_t **, int *, const uint8_t *, int, int, AVSampleFormat, int) { return AVERROR(EINVAL); };
	Api.m_pMakeWritable = [](AVFrame *) -> int { ADD_FAILURE() << "failed input reached output"; return 0; };
	EXPECT_EQ(Convert(Api), AVERROR(EINVAL));
	EXPECT_EQ(Convert(), 0);
}

TEST_F(CVideoAudioConversion, WritableFailurePreventsResamplingAndAllowsRetry)
{
	SVideoAudioConversionApi Api;
	Api.m_pMakeWritable = [](AVFrame *) { return AVERROR(ENOMEM); };
	Api.m_pConvert = [](SwrContext *, uint8_t **, int, const uint8_t **, int) -> int { ADD_FAILURE() << "unwritable output reached converter"; return 0; };
	EXPECT_EQ(Convert(Api), AVERROR(ENOMEM));
	EXPECT_EQ(Convert(), 0);
}

TEST_F(CVideoAudioConversion, ResamplingErrorCannotBeReportedAsSuccess)
{
	SVideoAudioConversionApi Api;
	Api.m_pConvert = [](SwrContext *, uint8_t **, int, const uint8_t **, int) { return AVERROR(EINVAL); };
	EXPECT_EQ(Convert(Api), AVERROR(EINVAL));
	EXPECT_EQ(Convert(), 0);
}

TEST_F(CVideoAudioConversion, ShortConversionCannotSubmitStaleTailSamples)
{
	SVideoAudioConversionApi Api;
	Api.m_pConvert = [](SwrContext *, uint8_t **, int, const uint8_t **, int) { return 512; };
	EXPECT_EQ(Convert(Api), AVERROR_INVALIDDATA);
}
