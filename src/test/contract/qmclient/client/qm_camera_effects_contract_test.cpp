#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

TEST(QmCameraEffectsContract, CinematicCameraAndDynamicFovKeepScopedState)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Header = ReadTextFile("src/game/client/components/camera.h");
	const std::string Source = ReadTextFile("src/game/client/components/camera.cpp");
	const std::string OnRender = FunctionBody(Source, "void CCamera::OnRender()");
	const std::string ScaleZoom = FunctionBody(Source, "void CCamera::ScaleZoom(");
	const std::string ChangeZoom = FunctionBody(Source, "void CCamera::ChangeZoom(");
	const std::string UpdateCamera = FunctionBody(Source, "void CCamera::UpdateCamera()");
	const std::string OnReset = FunctionBody(Source, "void CCamera::OnReset()");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmCinematicCamera, qm_cinematic_camera"), std::string::npos);
	EXPECT_NE(Header.find("m_CinematicCameraSmoothing"), std::string::npos);
	EXPECT_NE(OnRender.find("GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition"), std::string::npos);
	EXPECT_NE(OnRender.find("if(g_Config.m_QmCinematicCamera)"), std::string::npos);
	EXPECT_NE(OnRender.find("m_CinematicCameraSmoothing = false;"), std::string::npos);
	EXPECT_NE(ScaleZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(ChangeZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(UpdateCamera.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(OnReset.find("m_DynamicFovAppliedFactor = 1.0f;"), std::string::npos);
	EXPECT_EQ(UpdateCamera.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] += m_DriftCurrentOffset;"), std::string::npos);
	EXPECT_NE(OnRender.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] + m_DriftCurrentOffset"), std::string::npos);
	EXPECT_NE(Header.find("float BaseZoom() const"), std::string::npos);
	EXPECT_NE(GameClient.find("m_Camera.BaseZoom()"), std::string::npos);
	EXPECT_EQ(GameClient.find("float ShowDistanceZoom = m_Camera.m_Zoom;"), std::string::npos);

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmZoomInstantReverse, qm_zoom_instant_reverse, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ChangeZoom.find("QmCameraEffects::ZoomDerivativeOnRetarget(Current, m_ZoomSmoothing.Derivative(Progress), Target, IsUser && g_Config.m_QmZoomInstantReverse != 0)"), std::string::npos);
}

TEST(QmCameraEffectsSource, CameraViewCardCountsInstantZoomReverseRow)
{
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");

	// 渲染、预布局输入、卡片高度三处必须同时带上新增开关，否则点击热区与卡片高度会错位
	EXPECT_NE(CameraView.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, \"Instant zoom reverse\", Localize(\"Instant zoom reverse\"), &g_Config.m_QmZoomInstantReverse);"), std::string::npos);
	EXPECT_NE(QmMenusSource.find("HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmZoomInstantReverse, &g_Config.m_QmZoomInstantReverse)"), std::string::npos);
	// EstimateContentHeight 的基础行数必须随新增一行从 5 变 6
	EXPECT_NE(QmMenusSource.find("return Rows(6.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;"), std::string::npos);
}
