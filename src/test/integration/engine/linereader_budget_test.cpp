#include <base/system.h>

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CLineReaderBudgetTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "text.txt", m_aPath, sizeof(m_aPath));
		}
		void Write(const std::string &Text)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Text.data(), static_cast<unsigned>(Text.size())), Text.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
	};
}

TEST_F(CLineReaderBudgetTest, ExactRawBufferBudgetIncludesTerminator)
{
	ASSERT_NO_FATAL_FAILURE(Write("text\n"));
	CLineReader Reader;
	ASSERT_TRUE(Reader.OpenFileWithBudget(io_open(m_aPath, IOFLAG_READ), 6, true));
	EXPECT_STREQ(Reader.Get(), "text");
}

TEST_F(CLineReaderBudgetTest, InsufficientRawBufferBudgetRejectsBeforeParsing)
{
	ASSERT_NO_FATAL_FAILURE(Write("text\n"));
	CLineReader Reader;
	EXPECT_FALSE(Reader.OpenFileWithBudget(io_open(m_aPath, IOFLAG_READ), 5, true));
	EXPECT_EQ(Reader.LastOpenResult(), CLineReader::EOpenResult::CANNOT_READ);
}

TEST_F(CLineReaderBudgetTest, ExistingOpenEntryKeepsDefaultBehavior)
{
	ASSERT_NO_FATAL_FAILURE(Write("a longer text\n"));
	CLineReader Reader;
	ASSERT_TRUE(Reader.OpenFile(io_open(m_aPath, IOFLAG_READ), true));
	EXPECT_STREQ(Reader.Get(), "a longer text");
}

TEST_F(CLineReaderBudgetTest, Utf16DecodingStillWorksWithinRawBudget)
{
	ASSERT_NO_FATAL_FAILURE(Write(std::string("\xff\xfe"
						  "t\0e\0s\0t\0\n\0",
		12)));
	CLineReader Reader;
	ASSERT_TRUE(Reader.OpenFileWithBudget(io_open(m_aPath, IOFLAG_READ), 13, true));
	EXPECT_STREQ(Reader.Get(), "test");
}

TEST_F(CLineReaderBudgetTest, StrictValidationStillRejectsInvalidUtf8)
{
	ASSERT_NO_FATAL_FAILURE(Write(std::string("bad") + char(0xff) + "\n"));
	CLineReader Reader;
	EXPECT_FALSE(Reader.OpenFileWithBudget(io_open(m_aPath, IOFLAG_READ), 6, true));
	EXPECT_EQ(Reader.LastOpenResult(), CLineReader::EOpenResult::INVALID_CONTENT);
}
