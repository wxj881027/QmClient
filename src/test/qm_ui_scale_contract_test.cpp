#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

TEST(QmUiScaleContract, ScaleChangesResetContainersAndInvalidateScaleKeys)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string TClientMenusSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Update = FunctionBody(UiSource, "void CUi::Update(");
	const std::string Screen = FunctionBody(UiSource, "const CUIRect *CUi::Screen()");
	const std::string QmUiScaleHelper = FunctionBody(QmMenusSource, "void CMenus::RenderQmSettingsSliderWithValueInput(");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmUiScale, qm_ui_scale, 100, 50, 200"), std::string::npos);
	EXPECT_NE(Update.find("Client()->OnWindowResize();"), std::string::npos);
	EXPECT_NE(Screen.find("QmUiVirtualScreenHeight(g_Config.m_QmUiScale)"), std::string::npos);
	EXPECT_NE(MenusSource.find("StyleKey.m_UiScaleBucket = std::clamp(g_Config.m_QmUiScale, 50, 200);"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("std::clamp(g_Config.m_QmUiScale, 50, 200)"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_Flags = Flags;"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ?"), std::string::npos);
	EXPECT_NE(CameraView.find("RenderValue(\"qmclient-ui-scale\", \"UI scale\", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, \"%\", CUi::SCROLLBAR_OPTION_DELAYUPDATE);"), std::string::npos);
}
