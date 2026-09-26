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
	EXPECT_NE(SlicePopup.find("str_format(m_aPendingDemoRenderSelectionName, sizeof(m_aPendingDemoRenderSelectionName), \"%s.demo\", m_DemoSliceInput.GetString());"), std::string::npos);
	EXPECT_EQ(SlicePopup.find("str_copy(m_aPendingDemoRenderSelectionName, m_aCurrentDemoSelectionName"), std::string::npos);
}
