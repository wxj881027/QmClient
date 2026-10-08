#include <base/system.h>

#include <engine/client/qm_hang_diagnostics.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

namespace
{
	class CQmHangDumpReportTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		char m_aReportPath[IO_MAX_PATH_LENGTH];
		char m_aDumpPath[IO_MAX_PATH_LENGTH];

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "hang_report.txt", m_aReportPath, sizeof(m_aReportPath));
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "hang_dump.dmp", m_aDumpPath, sizeof(m_aDumpPath));
			IOHANDLE File = io_open(m_aReportPath, IOFLAG_WRITE);
			ASSERT_TRUE(File);
			const char *pContent = "Report type: hang\nClient state: offline (0)\n";
			EXPECT_EQ(io_write(File, pContent, str_length(pContent)), str_length(pContent));
			EXPECT_EQ(io_close(File), 0);
		}

		std::string ReadReport()
		{
			IOHANDLE File = io_open(m_aReportPath, IOFLAG_READ);
			EXPECT_TRUE(File);
			if(!File)
				return {};
			std::string Content((size_t)io_length(File), '\0');
			EXPECT_EQ(io_read(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
			return Content;
		}
	};
}

TEST_F(CQmHangDumpReportTest, SuccessfulDumpIsRecordedWithoutReplacingTheHangReport)
{
	const QmHangDiagnostics::SDumpResult Result{QmHangDiagnostics::EDumpStage::COMPLETE, 0};
	ASSERT_TRUE(QmHangDiagnostics::AppendDumpResult(m_aReportPath, m_aDumpPath, Result));
	const std::string Expected = std::string("Report type: hang\nClient state: offline (0)\nMinidump status: written\nMinidump path: ") + m_aDumpPath + "\nMinidump stage: complete\nMinidump error: 0\n";
	EXPECT_EQ(ReadReport(), Expected);
}

TEST_F(CQmHangDumpReportTest, FailedDumpRetainsThePathStageAndOriginalError)
{
	const QmHangDiagnostics::SDumpResult Result{QmHangDiagnostics::EDumpStage::WRITE_DUMP, 5};
	ASSERT_TRUE(QmHangDiagnostics::AppendDumpResult(m_aReportPath, m_aDumpPath, Result));
	const std::string Expected = std::string("Report type: hang\nClient state: offline (0)\nMinidump status: failed\nMinidump path: ") + m_aDumpPath + "\nMinidump stage: write_dump\nMinidump error: 5\n";
	EXPECT_EQ(ReadReport(), Expected);
}

TEST_F(CQmHangDumpReportTest, AppendFailureIsReturnedToTheCaller)
{
	char aMissingReport[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "missing/hang_report.txt", aMissingReport, sizeof(aMissingReport));
	const QmHangDiagnostics::SDumpResult Result{QmHangDiagnostics::EDumpStage::WRITE_DUMP, 5};
	EXPECT_FALSE(QmHangDiagnostics::AppendDumpResult(aMissingReport, m_aDumpPath, Result));
	EXPECT_EQ(ReadReport(), "Report type: hang\nClient state: offline (0)\n");
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CQmHangDumpReportTest, MissingDumpDirectoryReportsTheWindowsOpenError)
{
	char aMissingDump[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "missing/hang_dump.dmp", aMissingDump, sizeof(aMissingDump));
	const auto Result = QmHangDiagnostics::WriteDump(aMissingDump);
	EXPECT_FALSE(Result.Written());
	EXPECT_EQ(Result.m_Stage, QmHangDiagnostics::EDumpStage::OPEN_FILE);
	EXPECT_EQ(Result.m_Error, ERROR_PATH_NOT_FOUND);
	ASSERT_TRUE(QmHangDiagnostics::AppendDumpResult(m_aReportPath, aMissingDump, Result));
	const std::string Expected = std::string("Report type: hang\nClient state: offline (0)\nMinidump status: failed\nMinidump path: ") + aMissingDump + "\nMinidump stage: open_file\nMinidump error: 3\n";
	EXPECT_EQ(ReadReport(), Expected);
}
#endif
