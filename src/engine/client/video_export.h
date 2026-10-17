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

#include <string>
#include <vector>

class CVideoExportTask
{
public:
	struct CSegment
	{
		int m_StartTick = 0; // demo tick（含）
		int m_EndTick = 0; // demo tick（不含）
		bool m_Done = false;
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

	// 构造任务。pName 为用户可见输出基础名（不含扩展名）。
	CVideoExportTask(const char *pName, const char *pDemoPath, int FirstTick, int LastTick, int SegmentSeconds,
		int Fps, int Width, int Height, int Crf, int Preset, int WarmupTicks);

	// 任务指纹：demo 文件属性 + 导出参数。用于断点续渲时判断进度文件是否仍适用。
	void ComputeFingerprint(uint64_t DemoFileSize, int64_t DemoMtime);

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

	// 当前应录制的段（未完成的最靠前段）；-1 表示无待渲染段。
	int NextPendingSegment() const;

	// 段基础名（无扩展名，调用方按容器补 .mp4）：name_pNNN；FinalOutputName 为拼接产物基础名。
	void SegmentFilename(int SegmentIndex, char *pBuffer, size_t BufferSize) const;
	void FinalOutputName(char *pBuffer, size_t BufferSize) const;

	// 进度持久化：videos/<name>.progress。损坏或不匹配时返回 false 并重置全部段状态。
	bool LoadProgress(const char *pProgressPath);
	bool SaveProgress(const char *pProgressPath) const;

	void MarkSegmentDone(int SegmentIndex) { m_vSegments[SegmentIndex].m_Done = true; }

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
	}

	// 段文件完整性验证：容器可打开且含视频流即视为该段写入成功。
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
	char m_aFingerprint[MD5_MAXSTRSIZE]; // demo 属性 + 导出参数的十六进制指纹
};

// 流复制拼接：把参数一致的片段按顺序拷贝封装为单个输出文件（无重编码）。
// 返回 false 时输出文件不应被使用；调用方按「保留独立片段」处理。
bool ConcatVideoFiles(const std::vector<std::string> &vInputs, const char *pOutput, const char *pFormatName);

#endif
