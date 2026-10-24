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
		NONE, // 无可映射的质量参数（ProRes 由 profile 固定）
		CRF_X264, // libx264: crf + preset
		CRF_SVTAV1, // libsvtav1: crf(0-63) + preset(0-13, 数值越大越快)
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

	// 编码器家族别名：用户/预设可以只写 "h264" / "hevc" / "av1"，由运行时挑本机可用的
	// 具体编码器（优先硬件）。这样「高效高画质」预设在没有 NVENC 的机器上也不会直接失败。
	// 解析顺序见 ResolveEncoderAlias；不是别名时原样返回。
	bool IsEncoderAlias(const char *pName);

	// 把别名解析成具体编码器名。返回 false 表示没有可用实现（该家族完全不可用）。
	// 非别名输入在没有任何候选匹配时也返回 false，避免把拼错的名字当成可用编码器。
	bool ResolveEncoderAlias(const char *pName, char *pOut, size_t OutSize);

	// 给 UI/预设用：返回本机该家族实际会选中的编码器名；不可用时返回空串。
	const char *PreferredEncoderForFamily(const char *pFamily);

	// 4K 及以上是否应自动改用硬件编码器：软件 x264 在 4K60 实测只有约 38 fps，
	// 达不到录制帧率，导出会被编码拖成瓶颈。只在用户没有显式指定编码器、
	// 且输出高度达到 4K 时成立；显式选择永远优先。
	constexpr int AUTO_HARDWARE_MIN_HEIGHT = 2160;
	bool ShouldAutoPickHardware(const char *pConfiguredEncoder, int OutputHeight);

	// 录制入口只查候选清单和 FFmpeg 注册，不探测其他编码器或初始化硬件。
	// 可用性由实际输出参数下的 avcodec_open2 判断；未知/未编入返回 nullptr。
	// 别名（h264/hevc/av1）按 ResolveEncoderAlias 解析后再查找。
	const AVCodec *FindEncoderForRecording(const char *pName);

	// 像素格式协商：从编码器支持列表里按期望顺序选第一个可用的；
	// 都不支持时返回 AV_PIX_FMT_NONE。pWanted 列表以 AV_PIX_FMT_NONE 结尾。
	AVPixelFormat NegotiatePixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext, const AVPixelFormat *pWanted);

	// 用模块内置期望顺序（420 优先，422/444/RGB0 为后续里程碑备选）协商；
	// 返回 AV_PIX_FMT_NONE 表示该编码器无任何可用格式。
	AVPixelFormat NegotiateDefaultPixelFormat(const AVCodec *pCodec, const AVCodecContext *pContext);

	// 按编码器家族给出期望像素格式列表（M6）：剪辑格式（ProRes）优先 422 10bit，
	// 其余走通用顺序。返回以 AV_PIX_FMT_NONE 结尾。
	const AVPixelFormat *WantedPixelFormatsFor(const char *pEncoderName);

	// 按编码器家族设置质量参数；返回 false 表示选项被编码器拒绝（明确报错，不静默回退）。
	// ProresProfile 只在 ProRes 家族生效：0=Proxy 1=LT 2=422 3=HQ 4=4444 5=4444XQ，
	// 负数表示未指定（按 HQ 处理）。profile 必须与像素格式匹配，见 WantedPixelFormatsForPreference。
	bool ApplyQualityOptions(AVCodecContext *pContext, const char *pEncoderName, int QualityValue, int PresetIndex, int &QualityKindOut, int ProresProfile = -1);

	// 色度/位深偏好（qm_video_pixfmt）。渲染读回是 8bit RGBA，源信息量不变；
	// 4:4:4 保存的是完整色度分辨率（细线、彩色小字、描边），4:2:0→4:4:4 的
	// 色度保真差距可达 13 dB 以上，而位深提升本身不产生新信息。
	enum class EPixelFormat
	{
		AUTO = 0, // 跟随编码器家族默认（推荐）
		YUV420 = 1, // 最兼容，体积最小
		YUV422 = 2, // 剪辑中间格式通用色度
		YUV444 = 3, // 完整色度采样，剪辑/归档画质最好
	};

	// 把偏好展开成该编码器家族的期望像素格式列表；AUTO 与传 EQualityKind 无关的老行为一致。
	// 返回以 AV_PIX_FMT_NONE 结尾；pAuto 为家族默认列表（通常是 WantedPixelFormatsFor 的结果）。
	const AVPixelFormat *WantedPixelFormatsForPreference(const char *pEncoderName, EPixelFormat Preference, const AVPixelFormat *pAuto);

	// 默认 GOP：方案评估 60-120 帧区间，取 60（压缩率与容错平衡）。
	constexpr int DEFAULT_GOP = 60;
} // namespace video_encoder

#endif
