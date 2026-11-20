#ifndef TEST_SUPPORT_REGULAR_FILE_OPEN_TIMEOUT_H
#define TEST_SUPPORT_REGULAR_FILE_OPEN_TIMEOUT_H

#include <base/detect.h>

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <unistd.h>

#include <csignal>

// 仅在死亡测试的子进程调用；回归成阻塞打开时也必须有界结束，不改变父进程信号状态。
inline void ArmRegularFileOpenChildTimeout()
{
	if(signal(SIGALRM, SIG_DFL) == SIG_ERR)
		_exit(90);
	sigset_t Signals;
	sigemptyset(&Signals);
	sigaddset(&Signals, SIGALRM);
	if(sigprocmask(SIG_UNBLOCK, &Signals, nullptr) != 0)
		_exit(91);
	alarm(5);
}
#endif

#endif
