#ifndef ENGINE_CLIENT_VIDEO_EXPORT_H
#define ENGINE_CLIENT_VIDEO_EXPORT_H

// Demo 分段导出任务（Demo 视频导出现代化 M3）。
// 设计要点（docs/规格/2026-10-16-Demo视频导出现代化实施方案.md）：
// - 一次导出拆成固定时长片段，每段独立封装（新编码器实例天然保证段首 IDR）；
// - 进度文件为纯键值对文本，损坏时按「该片段从头渲染」处理，不引入数据库；
// - 任务指纹（demo 内容 + 导出参数）匹配通过后才复用已完成片段，否则整单重置；
// - 本模块只承载规划、进度与状态机，录像生命周期与 seek 由 CClient 驱动。

#include <base/hash.h>
#include <base/system.h>

#include <engine/shared/video.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

struct AVIOContext;
class CConfig;

// 「导出离屏帧」计划：导出是否改画离屏渲染目标、目标尺寸与多采样数。
struct SVideoOffscreenPlan
{
	// 离屏改画的触发条件（任一成立）：
	//  - qm_video_fsaa 显式设置（含显式 0：压过全局 FSAA，导出不要窗口的 AA）；
	//  - 输出大于窗口（真分辨率导出，M7 只支持向下超采样）。
	bool m_Offscreen = false;
	// 输出在任一维上大于窗口：离屏不可降级（窗口帧缓冲只有窗口大，
	// 读回尺寸对不上），离屏失败必须让录制报错终止。
	bool m_Upscale = false;
	// 读回（渲染）尺寸：输出不大于窗口时 = 窗口尺寸（沿用 M7 下采样语义），
	// 任一维超出窗口时 = 输出尺寸（按输出宽高比 1:1 光栅化）。
	int m_RenderWidth = 0;
	int m_RenderHeight = 0;
	// 离屏目标采样数；1 = 单采样。0（显式关）与不支持多采样时都是 1。
	int m_SampleCount = 1;
};

// 计算导出离屏参数（纯函数，单测直接覆盖）：
// - FsaaSetting：qm_video_fsaa（-1 跟随全局，0 关，2/4/8 显式档位）；
// - GlobalFsaa：gfx_fsaa_samples（跟随全局且需要离屏时按它采样，导出画质不低于游玩）；
// - MaxRenderTargetSamples：后端报告的离屏目标采样上限（不支持为 1，请求被压回单采样）。
// 采样预算：目标像素 × 采样数超过 1.28 亿时逐级降档（2/4/8 → 更小），
// 防止 4K 以上输出叠高倍采样把显存与光栅开销拖爆；降到 1 仍超预算时保持 1。
inline SVideoOffscreenPlan PlanVideoOffscreenFrame(int ScreenWidth, int ScreenHeight, int OutputWidth, int OutputHeight, int FsaaSetting, int GlobalFsaa, int MaxRenderTargetSamples)
{
	SVideoOffscreenPlan Plan;
	const int OutW = OutputWidth > 0 ? OutputWidth : ScreenWidth;
	const int OutH = OutputHeight > 0 ? OutputHeight : ScreenHeight;
	const bool Upscale = OutW > ScreenWidth || OutH > ScreenHeight;
	Plan.m_Upscale = Upscale;
	Plan.m_RenderWidth = Upscale ? OutW : ScreenWidth;
	Plan.m_RenderHeight = Upscale ? OutH : ScreenHeight;

	// -1 跟随全局：窗口路径（不离屏）由窗口帧缓冲自带 AA；因超采样被迫离屏时，
	// 按全局 FSAA 请求采样，保证「导出画质 ≥ 游玩画质」不被超采样反而稀释。
	const int Wanted = FsaaSetting == -1 ? GlobalFsaa : FsaaSetting;
	Plan.m_Offscreen = FsaaSetting != -1 || Upscale;
	if(!Plan.m_Offscreen)
		return Plan;

	static constexpr int64_t s_MaxSampledPixels = 128LL * 1000 * 1000;
	int Samples = Wanted > 1 ? Wanted : 1;
	const int MaxSupported = std::max(MaxRenderTargetSamples, 1);
	Samples = std::min(Samples, MaxSupported);
	const int64_t Pixels = (int64_t)Plan.m_RenderWidth * Plan.m_RenderHeight;
	while(Samples > 1 && Pixels * Samples > s_MaxSampledPixels)
		Samples /= 2;
	Plan.m_SampleCount = Samples;
	return Plan;
}

// 进度、拼接和直录共用输出事务；未提交时只清理本次打开的临时文件。
class CVideoExportOutput
{
	std::string m_Target;
	std::string m_Temporary;
	bool m_OwnsTemporary = false;

public:
	CVideoExportOutput(const char *pTarget, const char *pSuffix);
	~CVideoExportOutput();
	CVideoExportOutput(const CVideoExportOutput &) = delete;
	CVideoExportOutput &operator=(const CVideoExportOutput &) = delete;
	const char *TemporaryPath() const { return m_Temporary.c_str(); }
	void Opened() { m_OwnsTemporary = true; }
	bool Commit();
};

// 在任务启动时记录实际编码与 demo 显示配置，用于续渲一致性校验。
std::string VideoExportConfigParams(const CConfig &Config, const SVideoDisplaySnapshot &Display);

// 文件 I/O 边界可替换，便于复现短写、同步和收尾失败；默认使用真实文件系统。
struct SVideoExportIo
{
	unsigned (*m_pWrite)(IOHANDLE, const void *, unsigned) = io_write;
	int (*m_pSync)(IOHANDLE) = io_sync;
	int (*m_pClose)(IOHANDLE) = io_close;
	int (*m_pCloseVideo)(AVIOContext **) = nullptr; // 空值使用 avio_closep
};

class CVideoExportTask
{
public:
	struct CSegment
	{
		int m_StartTick = 0; // demo tick（含）
		int m_EndTick = 0; // demo tick（不含）
		bool m_Done = false;
		int64_t m_FileSize = -1; // 成功收尾时的实际文件大小；-1 表示旧进度未记录
	};

	enum class EState
	{
		IDLE, // 未激活
		WARMING_UP, // 段前预热播放中（seek 到起点前，播到段起点）
		RECORDING, // 正在录制当前片段
	};

	// 依据 demo tick 范围与段长切分片段；0 秒段长表示不分段（单段覆盖全范围）。
	// TicksPerSecond 取 SERVER_TICK_SPEED。
	static std::vector<CSegment> PlanSegments(int FirstTick, int LastTick, int SegmentSeconds);

	// 负数表示未指定端点；非有限输入仍算选区请求，必须拒绝而不能退回整段录制。
	static bool HasRange(float StartSeconds, float EndSeconds);
	// 选区相对 demo 起点，允许只指定任一端点；失败时不修改传入 tick。
	static bool ResolveRange(int &FirstTick, int &LastTick, float StartSeconds, float EndSeconds);

	// 构造任务。pName 为用户可见输出基础名（不含扩展名）。
	CVideoExportTask(const char *pName, const char *pDemoPath, int FirstTick, int LastTick, int SegmentSeconds,
		int Fps, int Width, int Height, int Crf, int Preset, int WarmupTicks);

	// 任务指纹：demo 文件属性 + 导出参数（渲染尺寸/质量/段长/选区）与调用方传入的附加参数
	// （编码器、容器、输出尺寸等）。断点续渲时据此判断进度文件是否仍适用，任一参数变化都必须重渲。
	void ComputeFingerprint(uint64_t DemoFileSize, int64_t DemoMtime, const char *pExtraParams);

	const char *Name() const { return m_aName; }
	const char *DemoPath() const { return m_aDemoPath; }
	const std::vector<CSegment> &Segments() const { return m_vSegments; }
	EState State() const { return m_State; }
	int CurrentSegment() const { return m_CurrentSegment; }
	bool Active() const { return m_State != EState::IDLE; }
	bool AllDone() const;
	int FirstTick() const { return m_FirstTick; }
	int LastTick() const { return m_LastTick; }
	int WarmupTicks() const { return m_WarmupTicks; }

	// 导出启动时冻结的 demo 显示配置；所有段与指纹共用。
	void SetDisplaySnapshot(const SVideoDisplaySnapshot &Snapshot) { m_DisplaySnapshot = Snapshot; }
	const SVideoDisplaySnapshot &DisplaySnapshot() const { return m_DisplaySnapshot; }

	// 当前应录制的段（未完成的最靠前段）；-1 表示无待渲染段。
	int NextPendingSegment() const;

	// 段基础名（无扩展名，调用方按容器补 .mp4）：name_pNNN；FinalOutputName 为拼接产物基础名。
	void SegmentFilename(int SegmentIndex, char *pBuffer, size_t BufferSize) const;
	void FinalOutputName(char *pBuffer, size_t BufferSize) const;

	// 进度持久化：videos/<name>.progress。损坏或不匹配时返回 false 并重置全部段状态。
	bool LoadProgress(const char *pProgressPath);
	bool SaveProgress(const char *pProgressPath, const SVideoExportIo *pIo = nullptr) const;

	void MarkSegmentDone(int SegmentIndex)
	{
		m_vSegments[SegmentIndex].m_Done = true;
		m_vSegments[SegmentIndex].m_FileSize = -1;
	}
	// 仅在录像器成功收尾后调用：记录文件大小，供续渲检测后续截断或替换。
	bool CompleteSegmentFile(int SegmentIndex, const char *pPath);
	// 续渲恢复：把已完成但段文件已缺失/无效的段重新置为待渲染。
	// pResolvePath 按段逻辑路径（如 "videos/name_p001"，无扩展名）返回用于
	// libavformat 打开的绝对路径（调用方拼容器扩展名），空值视为缺失。
	void RestoreMissingDoneSegments(const std::function<const char *(const char *)> &pResolvePath);

	// 状态机驱动（由 CClient 在 demo 播放推进时调用）。
	// 返回请求的动作，录像生命周期调用由 CClient 执行：
	//   ACT_NONE          继续当前行为
	//   ACT_START_RECORD  到达段起点，开始录制该段
	//   ACT_STOP_RECORD   到达段末，停止录制收尾
	//   ACT_TASK_DONE     全部片段完成
	enum EAction
	{
		ACT_NONE,
		ACT_START_RECORD,
		ACT_STOP_RECORD,
		ACT_TASK_DONE,
	};
	EAction UpdatePlayback(int CurrentTick);

	// 状态迁移（由 CClient 在录像启动/停止后回调确认）
	void OnRecordingStarted() { m_State = EState::RECORDING; }
	void OnRecordingStopped();
	void ResetToIdle() { m_State = EState::IDLE; }
	void SetCurrentSegment(int SegmentIndex) { m_CurrentSegment = SegmentIndex; }
	// 开始（或续渲）指定段：进入预热态，由 CClient 负责定位到段起点前。
	void BeginSegment(int SegmentIndex)
	{
		m_CurrentSegment = SegmentIndex;
		m_State = EState::WARMING_UP;
		m_SegmentRecordingFailed = false;
	}

	// 当前段录制是否以错误终止（编码失败、磁盘满等）。错误的段不得标记完成：
	// 容器可能仍是合法文件（mkv 无 trailer 也能打开），仅靠 VerifySegmentFile 会误判为成功。
	void MarkSegmentRecordingFailed() { m_SegmentRecordingFailed = true; }
	bool SegmentRecordingFailed() const { return m_SegmentRecordingFailed; }

	// 轻量格式校验：容器可打开且含视频流，不等于全量解码或内容完整性验证。
	// 续渲还会核对成功收尾时记录的文件大小；旧进度缺少此记录时保留原有兼容行为。
	static bool VerifySegmentFile(const char *pPath);

private:
	char m_aName[IO_MAX_PATH_LENGTH];
	char m_aDemoPath[IO_MAX_PATH_LENGTH];
	int m_FirstTick;
	int m_LastTick;
	int m_WarmupTicks;
	std::vector<CSegment> m_vSegments;
	EState m_State = EState::IDLE;
	int m_CurrentSegment = -1;
	int m_Fps;
	int m_Width;
	int m_Height;
	int m_Crf;
	int m_Preset;
	int m_SegmentSeconds;
	bool m_SegmentRecordingFailed = false; // 当前段录制是否以错误终止
	SVideoDisplaySnapshot m_DisplaySnapshot; // 续渲指纹与渲染共用的显示配置
	char m_aFingerprint[MD5_MAXSTRSIZE]; // demo 属性 + 导出参数的十六进制指纹
};

// 流复制拼接：把参数一致的片段按顺序拷贝封装为单个输出文件（无重编码）。
// 返回 false 时输出文件不应被使用；调用方按「保留独立片段」处理。
bool ConcatVideoFiles(const std::vector<std::string> &vInputs, const char *pOutput, const char *pFormatName, const SVideoExportIo *pIo = nullptr);

#endif
