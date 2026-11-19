#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/threaded_graphics_backend.h>

#include <cstdlib>
#include <new>
#include <string>
#include <utility>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
namespace
{
	class CDiagnosticProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
		SGfxWarningContainer m_Warning;

	public:
		SGfxErrorContainer m_Error;
		int m_RunCount = 0;
		int m_ClearCount = 0;
		void RunBuffer(CCommandBuffer *) override { ++m_RunCount; }
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

	// 错误消费回归可能阻止线程退出；每项在隔离进程中限制等待时间。
	template<typename F>
	void RunDiagnosticScenario(F &&Scenario)
	{
		const bool Completed = WaitForAsyncIo(std::forward<F>(Scenario));
		std::_Exit(Completed && !::testing::Test::HasFailure() ? 0 : 1);
	}

	class GraphicsDiagnosticFailureDeathTest : public ::testing::Test
	{
		void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
	};

	void CheckTranslationFailure(bool Allocation)
	{
		CDiagnosticProcessor Processor;
		Processor.m_Error = {GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, {{false, "incomplete prefix"}, {true, "driver advice"}}};
		CTestGraphicsBackend Backend([Allocation](const char *, const char *) -> const char * {
			if(Allocation)
				throw std::bad_alloc();
			throw 7;
		});
		ASSERT_TRUE(Backend.Start(Processor));
		ASSERT_TRUE(Backend.TakeFatalError());
		const std::string Report = Backend.GetFatalError();
		EXPECT_FALSE(Report.empty());
		EXPECT_EQ(Report.find("incomplete prefix"), std::string::npos);
		EXPECT_EQ(Processor.m_ClearCount, 1);
		EXPECT_FALSE(Backend.HasFatalError());
		EXPECT_FALSE(Backend.TakeFatalError());
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		EXPECT_EQ(Processor.m_RunCount, 0);
		EXPECT_EQ(Backend.GetFatalError(), Report);
	}
}

TEST_F(GraphicsDiagnosticFailureDeathTest, TranslationAllocationFailureStillConsumesTheFault)
{
	ASSERT_EXIT(RunDiagnosticScenario([] { CheckTranslationFailure(true); }), ::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, UnknownTranslationExceptionStillConsumesTheFault)
{
	ASSERT_EXIT(RunDiagnosticScenario([] { CheckTranslationFailure(false); }), ::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, SubmissionTimeReportFailureLeavesTheFaultConsumable)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		Processor.m_Error = {GFX_ERROR_TYPE_UNKNOWN, {{true, "unavailable translation"}}};
		CTestGraphicsBackend Backend([](const char *, const char *) -> const char * { throw std::bad_alloc(); });
		ASSERT_TRUE(Backend.Start(Processor));
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		EXPECT_STRNE(Backend.GetFatalError(), "");
		EXPECT_EQ(Processor.m_RunCount, 0);
		ASSERT_TRUE(Backend.TakeFatalError());
		EXPECT_EQ(Processor.m_ClearCount, 1);
		EXPECT_FALSE(Backend.TakeFatalError());
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, NullTranslationDiscardsTheIncompleteReport)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		Processor.m_Error = {GFX_ERROR_TYPE_RENDER_RECORDING, {{false, "incomplete prefix"}, {true, "driver advice"}}};
		CTestGraphicsBackend Backend([](const char *, const char *) -> const char * { return nullptr; });
		ASSERT_TRUE(Backend.Start(Processor));
		ASSERT_TRUE(Backend.TakeFatalError());
		EXPECT_STRNE(Backend.GetFatalError(), "");
		EXPECT_EQ(std::string(Backend.GetFatalError()).find("incomplete prefix"), std::string::npos);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, MissingBackendTextStillProducesANonemptyReport)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		Processor.m_Error.m_ErrorType = GFX_ERROR_TYPE_UNKNOWN;
		CTestGraphicsBackend Backend(Processor);
		ASSERT_TRUE(Backend.TakeFatalError());
		EXPECT_STRNE(Backend.GetFatalError(), "");
		EXPECT_FALSE(Backend.TakeFatalError());
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, LaterFaultDoesNotReplaceTheFallbackReport)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		Processor.m_Error = {GFX_ERROR_TYPE_UNKNOWN, {{true, "unavailable translation"}}};
		CTestGraphicsBackend Backend([](const char *, const char *) -> const char * { throw std::bad_alloc(); });
		ASSERT_TRUE(Backend.Start(Processor));
		ASSERT_TRUE(Backend.TakeFatalError());
		const std::string First = Backend.GetFatalError();
		Backend.ProcessError({GFX_ERROR_TYPE_SWAP_FAILED, {{false, "later error"}}});
		EXPECT_EQ(Backend.GetFatalError(), First);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, RestartClearsFallbackAndAcceptsNewCommands)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		Processor.m_Error = {GFX_ERROR_TYPE_UNKNOWN, {{true, "unavailable translation"}}};
		CTestGraphicsBackend Backend([](const char *, const char *) -> const char * { throw std::bad_alloc(); });
		ASSERT_TRUE(Backend.Start(Processor));
		ASSERT_TRUE(Backend.TakeFatalError());
		Backend.Restart(Processor);
		EXPECT_STREQ(Backend.GetFatalError(), "");
		EXPECT_FALSE(Backend.HasFatalError());
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		EXPECT_EQ(Processor.m_RunCount, 1);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsDiagnosticFailureDeathTest, NoneErrorDoesNotStopHealthySubmission)
{
	ASSERT_EXIT(RunDiagnosticScenario([] {
		CDiagnosticProcessor Processor;
		CTestGraphicsBackend Backend(Processor);
		Backend.ProcessError({});
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		EXPECT_EQ(Processor.m_RunCount, 1);
		EXPECT_STREQ(Backend.GetFatalError(), "");
	}),
		::testing::ExitedWithCode(0), "");
}
#endif
