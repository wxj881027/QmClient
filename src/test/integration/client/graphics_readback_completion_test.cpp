#include <engine/client/backend/graphics_command_execution.h>
#include <engine/client/render_target_readback_queue.h>

#include <gtest/gtest.h>
#include <test/support/threaded_graphics_backend.h>

#include <cstdlib>
#include <new>

namespace
{
	using SAllocation = CRenderTargetReadbackQueue::SAllocation;
	using EState = IGraphics::ERenderTargetReadbackState;
	// 只替代 GPU 的同步写入或延迟持有；命令异常、拒收和完成发布均由生产实现负责。
	class CReadbackFragment : public CCommandProcessorFragment_GLBase
	{
		CGraphicsReadbackCompletion m_Deferred;
		bool GetPresentedImageData(uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) override { return false; }
		void AbortCommands(bool) noexcept override { ++m_CpuAborts; }
		static void WriteImage(CImageInfo &Image)
		{
			Image.m_pData = static_cast<uint8_t *>(std::malloc(4));
			if(!Image.m_pData)
				throw std::bad_alloc();
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData[0] = 71;
		}

	public:
		bool m_Defer = true;
		bool m_ThrowStart = false;
		bool m_ThrowClear = false;
		bool m_ThrowEnd = false;
		bool m_FailClear = false;
		bool m_WarnClear = false;
		int m_Readbacks = 0;
		int m_CpuAborts = 0;
		~CReadbackFragment() override { AbortReadbacks(); }
		void Fail() { m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED; }
		void Clear() { m_Error = {}; }
		void StartCommands(size_t, size_t) override
		{
			if(m_ThrowStart)
				throw std::bad_alloc();
		}
		void EndCommands() override
		{
			if(m_ThrowEnd)
				throw std::bad_alloc();
		}
		void AbortReadbacks() noexcept override { m_Deferred.Abort(); }
		ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *pCommand) override
		{
			if(pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TARGET_READBACK)
			{
				++m_Readbacks;
				const auto *pReadback = static_cast<const CCommandBuffer::SCommand_RenderTarget_Readback *>(pCommand);
				if(m_Defer)
					m_Deferred = CGraphicsReadbackCompletion(pReadback->m_pImage, std::exchange(pReadback->m_pCompletion, nullptr));
				else
					WriteImage(*pReadback->m_pImage);
			}
			else if(pCommand->m_Cmd == CCommandBuffer::CMD_RENDER_TARGET_READBACK_FLUSH)
			{
				if(m_Deferred)
				{
					WriteImage(*m_Deferred.Image());
					m_Deferred.Complete();
				}
			}
			else if(pCommand->m_Cmd == CCommandBuffer::CMD_CLEAR)
			{
				if(m_ThrowClear)
					throw std::bad_alloc();
				if(m_FailClear)
				{
					Fail();
					return RUN_COMMAND_COMMAND_ERROR;
				}
				if(m_WarnClear)
				{
					m_Warning.m_WarningType = GFX_WARNING_LOW_ON_MEMORY;
					return RUN_COMMAND_COMMAND_WARNING;
				}
			}
			return RUN_COMMAND_COMMAND_HANDLED;
		}
	};
	class CReadbackProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		CReadbackFragment &m_Fragment;

	public:
		int m_Runs = 0;
		explicit CReadbackProcessor(CReadbackFragment &Fragment) :
			m_Fragment(Fragment) {}
		void RunBuffer(CCommandBuffer *pBuffer) noexcept override
		{
			++m_Runs;
			RunGraphicsCommandBuffer(m_Fragment, *pBuffer, [](const auto *) { return true; });
		}
		const SGfxErrorContainer &GetError() const override { return m_Fragment.GetError(); }
		const SGfxWarningContainer &GetWarning() const override { return m_Fragment.GetWarning(); }
		void ClearError() override { m_Fragment.Clear(); }
		void ClearFatalError() override { ClearError(); }
		void ErroneousCleanup() override {}
	};
	class CGraphicsReadbackFlow : public testing::Test
	{
	protected:
		CRenderTargetReadbackQueue m_Queue;
		CReadbackFragment m_Fragment;
		CCommandBuffer m_Buffer{1024, 64};
		SAllocation AddReadback()
		{
			const auto Allocation = m_Queue.Allocate();
			if(Allocation.m_Index < 0)
				throw std::bad_alloc();
			CCommandBuffer::SCommand_RenderTarget_Readback Command;
			Command.m_TargetId = 0;
			Command.m_pImage = Allocation.m_pImage;
			Command.m_pCompletion = Allocation.m_pCompletion;
			if(!m_Buffer.AddCommandUnsafe(Command))
				throw std::bad_alloc();
			return Allocation;
		}
		ERunCommandReturnTypes Run()
		{
			return RunGraphicsCommandBuffer(m_Fragment, m_Buffer, [](const auto *) { return true; });
		}
		void ExpectFailed(const SAllocation &Allocation)
		{
			EXPECT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), EState::FAILED);
			CImageInfo Image;
			EXPECT_FALSE(m_Queue.Resolve(Allocation.m_Index, Allocation.m_Generation, Image));
			EXPECT_EQ(Image.m_pData, nullptr);
			Image.Free();
		}
		void DeferOne(SAllocation &Allocation)
		{
			Allocation = AddReadback();
			ASSERT_EQ(Run(), RUN_COMMAND_COMMAND_HANDLED);
			EXPECT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), EState::PENDING);
			m_Buffer.Reset();
		}
	};
}

TEST_F(CGraphicsReadbackFlow, DeferredRequestStaysPendingAcrossBufferReuseUntilGpuCompletion)
{
	SAllocation Allocation;
	ASSERT_NO_FATAL_FAILURE(DeferOne(Allocation));
	EXPECT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), EState::PENDING);
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand_RenderTarget_ReadbackFlush{}));
	ASSERT_EQ(Run(), RUN_COMMAND_COMMAND_HANDLED);
	ASSERT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), EState::READY);
	CImageInfo Image;
	ASSERT_TRUE(m_Queue.Resolve(Allocation.m_Index, Allocation.m_Generation, Image));
	EXPECT_EQ(Image.m_pData[0], 71);
	Image.Free();
}

TEST_F(CGraphicsReadbackFlow, SynchronousBackendPublishesAtCommandBufferCompletionAndDrainsSignalBeforeReuse)
{
	m_Fragment.m_Defer = false;
	const auto Allocation = AddReadback();
	ASSERT_EQ(Run(), RUN_COMMAND_COMMAND_HANDLED);
	CImageInfo Image;
	ASSERT_TRUE(m_Queue.ResolveAfterIdle(Allocation.m_Index, Allocation.m_Generation, Image));
	EXPECT_EQ(Image.m_pData[0], 71);
	Image.Free();
	const auto Next = m_Queue.Allocate();
	EXPECT_EQ(Next.m_Index, Allocation.m_Index);
	EXPECT_EQ(m_Queue.Poll(Next.m_Index, Next.m_Generation), EState::PENDING);
}

TEST_F(CGraphicsReadbackFlow, ExceptionBeforeTheReadbackCompletesTheSkippedRequest)
{
	m_Fragment.m_ThrowClear = true;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	const auto Allocation = AddReadback();
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Readbacks, 0);
	ExpectFailed(Allocation);
}

TEST_F(CGraphicsReadbackFlow, StartExceptionCompletesRequestsThatNeverReachedTheGpu)
{
	m_Fragment.m_ThrowStart = true;
	const auto Allocation = AddReadback();
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Readbacks, 0);
	ExpectFailed(Allocation);
}

TEST_F(CGraphicsReadbackFlow, EndExceptionAbortsDeferredOutputFromAnEarlierBuffer)
{
	SAllocation Allocation;
	ASSERT_NO_FATAL_FAILURE(DeferOne(Allocation));
	m_Fragment.m_ThrowEnd = true;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_CpuAborts, 1);
	ExpectFailed(Allocation);
}

TEST_F(CGraphicsReadbackFlow, ReportedErrorAbortsDeferredOutputAfterNormalCpuDrain)
{
	SAllocation Allocation;
	ASSERT_NO_FATAL_FAILURE(DeferOne(Allocation));
	m_Fragment.m_FailClear = true;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_CpuAborts, 0);
	ExpectFailed(Allocation);
}

TEST_F(CGraphicsReadbackFlow, NonfatalWarningCompletesBothDeferredAndSkippedRequests)
{
	SAllocation Earlier;
	ASSERT_NO_FATAL_FAILURE(DeferOne(Earlier));
	m_Fragment.m_WarnClear = true;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	const auto Skipped = AddReadback();
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_WARNING);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_NONE);
	ExpectFailed(Earlier);
	ExpectFailed(Skipped);
}

TEST_F(CGraphicsReadbackFlow, BackendRefusalCompletesUnsubmittedRequestsBeforeAndAfterSubmissionStops)
{
	CReadbackProcessor Processor(m_Fragment);
	CTestGraphicsBackend Backend(Processor);
	m_Fragment.Fail();
	const auto First = AddReadback();
	Backend.RunBuffer(&m_Buffer);
	Backend.WaitForIdle();
	ExpectFailed(First);
	m_Buffer.Reset();
	const auto Second = AddReadback();
	Backend.RunBuffer(&m_Buffer);
	Backend.WaitForIdle();
	ExpectFailed(Second);
	EXPECT_EQ(Processor.m_Runs, 0);
}

TEST_F(CGraphicsReadbackFlow, UnsubmittedBufferDestructionCompletesItsOutstandingRequest)
{
	const auto Allocation = m_Queue.Allocate();
	ASSERT_GE(Allocation.m_Index, 0);
	{
		CCommandBuffer Unsubmitted(256, 64);
		CCommandBuffer::SCommand_RenderTarget_Readback Command;
		Command.m_pImage = Allocation.m_pImage;
		Command.m_pCompletion = Allocation.m_pCompletion;
		ASSERT_TRUE(Unsubmitted.AddCommandUnsafe(Command));
		EXPECT_EQ(m_Queue.Poll(Allocation.m_Index, Allocation.m_Generation), EState::PENDING);
	}
	ExpectFailed(Allocation);
}
