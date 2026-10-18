#include <test/support/qm_real_ui_fixture.h>

#include <algorithm>
#include <limits>

class QmTextBitmap : public qm_ui_test::CRealUiFixture
{
protected:
	std::vector<uint8_t> Render(const char *pText, int Length)
	{
		std::vector<uint8_t> vPixels(64 * 64 * 4, 0);
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 64;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = vPixels.data();
		m_Ui.TextRender()->UploadEntityLayerText(Image, 64, 64, pText, Length, 0, 0, 16);
		return vPixels;
	}
};

TEST_F(QmTextBitmap, TerminatorPreventsDrawingTheFollowingBytes)
{
	const char aText[] = {'A', '\0', 'B', '\0'};
	const auto vPrefix = Render("A", 1);
	ASSERT_TRUE(std::any_of(vPrefix.begin(), vPrefix.end(), [](uint8_t Pixel) { return Pixel != 0; }));
	EXPECT_EQ(Render(aText, 3), vPrefix);
}

TEST_F(QmTextBitmap, PartialUtf8SuffixIsNotDrawnPastTheByteLimit)
{
	EXPECT_EQ(Render("Aö", 2), Render("A", 1));
	EXPECT_NE(Render("Aö", 3), Render("A", 1));
}

TEST_F(QmTextBitmap, OversizedSubregionCannotWriteOutsideTheImage)
{
	// 实际图像只有一个像素；后面的可观察保护区不得被字形写入。
	std::vector<uint8_t> vPixels(4096, 0x7f);
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	m_Ui.TextRender()->UploadEntityLayerText(Image, 64, 64, "A", 1, 0, 0, 16);
	EXPECT_EQ(vPixels[0], 255);
	EXPECT_TRUE(std::all_of(vPixels.begin() + 4, vPixels.end(), [](uint8_t Pixel) { return Pixel == 0x7f; }));
}

TEST_F(QmTextBitmap, InvalidDestinationsAndEmptyExtentsLeavePixelsUnchanged)
{
	std::vector<uint8_t> vPixels(32 * 32 * 4, 0x7f);
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 32;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	auto *pText = m_Ui.TextRender();
	pText->UploadEntityLayerText(Image, 0, 32, "A", 1, 0, 0, 16);
	pText->UploadEntityLayerText(Image, 32, -1, "A", 1, 0, 0, 16);
	pText->UploadEntityLayerText(Image, 32, 32, "A", 1, -1, 0, 16);
	pText->UploadEntityLayerText(Image, 32, 32, "A", 1, 32, 0, 16);
	pText->UploadEntityLayerText(Image, 32, 32, "A", 1, 0, std::numeric_limits<float>::quiet_NaN(), 16);
	pText->UploadEntityLayerText(Image, 32, 32, "A", 1, std::numeric_limits<float>::infinity(), 0, 16);
	EXPECT_TRUE(std::all_of(vPixels.begin(), vPixels.end(), [](uint8_t Pixel) { return Pixel == 0x7f; }));
}

TEST_F(QmTextBitmap, MissingPixelsAndInvalidFormatsAreIgnored)
{
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 32;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	m_Ui.TextRender()->UploadEntityLayerText(Image, 32, 32, "A", 1, 0, 0, 16);
	std::vector<uint8_t> vPixels(32 * 32 * 4, 0x7f);
	Image.m_pData = vPixels.data();
	Image.m_Format = CImageInfo::FORMAT_UNDEFINED;
	m_Ui.TextRender()->UploadEntityLayerText(Image, 32, 32, "A", 1, 0, 0, 16);
	EXPECT_TRUE(std::all_of(vPixels.begin(), vPixels.end(), [](uint8_t Pixel) { return Pixel == 0x7f; }));
}
