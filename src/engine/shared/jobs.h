/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_JOBS_H
#define ENGINE_SHARED_JOBS_H

#include <base/lock.h>
#include <base/sphore.h>
#include <base/thread.h>

#include <atomic>
#include <memory>
#include <vector>

/**
 * A job which runs in a worker thread of a job pool.
 *
 * @see CJobPool
 */
class IJob
{
	friend class CJobPool;

public:
	/**
	 * The state of a job in the job pool.
	 */
	enum EJobState
	{
		/**
		 * Job has been created/queued but not started on a worker thread yet.
		 */
		STATE_QUEUED = 0,

		/**
		 * Job is currently running on a worker thread.
		 */
		STATE_RUNNING,

		/**
		 * Job was completed successfully.
		 */
		STATE_DONE,

		/**
		 * Job was aborted. Note the job may or may not still be running while
		 * in this state.
		 *
		 * @see IsAbortable
		 */
		STATE_ABORTED,

		// Run 已异常退出，输出可能不完整；不会再由 worker 修改。
		STATE_FAILED,
	};

private:
	std::shared_ptr<IJob> m_pNext;
	std::atomic<EJobState> m_State;
	std::atomic<bool> m_Abortable;
	std::atomic<bool> m_Finished;

protected:
	/**
	 * Performs tasks in a worker thread.
	 */
	virtual void Run() = 0;

	/**
	 * Sets whether this job can be aborted.
	 *
	 * @remark Has no effect if the job has already been aborted.
	 *
	 * @see IsAbortable
	 */
	void Abortable(bool Abortable);

public:
	IJob();
	virtual ~IJob();

	IJob(const IJob &Other) = delete;
	IJob &operator=(const IJob &Other) = delete;

	/**
	 * Returns the state of the job.
	 *
	 * @remark STATE_DONE / STATE_FAILED 发布后 worker 不再修改业务输出。
	 * STATE_FAILED 不证明业务成功；STATE_ABORTED 可能仍在运行。
	 * 具体任务若有独立的同步发布入口，其他访问遵循该入口的合同。
	 *
	 * @return State of the job.
	 */
	EJobState State() const;

	/**
	 * 返回作业是否处于成功、失败或取消终态。
	 *
	 * @return `true` if the job is done, `false` otherwise.
	 *
	 * @remark 取消作业可能仍在运行；Finished 另行确认 Run 已结束或已撤回。
	 * 基于通用状态判断业务成功时只接受 STATE_DONE。
	 */
	bool Done() const;

	// Run 已返回或已确认不再入队执行；单独的取消状态不能证明这一点。
	// 通用状态只以 STATE_DONE 判断成功；独立同步发布入口遵循其自身合同。
	bool Finished() const;

	/**
	 * Aborts the job, if it can be aborted.
	 *
	 * @return `true` if abort was accepted, `false` otherwise.
	 *
	 * @remark May be overridden to delegate abort to other jobs. Note that this
	 * function may be called from any thread and should be thread-safe.
	 */
	virtual bool Abort();

	/**
	 * Returns whether the job can be aborted. Jobs that are abortable may have
	 * their state set to `STATE_ABORTED` at any point if the job was aborted.
	 * The job state should be checked periodically in the `Run` function and the
	 * job should terminate at the earliest, safe opportunity when aborted.
	 * Scheduled jobs which are not abortable are guaranteed to fully complete
	 * before the job pool is shut down.
	 *
	 * @return `true` if the job can be aborted, `false` otherwise.
	 */
	bool IsAbortable() const;
};

/**
 * A job pool which runs jobs in one or more worker threads.
 *
 * @see IJob
 */
class CJobPool
{
	std::vector<void *> m_vpThreads;
	std::atomic<bool> m_Shutdown;

	CLock m_Lock;
	SEMAPHORE m_Semaphore;
	std::shared_ptr<IJob> m_pFirstJob GUARDED_BY(m_Lock);
	std::shared_ptr<IJob> m_pLastJob GUARDED_BY(m_Lock);

	CLock m_LockRunning;
	std::vector<std::shared_ptr<IJob>> m_RunningJobs GUARDED_BY(m_LockRunning);

	static void WorkerThread(void *pUser) NO_THREAD_SAFETY_ANALYSIS;
	void RunLoop() NO_THREAD_SAFETY_ANALYSIS;

public:
	CJobPool();
	~CJobPool();

	/**
	 * Initializes the job pool with the given number of worker threads.
	 *
	 * @param NumThreads The number of worker threads.
	 *
	 * @remark Must be called on the main thread.
	 */
	void Init(int NumThreads) REQUIRES(!m_Lock) REQUIRES(!m_LockRunning);

	// 失败时回收已启动线程并恢复关闭状态；可在主线程重新尝试。
	bool TryInit(int NumThreads, THREAD_CREATE pfnCreate = thread_try_init) REQUIRES(!m_Lock) REQUIRES(!m_LockRunning);

	/**
	 * Shuts down the job pool. Aborts all abortable jobs. Then waits for all
	 * worker threads to complete all remaining queued jobs and terminate.
	 *
	 * @remark Must be called on the main thread.
	 */
	void Shutdown() REQUIRES(!m_Lock) REQUIRES(!m_LockRunning);

	/**
	 * Adds a job to the queue of the job pool.
	 *
	 * @param pJob The job to enqueue.
	 *
	 * @remark If the job pool is already shutting down, no additional jobs
	 * will be enqueue anymore. Abortable jobs will immediately be aborted.
	 */
	void Add(std::shared_ptr<IJob> pJob) REQUIRES(!m_Lock);
	bool TryAdd(std::shared_ptr<IJob> pJob) REQUIRES(!m_Lock);
};
#endif
