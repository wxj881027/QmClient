/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "jobs.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/str.h>
#include <base/thread.h>

#include <algorithm>

IJob::IJob() :
	m_pNext(nullptr),
	m_State(STATE_QUEUED),
	m_Abortable(false)
{
}

IJob::~IJob() = default;

IJob::EJobState IJob::State() const
{
	return m_State;
}

bool IJob::Done() const
{
	EJobState State = m_State;
	return State != STATE_QUEUED && State != STATE_RUNNING;
}

bool IJob::Abort()
{
	if(!IsAbortable())
		return false;

	m_State = STATE_ABORTED;
	return true;
}

void IJob::Abortable(bool Abortable)
{
	m_Abortable = Abortable;
}

bool IJob::IsAbortable() const
{
	return m_Abortable;
}

CJobPool::CJobPool()
{
	m_Shutdown = true;
}

CJobPool::~CJobPool()
{
	if(!m_Shutdown)
	{
		Shutdown();
	}
}

void CJobPool::WorkerThread(void *pUser)
{
	static_cast<CJobPool *>(pUser)->RunLoop();
}

void CJobPool::RunLoop()
{
	while(true)
	{
		// wait for job to become available
		sphore_wait(&m_Semaphore);

		// fetch job from queue
		std::shared_ptr<IJob> pJob = nullptr;
		{
			const CLockScope LockScope(m_Lock);
			if(m_pFirstJob)
			{
				pJob = m_pFirstJob;
				m_pFirstJob = m_pFirstJob->m_pNext;
				// allow remaining objects in list to destruct, even when current object stays alive
				pJob->m_pNext = nullptr;
				if(!m_pFirstJob)
					m_pLastJob = nullptr;
			}
		}

		if(pJob)
		{
			IJob::EJobState OldStateQueued = IJob::STATE_QUEUED;
			if(!pJob->m_State.compare_exchange_strong(OldStateQueued, IJob::STATE_RUNNING))
			{
				if(OldStateQueued == IJob::STATE_ABORTED)
				{
					// job was aborted before it was started
					pJob->m_State = IJob::STATE_ABORTED;
					continue;
				}
				dbg_assert_failed("Job state invalid. Job was reused or uninitialized.");
			}

			// remember running jobs so we can abort them
			{
				const CLockScope LockScope(m_LockRunning);
				m_RunningJobs.push_back(pJob);
				// 注册与关闭可能交错，补发取消避免漏掉刚取出的作业。
				if(m_Shutdown)
					pJob->Abort();
			}
			pJob->Run();
			{
				const CLockScope LockScope(m_LockRunning);
				m_RunningJobs.erase(std::find(m_RunningJobs.begin(), m_RunningJobs.end(), pJob));
			}

			// do not change state to done if job was not completed successfully
			IJob::EJobState OldStateRunning = IJob::STATE_RUNNING;
			if(!pJob->m_State.compare_exchange_strong(OldStateRunning, IJob::STATE_DONE))
			{
				if(OldStateRunning != IJob::STATE_ABORTED)
				{
					dbg_assert_failed("Job state invalid, must be either running or aborted");
				}
			}
		}
		else if(m_Shutdown)
		{
			// shut down worker thread when pool is shutting down and no more jobs are left
			break;
		}
	}
}

void CJobPool::Init(int NumThreads)
{
	dbg_assert(TryInit(NumThreads), "Job pool initialization failed");
}

bool CJobPool::TryInit(int NumThreads, THREAD_CREATE pfnCreate)
{
	dbg_assert(m_Shutdown, "Job pool already running");
	if(NumThreads <= 0)
		return false;
	try
	{
		m_vpThreads.reserve(NumThreads);
		// 每个线程最多登记一个运行作业，在线程启动前准备完整容量。
		const CLockScope RunningLockScope(m_LockRunning);
		m_RunningJobs.reserve(NumThreads);
	}
	catch(...)
	{
		return false;
	}
	{
		const CLockScope LockScope(m_Lock);
		if(!sphore_try_init(&m_Semaphore))
			return false;
		m_pFirstJob = nullptr;
		m_pLastJob = nullptr;
		m_Shutdown = false;
		char aName[16];
		for(int i = 0; i < NumThreads; i++)
		{
			str_format(aName, sizeof(aName), "CJobPool W%d", i);
			void *pThread = pfnCreate(WorkerThread, this, aName);
			if(!pThread)
				break;
			m_vpThreads.push_back(pThread);
		}
	}
	if(m_vpThreads.size() == static_cast<size_t>(NumThreads))
		return true;
	// 先释放队列锁，再唤醒并等待部分启动的线程，避免启动回收互相等待。
	Shutdown();
	return false;
}

void CJobPool::Shutdown()
{
	dbg_assert(!m_Shutdown, "Job pool already shut down");
	m_Shutdown = true;

	// abort queued jobs
	{
		const CLockScope LockScope(m_Lock);
		std::shared_ptr<IJob> pJob = m_pFirstJob;
		std::shared_ptr<IJob> pPrev = nullptr;
		while(pJob != nullptr)
		{
			std::shared_ptr<IJob> pNext = pJob->m_pNext;
			if(pJob->Abort())
			{
				// only remove abortable jobs from queue
				pJob->m_pNext = nullptr;
				if(pPrev)
				{
					pPrev->m_pNext = pNext;
				}
				else
				{
					m_pFirstJob = pNext;
				}
			}
			else
			{
				pPrev = pJob;
			}
			pJob = pNext;
		}
		m_pLastJob = pPrev;
	}

	// abort running jobs
	{
		const CLockScope LockScope(m_LockRunning);
		for(const std::shared_ptr<IJob> &pJob : m_RunningJobs)
		{
			pJob->Abort();
		}
	}

	// wake up all worker threads
	for(size_t i = 0; i < m_vpThreads.size(); i++)
	{
		sphore_signal(&m_Semaphore);
	}

	// 作业可能持有引擎对象，线程不能在所属对象析构之后继续运行。
	// 等待真实结束；客户端退出看门狗负责不可取消作业卡死时的进程级兜底。
	for(void *pThread : m_vpThreads)
	{
		thread_wait(pThread);
	}

	m_vpThreads.clear();
	sphore_destroy(&m_Semaphore);
}

void CJobPool::Add(std::shared_ptr<IJob> pJob)
{
	// 与关闭同步检查和发信号，避免向已销毁的信号量提交作业。
	const CLockScope LockScope(m_Lock);
	if(m_Shutdown)
	{
		pJob->Abort();
		return;
	}
	if(m_pLastJob)
		m_pLastJob->m_pNext = pJob;
	m_pLastJob = std::move(pJob);
	if(!m_pFirstJob)
		m_pFirstJob = m_pLastJob;
	sphore_signal(&m_Semaphore);
}
