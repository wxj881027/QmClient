#include <base/logger_async.h>
#include <base/system.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace
{
#if defined(CONF_FAMILY_WINDOWS)
	const char *s_pNewline = "\r\n";
#else
	const char *s_pNewline = "\n";
#endif

	CLogMessage Message(const char *pLine)
	{
		CLogMessage Msg{};
		Msg.m_Level = LEVEL_INFO;
		str_copy(Msg.m_aLine, pLine);
		Msg.m_LineLength = str_length(Msg.m_aLine);
		return Msg;
	}

	ASYNCIO *FailCreate(IOHANDLE)
	{
		return nullptr;
	}
}

class AsyncLogger : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	std::string m_Path;
	IOHANDLE m_File = nullptr;

	void SetUp() override
	{
		m_Path = std::string("tmp/tests/") + m_Info.m_aFilename;
		ASSERT_EQ(fs_makedir_rec_for(m_Path.c_str()), 0);
		m_File = io_open(m_Path.c_str(), IOFLAG_WRITE);
		ASSERT_NE(m_File, nullptr);
	}

	void TearDown() override
	{
		if(m_File)
			io_close(m_File);
		if(!HasFailure() && !m_Path.empty())
			EXPECT_EQ(fs_remove(m_Path.c_str()), 0);
	}

	IOHANDLE TakeFile() { return std::exchange(m_File, nullptr); }

	void Expect(const std::string &Expected)
	{
		IOHANDLE File = io_open(m_Path.c_str(), IOFLAG_READ);
		ASSERT_NE(File, nullptr);
		char aBuffer[512];
		const unsigned Size = io_read(File, aBuffer, sizeof(aBuffer));
		io_close(File);
		EXPECT_EQ(std::string(aBuffer, Size), Expected);
	}
};

TEST_F(AsyncLogger, CreationFailureAcceptsLogAndRepeatedFinishWithoutAio)
{
	const auto Msg = Message("unavailable");
	{
		CLoggerAsync Logger(TakeFile(), false, true, FailCreate);
		Logger.Log(&Msg);
		Logger.GlobalFinish();
		Logger.GlobalFinish();
	}
	Expect("");
	// Windows 上仍打开的文件无法删除；也避免用已关闭的 FILE 指针验证归属。
	ASSERT_EQ(fs_remove(m_Path.c_str()), 0);
	m_Path.clear();
}

TEST_F(AsyncLogger, CreationFailureKeepsBorrowedFileOpen)
{
	const auto Msg = Message("unavailable");
	{
		CLoggerAsync Logger(m_File, false, false, FailCreate);
		Logger.Log(&Msg);
		Logger.GlobalFinish();
	}
	EXPECT_EQ(io_write(m_File, "owner", 5), 5u);
	io_close(TakeFile());
	Expect("owner");
}

TEST_F(AsyncLogger, DestructorDrainsQueuedMessage)
{
	auto pLogger = std::make_unique<CLoggerAsync>(TakeFile(), false, true);
	const auto Msg = Message("queued");
	pLogger->Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger)]() mutable { pLogger.reset(); }));
	Expect(std::string("queued") + s_pNewline);
}

TEST_F(AsyncLogger, RepeatedFinishDrainsOnceAndIgnoresLaterMessages)
{
	auto pLogger = std::make_unique<CLoggerAsync>(TakeFile(), false, true);
	const auto Accepted = Message("accepted");
	const auto Late = Message("late");
	pLogger->Log(&Accepted);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger), Late]() mutable {
		pLogger->GlobalFinish();
		pLogger->Log(&Late);
		pLogger->GlobalFinish();
		pLogger.reset();
	}));
	Expect(std::string("accepted") + s_pNewline);
}

TEST_F(AsyncLogger, SharedRouterCanFinishTheSameBackendTwice)
{
	auto pLogger = std::make_shared<CLoggerAsync>(TakeFile(), false, true);
	auto pRouter = log_logger_prefix_router(pLogger, pLogger, "prefix");
	pLogger.reset();
	const auto Msg = Message("routed");
	pRouter->Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pRouter = std::move(pRouter)]() mutable {
		pRouter->GlobalFinish();
		pRouter.reset();
	}));
	Expect(std::string("routed") + s_pNewline);
}

TEST_F(AsyncLogger, ConcurrentFinishCallsShareTheDrain)
{
	auto pLogger = std::make_shared<CLoggerAsync>(TakeFile(), false, true);
	const auto Msg = Message("concurrent");
	pLogger->Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger)] {
		std::thread First([pLogger] { pLogger->GlobalFinish(); });
		std::thread Second([pLogger] { pLogger->GlobalFinish(); });
		First.join();
		Second.join();
	}));
	Expect(std::string("concurrent") + s_pNewline);
}

TEST_F(AsyncLogger, BorrowedFileRemainsWritableAfterDrain)
{
	const IOHANDLE File = TakeFile();
	auto pLogger = std::make_unique<CLoggerAsync>(File, false, false);
	const auto Msg = Message("logger");
	pLogger->Log(&Msg);
	auto pWritten = std::make_shared<unsigned>(0);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger), File, pWritten]() mutable {
		pLogger->GlobalFinish();
		pLogger.reset();
		*pWritten = io_write(File, "owner", 5);
		io_close(File);
	}));
	EXPECT_EQ(*pWritten, 5u);
	Expect(std::string("logger") + s_pNewline + "owner");
}

TEST_F(AsyncLogger, FilterDropsExcludedMessages)
{
	auto pLogger = std::make_unique<CLoggerAsync>(TakeFile(), false, true);
	CLogFilter Filter;
	Filter.m_MaxLevel.store(LEVEL_ERROR, std::memory_order_relaxed);
	pLogger->SetFilter(Filter);
	auto Msg = Message("filtered");
	pLogger->Log(&Msg);
	Msg = Message("kept");
	Msg.m_Level = LEVEL_ERROR;
	pLogger->Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger)]() mutable { pLogger.reset(); }));
	Expect(std::string("kept") + s_pNewline);
}

TEST_F(AsyncLogger, ColorOutputKeepsEscapesAndNewline)
{
	auto pLogger = std::make_unique<CLoggerAsync>(TakeFile(), true, true);
	auto Msg = Message("color");
	Msg.m_HaveColor = true;
	Msg.m_Color = {1, 2, 3};
	pLogger->Log(&Msg);
	ASSERT_TRUE(WaitForAsyncIo([pLogger = std::move(pLogger)]() mutable { pLogger.reset(); }));
	Expect(std::string("\x1b[38;2;1;2;3mcolor\x1b[0m") + s_pNewline);
}

TEST_F(AsyncLogger, FutureBindingTransfersStartupAndLiveMessages)
{
	auto pFuture = std::make_unique<CFutureLogger>();
	const auto Startup = Message("startup");
	pFuture->Log(&Startup);
	pFuture->Set(std::make_shared<CLoggerAsync>(TakeFile(), false, true));
	const auto Live = Message("live");
	pFuture->Log(&Live);
	ASSERT_TRUE(WaitForAsyncIo([pFuture = std::move(pFuture)]() mutable { pFuture.reset(); }));
	Expect(std::string("startup") + s_pNewline + "live" + s_pNewline);
}

TEST_F(AsyncLogger, FutureBindingSurvivesUnavailableBorrowedBackend)
{
	{
		CFutureLogger Future;
		const auto Startup = Message("startup");
		Future.Log(&Startup);
		Future.Set(std::make_shared<CLoggerAsync>(m_File, false, false, FailCreate));
		const auto Live = Message("live");
		Future.Log(&Live);
		Future.GlobalFinish();
	}
	EXPECT_EQ(io_write(m_File, "owner", 5), 5u);
	io_close(TakeFile());
	Expect("owner");
}
