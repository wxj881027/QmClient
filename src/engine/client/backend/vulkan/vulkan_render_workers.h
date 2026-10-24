#ifndef ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RENDER_WORKERS_H
#define ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RENDER_WORKERS_H

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class CVulkanRenderWorkers
{
public:
	struct SWorker
	{
		bool m_IsRendering = false;
		std::thread m_Thread;
		std::mutex m_Mutex;
		std::condition_variable m_Cond;
		bool m_Finished = false;
		bool m_Started = false;
	};

private:
	std::vector<std::unique_ptr<SWorker>> m_vpWorkers;

public:
	CVulkanRenderWorkers() = default;
	~CVulkanRenderWorkers() { Stop(); }
	CVulkanRenderWorkers(const CVulkanRenderWorkers &) = delete;
	CVulkanRenderWorkers &operator=(const CVulkanRenderWorkers &) = delete;

	size_t Size() const noexcept { return m_vpWorkers.size(); }
	SWorker &operator[](size_t Index) { return *m_vpWorkers[Index]; }

	// 仅由所有者启动和停止；工作线程只在自己的锁下读取状态。
	template<typename TRun, typename TLaunch>
	bool Start(size_t Count, TRun &&Run, TLaunch &&Launch) noexcept
	{
		if(!m_vpWorkers.empty())
			return false;
		try
		{
			// 先完成列表和状态分配，工作线程启动后不再改变列表结构。
			m_vpWorkers.reserve(Count);
			for(size_t Index = 0; Index < Count; ++Index)
				m_vpWorkers.emplace_back(std::make_unique<SWorker>());
			for(size_t Index = 0; Index < Count; ++Index)
			{
				auto &Worker = *m_vpWorkers[Index];
				std::unique_lock<std::mutex> Lock(Worker.m_Mutex);
				Worker.m_Thread = Launch([this, Run = Run, Index] {
					auto &StartedWorker = *m_vpWorkers[Index];
					{
						std::unique_lock<std::mutex> StartedLock(StartedWorker.m_Mutex);
						StartedWorker.m_Started = true;
						StartedWorker.m_Cond.notify_one();
					}
					Run(Index);
				});
				if(!Worker.m_Thread.joinable())
				{
					Lock.unlock();
					Stop();
					return false;
				}
				Worker.m_Cond.wait(Lock, [&] { return Worker.m_Started; });
			}
			return true;
		}
		catch(...)
		{
			// 原生线程创建、入口复制或状态分配失败时，先结束已经启动的部分。
			Stop();
			return false;
		}
	}

	template<typename TRun>
	bool Start(size_t Count, TRun &&Run) noexcept
	{
		return Start(Count, std::forward<TRun>(Run), [](auto &&Entry) { return std::thread(std::forward<decltype(Entry)>(Entry)); });
	}

	void Stop() noexcept
	{
		for(auto &pWorker : m_vpWorkers)
		{
			std::unique_lock<std::mutex> Lock(pWorker->m_Mutex);
			pWorker->m_Finished = true;
			pWorker->m_Cond.notify_all();
		}
		for(auto &pWorker : m_vpWorkers)
		{
			if(pWorker->m_Thread.joinable())
				pWorker->m_Thread.join();
		}
		m_vpWorkers.clear();
	}
};

#endif
