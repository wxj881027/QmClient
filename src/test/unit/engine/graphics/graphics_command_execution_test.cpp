#include <engine/client/backend/graphics_command_execution.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <new>

namespace
{
	class CCommandFragment : public CCommandProcessorFragment_GLBase
	{
		bool GetPresentedImageData(uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) override { return false; }
		void AbortCommands(bool Initialization) noexcept override
		{
			++m_Aborts;
			m_InitializationAborted = Initialization;
			m_UploadWasLiveAtAbort = m_pUpload != nullptr && m_pUpload->m_pData != nullptr;
		}

	public:
		bool m_ThrowStart = false;
		bool m_ThrowEnd = false;
		bool m_ErrorAtEnd = false;
		int m_ThrowCommand = -1;
		int m_PlatformCommand = -1;
		int m_ErrorCommand = -1;
		int m_WarningCommand = -1;
		int m_Commands = 0;
		int m_Starts = 0;
		int m_Ends = 0;
		int m_Aborts = 0;
		bool m_InitializationAborted = false;
		bool m_UploadWasLiveAtAbort = false;
		const CCommandBuffer::SCommand_Texture_Create *m_pUpload = nullptr;
		std::array<int, 8> m_aCommands{};
		size_t m_CommandCount = 0;
		size_t m_RenderCalls = 0;

		void SetError()
		{
			m_Error = {GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, {{false, "first device failure"}}};
		}
		void StartCommands(size_t CommandCount, size_t RenderCalls) override
		{
			++m_Starts;
			m_CommandCount = CommandCount;
			m_RenderCalls = RenderCalls;
			if(m_ThrowStart)
				throw std::bad_alloc();
		}
		ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *pCommand) override
		{
			m_aCommands[m_Commands++] = pCommand->m_Cmd;
			if(pCommand->m_Cmd == m_ThrowCommand)
				throw std::bad_alloc();
			if(pCommand->m_Cmd == m_ErrorCommand)
			{
				SetError();
				return RUN_COMMAND_COMMAND_ERROR;
			}
			if(pCommand->m_Cmd == m_WarningCommand)
				return RUN_COMMAND_COMMAND_WARNING;
			return pCommand->m_Cmd == m_PlatformCommand ? RUN_COMMAND_COMMAND_UNHANDLED : RUN_COMMAND_COMMAND_HANDLED;
		}
		void EndCommands() override
		{
			++m_Ends;
			if(m_ErrorAtEnd)
				SetError();
			if(m_ThrowEnd)
				throw std::bad_alloc();
		}
	};

	class CGraphicsCommandExecutionTest : public ::testing::Test
	{
	protected:
		CCommandFragment m_Fragment;
		CCommandBuffer m_Buffer{2048, 64};
		static constexpr int FIRST = CCommandBuffer::CMD_CLEAR;
		static constexpr int SECOND = CCommandBuffer::CMD_SWAP;
		static constexpr int THIRD = CCommandBuffer::CMD_VSYNC;

		void SetUp() override
		{
			ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(FIRST)));
			ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(SECOND)));
			ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandBuffer::SCommand(THIRD)));
		}
		ERunCommandReturnTypes Run()
		{
			return RunGraphicsCommandBuffer(m_Fragment, m_Buffer, [](const auto *) { return true; });
		}
	};
}

TEST_F(CGraphicsCommandExecutionTest, SuccessfulCommandsKeepOrderAndFinishOnce)
{
	m_Buffer.AddRenderCalls(7);
	m_Fragment.m_PlatformCommand = SECOND;
	int PlatformCommands = 0;
	const auto Result = RunGraphicsCommandBuffer(m_Fragment, m_Buffer, [&](const auto *pCommand) {
		++PlatformCommands;
		EXPECT_EQ(pCommand->m_Cmd, SECOND);
		return true;
	});
	EXPECT_EQ(Result, RUN_COMMAND_COMMAND_HANDLED);
	EXPECT_EQ(m_Fragment.m_CommandCount, 3U);
	EXPECT_EQ(m_Fragment.m_RenderCalls, 7U);
	EXPECT_EQ(m_Fragment.m_Commands, 3);
	EXPECT_EQ((std::array<int, 3>{m_Fragment.m_aCommands[0], m_Fragment.m_aCommands[1], m_Fragment.m_aCommands[2]}), (std::array<int, 3>{FIRST, SECOND, THIRD}));
	EXPECT_EQ(PlatformCommands, 1);
	EXPECT_EQ(m_Fragment.m_Starts, 1);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
	EXPECT_EQ(m_Fragment.m_Aborts, 0);
}

TEST_F(CGraphicsCommandExecutionTest, StartExceptionAbortsBeforeExecutingAnyCommand)
{
	m_Fragment.m_ThrowStart = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 0);
	EXPECT_EQ(m_Fragment.m_Ends, 0);
	EXPECT_EQ(m_Fragment.m_Aborts, 1);
	EXPECT_FALSE(m_Fragment.m_InitializationAborted);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_RENDER_CMD_FAILED);
}

TEST_F(CGraphicsCommandExecutionTest, CommandExceptionSkipsTheTailAndNormalSubmission)
{
	m_Fragment.m_ThrowCommand = SECOND;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 2);
	EXPECT_EQ(m_Fragment.m_Ends, 0);
	EXPECT_EQ(m_Fragment.m_Aborts, 1);
}

TEST_F(CGraphicsCommandExecutionTest, PlatformExceptionUsesTheSameAbortBoundary)
{
	m_Fragment.m_PlatformCommand = FIRST;
	EXPECT_EQ(RunGraphicsCommandBuffer(m_Fragment, m_Buffer, [](const auto *) -> bool { throw std::bad_alloc(); }), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 1);
	EXPECT_EQ(m_Fragment.m_Ends, 0);
	EXPECT_EQ(m_Fragment.m_Aborts, 1);
}

TEST_F(CGraphicsCommandExecutionTest, EndExceptionDoesNotRetrySubmission)
{
	m_Fragment.m_ThrowEnd = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 3);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
	EXPECT_EQ(m_Fragment.m_Aborts, 1);
}

TEST_F(CGraphicsCommandExecutionTest, ReportedErrorKeepsNormalWorkerDrainAndSkipsTail)
{
	m_Fragment.m_ErrorCommand = SECOND;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 2);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
	EXPECT_EQ(m_Fragment.m_Aborts, 0);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED);
}

TEST_F(CGraphicsCommandExecutionTest, LateWorkerErrorTakesPriorityOverWarning)
{
	m_Fragment.m_WarningCommand = SECOND;
	m_Fragment.m_ErrorAtEnd = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.m_Commands, 2);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED);
}

TEST_F(CGraphicsCommandExecutionTest, WarningWithoutWorkerFailureRemainsNonfatal)
{
	m_Fragment.m_WarningCommand = FIRST;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_WARNING);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_NONE);
	EXPECT_EQ(m_Fragment.m_Commands, 1);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
}

TEST_F(CGraphicsCommandExecutionTest, EndExceptionPreservesTheOriginalDeviceError)
{
	m_Fragment.m_ErrorCommand = FIRST;
	m_Fragment.m_ThrowEnd = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	const auto Error = m_Fragment.TakeError();
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED);
	ASSERT_EQ(Error.m_vErrors.size(), 1U);
	EXPECT_EQ(Error.m_vErrors[0].m_Err, "first device failure");
}

TEST_F(CGraphicsCommandExecutionTest, UploadCleanupWaitsUntilTheBackendHasAborted)
{
	m_Buffer.Reset();
	auto pData = CGraphicsUploadData<uint8_t>(static_cast<uint8_t *>(std::malloc(16)));
	ASSERT_NE(pData, nullptr);
	CCommandBuffer::SCommand_Texture_Create Upload{};
	Upload.m_pData = pData.get();
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(Upload));
	pData.release();
	m_Fragment.m_pUpload = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(m_Buffer.Head());
	m_Fragment.m_ThrowStart = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_TRUE(m_Fragment.m_UploadWasLiveAtAbort);
	EXPECT_EQ(m_Fragment.m_pUpload->m_pData, nullptr);
}

TEST_F(CGraphicsCommandExecutionTest, PreInitExceptionWritesFailureOutputsAndKeepsFallbackAvailable)
{
	m_Buffer.Reset();
	int InitError = 0;
	const char *pError = nullptr;
	CCommandProcessorFragment_GLBase::SCommand_PreInit Init{};
	Init.m_pInitError = &InitError;
	Init.m_pErrStringPtr = &pError;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(Init));
	m_Fragment.m_ThrowCommand = Init.m_Cmd;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_WARNING);
	EXPECT_EQ(InitError, -1);
	ASSERT_NE(pError, nullptr);
	EXPECT_NE(*pError, '\0');
	EXPECT_TRUE(m_Fragment.m_InitializationAborted);
	EXPECT_EQ(m_Fragment.GetWarning().m_WarningType, GFX_WARNING_TYPE_INIT_FAILED);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_NONE);
}

TEST_F(CGraphicsCommandExecutionTest, InitEndExceptionOverridesAnOtherwiseSuccessfulInitialization)
{
	m_Buffer.Reset();
	int InitError = 0;
	const char *pError = nullptr;
	CCommandProcessorFragment_GLBase::SCommand_Init Init{};
	Init.m_pInitError = &InitError;
	Init.m_pErrStringPtr = &pError;
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(Init));
	m_Fragment.m_ThrowEnd = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_WARNING);
	EXPECT_EQ(InitError, -1);
	EXPECT_NE(pError, nullptr);
	EXPECT_TRUE(m_Fragment.m_InitializationAborted);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
}

TEST_F(CGraphicsCommandExecutionTest, PreInitStartExceptionHandlesOptionalOutputs)
{
	m_Buffer.Reset();
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandProcessorFragment_GLBase::SCommand_PreInit{}));
	m_Fragment.m_ThrowStart = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_WARNING);
	EXPECT_TRUE(m_Fragment.m_InitializationAborted);
	EXPECT_EQ(m_Fragment.m_Commands, 0);
}

TEST_F(CGraphicsCommandExecutionTest, EmptyBatchExceptionStillPublishesAFatalError)
{
	m_Buffer.Reset();
	m_Fragment.m_ThrowStart = true;
	EXPECT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	EXPECT_EQ(m_Fragment.GetError().m_ErrorType, GFX_ERROR_TYPE_RENDER_CMD_FAILED);
	EXPECT_FALSE(m_Fragment.m_InitializationAborted);
}

TEST_F(CGraphicsCommandExecutionTest, DirectPostShutdownStillRunsAfterAnException)
{
	m_Fragment.m_ThrowCommand = FIRST;
	ASSERT_EQ(Run(), RUN_COMMAND_COMMAND_ERROR);
	m_Buffer.Reset();
	ASSERT_TRUE(m_Buffer.AddCommandUnsafe(CCommandProcessorFragment_GLBase::SCommand_PostShutdown{}));
	Run();
	EXPECT_EQ(m_Fragment.m_Commands, 2);
	EXPECT_EQ(m_Fragment.m_aCommands[1], CCommandProcessorFragment_GLBase::CMD_POST_SHUTDOWN);
	EXPECT_EQ(m_Fragment.m_Aborts, 1);
	EXPECT_EQ(m_Fragment.m_Ends, 1);
}
