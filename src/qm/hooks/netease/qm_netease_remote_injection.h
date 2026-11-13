#ifndef QM_HOOKS_NETEASE_QM_NETEASE_REMOTE_INJECTION_H
#define QM_HOOKS_NETEASE_QM_NETEASE_REMOTE_INJECTION_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace QmNeteaseHelper
{
	// 本地句柄可关闭，但远端线程结束前不能释放它借用的路径。
	// 由 helper 主线程串行调用，借用的 Operations 必须活过本对象。
	class CRemoteInjection
	{
	public:
		enum class EWait
		{
			COMPLETE,
			PENDING,
			FAILED
		};
		class COperations
		{
		public:
			virtual ~COperations() = default;
			virtual void *OpenProcess(uint32_t Pid) = 0;
			virtual void *AllocatePath(void *pProcess, size_t Bytes) = 0;
			virtual bool WritePath(void *pProcess, void *pRemote, const wchar_t *pPath, size_t Bytes) = 0;
			// 返回已启动线程的句柄；失败或抛异常时不得遗留未返回的线程。
			virtual void *CreateThread(void *pProcess, void *pRemote) = 0;
			virtual EWait Wait(void *pThread, uint32_t TimeoutMs) noexcept = 0;
			virtual void FreePath(void *pProcess, void *pRemote) noexcept = 0;
			virtual void Close(void *pHandle) noexcept = 0;
		};

		explicit CRemoteInjection(COperations &Operations) :
			m_Operations(Operations) {}
		~CRemoteInjection()
		{
			Release(!m_pThread || m_Operations.Wait(m_pThread, 0) == EWait::COMPLETE);
		}
		CRemoteInjection(const CRemoteInjection &) = delete;
		CRemoteInjection &operator=(const CRemoteInjection &) = delete;

		// 返回本次任务是否成功启动，不把线程已启动当作 DLL 已完成加载。
		bool Start(uint32_t Pid, const std::wstring &Path) noexcept
		{
			Poll();
			if(Pending() || Pid == 0 || Path.empty() || Path.size() > std::numeric_limits<size_t>::max() / sizeof(wchar_t) - 1)
				return false;
			try
			{
				m_pProcess = m_Operations.OpenProcess(Pid);
				if(!m_pProcess)
					return false;
				const size_t Bytes = (Path.size() + 1) * sizeof(wchar_t);
				m_pRemote = m_Operations.AllocatePath(m_pProcess, Bytes);
				if(!m_pRemote || !m_Operations.WritePath(m_pProcess, m_pRemote, Path.c_str(), Bytes))
				{
					Release(true);
					return false;
				}
				m_pThread = m_Operations.CreateThread(m_pProcess, m_pRemote);
				if(!m_pThread)
				{
					Release(true);
					return false;
				}
				// 保留原十秒等待；超时后由原 Watch 轮询继续跟踪，不重复启动。
				if(m_Operations.Wait(m_pThread, 10000) == EWait::COMPLETE)
					Release(true);
				return true;
			}
			catch(...)
			{
				Release(!m_pThread || m_Operations.Wait(m_pThread, 0) == EWait::COMPLETE);
				return false;
			}
		}
		void Poll() noexcept
		{
			if(m_pThread && m_Operations.Wait(m_pThread, 0) == EWait::COMPLETE)
				Release(true);
		}
		bool Pending() const noexcept { return m_pThread != nullptr; }

	private:
		void Release(bool ThreadStopped) noexcept
		{
			// helper 退出时线程若仍在运行，路径保留到目标进程退出，避免远端 UAF。
			if(m_pRemote && (!m_pThread || ThreadStopped))
				m_Operations.FreePath(m_pProcess, m_pRemote);
			if(m_pThread)
				m_Operations.Close(m_pThread);
			if(m_pProcess)
				m_Operations.Close(m_pProcess);
			m_pRemote = m_pThread = m_pProcess = nullptr;
		}
		COperations &m_Operations;
		void *m_pProcess = nullptr;
		void *m_pRemote = nullptr;
		void *m_pThread = nullptr;
	};
}

#endif
