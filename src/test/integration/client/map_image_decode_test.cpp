#include <base/mem.h>

#include <engine/gfx/image_loader.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/qmclient/map_image_decode_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <new>
#include <stdexcept>

TEST(MapImageDecode, CompressedPixelsOutliveTheSourceMap)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::array<unsigned char, 8> aPixels = {10, 20, 30, 255, 1, 2, 3, 0};
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "images.map"));
	const int Index = Writer.AddData(aPixels.size(), aPixels.data());
	Writer.Finish();

	CMapImageDecodeJob Job(nullptr, "map image", false);
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "images.map", IStorage::TYPE_SAVE));
	Job.m_UncompressedSize = Reader.GetDataSize(Index);
	ASSERT_TRUE(Reader.ReadCompressedData(Index, Job.m_vCompressed));
	Reader.Close();
	Job.m_Image.m_Width = 2;
	Job.m_Image.m_Height = 1;
	Job.m_Image.m_Format = CImageInfo::FORMAT_RGBA;
	Job.Run();
	ASSERT_NE(Job.m_Image.m_pData, nullptr);
	EXPECT_EQ(mem_comp(Job.m_Image.m_pData, aPixels.data(), aPixels.size()), 0);
}

TEST(MapImageDecode, ProcessedMapBlocksUseTheMapReader)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "processed.map"));
	const unsigned char Pixel = 1;
	const int Index = Writer.AddData(sizeof(Pixel), &Pixel);
	Writer.Finish();

	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "processed.map", IStorage::TYPE_SAVE));
	Reader.AddDataProcessor(Index, [](void *pData, size_t Size) {
		*static_cast<unsigned char *>(pData) = 2;
		return std::make_pair(pData, Size);
	});
	std::vector<unsigned char> vCompressed;
	EXPECT_FALSE(Reader.ReadCompressedData(Index, vCompressed));
	const auto *pPixel = static_cast<unsigned char *>(Reader.GetData(Index));
	ASSERT_NE(pPixel, nullptr);
	EXPECT_EQ(*pPixel, 2);
}

TEST(MapImageDecode, InvalidCompressedPixelsProduceNoUploadableImage)
{
	CMapImageDecodeJob Job(nullptr, "map image", false);
	Job.m_Image.m_Width = 1;
	Job.m_Image.m_Height = 1;
	Job.m_Image.m_Format = CImageInfo::FORMAT_RGBA;
	Job.m_UncompressedSize = 4;
	Job.m_vCompressed = {1, 2, 3};
	Job.Run();
	EXPECT_EQ(Job.m_Image.m_pData, nullptr);
}

TEST(MapImageDecode, ExternalRgbPngIsReadyForAMoveOnlyRgbaUpload)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::array<uint8_t, 3> aPixel = {20, 30, 40};
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGB;
	Image.m_pData = aPixel.data();
	IOHANDLE File = pStorage->OpenFile("rgb.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_TRUE(CImageLoader::SavePng(File, "rgb.png", Image));

	CMapImageDecodeJob Job(pStorage.get(), "rgb.png", false);
	Job.Run();
	ASSERT_EQ(Job.m_Image.m_Format, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(Job.m_Image.m_pData, nullptr);
	EXPECT_EQ(mem_comp(Job.m_Image.m_pData, aPixel.data(), aPixel.size()), 0);
	EXPECT_EQ(Job.m_Image.m_pData[3], 255);
}

TEST(MapImageDecode, UnavailableCompressedDataKeepsSynchronousFallback)
{
	const auto Start = TryStartMapImageDecode(
		[] { return std::shared_ptr<CMapImageDecodeJob>{}; },
		[](const auto &) {
			ADD_FAILURE() << "No job should be submitted for synchronous fallback";
			return false;
		});
	EXPECT_EQ(Start.m_Result, SMapImageDecodeStart::EResult::NO_JOB);
	EXPECT_FALSE(Start.m_pJob);
}

TEST(MapImageDecode, AllocationFailureBeforeSubmissionLeavesNoJobToWaitFor)
{
	const auto Start = TryStartMapImageDecode(
		[]() -> std::shared_ptr<CMapImageDecodeJob> { throw std::bad_alloc(); },
		[](const auto &) {
			ADD_FAILURE() << "No job should be submitted when preparation fails";
			return false;
		});
	EXPECT_EQ(Start.m_Result, SMapImageDecodeStart::EResult::RESOURCE_FAILED);
	EXPECT_FALSE(Start.m_pJob);
}

TEST(MapImageDecode, ClosedPoolRejectsAndReleasesDecodeJob)
{
	CJobPool Pool;
	std::weak_ptr<CMapImageDecodeJob> pCandidate;
	const auto Start = TryStartMapImageDecode(
		[&] {
			auto pJob = std::make_shared<CMapImageDecodeJob>(nullptr, "map image", false);
			pCandidate = pJob;
			return pJob;
		},
		[&](const auto &pJob) { return Pool.TryAdd(pJob); });
	EXPECT_EQ(Start.m_Result, SMapImageDecodeStart::EResult::REJECTED);
	EXPECT_FALSE(Start.m_pJob);
	EXPECT_TRUE(pCandidate.expired());
}

TEST(MapImageDecode, SubmissionCapacityFailureReleasesDecodeJob)
{
	std::weak_ptr<CMapImageDecodeJob> pCandidate;
	const auto Start = TryStartMapImageDecode(
		[&] {
			auto pJob = std::make_shared<CMapImageDecodeJob>(nullptr, "map image", false);
			pCandidate = pJob;
			return pJob;
		},
		[](const auto &) -> bool { throw std::length_error("decode queue"); });
	EXPECT_EQ(Start.m_Result, SMapImageDecodeStart::EResult::RESOURCE_FAILED);
	EXPECT_FALSE(Start.m_pJob);
	EXPECT_TRUE(pCandidate.expired());
}

TEST(MapImageDecode, AcceptedJobIsReturnedToTheCaller)
{
	std::shared_ptr<CMapImageDecodeJob> pAccepted;
	const auto Start = TryStartMapImageDecode(
		[] { return std::make_shared<CMapImageDecodeJob>(nullptr, "map image", false); },
		[&](const auto &pJob) {
			pAccepted = pJob;
			return true;
		});
	EXPECT_EQ(Start.m_Result, SMapImageDecodeStart::EResult::STARTED);
	ASSERT_TRUE(Start.m_pJob);
	EXPECT_EQ(Start.m_pJob, pAccepted);
}
