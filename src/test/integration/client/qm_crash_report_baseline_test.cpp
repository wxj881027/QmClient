#include <engine/client/qm_crash_report_marker.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/qm_crash_report_file_memory.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <string_view>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#endif

namespace
{
	class QmCrashReportBaselineFile : public testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH]{};
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "baseline.marker", m_aPath, sizeof(m_aPath));
		}
		void Write(std::string_view Bytes)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
	};
}

TEST_F(QmCrashReportBaselineFile, ReadsTheExistingTimestampAndLineEnding)
{
	Write("1700000000\n");
	EXPECT_EQ(QmCrashReportMarker::ReadStartedAt(m_aPath), 1700000000LL);
}

TEST_F(QmCrashReportBaselineFile, AFileExactlyAtMarkerBudgetIsAccepted)
{
	const std::string Text = std::string(QmCrashReportMarker::MAX_TIMESTAMP_BYTES - 4, ' ') + "123\n";
	Write(Text);
	EXPECT_EQ(QmCrashReportMarker::ReadStartedAt(m_aPath), 123);
}

TEST_F(QmCrashReportBaselineFile, OversizedFileIsRejectedBeforeTextAllocation)
{
	Write(std::string(QmCrashReportMarker::MAX_TIMESTAMP_BYTES + 1, '1'));
	CCrashReportFileMemory Memory;
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath, &Memory));
	EXPECT_EQ(Memory.m_Calls, 0u);
	EXPECT_EQ(Memory.m_Live, 0u);
}

TEST_F(QmCrashReportBaselineFile, MissingFileDoesNotYieldTimestampOrCreateFile)
{
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath));
	EXPECT_FALSE(fs_is_file(m_aPath));
}

TEST_F(QmCrashReportBaselineFile, EmptyFileDoesNotYieldTimestamp)
{
	Write("");
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath));
}

TEST_F(QmCrashReportBaselineFile, MalformedAndOverflowingValuesAreRejected)
{
	Write("123suffix\n");
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath));
	Write("9223372036854775808\n");
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath));
}

TEST_F(QmCrashReportBaselineFile, EmbeddedNullIsRejectedByTheFileReader)
{
	const char aText[] = "123\0suffix\n";
	Write(std::string_view(aText, sizeof(aText) - 1));
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath));
}

TEST_F(QmCrashReportBaselineFile, AllocationFailureReleasesMemoryAndAllowsRetry)
{
	Write(std::string(QmCrashReportMarker::MAX_TIMESTAMP_BYTES - 4, ' ') + "123\n");
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(QmCrashReportMarker::ReadStartedAt(m_aPath, &Memory));
	EXPECT_EQ(Memory.m_Live, 0u);
#if defined(CONF_FAMILY_WINDOWS)
	const HANDLE Exclusive = CreateFileW(windows_utf8_to_wide(m_aPath).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(Exclusive, INVALID_HANDLE_VALUE);
	EXPECT_TRUE(CloseHandle(Exclusive));
#endif
	Memory.m_FailAt = 0;
	EXPECT_EQ(QmCrashReportMarker::ReadStartedAt(m_aPath, &Memory), 123);
	EXPECT_EQ(Memory.m_Live, 0u);
}

TEST_F(QmCrashReportBaselineFile, CustomFileBudgetDoesNotIncreaseTheGlobalReportLimit)
{
	Write("123\n");
	CCrashReportFileMemory Memory;
	EXPECT_FALSE(QmCrashReportFile::ReadFile(m_aPath, &Memory, QmCrashReportFile::MAX_REPORT_BYTES + 1));
	EXPECT_EQ(Memory.m_Calls, 0u);
	EXPECT_EQ(QmCrashReportMarker::ReadStartedAt(m_aPath), 123);
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(QmCrashReportBaselineFile, ExistingNativeWriterRemainsCompatibleWithBoundedReader)
{
	ASSERT_TRUE(QmCrashReportMarker::Save(m_aPath, 1700000000));
	EXPECT_EQ(QmCrashReportMarker::ReadStartedAt(m_aPath), 1700000000LL);
}
#endif
