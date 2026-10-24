#ifndef TEST_SUPPORT_QM_NETEASE_BRIDGE_OPERATIONS_H
#define TEST_SUPPORT_QM_NETEASE_BRIDGE_OPERATIONS_H

#include <qm/hooks/netease/qm_netease_frontend_bridge.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace NeteaseBridgeTest
{
	class COperations : public QmNeteaseBridge::CFrontendLyricBridge::COperations
	{
		mutable std::mutex m_DataMutex;
		std::mutex m_EventMutex;
		std::condition_variable m_Changed;
		TReportCallback m_Report;
		QmNeteaseHook::SSnapshotV5 m_Last{};
		bool m_HasSnapshot = false;
		int m_TickPermits = 0;
		int m_Order = 0;

		static bool TakeFailure(std::atomic_int &Failures)
		{
			int Remaining = Failures.load();
			while(Remaining > 0)
				if(Failures.compare_exchange_weak(Remaining, Remaining - 1))
					return true;
			return false;
		}

	public:
		enum class EFailure
		{
			NONE,
			OPEN,
			READ,
			REPORTS,
			PUBLISH,
			THREAD
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Exception = 1;
		bool m_OpenResult = true, m_ReportResult = true, m_RealThread = false;
		std::atomic_bool m_WriterOpen{false}, m_ReportsActive{false}, m_TickLive{false}, m_CloseAfterJoin{true};
		std::atomic_int m_Opened{0}, m_Closed{0}, m_ReportStarts{0}, m_ReportStops{0}, m_ThreadCalls{0}, m_TickExits{0}, m_Waits{0}, m_Published{0};
		std::atomic_int m_PublishFailures{0}, m_WaitFailures{0};
		mutable std::atomic_int m_ClockFailures{0};
		std::atomic_uint64_t m_Now{1000};
		int m_LastStopOrder = 0, m_LastCloseOrder = 0;

		void Fail(EFailure Stage) const
		{
			if(m_Failure != Stage)
				return;
			if(m_Exception == 1)
				throw std::bad_alloc();
			if(m_Exception == 2)
				throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			throw 7;
		}
		bool OpenWriter() override
		{
			m_WriterOpen = true;
			++m_Opened;
			Fail(EFailure::OPEN);
			return m_OpenResult;
		}
		void CloseWriter() noexcept override
		{
			if(m_WriterOpen.exchange(false))
			{
				m_CloseAfterJoin = !m_TickLive.load();
				m_LastCloseOrder = ++m_Order;
				++m_Closed;
			}
		}
		bool ReadSnapshot(QmNeteaseHook::SSnapshotV5 &Snapshot) override
		{
			Fail(EFailure::READ);
			const std::lock_guard<std::mutex> Lock(m_DataMutex);
			Snapshot = m_Last;
			return m_HasSnapshot;
		}
		bool PublishSnapshot(QmNeteaseHook::SSnapshotV5 Snapshot) override
		{
			Fail(EFailure::PUBLISH);
			if(TakeFailure(m_PublishFailures))
				throw std::bad_alloc();
			{
				const std::lock_guard<std::mutex> Lock(m_DataMutex);
				// fake 只记录传输结果，序列/校验调用生产接口，不模拟歌词状态。
				Snapshot.m_Sequence = 2;
				QmNeteaseHook::FinalizeSnapshotV5(&Snapshot);
				m_Last = Snapshot;
				m_HasSnapshot = true;
				++m_Published;
			}
			m_Changed.notify_all();
			return true;
		}
		bool StartReports(uint32_t, const std::wstring &, TReportCallback Callback) override
		{
			{
				const std::lock_guard<std::mutex> Lock(m_DataMutex);
				m_Report = std::move(Callback);
			}
			m_ReportsActive = true;
			++m_ReportStarts;
			Fail(EFailure::REPORTS);
			return m_ReportResult;
		}
		void StopReports() noexcept override
		{
			const std::lock_guard<std::mutex> Lock(m_DataMutex);
			m_Report = {};
			if(m_ReportsActive.exchange(false))
			{
				m_LastStopOrder = ++m_Order;
				++m_ReportStops;
			}
		}
		bool ReportsRunning() const noexcept override { return m_ReportsActive; }
		bool IsConnected() const noexcept override { return m_ReportsActive; }
		uint64_t NowTick() const override
		{
			if(TakeFailure(m_ClockFailures))
				throw std::bad_alloc();
			return m_Now;
		}
		std::thread CreateTickThread(std::function<void()> Run) override
		{
			++m_ThreadCalls;
			Fail(EFailure::THREAD);
			if(!m_RealThread)
				return {};
			return std::thread([this, Run = std::move(Run)] {
				m_TickLive = true;
				Run();
				m_TickLive = false;
				++m_TickExits;
				m_Changed.notify_all();
			});
		}
		void WaitForTick(const std::atomic_bool &Stop) override
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			std::unique_lock<std::mutex> Lock(m_EventMutex);
			++m_Waits;
			m_Changed.notify_all();
			while(m_TickPermits == 0 && !Stop.load())
			{
				const auto Now = std::chrono::steady_clock::now();
				if(Now >= Deadline)
					throw std::runtime_error("tick permit timeout");
				m_Changed.wait_until(Lock, std::min(Deadline, Now + std::chrono::milliseconds(5)));
			}
			if(Stop.load())
				return;
			--m_TickPermits;
			if(TakeFailure(m_WaitFailures))
				throw std::bad_alloc();
		}
		void Advance()
		{
			const std::lock_guard<std::mutex> Lock(m_EventMutex);
			++m_TickPermits;
			m_Changed.notify_all();
		}
		template<typename F>
		bool Wait(F Predicate)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			std::unique_lock<std::mutex> Lock(m_EventMutex);
			while(!Predicate())
			{
				const auto Now = std::chrono::steady_clock::now();
				if(Now >= Deadline)
					return false;
				m_Changed.wait_until(Lock, std::min(Deadline, Now + std::chrono::milliseconds(5)));
			}
			return true;
		}
		void Emit(std::string_view Report)
		{
			TReportCallback Callback;
			{
				const std::lock_guard<std::mutex> Lock(m_DataMutex);
				Callback = m_Report;
			}
			if(Callback)
				Callback(Report);
		}
		QmNeteaseHook::SSnapshotV5 Latest() const
		{
			const std::lock_guard<std::mutex> Lock(m_DataMutex);
			return m_Last;
		}
	};
}

#endif
