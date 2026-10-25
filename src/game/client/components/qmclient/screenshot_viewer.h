#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_VIEWER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_VIEWER_H

#include <base/thread.h>
#include <base/types.h>

#include <future>
#include <memory>
#include <type_traits>
#include <utility>

// 先准备回收线程，再启动子进程；启动异常由 promise 析构通知线程退出。
// Launch 抛出时不能遗留未返回的进程，Wait 只持有自己的值快照。
template<typename TLaunch, typename TWait>
bool QmLaunchScreenshotViewerProcess(TLaunch &&Launch, TWait &&Wait, THREAD_CREATE pfnCreate = thread_try_init) noexcept
{
	struct SWaiter
	{
		std::future<PROCESS> m_Process;
		std::decay_t<TWait> m_Wait;
	};
	try
	{
		std::promise<PROCESS> ProcessPromise;
		std::unique_ptr<SWaiter> pWaiter(new SWaiter{ProcessPromise.get_future(), std::forward<TWait>(Wait)});
		SWaiter *pArgument = pWaiter.release();
		void *pThread = pfnCreate([](void *pUser) noexcept {
			std::unique_ptr<SWaiter> pContext(static_cast<SWaiter *>(pUser));
			try
			{
				const PROCESS Process = pContext->m_Process.get();
				if(Process != INVALID_PROCESS)
					pContext->m_Wait(Process);
			}
			catch(...)
			{
				// 未发布有效进程或回收失败时，线程自行释放上下文。
			}
		},
			pArgument, "screenshot-wait");
		if(pThread == nullptr)
		{
			delete pArgument;
			return false;
		}
		thread_detach(pThread);
		const PROCESS Process = std::forward<TLaunch>(Launch)();
		ProcessPromise.set_value(Process);
		return Process != INVALID_PROCESS;
	}
	catch(...)
	{
		return false;
	}
}

// 桌面平台使用当前程序的专用看图进程，不初始化游戏、网络或配置存储。
class IClient;
bool OpenQmScreenshotViewer(IClient *pClient, const char *pPath, const char *pErrorTitle, const char *pErrorMessage);
int RunQmScreenshotViewer(const char *pPath, const char *pErrorTitle, const char *pErrorMessage);

#endif
