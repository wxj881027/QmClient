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

	// SVT-AV1 编入时必须通过探测（三层：注册、真实 open、像素格式可协商）；
	// 未编入（旧版捆绑 FFmpeg）时跳过该断言。
	const video_encoder::SProbeResult *pSvt = video_encoder::FindProbed("libsvtav1");
	ASSERT_NE(pSvt, nullptr) << "libsvtav1 must stay in the candidate list";
	if(avcodec_find_encoder_by_name("libsvtav1") != nullptr)
	{
		EXPECT_TRUE(pSvt->m_Available) << "libsvtav1 probe reason: " << pSvt->m_aFailReason;
		EXPECT_EQ(pSvt->m_QualityKind, video_encoder::EQualityKind::CRF_SVTAV1);
	}

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
		// CRF 与 CQ 同刻度但不等价：实测同一素材 CQ23 ≈ CRF18，因此统一 +5 档偏移。
		EXPECT_EQ(Cq, 25);
	}

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, NvencCqOffsetClampsAtBothEnds)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("h264_nvenc");
	if(!pCodec)
	{
		GTEST_SKIP() << "h264_nvenc not built into this FFmpeg";
	}

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	int QualityKind = 0;
	int64_t Cq = -1;
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "h264_nvenc", 50, 5, QualityKind));
	av_opt_get_int(pContext->priv_data, "cq", 0, &Cq);
	EXPECT_EQ(Cq, 51) << "top of the scale must clamp instead of overflowing";

	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "h264_nvenc", 0, 5, QualityKind));
	av_opt_get_int(pContext->priv_data, "cq", 0, &Cq);
	EXPECT_EQ(Cq, 5) << "bottom of the scale keeps the offset instead of going negative";

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, ProresProfileFollowsExplicitArgument)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("prores_ks");
	if(!pCodec)
	{
		GTEST_SKIP() << "prores_ks not built into this FFmpeg";
	}

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	int QualityKind = 0;
	int64_t Profile = -1;

	// 4444 由调用方按像素格式显式指定；质量值不再隐式切换 profile。
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "prores_ks", 12, 5, QualityKind, 4));
	av_opt_get_int(pContext->priv_data, "profile", 0, &Profile);
	EXPECT_EQ(Profile, 4);

	// 未指定（-1）时回落到 HQ，与既有默认一致。
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "prores_ks", 12, 5, QualityKind, -1));
	av_opt_get_int(pContext->priv_data, "profile", 0, &Profile);
	EXPECT_EQ(Profile, 3);

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, PixelPreferenceAutoKeepsFamilyDefaults)
{
	const AVPixelFormat *pAuto = video_encoder::WantedPixelFormatsFor("prores_ks");
	EXPECT_EQ(video_encoder::WantedPixelFormatsForPreference("prores_ks", video_encoder::EPixelFormat::AUTO, pAuto), pAuto)
		<< "AUTO must not change the family list";

	const AVPixelFormat *pAutoX264 = video_encoder::WantedPixelFormatsFor("libx264");
	EXPECT_EQ(video_encoder::WantedPixelFormatsForPreference(nullptr, video_encoder::EPixelFormat::YUV444, pAutoX264), pAutoX264)
		<< "an unknown encoder name must fall back to the family list";
}

TEST(VideoEncoder, PixelPreferenceListsRequestedChromaFirst)
{
	const AVPixelFormat *pAuto = video_encoder::WantedPixelFormatsFor("libx264");

	const AVPixelFormat *p444 = video_encoder::WantedPixelFormatsForPreference("libx264", video_encoder::EPixelFormat::YUV444, pAuto);
	ASSERT_NE(p444, nullptr);
	EXPECT_EQ(p444[0], AV_PIX_FMT_YUV444P10LE) << "4:4:4 preference must ask for full chroma first";
	EXPECT_EQ(p444[1], AV_PIX_FMT_YUV444P);

	const AVPixelFormat *p422 = video_encoder::WantedPixelFormatsForPreference("libx264", video_encoder::EPixelFormat::YUV422, pAuto);
	EXPECT_EQ(p422[0], AV_PIX_FMT_YUV422P10LE);

	// ProRes 没有 4:2:0 profile：请求 4:2:0 时降级到 422 而不是让导出失败。
	const AVPixelFormat *pProres = video_encoder::WantedPixelFormatsForPreference("prores_ks", video_encoder::EPixelFormat::YUV420, video_encoder::WantedPixelFormatsFor("prores_ks"));
	EXPECT_EQ(pProres[0], AV_PIX_FMT_YUV422P10LE);
}

TEST(VideoEncoder, QualityOptionsSvtAv1MapsCrfAndPreset)
{
	const AVCodec *pCodec = avcodec_find_encoder_by_name("libsvtav1");
	if(!pCodec)
	{
		GTEST_SKIP() << "libsvtav1 not built into this FFmpeg";
	}

	AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
	ASSERT_NE(pContext, nullptr);

	int QualityKind = (int)video_encoder::EQualityKind::NONE;
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "libsvtav1", 35, 5, QualityKind));
	EXPECT_EQ(QualityKind, (int)video_encoder::EQualityKind::CRF_SVTAV1);

	int64_t Crf = -1;
	av_opt_get_int(pContext->priv_data, "crf", 0, &Crf);
	EXPECT_EQ(Crf, 35) << "统一质量刻度 0-51 直接沿用为 SVT CRF";

	int64_t Preset = -1;
	av_opt_get_int(pContext->priv_data, "preset", 0, &Preset);
	EXPECT_EQ(Preset, 6) << "medium(5) 映射到 SVT preset 6（SVT 数值越大越快）";

	// 两端档位映射：ultrafast(0) → 12、placebo(9) → 2。
	QualityKind = (int)video_encoder::EQualityKind::NONE;
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "libsvtav1", 30, 0, QualityKind));
	av_opt_get_int(pContext->priv_data, "preset", 0, &Preset);
	EXPECT_EQ(Preset, 12);
	EXPECT_TRUE(video_encoder::ApplyQualityOptions(pContext, "libsvtav1", 30, 9, QualityKind));
	av_opt_get_int(pContext->priv_data, "preset", 0, &Preset);
	EXPECT_EQ(Preset, 2);

	avcodec_free_context(&pContext);
}

TEST(VideoEncoder, FamilyAliasesResolveToAvailableEncoders)
{
	// libx264 一定编进了捆绑 FFmpeg，所以 h264 家族必有可用实现。
	EXPECT_TRUE(video_encoder::IsEncoderAlias("h264"));
	EXPECT_TRUE(video_encoder::IsEncoderAlias("HEVC")) << "alias matching is case-insensitive";
	EXPECT_TRUE(video_encoder::IsEncoderAlias("av1"));
	EXPECT_FALSE(video_encoder::IsEncoderAlias("libx264")) << "concrete encoder names are not aliases";
	EXPECT_FALSE(video_encoder::IsEncoderAlias(""));
	EXPECT_FALSE(video_encoder::IsEncoderAlias(nullptr));

	char aResolved[48];
	ASSERT_TRUE(video_encoder::ResolveEncoderAlias("h264", aResolved, sizeof(aResolved)));
	EXPECT_STREQ(aResolved, video_encoder::PreferredEncoderForFamily("h264"));
	// 解析结果必须真的是本机可用的编码器，而不是某个写死的名字。
	const video_encoder::SProbeResult *pProbe = video_encoder::FindProbed(aResolved);
	ASSERT_NE(pProbe, nullptr);
	EXPECT_TRUE(pProbe->m_Available) << "resolved encoder must pass the probe, reason: " << pProbe->m_aFailReason;
}

TEST(VideoEncoder, ConcreteEncoderNamePassesThroughUnchanged)
{
	char aResolved[48];
	ASSERT_TRUE(video_encoder::ResolveEncoderAlias("libx264", aResolved, sizeof(aResolved)));
	EXPECT_STREQ(aResolved, "libx264");
}

TEST(VideoEncoder, UnknownEncoderNameDoesNotResolve)
{
	char aResolved[48];
	// 拼错的名字不能被当成可用编码器，否则导出会在更晚的地方以难懂的错误失败。
	EXPECT_FALSE(video_encoder::ResolveEncoderAlias("libx2640", aResolved, sizeof(aResolved)));
	EXPECT_FALSE(video_encoder::ResolveEncoderAlias("no_such_encoder", aResolved, sizeof(aResolved)));
	EXPECT_FALSE(video_encoder::ResolveEncoderAlias("", aResolved, sizeof(aResolved)));
	EXPECT_FALSE(video_encoder::ResolveEncoderAlias(nullptr, aResolved, sizeof(aResolved)));
}

TEST(VideoEncoder, AliasNeverResolvesToAnUnavailableEncoder)
{
	// 两个方向都必须在同一台机器上成立：有实现就解析到真正可用的那个，
	// 没有实现就明确失败，不允许解析出任何探测不可用的名字（那会让导出在更晚处失败）。
	const char *pPreferred = video_encoder::PreferredEncoderForFamily("av1");
	char aResolved[48];
	const bool Resolved = video_encoder::ResolveEncoderAlias("av1", aResolved, sizeof(aResolved));
	EXPECT_EQ(Resolved, pPreferred[0] != '\0');

	if(Resolved)
	{
		EXPECT_STREQ(aResolved, pPreferred);
		const video_encoder::SProbeResult *pProbe = video_encoder::FindProbed(aResolved);
		ASSERT_NE(pProbe, nullptr);
		EXPECT_TRUE(pProbe->m_Available) << "resolved encoder must be probe-available: " << pProbe->m_aFailReason;
	}
	else
	{
		EXPECT_EQ(video_encoder::FindEncoderForRecording("av1"), nullptr);
	}
}

TEST(VideoEncoder, AutoHardwareOnlyKicksInFor4KWhenEncoderIsLeftAutomatic)
{
	// 显式选择永远优先：用户写了具体编码器或别名时不得被自动替换。
	EXPECT_FALSE(video_encoder::ShouldAutoPickHardware("libx264", 4320));
	EXPECT_FALSE(video_encoder::ShouldAutoPickHardware("hevc", 4320));
	// 只有留空（跟随封装器默认）且达到 4K 才自动挑硬件。
	EXPECT_TRUE(video_encoder::ShouldAutoPickHardware("", video_encoder::AUTO_HARDWARE_MIN_HEIGHT));
	EXPECT_TRUE(video_encoder::ShouldAutoPickHardware(nullptr, 4320));
	// 低于 4K 不动：1080p 下软件编码仍能跑满帧率，没必要引入硬件画质差异。
	EXPECT_FALSE(video_encoder::ShouldAutoPickHardware("", 1080));
	EXPECT_FALSE(video_encoder::ShouldAutoPickHardware("", video_encoder::AUTO_HARDWARE_MIN_HEIGHT - 1));
}

TEST(VideoEncoder, PixelPreferenceNegotiatesRealEncoderSupport)
{
	// 4:4:4 只有编码器确实支持时才协商成功：libx264 支持，4:2:0-only 编码器不会拿到它。
	const AVCodec *pX264 = avcodec_find_encoder_by_name("libx264");
	ASSERT_NE(pX264, nullptr);
	AVCodecContext *pContext = avcodec_alloc_context3(pX264);
	ASSERT_NE(pContext, nullptr);

	const AVPixelFormat *pWanted = video_encoder::WantedPixelFormatsForPreference("libx264", video_encoder::EPixelFormat::YUV444, video_encoder::WantedPixelFormatsFor("libx264"));
	EXPECT_EQ(video_encoder::NegotiatePixelFormat(pX264, pContext, pWanted), AV_PIX_FMT_YUV444P10LE)
		<< "libx264 accepts 10-bit 4:4:4 and must be preferred over 8-bit";
	avcodec_free_context(&pContext);
}
