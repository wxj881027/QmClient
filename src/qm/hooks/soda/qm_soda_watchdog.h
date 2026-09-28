#ifndef QM_HOOKS_SODA_QM_SODA_WATCHDOG_H
#define QM_HOOKS_SODA_QM_SODA_WATCHDOG_H

#include <cstdint>
#include <string>

// 汽水音乐(SodaMusic.exe)主进程发现与 Node inspector(9229)激活。
// 汽水音乐是 Electron,有原生反调试:启动 argv 带 --remote-debugging-port 会 ~2s 自杀,
// 因此不能走网易云那套「重启 + 加 argv」。绕过手段 = 复刻 Node 的 process._debugProcess(pid):
//   1. 目标主进程启动时创建命名映射 "node-debug-handler-<pid>",内含一个指针 =
//      目标地址空间里 StartIoThreadWrapper 的函数地址。
//   2. OpenFileMapping + MapViewOfFile 读出该地址。
//   3. CreateRemoteThread 让目标自己跑该函数,拉起 inspector I/O 线程(默认 9229)。
// 非破坏性:只读映射 + 跑目标自带的激活函数,不改任何内存/状态。
namespace QmSodaWatchdog
{
	constexpr wchar_t TARGET_EXE[] = L"SodaMusic.exe";
	constexpr uint16_t INSPECTOR_PORT = 9229;

	// 查找汽水音乐主进程(Electron browser 进程,命令行无 --type=)。
	// 返回 PID;未运行时返回 0。
	uint32_t FindMainPid();

	// 激活指定 PID 的 Node inspector。返回 true 表示已激活(或已就绪)。
	bool EnsureInspector(uint32_t Pid);

	// 检查 9229 是否已监听(HTTP /json/list 可达)。
	bool IsInspectorUp();
} // namespace QmSodaWatchdog

#endif
