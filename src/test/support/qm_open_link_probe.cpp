#include "qm_open_link_probe.h"

#include <base/system.h>

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID)
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

std::optional<int> QmOpenLinkProbe(int argc, const char **ppArgv)
{
	if(argc < 2 || str_comp(ppArgv[1], "--qm-test-open-link") != 0)
		return std::nullopt;
#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID)
	if(argc != 5 || (str_comp(ppArgv[2], "link") != 0 && str_comp(ppArgv[2], "file") != 0) ||
		(str_comp(ppArgv[4], "0") != 0 && str_comp(ppArgv[4], "1") != 0))
		return 2;
	const pid_t OriginalPid = getpid();
	const int Started = str_comp(ppArgv[2], "link") == 0 ? open_link(ppArgv[3]) : open_file(ppArgv[3]);
	if(getpid() != OriginalPid)
	{
		// 旧实现的 exec 失败分支会返回到这里；不让它继续执行 runner。
		_exit(70);
	}
	if(Started != 1)
		return 3;
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while(std::chrono::steady_clock::now() < Deadline)
	{
		int Status;
		const pid_t Child = waitpid(-1, &Status, WNOHANG);
		if(Child > 0)
		{
			const int ExpectedExit = ppArgv[4][0] - '0';
			if(!WIFEXITED(Status) || WEXITSTATUS(Status) != ExpectedExit)
				return 4;
			std::printf("launcher-exit=%d\n", WEXITSTATUS(Status));
			return 0;
		}
		if(Child < 0 && errno != EINTR)
			return 5;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return 6;
#else
	return 77;
#endif
}
