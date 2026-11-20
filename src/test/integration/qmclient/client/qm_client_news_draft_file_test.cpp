#include <base/io.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CQmNewsDraftFileTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::string m_Draft;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(qm_storage::EnsureFolder(m_pStorage.get(), qm_storage::CONFIG_DRAFTS_DIR));
		}

		void Write(const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile(QMCLIENT_NEWS_DRAFT_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), static_cast<unsigned>(Text.size())), Text.size());
			EXPECT_EQ(io_close(File), 0);
		}

		bool Reload() { return QmLoadNewsDraft(*m_pStorage, m_Draft); }
	};
}

TEST_F(CQmNewsDraftFileTest, FileAtByteLimitLoadsCompleteUtf8)
{
	std::string Text = "# 公告\n";
	Text.resize(QMCLIENT_NEWS_MAX_BYTES, ' ');
	ASSERT_NO_FATAL_FAILURE(Write(Text));
	ASSERT_TRUE(Reload());
	EXPECT_EQ(m_Draft, Text);
}

TEST_F(CQmNewsDraftFileTest, OversizedFileClearsDraftAndValidReplacementLoads)
{
	ASSERT_NO_FATAL_FAILURE(Write("old"));
	ASSERT_TRUE(Reload());

	ASSERT_NO_FATAL_FAILURE(Write(std::string(QMCLIENT_NEWS_MAX_BYTES + 1, 'x')));
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());

	ASSERT_NO_FATAL_FAILURE(Write("new"));
	ASSERT_TRUE(Reload());
	EXPECT_EQ(m_Draft, "new");
}

TEST_F(CQmNewsDraftFileTest, EmbeddedNullClearsDraft)
{
	m_Draft = "old";
	ASSERT_NO_FATAL_FAILURE(Write(std::string("Title\0body", 10)));
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
}

TEST_F(CQmNewsDraftFileTest, MissingFileClearsDraft)
{
	m_Draft = "old";
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
}
