// 截图网格的稳定 UI 合同：四列布局、缩略图缓存，以及双击大图入口。
#include <game/client/components/qmclient/screenshot_manager.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmScreenshotGalleryContract, UsesFourColumnsAndSeparateThumbnailCache)
{
	const std::string Gallery = ReadTextFile("src/game/client/components/menus_demo_screenshots.cpp");
	const std::string Manager = ReadTextFile("src/game/client/components/qmclient/screenshot_manager.h");
	const std::string Details = ReadTextFile("src/game/client/components/menus_demo.cpp");

	EXPECT_NE(Gallery.find("constexpr int SCREENSHOT_GALLERY_COLUMNS = 4;"), std::string::npos);
	EXPECT_NE(Gallery.find("DoStart(RowHeight, (int)m_vpFilteredDemos.size(), SCREENSHOT_GALLERY_COLUMNS"), std::string::npos);
	EXPECT_NE(Gallery.find("ToggleDemoScreenshotPreview"), std::string::npos);
	EXPECT_NE(Gallery.find("WasListboxItemActivated = false"), std::string::npos);
	EXPECT_NE(Manager.find("LoadThumbnail"), std::string::npos);
	EXPECT_NE(Manager.find("ClearThumbnails"), std::string::npos);
	EXPECT_NE(Details.find("窄面板内按纵向表单排列"), std::string::npos);
	EXPECT_EQ(Details.find("Left.VSplitMid(&Timestamp, &Map)"), std::string::npos);
}
