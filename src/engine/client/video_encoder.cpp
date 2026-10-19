#include "video_encoder.h"

#include <base/log.h>
#include <base/system.h>

#include <mutex>

namespace video_encoder
{
	namespace
	{
		// 像素格式期望顺序：420 优先（现管线），后续为 M6 预留的备选。
		const AVPixelFormat s_aWantedPixelFormats[] = {
			AV_PIX_FMT_YUV420P,
			AV_PIX_FMT_YUVJ420P,
			AV_PIX_FMT_NV12,
			AV_PIX_FMT_YUV422P,
			AV_PIX_FMT_YUV444P,
			AV_PIX_FMT_RGB0,
			AV_PIX_FMT_NONE,
		};

		// 剪辑格式：ProRes/DNxHR 的原生位深是 422 10bit。
		const AVPixelFormat s_aWantedEditingFormats[] = {
			AV_PIX_FMT_YUV422P10LE,
			AV_PIX_FMT_YUV422P,
			AV_PIX_FMT_YUV420P,
			AV_PIX_FMT_NONE,
		};

		// FFV1 无损归档：保真优先，444/422 高位深。
		const AVPixelFormat s_aWantedLosslessFormats[] = {
			AV_PIX_FMT_YUV444P10LE,
			AV_PIX_FMT_YUV422P10LE,
			AV_PIX_FMT_YUV444P,
			AV_PIX_FMT_YUV422P,
			AV_PIX_FMT_YUV420P,
			AV_PIX_FMT_NONE,
		};

		// 候选编码器清单。名字必须是 avcodec_find_encoder_by_name 能解析的准确名。
		struct SCandidate
		{
			const char *m_pName;
			const char *m_pDescription;
			EQualityKind m_QualityKind;
		};

		const SCandidate s_aCandidates[] = {
			{"libx264", "H.264 / x264 (software)", EQualityKind::CRF_X264},
			{"prores_ks", "ProRes 422 (editing)", EQualityKind::NONE},
			{"dnxhd", "DNxHR (editing)", EQualityKind::NONE},
			{"ffv1", "FFV1 (lossless)", EQualityKind::NONE},
			{"h264_nvenc", "H.264 / NVENC", EQualityKind::CQ_NVENC},
			{"hevc_nvenc", "H.265 / NVENC", EQualityKind::CQ_NVENC},
			{"av1_nvenc", "AV1 / NVENC", EQualityKind::CQ_NVENC},
			{"h264_amf", "H.264 / AMF", EQualityKind::CQP_AMF},
			{"hevc_amf", "H.265 / AMF", EQualityKind::CQP_AMF},
			{"av1_amf", "AV1 / AMF", EQualityKind::CQP_AMF},
			{"h264_qsv", "H.264 / Quick Sync", EQualityKind::ICQ_QSV},
			{"hevc_qsv", "H.265 / Quick Sync", EQualityKind::ICQ_QSV},
			{"av1_qsv", "AV1 / Quick Sync", EQualityKind::ICQ_QSV},
			{"h264_videotoolbox", "H.264 / VideoToolbox", EQualityKind::Q_VT},
			{"hevc_videotoolbox", "H.265 / VideoToolbox", EQualityKind::Q_VT},
		};

		// 三层探测第 2/3 层：真实 avcodec_open2 初始化（硬件会创建编解码 session）。
		// 分辨率取硬件编码器普遍支持的 256x256；探测后立即关闭释放。
		bool ProbeOpenEncoder(const AVCodec *pCodec, char *pError, size_t ErrorSize)
		{
			AVCodecContext *pContext = avcodec_alloc_context3(pCodec);
			if(!pContext)
			{
				str_copy(pError, "context allocation failed", ErrorSize);
				return false;
			}

			pContext->width = 256;
			pContext->height = 256;
			pContext->time_base = AVRational{1, 60};
			pContext->framerate = AVRational{60, 1};
			pContext->gop_size = DEFAULT_GOP;
			// 按编码器家族选格式：ProRes 只支持 10bit 422/444，8bit 通用表会协商失败。
			pContext->pix_fmt = NegotiatePixelFormat(pCodec, pContext, WantedPixelFormatsFor(pCodec->name));
			if(pContext->pix_fmt == AV_PIX_FMT_NONE)
			{
				str_copy(pError, "no supported pixel format", ErrorSize);
				avcodec_free_context(&pContext);
				return false;
			}

			// 探测初始化与真实编码同条件：质量选项里的 profile/coder 等是
			// dnxhd（需 DNxHR profile 才接受任意分辨率）/ffv1/prores 的 open 前置。
			int ProbeQualityKind = 0;
			if(!ApplyQualityOptions(pContext, pCodec->name, 23, 5, ProbeQualityKind))
			{
				str_copy(pError, "quality options failed", ErrorSize);
				avcodec_free_context(&pContext);
				return false;
			}

			const int OpenResult = avcodec_open2(pContext, pCodec, nullptr);
			if(OpenResult < 0)
			{
				char aAvError[AV_ERROR_MAX_STRING_SIZE];
				av_strerror(OpenResult, aAvError, sizeof(aAvError));
				str_format(pError, ErrorSize, "open failed: %s", aAvError);
				avcodec_free_context(&pContext);
				return false;
			}

			avcodec_free_context(&pContext);
			return true;
		}

		std::vector<SProbeResult> &ProbeCache()
		{
			static std::vector<SProbeResult> s_Cache;
			return s_Cache;
		}

		void ProbeOnce()
		{
			std::vector<SProbeResult> vResults;
			vResults.reserve(std::size(s_aCandidates));
			for(const SCandidate &Candidate : s_aCandidates)
			{
				SProbeResult Result;
				str_copy(Result.m_aName, Candidate.m_pName);
				str_copy(Result.m_aDescription, Candidate.m_pDescription);
				Result.m_QualityKind = Candidate.m_QualityKind;
				Result.m_Available = false;
				Result.m_aFailReason[0] = '\0';

				// 层 1：FFmpeg 构建是否含该编码器
				const AVCodec *pCodec = avcodec_find_encoder_by_name(Candidate.m_pName);
				if(!pCodec)
				{
					str_copy(Result.m_aFailReason, "not built into this FFmpeg", sizeof(Result.m_aFailReason));
					vResults.push_back(Result);
					continue;
				}

				// 层 2/3：真实初始化 + 像素格式协商
				if(!ProbeOpenEncoder(pCodec, Result.m_aFailReason, sizeof(Result.m_aFailReason)))
				{
					vResults.push_back(Result);
					continue;
				}

				Result.m_Available = true;
				vResults.push_back(Result);
			}
			ProbeCache() = std::move(vResults);
		}
	} // namespace

	const std::vector<SProbeResult> &ProbeEncoders()
	{
		static std::once_flag s_Once;
		std::call_once(s_Once, ProbeOnce);
		return ProbeCache();
	}

	const SProbeResult *FindProbed(const char *pName)
	{
		for(const SProbeResult &Result : ProbeEncoders())
		{
			if(str_comp(Result.m_aName, pName) == 0)
				return &Result;
		}
		return nullptr;
	}

	const AVCodec *FindEncoderForRecording(const char *pName)
	{
		if(!pName)
			return nullptr;
		for(const SCandidate &Candidate : s_aCandidates)
		{
			if(str_comp(Candidate.m_pName, pName) == 0)
				return avcodec_find_encoder_by_name(pName);
		}
		return nullptr;
	}

	AVPixelFormat NegotiateDefaultPixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext)
	{
		return NegotiatePixelFormat(pCodec, pContext, s_aWantedPixelFormats);
	}

	const AVPixelFormat *WantedPixelFormatsFor(const char *pEncoderName)
	{
		if(!pEncoderName)
			return s_aWantedPixelFormats;
		if(str_find_nocase(pEncoderName, "prores") != nullptr || str_find_nocase(pEncoderName, "dnxhd") != nullptr)
			return s_aWantedEditingFormats;
		if(str_find_nocase(pEncoderName, "ffv1") != nullptr)
			return s_aWantedLosslessFormats;
		return s_aWantedPixelFormats;
	}

	AVPixelFormat NegotiatePixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext, const AVPixelFormat *pWanted)
	{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
		const AVPixelFormat *pSupported = nullptr;
		int Count = 0;
		if(avcodec_get_supported_config(pContext, pCodec, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void **)&pSupported, &Count) < 0)
			return AV_PIX_FMT_NONE;
		if(!pSupported)
			return pWanted[0]; // 未声明支持列表：按惯例视为任意格式，给首选

		for(const AVPixelFormat *pW = pWanted; *pW != AV_PIX_FMT_NONE; ++pW)
		{
			for(int i = 0; i < Count; i++)
			{
				if(pSupported[i] == *pW)
					return *pW;
			}
		}
		return AV_PIX_FMT_NONE;
#else
		// 旧 API：pix_fmts 是以 AV_PIX_FMT_NONE 结尾的数组（可能为 NULL 表示任意）。
		const AVPixelFormat *pSupported = pCodec->pix_fmts;
		if(!pSupported)
			return pWanted[0];

		for(const AVPixelFormat *pW = pWanted; *pW != AV_PIX_FMT_NONE; ++pW)
		{
			for(const AVPixelFormat *pS = pSupported; *pS != AV_PIX_FMT_NONE; ++pS)
			{
				if(*pS == *pW)
					return *pW;
			}
		}
		return AV_PIX_FMT_NONE;
#endif
	}

	bool ApplyQualityOptions(AVCodecContext *pContext, const char *pEncoderName, int QualityValue, int PresetIndex, int &QualityKindOut)
	{
		QualityKindOut = (int)EQualityKind::NONE;
		if(!pEncoderName || !pContext)
			return false;
		const bool IsNvenc = str_find_nocase(pEncoderName, "nvenc") != nullptr;
		const bool IsAmf = str_find_nocase(pEncoderName, "_amf") != nullptr;
		const bool IsQsv = str_find_nocase(pEncoderName, "_qsv") != nullptr;
		const bool IsVt = str_find_nocase(pEncoderName, "videotoolbox") != nullptr;
		const bool IsProres = str_find_nocase(pEncoderName, "prores") != nullptr;
		const bool IsDnxhd = str_find_nocase(pEncoderName, "dnxhd") != nullptr;
		const bool IsFfv1 = str_find_nocase(pEncoderName, "ffv1") != nullptr;

		// 剪辑/归档格式（M6）：质量由 profile 固定（ProRes 422 HQ、DNxHR HQX），FFV1 无损。
		if(IsProres)
		{
			// 0=Proxy 1=LT 2=Standard 3=HQ 4=4444 5=4444XQ；质量值低段用 4444，常规 HQ。
			if(av_opt_set_int(pContext->priv_data, "profile", QualityValue <= 15 ? 4 : 3, 0) < 0)
				return false;
			return true;
		}
		if(IsDnxhd)
		{
			if(av_opt_set(pContext->priv_data, "profile", "dnxhr_hqx", 0) < 0)
				return false;
			return true;
		}
		if(IsFfv1)
		{
			// 无损归档：slice CRC 完整性校验；高位深时编码器自动切 range coder，
			// 无需手动指定 coder（"crc" 不是 FFV1 的选项名，对应项是 BOOL 型 slicecrc）。
			if(av_opt_set_int(pContext->priv_data, "slicecrc", 1, 0) < 0)
				return false;
			return true;
		}

		// preset 值索引按 cl_video_preset 的 10 档映射到各家族的原生档位。
		// x264: ultrafast..placebo；NVENC: p1..p7；AMF: speed/balanced/quality；QSV: veryfast..slow。
		const int ClampedPreset = std::clamp(PresetIndex, 0, 9);

		char aPreset[16];
		if(IsNvenc)
		{
			// p1 最快 → p7 质量最高；10 档 x264 preset 粗映射到 7 档。
			static const char *s_apNvencPresets[10] = {"p1", "p1", "p2", "p3", "p4", "p4", "p5", "p6", "p7", "p7"};
			str_copy(aPreset, s_apNvencPresets[ClampedPreset]);
			if(av_opt_set(pContext->priv_data, "preset", aPreset, 0) < 0)
				return false;
			if(av_opt_set(pContext->priv_data, "rc", "vbr", 0) < 0)
				return false;
			// CQ 与 CRF 同为 0-51 刻度，直接沿用用户质量值。
			if(av_opt_set_int(pContext->priv_data, "cq", std::clamp(QualityValue, 0, 51), 0) < 0)
				return false;
			QualityKindOut = (int)EQualityKind::CQ_NVENC;
			return true;
		}

		if(IsAmf)
		{
			static const char *s_apAmfQuality[10] = {"speed", "speed", "speed", "balanced", "balanced", "balanced", "quality", "quality", "quality", "quality"};
			if(av_opt_set(pContext->priv_data, "quality", s_apAmfQuality[ClampedPreset], 0) < 0)
				return false;
			if(av_opt_set(pContext->priv_data, "rc", "cqp", 0) < 0)
				return false;
			const int Qp = std::clamp(QualityValue, 0, 51);
			if(av_opt_set_int(pContext->priv_data, "qp_i", Qp, 0) < 0 ||
				av_opt_set_int(pContext->priv_data, "qp_p", Qp, 0) < 0 ||
				av_opt_set_int(pContext->priv_data, "qp_b", Qp, 0) < 0)
				return false;
			QualityKindOut = (int)EQualityKind::CQP_AMF;
			return true;
		}

		if(IsQsv)
		{
			static const char *s_apQsvPresets[10] = {"veryfast", "veryfast", "veryfast", "fast", "fast", "medium", "medium", "slow", "slow", "slow"};
			if(av_opt_set(pContext->priv_data, "preset", s_apQsvPresets[ClampedPreset], 0) < 0)
				return false;
			// global_quality 非 0 启用 ICQ 模式，数值越大质量越低（与 CRF 同向）。
			if(av_opt_set_int(pContext->priv_data, "global_quality", std::clamp(QualityValue, 1, 51), 0) < 0)
				return false;
			QualityKindOut = (int)EQualityKind::ICQ_QSV;
			return true;
		}

		if(IsVt)
		{
			// video_quality 1..100000，越大越好；CRF 0-51 反向映射到质量刻度。
			const int Quality = std::clamp((51 - std::clamp(QualityValue, 0, 51)) * 2000 + 1, 1, 100000);
			if(av_opt_set_int(pContext->priv_data, "video_quality", Quality, 0) < 0)
				return false;
			QualityKindOut = (int)EQualityKind::Q_VT;
			return true;
		}

		// 软件默认（libx264）：CRF + preset，与既有行为一致。
		// 注：GOP 60 会让 x264 自动 lookahead 涨到 40，4K 实测内存可控（约 500MB），
		// 不额外干预；若未来需要，rc_lookahead 是 x264 priv_data 选项而非 AVCodecContext 字段。
		static const char *s_apX264Presets[10] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"};
		if(av_opt_set(pContext->priv_data, "preset", s_apX264Presets[ClampedPreset], 0) < 0)
			return false;
		if(av_opt_set_int(pContext->priv_data, "crf", std::clamp(QualityValue, 0, 51), 0) < 0)
			return false;
		QualityKindOut = (int)EQualityKind::CRF_X264;
		return true;
	}
} // namespace video_encoder
