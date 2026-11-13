#ifndef BASE_ASSERT_HANDLER_SLOT_H
#define BASE_ASSERT_HANDLER_SLOT_H

#include "dbg.h"

#include <mutex>

// 首次断言独占取走回调；值捕获随回调转移，借用资源仍须由调用方保证寿命。
class CAssertHandlerSlot
{
	std::mutex m_Mutex;
	DBG_ASSERT_HANDLER m_Handler;

public:
	void Set(DBG_ASSERT_HANDLER Handler)
	{
		{
			std::lock_guard Lock(m_Mutex);
			m_Handler.swap(Handler);
		}
		// 被替换的回调及其捕获在解锁后释放，析构可重新访问本槽位。
	}

	DBG_ASSERT_HANDLER TryTake() noexcept
	{
		DBG_ASSERT_HANDLER Result;
		try
		{
			// 诊断不能等待正在注册/注销的线程；忙时跳过可选回调。
			std::unique_lock Lock(m_Mutex, std::try_to_lock);
			if(Lock.owns_lock())
				Result.swap(m_Handler);
		}
		catch(...)
		{
			// 系统锁失败也不能取代原断言的终止路径。
		}
		return Result;
	}
};

#endif
