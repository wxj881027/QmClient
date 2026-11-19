/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "dbg.h"

#include "assert_handler_slot.h"
#include "fs.h"
#include "logger.h"
#include "str.h"

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <new>

std::atomic_bool dbg_assert_failing = false;
// 断言可能来自其它全局对象的清理；槽位使用固定存储且不参与静态析构。
static CAssertHandlerSlot &AssertHandlerSlot()
{
	struct SStorage
	{
		alignas(CAssertHandlerSlot) unsigned char m_aData[sizeof(CAssertHandlerSlot)];
		CAssertHandlerSlot *m_pSlot;
		SStorage() noexcept :
			m_pSlot(::new(m_aData) CAssertHandlerSlot) {}
	};
	static SStorage s_Storage;
	return *s_Storage.m_pSlot;
}

bool dbg_assert_has_failed()
{
	return dbg_assert_failing.load(std::memory_order_acquire);
}

void dbg_assert_imp(const char *filename, int line, const char *fmt, ...)
{
	// 递归或其他线程的后续断言不再重入日志、弹窗和收尾。
	if(dbg_assert_failing.exchange(true, std::memory_order_acq_rel))
		dbg_break();
	auto Handler = AssertHandlerSlot().TryTake();
	char msg[512];
	va_list args;
	va_start(args, fmt);
	str_format_v(msg, sizeof(msg), fmt, args);
	char error[1024];
	str_format(error, sizeof(error), "%s(%d): %s", fs_filename(filename), line, msg);
	va_end(args);
	log_error("assert", "%s", error);
	try
	{
		if(Handler)
			Handler(error);
	}
	catch(...)
	{
		// 回调只负责附加诊断，失败后仍收尾并按原断言终止。
	}
	try
	{
		log_global_logger_finish();
	}
	catch(...)
	{
		// 不能为了报告日志错误再次进入断言/日志链。
	}
	dbg_break();
}

void dbg_break()
{
#ifdef __GNUC__
	__builtin_trap();
#else
	abort();
#endif
}

void dbg_assert_set_handler(DBG_ASSERT_HANDLER handler)
{
	AssertHandlerSlot().Set(std::move(handler));
}

void dbg_msg(const char *sys, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	log_log_v(LEVEL_INFO, sys, fmt, args);
	va_end(args);
}
