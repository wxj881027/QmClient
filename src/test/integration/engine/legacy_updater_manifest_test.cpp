#include <engine/client/updater_data.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <iterator>
#include <memory>
#include <string>

namespace
{
	class LegacyUpdaterManifestFile : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::string m_Path;
		QmLegacyUpdater::SUpdatePlan m_Plan;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "manifest.json", aPath, sizeof(aPath));
			m_Path = aPath;
			m_Plan.m_FileJobs.emplace_front("previous.bin", false);
		}
		void Write(std::string_view Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(m_Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}
		bool Load() { return QmLegacyUpdater::LoadManifest(m_pStorage.get(), m_Path.c_str(), "1", m_Plan); }
		void ExpectPrevious()
		{
			ASSERT_FALSE(m_Plan.m_FileJobs.empty());
			EXPECT_EQ(m_Plan.m_FileJobs.front().first, "previous.bin");
			EXPECT_FALSE(m_Plan.m_FileJobs.front().second);
			EXPECT_EQ(std::distance(m_Plan.m_FileJobs.begin(), m_Plan.m_FileJobs.end()), 1);
		}
	};
}

TEST_F(LegacyUpdaterManifestFile, MissingFilePreservesTheExistingPlan)
{
	EXPECT_FALSE(Load());
	ExpectPrevious();
}

TEST_F(LegacyUpdaterManifestFile, MalformedFileCanBeReplacedAndRetried)
{
	Write(R"([{"version":42}])");
	EXPECT_FALSE(Load());
	ExpectPrevious();
	Write(R"([{"version":"2","client":true,"download":["new.bin"]}])");
	ASSERT_TRUE(Load());
	EXPECT_TRUE(m_Plan.m_ClientUpdate);
	ASSERT_FALSE(m_Plan.m_FileJobs.empty());
	EXPECT_EQ(m_Plan.m_FileJobs.front().first, "new.bin");
}

TEST_F(LegacyUpdaterManifestFile, OversizedFileIsRejectedBeforePublishingAPlan)
{
	Write("[]" + std::string(QmLegacyUpdater::MAX_MANIFEST_BYTES, ' '));
	EXPECT_FALSE(Load());
	ExpectPrevious();
}

TEST_F(LegacyUpdaterManifestFile, EmbeddedNullInTheFileCannotHideATrailingDocument)
{
	std::string Content = "[]";
	Content.push_back('\0');
	Content += R"([{"version":"2"}])";
	Write(Content);
	EXPECT_FALSE(Load());
	ExpectPrevious();
}

TEST_F(LegacyUpdaterManifestFile, InvalidConsumedPathKeepsTheCompletePreviousPlan)
{
	Write(R"([{"version":"2","download":["good.bin","../bad"]}])");
	EXPECT_FALSE(Load());
	ExpectPrevious();
}

TEST_F(LegacyUpdaterManifestFile, ExactByteBudgetCanBeReplacedAndReloaded)
{
	Write("[]" + std::string(QmLegacyUpdater::MAX_MANIFEST_BYTES - 2, ' '));
	ASSERT_TRUE(Load());
	EXPECT_TRUE(m_Plan.m_FileJobs.empty());
	// 同一路径随后可重写并重新读取，不借用已关闭文件的缓冲。
	Write(R"([{"version":"2","remove":["old.bin"]}])");
	ASSERT_TRUE(Load());
	ASSERT_FALSE(m_Plan.m_FileJobs.empty());
	EXPECT_EQ(m_Plan.m_FileJobs.front().first, "old.bin");
	EXPECT_FALSE(m_Plan.m_FileJobs.front().second);
}
