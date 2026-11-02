#include <base/system.h>

#include <engine/client/screenshot_save.h>
#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// 合影超采样静帧的落盘契约：保存函数报告的路径必须就是磁盘上的最终文件，
// 而且回调只在文件写完之后触发。游戏内照片卡会把回调路径直接交给看图子进程，
// 路径对不上或文件还没写完，用户看到的就是「无法打开截图文件」。
namespace
{
	// 每个用例独占临时存档目录与一张 RGBA 测试图，不依赖执行顺序。
	class CQmPoseCaptureSaveFixture
	{
	public:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CImageInfo m_Image;

		CQmPoseCaptureSaveFixture()
		{
			m_pStorage = m_Info.CreateTestStorage();
			if(m_pStorage == nullptr)
				return;
			m_Image.m_Width = 96;
			m_Image.m_Height = 48;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(std::calloc(m_Image.DataSize(), 1));
		}

		~CQmPoseCaptureSaveFixture() { m_Image.Free(); }

		static std::string Read(const std::string &Path)
		{
			std::ifstream Input(std::filesystem::u8path(Path), std::ios::binary);
			return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		}
	};
}

TEST(QmPoseCaptureSave, ReportsThePathThatActuallyLandedOnDisk)
{
	CQmPoseCaptureSaveFixture Fixture;
	ASSERT_NE(Fixture.m_pStorage, nullptr);
	ASSERT_NE(Fixture.m_Image.m_pData, nullptr);

	char aWholePath[IO_MAX_PATH_LENGTH] = "";
	ASSERT_TRUE(screenshot_save::Save(Fixture.m_pStorage.get(), "screenshots/pose_2x.png", Fixture.m_Image, nullptr, aWholePath, sizeof(aWholePath)));

	// 界面拿到的就是这条路径：必须真实存在，而且内容就是刚抓的那张图。
	ASSERT_NE(aWholePath[0], '\0');
	ASSERT_TRUE(std::filesystem::is_regular_file(std::filesystem::u8path(aWholePath)));

	const std::string Bytes = CQmPoseCaptureSaveFixture::Read(aWholePath);
	CImageInfo Loaded;
	ASSERT_TRUE(CImageLoader::LoadPng(Bytes.data(), Bytes.size(), aWholePath, Loaded));
	EXPECT_EQ(Loaded.m_Width, 96u);
	EXPECT_EQ(Loaded.m_Height, 48u);
	Loaded.Free();
}

TEST(QmPoseCaptureSave, CallbackOnlyRunsAfterTheFileIsOnDisk)
{
	// 「查看截图」打不开的回归：回调必须晚于落盘，并且回传最终写盘路径。
	CQmPoseCaptureSaveFixture Fixture;
	ASSERT_NE(Fixture.m_pStorage, nullptr);
	ASSERT_NE(Fixture.m_Image.m_pData, nullptr);

	std::string CallbackPath;
	bool ExistedInsideCallback = false;
	const IGraphics::FScreenshotCallback Callback = [&](CImageInfo &&Moved, const char *pPath) {
		CallbackPath = pPath == nullptr ? "" : pPath;
		ExistedInsideCallback = !CallbackPath.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(CallbackPath));
		Moved.Free();
	};

	ASSERT_TRUE(screenshot_save::SaveAndNotify(Fixture.m_pStorage.get(), "screenshots/pose_4x.png", std::move(Fixture.m_Image), nullptr, Callback));

	EXPECT_TRUE(ExistedInsideCallback) << "callback path did not exist yet: " << CallbackPath;
	ASSERT_FALSE(CallbackPath.empty());
	EXPECT_TRUE(std::filesystem::is_regular_file(std::filesystem::u8path(CallbackPath)));
	// 回调路径是最终文件名（调用方决定的名字与后缀），不是没解析的请求名。
	EXPECT_NE(CallbackPath.find("pose_4x.png"), std::string::npos);
}

TEST(QmPoseCaptureSave, WebpNameSelectsTheWebpEncoder)
{
	CQmPoseCaptureSaveFixture Fixture;
	ASSERT_NE(Fixture.m_pStorage, nullptr);
	ASSERT_NE(Fixture.m_Image.m_pData, nullptr);

	char aWholePath[IO_MAX_PATH_LENGTH] = "";
	ASSERT_TRUE(screenshot_save::Save(Fixture.m_pStorage.get(), "screenshots/pose_4x.webp", Fixture.m_Image, nullptr, aWholePath, sizeof(aWholePath)));
	ASSERT_TRUE(std::filesystem::is_regular_file(std::filesystem::u8path(aWholePath)));

	const std::string Bytes = CQmPoseCaptureSaveFixture::Read(aWholePath);
	ASSERT_GE(Bytes.size(), 12u);
	// RIFF....WEBP：扩展名决定编码器，而不是永远写 PNG。
	EXPECT_EQ(Bytes.compare(0, 4, "RIFF"), 0);
	EXPECT_EQ(Bytes.compare(8, 4, "WEBP"), 0);
}

TEST(QmPoseCaptureSave, ProcessorFailureLeavesNoFileBehind)
{
	// 后处理（水印等）失败时不能落一张没处理过的图，也不能报告一个可用路径。
	CQmPoseCaptureSaveFixture Fixture;
	ASSERT_NE(Fixture.m_pStorage, nullptr);
	ASSERT_NE(Fixture.m_Image.m_pData, nullptr);

	const IGraphics::FScreenshotProcessor Failing = [](CImageInfo &, std::string &) { return false; };
	char aWholePath[IO_MAX_PATH_LENGTH] = "";
	EXPECT_FALSE(screenshot_save::Save(Fixture.m_pStorage.get(), "screenshots/pose_failed.png", Fixture.m_Image, Failing, aWholePath, sizeof(aWholePath)));
	EXPECT_EQ(aWholePath[0], '\0');
	EXPECT_FALSE(Fixture.m_pStorage->FileExists("screenshots/pose_failed.png", IStorage::TYPE_SAVE));
}
