#include <base/logger_async.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/test.h>

#include <cerrno>
#include <cstdio>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
	ASYNCIO *UnavailableAio(IOHANDLE) { return nullptr; }
	ASYNCIO *ThrowBeforeAio(IOHANDLE) { throw std::bad_alloc(); }

	class CFileProbe
	{
#if defined(CONF_FAMILY_WINDOWS)
		HANDLE m_Handle;
#else
		int m_Descriptor;
#endif

	public:
		explicit CFileProbe(IOHANDLE File)
#if defined(CONF_FAMILY_WINDOWS)
			:
			m_Handle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(static_cast<FILE *>(File))))){}
#else
			:
			m_Descriptor(fileno(static_cast<FILE *>(File)))
		{
		}
#endif
			bool Closed() const
		{
#if defined(CONF_FAMILY_WINDOWS)
			DWORD Flags;
			return !GetHandleInformation(m_Handle, &Flags) && GetLastError() == ERROR_INVALID_HANDLE;
#else
			return fcntl(m_Descriptor, F_GETFD) == -1 && errno == EBADF;
#endif
		}
	};

	struct CFileState
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::string m_Path;
		IOHANDLE m_File = nullptr;
		~CFileState()
		{
			if(m_File)
				io_close(m_File);
		}
		IOHANDLE Take() { return std::exchange(m_File, nullptr); }
		std::string Read()
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_Path.c_str(), IOFLAG_READ), &io_close);
			if(!File)
				return {};
			char aBytes[128];
			const unsigned Size = io_read(File.get(), aBytes, sizeof(aBytes));
			return {aBytes, Size};
		}
	};

	class LoggerFileCreation : public ::testing::Test
	{
		int m_Errno = errno;
#if defined(CONF_FAMILY_WINDOWS)
		DWORD m_LastError = GetLastError();
#endif

	protected:
		std::shared_ptr<CFileState> m_pState;
		void SetUp() override
		{
			m_pState = std::make_shared<CFileState>();
			ASSERT_NE(m_pState->m_pStorage, nullptr);
			m_pState->m_Path = std::string(m_pState->m_Info.StoragePath()) + "/creation.log";
			m_pState->m_File = io_open(m_pState->m_Path.c_str(), IOFLAG_WRITE);
			ASSERT_NE(m_pState->m_File, nullptr);
		}
		void TearDown() override
		{
			m_pState.reset();
			errno = m_Errno;
#if defined(CONF_FAMILY_WINDOWS)
			SetLastError(m_LastError);
#endif
		}
		template<typename P>
		void Finish(P pLogger)
		{
			ASSERT_TRUE(WaitForAsyncIo([pState = m_pState, pLogger = std::move(pLogger)]() mutable {
				pLogger->GlobalFinish();
				pLogger.reset();
			}));
		}
		CLogMessage Message(const char *pSystem, const char *pText)
		{
			CLogMessage Msg{};
			Msg.m_Level = LEVEL_INFO;
			str_copy(Msg.m_aSystem, pSystem);
			str_copy(Msg.m_aLine, pText);
			Msg.m_LineLength = str_length(Msg.m_aLine);
			return Msg;
		}
	};
}

TEST_F(LoggerFileCreation, ObjectAllocationFailureClosesTheUnconsumedFileAndStillThrows)
{
	const CFileProbe Probe(m_pState->m_File);
	EXPECT_THROW(CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE) -> std::unique_ptr<CLoggerAsync> { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, EmptyCreationResultClosesTheUnconsumedFile)
{
	const CFileProbe Probe(m_pState->m_File);
	EXPECT_EQ(CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE) { return std::unique_ptr<CLoggerAsync>{}; }), nullptr);
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, OtherCreationExceptionClosesTheUnconsumedFileAndStillThrows)
{
	const CFileProbe Probe(m_pState->m_File);
	EXPECT_THROW(CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE) -> std::unique_ptr<CLoggerAsync> { throw std::runtime_error("creation"); }), std::runtime_error);
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, ConstructorFailureBeforeAioOwnershipClosesTheFile)
{
	const CFileProbe Probe(m_pState->m_File);
	EXPECT_THROW(CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE File) { return std::make_unique<CLoggerAsync>(File, false, true, ThrowBeforeAio); }), std::bad_alloc);
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, UnavailableAioConsumesTheFileOnlyOnceAndReportsThroughTheBaseInterface)
{
	const CFileProbe Probe(m_pState->m_File);
	std::unique_ptr<ILogger> pLogger = CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE File) { return std::make_unique<CLoggerAsync>(File, false, true, UnavailableAio); });
	ASSERT_NE(pLogger, nullptr);
	EXPECT_FALSE(pLogger->IsAvailable());
	pLogger.reset();
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, AnUnavailablePrefixBackendIsRejectedWithoutLosingTheFallback)
{
	const CFileProbe Probe(m_pState->m_File);
	std::shared_ptr<ILogger> pTarget(log_logger_noop());
	const auto pFallback = pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, [&] {
		auto pFile = CLoggerAsync::CreateOwned(m_pState->Take(), [](IOHANDLE File) { return std::make_unique<CLoggerAsync>(File, false, true, UnavailableAio); });
		return log_logger_prefix(std::move(pFile), "perf/");
	}));
	EXPECT_EQ(pTarget, pFallback);
	EXPECT_TRUE(Probe.Closed());
}

TEST_F(LoggerFileCreation, PublicFileFactoryWritesAndOwnsTheFileUntilDrain)
{
	const CFileProbe Probe(m_pState->m_File);
	auto pLogger = log_logger_file(m_pState->Take());
	ASSERT_TRUE(pLogger->IsAvailable());
	const auto Msg = Message("client", "line");
	pLogger->Log(&Msg);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	EXPECT_TRUE(Probe.Closed());
#if defined(CONF_FAMILY_WINDOWS)
	EXPECT_EQ(m_pState->Read(), "line\r\n");
#else
	EXPECT_EQ(m_pState->Read(), "line\n");
#endif
}

TEST_F(LoggerFileCreation, PrefixFileFactoryPreservesFilteringAndAvailability)
{
	auto pLogger = log_logger_prefix_file(m_pState->Take(), "perf/");
	ASSERT_TRUE(pLogger->IsAvailable());
	const auto Accepted = Message("perf/frame", "accepted");
	const auto Rejected = Message("client", "rejected");
	pLogger->Log(&Accepted);
	pLogger->Log(&Rejected);
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pLogger)));
	const auto Bytes = m_pState->Read();
	EXPECT_NE(Bytes.find("accepted"), std::string::npos);
	EXPECT_EQ(Bytes.find("rejected"), std::string::npos);
}

TEST_F(LoggerFileCreation, PreparedFileBackendCanReceiveFutureStartupMessages)
{
	auto pFuture = std::make_unique<CFutureLogger>();
	const auto Msg = Message("client", "startup");
	pFuture->Log(&Msg);
	std::shared_ptr<ILogger> pTarget(log_logger_noop());
	ASSERT_TRUE(log_prepare_backend(pTarget, [&] { return log_logger_file(m_pState->Take()); }));
	pFuture->Set(std::move(pTarget));
	ASSERT_NO_FATAL_FAILURE(Finish(std::move(pFuture)));
	EXPECT_NE(m_pState->Read().find("startup"), std::string::npos);
}
