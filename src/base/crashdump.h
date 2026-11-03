#ifndef BASE_CRASHDUMP_H
#define BASE_CRASHDUMP_H

#include <cstddef>
#include <cstdint>
#include <ctime>

/**
 * @defgroup Crash-Dumping Crash Dumping
 */

/**
 * Initializes the crash dumper and sets the filename to write the crash dump
 * to, if support for crash logging was compiled in. Otherwise does nothing.
 *
 * @ingroup Crash-Dumping
 *
 * @param log_file_path Absolute path to which crash log file should be written.
 */
void crashdump_init_if_available(const char *log_file_path);

// 图形后端在初始化/切换时可报告名称；不支持的平台保留空实现。
void crashdump_set_graphics_backend(const char *pBackendName);

/**
 * 为指定报告启动一个干净的客户端崩溃报告进程。
 *
 * 报告进程会在 SDL 和图形后端之前启动，因此当前客户端发生致命错误或卡死时也可使用。
 *
 * @param report_path QmClient 致命崩溃或卡死报告的绝对路径。
 *
 * @return 是否成功启动报告进程。
 */
bool crashdump_launch_reporter_if_available(const char *report_path);

/**
 * 阻止下一次致命错误处理再次打开重复的报告窗口。
 *
 * 用于客户端已经同步显示断言窗口的情况；崩溃文件仍会正常生成。
 */
void crashdump_suppress_reporter_once();

/**
 * 标记进程已进入退出清理阶段。
 *
 * 退出清理会销毁图形后端，个别图形驱动（例如 NVIDIA 的 nvoglv64.dll）会在
 * 销毁设备时访问已释放内存。此时进程本来就要结束，弹窗与几十 MB 的完整转储
 * 对用户没有价值。客户端在开始清理前调用本函数，致命错误处理在确认
 * “进程处于退出阶段”且“异常发生在已知图形驱动模块内”时，只记录一条日志，
 * 不再生成报告窗口；真正的崩溃（未进入退出阶段）不受影响。
 *
 * @param p_driver_module 预留参数，当前为 nullptr 即可；驱动模块清单固定在
 *                        crashdump.cpp 内维护。
 *
 * @ingroup Crash-Dumping
 */
void crashdump_mark_shutdown_begin(const char *p_driver_module);

/**
 * 查询当前异常是否属于退出阶段的图形驱动故障。
 *
 * 供客户端在结束退出阶段前判断是否应把崩溃转成退出告警。
 *
 * @return 退出阶段且异常落在已知图形驱动模块内时为 true。
 *
 * @ingroup Crash-Dumping
 */
bool crashdump_is_shutdown_graphics_fault();

/**
 * 显式结束退出阶段标记，取消后续退出期抑制。
 *
 * @ingroup Crash-Dumping
 */
void crashdump_mark_shutdown_end();

/**
 * 查询模块名是否属于“退出期可忽略”的图形驱动清单。
 *
 * 清单包含 NVIDIA / AMD / Intel 的 OpenGL、Vulkan、D3D12 驱动与运行库。
 * 退出清理在驱动内部释放 GPU 对象时崩溃属于已知无害故障，本函数是这一
 * 判断的可测试入口（大小写不敏感、按文件名匹配）。
 *
 * @param p_module_name 模块文件名，例如 "nvoglv64.dll"。
 *
 * @return 属于清单时为 true；nullptr 或空串为 false。
 *
 * @ingroup Crash-Dumping
 */
bool crashdump_is_ignorable_shutdown_driver_module(const char *p_module_name);

/**
 * 一次致命信号的己方报告内容。
 *
 * 结构体与平台无关，便于在任意平台验证文本格式与栈回溯逻辑；Android 的
 * signal handler 只在里面填充现场寄存器，不做额外的字符串处理。
 */
struct SCrashFatalReport
{
	const char *m_pProgramName;
	/** 版本字符串；为 nullptr 时省略该行。 */
	const char *m_pVersion;
	const char *m_pPlatform;
	/** 进程启动时间，由初始化阶段预先格式化，避免在 handler 内做时区换算。 */
	const char *m_pStartTime;
	/** 图形后端名称；为 nullptr 时省略该行。 */
	const char *m_pGraphicsBackend;
	int m_Pid;
	int m_Signal;
	int m_SignalCode;
	uintptr_t m_FaultAddress;
	uintptr_t m_Pc;
	uintptr_t m_Sp;
	uintptr_t m_Fp;
	uintptr_t m_Lr;
};

/**
 * 信号编号转可读名称，未知信号返回 "SIG?"。
 *
 * @ingroup Crash-Dumping
 */
const char *crashdump_signal_name(int Signal);

/**
 * 把致命信号现场格式化为报告正文（不含栈回溯）。
 *
 * 返回值不含结尾 '\0'；缓冲区不足时返回实际写入长度。
 *
 * @ingroup Crash-Dumping
 */
size_t crashdump_format_fatal_report(char *pBuffer, size_t BufferSize, const SCrashFatalReport &Report);

/**
 * 读取一帧调用栈所需的内存访问回调。
 *
 * 真实实现直接解引用地址，非法地址会触发二次崩溃，由调用方的重入保护兜底；
 * 测试实现可以返回构造好的假栈。
 *
 * @ingroup Crash-Dumping
 */
typedef bool (*FS_CRASH_READ_MEMORY)(uintptr_t Address, uintptr_t *pValue, void *pUser);

/**
 * 沿 frame pointer 链收集返回地址，第一帧固定为当前 pc。
 *
 * 遇到不可读帧、空返回地址或不再增长的 frame pointer 时停止，避免死循环。
 *
 * @param pReadMemory 内存读回调，不得为 nullptr。
 * @param Pc          崩溃时的程序计数器。
 * @param Fp          崩溃时的 frame pointer。
 * @param Sp          崩溃时的栈指针；为 0 时跳过有效性检查。
 * @param pFrames     输出数组。
 * @param MaxFrames   输出数组容量。
 *
 * @return 实际写入的帧数（含首帧）。
 *
 * @ingroup Crash-Dumping
 */
size_t crashdump_walk_frame_chain(FS_CRASH_READ_MEMORY pReadMemory, void *pUser, uintptr_t Pc, uintptr_t Fp, uintptr_t Sp, uintptr_t *pFrames, size_t MaxFrames);

/**
 * 把栈帧列表追加为报告末尾的多行文本。
 *
 * @param Length 缓冲区中已有内容的长度，新内容紧跟其后。
 *
 * @return 追加后的总长度。
 *
 * @ingroup Crash-Dumping
 */
size_t crashdump_append_frame_lines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames);

/**
 * 一个已加载模块的地址范围。
 *
 * 移动端在初始化阶段（进程健康时）读取 /proc/self/maps 缓存，崩溃处理器只做
 * 纯内存查表，把裸地址换算成「模块名+偏移」，回报问题时可直接 addr2line。
 *
 * @ingroup Crash-Dumping
 */
struct SCrashModuleRange
{
	uintptr_t m_Start;
	uintptr_t m_End;
	char m_aName[64];
};

/**
 * 查找包含指定地址的模块。
 *
 * @param pOffset 输出相对模块基址的偏移；未命中时不写入。
 *
 * @return 模块名；未命中返回 nullptr。
 *
 * @ingroup Crash-Dumping
 */
const char *crashdump_find_module(const SCrashModuleRange *pModules, size_t NumModules, uintptr_t Address, uintptr_t *pOffset);

/**
 * 选出超出保留数量的最旧报告，供移动端限制沙盒内的报告总量。
 *
 * @param pTimes     每个报告的修改时间。
 * @param NumReports 报告数量。
 * @param MaxKeep    保留数量。
 * @param pIndices   输出待删除索引，按时间从旧到新。
 * @param MaxIndices 输出容量。
 *
 * @return 实际写入的索引数量。
 *
 * @ingroup Crash-Dumping
 */
size_t crashdump_select_stale_reports(const time_t *pTimes, size_t NumReports, size_t MaxKeep, size_t *pIndices, size_t MaxIndices);

/**
 * 带模块信息的栈帧输出；未命中模块的地址只输出裸地址。
 *
 * @ingroup Crash-Dumping
 */
size_t crashdump_append_frame_lines(char *pBuffer, size_t BufferSize, size_t Length, const uintptr_t *pFrames, size_t NumFrames, const SCrashModuleRange *pModules, size_t NumModules);

#endif
