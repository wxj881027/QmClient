#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

TEST(QmCameraAspectRatioContract, KeepsUiAspectPhysicalAndOverridesOnlyGameWorld)
{
	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string GraphicsSource = ReadTextFile("src/engine/graphics.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ControlsSource = ReadTextFile("src/game/client/components/controls.cpp");
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string TouchControlsSource = ReadTextFile("src/game/client/components/touch_controls.cpp");
	const std::string CollisionHitboxSource = ReadTextFile("src/game/client/components/qmclient/collision_hitbox.cpp");
	const std::string BackgroundParticlesSource = ReadTextFile("src/game/client/components/tclient/background_particles.cpp");
	const std::string RenderLayerSource = ReadTextFile("src/game/map/render_layer.cpp");
	const std::string MapRendererSource = ReadTextFile("src/game/map/map_renderer.cpp");
	const std::string MovingTilesSource = ReadTextFile("src/game/client/components/tclient/moving_tiles.cpp");
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");

	EXPECT_NE(GraphicsHeader.find("float ScreenAspect() const { return (float)ScreenWidth() / (float)ScreenHeight(); }"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("float GameScreenAspect() const { return m_GameScreenAspectOverride > 0.0f ? m_GameScreenAspectOverride : ScreenAspect(); }"), std::string::npos);
	EXPECT_NE(TClientSource.find("Graphics()->SetGameScreenAspectOverride(GameScreenAspectOverride);"), std::string::npos);
	EXPECT_EQ(TClientSource.find("SetScreenAspectOverride"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MapRendererSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MovingTilesSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("This.Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), ShowDistanceZoom"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_LastScreenAspect = Graphics()->GameScreenAspect();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(ControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), 1.0f"), std::string::npos);
	EXPECT_NE(HudSource.find("CalcScreenParams(pGraphics->GameScreenAspect(), GameClient.m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), MiniZoom, aPoints"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), 1.0f, aPoints"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(m_pTouchControls->Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), Zoom"), std::string::npos);
	EXPECT_NE(CollisionHitboxSource.find("Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(BackgroundParticlesSource.find("Graphics()->GameScreenAspect(), Zoom, aPoints"), std::string::npos);

	const std::string MapScreenToInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToInterface(");
	const std::string MapScreenToGameInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToGameInterface(");
	EXPECT_NE(MapScreenToInterface.find("ScreenAspect()"), std::string::npos);
	EXPECT_EQ(MapScreenToInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(MapScreenToGameInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(UiSource.find("Graphics()->ScreenAspect()"), std::string::npos);
	EXPECT_EQ(UiSource.find("GameScreenAspect()"), std::string::npos);
}
