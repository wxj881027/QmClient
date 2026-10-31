#include <game/client/components/qmclient/ime/qm_ime_runtime.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using namespace std::chrono_literals;
	using ECloseResult = CQmImeRuntimeShutdown::EResult;
	constexpr auto OBSERVE_TIMEOUT = 2s;
	constexpr auto FAKE_WORK_TIMEOUT = 10s;

	// 仅模拟 Rime 的维护线程、通知上下文和 C API；关闭算法由生产实现执行。
	struct SApiState
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		CQmImeRuntime *m_pContext = nullptr;
		bool m_WorkFinished = true;
		bool m_AllowCallbackStart = false;
		bool m_AllowCallbackReturn = false;
		bool m_CallbackEntered = false;
		bool m_JoinEntered = false;
		bool m_Destroyed = false;
		bool m_FailureObserved = false;
		bool m_TimedOut = false;
		std::vector<std::string> m_vCalls;

		template<typename FPredicate>
		bool WaitFor(FPredicate Predicate)
		{
			std::unique_lock Lock(m_Mutex);
			return m_Condition.wait_for(Lock, OBSERVE_TIMEOUT, Predicate);
		}
		void Release(bool HasWorker)
		{
			{
				std::lock_guard Lock(m_Mutex);
				m_AllowCallbackStart = true;
				m_AllowCallbackReturn = true;
				if(!HasWorker)
					m_WorkFinished = true;
			}
			m_Condition.notify_all();
		}
		void DeliverFailure()
		{
			std::unique_lock Lock(m_Mutex);
			if(!m_Condition.wait_for(Lock, FAKE_WORK_TIMEOUT, [&] { return m_AllowCallbackStart; }))
				m_TimedOut = true;
			CQmImeRuntime *pContext = m_pContext;
			m_CallbackEntered = true;
			m_Condition.notify_all();
			if(!m_Condition.wait_for(Lock, FAKE_WORK_TIMEOUT, [&] { return m_AllowCallbackReturn; }))
				m_TimedOut = true;
			// 回归时记录提前释放，避免测试本身解引用已销毁对象。
			if(!m_Destroyed && pContext != nullptr && !m_TimedOut)
			{
				pContext->OnNotification("deploy", "failure");
				m_FailureObserved = pContext->DeploymentFailed();
				m_vCalls.emplace_back("callback");
			}
			m_WorkFinished = true;
			m_Condition.notify_all();
		}
		std::vector<std::string> Calls()
		{
			std::lock_guard Lock(m_Mutex);
			return m_vCalls;
		}
	};

	class CApiRuntime final : public CQmImeRuntime
	{
		std::shared_ptr<SApiState> m_pApi;
		bool m_Initialized;

		void Record(const char *pCall)
		{
			std::lock_guard Lock(m_pApi->m_Mutex);
			m_pApi->m_vCalls.emplace_back(pCall);
		}

	public:
		CApiRuntime(std::shared_ptr<SApiState> pApi, bool Initialized) :
			m_pApi(std::move(pApi)), m_Initialized(Initialized)
		{
			m_pApi->m_pContext = this;
		}
		~CApiRuntime() override
		{
			{
				std::lock_guard Lock(m_pApi->m_Mutex);
				m_pApi->m_Destroyed = true;
				m_pApi->m_pContext = nullptr;
				m_pApi->m_vCalls.emplace_back("destroy_runtime");
			}
			m_pApi->m_Condition.notify_all();
		}
		bool Initialized() const override { return m_Initialized; }
		void JoinMaintenance() override
		{
			std::unique_lock Lock(m_pApi->m_Mutex);
			m_pApi->m_vCalls.emplace_back("join");
			m_pApi->m_JoinEntered = true;
			m_pApi->m_Condition.notify_all();
			if(!m_pApi->m_Condition.wait_for(Lock, FAKE_WORK_TIMEOUT, [&] { return m_pApi->m_WorkFinished; }))
				m_pApi->m_TimedOut = true;
		}
		void DestroySession() override { Record("destroy_session"); }
		void ClearNotificationHandler() override
		{
			std::lock_guard Lock(m_pApi->m_Mutex);
			m_pApi->m_pContext = nullptr;
			m_pApi->m_vCalls.emplace_back("clear_handler");
		}
		void Finalize() override { Record("finalize"); }
		void UnloadLibrary() override { Record("unload"); }
	};

	class QmImeRuntimeShutdown : public ::testing::Test
	{
	protected:
		std::shared_ptr<SApiState> m_pApi = std::make_shared<SApiState>();
		std::unique_ptr<CQmImeRuntimeShutdown> m_pShutdown;
		std::unique_ptr<CQmImeRuntime> m_pRuntime;
		std::shared_future<ECloseResult> m_Completion;
		std::thread m_CallbackThread;
		std::thread m_CloseThread;
		std::promise<void> m_CloseReturned;

		void SetUp() override
		{
			m_pShutdown = CQmImeRuntimeShutdown::Prepare();
			ASSERT_NE(m_pShutdown, nullptr);
			m_Completion = m_pShutdown->Completion();
		}
		void TearDown() override
		{
			// ASSERT 提前结束时也释放外部边界的等待条件，再有界观察生产清理。
			m_pApi->Release(m_CallbackThread.joinable());
			if(m_CloseThread.joinable())
				m_CloseThread.join();
			if(m_pShutdown != nullptr)
				m_pShutdown->Close(std::move(m_pRuntime));
			if(m_CallbackThread.joinable())
				m_CallbackThread.join();
			if(m_Completion.valid())
			{
				const auto Status = m_Completion.wait_for(OBSERVE_TIMEOUT);
				EXPECT_EQ(Status, std::future_status::ready);
				if(Status == std::future_status::ready)
					EXPECT_EQ(m_Completion.get(), ECloseResult::CLOSED);
			}
			std::lock_guard Lock(m_pApi->m_Mutex);
			EXPECT_FALSE(m_pApi->m_TimedOut);
		}
		void MakeRuntime(bool Initialized = true)
		{
			m_pRuntime = std::make_unique<CApiRuntime>(m_pApi, Initialized);
		}
		void StartFailure(bool AllowStart)
		{
			{
				std::lock_guard Lock(m_pApi->m_Mutex);
				m_pApi->m_WorkFinished = false;
				m_pApi->m_AllowCallbackStart = AllowStart;
			}
			m_CallbackThread = std::thread([pApi = m_pApi] { pApi->DeliverFailure(); });
		}
		bool CloseReturnsBeforeMaintenanceFinishes()
		{
			auto Returned = m_CloseReturned.get_future();
			m_CloseThread = std::thread([this] {
				m_pShutdown->Close(std::move(m_pRuntime));
				m_CloseReturned.set_value();
			});
			if(Returned.wait_for(OBSERVE_TIMEOUT) != std::future_status::ready)
				return false;
			m_CloseThread.join();
			return true;
		}
		void ExpectClosed()
		{
			ASSERT_EQ(m_Completion.wait_for(OBSERVE_TIMEOUT), std::future_status::ready);
			EXPECT_EQ(m_Completion.get(), ECloseResult::CLOSED);
		}
		void ExpectRetained()
		{
			std::lock_guard Lock(m_pApi->m_Mutex);
			EXPECT_FALSE(m_pApi->m_Destroyed);
			EXPECT_EQ(m_pApi->m_vCalls, (std::vector<std::string>{"join"}));
		}
	};

	std::thread FailToCreateWorker(std::function<void()>)
	{
		throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
	}

	std::thread ReturnNonJoinableWorker(std::function<void()>)
	{
		return {};
	}

	void ExpectWorkerFailureIsRecoverable(CQmImeRuntimeShutdown::FStartWorker StartWorker)
	{
		auto pFailed = CQmImeRuntimeShutdown::Prepare(StartWorker);
		if(pFailed != nullptr)
		{
			pFailed->Close();
			ASSERT_EQ(pFailed->Completion().wait_for(OBSERVE_TIMEOUT), std::future_status::ready);
		}
		EXPECT_EQ(pFailed, nullptr);
		auto pRetry = CQmImeRuntimeShutdown::Prepare();
		ASSERT_NE(pRetry, nullptr);
		pRetry->Close();
		const auto Completion = pRetry->Completion();
		ASSERT_EQ(Completion.wait_for(OBSERVE_TIMEOUT), std::future_status::ready);
		EXPECT_EQ(Completion.get(), ECloseResult::CLOSED);
	}
}

TEST_F(QmImeRuntimeShutdown, CompletedMaintenanceIsJoinedBeforeNotificationAndLibraryAreReleased)
{
	MakeRuntime();
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	ExpectClosed();
	EXPECT_EQ(m_pApi->Calls(), (std::vector<std::string>{"join", "destroy_session", "clear_handler", "finalize", "unload", "destroy_runtime"}));
}

TEST_F(QmImeRuntimeShutdown, InFlightFailureKeepsContextAliveAfterForegroundOwnerIsDestroyed)
{
	MakeRuntime();
	StartFailure(true);
	ASSERT_TRUE(m_pApi->WaitFor([&] { return m_pApi->m_CallbackEntered; }));
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	ASSERT_TRUE(m_pApi->WaitFor([&] { return m_pApi->m_JoinEntered; }));
	m_pShutdown.reset();
	ExpectRetained();
	m_pApi->Release(true);
	ExpectClosed();
	std::lock_guard Lock(m_pApi->m_Mutex);
	EXPECT_TRUE(m_pApi->m_FailureObserved);
	EXPECT_TRUE(m_pApi->m_Destroyed);
}

TEST_F(QmImeRuntimeShutdown, FailureArrivingAfterCloseRunsBeforeHandlerIsUnregistered)
{
	MakeRuntime();
	StartFailure(false);
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	ASSERT_TRUE(m_pApi->WaitFor([&] { return m_pApi->m_JoinEntered; }));
	m_pShutdown.reset();
	ExpectRetained();
	m_pApi->Release(true);
	ExpectClosed();
	EXPECT_EQ(m_pApi->Calls(), (std::vector<std::string>{"join", "callback", "destroy_session", "clear_handler", "finalize", "unload", "destroy_runtime"}));
	std::lock_guard Lock(m_pApi->m_Mutex);
	EXPECT_TRUE(m_pApi->m_FailureObserved);
}

TEST_F(QmImeRuntimeShutdown, UninitializedRuntimeOnlyUnloadsItsLibrary)
{
	MakeRuntime(false);
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	ExpectClosed();
	EXPECT_EQ(m_pApi->Calls(), (std::vector<std::string>{"unload", "destroy_runtime"}));
}

TEST_F(QmImeRuntimeShutdown, RepeatedCloseDoesNotFinalizeOrUnloadTwice)
{
	MakeRuntime();
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	m_pShutdown->Close();
	m_pShutdown->Close();
	ExpectClosed();
	EXPECT_EQ(m_pApi->Calls(), (std::vector<std::string>{"join", "destroy_session", "clear_handler", "finalize", "unload", "destroy_runtime"}));
}

TEST_F(QmImeRuntimeShutdown, AnotherRuntimeIsRejectedUntilAllCleanupHasFinished)
{
	MakeRuntime();
	{
		std::lock_guard Lock(m_pApi->m_Mutex);
		m_pApi->m_WorkFinished = false;
	}
	ASSERT_TRUE(CloseReturnsBeforeMaintenanceFinishes());
	ASSERT_TRUE(m_pApi->WaitFor([&] { return m_pApi->m_JoinEntered; }));
	EXPECT_EQ(CQmImeRuntimeShutdown::Prepare(), nullptr);
	m_pApi->Release(false);
	ExpectClosed();
	auto pNext = CQmImeRuntimeShutdown::Prepare();
	ASSERT_NE(pNext, nullptr);
	pNext->Close();
	const auto Completion = pNext->Completion();
	ASSERT_EQ(Completion.wait_for(OBSERVE_TIMEOUT), std::future_status::ready);
	EXPECT_EQ(Completion.get(), ECloseResult::CLOSED);
}

TEST_F(QmImeRuntimeShutdown, PreparedWorkerCanCloseWithoutStartingRime)
{
	m_pShutdown->Close();
	ExpectClosed();
	EXPECT_TRUE(m_pApi->Calls().empty());
}

TEST(QmImeRuntimeWorker, ThreadCreationFailureReleasesTheInitializationReservation)
{
	ExpectWorkerFailureIsRecoverable(FailToCreateWorker);
}

TEST(QmImeRuntimeWorker, NonJoinableThreadIsRejectedAndAllowsAnotherPreparation)
{
	ExpectWorkerFailureIsRecoverable(ReturnNonJoinableWorker);
}
