#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/word_filter_list.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	class QmWordFilterFile : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		QmWordFilterList::CFileList m_List;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Write(std::string_view Content)
		{
			IOHANDLE File = m_pStorage->OpenFile("words.txt", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const unsigned Written = io_write(File, Content.data(), static_cast<unsigned>(Content.size()));
			const int Closed = io_close(File);
			ASSERT_EQ(Written, Content.size());
			ASSERT_EQ(Closed, 0);
		}
	};
}

TEST_F(QmWordFilterFile, ReadsAndPublishesACompleteUtf8File)
{
	Write("\xef\xbb\xbf# note\r\n原有词\r\n\"other\",\r\n原有词\r\n");
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Words(), (std::vector<std::string>{"原有词", "other"}));
	EXPECT_TRUE(m_List.Error().empty());
}

TEST_F(QmWordFilterFile, MalformedFilePreservesPreviousWordsAndNormalAttemptCaching)
{
	ASSERT_TRUE(m_List.UpdateFromText("old"));
	const uint64_t Revision = m_List.Revision();
	Write("new\n\"unclosed");
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Words(), (std::vector<std::string>{"old"}));
	EXPECT_EQ(m_List.Revision(), Revision);
	EXPECT_FALSE(m_List.Error().empty());
	Write("new");
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Revision(), Revision);
	m_List.Refresh(m_pStorage.get(), "words.txt", true);
	EXPECT_EQ(m_List.Words(), (std::vector<std::string>{"new"}));
	EXPECT_EQ(m_List.Revision(), Revision + 1);
	EXPECT_TRUE(m_List.Error().empty());
}

TEST_F(QmWordFilterFile, MissingFilePreservesWordsUntilAnExplicitReload)
{
	ASSERT_TRUE(m_List.UpdateFromText("old"));
	const uint64_t Revision = m_List.Revision();
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Revision(), Revision);
	EXPECT_FALSE(m_List.Error().empty());
	Write("new");
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Revision(), Revision);
	m_List.Refresh(m_pStorage.get(), "words.txt", true);
	EXPECT_EQ(m_List.Words(), (std::vector<std::string>{"new"}));
}

TEST_F(QmWordFilterFile, OversizedPhysicalFileIsRejectedWithoutPublishingWords)
{
	ASSERT_TRUE(m_List.UpdateFromText("old"));
	const uint64_t Revision = m_List.Revision();
	Write(std::string(QmWordFilterList::MAX_FILE_BYTES + 1, 'a'));
	m_List.Refresh(m_pStorage.get(), "words.txt");
	EXPECT_EQ(m_List.Revision(), Revision);
	EXPECT_EQ(m_List.Words(), (std::vector<std::string>{"old"}));
	EXPECT_FALSE(m_List.Error().empty());
}

TEST_F(QmWordFilterFile, ClearingThePathClearsPublishedWordsWithoutOpeningAFile)
{
	ASSERT_TRUE(m_List.UpdateFromText("old"));
	const uint64_t Revision = m_List.Revision();
	m_List.Refresh(nullptr, "", true);
	EXPECT_TRUE(m_List.Words().empty());
	EXPECT_TRUE(m_List.Error().empty());
	EXPECT_EQ(m_List.Revision(), Revision + 1);
}
