#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/thread_creation_probe.h>
#include <test/support/threaded_graphics_backend.h>

#include <memory>
#include <new>

namespace
{
	// 只替代原生处理器；准备缓冲、命令存储、线程提交与回收均使用生产实现。
	class CPreparedProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;
		int *m_pDestroyed;

	public:
		explicit CPreparedProcessor(int *pDestroyed = nullptr) :
			m_pDestroyed(pDestroyed) {}
		~CPreparedProcessor() override
		{
			if(m_pDestroyed)
				++*m_pDestroyed;
		}
		int m_Runs = 0;
		int m_Commands = 0;
		CCommandBuffer *m_pLastBuffer = nullptr;
		void RunBuffer(CCommandBuffer *pBuffer) override
		{
			++m_Runs;
			m_pLastBuffer = pBuffer;
			for(auto *pCommand = pBuffer->Head(); pCommand; pCommand = pCommand->m_pNext)
				++m_Commands;
		}
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		void ClearError() override { m_Error = {}; }
		void ErroneousCleanup() override {}
		const SGfxWarningContainer &GetWarning() const override { return m_Warning; }
	};
}

TEST(GraphicsProcessorPreparation, FactoryAllocationFailureReleasesCommandsAndAllowsRetry)
{
	CTestGraphicsBackend Backend;
	bool SawPreparedCommands = false;
	EXPECT_EQ(Backend.PrepareProcessor([&]() -> CPreparedProcessor * {
		SawPreparedCommands = Backend.LifecycleCommands() != nullptr;
		throw std::bad_alloc();
	}),
		nullptr);
	EXPECT_TRUE(SawPreparedCommands);
	EXPECT_EQ(Backend.LifecycleCommands(), nullptr);
	std::unique_ptr<CPreparedProcessor> pProcessor(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
	ASSERT_NE(pProcessor, nullptr);
	EXPECT_NE(Backend.LifecycleCommands(), nullptr);
	Backend.ReleaseLifecycleCommands();
}

TEST(GraphicsProcessorPreparation, UnexpectedFactoryExceptionLeavesNoPreparation)
{
	CTestGraphicsBackend Backend;
	EXPECT_EQ(Backend.PrepareProcessor([]() -> CPreparedProcessor * { throw 7; }), nullptr);
	EXPECT_EQ(Backend.LifecycleCommands(), nullptr);
	Backend.ReleaseLifecycleCommands();
}

TEST(GraphicsProcessorPreparation, NullFactoryResultReleasesCommandsBeforeRetry)
{
	CTestGraphicsBackend Backend;
	EXPECT_EQ(Backend.PrepareProcessor([]() -> CPreparedProcessor * { return nullptr; }), nullptr);
	EXPECT_EQ(Backend.LifecycleCommands(), nullptr);
	std::unique_ptr<CPreparedProcessor> pProcessor(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
	ASSERT_NE(pProcessor, nullptr);
	Backend.ReleaseLifecycleCommands();
}

TEST(GraphicsProcessorPreparation, DuplicatePreparationPreservesCommandsAndOriginalOwner)
{
	CTestGraphicsBackend Backend;
	std::unique_ptr<CPreparedProcessor> pProcessor(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
	ASSERT_NE(pProcessor, nullptr);
	auto *pBuffer = Backend.LifecycleCommands();
	ASSERT_NE(pBuffer, nullptr);
	ASSERT_TRUE(pBuffer->AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)));
	int Calls = 0;
	EXPECT_EQ(Backend.PrepareProcessor([&]() -> CPreparedProcessor * { ++Calls; return nullptr; }), nullptr);
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(Backend.LifecycleCommands(), pBuffer);
	EXPECT_EQ(pBuffer->m_CommandCount, 1u);
	Backend.ReleaseLifecycleCommands();
}

TEST(GraphicsProcessorPreparation, ReentrantFactoryCannotReplaceOuterPreparation)
{
	CTestGraphicsBackend Backend;
	bool NestedRejected = false;
	bool Preserved = false;
	std::unique_ptr<CPreparedProcessor> pProcessor(Backend.PrepareProcessor([&] {
		auto *pBuffer = Backend.LifecycleCommands();
		NestedRejected = Backend.PrepareProcessor([]() -> CPreparedProcessor * { return nullptr; }) == nullptr;
		Preserved = pBuffer != nullptr && pBuffer == Backend.LifecycleCommands();
		return new CPreparedProcessor;
	}));
	ASSERT_NE(pProcessor, nullptr);
	EXPECT_TRUE(NestedRejected);
	EXPECT_TRUE(Preserved);
	Backend.ReleaseLifecycleCommands();
}

TEST(GraphicsProcessorPreparation, ReleasingUnusedCommandsDoesNotDeleteCallerOwnedProcessor)
{
	int Destroyed = 0;
	CTestGraphicsBackend Backend;
	std::unique_ptr<CPreparedProcessor> pProcessor(Backend.PrepareProcessor([&] { return new CPreparedProcessor(&Destroyed); }));
	ASSERT_NE(pProcessor, nullptr);
	Backend.ReleaseLifecycleCommands();
	Backend.ReleaseLifecycleCommands();
	EXPECT_EQ(Backend.LifecycleCommands(), nullptr);
	EXPECT_EQ(Destroyed, 0);
	pProcessor.reset();
	EXPECT_EQ(Destroyed, 1);
}

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST(GraphicsProcessorPreparation, FailedThreadCanReleasePreparationAndCreateFreshProcessor)
{
	struct SResult
	{
		std::shared_ptr<CThreadCreationProbe::CCounts> m_pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		bool m_FirstStarted = true;
		bool m_Cleaned = false;
		bool m_RetryStarted = false;
		int m_Runs = 0;
	};
	auto pResult = std::make_shared<SResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CThreadCreationProbe Probe(pResult->m_pCounts, 1);
		std::unique_ptr<CPreparedProcessor> pFirst;
		std::unique_ptr<CPreparedProcessor> pRetry;
		CTestGraphicsBackend Backend;
		pFirst.reset(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
		if(!pFirst)
			return;
		pResult->m_FirstStarted = Backend.Start(*pFirst, CThreadCreationProbe::Create);
		if(pResult->m_FirstStarted)
			return;
		Backend.ReleaseLifecycleCommands();
		pFirst.reset();
		pResult->m_Cleaned = Backend.LifecycleCommands() == nullptr;
		pRetry.reset(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
		if(!pRetry)
			return;
		pResult->m_RetryStarted = Backend.Start(*pRetry, CThreadCreationProbe::Create);
		if(!pResult->m_RetryStarted)
			return;
		auto *pBuffer = Backend.LifecycleCommands();
		if(pBuffer->AddCommandUnsafe(CCommandBuffer::SCommand(CCommandBuffer::CMD_CLEAR)))
		{
			Backend.RunBuffer(pBuffer);
			Backend.WaitForIdle();
			pBuffer->Reset();
		}
		Backend.Stop();
		pResult->m_Runs = pRetry->m_Runs;
		Backend.ReleaseLifecycleCommands();
	}));
	EXPECT_FALSE(pResult->m_FirstStarted);
	EXPECT_TRUE(pResult->m_Cleaned);
	EXPECT_TRUE(pResult->m_RetryStarted);
	EXPECT_EQ(pResult->m_Runs, 1);
	EXPECT_EQ(pResult->m_pCounts->m_Attempts.load(), 2);
	EXPECT_EQ(pResult->m_pCounts->m_Completed.load(), 1);
}

TEST(GraphicsProcessorPreparation, LiveThreadReusesPreparedBufferAndRejectsReplacement)
{
	struct SResult
	{
		bool m_Started = false;
		bool m_ReplacementRejected = false;
		bool m_SameBuffer = false;
		bool m_Released = false;
		int m_FactoryCalls = 0;
		int m_Runs = 0;
		int m_Commands = 0;
	};
	auto pResult = std::make_shared<SResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		std::unique_ptr<CPreparedProcessor> pProcessor;
		CTestGraphicsBackend Backend;
		pProcessor.reset(Backend.PrepareProcessor([] { return new CPreparedProcessor; }));
		if(!pProcessor)
			return;
		auto *pBuffer = Backend.LifecycleCommands();
		pResult->m_Started = Backend.Start(*pProcessor);
		if(!pResult->m_Started)
			return;
		pResult->m_ReplacementRejected = Backend.PrepareProcessor([&]() -> CPreparedProcessor * {
			++pResult->m_FactoryCalls;
			return nullptr;
		}) == nullptr;
		for(const int Command : {CCommandBuffer::CMD_CLEAR, CCommandBuffer::CMD_SWAP})
		{
			if(!pBuffer->AddCommandUnsafe(CCommandBuffer::SCommand(Command)))
				break;
			Backend.RunBuffer(pBuffer);
			Backend.WaitForIdle();
			pBuffer->Reset();
		}
		Backend.Stop();
		pResult->m_SameBuffer = Backend.LifecycleCommands() == pBuffer && pProcessor->m_pLastBuffer == pBuffer;
		pResult->m_Runs = pProcessor->m_Runs;
		pResult->m_Commands = pProcessor->m_Commands;
		Backend.ReleaseLifecycleCommands();
		pResult->m_Released = Backend.LifecycleCommands() == nullptr;
	}));
	EXPECT_TRUE(pResult->m_Started);
	EXPECT_TRUE(pResult->m_ReplacementRejected);
	EXPECT_EQ(pResult->m_FactoryCalls, 0);
	EXPECT_TRUE(pResult->m_SameBuffer);
	EXPECT_EQ(pResult->m_Runs, 2);
	EXPECT_EQ(pResult->m_Commands, 2);
	EXPECT_TRUE(pResult->m_Released);
}
#endif
