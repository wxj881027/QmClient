#include <base/logger.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <new>
#include <utility>
#include <vector>

namespace
{
	class CDispatchLogger : public ILogger
	{
	public:
		int m_ThrowNext = 0;
		bool m_Reenter = false;
		int m_Calls = 0;
		std::vector<CLogMessage> m_vMessages;

		void Log(const CLogMessage *pMessage) override
		{
			++m_Calls;
			if(m_Reenter)
				log_info("dispatch-test", "recursive");
			const int Failure = std::exchange(m_ThrowNext, 0);
			if(Failure == 1)
				throw std::bad_alloc();
			if(Failure == 2)
				throw 7;
			m_vMessages.push_back(*pMessage);
		}
	};

	void WriteArguments(const char *pFormat, ...)
	{
		va_list Args;
		va_start(Args, pFormat);
		try
		{
			log_log_v(LEVEL_WARN, "dispatch-test", pFormat, Args);
		}
		catch(...)
		{
			va_end(Args);
			throw;
		}
		va_end(Args);
	}

	void WriteColorArguments(const char *pFormat, ...)
	{
		va_list Args;
		va_start(Args, pFormat);
		try
		{
			log_log_color_v(LEVEL_ERROR, LOG_COLOR{11, 22, 33}, "dispatch-test", pFormat, Args);
		}
		catch(...)
		{
			va_end(Args);
			throw;
		}
		va_end(Args);
	}
}

TEST(LogDispatch, BackendExceptionDoesNotEscapeOrSuppressNextMessage)
{
	// 每项使用独立线程，旧递归标记缺陷不能污染其他测试的日志状态。
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Logger;
		CLogScope Scope(&Logger);
		Logger.m_ThrowNext = 1;
		EXPECT_NO_THROW(log_info("dispatch-test", "failed %d", 1));
		log_info("dispatch-test", "recovered %d", 2);
		EXPECT_EQ(Logger.m_Calls, 2);
		ASSERT_EQ(Logger.m_vMessages.size(), 1U);
		EXPECT_STREQ(Logger.m_vMessages[0].Message(), "recovered 2");
	}));
}

TEST(LogDispatch, UnknownBackendExceptionAllowsNewScopeToLog)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Failed;
		CDispatchLogger Recovered;
		{
			CLogScope Scope(&Failed);
			Failed.m_ThrowNext = 2;
			EXPECT_NO_THROW(log_error("dispatch-test", "unknown failure"));
		}
		CLogScope Scope(&Recovered);
		log_info("dispatch-test", "new scope");
		EXPECT_EQ(Failed.m_Calls, 1);
		ASSERT_EQ(Recovered.m_vMessages.size(), 1U);
		EXPECT_STREQ(Recovered.m_vMessages[0].Message(), "new scope");
	}));
}

TEST(LogDispatch, RecursiveMessageIsSuppressedOnlyWhileBackendRuns)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Logger;
		Logger.m_Reenter = true;
		CLogScope Scope(&Logger);
		log_info("dispatch-test", "first");
		log_info("dispatch-test", "second");
		EXPECT_EQ(Logger.m_Calls, 2);
		ASSERT_EQ(Logger.m_vMessages.size(), 2U);
		EXPECT_STREQ(Logger.m_vMessages[0].Message(), "first");
		EXPECT_STREQ(Logger.m_vMessages[1].Message(), "second");
	}));
}

TEST(LogDispatch, VariadicArgumentsStillFormatAfterFailedWrite)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Logger;
		CLogScope Scope(&Logger);
		Logger.m_ThrowNext = 1;
		EXPECT_NO_THROW(WriteArguments("failed %s %d", "value", 3));
		WriteArguments("saved %s %d", "value", 4);
		EXPECT_EQ(Logger.m_Calls, 2);
		ASSERT_EQ(Logger.m_vMessages.size(), 1U);
		EXPECT_EQ(Logger.m_vMessages[0].m_Level, LEVEL_WARN);
		EXPECT_STREQ(Logger.m_vMessages[0].Message(), "saved value 4");
	}));
}

TEST(LogDispatch, ColoredMessagesRetainMetadataAfterFailure)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Logger;
		CLogScope Scope(&Logger);
		Logger.m_ThrowNext = 1;
		const LOG_COLOR Color{41, 42, 43};
		EXPECT_NO_THROW(log_log_color(LEVEL_INFO, Color, "dispatch-test", "failed"));
		log_log_color(LEVEL_INFO, Color, "dispatch-test", "saved %d", 5);
		EXPECT_EQ(Logger.m_Calls, 2);
		ASSERT_EQ(Logger.m_vMessages.size(), 1U);
		const auto &Message = Logger.m_vMessages[0];
		EXPECT_TRUE(Message.m_HaveColor);
		EXPECT_EQ(Message.m_Color.r, 41);
		EXPECT_EQ(Message.m_Color.g, 42);
		EXPECT_EQ(Message.m_Color.b, 43);
		EXPECT_STREQ(Message.Message(), "saved 5");
	}));
}

TEST(LogDispatch, ColoredVariadicMessageFailureDoesNotEscape)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		CDispatchLogger Logger;
		CLogScope Scope(&Logger);
		Logger.m_ThrowNext = 2;
		EXPECT_NO_THROW(WriteColorArguments("failed %d", 6));
		WriteColorArguments("saved %d", 7);
		EXPECT_EQ(Logger.m_Calls, 2);
		ASSERT_EQ(Logger.m_vMessages.size(), 1U);
		const auto &Message = Logger.m_vMessages[0];
		EXPECT_EQ(Message.m_Level, LEVEL_ERROR);
		EXPECT_TRUE(Message.m_HaveColor);
		EXPECT_EQ(Message.m_Color.r, 11);
		EXPECT_EQ(Message.m_Color.g, 22);
		EXPECT_EQ(Message.m_Color.b, 33);
		EXPECT_STREQ(Message.Message(), "saved 7");
	}));
}
