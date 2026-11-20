#include <engine/client/qm_crash_report_marker.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <engine/storage.h>

#include <test/test.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	thread_local int gs_CloseCalls = 0;
	thread_local bool gs_FailClose = false;
	BOOL WINAPI FailFlush(HANDLE)
	{
		SetLastError(ERROR_DISK_FULL);
		return FALSE;
	}
	BOOL WINAPI CountClose(HANDLE Handle)
	{
		++gs_CloseCalls;
		if(!CloseHandle(Handle))
			return FALSE;
		if(gs_FailClose)
		{
			SetLastError(ERROR_WRITE_FAULT);
			return FALSE;
		}
		return TRUE;
	}

	class CQmCrashReportMarkerTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];
		void SetUp() override
		{
			gs_CloseCalls = 0;
			gs_FailClose = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "timestamp.marker", m_aPath, sizeof(m_aPath));
		}
		void TearDown() override { gs_FailClose = false; }
		void WriteOld()
		{
			IOHANDLE File = io_open(m_aPath, IOFLAG_WRITE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "123\n", 4), 4U);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read(const char *pPath)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(pPath, IOFLAG_READ), &io_close);
			if(!File)
				return {};
			std::unique_ptr<char, decltype(&std::free)> Text(io_read_all_str(File.get()), &std::free);
			return Text ? Text.get() : "";
		}
		std::vector<std::string> Candidates()
		{
			const std::wstring Path = windows_utf8_to_wide(m_aPath);
			const auto Slash = Path.find_last_of(L"/\\");
			WIN32_FIND_DATAW Data{};
			const HANDLE Search = FindFirstFileW((Path + L".*.tmp").c_str(), &Data);
			std::vector<std::string> Result;
			if(Search == INVALID_HANDLE_VALUE)
				return Result;
			std::unique_ptr<void, decltype(&FindClose)> SearchOwner(Search, &FindClose);
			do
			{
				const auto Utf8 = windows_wide_to_utf8((Path.substr(0, Slash + 1) + Data.cFileName).c_str());
				if(Utf8)
					Result.push_back(*Utf8);
			} while(FindNextFileW(Search, &Data));
			return Result;
		}
	};
}

TEST_F(CQmCrashReportMarkerTest, CompleteCandidatePublishesTheOriginalTimestampFormat)
{
	ASSERT_TRUE(QmCrashReportMarker::Save(m_aPath, 456));
	EXPECT_EQ(Read(m_aPath), "456\n");
	EXPECT_TRUE(Candidates().empty());
}

TEST_F(CQmCrashReportMarkerTest, CompleteCandidateReplacesAnExistingMarker)
{
	WriteOld();
	ASSERT_TRUE(QmCrashReportMarker::Save(m_aPath, 456));
	EXPECT_EQ(Read(m_aPath), "456\n");
	EXPECT_TRUE(Candidates().empty());
}

TEST_F(CQmCrashReportMarkerTest, FlushFailureKeepsOldMarkerAndStillClosesOnce)
{
	WriteOld();
	crash_output::SWindowsFileOperations Operations;
	Operations.m_pfnFlush = FailFlush;
	Operations.m_pfnClose = CountClose;
	EXPECT_FALSE(QmCrashReportMarker::Save(m_aPath, 456, &Operations));
	EXPECT_EQ(Read(m_aPath), "123\n");
	EXPECT_EQ(gs_CloseCalls, 1);
	const auto Files = Candidates();
	ASSERT_EQ(Files.size(), 1U);
	EXPECT_EQ(Read(Files[0].c_str()), "456\n");
}

TEST_F(CQmCrashReportMarkerTest, CloseFailureFeedbackKeepsOldMarker)
{
	WriteOld();
	gs_FailClose = true;
	crash_output::SWindowsFileOperations Operations;
	Operations.m_pfnClose = CountClose;
	EXPECT_FALSE(QmCrashReportMarker::Save(m_aPath, 456, &Operations));
	EXPECT_EQ(Read(m_aPath), "123\n");
	EXPECT_EQ(gs_CloseCalls, 1);
	EXPECT_EQ(Candidates().size(), 1U);
}

TEST_F(CQmCrashReportMarkerTest, PublishFailureKeepsDirectoryAndCompletedCandidate)
{
	ASSERT_EQ(fs_makedir(m_aPath), 0);
	EXPECT_FALSE(QmCrashReportMarker::Save(m_aPath, 456));
	EXPECT_TRUE(fs_is_dir(m_aPath));
	const auto Files = Candidates();
	ASSERT_EQ(Files.size(), 1U);
	EXPECT_EQ(Read(Files[0].c_str()), "456\n");
}

TEST_F(CQmCrashReportMarkerTest, FailedInitialSaveDoesNotCreateAnOfficialMarker)
{
	crash_output::SWindowsFileOperations Operations;
	Operations.m_pfnFlush = FailFlush;
	EXPECT_FALSE(QmCrashReportMarker::Save(m_aPath, 456, &Operations));
	EXPECT_FALSE(fs_is_file(m_aPath));
	EXPECT_EQ(Candidates().size(), 1U);
}

TEST_F(CQmCrashReportMarkerTest, NewAttemptCanPublishAfterAnEarlierFailure)
{
	WriteOld();
	crash_output::SWindowsFileOperations Operations;
	Operations.m_pfnFlush = FailFlush;
	EXPECT_FALSE(QmCrashReportMarker::Save(m_aPath, 456, &Operations));
	const auto Failed = Candidates();
	ASSERT_EQ(Failed.size(), 1U);
	ASSERT_TRUE(QmCrashReportMarker::Save(m_aPath, 789));
	EXPECT_EQ(Read(m_aPath), "789\n");
	EXPECT_EQ(Read(Failed[0].c_str()), "456\n");
	EXPECT_EQ(Candidates().size(), 1U);
}

TEST_F(CQmCrashReportMarkerTest, InvalidPathsFailWithoutCreatingCandidates)
{
	const char aInvalid[] = {static_cast<char>(0xff), '\0'};
	const char *apPaths[] = {nullptr, "", aInvalid};
	for(const char *pPath : apPaths)
		EXPECT_FALSE(QmCrashReportMarker::Save(pPath, 456));
	EXPECT_TRUE(Candidates().empty());
}

TEST_F(CQmCrashReportMarkerTest, OversizedPathFailsWithoutTruncatingTheName)
{
	const std::string LongPath(IO_MAX_PATH_LENGTH + 256, 'x');
	EXPECT_FALSE(QmCrashReportMarker::Save(LongPath.c_str(), 456));
	EXPECT_TRUE(Candidates().empty());
}

TEST_F(CQmCrashReportMarkerTest, TimestampFormattingRetainsTheSigned64BitRange)
{
	const int64_t aTimes[] = {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()};
	for(const int64_t Timestamp : aTimes)
	{
		ASSERT_TRUE(QmCrashReportMarker::Save(m_aPath, Timestamp));
		EXPECT_EQ(Read(m_aPath), std::to_string(Timestamp) + "\n");
	}
}
#endif
