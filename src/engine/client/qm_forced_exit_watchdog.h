#ifndef ENGINE_CLIENT_QM_FORCED_EXIT_WATCHDOG_H
#define ENGINE_CLIENT_QM_FORCED_EXIT_WATCHDOG_H

#include <base/detect.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <process.h>
#elif defined(CONF_FAMILY_UNIX)
#include <pthread.h>
#endif

namespace QmForcedExit
{
	using FEntry = void (*)(void *) noexcept;

	namespace detail
	{
		struct SDetachedRun
		{
			FEntry m_pfnEntry;
			void *m_pUser;
		};

#if defined(CONF_FAMILY_WINDOWS)
		inline unsigned __stdcall DetachedEntry(void *pUser) noexcept
#elif defined(CONF_FAMILY_UNIX)
		inline void *DetachedEntry(void *pUser) noexcept
#endif
#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_FAMILY_UNIX)
		{
			std::unique_ptr<SDetachedRun> pRun(static_cast<SDetachedRun *>(pUser));
			const auto Entry = pRun->m_pfnEntry;
			void *pArgument = pRun->m_pUser;
			pRun.reset();
			Entry(pArgument);
			return 0;
		}
#endif

		// true后参数已属于线程，入口可能在创建调用返回前完成；false不接管参数。
		inline bool Launch(void *, FEntry Entry, void *pUser) noexcept
		{
			std::unique_ptr<SDetachedRun> pRun;
			try
			{
				pRun = std::make_unique<SDetachedRun>(SDetachedRun{Entry, pUser});
			}
			catch(...)
			{
				return false;
			}
#if defined(CONF_FAMILY_WINDOWS)
			auto *pRaw = pRun.release();
			const uintptr_t Handle = _beginthreadex(nullptr, 0, DetachedEntry, pRaw, 0, nullptr);
			if(Handle == 0)
			{
				delete pRaw;
				return false;
			}
			// 已成功创建，关闭句柄的反馈不能改报拒收，否则会回收活动线程的参数。
			(void)CloseHandle(reinterpret_cast<HANDLE>(Handle));
			return true;
#elif defined(CONF_FAMILY_UNIX)
			pthread_attr_t Attributes;
			if(pthread_attr_init(&Attributes) != 0)
				return false;
			if(pthread_attr_setdetachstate(&Attributes, PTHREAD_CREATE_DETACHED) != 0)
			{
				pthread_attr_destroy(&Attributes);
				return false;
			}
			pthread_t Thread;
			auto *pRaw = pRun.release();
			const int Result = pthread_create(&Thread, &Attributes, DetachedEntry, pRaw);
			pthread_attr_destroy(&Attributes);
			if(Result != 0)
				delete pRaw;
			return Result == 0;
#else
			return false;
#endif
		}

		inline void Wait(void *, std::chrono::milliseconds Timeout)
		{
			std::this_thread::sleep_for(Timeout);
		}
	}

	// context借用至线程完成；Launch必须不抛异常并遵守参数接管约定。
	struct SOperations
	{
		void *m_pContext = nullptr;
		bool (*m_pfnLaunch)(void *, FEntry, void *) noexcept = detail::Launch;
		void (*m_pfnWait)(void *, std::chrono::milliseconds) = detail::Wait;
	};

	class CWatchdog
	{
	public:
		struct SState
		{
			const uint64_t m_Epoch;
			std::atomic<bool> m_Armed{true};
			explicit SState(uint64_t Epoch) noexcept :
				m_Epoch(Epoch) {}
		};

	private:
		struct SControl
		{
			std::atomic<uint64_t> m_Epoch{0};
			std::shared_ptr<SState> m_pActive;
		};
		struct SRun
		{
			std::shared_ptr<SControl> m_pControl;
			std::shared_ptr<SState> m_pState;
			std::chrono::milliseconds m_Timeout;
			std::function<void()> m_Exit;
			SOperations m_Operations;

			static void Run(void *pUser) noexcept
			{
				std::unique_ptr<SRun> pRun(static_cast<SRun *>(pUser));
				try
				{
					pRun->m_Operations.m_pfnWait(pRun->m_Operations.m_pContext, pRun->m_Timeout);
					if(pRun->m_pControl->m_Epoch.load(std::memory_order_acquire) == pRun->m_pState->m_Epoch &&
						pRun->m_pState->m_Armed.exchange(false, std::memory_order_acq_rel))
						pRun->m_Exit();
				}
				catch(...)
				{
					// 退出保障自身失败不能再次以线程异常终止整个客户端。
				}
				pRun->m_pState->m_Armed.store(false, std::memory_order_release);
			}
		};
		std::shared_ptr<SControl> m_pControl;

	public:
		CWatchdog() noexcept = default;
		CWatchdog(const CWatchdog &) = delete;
		CWatchdog &operator=(const CWatchdog &) = delete;
		~CWatchdog() { Stop(); }

		template<typename TExit, typename TFactory>
		bool Start(std::chrono::milliseconds Timeout, TExit &&Exit, const SOperations &Operations, TFactory &&Factory) noexcept
		{
			if(Timeout.count() < 0 || !Operations.m_pfnLaunch || !Operations.m_pfnWait)
				return false;
			try
			{
				auto pControl = std::atomic_load_explicit(&m_pControl, std::memory_order_acquire);
				if(!pControl)
				{
					auto pNew = std::make_shared<SControl>();
					if(std::atomic_compare_exchange_strong_explicit(&m_pControl, &pControl, pNew, std::memory_order_acq_rel, std::memory_order_acquire))
						pControl = std::move(pNew);
				}
				const uint64_t Epoch = pControl->m_Epoch.load(std::memory_order_acquire);
				auto pState = std::forward<TFactory>(Factory)(Epoch);
				if(!pState || pState->m_Epoch != Epoch)
					return false;
				auto pRun = std::make_unique<SRun>(SRun{pControl, pState, Timeout, std::function<void()>(std::forward<TExit>(Exit)), Operations});
				if(!pRun->m_Exit || pControl->m_Epoch.load(std::memory_order_acquire) != Epoch)
					return false;
				auto *pRaw = pRun.release();
				if(!Operations.m_pfnLaunch(Operations.m_pContext, SRun::Run, pRaw))
				{
					delete pRaw;
					return false;
				}
				auto pOld = std::atomic_exchange_explicit(&pControl->m_pActive, pState, std::memory_order_acq_rel);
				if(pOld)
					pOld->m_Armed.store(false, std::memory_order_release);
				if(pControl->m_Epoch.load(std::memory_order_acquire) != Epoch)
				{
					pState->m_Armed.store(false, std::memory_order_release);
					auto pExpected = pState;
					std::atomic_compare_exchange_strong_explicit(&pControl->m_pActive, &pExpected, std::shared_ptr<SState>{}, std::memory_order_acq_rel, std::memory_order_acquire);
					return false;
				}
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

		template<typename TExit>
		bool Start(std::chrono::milliseconds Timeout, TExit &&Exit, const SOperations &Operations = {}) noexcept
		{
			return Start(Timeout, std::forward<TExit>(Exit), Operations,
				[](uint64_t Epoch) { return std::make_shared<SState>(Epoch); });
		}

		bool Active() const noexcept
		{
			const auto pControl = std::atomic_load_explicit(&m_pControl, std::memory_order_acquire);
			if(!pControl)
				return false;
			const auto pState = std::atomic_load_explicit(&pControl->m_pActive, std::memory_order_acquire);
			return pState && pState->m_Epoch == pControl->m_Epoch.load(std::memory_order_acquire) && pState->m_Armed.load(std::memory_order_acquire);
		}

		bool Stop() noexcept
		{
			const auto pControl = std::atomic_load_explicit(&m_pControl, std::memory_order_acquire);
			if(!pControl)
				return false;
			const uint64_t Epoch = pControl->m_Epoch.fetch_add(1, std::memory_order_acq_rel);
			const auto pState = std::atomic_exchange_explicit(&pControl->m_pActive, std::shared_ptr<SState>{}, std::memory_order_acq_rel);
			return pState && pState->m_Armed.exchange(false, std::memory_order_acq_rel) && pState->m_Epoch == Epoch;
		}
	};
}

#endif
