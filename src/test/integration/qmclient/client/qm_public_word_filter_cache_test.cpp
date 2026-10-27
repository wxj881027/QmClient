#include <game/client/components/qmclient/public_word_filter.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	class CQmPublicWordFilterCache : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}

		void WriteCache(std::string_view Content)
		{
			IOHANDLE File = m_pStorage->OpenFile("qmclient/public_word_filter.txt", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), static_cast<unsigned>(Content.size())), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CQmPublicWordFilterCache, RestoresValidatedDiskSnapshotBeforeAnyNetworkRequest)
{
	WriteCache(std::string(QmPublicWordFilter::RESPONSE_HEADER) + "测试词");
	QmPublicWordFilter::CClient Client;
	Client.Initialize(m_pStorage.get(), true);
	EXPECT_TRUE(Client.State().HasSnapshot());
	std::string Text = "这是测试词";
	EXPECT_TRUE(Client.State().Filter(Text, "*", false));
	EXPECT_EQ(Text, "这是*");
}

TEST_F(CQmPublicWordFilterCache, DisablingKeepsCacheAvailableForALaterClient)
{
	WriteCache(std::string(QmPublicWordFilter::RESPONSE_HEADER) + "cached");
	{
		QmPublicWordFilter::CClient Client;
		Client.Initialize(m_pStorage.get(), true);
		Client.Update(nullptr, m_pStorage.get(), false, 100);
		std::string Text = "cached";
		EXPECT_FALSE(Client.State().Filter(Text, "*", false));
		EXPECT_EQ(Text, "cached");
	}
	QmPublicWordFilter::CClient Reopened;
	Reopened.Initialize(m_pStorage.get(), true);
	std::string Text = "cached";
	EXPECT_TRUE(Reopened.State().Filter(Text, "*", false));
	EXPECT_EQ(Text, "*");
}

TEST_F(CQmPublicWordFilterCache, CorruptCacheDoesNotPublishPartialWords)
{
	WriteCache(std::string(QmPublicWordFilter::RESPONSE_HEADER) + "new\n\"unclosed");
	QmPublicWordFilter::CClient Client;
	Client.Initialize(m_pStorage.get(), true);
	EXPECT_FALSE(Client.State().HasSnapshot());
	EXPECT_TRUE(Client.State().Words().empty());
	EXPECT_FALSE(Client.State().Error().empty());
}
