/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef BASE_SPHORE_H
#define BASE_SPHORE_H

#include "detect.h"

#include <atomic>
#include <memory>
#include <utility>

/**
 * @defgroup Semaphore Semaphores
 *
 * @see Threads
 */

#if defined(CONF_FAMILY_WINDOWS)
typedef void *SEMAPHORE;
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include <semaphore.h>
typedef sem_t *SEMAPHORE;
#elif defined(CONF_FAMILY_UNIX)
#include <semaphore.h>
typedef sem_t SEMAPHORE;
#else
#error not implemented on this platform
#endif

/**
 * @ingroup Semaphore
 */
void sphore_init(SEMAPHORE *sem);

// 创建失败返回 false，调用方不能等待、发送或销毁失败的信号量。
bool sphore_try_init(SEMAPHORE *sem);

/**
 * @ingroup Semaphore
 */
void sphore_wait(SEMAPHORE *sem);

/**
 * @ingroup Semaphore
 */
void sphore_signal(SEMAPHORE *sem);

/**
 * @ingroup Semaphore
 */
void sphore_destroy(SEMAPHORE *sem);

class CSemaphore
{
	SEMAPHORE m_Sem;
	// implement the counter separately, because the `sem_getvalue`-API is
	// deprecated on macOS: https://stackoverflow.com/a/16655541
	std::atomic_int m_Count{0};
	bool m_Initialized = false;

	struct SDeferredInitialization
	{
	};
	explicit CSemaphore(SDeferredInitialization) {}

public:
	CSemaphore()
	{
		sphore_init(&m_Sem);
		m_Initialized = true;
	}
	~CSemaphore()
	{
		if(m_Initialized)
			sphore_destroy(&m_Sem);
	}
	CSemaphore(const CSemaphore &) = delete;

	static std::unique_ptr<CSemaphore> TryCreate() noexcept
	{
		return TryCreate(sphore_try_init);
	}

	// 初始化器仅同步借用；失败须自行回收部分成果，成功后由对象接管。
	// 原生信号量在最终地址初始化，不复制 Unix 的 sem_t。
	template<typename TInitialize>
	static std::unique_ptr<CSemaphore> TryCreate(TInitialize &&Initialize) noexcept
	{
		try
		{
			auto pSemaphore = std::unique_ptr<CSemaphore>(new CSemaphore(SDeferredInitialization{}));
			if(!std::forward<TInitialize>(Initialize)(&pSemaphore->m_Sem))
				return nullptr;
			pSemaphore->m_Initialized = true;
			return pSemaphore;
		}
		catch(...)
		{
			return nullptr;
		}
	}

	int GetApproximateValue() { return m_Count.load(); }
	void Wait()
	{
		sphore_wait(&m_Sem);
		m_Count.fetch_sub(1);
	}
	void Signal()
	{
		m_Count.fetch_add(1);
		sphore_signal(&m_Sem);
	}
};

#endif
