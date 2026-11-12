#include <game/client/components/qmclient/screenshot_clipboard.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	CImageInfo ImageView(uint8_t *pPixels, size_t Width, size_t Height, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGB)
	{
		CImageInfo Image;
		Image.m_pData = pPixels;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = Format;
		return Image;
	}
}

TEST(QmScreenshotClipboard, RgbDibHasBitmapHeaderAndBottomUpBgrRows)
{
	uint8_t aPixels[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
	const CImageInfo Image = ImageView(aPixels, 2, 2);
	std::vector<uint8_t> vDib(qm_screenshot_clipboard::DibSize(Image), 0xcc);
	ASSERT_EQ(vDib.size(), 56u);
	ASSERT_TRUE(qm_screenshot_clipboard::WriteDib(Image, vDib.data(), vDib.size()));
	const std::array<uint8_t, 40> aExpectedHeader = {
		40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
		0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0};
	EXPECT_EQ(std::vector<uint8_t>(vDib.begin(), vDib.begin() + 40), std::vector<uint8_t>(aExpectedHeader.begin(), aExpectedHeader.end()));
	EXPECT_EQ(std::vector<uint8_t>(vDib.begin() + 40, vDib.end()), (std::vector<uint8_t>{255, 0, 0, 255, 255, 255, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0}));
}

TEST(QmScreenshotClipboard, RgbaIgnoresFramebufferAlphaAndPreservesSourcePixels)
{
	std::array<uint8_t, 8> aPixels = {12, 34, 56, 0, 78, 90, 123, 128};
	const auto Original = aPixels;
	const CImageInfo Image = ImageView(aPixels.data(), 2, 1, CImageInfo::FORMAT_RGBA);
	std::vector<uint8_t> vDib(qm_screenshot_clipboard::DibSize(Image));
	ASSERT_TRUE(qm_screenshot_clipboard::WriteDib(Image, vDib.data(), vDib.size()));
	EXPECT_EQ(std::vector<uint8_t>(vDib.begin() + 40, vDib.end()), (std::vector<uint8_t>{56, 34, 12, 123, 90, 78, 0, 0}));
	EXPECT_EQ(aPixels, Original);
}

TEST(QmScreenshotClipboard, RowPaddingRoundsUpToFourBytes)
{
	std::array<uint8_t, 12> aPixels = {};
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(aPixels.data(), 1, 1)), 44u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(aPixels.data(), 2, 1)), 48u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(aPixels.data(), 3, 1)), 52u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(aPixels.data(), 4, 1)), 52u);
}

TEST(QmScreenshotClipboard, InvalidImageDoesNotWriteToDestination)
{
	uint8_t aPixels[4] = {};
	const std::array<CImageInfo, 5> aImages = {
		ImageView(nullptr, 1, 1), ImageView(aPixels, 0, 1), ImageView(aPixels, 1, 0),
		ImageView(aPixels, 1, 1, CImageInfo::FORMAT_UNDEFINED), ImageView(aPixels, 1, 1, CImageInfo::FORMAT_R)};
	for(size_t Index = 0; Index < aImages.size(); ++Index)
	{
		SCOPED_TRACE(Index);
		std::array<uint8_t, 44> aTarget;
		aTarget.fill(0xcc);
		const auto Original = aTarget;
		EXPECT_EQ(qm_screenshot_clipboard::DibSize(aImages[Index]), 0u);
		EXPECT_FALSE(qm_screenshot_clipboard::WriteDib(aImages[Index], aTarget.data(), aTarget.size()));
		EXPECT_EQ(aTarget, Original);
	}
}

TEST(QmScreenshotClipboard, OversizedDimensionsAreRejectedBeforeReadingPixels)
{
	uint8_t Pixel = 0;
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(&Pixel, (size_t)std::numeric_limits<int32_t>::max() + 1, 1)), 0u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(&Pixel, 1, (size_t)std::numeric_limits<int32_t>::max() + 1)), 0u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(&Pixel, 100000, 100000)), 0u);
	EXPECT_EQ(qm_screenshot_clipboard::DibSize(ImageView(&Pixel, std::numeric_limits<size_t>::max(), 2)), 0u);
}

TEST(QmScreenshotClipboard, SmallDestinationRemainsUnchanged)
{
	uint8_t aPixels[3] = {};
	const CImageInfo Image = ImageView(aPixels, 1, 1);
	std::array<uint8_t, 43> aTarget;
	aTarget.fill(0xcc);
	const auto Original = aTarget;
	EXPECT_FALSE(qm_screenshot_clipboard::WriteDib(Image, aTarget.data(), aTarget.size()));
	EXPECT_FALSE(qm_screenshot_clipboard::WriteDib(Image, nullptr, 44));
	EXPECT_EQ(aTarget, Original);
}

TEST(QmScreenshotClipboard, EnabledCaptureCopiesBeforePreviewChangesPixels)
{
	uint8_t aPixels[] = {12, 34, 56};
	CImageInfo Image = ImageView(aPixels, 1, 1);
	std::vector<int> vCalls;
	auto Callback = qm_screenshot_clipboard::CaptureCallback(true, [&](CImageInfo &&Taken, const char *pPath) {
			vCalls.push_back(2);
			EXPECT_STREQ(pPath, "screenshots/manual.png");
			EXPECT_EQ(Taken.m_pData, aPixels);
			Taken.m_pData[0] = 99; }, [&](const CImageInfo &Taken) {
			vCalls.push_back(1);
			EXPECT_EQ(Taken.m_pData[0], 12);
			return true; });
	Callback(std::move(Image), "screenshots/manual.png");
	EXPECT_EQ(vCalls, (std::vector<int>{1, 2}));
}

TEST(QmScreenshotClipboard, DisabledCaptureStillDeliversPreviewWithoutCopying)
{
	uint8_t aPixels[3] = {};
	CImageInfo Image = ImageView(aPixels, 1, 1);
	int CopyCalls = 0;
	int PreviewCalls = 0;
	auto Callback = qm_screenshot_clipboard::CaptureCallback(false, [&](CImageInfo &&, const char *) { ++PreviewCalls; }, [&](const CImageInfo &) { ++CopyCalls; return true; });
	Callback(std::move(Image), "screenshot.png");
	EXPECT_EQ(CopyCalls, 0);
	EXPECT_EQ(PreviewCalls, 1);
}

TEST(QmScreenshotClipboard, CopyFailureStillDeliversPreviewAndPath)
{
	uint8_t aPixels[3] = {};
	CImageInfo Image = ImageView(aPixels, 1, 1);
	bool PreviewCalled = false;
	auto Callback = qm_screenshot_clipboard::CaptureCallback(true, [&](CImageInfo &&Taken, const char *pPath) {
			PreviewCalled = true;
			EXPECT_EQ(Taken.m_pData, aPixels);
			EXPECT_STREQ(pPath, "screenshot.png"); }, [](const CImageInfo &) { return false; });
	Callback(std::move(Image), "screenshot.png");
	EXPECT_TRUE(PreviewCalled);
}

TEST(QmScreenshotClipboard, CaptureKeepsRequestedOptionAndSupportsRepeatedCalls)
{
	bool Enabled = true;
	int CopyCalls = 0;
	int PreviewCalls = 0;
	auto Callback = qm_screenshot_clipboard::CaptureCallback(Enabled, [&](CImageInfo &&, const char *) { ++PreviewCalls; }, [&](const CImageInfo &) { ++CopyCalls; return true; });
	Enabled = false;
	uint8_t aPixels[3] = {};
	CImageInfo First = ImageView(aPixels, 1, 1);
	CImageInfo Second = ImageView(aPixels, 1, 1);
	Callback(std::move(First), "first.png");
	Callback(std::move(Second), "second.png");
	EXPECT_FALSE(Enabled);
	EXPECT_EQ(CopyCalls, 2);
	EXPECT_EQ(PreviewCalls, 2);
}
