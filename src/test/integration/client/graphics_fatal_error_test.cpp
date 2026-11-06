#include <engine/client/backend_sdl.h>

#include <gtest/gtest.h>
#include <test/support/threaded_graphics_backend.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
	class CFailingGraphicsProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;

	public:
		int m_RunCount = 0;
		int m_ClearCount = 0;

		void RunBuffer(CCommandBuffer *) override
		{
			++m_RunCount;
			m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
			m_Error.m_vErrors = {{false, "vkQueueSubmit (frame) failed."}, {false, "device lost (VkResult -4)"}, {true, "driver advice"}};
		}
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		void ClearError() override { m_Error = {}; }
		void ClearFatalError() override
		{
			++m_ClearCount;
			ClearError();
		}
		void ErroneousCleanup() override {}
		const SGfxWarningContainer &GetWarning() const override { return m_Warning; }
	};

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	class CBlockedGraphicsProcessor : public CFailingGraphicsProcessor
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Released = false;
		bool m_Fail;

	public:
		bool m_TimedOut = false;
		mutable std::atomic<int> m_ErrorReads{0};

		explicit CBlockedGraphicsProcessor(bool Fail) : m_Fail(Fail) {}

		void RunBuffer(CCommandBuffer *pBuffer) override
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			m_Entered = true;
			m_Condition.notify_all();
			// 主线程检查返回后才放行；超时让旧的阻塞实现能够结束并报告回归。
			m_TimedOut = !m_Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return m_Released; });
			Lock.unlock();
			if(m_Fail)
				CFailingGraphicsProcessor::RunBuffer(pBuffer);
			else
				++m_RunCount;
		}

		bool WaitUntilEntered()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return m_Entered; });
		}

		void Release()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Released = true;
			m_Condition.notify_all();
		}

		const SGfxErrorContainer &GetError() const override
		{
			++m_ErrorReads;
			return CFailingGraphicsProcessor::GetError();
		}
	};
#endif
} // namespace

TEST(GraphicsFatalError, ConsumptionBeforeNextSubmissionPreservesBackendDetails)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
	EXPECT_EQ(Processor.m_ClearCount, 1);
	EXPECT_FALSE(Backend.HasFatalError());
}

TEST(GraphicsFatalError, ConsumedFaultStopsFurtherBufferExecution)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	ASSERT_TRUE(Backend.TakeFatalError());

	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Processor.m_RunCount, 1);
}

TEST(GraphicsFatalError, RepeatedConsumptionKeepsTheSavedReport)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	ASSERT_TRUE(Backend.TakeFatalError());

	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_EQ(Processor.m_ClearCount, 1);
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, AlreadyProcessedFaultCanBeConsumedWithoutLosingDetails)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	Backend.ProcessError(Processor.GetError());

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, HealthyProcessorHasNoFaultToConsume)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "");
	EXPECT_EQ(Processor.m_ClearCount, 0);
}

TEST(GraphicsFatalError, LaterFaultDoesNotReplaceTheFirstReport)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	Backend.ProcessError(Processor.GetError());
	SGfxErrorContainer LaterError;
	LaterError.m_ErrorType = GFX_ERROR_TYPE_SWAP_FAILED;
	LaterError.m_vErrors = {{false, "later shutdown fault"}};
	Backend.ProcessError(LaterError);

	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
}

TEST(GraphicsFatalError, RestartedProcessorClearsSavedFaultAndAcceptsNewBuffers)
{
	CFailingGraphicsProcessor Processor;
	CFailingGraphicsProcessor NewProcessor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	ASSERT_TRUE(Backend.TakeFatalError());

	Backend.Restart(NewProcessor);
	EXPECT_STREQ(Backend.GetFatalError(), "");
	EXPECT_FALSE(Backend.TakeFatalError());
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_EQ(NewProcessor.m_RunCount, 1);
}

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST(GraphicsFatalError, PendingHealthyBufferDoesNotBlockFaultPolling)
{
	CBlockedGraphicsProcessor Processor(false);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Processor.WaitUntilEntered());
	const int ErrorReadsBeforePolling = Processor.m_ErrorReads.load();

	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_EQ(Processor.m_ErrorReads.load(), ErrorReadsBeforePolling);
	Processor.Release();
	Backend.WaitForIdle();
	EXPECT_FALSE(Processor.m_TimedOut);
	EXPECT_FALSE(Backend.TakeFatalError());

	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Processor.m_RunCount, 2);
}

TEST(GraphicsFatalError, PendingFaultIsConsumedOnceAfterBufferCompletes)
{
	CBlockedGraphicsProcessor Processor(true);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Processor.WaitUntilEntered());
	const int ErrorReadsBeforePolling = Processor.m_ErrorReads.load();

	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_EQ(Processor.m_ErrorReads.load(), ErrorReadsBeforePolling);
	Processor.Release();
	// 完成后的错误也从轮询入口观察，不靠 WaitForIdle 提供额外同步。
	bool FaultConsumed = false;
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(!(FaultConsumed = Backend.TakeFatalError()) && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	Backend.WaitForIdle();
	EXPECT_FALSE(Processor.m_TimedOut);
	ASSERT_TRUE(FaultConsumed);
	EXPECT_STREQ(Backend.GetFatalError(), "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\ntranslated driver advice");
	EXPECT_FALSE(Backend.TakeFatalError());
	EXPECT_EQ(Processor.m_ClearCount, 1);

	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Processor.m_RunCount, 1);
}
#endif
