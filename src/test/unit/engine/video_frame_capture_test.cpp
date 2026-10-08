#include <engine/client/video_frame_capture.h>

#include <gtest/gtest.h>

namespace
{
	TGLBackendReadPresentedImageData FrameReader(uint32_t Width, uint32_t Height, CImageInfo::EImageFormat Format, size_t Bytes)
	{
		return [=](uint32_t &OutWidth, uint32_t &OutHeight, CImageInfo::EImageFormat &OutFormat, std::vector<uint8_t> &vData) {
			OutWidth = Width;
			OutHeight = Height;
			OutFormat = Format;
			vData.assign(Bytes, 42);
			return true;
		};
	}
}

TEST(VideoFrameCapture, RepeatedRgbaFramesRemainAvailableForEncoding)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	const auto Reader = FrameReader(2, 2, CImageInfo::FORMAT_RGBA, 16);
	for(int Frame = 0; Frame < 3; ++Frame)
	{
		ASSERT_TRUE(Capture.Read(Reader, vData));
		EXPECT_FALSE(Capture.HasError());
		EXPECT_EQ(vData.size(), 16u);
		EXPECT_EQ(vData.front(), 42);
	}
}

TEST(VideoFrameCapture, ReadFailureRejectsStaleDataWithoutInspectingUnwrittenMetadata)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData(16, 42);
	const TGLBackendReadPresentedImageData Reader = [](uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) { return false; };
	EXPECT_FALSE(Capture.Read(Reader, vData));
	EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::READ_FAILED);
}

TEST(VideoFrameCapture, MissingBackendReaderBecomesARecordingError)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Capture.Read({}, vData));
	EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::NO_READER);
}

TEST(VideoFrameCapture, ChangedOrZeroDimensionsRejectTheFrame)
{
	const ivec2 aSizes[] = {{0, 0}, {1, 2}, {2, 1}, {3, 2}};
	for(const auto &Size : aSizes)
	{
		SCOPED_TRACE(::testing::Message() << Size.x << "x" << Size.y);
		CVideoFrameCapture Capture(2, 2);
		std::vector<uint8_t> vData;
		EXPECT_FALSE(Capture.Read(FrameReader(Size.x, Size.y, CImageInfo::FORMAT_RGBA, 16), vData));
		EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::SIZE_MISMATCH);
	}
}

TEST(VideoFrameCapture, NonRgbaPixelsAreNotPassedToTheEncoder)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Capture.Read(FrameReader(2, 2, CImageInfo::FORMAT_RGB, 12), vData));
	EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::FORMAT_MISMATCH);
}

TEST(VideoFrameCapture, TruncatedPixelBufferIsRejected)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	EXPECT_FALSE(Capture.Read(FrameReader(2, 2, CImageInfo::FORMAT_RGBA, 15), vData));
	EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::INCOMPLETE_DATA);
}

TEST(VideoFrameCapture, BackendScratchRowsDoNotInvalidateCompletePixels)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	EXPECT_TRUE(Capture.Read(FrameReader(2, 2, CImageInfo::FORMAT_RGBA, 24), vData));
	EXPECT_FALSE(Capture.HasError());
}

TEST(VideoFrameCapture, FailureStopsFurtherReadsUntilANewRecordingIsCreated)
{
	CVideoFrameCapture Capture(2, 2);
	std::vector<uint8_t> vData;
	int Reads = 0;
	const TGLBackendReadPresentedImageData Reader = [&](uint32_t &, uint32_t &, CImageInfo::EImageFormat &, std::vector<uint8_t> &) {
		++Reads;
		return false;
	};
	EXPECT_FALSE(Capture.Read(Reader, vData));
	EXPECT_FALSE(Capture.Read(Reader, vData));
	EXPECT_EQ(Reads, 1);
	EXPECT_EQ(Capture.Error(), CVideoFrameCapture::EError::READ_FAILED);

	CVideoFrameCapture NewCapture(2, 2);
	EXPECT_TRUE(NewCapture.Read(FrameReader(2, 2, CImageInfo::FORMAT_RGBA, 16), vData));
	EXPECT_FALSE(NewCapture.HasError());
}
