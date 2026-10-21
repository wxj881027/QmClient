#ifndef BASE_THREAD_STARTUP_H
#define BASE_THREAD_STARTUP_H

#include <future>
#include <utility>

// 启动结果在业务入口前发布；准备失败或生产者提前退出时，调用方可保留 user 的所有权。
class CThreadStartup
{
	std::promise<bool> m_Result;

public:
	std::future<bool> GetResult() { return m_Result.get_future(); }
	template<typename F>
	void Run(bool Initialized, F &&Entry) noexcept
	{
		try
		{
			m_Result.set_value(Initialized);
			if(Initialized)
				std::forward<F>(Entry)();
		}
		catch(...)
		{
			// 线程入口无法向调用栈传播异常；未发布结果由生产者析构通知失败。
		}
	}
	static bool Wait(std::future<bool> &Result) noexcept
	{
		try
		{
			return Result.get();
		}
		catch(...)
		{
			return false;
		}
	}
};

#endif
