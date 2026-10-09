#include <engine/storage.h>
#include <game/client/components/qmclient/qm_sponsors.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	class CQmSponsorsDraftTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::string m_Draft;
		std::vector<std::string> m_vNames;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Write(const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile("sponsors_draft.md", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), (unsigned)Text.size()), Text.size());
			EXPECT_EQ(io_close(File), 0);
		}
		bool Reload()
		{
			return qm_sponsors::LoadDraft(m_pStorage->OpenFile("sponsors_draft.md", IOFLAG_READ, IStorage::TYPE_SAVE), m_Draft, m_vNames);
		}
	};
}

TEST_F(CQmSponsorsDraftTest, ExactByteLimitLoadsCompleteDraftAndNames)
{
	std::string Text = "- Alice\n";
	Text.resize(64 * 1024, ' ');
	Write(Text);
	ASSERT_TRUE(Reload());
	EXPECT_EQ(m_Draft, Text);
	EXPECT_EQ(m_vNames, (std::vector<std::string>{"Alice"}));
}

TEST_F(CQmSponsorsDraftTest, OversizedReloadClearsOldDraftAndRecoversAfterReplacement)
{
	Write("- Old");
	ASSERT_TRUE(Reload());
	Write(std::string(64 * 1024 + 1, 'x'));
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
	EXPECT_TRUE(m_vNames.empty());
	Write("- 新名字\n- Bob");
	ASSERT_TRUE(Reload());
	EXPECT_EQ(m_Draft, "- 新名字\n- Bob");
	EXPECT_EQ(m_vNames, (std::vector<std::string>{"新名字", "Bob"}));
}

TEST_F(CQmSponsorsDraftTest, EmbeddedNullDoesNotLoadPartialNames)
{
	Write(std::string("- Alice\0\n- Bob", 14));
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
	EXPECT_TRUE(m_vNames.empty());
}

TEST_F(CQmSponsorsDraftTest, EmptyDraftClearsPreviouslyLoadedNames)
{
	Write("- Old");
	ASSERT_TRUE(Reload());
	Write("");
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
	EXPECT_TRUE(m_vNames.empty());
}

TEST_F(CQmSponsorsDraftTest, MissingDraftLeavesNoLoadedData)
{
	m_Draft = "old";
	m_vNames = {"Old"};
	EXPECT_FALSE(Reload());
	EXPECT_TRUE(m_Draft.empty());
	EXPECT_TRUE(m_vNames.empty());
}
