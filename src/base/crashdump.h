#ifndef BASE_CRASHDUMP_H
#define BASE_CRASHDUMP_H

#include "detect.h"

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
 * Windows 回退处理器只有在完整准备输出路径后才启用文件输出。报告、转储和
 * 确认标记各自写入候选文件，完成写入并关闭后才发布正式文件。
 *
 * @ingroup Crash-Dumping
 *
 * @param log_file_path UTF-8编码的崩溃日志绝对路径。
 */
void crashdump_init_if_available(const char *log_file_path);

#if defined(CONF_PLATFORM_ANDROID)
/**
 * 设置预打开的公共报告文件描述符（仅手机版）。
 *
 * 崩溃处理器只做 ftruncate/write/fsync，因此报告在崩溃后立刻出现在系统
 * 「下载」目录，不需要再启动一次客户端。传 -1 关闭该路径，处理器退回
 * 只写沙盒报告。桌面端没有这条路径，全部实现位于 Android 条件编译内。
 *
 * @ingroup Crash-Dumping
 */
void crashdump_set_live_report_fd(int file);
#endif

// 记录已识别的图形后端名称。传入 nullptr 或未识别的名称时清除记录。
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
 * 在客户端已同步显示断言窗口时，跳过下一次 Windows 致命报告的即时提示和确认标记。
 *
 * 致命处理器先尝试写入报告和转储，再检查此标记。
 */
void crashdump_suppress_reporter_once();

/**
 * 标记进程已进入退出清理阶段。
 *
 * 客户端在销毁图形后端前调用本函数。Windows 致命处理器在此期间检查异常地址，
 * 命中清单中的驱动模块时只记录日志，并跳过回退报告、转储和即时提示。
 *
 * @param p_driver_module 预留参数，当前为 nullptr 即可；驱动模块清单固定在
 *                        crashdump.cpp 内维护。
 *
 * @ingroup Crash-Dumping
 */
void crashdump_mark_shutdown_begin(const char *p_driver_module);

/**
 * 查询 Windows 致命处理器是否已在退出清理期间识别出图形驱动故障。
 *
 * 识别结果保留到进程结束，报告启动入口据此跳过提示。
 *
 * @return 已识别出这类故障时为 true。
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
 * 按文件名进行大小写不敏感的匹配。
 *
 * @param p_module_name 模块文件名，例如 "nvoglv64.dll"。
 *
 * @return 属于清单时为 true；nullptr 或空串为 false。
 *
 * @ingroup Crash-Dumping
 */
bool crashdump_is_ignorable_shutdown_driver_module(const char *p_module_name);

/**
 * 一次致命信号报告的现场信息。
 *
 * 字符串由调用方持有，须在格式化期间保持有效。
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
 * 读取一个指针宽度的内存值。读到有效值时写入 pValue 并返回 true。
 *
 * Android 回调直接读取栈内存。调用前，致命处理器会先尝试将信号和寄存器写入报告。
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
 * @param Sp          崩溃时的栈指针；为 0 时跳过 Fp 与 Sp 的比较。
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
 * Android 初始化时从 /proc/self/maps 缓存模块映射。信号处理器查表后，
 * 把地址标注为模块短名和相对缓存范围起点的偏移。
 *
 * @ingroup Crash-Dumping
 */
struct SCrashModuleRange
{
	uintptr_t m_Start;
	uintptr_t m_End;
	/** 显示用短名（basename）。 */
	char m_aName[64];
	/**
	 * 合并键为 Android 模块解析器传入的路径。容量与 AndroidLoadModuleRanges
	 * 的路径缓冲区一致，用于区分不同目录下的同名库。
	 */
	char m_aPath[384];
};

/**
 * 查找包含指定地址的模块。
 *
 * @param pOffset 输出相对缓存范围起点的偏移；未命中时不写入。
 *
 * @return 模块表内的短名；未命中返回 nullptr。
 *
 * @ingroup Crash-Dumping
 */
const char *crashdump_find_module(const SCrashModuleRange *pModules, size_t NumModules, uintptr_t Address, uintptr_t *pOffset);

/**
 * 登记一段已加载模块映射，同路径的多个段合并成一个范围。
 *
 * 合并键必须是完整路径：不同目录下的同名库（/system/lib64 与 APEX 各一份）
 * 若按 basename 合并，范围会横跨数个 GB，把小地址误判进该模块。
 *
 * @param pModules    模块表。
 * @param pNumModules 当前条目数，新增时递增。
 * @param MaxModules  模块表容量。
 * @param pPath       Android 模块解析器提供的路径，最多 383 字节。
 *
 * @return 是否写入了新条目；同路径合并或容量不足时返回 false。
 *
 * @ingroup Crash-Dumping
 */
bool crashdump_add_module_range(SCrashModuleRange *pModules, size_t *pNumModules, size_t MaxModules, uintptr_t Start, uintptr_t End, const char *pPath);

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
