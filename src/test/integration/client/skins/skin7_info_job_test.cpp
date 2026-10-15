#include <game/client/components/qmclient/skin7_info_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
	const CQmSkin7InfoJob::SEntry *FindEntry(const std::vector<CQmSkin7InfoJob::SEntry> &Entries, const char *pName)
	{
		auto It = std::find_if(Entries.begin(), Entries.end(), [pName](const auto &Entry) { return Entry.m_Name == pName; });
		return It == Entries.end() ? nullptr : &*It;
	}
	class CTestSkin7InfoJob : public CQmSkin7InfoJob
	{
	public:
		using CQmSkin7InfoJob::CQmSkin7InfoJob;
		using CQmSkin7InfoJob::Run;
	};

	class CQmSkin7InfoJobTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
		}
		void Write(const char *pName, const char *pBody)
		{
			std::string Path = "skins7/" + std::string(pName);
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pBody, str_length(pBody)), static_cast<unsigned>(str_length(pBody)));
			io_close(File);
		}
	};
}

TEST_F(CQmSkin7InfoJobTest, WorkerPublishesParsedDescriptionsWithoutTextureOrClientAccess)
{
	Write("default.json", R"({"skin":{"body":{"filename":"standard","custom_colors":true,"hue":42}}})");
	CJobPool Pool;
	Pool.Init(1);
	auto pJob = std::make_shared<CQmSkin7InfoJob>(m_pStorage.get());
	Pool.Add(pJob);
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while(pJob->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
	auto Entries = pJob->TakeEntries();
	const auto *pEntry = FindEntry(Entries, "default");
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_StorageType, IStorage::TYPE_SAVE);
	EXPECT_STREQ((*pEntry->m_pInfo)["skin"]["body"]["filename"], "standard");
	EXPECT_EQ((*pEntry->m_pInfo)["skin"]["body"]["hue"].u.integer, 42);
}

TEST_F(CQmSkin7InfoJobTest, SkipsMalformedAndUnrelatedFilesButKeepsValidSkins)
{
	Write("broken.json", "{");
	Write("wrong.json", R"({"skin":[]})");
	Write("ignored.txt", R"({"skin":{}})");
	Write("good.json", R"({"skin":{}})");
	CTestSkin7InfoJob Job(m_pStorage.get());
	Job.Run();
	auto Entries = Job.TakeEntries();
	EXPECT_NE(FindEntry(Entries, "good"), nullptr);
	EXPECT_EQ(FindEntry(Entries, "broken"), nullptr);
	EXPECT_EQ(FindEntry(Entries, "wrong"), nullptr);
}

TEST_F(CQmSkin7InfoJobTest, CancelledJobDoesNotScanOrPublish)
{
	Write("default.json", R"({"skin":{}})");
	CTestSkin7InfoJob Job(m_pStorage.get());
	ASSERT_TRUE(Job.Abort());
	Job.Run();
	EXPECT_TRUE(Job.TakeEntries().empty());
	EXPECT_TRUE(Job.TakeParts().empty());
	auto Hat = Job.TakeXmasHat();
	EXPECT_EQ(Hat.m_pData, nullptr);
	Hat.Free();
}

TEST_F(CQmSkin7InfoJobTest, WorkerDiscoversPartFilesWithTheirStorageAndPartType)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("skins7/feet", IStorage::TYPE_SAVE));
	Write("feet/qm_test_part.png", "discovery does not decode part images");
	Write("feet/ignored.txt", "ignored");
	CTestSkin7InfoJob Job(m_pStorage.get());
	Job.Run();
	auto Parts = Job.TakeParts();
	auto It = std::find_if(Parts.begin(), Parts.end(), [](const auto &Part) { return Part.m_Name == "qm_test_part.png"; });
	ASSERT_NE(It, Parts.end());
	EXPECT_EQ(It->m_Part, protocol7::SKINPART_FEET);
	EXPECT_EQ(It->m_StorageType, IStorage::TYPE_SAVE);
	EXPECT_EQ(std::count_if(Parts.begin(), Parts.end(), [](const auto &Part) { return Part.m_Name == "ignored.txt"; }), 0);
}

TEST_F(CQmSkin7InfoJobTest, WorkerDecodesAccessoryAndTransfersItsPixelsOnlyOnce)
{
	CImageInfo Image;
	Image.m_Width = 2;
	Image.m_Height = 4;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = static_cast<uint8_t *>(calloc(8, 4));
	ASSERT_NE(Image.m_pData, nullptr);
	Image.m_pData[0] = 123;
	IOHANDLE File = m_pStorage->OpenFile("skins7/xmas_hat.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_TRUE(CImageLoader::SavePng(File, "test hat", Image));
	Image.Free();
	CTestSkin7InfoJob Job(m_pStorage.get());
	Job.Run();
	auto Hat = Job.TakeXmasHat();
	ASSERT_NE(Hat.m_pData, nullptr);
	EXPECT_EQ(Hat.m_Width, 2u);
	EXPECT_EQ(Hat.m_Height, 4u);
	EXPECT_EQ(Hat.m_pData[0], 123);
	auto Taken = Job.TakeXmasHat();
	EXPECT_EQ(Taken.m_pData, nullptr);
	Hat.Free();
	Taken.Free();
}

TEST_F(CQmSkin7InfoJobTest, InvalidAccessoryDoesNotPreventValidDescriptions)
{
	Write("xmas_hat.png", "not a PNG");
	Write("good.json", R"({"skin":{}})");
	CTestSkin7InfoJob Job(m_pStorage.get());
	Job.Run();
	auto Entries = Job.TakeEntries();
	EXPECT_NE(FindEntry(Entries, "good"), nullptr);
	auto Hat = Job.TakeXmasHat();
	EXPECT_EQ(Hat.m_pData, nullptr);
	Hat.Free();
}

TEST_F(CQmSkin7InfoJobTest, FreshJobReadsNewFilesAndOldResultsRemainOwned)
{
	Write("default.json", R"({"skin":{"body":{"filename":"old"}}})");
	CTestSkin7InfoJob OldJob(m_pStorage.get());
	OldJob.Run();
	auto OldEntries = OldJob.TakeEntries();
	Write("default.json", R"({"skin":{"body":{"filename":"new"}}})");
	CTestSkin7InfoJob NewJob(m_pStorage.get());
	NewJob.Run();
	auto NewEntries = NewJob.TakeEntries();
	const auto *pOld = FindEntry(OldEntries, "default");
	const auto *pNew = FindEntry(NewEntries, "default");
	ASSERT_NE(pOld, nullptr);
	ASSERT_NE(pNew, nullptr);
	EXPECT_STREQ((*pOld->m_pInfo)["skin"]["body"]["filename"], "old");
	EXPECT_STREQ((*pNew->m_pInfo)["skin"]["body"]["filename"], "new");
}
