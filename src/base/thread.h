/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef BASE_THREAD_H
#define BASE_THREAD_H

/**
 * Threading related functions.
 *
 * @defgroup Threads Threading
 *
 * @see Locks
 * @see Semaphore
 */

/**
 * Creates a new thread.
 *
 * @ingroup Threads
 *
 * @param threadfunc Entry point for the new thread.
 * @param user Pointer to pass to the thread.
 * @param name Name describing the use of the thread.
 *
 * @return Handle for the new thread.
 */
void *thread_init(void (*threadfunc)(void *), void *user, const char *name);

// 分配或系统线程创建失败时返回空；失败时不会调用入口或接管 user。
// 命名属于尽力完成的附加操作，不影响已成功创建线程的句柄归属。
void *thread_try_init(void (*threadfunc)(void *), void *user, const char *name) noexcept;
using THREAD_CREATE = decltype(&thread_try_init);

/**
 * Waits for a thread to be done or destroyed.
 *
 * @ingroup Threads
 *
 * @param thread Thread to wait for.
 */
void thread_wait(void *thread);

/**
 * Yield the current thread's execution slice.
 *
 * @ingroup Threads
 */
void thread_yield();

/**
 * Requests the most precise timer wakeups that the system offers for the calling thread.
 *
 * @ingroup Threads
 *
 * @remark Wakeups from sleeping and waiting are delayed to save power by default,
 * on macOS by a quarter of the requested duration and on Linux by 50 microseconds.
 * On Windows SDL already raises the timer resolution while initializing.
 */
void thread_request_precise_wakeups();

/**
 * Puts the thread in the detached state, guaranteeing that
 * resources of the thread will be freed immediately when the
 * thread terminates.
 *
 * @ingroup Threads
 *
 * @param thread Thread to detach.
 */
void thread_detach(void *thread);

/**
 * Creates a new thread and detaches it.
 *
 * @ingroup Threads
 *
 * @param threadfunc Entry point for the new thread.
 * @param user Pointer to pass to the thread.
 * @param name Name describing the use of the thread.
 */
void thread_init_and_detach(void (*threadfunc)(void *), void *user, const char *name);

#endif
