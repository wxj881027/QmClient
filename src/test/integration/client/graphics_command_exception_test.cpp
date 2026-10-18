#include <engine/client/backend/graphics_command_execution.h>
#include <engine/client/backend/vulkan/vulkan_render_workers.h>

#include <gtest/gtest.h>
#include <test/support/threaded_graphics_backend.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <new>

namespace
{
	class CThrowingCommandFragment : public CCommandProcessorFragment_GLBase
	{
		bool GetPresentedImageData(uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) override { return false; }

	public:
		int m_Commands = 0;
		bool m_Fail = true;
		ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *) override
		{
			++m_Commands;
			if(m_Fail)
				throw std::bad_alloc();
			return RUN_COMMAND_COMMAND_HANDLED;
		}
	};

	// 只替代 GPU/窗口边界；命令执行、提交线程、空闲发布与错误消费均调用生产接口。
	class CCommandProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		CCommandProcessorFragment_GLBase &m_Fragment;
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;

	public:
		explicit CCommandProcessor(CCommandProcessorFragment_GLBase &Fragment) :
			m_Fragment(Fragment) {}
		void RunBuffer(CCommandBuffer *pBuffer) noexcept override
		{
			const auto Result = RunGraphicsCommandBuffer(m_Fragment, *pBuffer, [](const auto *) { return true; });
			if(Result == RUN_COMMAND_COMMAND_ERROR)
				m_Error = m_Fragment.TakeError();
			else if(Result == RUN_COMMAND_COMMAND_WARNING)
				m_Warning = m_Fragment.TakeWarning();
		}
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		const SGfxWarningContainer &GetWarning() const override { return m_Warning; }
		void ClearError() override { m_Error = {}; }
		void ClearFatalError() override { ClearError(); }
		void ErroneousCleanup() override {}
	};

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
	class CBorrowingCommandFragment : public CThrowingCommandFragment
	{
		CVulkanRenderWorkers m_Workers;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_AbortEntered = false;
		bool m_Release = false;

		void AbortCommands(bool) noexcept override
		{
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				m_AbortEntered = true;
				m_Condition.notify_all();
			}
			m_Workers.Stop();
		}

	public:
		const CCommandBuffer::SCommand_Texture_Create *m_pUpload = nullptr;
		bool m_TimedOut = false;
		bool m_WorkerSawLiveUpload = false;
		bool m_WorkerStarted = false;
		bool m_WorkerFinished = false;

		void StartCommands(size_t, size_t) override
		{
			m_WorkerStarted = m_Workers.Start(1, [this](size_t) {
				std::unique_lock<std::mutex> Lock(m_Mutex);
				m_TimedOut = !m_Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return m_Release; });
				m_WorkerSawLiveUpload = m_pUpload != nullptr && m_pUpload->m_pData != nullptr;
				m_WorkerFinished = true;
			});
		}
		bool WaitForAbort()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return m_AbortEntered; });
		}
		void ReleaseWorker()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Release = true;
			m_Condition.notify_all();
		}
	};
#endif
}

TEST(GraphicsCommandException, ThreadCompletesAndPublishesTheCaughtFault)
{
	CThrowingCommandFragment Fragment;
	CCommandProcessor Processor(Fragment);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 64);
	ASSERT_TRUE(Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_TRUE(Backend.IsIdle());
	EXPECT_EQ(Fragment.m_Commands, 1);
	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_NE(*Backend.GetFatalError(), '\0');
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Fragment.m_Commands, 1);
}

TEST(GraphicsCommandException, FreshProcessorAfterCaughtFaultAcceptsCommands)
{
	CThrowingCommandFragment Fragment;
	CCommandProcessor Processor(Fragment);
	CThrowingCommandFragment HealthyFragment;
	HealthyFragment.m_Fail = false;
	CCommandProcessor HealthyProcessor(HealthyFragment);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 64);
	ASSERT_TRUE(Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	ASSERT_TRUE(Backend.TakeFatalError());
	Backend.Restart(HealthyProcessor);
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(HealthyFragment.m_Commands, 1);
	EXPECT_FALSE(Backend.TakeFatalError());
}

TEST(GraphicsCommandException, InitializationExceptionReturnsFailureWithoutStoppingCleanup)
{
	CThrowingCommandFragment Fragment;
	CCommandProcessor Processor(Fragment);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(512, 64);
	int InitError = 0;
	CCommandProcessorFragment_GLBase::SCommand_Init Init{};
	Init.m_pInitError = &InitError;
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Init));
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(InitError, -1);
	EXPECT_FALSE(Backend.TakeFatalError());
	Fragment.m_Fail = false;
	Buffer.Reset();
	ASSERT_TRUE(Buffer.AddCommandUnsafe(CCommandProcessorFragment_GLBase::SCommand_Shutdown{}));
	Backend.RunBuffer(&Buffer);
	Backend.WaitForIdle();
	EXPECT_EQ(Fragment.m_Commands, 2);
}

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST(GraphicsCommandException, CompletionAndUploadReleaseWaitForTheBorrowingWorker)
{
	CBorrowingCommandFragment Fragment;
	CCommandProcessor Processor(Fragment);
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(256, 64);
	auto pData = CGraphicsUploadData<uint8_t>(static_cast<uint8_t *>(std::malloc(16)));
	ASSERT_NE(pData, nullptr);
	CCommandBuffer::SCommand_Texture_Create Upload{};
	Upload.m_pData = pData.get();
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Upload));
	pData.release();
	Fragment.m_pUpload = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(Buffer.Head());
	Backend.RunBuffer(&Buffer);
	const bool Aborting = Fragment.WaitForAbort();
	EXPECT_TRUE(Aborting);
	if(Aborting)
	{
		EXPECT_FALSE(Backend.IsIdle());
		EXPECT_FALSE(Backend.TakeFatalError());
		EXPECT_NE(Fragment.m_pUpload->m_pData, nullptr);
	}
	Fragment.ReleaseWorker();
	Backend.WaitForIdle();
	EXPECT_TRUE(Fragment.m_WorkerStarted);
	EXPECT_TRUE(Fragment.m_WorkerFinished);
	EXPECT_FALSE(Fragment.m_TimedOut);
	EXPECT_TRUE(Fragment.m_WorkerSawLiveUpload);
	EXPECT_EQ(Fragment.m_pUpload->m_pData, nullptr);
	EXPECT_TRUE(Backend.TakeFatalError());
}
#endif
