#ifndef TEST_SUPPORT_THREAD_CREATION_PROBE_H
#define TEST_SUPPORT_THREAD_CREATION_PROBE_H

#include <base/thread.h>

#include <atomic>
#include <memory>
#include <new>

// 只替换创建边界；成功的入口、等待与退出仍使用真实生产线程。
class CThreadCreationProbe
{
public:
	struct CCounts
	{
		std::atomic<int> m_Attempts{0};
		std::atomic<int> m_Completed{0};
	};

private:
	struct CStart
	{
		void (*m_pfnEntry)(void *);
		void *m_pUser;
		std::shared_ptr<CCounts> m_pCounts;
	};
	inline static thread_local CThreadCreationProbe *s_pCurrent = nullptr;
	CThreadCreationProbe *m_pPrevious;
	std::shared_ptr<CCounts> m_pCounts;
	int m_FailOnAttempt;

public:
	CThreadCreationProbe(std::shared_ptr<CCounts> pCounts, int FailOnAttempt) :
		m_pPrevious(s_pCurrent), m_pCounts(std::move(pCounts)), m_FailOnAttempt(FailOnAttempt)
	{
		s_pCurrent = this;
	}
	~CThreadCreationProbe() { s_pCurrent = m_pPrevious; }
	CThreadCreationProbe(const CThreadCreationProbe &) = delete;

	static void *Create(void (*pfnEntry)(void *), void *pUser, const char *pName) noexcept
	{
		if(!s_pCurrent)
			return nullptr;
		auto pCounts = s_pCurrent->m_pCounts;
		if(++pCounts->m_Attempts == s_pCurrent->m_FailOnAttempt)
			return nullptr;
		auto *pStart = new(std::nothrow) CStart{pfnEntry, pUser, std::move(pCounts)};
		if(!pStart)
			return nullptr;
		void *pThread = thread_try_init([](void *pData) {
			std::unique_ptr<CStart> pOwned(static_cast<CStart *>(pData));
			pOwned->m_pfnEntry(pOwned->m_pUser);
			++pOwned->m_pCounts->m_Completed;
		},
			pStart, pName);
		if(!pThread)
			delete pStart;
		return pThread;
	}
};

#endif
