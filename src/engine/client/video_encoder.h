#ifndef ENGINE_CLIENT_VIDEO_ENCODER_H
#define ENGINE_CLIENT_VIDEO_ENCODER_H

// 编码器能力探测模块（M4）：
// - 候选编码器清单（软件 + NVENC/AMF/QSV/VideoToolbox 硬件）。
// - 三层探测：FFmpeg 构建含该编码器 → avcodec_open2 真实初始化 → 像素格式可协商。
// - 探测结果进程内缓存；UI 只展示可用项，导出走名称字符串选择。
// - 各编码器家族独立的质量参数映射（禁止 CRF 跨编码器套用）。

#include <base/math.h>
#include <base/system.h>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include <vector>

namespace video_encoder
{
	// 质量控制方式：每种编码器家族用各自的原生参数。
	enum class EQualityKind
	{
		NONE, // 无可映射的质量参数（ProRes/DNxHR 由 profile 固定、FFV1 无损）
		CRF_X264, // libx264: crf + preset
		CQ_NVENC, // NVENC: rc=vbr + cq + preset(p1..p7)
		CQP_AMF, // AMF: rc=cqp + qp_i/qp_p/qp_b + quality(speed/balanced/quality)
		ICQ_QSV, // QSV: global_quality (ICQ) + preset
		Q_VT, // VideoToolbox: video_quality (1..100000)
	};

	struct SProbeResult
	{
		char m_aName[48]; // 编码器名称（avcodec_find_encoder_by_name 用）
		char m_aDescription[96]; // 展示名（UI）
		EQualityKind m_QualityKind;
		bool m_Available; // 通过全部三层探测
		char m_aFailReason[128]; // 不可用原因（调试/tooltip）
	};

	// 候选清单探测（结果缓存；首次调用执行真实 avcodec_open2 硬件探测）。
	const std::vector<SProbeResult> &ProbeEncoders();

	// 按名称查探测结果；未知名返回 nullptr。
	const SProbeResult *FindProbed(const char *pName);

	// 像素格式协商：从编码器支持列表里按期望顺序选第一个可用的；
	// 都不支持时返回 AV_PIX_FMT_NONE。pWanted 列表以 AV_PIX_FMT_NONE 结尾。
	AVPixelFormat NegotiatePixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext, const AVPixelFormat *pWanted);

	// 用模块内置期望顺序（420 优先，422/444/RGB0 为后续里程碑备选）协商；
	// 返回 AV_PIX_FMT_NONE 表示该编码器无任何可用格式。
	AVPixelFormat NegotiateDefaultPixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext);

	// 按编码器家族给出期望像素格式列表（M6）：剪辑格式（ProRes/DNxHR）优先 422 10bit，
	// FFV1 无损归档优先 444/422 高位深，其余走通用顺序。返回以 AV_PIX_FMT_NONE 结尾。
	const AVPixelFormat *WantedPixelFormatsFor(const char *pEncoderName);

	// 按编码器家族设置质量参数；返回 false 表示选项被编码器拒绝（明确报错，不静默回退）。
	bool ApplyQualityOptions(AVCodecContext *pContext, const char *pEncoderName, int QualityValue, int PresetIndex, int &QualityKindOut);

	// 默认 GOP：方案评估 60-120 帧区间，取 60（压缩率与容错平衡）。
	constexpr int DEFAULT_GOP = 60;
} // namespace video_encoder

#endif
