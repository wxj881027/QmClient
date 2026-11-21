#include <game/client/components/menus.h>

#include <gtest/gtest.h>

namespace
{
	CImageInfo ComposeOnePixel()
	{
		uint8_t aPixel[4] = {10, 20, 30, 255};
		CImageInfo Source;
		Source.m_Width = 1;
		Source.m_Height = 1;
		Source.m_Format = CImageInfo::FORMAT_RGBA;
		Source.m_pData = aPixel;
		return Source.DeepCopy();
	}
}

TEST(QmAssetsEditorExportComposition, RejectsAndFreesTheImageWhenAnEditedSlotWasSkipped)
{
	CImageInfo Image = ComposeOnePixel();
	ASSERT_NE(Image.m_pData, nullptr);

	EXPECT_FALSE(qmclient::assets_editor::KeepCompleteExportImage(Image, 1));
	EXPECT_EQ(Image.m_pData, nullptr);
	Image.Free();
}

TEST(QmAssetsEditorExportComposition, LeavesCompleteImageAvailableForExport)
{
	CImageInfo Image = ComposeOnePixel();
	ASSERT_NE(Image.m_pData, nullptr);
	const uint8_t *pPixels = Image.m_pData;

	EXPECT_TRUE(qmclient::assets_editor::KeepCompleteExportImage(Image, 0));
	EXPECT_EQ(Image.m_pData, pPixels);
	Image.Free();
}
