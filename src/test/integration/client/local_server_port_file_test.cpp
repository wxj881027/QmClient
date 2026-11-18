#include <base/io.h>

#include <game/client/components/local_server_launch.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string_view>

namespace
{
	constexpr char PORT_FILE[] = "local_server_port.txt";

	class CLocalServerPortFileTest : public ::testing::Test
	{
	protected:
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_TestInfo.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void WritePortFile(std::string_view Contents)
		{
			IOHANDLE File = m_pStorage->OpenFile(PORT_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Contents.data(), static_cast<unsigned>(Contents.size())), Contents.size());
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CLocalServerPortFileTest, ReadsTheActualPortWhenTheServerPublishesIt)
{
	EXPECT_EQ(ReadLocalServerReportedPort(*m_pStorage, PORT_FILE), 0);
	ASSERT_NO_FATAL_FAILURE(WritePortFile("8304"));
	EXPECT_EQ(ReadLocalServerReportedPort(*m_pStorage, PORT_FILE), 8304);
}

TEST_F(CLocalServerPortFileTest, RejectsInvalidPortReportsAndAcceptsAReplacement)
{
	constexpr std::string_view aInvalidReports[] = {"8304x", "65536", "830400", "0"};
	for(std::string_view Contents : aInvalidReports)
	{
		SCOPED_TRACE(Contents);
		ASSERT_NO_FATAL_FAILURE(WritePortFile(Contents));
		EXPECT_EQ(ReadLocalServerReportedPort(*m_pStorage, PORT_FILE), 0);
	}

	ASSERT_NO_FATAL_FAILURE(WritePortFile("65535"));
	EXPECT_EQ(ReadLocalServerReportedPort(*m_pStorage, PORT_FILE), 65535);
}
