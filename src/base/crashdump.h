#ifndef BASE_CRASHDUMP_H
#define BASE_CRASHDUMP_H

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

#endif
