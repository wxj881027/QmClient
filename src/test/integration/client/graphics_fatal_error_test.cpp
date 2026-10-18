#include <engine/client/backend_sdl.h>

#include <gtest/gtest.h>
#include <test/support/threaded_graphics_backend.h>

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
} // namespace

TEST(GraphicsFatalError, ConsumptionBeforeNextSubmissionPreservesBackendDetails)
{
	CFailingGraphicsProcessor Processor;
	CTestGraphicsBackend Backend(Processor);
	CCommandBuffer Buffer(128, 128);
	Backend.RunBuffer(&Buffer);

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
	ASSERT_TRUE(Backend.TakeFatalError());

	Backend.Restart(NewProcessor);
	EXPECT_STREQ(Backend.GetFatalError(), "");
	EXPECT_FALSE(Backend.TakeFatalError());
	Backend.RunBuffer(&Buffer);
	ASSERT_TRUE(Backend.TakeFatalError());
	EXPECT_EQ(NewProcessor.m_RunCount, 1);
}
