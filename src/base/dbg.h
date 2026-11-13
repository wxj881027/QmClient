/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef BASE_DBG_H
#define BASE_DBG_H

#include <functional>

/**
 * Utilities for debugging.
 *
 * @defgroup Debug Debugging
 */

/**
 * Breaks into the debugger based on a test.
 *
 * @ingroup Debug
 *
 * @param test Result of the test.
 * @param fmt A printf styled format message that should be printed if the test fails.
 *
 * @remark Also works in release mode.
 *
 * @see dbg_break
 */
#define dbg_assert(test, fmt, ...) \
	do \
	{ \
		if(!(test)) [[unlikely]] \
		{ \
			dbg_assert_imp(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
		} \
	} while(false)

/**
 * Breaks into the debugger with a message.
 *
 * @ingroup Debug
 * @param fmt A printf styled format message that should be printed in case the
 * code is reached.
 *
 * @remark Also works in release mode.
 *
 * @see dbg_break
 */
#define dbg_assert_failed(fmt, ...) [[unlikely]] dbg_assert_imp(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

/**
 * Use the @link dbg_assert @endlink function instead!
 *
 * @ingroup Debug
 *
 * @see dbg_assert
 */
extern "C" [[gnu::format(printf, 3, 4)]] [[noreturn]] void
dbg_assert_imp(const char *filename, int line, const char *fmt, ...);

/**
 * Checks whether the program is currently shutting down due to a failed
 * assert.
 *
 * @ingroup Debug
 *
 * @return indication whether the program is currently shutting down due to a
 * failed assert.
 *
 * @see dbg_assert
 */
bool dbg_assert_has_failed();

/**
 * Breaks into the debugger.
 *
 * @ingroup Debug
 *
 * @remark Also works in release mode.
 *
 * @see dbg_assert
 */
[[noreturn]] void dbg_break();

/**
 * Callback function type for @link dbg_assert_set_handler @endlink.
 *
 * @ingroup Debug
 *
 * @param message The message that a @link dbg_assert @endlink is failing with.
 *
 * @see dbg_assert_set_handler
 */
typedef std::function<void(const char *message)> DBG_ASSERT_HANDLER;

/**
 * Sets a callback function that will be invoked before breaking into the
 * debugger in @link dbg_assert @endlink.
 *
 * @ingroup Debug
 *
 * @remark Also works in release mode.
 *
 * @see dbg_assert
 */
// 允许并发注册/注销；注销只移除尚未取出的回调，不等待已取出的回调。
// 断言遇到正在修改的槽位时可跳过回调；值捕获独立持有，借用资源须保持有效。
// 回调异常不会跳过默认终止，回调及其捕获的析构不得抛异常。
// 槽位不参与静态析构；需要释放待调用回调的捕获时须显式注销。
void dbg_assert_set_handler(DBG_ASSERT_HANDLER handler);

/**
 * Prints a debug message.
 *
 * @ingroup Debug
 *
 * @param sys A string that describes what system the message belongs to.
 * @param fmt A printf styled format string.
 *
 * @remark Also works in release mode.
 *
 * @deprecated Use `log_*` functions with appropriate severity instead.
 */
[[gnu::format(printf, 2, 3)]] void dbg_msg(const char *sys, const char *fmt, ...);

#endif
