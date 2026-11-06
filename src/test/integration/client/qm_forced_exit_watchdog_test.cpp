#include <engine/client/qm_forced_exit_watchdog.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

using namespace std::chrono_literals;

namespace
{
	struct CProbe
	{
		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		bool m_Started = false;
		bool m_Release = false;
		bool m_Finished = false;
		bool m_TimedOut = false;
		bool m_ThrowWait = false;
		int m_Exits = 0;
		static void Wait(void *pContext, std::chrono::milliseconds)
		{
			auto &Self = *static_cast<CProbe *>(pContext);
			std::unique_lock<std::mutex> Lock(Self.m_Mutex);
			Self.m_Started = true;
			Self.m_Changed.notify_all();
			if(!Self.m_Changed.wait_for(Lock, 2s, [&] { return Self.m_Release; }))
			{
				Self.m_TimedOut = true;
				throw 7;
			}
			if(Self.m_ThrowWait)
				throw 7;
		}
		bool WaitStarted()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Changed.wait_for(Lock, 2s, [&] { return m_Started; });
		}
		bool WaitFinished()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Changed.wait_for(Lock, 2s, [&] { return m_Finished; });
		}
		void Release()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Release = true;
			m_Changed.notify_all();
		}
	};

	// token只由退出回调拥有；析构在worker释放回调时发布完成，避免凭睡眠猜测收尾。
	struct CCompletion
	{
		std::shared_ptr<CProbe> m_pProbe;
		explicit CCompletion(std::shared_ptr<CProbe> pProbe) :
			m_pProbe(std::move(pProbe)) {}
		~CCompletion()
		{
			std::lock_guard<std::mutex> Lock(m_pProbe->m_Mutex);
			m_pProbe->m_Finished = true;
			m_pProbe->m_Changed.notify_all();
		}
	};

	auto ExitCallback(const std::shared_ptr<CProbe> &pProbe)
	{
		return [Token = std::make_shared<CCompletion>(pProbe)] {
			std::lock_guard<std::mutex> Lock(Token->m_pProbe->m_Mutex);
			++Token->m_pProbe->m_Exits;
		};
	}

	QmForcedExit::SOperations GatedOperations(const std::shared_ptr<CProbe> &pProbe)
	{
		QmForcedExit::SOperations Operations;
		Operations.m_pContext = pProbe.get();
		Operations.m_pfnWait = CProbe::Wait;
		return Operations;
	}
}

TEST(QmForcedExitWatchdogNative, NativeDetachedWorkerCompletesTheExitCallback)
{
	auto Probe = std::make_shared<CProbe>();
	QmForcedExit::CWatchdog Watchdog;
	ASSERT_TRUE(Watchdog.Start(1ms, ExitCallback(Probe)));
	ASSERT_TRUE(Probe->WaitFinished());
	EXPECT_EQ(Probe->m_Exits, 1);
	EXPECT_FALSE(Watchdog.Active());
}

TEST(QmForcedExitWatchdogNative, StopCancelsWorkerBeforeItsDeadlineCheck)
{
	auto Probe = std::make_shared<CProbe>();
	QmForcedExit::CWatchdog Watchdog;
	ASSERT_TRUE(Watchdog.Start(10s, ExitCallback(Probe), GatedOperations(Probe)));
	ASSERT_TRUE(Probe->WaitStarted());
	EXPECT_TRUE(Watchdog.Stop());
	Probe->Release();
	ASSERT_TRUE(Probe->WaitFinished());
	EXPECT_FALSE(Probe->m_TimedOut);
	EXPECT_EQ(Probe->m_Exits, 0);
}

TEST(QmForcedExitWatchdogNative, ReplacementCancelsOldWorkerAndKeepsNewWorker)
{
	auto First = std::make_shared<CProbe>();
	auto Second = std::make_shared<CProbe>();
	QmForcedExit::CWatchdog Watchdog;
	ASSERT_TRUE(Watchdog.Start(10s, ExitCallback(First), GatedOperations(First)));
	ASSERT_TRUE(First->WaitStarted());
	ASSERT_TRUE(Watchdog.Start(10s, ExitCallback(Second), GatedOperations(Second)));
	ASSERT_TRUE(Second->WaitStarted());
	First->Release();
	ASSERT_TRUE(First->WaitFinished());
	EXPECT_EQ(First->m_Exits, 0);
	Second->Release();
	ASSERT_TRUE(Second->WaitFinished());
	EXPECT_EQ(Second->m_Exits, 1);
	EXPECT_FALSE(First->m_TimedOut);
	EXPECT_FALSE(Second->m_TimedOut);
}

TEST(QmForcedExitWatchdogNative, OwnerDestructionDoesNotLeaveWorkerBorrowingOwner)
{
	auto Probe = std::make_shared<CProbe>();
	{
		QmForcedExit::CWatchdog Watchdog;
		ASSERT_TRUE(Watchdog.Start(10s, ExitCallback(Probe), GatedOperations(Probe)));
		ASSERT_TRUE(Probe->WaitStarted());
	}
	Probe->Release();
	ASSERT_TRUE(Probe->WaitFinished());
	EXPECT_EQ(Probe->m_Exits, 0);
	EXPECT_FALSE(Probe->m_TimedOut);
}

TEST(QmForcedExitWatchdogNative, WaitExceptionFinishesOnlyItsWorker)
{
	auto Probe = std::make_shared<CProbe>();
	Probe->m_ThrowWait = true;
	QmForcedExit::CWatchdog Watchdog;
	ASSERT_TRUE(Watchdog.Start(10s, ExitCallback(Probe), GatedOperations(Probe)));
	ASSERT_TRUE(Probe->WaitStarted());
	Probe->Release();
	ASSERT_TRUE(Probe->WaitFinished());
	EXPECT_EQ(Probe->m_Exits, 0);
	EXPECT_FALSE(Probe->m_TimedOut);
	EXPECT_FALSE(Watchdog.Active());
}
