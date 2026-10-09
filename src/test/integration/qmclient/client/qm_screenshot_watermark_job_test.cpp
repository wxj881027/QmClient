#include <base/system.h>
#include <base/windows.h>

#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

namespace
{
	class CQmScreenshotWatermarkJobTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CQmScreenshotManager m_Manager;
		CImageInfo m_Image;
		std::string m_Target;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Target = (std::filesystem::absolute(m_Info.StoragePath()) / "export.png").string();
			m_Image.m_Width = 160;
			m_Image.m_Height = 80;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(std::calloc(m_Image.DataSize(), 1));
			ASSERT_NE(m_Image.m_pData, nullptr);
			ASSERT_TRUE(CImageLoader::SavePng(m_pStorage->OpenFile("source.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "source.png", m_Image));
		}
		void TearDown() override { m_Image.Free(); }
		std::string Read(const std::string &Path)
		{
			std::ifstream Input(std::filesystem::u8path(Path), std::ios::binary);
			return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		}
		void WriteOldExport() { std::ofstream(std::filesystem::u8path(m_Target), std::ios::binary) << "previous export"; }
		bool Run(const std::shared_ptr<CQmScreenshotWatermarkJob> &pJob)
		{
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(pJob);
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while(pJob->State() != IJob::STATE_DONE && std::chrono::steady_clock::now() < Deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			const bool Done = pJob->State() == IJob::STATE_DONE;
			Pool.Shutdown();
			return Done;
		}
		CQmScreenshotManager::SWatermarkOptions Options(const char *pText)
		{
			CQmScreenshotManager::SWatermarkOptions Result;
			Result.m_ShowTimestamp = false;
			Result.m_ShowMapName = false;
			Result.m_CustomText = pText;
			return Result;
		}
	};
}

TEST_F(CQmScreenshotWatermarkJobTest, EncodingFailurePreservesPreviousExport)
{
	WriteOldExport();
	CImageInfo Invalid;
	EXPECT_FALSE(CQmScreenshotManager::SavePngAtomically(m_Target, Invalid, ""));
	EXPECT_EQ(Read(m_Target), "previous export");
}

TEST_F(CQmScreenshotWatermarkJobTest, SuccessfulReplaceWritesDecodablePixelsAndMetadata)
{
	WriteOldExport();
	ASSERT_TRUE(CQmScreenshotManager::SavePngAtomically(m_Target, m_Image, "metadata"));
	CImageInfo Decoded;
	int Incompatible = 0;
	ASSERT_TRUE(CImageLoader::LoadPng(io_open(m_Target.c_str(), IOFLAG_READ), m_Target.c_str(), Decoded, Incompatible));
	EXPECT_EQ(Decoded.m_Width, m_Image.m_Width);
	EXPECT_EQ(Decoded.m_Height, m_Image.m_Height);
	Decoded.Free();
	std::string Comment;
	ASSERT_TRUE(CImageLoader::ReadPngComment(io_open(m_Target.c_str(), IOFLAG_READ), m_Target.c_str(), Comment));
	EXPECT_EQ(Comment, "metadata");
}

TEST_F(CQmScreenshotWatermarkJobTest, CancelBeforeCommitKeepsOldFileAndUncommittedTemp)
{
	WriteOldExport();
	const std::string Temp = m_Target + ".pending";
	std::ofstream(std::filesystem::u8path(Temp)) << "new export";
	CQmScreenshotExportControl Control;
	Control.Cancel();
	EXPECT_FALSE(Control.Commit(Temp, m_Target));
	EXPECT_EQ(Read(m_Target), "previous export");
	EXPECT_EQ(Read(Temp), "new export");
}

TEST_F(CQmScreenshotWatermarkJobTest, CancelQueuedJobPreservesPreviousExport)
{
	WriteOldExport();
	auto pJob = m_Manager.CreateWatermarkJob(m_pStorage.get(), "source.png", IStorage::TYPE_SAVE, "export.png", Options("123"));
	ASSERT_NE(pJob, nullptr);
	pJob->Cancel();
	ASSERT_TRUE(Run(pJob));
	EXPECT_TRUE(pJob->Canceled());
	EXPECT_FALSE(pJob->Saved());
	EXPECT_EQ(Read(m_Target), "previous export");
}

TEST_F(CQmScreenshotWatermarkJobTest, JobKeepsRequestOptionsAndSurvivesOriginalStorageDestruction)
{
	auto RequestOptions = Options("123");
	auto pJob = m_Manager.CreateWatermarkJob(m_pStorage.get(), "source.png", IStorage::TYPE_SAVE, "export.png", RequestOptions);
	ASSERT_NE(pJob, nullptr);
	ASSERT_TRUE(m_Manager.ApplyWatermark(m_pStorage.get(), "source.png", IStorage::TYPE_SAVE, "expected.png", RequestOptions));
	const std::string Expected = Read((std::filesystem::absolute(m_Info.StoragePath()) / "expected.png").string());
	ASSERT_FALSE(Expected.empty());
	RequestOptions.m_CustomText = "456";
	RequestOptions.m_Position = CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT;
	m_pStorage.reset();
	ASSERT_TRUE(Run(pJob));
	EXPECT_TRUE(pJob->Saved());
	EXPECT_EQ(Read(m_Target), Expected);
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CQmScreenshotWatermarkJobTest, ReplacementDeniedByOpenHandlePreservesPreviousExport)
{
	WriteOldExport();
	const auto Wide = windows_utf8_to_wide(m_Target.c_str());
	struct SHandle
	{
		HANDLE m_Handle = INVALID_HANDLE_VALUE;
		~SHandle()
		{
			if(m_Handle != INVALID_HANDLE_VALUE)
				CloseHandle(m_Handle);
		}
	} Handle;
	Handle.m_Handle = CreateFileW(Wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(Handle.m_Handle, INVALID_HANDLE_VALUE);
	EXPECT_FALSE(CQmScreenshotManager::SavePngAtomically(m_Target, m_Image, ""));
	EXPECT_EQ(Read(m_Target), "previous export");
	for(const auto &Entry : std::filesystem::directory_iterator(std::filesystem::u8path(m_Target).parent_path()))
		EXPECT_NE(Entry.path().extension(), ".tmp");
}
#endif
