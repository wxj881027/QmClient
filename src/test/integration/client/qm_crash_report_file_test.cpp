#include <engine/client/qm_crash_report_file.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/qm_crash_report_file_memory.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <string_view>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <windows.h>
#endif

namespace
{
	class CQmCrashReportFileTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "report.txt", m_aPath, sizeof(m_aPath));
		}
		void Write(std::string_view Bytes)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
		int64_t Length()
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_READ), &io_close);
			return File ? io_length(File.get()) : -1;
		}
	};

	struct SAppendAtAllocation
	{
		const char *m_pPath;
		std::string_view m_Bytes;
		bool m_Written = false;
		static void Append(void *pUser)
		{
			auto &State = *static_cast<SAppendAtAllocation *>(pUser);
			std::unique_ptr<void, decltype(&io_close)> File(io_open(State.m_pPath, IOFLAG_APPEND), &io_close);
			if(!File)
				return;
			const bool Written = io_write(File.get(), State.m_Bytes.data(), static_cast<unsigned>(State.m_Bytes.size())) == State.m_Bytes.size();
			const bool Closed = io_close(File.release()) == 0;
			State.m_Written = Written && Closed;
		}
	};
}

TEST_F(CQmCrashReportFileTest, ReadsTheWholeReportWithoutChangingItsBytes)
{
	const std::string Bytes = "Report type: fatal-crash\r\nraw: \xff\xe4";
	Write(Bytes);
	const auto Text = QmCrashReportFile::ReadFile(m_aPath);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Bytes);
	EXPECT_EQ(Length(), static_cast<int64_t>(Bytes.size()));
}

TEST_F(CQmCrashReportFileTest, EmptyFileIsAValidEmptyReport)
{
	Write("");
	const auto Text = QmCrashReportFile::ReadFile(m_aPath);
	ASSERT_TRUE(Text);
	EXPECT_TRUE(Text->empty());
}

TEST_F(CQmCrashReportFileTest, AFileExactlyAtTwoMibIsAccepted)
{
	const std::string Bytes(QmCrashReportFile::MAX_REPORT_BYTES, 'x');
	Write(Bytes);
	const auto Text = QmCrashReportFile::ReadFile(m_aPath);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Bytes);
}

TEST_F(CQmCrashReportFileTest, KnownOversizedFileIsRejectedBeforeTextAllocation)
{
	const std::string Bytes(QmCrashReportFile::MAX_REPORT_BYTES + 1, 'x');
	Write(Bytes);
	CCrashReportFileMemory Memory;
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath, &Memory));
	EXPECT_EQ(Memory.m_Calls, 0U);
	EXPECT_EQ(Length(), static_cast<int64_t>(Bytes.size()));
}

TEST_F(CQmCrashReportFileTest, EmbeddedNullIsRejectedWithoutChangingTheFile)
{
	const std::string Bytes("valid\0tail", 10);
	Write(Bytes);
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath));
	std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_READ), &io_close);
	ASSERT_TRUE(File);
	char aBytes[10];
	ASSERT_EQ(io_read(File.get(), aBytes, sizeof(aBytes)), sizeof(aBytes));
	EXPECT_EQ(std::string_view(aBytes, sizeof(aBytes)), Bytes);
}

TEST_F(CQmCrashReportFileTest, MissingFileIsRejectedWithoutCreatingIt)
{
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath));
	EXPECT_FALSE(fs_is_file(m_aPath));
}

TEST_F(CQmCrashReportFileTest, InvalidUtf8PathReturnsFailureWithoutOpeningAFile)
{
	const char aInvalid[] = {'x', static_cast<char>(0xff), '\0'};
	EXPECT_FALSE(QmCrashReportFile::ReadFile(aInvalid));
	EXPECT_FALSE(QmCrashReportFile::ReadFile(nullptr));
	EXPECT_FALSE(QmCrashReportFile::ReadFile(""));
}

TEST_F(CQmCrashReportFileTest, NativeFileCanBeReadAgainAfterTextAllocationFailure)
{
	const std::string Bytes(8192, 'x');
	Write(Bytes);
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath, &Memory));
	EXPECT_EQ(Memory.m_Live, 0U);
	const auto Text = QmCrashReportFile::ReadFile(m_aPath);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Bytes);
}

TEST_F(CQmCrashReportFileTest, GrowthAfterLengthPrecheckIsAcceptedWhileWithinTheLimit)
{
	const std::string Initial(4096, 'a'), Added(4096, 'b');
	Write(Initial);
	SAppendAtAllocation Append{m_aPath, Added};
	CCrashReportFileMemory Memory;
	Memory.m_pfnBeforeFirstAllocation = SAppendAtAllocation::Append;
	Memory.m_pUser = &Append;
	const auto Text = QmCrashReportFile::ReadFile(m_aPath, &Memory);
	ASSERT_TRUE(Append.m_Written);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Initial + Added);
}

TEST_F(CQmCrashReportFileTest, GrowthAfterLengthPrecheckCannotBypassTheTwoMibLimit)
{
	const std::string Initial(4096, 'a'), Added(QmCrashReportFile::MAX_REPORT_BYTES, 'b');
	Write(Initial);
	SAppendAtAllocation Append{m_aPath, Added};
	CCrashReportFileMemory Memory;
	Memory.m_pfnBeforeFirstAllocation = SAppendAtAllocation::Append;
	Memory.m_pUser = &Append;
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath, &Memory));
	ASSERT_TRUE(Append.m_Written);
	EXPECT_EQ(Memory.m_Live, 0U);
	EXPECT_EQ(Length(), static_cast<int64_t>(Initial.size() + Added.size()));
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CQmCrashReportFileTest, AllocationFailureClosesTheNativeReadHandle)
{
	Write(std::string(8192, 'x'));
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath, &Memory));
	const std::wstring Path = windows_utf8_to_wide(m_aPath);
	const HANDLE File = CreateFileW(Path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(File, INVALID_HANDLE_VALUE) << GetLastError();
	EXPECT_NE(CloseHandle(File), 0);
}
#endif
