#include <base/logger.h>
#include <base/str.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	CLogMessage Message(const char *pLine, LEVEL Level = LEVEL_INFO)
	{
		CLogMessage Msg{};
		Msg.m_Level = Level;
		str_copy(Msg.m_aLine, pLine);
		Msg.m_LineLength = str_length(Msg.m_aLine);
		return Msg;
	}

	class CRecordingLogger : public ILogger
	{
		std::mutex m_Mutex;
		std::vector<std::string> m_vLines;

	public:
		std::atomic<int> m_FinishCount{0};

		void Log(const CLogMessage *pMessage) override
		{
			if(m_Filter.Filters(pMessage))
				return;
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			m_vLines.emplace_back(pMessage->m_aLine, pMessage->m_LineLength);
		}

		void GlobalFinish() override { ++m_FinishCount; }

		std::vector<std::string> Lines()
		{
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			return m_vLines;
		}
	};

	class CReentrantLogger : public CRecordingLogger
	{
		CFutureLogger &m_Future;
		bool m_Reentered = false;

	public:
		explicit CReentrantLogger(CFutureLogger &Future) :
			m_Future(Future) {}

		void Log(const CLogMessage *pMessage) override
		{
			if(!m_Reentered)
			{
				m_Reentered = true;
				const auto Msg = Message("reentrant");
				m_Future.Log(&Msg);
			}
			CRecordingLogger::Log(pMessage);
		}
	};
}

TEST(FutureLogger, BindingTransfersQueuedMessagesInOrder)
{
	CFutureLogger Future;
	const auto First = Message("first");
	const auto Second = Message("second");
	Future.Log(&First);
	Future.Log(&Second);
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	EXPECT_EQ(pBackend->Lines(), (std::vector<std::string>{"first", "second"}));
}

TEST(FutureLogger, QueueOwnsMessageValuesUntilBinding)
{
	CFutureLogger Future;
	auto Msg = Message("original");
	Future.Log(&Msg);
	Msg = Message("changed");
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	EXPECT_EQ(pBackend->Lines(), (std::vector<std::string>{"original"}));
}

TEST(FutureLogger, BoundMessagesAreForwardedExactlyOnce)
{
	CFutureLogger Future;
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	const auto First = Message("first");
	const auto Second = Message("second");
	Future.Log(&First);
	Future.Log(&Second);
	EXPECT_EQ(pBackend->Lines(), (std::vector<std::string>{"first", "second"}));
}

TEST(FutureLogger, PublishedBackendOutlivesTheBindingCaller)
{
	std::weak_ptr<CRecordingLogger> Weak;
	{
		CFutureLogger Future;
		auto pBackend = std::make_shared<CRecordingLogger>();
		Weak = pBackend;
		Future.Set(pBackend);
		pBackend.reset();
		const auto Msg = Message("owned");
		Future.Log(&Msg);
		ASSERT_FALSE(Weak.expired());
		EXPECT_EQ(Weak.lock()->Lines(), (std::vector<std::string>{"owned"}));
	}
	EXPECT_TRUE(Weak.expired());
}

TEST(FutureLogger, FilterBeforeBindingDoesNotConfigureTheBackend)
{
	CFutureLogger Future;
	CLogFilter Filter;
	Filter.m_MaxLevel.store(LEVEL_ERROR);
	Future.SetFilter(Filter);
	const auto Msg = Message("pending info");
	Future.Log(&Msg);
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	EXPECT_EQ(pBackend->Lines(), (std::vector<std::string>{"pending info"}));
}

TEST(FutureLogger, FilterAfterBindingReachesTheBackend)
{
	CFutureLogger Future;
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	CLogFilter Filter;
	Filter.m_MaxLevel.store(LEVEL_ERROR);
	Future.SetFilter(Filter);
	const auto Filtered = Message("filtered");
	const auto Kept = Message("kept", LEVEL_ERROR);
	Future.Log(&Filtered);
	Future.Log(&Kept);
	EXPECT_EQ(pBackend->Lines(), (std::vector<std::string>{"kept"}));
}

TEST(FutureLogger, FinishReachesThePublishedBackend)
{
	CFutureLogger Future;
	auto pBackend = std::make_shared<CRecordingLogger>();
	Future.Set(pBackend);
	Future.GlobalFinish();
	EXPECT_EQ(pBackend->m_FinishCount.load(), 1);
}

TEST(FutureLogger, PendingTransferCanReenterThePublishedBackend)
{
	struct SState
	{
		CFutureLogger m_Future;
		std::shared_ptr<CReentrantLogger> m_pBackend = std::make_shared<CReentrantLogger>(m_Future);
	};
	auto pState = std::make_shared<SState>();
	const auto Msg = Message("pending");
	pState->m_Future.Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pState] { pState->m_Future.Set(pState->m_pBackend); }));
	EXPECT_EQ(pState->m_pBackend->Lines(), (std::vector<std::string>{"reentrant", "pending"}));
}

TEST(FutureLogger, ConcurrentBindingAndWritersPreserveEveryMessage)
{
	struct SState
	{
		std::array<CFutureLogger, 64> m_aFutures;
		std::array<std::shared_ptr<CRecordingLogger>, 64> m_apBackends;
		std::atomic<bool> m_StartTimedOut{false};
	};
	auto pState = std::make_shared<SState>();
	for(auto &pBackend : pState->m_apBackends)
		pBackend = std::make_shared<CRecordingLogger>();
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		std::promise<void> Start;
		auto Ready = Start.get_future().share();
		std::vector<std::thread> vWorkers;
		for(int Writer = 0; Writer < 4; ++Writer)
		{
			vWorkers.emplace_back([pState, Ready, Writer] {
				if(Ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
				{
					pState->m_StartTimedOut = true;
					return;
				}
				const auto Msg = Message(std::to_string(Writer).c_str());
				for(auto &Future : pState->m_aFutures)
					Future.Log(&Msg);
			});
		}
		Start.set_value();
		for(size_t i = 0; i < pState->m_aFutures.size(); ++i)
			pState->m_aFutures[i].Set(pState->m_apBackends[i]);
		for(auto &Worker : vWorkers)
			Worker.join();
	}));
	ASSERT_FALSE(pState->m_StartTimedOut.load());
	for(size_t i = 0; i < pState->m_apBackends.size(); ++i)
	{
		SCOPED_TRACE(i);
		auto vLines = pState->m_apBackends[i]->Lines();
		std::sort(vLines.begin(), vLines.end());
		EXPECT_EQ(vLines, (std::vector<std::string>{"0", "1", "2", "3"}));
	}
}
