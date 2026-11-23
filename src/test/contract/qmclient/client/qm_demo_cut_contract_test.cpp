#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmDemoCutContract, UsesExportedCutAsRenderSource)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/menus_demo.cpp");
	const size_t Start = Source.find("void CMenus::RenderDemoPlayerSliceSavePopup(CUIRect MainView)");
	ASSERT_NE(Start, std::string::npos);
	const size_t End = Source.find("\nvoid CMenus::", Start + 1);
	ASSERT_NE(End, std::string::npos);
	const std::string SlicePopup = Source.substr(Start, End - Start);
	// 导出源直接取已选中的切片文件，不再按输入框拼 .demo 名称。
	EXPECT_NE(SlicePopup.find("m_HasPendingDemoRenderSource = true;"), std::string::npos);
	EXPECT_NE(SlicePopup.find("str_copy(m_aPendingDemoRenderSelectionName, pFilename);"), std::string::npos);
	EXPECT_EQ(SlicePopup.find("str_copy(m_aPendingDemoRenderSelectionName, m_aCurrentDemoSelectionName"), std::string::npos);
}
