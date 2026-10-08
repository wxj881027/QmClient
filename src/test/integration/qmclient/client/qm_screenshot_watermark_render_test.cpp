#include <base/color.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

namespace
{
	class CQmScreenshotWatermarkRender : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::filesystem::path m_DataPath;
		CQmScreenshotManager m_Manager;

		void SetUp() override
		{
			// 为本例建立独立 data，避免构建目录遗留的已移除字体掩盖资源缺失。
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_DataPath = std::filesystem::absolute(m_Info.StoragePath()) / "data";
			std::filesystem::create_directories(m_DataPath / "mapres");
			std::filesystem::create_directories(m_DataPath / "fonts");
			const std::string Executable = (m_DataPath.parent_path() / "client.exe").string();
			const char *apArgs[] = {Executable.c_str()};
			m_pStorage = CreateTempStorage(m_Info.StoragePath(), std::size(apArgs), apArgs);
			ASSERT_NE(m_pStorage, nullptr);

			CImageInfo Image;
			Image.m_Width = 640;
			Image.m_Height = 120;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = static_cast<uint8_t *>(std::malloc(Image.m_Width * Image.m_Height * 4));
			ASSERT_NE(Image.m_pData, nullptr);
			for(size_t Y = 0; Y < Image.m_Height; ++Y)
				for(size_t X = 0; X < Image.m_Width; ++X)
					Image.SetPixelColor(X, Y, ColorRGBA(0.25f, 0.25f, 0.25f, 1.0f));
			const bool Saved = CImageLoader::SavePng(m_pStorage->OpenFile("source.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "source.png", Image);
			Image.Free();
			ASSERT_TRUE(Saved);
		}

		void InstallFont(const char *pName)
		{
			std::filesystem::copy_file(TestSourcePath((std::string("data/fonts/") + pName).c_str()), m_DataPath / "fonts" / pName, std::filesystem::copy_options::overwrite_existing);
		}

		std::vector<uint8_t> ReadPixels(const char *pPath)
		{
			CImageInfo Image;
			int Incompatible = 0;
			if(!CImageLoader::LoadPng(m_pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE), pPath, Image, Incompatible))
			{
				ADD_FAILURE() << "image decode failed: " << pPath;
				return {};
			}
			std::vector<uint8_t> Pixels(Image.m_pData, Image.m_pData + Image.DataSize());
			Image.Free();
			return Pixels;
		}

		bool Capture(const char *pTarget, IGraphics::FScreenshotProcessor Processor)
		{
			CImageInfo Image;
			int Incompatible = 0;
			if(!CImageLoader::LoadPng(m_pStorage->OpenFile("source.png", IOFLAG_READ, IStorage::TYPE_SAVE), "source.png", Image, Incompatible))
				return false;
			std::string Comment;
			const bool Processed = Processor(Image, Comment);
			const bool Saved = Processed && CImageLoader::SavePng(m_pStorage->OpenFile(pTarget, IOFLAG_WRITE, IStorage::TYPE_SAVE), pTarget, Image, Comment.c_str());
			Image.Free();
			return Saved;
		}

		std::vector<uint8_t> Export(const char *pText, const char *pTarget)
		{
			CQmScreenshotManager::SWatermarkOptions Options;
			Options.m_ShowTimestamp = false;
			Options.m_ShowMapName = false;
			Options.m_CustomText = pText;
			if(!m_Manager.ApplyWatermark(m_pStorage.get(), "source.png", IStorage::TYPE_SAVE, pTarget, Options))
			{
				ADD_FAILURE() << "watermark export failed: " << pTarget;
				return {};
			}
			CImageInfo Image;
			int PngliteIncompatible = 0;
			if(!CImageLoader::LoadPng(m_pStorage->OpenFile(pTarget, IOFLAG_READ, IStorage::TYPE_SAVE), pTarget, Image, PngliteIncompatible))
			{
				ADD_FAILURE() << "watermark decode failed: " << pTarget;
				Image.Free();
				return {};
			}
			std::vector<uint8_t> vPixels(Image.m_pData, Image.m_pData + Image.DataSize());
			Image.Free();
			return vPixels;
		}
	};
}

TEST_F(CQmScreenshotWatermarkRender, BundledChineseCollectionProducesDistinctChineseGlyphs)
{
	InstallFont("SourceHanSans.ttc");
	InstallFont("DejaVuSans.ttf");
	const auto vFirst = Export("中文", "chinese-first.png");
	const auto vSecond = Export("截图", "chinese-second.png");
	ASSERT_FALSE(vFirst.empty());
	ASSERT_FALSE(vSecond.empty());
	// 等长中文若都落入缺字方框，两张图的像素完全一致。
	EXPECT_NE(vFirst, vSecond);
	EXPECT_EQ(vFirst, Export("中文", "chinese-repeat.png"));
}

TEST_F(CQmScreenshotWatermarkRender, MissingGlyphUsesQuestionMarkInsteadOfNotdefBox)
{
	InstallFont("SourceHanSans.ttc");
	const auto vMissing = Export("\xF4\x8F\xBF\xBF", "missing-glyph.png");
	ASSERT_FALSE(vMissing.empty());
	EXPECT_EQ(vMissing, Export("?", "question-mark.png"));
}

TEST_F(CQmScreenshotWatermarkRender, InvalidChineseCollectionFallsBackToBundledLatinFont)
{
	InstallFont("DejaVuSans.ttf");
	IOHANDLE File = m_pStorage->OpenFile("data/fonts/SourceHanSans.ttc", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const char aInvalidFont[] = "invalid font";
	io_write(File, aInvalidFont, sizeof(aInvalidFont));
	io_close(File);
	const auto vFirst = Export("ABC", "latin-first.png");
	ASSERT_FALSE(vFirst.empty());
	EXPECT_NE(vFirst, Export("XYZ", "latin-second.png"));
}

TEST_F(CQmScreenshotWatermarkRender, MissingFontsKeepBuiltInDigitsExportable)
{
	const auto vFirst = Export("123", "digits-first.png");
	ASSERT_FALSE(vFirst.empty());
	EXPECT_NE(vFirst, Export("456", "digits-second.png"));
}

TEST_F(CQmScreenshotWatermarkRender, CaptureWithoutWatermarkPreservesPixelsAndStoresUtf8MapMetadata)
{
	CQmScreenshotManager::SWatermarkOptions Options;
	Options.m_ShowTimestamp = false;
	Options.m_ShowMapName = false;
	const auto Original = ReadPixels("source.png");
	ASSERT_FALSE(Original.empty());
	ASSERT_TRUE(Capture("raw.png", CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), false, Options, "中文地图", 1234567890)));
	EXPECT_EQ(ReadPixels("raw.png"), Original);
	EXPECT_EQ(ReadPixels("source.png"), Original);

	std::string Comment;
	ASSERT_TRUE(CImageLoader::ReadPngComment(m_pStorage->OpenFile("raw.png", IOFLAG_READ, IStorage::TYPE_SAVE), "raw.png", Comment));
	EXPECT_NE(Comment.find("中文地图"), std::string::npos);
	Options.m_ShowTimestamp = true;
	Options.m_ShowMapName = true;
	char aTimestamp[64];
	str_timestamp_ex(1234567890, aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "raw.png", IStorage::TYPE_SAVE, Options), std::string(aTimestamp) + " | MAP: 中文地图");
}

TEST_F(CQmScreenshotWatermarkRender, CaptureProcessorKeepsOptionsAndMapFromRequestBeforeLaterChanges)
{
	CQmScreenshotManager::SWatermarkOptions Options;
	Options.m_ShowTimestamp = false;
	Options.m_CustomText = "AAA";
	char aMap[32] = "first-map";
	auto Processor = CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), true, Options, aMap, 1234567890);
	Options.m_CustomText = "BBB";
	Options.m_Position = CQmScreenshotManager::EWatermarkPosition::TOP_RIGHT;
	str_copy(aMap, "second-map");
	ASSERT_TRUE(Capture("first.png", std::move(Processor)));
	const auto CapturedPixels = ReadPixels("first.png");
	EXPECT_NE(CapturedPixels, ReadPixels("source.png"));
	Options.m_CustomText = "AAA";
	Options.m_Position = CQmScreenshotManager::EWatermarkPosition::BOTTOM_LEFT;
	ASSERT_TRUE(Capture("same.png", CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), true, Options, "first-map", 1234567890)));
	EXPECT_EQ(CapturedPixels, ReadPixels("same.png"));
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "first.png", IStorage::TYPE_SAVE, Options), "MAP: first-map | AAA");
}

TEST_F(CQmScreenshotWatermarkRender, HistoryExportPreservesCaptureMetadataForReopening)
{
	CQmScreenshotManager::SWatermarkOptions Options;
	Options.m_ShowTimestamp = false;
	Options.m_ShowMapName = true;
	ASSERT_TRUE(Capture("capture.png", CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), false, Options, "original-map", 1234567890)));
	ASSERT_TRUE(m_Manager.ApplyWatermark(m_pStorage.get(), "capture.png", IStorage::TYPE_SAVE, "export.png", Options));
	CQmScreenshotManager Reopened;
	EXPECT_EQ(Reopened.BuildWatermarkText(m_pStorage.get(), "export.png", IStorage::TYPE_SAVE, Options), "MAP: original-map");
	EXPECT_EQ(ReadPixels("capture.png"), ReadPixels("source.png"));
	EXPECT_NE(ReadPixels("export.png"), ReadPixels("source.png"));
}

TEST_F(CQmScreenshotWatermarkRender, LegacyOrMissingImageOmitsUnknownCaptureMapAndTime)
{
	CQmScreenshotManager::SWatermarkOptions Options;
	Options.m_ShowTimestamp = false;
	Options.m_ShowMapName = true;
	Options.m_CustomText = "legacy";
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "source.png", IStorage::TYPE_SAVE, Options), "legacy");
	Options.m_ShowTimestamp = true;
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "missing.png", IStorage::TYPE_SAVE, Options), "legacy");
}

TEST_F(CQmScreenshotWatermarkRender, RefreshReplacesCachedMetadataAfterImageChanges)
{
	CQmScreenshotManager::SWatermarkOptions Options;
	Options.m_ShowTimestamp = false;
	ASSERT_TRUE(Capture("capture.png", CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), false, Options, "before-map", 1234567890)));
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "capture.png", IStorage::TYPE_SAVE, Options), "MAP: before-map");
	ASSERT_TRUE(Capture("capture.png", CQmScreenshotManager::CaptureProcessor(m_pStorage.get(), false, Options, "after-map", 1234567890)));
	m_Manager.Refresh(m_pStorage.get(), ".", IStorage::TYPE_SAVE);
	EXPECT_EQ(m_Manager.BuildWatermarkText(m_pStorage.get(), "capture.png", IStorage::TYPE_SAVE, Options), "MAP: after-map");
}

TEST_F(CQmScreenshotWatermarkRender, MetadataReaderStopsAtBoundedHeaderForOversizedComment)
{
	CImageInfo Image;
	int Incompatible = 0;
	ASSERT_TRUE(CImageLoader::LoadPng(m_pStorage->OpenFile("source.png", IOFLAG_READ, IStorage::TYPE_SAVE), "source.png", Image, Incompatible));
	const std::string OversizedComment(80 * 1024, 'x');
	const bool Saved = CImageLoader::SavePng(m_pStorage->OpenFile("large.png", IOFLAG_WRITE, IStorage::TYPE_SAVE), "large.png", Image, OversizedComment.c_str());
	Image.Free();
	ASSERT_TRUE(Saved);
	std::string Comment = "previous";
	EXPECT_FALSE(CImageLoader::ReadPngComment(m_pStorage->OpenFile("large.png", IOFLAG_READ, IStorage::TYPE_SAVE), "large.png", Comment));
	EXPECT_TRUE(Comment.empty());
}
