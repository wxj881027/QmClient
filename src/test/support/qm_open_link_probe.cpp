#include "qm_open_link_probe.h"

#include <base/os.h>
#include <base/str.h>

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN) && defined(WEXITED) && defined(WNOWAIT)
static int PrepareLauncherInput()
{
	int aPipe[2];
	if(pipe(aPipe) != 0)
		return -1;
	// 假启动器只继承读端，由探针在调用返回后关闭写端。
	const bool Ready = fcntl(aPipe[1], F_SETFD, FD_CLOEXEC) == 0 && dup2(aPipe[0], STDIN_FILENO) == STDIN_FILENO;
	if(aPipe[0] != STDIN_FILENO)
		close(aPipe[0]);
	if(!Ready)
	{
		close(aPipe[1]);
		return -1;
	}
	return aPipe[1];
}
#endif

std::optional<int> QmOpenLinkProbe(int argc, const char **ppArgv)
{
	if(argc < 2 || str_comp(ppArgv[1], "--qm-test-open-link") != 0)
		return std::nullopt;
#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN) && defined(WEXITED) && defined(WNOWAIT)
	const bool WaitForReturn = argc == 5 && str_comp(ppArgv[4], "wait-for-return") == 0;
	if((argc != 4 && !WaitForReturn) || (str_comp(ppArgv[2], "link") != 0 && str_comp(ppArgv[2], "file") != 0))
		return 2;
	const int LauncherInput = WaitForReturn ? PrepareLauncherInput() : -1;
	if(WaitForReturn && LauncherInput < 0)
		return 4;
	const pid_t OriginalPid = getpid();
	const int Started = str_comp(ppArgv[2], "link") == 0 ? os_open_link(ppArgv[3]) : os_open_file(ppArgv[3]);
	if(getpid() != OriginalPid)
	{
		// 即使退出状态由生产回收线程收取，也能检测 exec 失败后回到调用栈。
		static constexpr char aMessage[] = "launcher-returned-to-probe\n";
		while(write(STDERR_FILENO, aMessage, sizeof(aMessage) - 1) < 0 && errno == EINTR)
		{
		}
		_exit(70);
	}
	if(LauncherInput >= 0)
		close(LauncherInput);
	if(Started != 1)
		return 3;

	// 专用探针进程只启动这里的打开器。WNOWAIT 保留退出状态供生产线程回收。
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while(std::chrono::steady_clock::now() < Deadline)
	{
		siginfo_t Info{};
		if(waitid(P_ALL, 0, &Info, WEXITED | WNOHANG | WNOWAIT) < 0)
		{
			if(errno == ECHILD)
			{
				std::puts("launcher-reaped");
				return 0;
			}
			if(errno != EINTR)
				return 5;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return 6;
#else
	return 77;
#endif
}
