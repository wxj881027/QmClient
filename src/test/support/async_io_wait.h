#ifndef TEST_SUPPORT_ASYNC_IO_WAIT_H
#define TEST_SUPPORT_ASYNC_IO_WAIT_H

#include <gtest/gtest.h>

#include <chrono>
#include <exception>
#include <future>
#include <thread>
#include <utility>

// 回调必须拥有其使用的全部资源；超时后不借用测试栈，也不阻塞后续测试。
template<typename F>
::testing::AssertionResult WaitForAsyncIo(F &&Function)
{
	std::packaged_task<void()> Task(std::forward<F>(Function));
	auto Result = Task.get_future();
	std::thread Worker(std::move(Task));
	if(Result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
	{
		Worker.detach();
		return ::testing::AssertionFailure() << "asynchronous IO did not finish within five seconds";
	}
	Worker.join();
	try
	{
		Result.get();
	}
	catch(const std::exception &Exception)
	{
		return ::testing::AssertionFailure() << Exception.what();
	}
	catch(...)
	{
		return ::testing::AssertionFailure() << "asynchronous IO raised an unknown exception";
	}
	return ::testing::AssertionSuccess();
}

#endif
