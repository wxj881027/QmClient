#include <engine/client/screenshot_save.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <new>
#include <stdexcept>

namespace
{
	// 同步调用真实Run以观察文件结果；不模拟worker内部算法，也不声称线程时序验证。
	class CInlineSaveJob : public screenshot_save::CSaveJob
	{
	public:
		using CSaveJob::CSaveJob;
		using CSaveJob::Run;
	};

	CImageInfo MakeImage()
	{
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(std::malloc(4));
		if(Image.m_pData)
		{
			Image.m_pData[0] = 12;
			Image.m_pData[1] = 34;
			Image.m_pData[2] = 56;
			Image.m_pData[3] = 255;
		}
		return Image;
	}

	bool WriteText(IStorage &Storage, const char *pName, const char *pText)
	{
		IOHANDLE File = Storage.OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const auto Size = str_length(pText);
		const bool Written = io_write(File, pText, Size) == static_cast<unsigned>(Size);
		return io_close(File) == 0 && Written;
	}
}

TEST(ScreenshotSave, ActualStorageAndPngCodecPreservePixelsAndProcessorComment)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CImageInfo Image = MakeImage();
	ASSERT_NE(Image.m_pData, nullptr);
	int Failures = 0;
	CInlineSaveJob Job(pStorage.get(), "shot.png", std::move(Image), [](CImageInfo &, std::string &Comment) { Comment = "screenshot test"; return true; }, [&] { ++Failures; });
	Job.Run();
	EXPECT_EQ(Failures, 0);
	screenshot_save::COwnedImage Decoded(CImageInfo{});
	int Incompatible = 0;
	ASSERT_TRUE(CImageLoader::LoadPng(pStorage->OpenFile("shot.png", IOFLAG_READ, IStorage::TYPE_SAVE), "shot.png", Decoded.Image(), Incompatible));
	EXPECT_EQ(Decoded.Image().m_Width, 1u);
	EXPECT_EQ(Decoded.Image().m_Height, 1u);
	ASSERT_NE(Decoded.Image().m_pData, nullptr);
	EXPECT_EQ(Decoded.Image().m_pData[0], 12);
	EXPECT_EQ(Decoded.Image().m_pData[1], 34);
	EXPECT_EQ(Decoded.Image().m_pData[2], 56);
	EXPECT_EQ(Decoded.Image().m_pData[3], 255);
	std::string Comment;
	ASSERT_TRUE(CImageLoader::ReadPngComment(pStorage->OpenFile("shot.png", IOFLAG_READ, IStorage::TYPE_SAVE), "shot.png", Comment));
	EXPECT_EQ(Comment, "screenshot test");
}

TEST(ScreenshotSave, RejectedProcessorKeepsExistingDestinationBytes)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(WriteText(*pStorage, "shot.png", "original"));
	CImageInfo Image = MakeImage();
	ASSERT_NE(Image.m_pData, nullptr);
	int Failures = 0;
	CInlineSaveJob Job(pStorage.get(), "shot.png", std::move(Image), [](CImageInfo &, std::string &) { return false; }, [&] { ++Failures; });
	Job.Run();
	EXPECT_EQ(Failures, 1);
	void *pBytes = nullptr;
	unsigned Size = 0;
	ASSERT_TRUE(pStorage->ReadFile("shot.png", IStorage::TYPE_SAVE, &pBytes, &Size));
	std::unique_ptr<void, decltype(&std::free)> Bytes(pBytes, std::free);
	ASSERT_EQ(Size, 8u);
	EXPECT_EQ(mem_comp(pBytes, "original", Size), 0);
}

TEST(ScreenshotSave, ProcessorExceptionReportsFailureBeforeOpeningDestination)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CImageInfo Image = MakeImage();
	ASSERT_NE(Image.m_pData, nullptr);
	int Failures = 0;
	CInlineSaveJob Job(pStorage.get(), "shot.png", std::move(Image), [](CImageInfo &, std::string &) -> bool { throw std::bad_alloc(); }, [&] { ++Failures; });
	EXPECT_NO_THROW(Job.Run());
	EXPECT_EQ(Failures, 1);
	EXPECT_FALSE(pStorage->FileExists("shot.png", IStorage::TYPE_SAVE));
}

TEST(ScreenshotSave, FailureCallbackExceptionDoesNotEscapeOrRepeat)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CImageInfo Image = MakeImage();
	ASSERT_NE(Image.m_pData, nullptr);
	int Failures = 0;
	CInlineSaveJob Job(pStorage.get(), "shot.png", std::move(Image), [](CImageInfo &, std::string &) { return false; }, [&] { ++Failures; throw std::runtime_error("warning unavailable"); });
	EXPECT_NO_THROW(Job.Run());
	EXPECT_EQ(Failures, 1);
}

TEST(ScreenshotSave, UnopenableDestinationReportsFailureAndPreservesParentFile)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(WriteText(*pStorage, "parent", "keep"));
	CImageInfo Image = MakeImage();
	ASSERT_NE(Image.m_pData, nullptr);
	int Failures = 0;
	CInlineSaveJob Job(pStorage.get(), "parent/shot.png", std::move(Image), nullptr, [&] { ++Failures; });
	EXPECT_NO_THROW(Job.Run());
	EXPECT_EQ(Failures, 1);
	EXPECT_TRUE(pStorage->FileExists("parent", IStorage::TYPE_SAVE));
}
