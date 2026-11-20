#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/thread_creation_probe.h>
#include <test/support/threaded_graphics_backend.h>

#include <memory>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
namespace
{
	class CCountingProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		SGfxWarningContainer m_Warning;

	public:
		SGfxErrorContainer m_Error;
		int m_RunCount = 0;
		void RunBuffer(CCommandBuffer *) override { ++m_RunCount; }
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		void ClearError() override { m_Error = {}; }
		void ErroneousCleanup() override {}
		const SGfxWarningContainer &GetWarning() const override { return m_Warning; }
	};

	struct CStartupResult
	{
		std::shared_ptr<CThreadCreationProbe::CCounts> m_pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		bool m_First = true;
		bool m_Retry = false;
		bool m_HasError = true;
		bool m_TookError = true;
		int m_RunCount = -1;
	};
}

TEST(GraphicsThreadStartup, CreationFailureDoesNotWaitOrSubmitCommands)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CCountingProcessor Processor;
		CTestGraphicsBackend Backend;
		pResult->m_First = Backend.Start(Processor, CThreadCreationProbe::Create);
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		pResult->m_RunCount = Processor.m_RunCount;
		pResult->m_HasError = Backend.HasFatalError();
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_FALSE(pResult->m_HasError);
	EXPECT_EQ(pResult->m_RunCount, 0);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 0);
}

TEST(GraphicsThreadStartup, RetryAfterFailureAcceptsCommandsAndJoinsThread)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		CCountingProcessor Processor;
		CTestGraphicsBackend Backend;
		pResult->m_First = Backend.Start(Processor, CThreadCreationProbe::Create);
		pResult->m_Retry = Backend.Start(Processor, CThreadCreationProbe::Create);
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		pResult->m_RunCount = Processor.m_RunCount;
	}));
	EXPECT_FALSE(pResult->m_First);
	EXPECT_TRUE(pResult->m_Retry);
	EXPECT_EQ(pResult->m_RunCount, 1);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}

TEST(GraphicsThreadStartup, RestartFailureDoesNotReuseTheJoinedHandle)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 2);
		CCountingProcessor Processor;
		CTestGraphicsBackend Backend;
		pResult->m_First = Backend.Start(Processor, CThreadCreationProbe::Create);
		Backend.Stop();
		pResult->m_Retry = Backend.Start(Processor, CThreadCreationProbe::Create);
		pResult->m_HasError = Backend.HasFatalError();
	}));
	EXPECT_TRUE(pResult->m_First);
	EXPECT_FALSE(pResult->m_Retry);
	EXPECT_FALSE(pResult->m_HasError);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 2);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}

TEST(GraphicsThreadStartup, StopReleasesTheBorrowedProcessor)
{
	auto pResult = std::make_shared<CStartupResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 0);
		CCountingProcessor Processor;
		CTestGraphicsBackend Backend;
		pResult->m_First = Backend.Start(Processor, CThreadCreationProbe::Create);
		Backend.Stop();
		Processor.m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
		pResult->m_HasError = Backend.HasFatalError();
		pResult->m_TookError = Backend.TakeFatalError();
	}));
	EXPECT_TRUE(pResult->m_First);
	EXPECT_FALSE(pResult->m_HasError);
	EXPECT_FALSE(pResult->m_TookError);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}
#endif
