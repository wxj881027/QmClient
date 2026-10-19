#include <engine/client/video_encoder.h>

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <base/system.h>

#include <vector>

TEST(VideoEncoder, ProbeListsSoftwareEncoderAsAvailable)
{
	const std::vector<video_encoder::SProbeResult> &vResults = video_encoder::ProbeEncoders();
	ASSERT_FALSE(vResults.empty());

	const video_encoder::SProbeResult *pX264 = video_encoder::FindProbed("libx264");
	ASSERT_NE(pX264, nullptr);
	EXPECT_TRUE(pX264->m_Available) << "libx264 must exist in the bundled FFmpeg; probe reason: " << pX264->m_aFailReason;
	EXPECT_EQ(pX264->m_QualityKind, video_encoder::EQualityKind::CRF_X264);

	// 探测结果里不允许出现无名或无名描述的条目
	for(const video_encoder::SProbeResult &Result : vResults)
	{
		EXPECT_STRNE(Result.m_aName, "");
		EXPECT_STRNE(Result.m_aDescription, "");
		if(!Result.m_Available)
			EXPECT_STRNE(Result.m_aFailReason, "") << Result.m_aName << " must state why it is unavailable";
	}
}

TEST(VideoEncoder, FindProbedRejectsUnknownName)
{
	EXPECT_EQ(video_encoder::FindProbed("no_such_encoder"), nullptr);
	EXPECT_EQ(video_encoder::FindProbed(""), nullptr);
}

TEST(VideoEncoder, RecordingLookupResolvesRegisteredCandidate)
{
	const AVCodec *pCodec = video_encoder::FindEncoderForRecording("libx264");
	ASSERT_NE(pCodec, nullptr);
	EXPECT_EQ(pCodec, avcodec_find_encoder_by_name("libx264"));
	EXPECT_EQ(video_encoder::FindEncoderForRecording("h264_nvenc"), avcodec_find_encoder_by_name("h264_nvenc"));
}

TEST(VideoEncoder, RecordingLookupRejectsUnknownOrEmptyNames)
{
	EXPECT_EQ(video_encoder::FindEncoderForRecording("no_such_encoder"), nullptr);
	EXPECT_EQ(video_encoder::FindEncoderForRecording(""), nullptr);
	EXPECT_EQ(video_encoder::FindEncoderForRecording(nullptr), nullptr);
}

TEST(VideoEncoder, NegotiateDefaultPrefersYuv420P)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("libx264");
	ASSERT_NE(pCodec, nullptr);

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);
	EXPECT_EQ(video_encoder::NegotiateDefaultPixelFormat(pCodec, pContext), AV_PIX_FMT_YUV420P);
	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, NegotiateRespectsWantedOrder)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("libx264");
	ASSERT_NE(pCodec, nullptr);

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	// x264 支持 444：wanted 只给 444 时选 444
	const AVPixelFormat a444[] = {AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE};
	EXPECT_EQ(video_encoder::NegotiatePixelFormat(pCodec, pContext, a444), AV_PIX_FMT_YUV444P);

	// 期望顺序决定选择：420 在前则胜过 444
	const AVPixelFormat aBoth[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE};
	EXPECT_EQ(video_encoder::NegotiatePixelFormat(pCodec, pContext, aBoth), AV_PIX_FMT_YUV420P);

	// 编码器不支持列表中任何格式时返回 NONE
	const AVPixelFormat aBogus[] = {AV_PIX_FMT_RGB8, AV_PIX_FMT_NONE};
	EXPECT_EQ(video_encoder::NegotiatePixelFormat(pCodec, pContext, aBogus), AV_PIX_FMT_NONE);

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, QualityOptionsDefaultToX264Crf)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("libx264");
	ASSERT_NE(pCodec, nullptr);

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	int QualityKind = (int)video_encoder::EQualityKind::NONE;
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "libx264", 18, 5, QualityKind));
	EXPECT_EQ(QualityKind, (int)video_encoder::EQualityKind::CRF_X264);

	int64_t Crf = 0;
	av_opt_get_int(pContext->priv_data, "crf", 0, &Crf);
	EXPECT_EQ(Crf, 18);

	// 非硬件家族名字走软件默认路径（CRF），质量值被钳制到 0-51
	QualityKind = (int)video_encoder::EQualityKind::NONE;
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "libopenh264", 99, 0, QualityKind));
	EXPECT_EQ(QualityKind, (int)video_encoder::EQualityKind::CRF_X264);
	av_opt_get_int(pContext->priv_data, "crf", 0, &Crf);
	EXPECT_EQ(Crf, 51);

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, QualityOptionsNvencUsesCqWhenPresent)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("h264_nvenc");
	if(!pCodec)
	{
		GTEST_SKIP() << "h264_nvenc not built into this FFmpeg";
	}

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	int QualityKind = (int)video_encoder::EQualityKind::NONE;
	// 不做真实 open，仅验证选项在 nvenc 的 AVOption 表上可设置（无 GPU 也能设置选项）。
	if(video_encoder::ApplyQualityOptions(pContext, "h264_nvenc", 20, 5, QualityKind))
	{
		EXPECT_EQ(QualityKind, (int)video_encoder::EQualityKind::CQ_NVENC);
		int64_t Cq = 0;
		av_opt_get_int(pContext->priv_data, "cq", 0, &Cq);
		EXPECT_EQ(Cq, 20);
	}

	avcodec_free_context(&pContext);
}
