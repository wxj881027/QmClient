#ifndef GAME_MAP_MAP_LOAD_TASK_H
#define GAME_MAP_MAP_LOAD_TASK_H

#include <chrono>
#include <coroutine>
#include <exception>
#include <utility>

// 地图准备在主线程分段执行。暂停保留当前遍历位置与临时缓冲，取消时一起析构。
class [[nodiscard]] CMapLoadTask
{
public:
	struct promise_type;
	using THandle = std::coroutine_handle<promise_type>;
	using TClock = std::chrono::steady_clock;

	struct promise_type
	{
		THandle m_Child{};
		std::exception_ptr m_Exception;
		CMapLoadTask get_return_object() { return CMapLoadTask(THandle::from_promise(*this)); }
		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() { m_Exception = std::current_exception(); }
	};

	CMapLoadTask() = default;
	CMapLoadTask(CMapLoadTask &&Other) noexcept :
		m_Handle(std::exchange(Other.m_Handle, {})) {}
	CMapLoadTask &operator=(CMapLoadTask Other) noexcept
	{
		std::swap(m_Handle, Other.m_Handle);
		return *this;
	}
	~CMapLoadTask()
	{
		if(m_Handle)
			m_Handle.destroy();
	}

	bool Done() const { return !m_Handle || m_Handle.done(); }
	bool Resume()
	{
		if(!Done())
		{
			THandle Leaf = m_Handle;
			while(Leaf.promise().m_Child && !Leaf.promise().m_Child.done())
				Leaf = Leaf.promise().m_Child;
			Leaf.resume();
		}
		if(Done() && m_Handle && m_Handle.promise().m_Exception)
			std::rethrow_exception(m_Handle.promise().m_Exception);
		return Done();
	}
	bool RunUntil(TClock::time_point Deadline)
	{
		do
		{
			if(Resume())
				return true;
		} while(TClock::now() < Deadline);
		return false;
	}
	void Finish()
	{
		while(!Resume())
		{
		}
	}

	// 子任务的所有者留在父协程帧内，父任务只在子任务结束后继续。
	struct CAwaiter
	{
		THandle m_Child;
		THandle m_Parent{};
		bool await_ready() const noexcept { return !m_Child || m_Child.done(); }
		void await_suspend(THandle Parent) noexcept
		{
			m_Parent = Parent;
			Parent.promise().m_Child = m_Child;
		}
		void await_resume() const
		{
			if(m_Parent)
				m_Parent.promise().m_Child = {};
			if(m_Child && m_Child.promise().m_Exception)
				std::rethrow_exception(m_Child.promise().m_Exception);
		}
	};
	CAwaiter operator co_await() const { return {m_Handle}; }

private:
	explicit CMapLoadTask(THandle Handle) :
		m_Handle(Handle) {}
	THandle m_Handle{};
};

#endif
