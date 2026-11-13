#include <engine/client/video_frame_convert.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	class CVideoFrameConversion : public testing::Test
	{
	protected:
		static constexpr int WIDTH = 4;
		static constexpr int HEIGHT = 2;
		AVFrame *m_pOutput = av_frame_alloc();
		AVFrame *m_pRetained = nullptr;
		SwsContext *m_pContext = nullptr;
		std::vector<uint8_t> m_vRgba = std::vector<uint8_t>(WIDTH * HEIGHT * 4, 0);

		void SetUp() override
		{
			ASSERT_NE(m_pOutput, nullptr);
			m_pOutput->format = AV_PIX_FMT_RGBA;
			m_pOutput->width = WIDTH;
			m_pOutput->height = HEIGHT;
			ASSERT_EQ(av_frame_get_buffer(m_pOutput, 32), 0);
			m_pContext = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGBA, WIDTH, HEIGHT, AV_PIX_FMT_RGBA, SWS_POINT, nullptr, nullptr, nullptr);
			ASSERT_NE(m_pContext, nullptr);
		}
		void TearDown() override
		{
			av_frame_free(&m_pRetained);
			av_frame_free(&m_pOutput);
			sws_freeContext(m_pContext);
		}
		int Convert(const SVideoFrameConversionApi &Api = {})
		{
			return ConvertVideoFrame(m_pContext, m_pOutput, m_vRgba, WIDTH, HEIGHT, Api);
		}
		void Fill(uint8_t Red, uint8_t Green)
		{
			for(size_t i = 0; i < m_vRgba.size(); i += 4)
			{
				m_vRgba[i] = Red;
				m_vRgba[i + 1] = Green;
				m_vRgba[i + 2] = 0;
				m_vRgba[i + 3] = 255;
			}
		}
		void ExpectPixels(const AVFrame *pFrame, uint8_t Red, uint8_t Green)
		{
			for(int y = 0; y < HEIGHT; ++y)
				for(int x = 0; x < WIDTH; ++x)
				{
					const uint8_t *pPixel = pFrame->data[0] + y * pFrame->linesize[0] + x * 4;
					EXPECT_EQ(pPixel[0], Red) << x << "," << y;
					EXPECT_EQ(pPixel[1], Green) << x << "," << y;
					EXPECT_EQ(pPixel[2], 0);
					EXPECT_EQ(pPixel[3], 255);
				}
		}
	};
}

TEST_F(CVideoFrameConversion, ReusePreservesFrameDataRetainedByEncoder)
{
	Fill(255, 0);
	ASSERT_EQ(Convert(), 0);
	m_pRetained = av_frame_clone(m_pOutput);
	ASSERT_NE(m_pRetained, nullptr);
	Fill(0, 255);
	ASSERT_EQ(Convert(), 0);
	ExpectPixels(m_pRetained, 255, 0);
	ExpectPixels(m_pOutput, 0, 255);
}

TEST_F(CVideoFrameConversion, WritableFailurePreventsScalingAndAllowsRetry)
{
	SVideoFrameConversionApi Api;
	Api.m_pMakeWritable = [](AVFrame *) { return AVERROR(ENOMEM); };
	Api.m_pScale = [](SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *) -> int {
		ADD_FAILURE() << "unwritable output reached scaler";
		return 0;
	};
	EXPECT_EQ(Convert(Api), AVERROR(ENOMEM));
	Fill(255, 0);
	ASSERT_EQ(Convert(), 0);
	ExpectPixels(m_pOutput, 255, 0);
}

TEST_F(CVideoFrameConversion, ScalingErrorIsReturnedAndAllowsRetry)
{
	SVideoFrameConversionApi Api;
	Api.m_pScale = [](SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *) { return AVERROR(EINVAL); };
	EXPECT_EQ(Convert(Api), AVERROR(EINVAL));
	Fill(0, 255);
	ASSERT_EQ(Convert(), 0);
	ExpectPixels(m_pOutput, 0, 255);
}

TEST_F(CVideoFrameConversion, ShortConversionCannotReportStaleOutputAsComplete)
{
	SVideoFrameConversionApi Api;
	Api.m_pScale = [](SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *) { return 1; };
	EXPECT_EQ(Convert(Api), AVERROR_INVALIDDATA);
	EXPECT_EQ(Convert(), 0);
}

TEST_F(CVideoFrameConversion, ShortInputIsRejectedBeforeWritableOrScalingCalls)
{
	m_vRgba.pop_back();
	SVideoFrameConversionApi Api;
	Api.m_pMakeWritable = [](AVFrame *) -> int { ADD_FAILURE() << "short input reached output"; return 0; };
	Api.m_pScale = [](SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *) -> int {
		ADD_FAILURE() << "short input reached scaler";
		return 0;
	};
	EXPECT_EQ(Convert(Api), AVERROR_INVALIDDATA);
}

TEST_F(CVideoFrameConversion, InvalidSourceDimensionsAreRejectedBeforeConversion)
{
	SVideoFrameConversionApi Api;
	Api.m_pMakeWritable = [](AVFrame *) -> int { ADD_FAILURE() << "invalid dimensions reached output"; return 0; };
	const int aInvalid[][2] = {{0, HEIGHT}, {WIDTH, 0}, {-1, HEIGHT}, {WIDTH, -1}, {std::numeric_limits<int>::max(), HEIGHT}};
	for(const auto &aDimensions : aInvalid)
	{
		SCOPED_TRACE(testing::Message() << aDimensions[0] << "x" << aDimensions[1]);
		EXPECT_EQ(ConvertVideoFrame(m_pContext, m_pOutput, m_vRgba, aDimensions[0], aDimensions[1], Api), AVERROR(EINVAL));
	}
}

TEST_F(CVideoFrameConversion, SupersampledInputConvertsToOutputFrameHeight)
{
	sws_freeContext(m_pContext);
	m_pContext = sws_getContext(WIDTH * 2, HEIGHT * 2, AV_PIX_FMT_RGBA, WIDTH, HEIGHT, AV_PIX_FMT_RGBA, SWS_POINT, nullptr, nullptr, nullptr);
	ASSERT_NE(m_pContext, nullptr);
	m_vRgba.resize(WIDTH * HEIGHT * 4 * 4);
	Fill(0, 255);
	ASSERT_EQ(ConvertVideoFrame(m_pContext, m_pOutput, m_vRgba, WIDTH * 2, HEIGHT * 2), 0);
	ExpectPixels(m_pOutput, 0, 255);
}
