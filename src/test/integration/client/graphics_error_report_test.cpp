#include <base/system.h>

#include <engine/client/qm_graphics_diagnostics.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CGraphicsErrorReportTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		char m_aPath[IO_MAX_PATH_LENGTH];

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "graphics_fatal_report.txt", m_aPath, sizeof(m_aPath));
		}

		std::string ReadReport()
		{
			IOHANDLE File = io_open(m_aPath, IOFLAG_READ);
			EXPECT_TRUE(File);
			if(!File)
				return {};
			std::string Result((size_t)io_length(File), '\0');
			EXPECT_EQ(io_read(File, Result.data(), Result.size()), Result.size());
			EXPECT_EQ(io_close(File), 0);
			return Result;
		}
	};
} // namespace

TEST_F(CGraphicsErrorReportTest, LongBackendDiagnosticsRetainAllDetailsAndGpuInfo)
{
	const std::string Error = "vkQueueSubmit (frame) failed.\ndevice lost (VkResult -4)\n" + std::string(4096, 'x');
	ASSERT_TRUE(QmGraphicsDiagnostics::WriteReport(m_aPath, "Report type: graphics_fatal_error\n", Error.c_str(), "GPU: test device"));
	EXPECT_EQ(ReadReport(), "Report type: graphics_fatal_error\n\nGraphics error:\n" + Error + "\n\nGPU: test device\n");
}

TEST_F(CGraphicsErrorReportTest, EmptyBackendDiagnosticIsExplicitlyReported)
{
	ASSERT_TRUE(QmGraphicsDiagnostics::WriteReport(m_aPath, "header\n", "", "GPU: test device"));
	EXPECT_EQ(ReadReport(), "header\n\nGraphics error:\n(not reported by the backend)\n\nGPU: test device\n");
}

TEST_F(CGraphicsErrorReportTest, MissingReportDirectoryReturnsWriteFailure)
{
	char aMissingPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "missing/graphics_fatal_report.txt", aMissingPath, sizeof(aMissingPath));
	EXPECT_FALSE(QmGraphicsDiagnostics::WriteReport(aMissingPath, "header\n", "error", "gpu"));
	EXPECT_FALSE(m_pStorage->FileExists("missing/graphics_fatal_report.txt", IStorage::TYPE_SAVE));
}
