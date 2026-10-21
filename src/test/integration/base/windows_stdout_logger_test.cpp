#include <base/logger_async.h>
#include <base/system.h>
#include <base/windows.h>
#include <base/windows_logger.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/test.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <engine/storage.h>

#include <windows.h>

#include <fcntl.h>

#include <array>
#include <cerrno>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
	using CHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
	using CFile = std::unique_ptr<void, decltype(&io_close)>;

	ASYNCIO *UnavailableAio(IOHANDLE) { return nullptr; }

	bool HandleIsClosed(intptr_t Handle)
	{
		DWORD Flags;
		return !GetHandleInformation(reinterpret_cast<HANDLE>(Handle), &Flags) && GetLastError() == ERROR_INVALID_HANDLE;
	}

	class CErrorRestore
	{
		int m_Errno = errno;
		DWORD m_LastError = GetLastError();

	public:
		~CErrorRestore()
		{
			errno = m_Errno;
			SetLastError(m_LastError);
		}
	};

	class CObservedStdout : public CWindowsStdoutOperations
	{
	public:
		enum class EAsyncFailure
		{
			NONE,
			EMPTY,
			ALLOCATION,
			UNKNOWN,
			AIO
		};
		intptr_t m_Borrowed = 0;
		intptr_t m_Unavailable = 0;
		intptr_t m_LastCopy = 0;
		bool m_ExposeOutput = true;
		bool m_RecoverAfterAttach = false;
		bool m_Attached = false;
		unsigned long m_Type = FILE_TYPE_DISK;
		bool m_ReadMode = true;
		unsigned long m_Mode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
		unsigned long m_OriginalMode = m_Mode;
		unsigned int m_CodePage = 936;
		bool m_ReadCodePage = true;
		bool m_SetUtf8 = true;
		bool m_Colors = true;
		bool m_FailDuplicate = false;
		bool m_FailDescriptor = false;
		bool m_FailStream = false;
		bool m_ThrowConsole = false;
		EAsyncFailure m_AsyncFailure = EAsyncFailure::NONE;
		int m_Attaches = 0;
		int m_ModeFailures = 0;
		int m_ModeCalls = 0;
		std::array<unsigned long, 8> m_aModes{};
		int m_Duplicates = 0;
		int m_DescriptorCalls = 0;
		int m_OpenFlags = 0;
		int m_NativeCloses = 0;
		int m_DescriptorCloses = 0;
		int m_AsyncCalls = 0;
		int m_ConsoleCalls = 0;
		bool m_AsyncColors = false;
		bool m_ConsoleColors = false;

		intptr_t OutputHandle() noexcept override
		{
			return m_ExposeOutput || (m_RecoverAfterAttach && m_Attached) ? m_Borrowed : m_Unavailable;
		}
		unsigned long OutputType(intptr_t) noexcept override { return m_Type; }
		void AttachParentConsole() noexcept override
		{
			++m_Attaches;
			m_Attached = true;
		}
		bool ReadConsoleMode(intptr_t, unsigned long *pMode) noexcept override
		{
			if(!m_ReadMode)
			{
				SetLastError(ERROR_ACCESS_DENIED);
				return false;
			}
			*pMode = m_Mode;
			return true;
		}
		bool WriteConsoleMode(intptr_t, unsigned long Mode) noexcept override
		{
			if(m_ModeCalls < static_cast<int>(m_aModes.size()))
				m_aModes[m_ModeCalls] = Mode;
			++m_ModeCalls;
			if(m_ModeFailures > 0)
			{
				--m_ModeFailures;
				return false;
			}
			m_Mode = Mode;
			return true;
		}
		unsigned int OutputCodePage() noexcept override { return m_ReadCodePage ? m_CodePage : 0; }
		bool SetOutputCodePage(unsigned int CodePage) noexcept override
		{
			if(CodePage == CP_UTF8 && !m_SetUtf8)
				return false;
			m_CodePage = CodePage;
			return true;
		}
		bool ColorsEnabled() noexcept override { return m_Colors; }
		intptr_t DuplicateOutput(intptr_t Handle) noexcept override
		{
			++m_Duplicates;
			if(m_FailDuplicate)
				return 0;
			m_LastCopy = CWindowsStdoutOperations::DuplicateOutput(Handle);
			return m_LastCopy;
		}
		int OpenDescriptor(intptr_t Handle, int OpenFlags) noexcept override
		{
			++m_DescriptorCalls;
			m_OpenFlags = OpenFlags;
			if(m_FailDescriptor)
			{
				errno = EMFILE;
				return -1;
			}
			return CWindowsFileOperations::OpenDescriptor(Handle, OpenFlags);
		}
		FILE *OpenStream(int Descriptor, const char *pMode) noexcept override
		{
			if(m_FailStream)
			{
				errno = EMFILE;
				return nullptr;
			}
			return CWindowsFileOperations::OpenStream(Descriptor, pMode);
		}
		void CloseNativeHandle(intptr_t Handle) noexcept override
		{
			++m_NativeCloses;
			CWindowsFileOperations::CloseNativeHandle(Handle);
		}
		void CloseDescriptor(int Descriptor) noexcept override
		{
			++m_DescriptorCloses;
			CWindowsFileOperations::CloseDescriptor(Descriptor);
		}
		std::unique_ptr<CLoggerAsync> CreateAsyncLogger(IOHANDLE File, bool Colors) override
		{
			++m_AsyncCalls;
			m_AsyncColors = Colors;
			switch(m_AsyncFailure)
			{
			case EAsyncFailure::EMPTY: return nullptr;
			case EAsyncFailure::ALLOCATION: throw std::bad_alloc();
			case EAsyncFailure::UNKNOWN: throw std::runtime_error("logger creation");
			case EAsyncFailure::AIO: return std::make_unique<CLoggerAsync>(File, Colors, true, UnavailableAio);
			case EAsyncFailure::NONE: return CWindowsStdoutOperations::CreateAsyncLogger(File, Colors);
			}
			return nullptr;
		}
		std::unique_ptr<ILogger> CreateConsoleLogger(intptr_t, bool Colors) override
		{
			++m_ConsoleCalls;
			m_ConsoleColors = Colors;
			if(m_ThrowConsole)
				throw std::bad_alloc();
			return log_logger_noop();
		}
	};

	struct CStdoutState
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		CHandle m_Original{nullptr, &CloseHandle};
		CHandle m_PipeRead{nullptr, &CloseHandle};
		std::string m_Path;
		std::unique_ptr<CObservedStdout> m_pOperations = std::make_unique<CObservedStdout>();

		bool OwnerCanWrite()
		{
			DWORD Written = 0;
			return WriteFile(m_Original.get(), "owner", 5, &Written, nullptr) && Written == 5;
		}
		std::string Read()
		{
			CFile File(io_open(m_Path.c_str(), IOFLAG_READ), &io_close);
			if(!File)
				return {};
			char aBytes[256];
			const unsigned Count = io_read(File.get(), aBytes, sizeof(aBytes));
			return {aBytes, Count};
		}
	};

	class WindowsStdoutLogger : public ::testing::Test
	{
		CErrorRestore m_ErrorRestore;

	protected:
		std::shared_ptr<CStdoutState> m_pState;
		void SetUp() override
		{
			m_pState = std::make_shared<CStdoutState>();
			ASSERT_NE(m_pState->m_pStorage, nullptr);
			m_pState->m_Path = std::string(m_pState->m_Info.StoragePath()) + "/stdout.tmp";
			const auto Path = windows_utf8_to_wide(m_pState->m_Path.c_str());
			HANDLE Original = CreateFileW(Path.c_str(), FILE_WRITE_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			ASSERT_NE(Original, INVALID_HANDLE_VALUE);
			m_pState->m_Original.reset(Original);
			Ops().m_Borrowed = reinterpret_cast<intptr_t>(Original);
		}
		CObservedStdout &Ops() { return *m_pState->m_pOperations; }
		std::unique_ptr<ILogger> Create() { return log_logger_windows_stdout(&Ops()); }
		void Finish(std::unique_ptr<ILogger> pLogger)
		{
			ASSERT_TRUE(WaitForAsyncIo([pState = m_pState, pLogger = std::move(pLogger)]() mutable {
				pLogger->GlobalFinish();
				pLogger->GlobalFinish();
				pLogger.reset();
			}));
		}
	};
}

TEST_F(WindowsStdoutLogger, MissingAndInvalidOutputReturnUnavailableAfterAttachment)
{
	for(intptr_t Missing : {intptr_t{0}, intptr_t{-1}})
	{
		SCOPED_TRACE(Missing);
		CObservedStdout Operations;
		Operations.m_Borrowed = Ops().m_Borrowed;
		Operations.m_ExposeOutput = false;
		Operations.m_Unavailable = Missing;
		EXPECT_EQ(log_logger_windows_stdout(&Operations), nullptr);
		EXPECT_EQ(Operations.m_Attaches, 1);
		EXPECT_EQ(Operations.m_Duplicates, 0);
	}
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, AttachmentCanExposeAUsableRedirectedOutput)
{
	Ops().m_ExposeOutput = false;
	Ops().m_RecoverAfterAttach = true;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_Attaches, 1);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, UnknownOutputTypeReturnsUnavailableWithoutClosingTheOwner)
{
	Ops().m_Type = FILE_TYPE_UNKNOWN;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_Attaches, 1);
	EXPECT_EQ(Ops().m_Duplicates, 0);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, ConsoleModeReadFailureReturnsUnavailableForAnyNativeError)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_ReadMode = false;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_ModeCalls, 0);
	EXPECT_EQ(Ops().m_Duplicates, 0);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, FailedFullConsoleModeFallsBackToTheReducedVirtualMode)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_ModeFailures = 1;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_ModeCalls, 2);
	EXPECT_EQ(Ops().m_Mode, Ops().m_OriginalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	EXPECT_EQ(Ops().m_CodePage, CP_UTF8);
	EXPECT_EQ(Ops().m_OpenFlags, _O_TEXT);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
}

TEST_F(WindowsStdoutLogger, FailedVirtualModesUseTheWideConsoleFallbackAndHonorColorPreference)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_ModeFailures = 2;
	Ops().m_Colors = false;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_ConsoleCalls, 1);
	EXPECT_FALSE(Ops().m_ConsoleColors);
	EXPECT_EQ(Ops().m_Duplicates, 0);
	EXPECT_EQ(Ops().m_Mode, Ops().m_OriginalMode);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, CodePageReadFailureRestoresConsoleModeAndUsesWideOutput)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_ReadCodePage = false;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_ConsoleCalls, 1);
	EXPECT_EQ(Ops().m_Mode, Ops().m_OriginalMode);
	EXPECT_EQ(Ops().m_CodePage, 936U);
	EXPECT_EQ(Ops().m_Duplicates, 0);
}

TEST_F(WindowsStdoutLogger, CodePageSetFailureRestoresConsoleModeAndUsesWideOutput)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_SetUtf8 = false;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_ConsoleCalls, 1);
	EXPECT_EQ(Ops().m_Mode, Ops().m_OriginalMode);
	EXPECT_EQ(Ops().m_CodePage, 936U);
}

TEST_F(WindowsStdoutLogger, DuplicateFailureLeavesTheBorrowedHandleWritable)
{
	Ops().m_FailDuplicate = true;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_DescriptorCalls, 0);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, DescriptorFailureClosesOnlyTheDuplicate)
{
	Ops().m_FailDescriptor = true;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_NativeCloses, 1);
	EXPECT_EQ(Ops().m_DescriptorCloses, 0);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, StreamFailureClosesTheDuplicateThroughItsDescriptor)
{
	Ops().m_FailStream = true;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_NativeCloses, 0);
	EXPECT_EQ(Ops().m_DescriptorCloses, 1);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, EmptyAsyncFactoryResultClosesTheOwnedStream)
{
	Ops().m_AsyncFailure = CObservedStdout::EAsyncFailure::EMPTY;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, AsyncAllocationFailureClosesTheOwnedStream)
{
	Ops().m_AsyncFailure = CObservedStdout::EAsyncFailure::ALLOCATION;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, UnknownAsyncCreationExceptionClosesTheOwnedStream)
{
	Ops().m_AsyncFailure = CObservedStdout::EAsyncFailure::UNKNOWN;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, AioStartupFailureDoesNotCloseTheConsumedStreamTwice)
{
	Ops().m_AsyncFailure = CObservedStdout::EAsyncFailure::AIO;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, ConsoleAsyncFailureRestoresModeAndCodePageBeforeUsingWideOutput)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_AsyncFailure = CObservedStdout::EAsyncFailure::ALLOCATION;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_ConsoleCalls, 1);
	EXPECT_EQ(Ops().m_Mode, Ops().m_OriginalMode);
	EXPECT_EQ(Ops().m_CodePage, 936U);
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, WideFallbackAllocationFailureReturnsUnavailable)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_ModeFailures = 2;
	Ops().m_ThrowConsole = true;
	EXPECT_EQ(Create(), nullptr);
	EXPECT_EQ(Ops().m_ConsoleCalls, 1);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, RedirectedOutputDrainsAndClosesItsCopyWhileKeepingTheOwnerWritable)
{
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_EQ(Ops().m_OpenFlags, _O_APPEND);
	EXPECT_FALSE(Ops().m_AsyncColors);
	CLogMessage Message{};
	Message.m_Level = LEVEL_INFO;
	str_copy(Message.m_aLine, "line");
	Message.m_LineLength = 4;
	pLogger->Log(&Message);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
	EXPECT_EQ(m_pState->Read(), "line\r\nowner");
}

TEST_F(WindowsStdoutLogger, FailedCreationCanBeRetriedWithoutSharedFailureState)
{
	Ops().m_FailDescriptor = true;
	ASSERT_EQ(Create(), nullptr);
	Ops().m_FailDescriptor = false;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	EXPECT_EQ(Ops().m_Duplicates, 2);
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, LoggerDoesNotRetainTheBorrowedOperationsObject)
{
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	m_pState->m_pOperations.reset();
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}

TEST_F(WindowsStdoutLogger, ConsoleAsyncOutputHonorsDisabledColor)
{
	Ops().m_Type = FILE_TYPE_CHAR;
	Ops().m_Colors = false;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	EXPECT_FALSE(Ops().m_AsyncColors);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
}

TEST_F(WindowsStdoutLogger, RedirectedPipeReceivesOutputAndKeepsItsBorrowedWriter)
{
	HANDLE Read = nullptr;
	HANDLE Write = nullptr;
	ASSERT_TRUE(CreatePipe(&Read, &Write, nullptr, 0));
	m_pState->m_PipeRead.reset(Read);
	m_pState->m_Original.reset(Write);
	Ops().m_Borrowed = reinterpret_cast<intptr_t>(Write);
	Ops().m_Type = FILE_TYPE_PIPE;
	auto pLogger = Create();
	ASSERT_NE(pLogger, nullptr);
	CLogMessage Message{};
	Message.m_Level = LEVEL_INFO;
	str_copy(Message.m_aLine, "pipe");
	Message.m_LineLength = 4;
	pLogger->Log(&Message);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	DWORD Available = 0;
	ASSERT_TRUE(PeekNamedPipe(Read, nullptr, 0, nullptr, &Available, nullptr));
	ASSERT_GE(Available, 6U);
	char aBytes[6];
	DWORD Count = 0;
	ASSERT_TRUE(ReadFile(Read, aBytes, sizeof(aBytes), &Count, nullptr));
	EXPECT_EQ(std::string(aBytes, Count), "pipe\r\n");
	EXPECT_TRUE(HandleIsClosed(Ops().m_LastCopy));
	EXPECT_TRUE(m_pState->OwnerCanWrite());
}
#endif
