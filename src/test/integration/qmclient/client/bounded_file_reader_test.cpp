#include <engine/storage.h>
#include <game/client/components/qmclient/bounded_file_reader.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	class CQmBoundedFileReaderTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override { m_pStorage = m_Info.CreateTestStorage(); ASSERT_NE(m_pStorage, nullptr); }
		void Write(const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile("cache.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), (unsigned)Text.size()), Text.size());
			io_close(File);
		}
		bool Read(size_t Limit, std::string &Text)
		{
			return QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), Limit, Text);
		}
	};
}

TEST_F(CQmBoundedFileReaderTest, ReadsExactBudgetAndPublishesCompleteContents)
{
	Write("12345678");
	std::string Text = "old";
	ASSERT_TRUE(Read(8, Text));
	EXPECT_EQ(Text, "12345678");
}

TEST_F(CQmBoundedFileReaderTest, RejectsOverBudgetAndRecoversAfterFileIsReplaced)
{
	Write("123456789");
	std::string Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
	Write("valid");
	ASSERT_TRUE(Read(8, Text));
	EXPECT_EQ(Text, "valid");
}

TEST_F(CQmBoundedFileReaderTest, RejectsEmptyAndMissingFilesWithoutStaleOutput)
{
	std::string Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
	Write("");
	Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
}
