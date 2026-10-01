// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/logger.h>
#include <base/system.h>

#include <engine/client/perf_file_logger.h>
#include <engine/shared/jobs.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

TEST(Logger, PrefixFileLoggerWritesOnlyMatchingSystems)
{
	CTestInfo Info;

	{
		IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
		ASSERT_TRUE(File);
		std::unique_ptr<ILogger> pLogger = log_logger_prefix_file(File, "perf/");
		ASSERT_TRUE(pLogger);

		CLogMessage PerfMessage;
		PerfMessage.m_Level = LEVEL_INFO;
		str_copy(PerfMessage.m_aSystem, "perf/settings-resource");
		str_copy(PerfMessage.m_aLine, "perf line");
		PerfMessage.m_LineLength = str_length(PerfMessage.m_aLine);

		CLogMessage MenuMessage = PerfMessage;
		str_copy(MenuMessage.m_aSystem, "menu");
		str_copy(MenuMessage.m_aLine, "menu line");
		MenuMessage.m_LineLength = str_length(MenuMessage.m_aLine);

		pLogger->Log(&PerfMessage);
		pLogger->Log(&MenuMessage);
	}

	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(File);
	char *pOutput = io_read_all_str(File);
	io_close(File);
	ASSERT_TRUE(pOutput);
	EXPECT_NE(str_find(pOutput, "perf line"), nullptr);
	EXPECT_EQ(str_find(pOutput, "menu line"), nullptr);
	free(pOutput);
	fs_remove(Info.m_aFilename);
}

TEST(Logger, PrefixRouterKeepsMatchingSystemsOutOfFallbackLogger)
{
	std::shared_ptr<CMemoryLogger> pPerfLogger = std::make_shared<CMemoryLogger>();
	std::shared_ptr<CMemoryLogger> pFallbackLogger = std::make_shared<CMemoryLogger>();
	std::unique_ptr<ILogger> pRouter = log_logger_prefix_router(pPerfLogger, pFallbackLogger, "perf/");

	CLogMessage PerfMessage;
	PerfMessage.m_Level = LEVEL_INFO;
	str_copy(PerfMessage.m_aSystem, "perf/settings-resource");
	str_copy(PerfMessage.m_aLine, "perf line");
	PerfMessage.m_LineLength = str_length(PerfMessage.m_aLine);

	CLogMessage MenuMessage = PerfMessage;
	str_copy(MenuMessage.m_aSystem, "menu");
	str_copy(MenuMessage.m_aLine, "menu line");
	MenuMessage.m_LineLength = str_length(MenuMessage.m_aLine);

	pRouter->Log(&PerfMessage);
	pRouter->Log(&MenuMessage);

	const std::vector<CLogMessage> vPerfLines = pPerfLogger->Lines();
	const std::vector<CLogMessage> vFallbackLines = pFallbackLogger->Lines();
	ASSERT_EQ(vPerfLines.size(), 1);
	ASSERT_EQ(vFallbackLines.size(), 1);
	EXPECT_STREQ(vPerfLines[0].m_aSystem, "perf/settings-resource");
	EXPECT_STREQ(vFallbackLines[0].m_aSystem, "menu");
}

TEST(Logger, PrefixRouterDoesNotQueueFallbackMessagesInUnresolvedPrefixLogger)
{
	std::shared_ptr<CFutureLogger> pPerfLogger = std::make_shared<CFutureLogger>();
	std::shared_ptr<CMemoryLogger> pFallbackLogger = std::make_shared<CMemoryLogger>();
	std::unique_ptr<ILogger> pRouter = log_logger_prefix_router(pPerfLogger, pFallbackLogger, "perf/");

	CLogMessage MenuMessage;
	MenuMessage.m_Level = LEVEL_INFO;
	str_copy(MenuMessage.m_aSystem, "menu");
	str_copy(MenuMessage.m_aLine, "menu line");
	MenuMessage.m_LineLength = str_length(MenuMessage.m_aLine);
	pRouter->Log(&MenuMessage);

	std::shared_ptr<CMemoryLogger> pResolvedPerfLogger = std::make_shared<CMemoryLogger>();
	pPerfLogger->Set(pResolvedPerfLogger);

	EXPECT_TRUE(pResolvedPerfLogger->Lines().empty());
	ASSERT_EQ(pFallbackLogger->Lines().size(), 1);
	EXPECT_STREQ(pFallbackLogger->Lines()[0].m_aSystem, "menu");
}

namespace
{
	struct SLoggerCloseGate
	{
		std::mutex m_Mutex;
		std::condition_variable m_Cv;
		bool m_Closing = false;
		bool m_Release = false;
	};

	class CBlockingCloseLogger : public ILogger
	{
		SLoggerCloseGate &m_Gate;

	public:
		explicit CBlockingCloseLogger(SLoggerCloseGate &Gate) :
			m_Gate(Gate) {}
		void Log(const CLogMessage *) override {}
		~CBlockingCloseLogger() override
		{
			std::unique_lock<std::mutex> Lock(m_Gate.m_Mutex);
			m_Gate.m_Closing = true;
			m_Gate.m_Cv.notify_all();
			m_Gate.m_Cv.wait(Lock, [&]() { return m_Gate.m_Release; });
		}
	};
}

TEST(Logger, PerfSwitchRemainsWritableWhilePreviousFileCloses)
{
	SLoggerCloseGate Gate;
	CQmPerfFileSwitchLogger Switch;
	Switch.Set(std::make_shared<CBlockingCloseLogger>(Gate));
	auto pNext = std::make_shared<CMemoryLogger>();
	auto pClose = Switch.SetAsync(pNext);
	ASSERT_TRUE(pClose);
	EXPECT_FALSE(pClose->IsAbortable());
	EXPECT_FALSE(Gate.m_Closing);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pClose);
	{
		std::unique_lock<std::mutex> Lock(Gate.m_Mutex);
		EXPECT_TRUE(Gate.m_Cv.wait_for(Lock, std::chrono::seconds(1), [&]() { return Gate.m_Closing; }));
	}
	CLogMessage Message{};
	Message.m_Level = LEVEL_INFO;
	str_copy(Message.m_aSystem, "perf/session");
	str_copy(Message.m_aLine, "new session");
	Message.m_LineLength = str_length(Message.m_aLine);
	std::atomic<bool> Logged{false};
	std::thread Writer([&]() {
		Switch.Log(&Message);
		Logged.store(true);
	});
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while(!Logged.load() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	EXPECT_TRUE(Logged.load());
	// 即使断言失败，也先释放关闭任务，再回收所有线程。
	{
		std::lock_guard<std::mutex> Lock(Gate.m_Mutex);
		Gate.m_Release = true;
	}
	Gate.m_Cv.notify_all();
	Writer.join();
	Switch.FinishPending();
	Pool.Shutdown();
	ASSERT_EQ(pNext->Lines().size(), 1u);
	EXPECT_STREQ(pNext->Lines()[0].m_aLine, "new session");
}

TEST(Logger, PerfSwitchShutdownDrainsQueuedCloseWithoutWaitingForWorkerDispatch)
{
	CTestInfo Info;
	CQmPerfFileSwitchLogger Switch;
	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);
	Switch.Set(log_logger_prefix_file(File, "perf/"));
	CLogMessage Message{};
	Message.m_Level = LEVEL_INFO;
	str_copy(Message.m_aSystem, "perf/session");
	str_copy(Message.m_aLine, "last session record");
	Message.m_LineLength = str_length(Message.m_aLine);
	Switch.Log(&Message);
	auto pClose = Switch.SetAsync(log_logger_noop());
	Switch.Log(&Message);
	// 任务尚未派发时退出也必须写完，之后派发该任务不能再次关闭文件。
	Switch.GlobalFinish();
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pClose);
	Pool.Shutdown();
	File = io_open(Info.m_aFilename, IOFLAG_READ);
	ASSERT_TRUE(File);
	char *pOutput = io_read_all_str(File);
	io_close(File);
	ASSERT_NE(pOutput, nullptr);
	const char *pMatch = str_find(pOutput, "last session record");
	ASSERT_NE(pMatch, nullptr);
	EXPECT_EQ(str_find(pMatch + 1, "last session record"), nullptr);
	free(pOutput);
	fs_remove(Info.m_aFilename);
}
