#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/thread_creation_probe.h>
#include <test/support/threaded_graphics_backend.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
namespace
{
	enum class EWarningFailure
	{
		NONE,
		ALLOCATION,
		UNKNOWN,
	};

	class CWarningProcessor : public CGraphicsBackend_Threaded::ICommandProcessor
	{
	public:
		SGfxErrorContainer m_Error;
		SGfxWarningContainer m_Warning;
		std::shared_ptr<CThreadCreationProbe::CCounts> m_pCounts;
		EWarningFailure m_Failure = EWarningFailure::NONE;
		int m_RunCount = 0;
		mutable int m_WarningReads = 0;
		mutable int m_CompletedAtWarningRead = -1;

		void RunBuffer(CCommandBuffer *) override { ++m_RunCount; }
		const SGfxErrorContainer &GetError() const override { return m_Error; }
		void ClearError() override { m_Error = {}; }
		void ErroneousCleanup() override {}
		const SGfxWarningContainer &GetWarning() const override
		{
			++m_WarningReads;
			if(m_pCounts)
				m_CompletedAtWarningRead = m_pCounts->m_Completed.load();
			if(m_Failure == EWarningFailure::ALLOCATION)
				throw std::bad_alloc();
			if(m_Failure == EWarningFailure::UNKNOWN)
				throw 7;
			return m_Warning;
		}
	};

	// 关闭异常立即结束隔离测试进程，避免再次调用半关闭状态的析构路径。
	void StopBackend(CTestGraphicsBackend &Backend)
	{
		try
		{
			Backend.Stop();
		}
		catch(...)
		{
			std::_Exit(2);
		}
	}

	// 关闭回归可能留下等待线程；只在隔离测试进程内执行，并限制等待时间。
	template<typename F>
	void RunShutdownScenario(F &&Scenario)
	{
		const bool Completed = WaitForAsyncIo([Scenario = std::forward<F>(Scenario)]() mutable {
			try
			{
				Scenario();
			}
			catch(...)
			{
				std::_Exit(2);
			}
		});
		std::_Exit(Completed && !::testing::Test::HasFailure() ? 0 : 1);
	}

	class GraphicsThreadShutdownDeathTest : public ::testing::Test
	{
		void SetUp() override
		{
			GTEST_FLAG_SET(death_test_style, "threadsafe");
		}
	};

	void CheckWarningFailure(EWarningFailure Failure)
	{
		auto pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		CThreadCreationProbe Probe(pCounts, 0);
		CWarningProcessor Processor;
		Processor.m_Failure = Failure;
		CTestGraphicsBackend Backend;
		ASSERT_TRUE(Backend.Start(Processor, CThreadCreationProbe::Create));
		StopBackend(Backend);
		EXPECT_EQ(pCounts->m_Completed.load(), 1);
		EXPECT_EQ(Processor.m_WarningReads, 1);
		Processor.m_Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED;
		EXPECT_FALSE(Backend.HasFatalError());
		EXPECT_FALSE(Backend.TakeFatalError());
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		EXPECT_EQ(Processor.m_RunCount, 0);
		std::vector<std::string> vWarnings{"caller contents"};
		EXPECT_FALSE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"caller contents"}));
	}
}

TEST_F(GraphicsThreadShutdownDeathTest, WarningsSurviveProcessorDestructionAndAreConsumedOnce)
{
	ASSERT_EXIT(RunShutdownScenario([] {
		CTestGraphicsBackend Backend;
		{
			CWarningProcessor Processor;
			Processor.m_Warning = {GFX_WARNING_TYPE_INIT_FAILED, {"fallback backend", "driver advice"}};
			ASSERT_TRUE(Backend.Start(Processor));
			StopBackend(Backend);
		}
		std::vector<std::string> vWarnings{"caller contents"};
		ASSERT_TRUE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"fallback backend", "driver advice"}));
		EXPECT_FALSE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"fallback backend", "driver advice"}));
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, ThreadFinishesBeforeWarningsAreRead)
{
	ASSERT_EXIT(RunShutdownScenario([] {
		auto pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		CThreadCreationProbe Probe(pCounts, 0);
		CWarningProcessor Processor;
		Processor.m_pCounts = pCounts;
		CTestGraphicsBackend Backend;
		ASSERT_TRUE(Backend.Start(Processor, CThreadCreationProbe::Create));
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		StopBackend(Backend);
		EXPECT_EQ(Processor.m_CompletedAtWarningRead, 1);
		EXPECT_EQ(Processor.m_RunCount, 1);
		EXPECT_EQ(Processor.m_WarningReads, 1);
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, AllocationExceptionStillCompletesShutdown)
{
	ASSERT_EXIT(RunShutdownScenario([] { CheckWarningFailure(EWarningFailure::ALLOCATION); }), ::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, UnknownExceptionStillCompletesShutdown)
{
	ASSERT_EXIT(RunShutdownScenario([] { CheckWarningFailure(EWarningFailure::UNKNOWN); }), ::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, FailedWarningSnapshotKeepsPreviousCompleteWarning)
{
	ASSERT_EXIT(RunShutdownScenario([] {
		CWarningProcessor Previous;
		Previous.m_Warning = {GFX_WARNING_TYPE_INIT_FAILED, {"first warning", "first advice"}};
		CWarningProcessor Failing;
		Failing.m_Warning = {GFX_WARNING_LOW_ON_MEMORY, {"unpublished warning"}};
		Failing.m_Failure = EWarningFailure::ALLOCATION;
		CTestGraphicsBackend Backend(Previous);
		StopBackend(Backend);
		ASSERT_TRUE(Backend.Start(Failing));
		StopBackend(Backend);
		std::vector<std::string> vWarnings;
		ASSERT_TRUE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"first warning", "first advice"}));
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, RestartAfterWarningFailureExecutesCommandsAndPublishesNewWarning)
{
	ASSERT_EXIT(RunShutdownScenario([] {
		auto pCounts = std::make_shared<CThreadCreationProbe::CCounts>();
		CThreadCreationProbe Probe(pCounts, 0);
		CWarningProcessor Failing;
		Failing.m_Failure = EWarningFailure::ALLOCATION;
		CWarningProcessor Healthy;
		Healthy.m_Warning = {GFX_WARNING_TYPE_INIT_FAILED, {"new backend warning"}};
		CTestGraphicsBackend Backend;
		ASSERT_TRUE(Backend.Start(Failing, CThreadCreationProbe::Create));
		StopBackend(Backend);
		ASSERT_TRUE(Backend.Start(Healthy, CThreadCreationProbe::Create));
		CCommandBuffer Buffer(128, 128);
		Backend.RunBuffer(&Buffer);
		Backend.WaitForIdle();
		StopBackend(Backend);
		EXPECT_EQ(pCounts->m_Completed.load(), 2);
		EXPECT_EQ(Healthy.m_RunCount, 1);
		std::vector<std::string> vWarnings;
		ASSERT_TRUE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"new backend warning"}));
	}),
		::testing::ExitedWithCode(0), "");
}

TEST_F(GraphicsThreadShutdownDeathTest, EmptySnapshotReplacesAnUndeliveredWarning)
{
	ASSERT_EXIT(RunShutdownScenario([] {
		CWarningProcessor Previous;
		Previous.m_Warning = {GFX_WARNING_TYPE_INIT_FAILED, {"previous warning"}};
		CWarningProcessor Healthy;
		CTestGraphicsBackend Backend(Previous);
		StopBackend(Backend);
		ASSERT_TRUE(Backend.Start(Healthy));
		StopBackend(Backend);
		std::vector<std::string> vWarnings{"caller contents"};
		EXPECT_FALSE(Backend.GetWarning(vWarnings));
		EXPECT_EQ(vWarnings, (std::vector<std::string>{"caller contents"}));
	}),
		::testing::ExitedWithCode(0), "");
}
#endif
