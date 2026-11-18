// 缩略图后台任务：真实存储目录 + 真实任务池 + 真实图像编解码，观察线程外的可绘制结果。
#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_image_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

namespace
{
	constexpr int64_t JOB_TIMEOUT_MS = 30000;

	struct SLoadedImage
	{
		CImageInfo m_Image;
		~SLoadedImage() { m_Image.Free(); }
	};

	CImageInfo MakeSolidImage(size_t Width, size_t Height, ColorRGBA Color, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = Format;
		Image.m_pData = static_cast<uint8_t *>(std::malloc(Width * Height * CImageInfo::PixelSize(Format)));
		for(size_t Y = 0; Y < Height; ++Y)
			for(size_t X = 0; X < Width; ++X)
				Image.SetPixelColor(X, Y, Color);
		return Image;
	}

	bool StorePng(IStorage *pStorage, const char *pFilename, size_t Width, size_t Height, ColorRGBA Color, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
	{
		CImageInfo Image = MakeSolidImage(Width, Height, Color, Format);
		const bool Saved = CImageLoader::SavePng(pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE), pFilename, Image);
		Image.Free();
		return Saved;
	}

	// 在真实任务池里跑完一个任务；超时返回 false，避免测试无界等待。
	bool RunJobToCompletion(const std::shared_ptr<CQmScreenshotImageJob> &pJob)
	{
		CJobPool Pool;
		Pool.Init(1);
		Pool.Add(pJob);
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(JOB_TIMEOUT_MS);
		bool Done = false;
		while(!Done && std::chrono::steady_clock::now() < Deadline)
		{
			Done = pJob->State() == IJob::STATE_DONE;
			if(!Done)
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		Pool.Shutdown();
		return Done;
	}
}

TEST(QmScreenshotImageJob, DecodesAndDownscalesToThumbnailEdge)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	const ColorRGBA SourceColor(0.25f, 0.5f, 0.75f, 1.0f);
	ASSERT_TRUE(StorePng(pStorage.get(), "big.png", 1024, 512, SourceColor));

	auto pJob = std::make_shared<CQmScreenshotImageJob>(pStorage.get(), "big.png", IStorage::TYPE_SAVE, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	ASSERT_TRUE(RunJobToCompletion(pJob));

	CImageInfo *pImage = pJob->Image();
	ASSERT_NE(pImage, nullptr);
	EXPECT_FALSE(pJob->LoadFailed());
	EXPECT_EQ(pImage->m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(pImage->m_Width, (size_t)QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	EXPECT_EQ(pImage->m_Height, (size_t)(QM_SCREENSHOT_THUMBNAIL_MAX_EDGE / 2));

	// 纯色图缩放后颜色不变，用来确认任务确实解码出了像素而不是只报告了尺寸。
	const ColorRGBA Pixel = pImage->PixelColor(0, 0);
	EXPECT_NEAR(Pixel.r, SourceColor.r, 0.02f);
	EXPECT_NEAR(Pixel.g, SourceColor.g, 0.02f);
	EXPECT_NEAR(Pixel.b, SourceColor.b, 0.02f);
}

TEST(QmScreenshotImageJob, KeepsSmallScreenshotAtSourceSize)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(StorePng(pStorage.get(), "small.png", 200, 100, ColorRGBA(0.1f, 0.2f, 0.3f, 1.0f)));

	auto pJob = std::make_shared<CQmScreenshotImageJob>(pStorage.get(), "small.png", IStorage::TYPE_SAVE, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	ASSERT_TRUE(RunJobToCompletion(pJob));

	CImageInfo *pImage = pJob->Image();
	ASSERT_NE(pImage, nullptr);
	EXPECT_EQ(pImage->m_Width, (size_t)200);
	EXPECT_EQ(pImage->m_Height, (size_t)100);
}

TEST(QmScreenshotImageJob, DetectsFormatByContentInsteadOfExtension)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(StorePng(pStorage.get(), "renamed.webp", 300, 150, ColorRGBA(0.4f, 0.4f, 0.4f, 1.0f)));

	auto pJob = std::make_shared<CQmScreenshotImageJob>(pStorage.get(), "renamed.webp", IStorage::TYPE_SAVE, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	ASSERT_TRUE(RunJobToCompletion(pJob));

	CImageInfo *pImage = pJob->Image();
	ASSERT_NE(pImage, nullptr);
	EXPECT_EQ(pImage->m_Width, (size_t)300);
	EXPECT_EQ(pImage->m_Height, (size_t)150);
}

TEST(QmScreenshotImageJob, ReportsMissingAndUndecodableFilesAsFailed)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	auto pMissing = std::make_shared<CQmScreenshotImageJob>(pStorage.get(), "missing.png", IStorage::TYPE_SAVE, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	ASSERT_TRUE(RunJobToCompletion(pMissing));
	EXPECT_TRUE(pMissing->LoadFailed());
	EXPECT_EQ(pMissing->Image(), nullptr);

	IOHANDLE File = pStorage->OpenFile("notes.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const char aText[] = "not an image";
	ASSERT_EQ(io_write(File, aText, sizeof(aText) - 1), (unsigned)(sizeof(aText) - 1));
	io_close(File);

	auto pInvalid = std::make_shared<CQmScreenshotImageJob>(pStorage.get(), "notes.png", IStorage::TYPE_SAVE, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
	ASSERT_TRUE(RunJobToCompletion(pInvalid));
	EXPECT_TRUE(pInvalid->LoadFailed());
	EXPECT_EQ(pInvalid->Image(), nullptr);
}

TEST(QmScreenshotImageJob, ViewerConvertsRgbPngAndPreservesColors)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const ColorRGBA SourceColor(0.25f, 0.5f, 0.75f, 1.0f);
	ASSERT_TRUE(StorePng(pStorage.get(), "viewer-rgb.png", 3, 2, SourceColor, CImageInfo::FORMAT_RGB));

	SLoadedImage Loaded;
	ASSERT_TRUE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), "viewer-rgb.png", IStorage::TYPE_SAVE, Loaded.m_Image));
	ASSERT_NE(Loaded.m_Image.m_pData, nullptr);
	EXPECT_EQ(Loaded.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(Loaded.m_Image.m_Width, (size_t)3);
	EXPECT_EQ(Loaded.m_Image.m_Height, (size_t)2);
	const ColorRGBA Pixel = Loaded.m_Image.PixelColor(2, 1);
	EXPECT_NEAR(Pixel.r, SourceColor.r, 0.01f);
	EXPECT_NEAR(Pixel.g, SourceColor.g, 0.01f);
	EXPECT_NEAR(Pixel.b, SourceColor.b, 0.01f);
	EXPECT_FLOAT_EQ(Pixel.a, 1.0f);
}

TEST(QmScreenshotImageJob, ViewerPreservesRgbaPngAlpha)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const ColorRGBA SourceColor(0.25f, 0.5f, 0.75f, 0.5f);
	ASSERT_TRUE(StorePng(pStorage.get(), "viewer-rgba.png", 3, 2, SourceColor));

	SLoadedImage Loaded;
	ASSERT_TRUE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), "viewer-rgba.png", IStorage::TYPE_SAVE, Loaded.m_Image));
	ASSERT_NE(Loaded.m_Image.m_pData, nullptr);
	EXPECT_EQ(Loaded.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	const ColorRGBA Pixel = Loaded.m_Image.PixelColor(2, 1);
	EXPECT_NEAR(Pixel.r, SourceColor.r, 0.01f);
	EXPECT_NEAR(Pixel.g, SourceColor.g, 0.01f);
	EXPECT_NEAR(Pixel.b, SourceColor.b, 0.01f);
	EXPECT_NEAR(Pixel.a, SourceColor.a, 0.01f);
}

TEST(QmScreenshotImageJob, ViewerLoadFailureClearsPreviousImageAndCanRetry)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(StorePng(pStorage.get(), "viewer-retry.png", 3, 2, ColorRGBA(0.25f, 0.5f, 0.75f, 1.0f)));

	SLoadedImage Loaded;
	ASSERT_TRUE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), "viewer-retry.png", IStorage::TYPE_SAVE, Loaded.m_Image));
	EXPECT_FALSE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), "missing-viewer.png", IStorage::TYPE_SAVE, Loaded.m_Image));
	EXPECT_EQ(Loaded.m_Image.m_pData, nullptr);
	EXPECT_EQ(Loaded.m_Image.m_Width, (size_t)0);
	EXPECT_EQ(Loaded.m_Image.m_Height, (size_t)0);
	EXPECT_TRUE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pStorage.get(), "viewer-retry.png", IStorage::TYPE_SAVE, Loaded.m_Image));
	EXPECT_EQ(Loaded.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
}

TEST(QmScreenshotImageJob, ViewerAcceptsExactEncodedSizeBudget)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(StorePng(pStorage.get(), "viewer-budget.png", 3, 2, ColorRGBA(0.25f, 0.5f, 0.75f, 1.0f)));

	IOHANDLE File = pStorage->OpenFile("viewer-budget.png", IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const int64_t FileSize = io_length(File);
	EXPECT_EQ(io_close(File), 0);
	ASSERT_GT(FileSize, 3 * 2 * 4);

	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "viewer-budget.png", aPath, sizeof(aPath));
	auto pLocalStorage = CreateLocalStorage();
	ASSERT_NE(pLocalStorage, nullptr);
	SLoadedImage Loaded;
	EXPECT_FALSE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pLocalStorage.get(), aPath, IStorage::TYPE_ABSOLUTE, Loaded.m_Image, static_cast<size_t>(FileSize) - 1));
	EXPECT_EQ(Loaded.m_Image.m_pData, nullptr);
	ASSERT_TRUE(CQmScreenshotImageJob::LoadRgbaImageFromDisk(pLocalStorage.get(), aPath, IStorage::TYPE_ABSOLUTE, Loaded.m_Image, static_cast<size_t>(FileSize)));
	EXPECT_EQ(Loaded.m_Image.m_Width, (size_t)3);
	EXPECT_EQ(Loaded.m_Image.m_Height, (size_t)2);
}
