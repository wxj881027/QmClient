#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmHudMediaIslandSource, RemovedTuningSatelliteDoesNotRemain)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string CountdownLogic = ReadTestSourceFile("src/game/client/components/hud_media_island_logic.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string IconHeader = ReadTestSourceFile("src/game/client/qm_icon_manager.h");
	const std::string IconSource = ReadTestSourceFile("src/game/client/qm_icon_manager.cpp");

	EXPECT_EQ(Source.find("TuneZoneEffect"), std::string::npos);
	EXPECT_EQ(Header.find("TuneZoneEffect"), std::string::npos);
	EXPECT_EQ(CountdownLogic.find("TUNE_ZONE"), std::string::npos);
	EXPECT_EQ(Menus.find("qmclient-dynamic-island-tune-zone-icon-legend"), std::string::npos);
	EXPECT_EQ(Config.find("QmHudIslandShowTuneZoneEffects"), std::string::npos);
	EXPECT_EQ(IconHeader.find("TUNE_GRAVITY"), std::string::npos);
	EXPECT_EQ(IconSource.find("tune-gravity"), std::string::npos);
}

TEST(QmHudMediaIslandSource, DynamicIslandUsesCompactSharedSpacing)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");

	EXPECT_NE(Source.find("QmHudMediaIslandScaled(2.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(3.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(7.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(5.0f)"), std::string::npos);
}

TEST(QmHudMediaIslandSource, MovesClockAndFrozenCountIntoStackAndReplacesClockSlotWithWaveform)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	const std::string VisibleBody = FunctionBody(Source, "void CHud::EnsureMediaIslandFrameCache() const");

	EXPECT_NE(RenderBody.find("constexpr float InfoStackGap = QmHudMediaIslandScaled(0.8f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandMirroredInfoStack"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandWaveBarHeight"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr int WaveBarCount = 7;"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr int WaveTargetBarCount = 6;"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandWaveBarSettleProgress"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr float WaveMaxHeight = QmHudMediaIslandScaled(7.2f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandTimerRows"), std::string::npos);
	EXPECT_NE(VisibleBody.find("BuildHudFrozenSummaryText"), std::string::npos);
	EXPECT_EQ(RenderBody.find("ShowFrozenSummaryInBottomRow"), std::string::npos);
	EXPECT_EQ(RenderBody.find("%s CP%d"), std::string::npos);
}

TEST(QmHudMediaIslandSource, DisablesNeteaseOnlyWorkWhenTheHookIsOff)
{
	const std::string HudSource = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string HudCacheBody = FunctionBody(HudSource, "void CHud::EnsureMediaIslandFrameCache() const");
	const std::string VisibleBody = FunctionBody(HudSource, "bool CHud::HasVisibleMediaIsland() const");
	const std::string AvoidanceBody = FunctionBody(HudSource, "float CHud::GetTopIslandAvoidanceRight() const");
	const std::string IntegrationSource = ReadTestSourceFile("src/game/client/components/qmclient/netease/netease_integration.cpp");
	const std::string IntegrationBody = FunctionBody(IntegrationSource, "void CNeteaseIntegration::OnUpdate()");

	EXPECT_NE(HudCacheBody.find("if(g_Config.m_QmNeteaseHookEnable != 0)"), std::string::npos);
	EXPECT_NE(VisibleBody.find("if(g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("if(g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	EXPECT_NE(IntegrationBody.find("if(!g_Config.m_QmNeteaseHookEnable)"), std::string::npos);
	EXPECT_NE(IntegrationBody.find("ClearForStaleMedia();"), std::string::npos);
	EXPECT_NE(FunctionBody(HudSource, "float CHud::RenderLegacyMediaInfoAt(float AnchorX, float CenterY)").find("EnsureMediaIslandFrameCache();"), std::string::npos);
}

TEST(QmHudMediaIslandSource, RenderPathKeepsStableNodesAndEditorRect)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string AnimResolveSource = ReadTestSourceFile("src/game/client/QmUi/QmAnimResolve.cpp");
	const size_t RenderBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(RenderBegin, std::string::npos);
	const size_t RenderEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", RenderBegin);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderBegin, RenderEnd - RenderBegin);

	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"cover_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"cover_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_title_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_title_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_meta_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_meta_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("StartCapsuleMorph()"), std::string::npos);
	EXPECT_NE(RenderBody.find("m_CapsuleMorphNeedsCapture"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandApplyCapsuleSqueeze"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"capsule_morph\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandResolveEntranceSprings"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"entrance_drop\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntranceSprings.m_ExpandProgress"), std::string::npos);
	EXPECT_NE(RenderBody.find("RelaxMediaIslandEntranceSprings"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandEntrancePose"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_BackgroundColor"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_ContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_DisabledCornerRadius"), std::string::npos);
	EXPECT_NE(RenderBody.find("CoverInAlpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("TrackTitleInAlpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("TimerCapsule.m_Alpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandDesiredBottomWidth("), std::string::npos);
	EXPECT_EQ(RenderBody.find("TextBoundingBox(BottomFontSize, aLyricsIslandBuf)"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandMarqueeOffset("), std::string::npos);
	EXPECT_NE(RenderBody.find("EnableMappedClip("), std::string::npos);
	EXPECT_NE(RenderBody.find("0.42f * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("SdfItem.m_ContentScale = Item.m_ContentScale * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("SatelliteIconSize * Item.m_ContentScale * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(AnimResolveSource.find("Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;"), std::string::npos);
	EXPECT_EQ(RenderBody.find("EUiAnimInterruptPolicy::QUEUE"), std::string::npos);
	EXPECT_EQ(RenderBody.find("m_CoverRotation"), std::string::npos);
	EXPECT_NE(RenderBody.find("m_MediaIslandLastVisibleRect = HudEditorScope.m_VisibleRect;"), std::string::npos);
	EXPECT_NE(RenderBody.find("BeginTransform(EHudEditorElement::MediaIsland, EditorTransformRect, EditorVisibleRect);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("QmHudIslandEdgeMargin"), std::string::npos);
	const std::string HudEditorSource = ReadTestSourceFile("src/game/client/components/hud_editor.cpp");
	// 灵动岛不再有独立的邻近吸附半径：所有 HUD 元素共用同一套「屏幕边只认重合」判定。
	EXPECT_EQ(HudEditorSource.find("MEDIA_ISLAND_EDGE_SNAP_DISTANCE"), std::string::npos);
	EXPECT_EQ(HudEditorSource.find("HudEditorEdgeSnapDistance"), std::string::npos);
	EXPECT_NE(HudEditorSource.find("const QmHudEditor::SSnapAxisResult SnapX = QmHudEditor::ResolveAxisSnapEx("), std::string::npos);
}

TEST(QmHudMediaIslandSource, IslandRendersBeforeCheckpointAndFinishEffects)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const size_t OnRenderBegin = Source.find("void CHud::OnRender()");
	ASSERT_NE(OnRenderBegin, std::string::npos);
	const size_t OnRenderEnd = Source.find("void CHud::OnMessage", OnRenderBegin);
	ASSERT_NE(OnRenderEnd, std::string::npos);
	const std::string OnRenderBody = Source.substr(OnRenderBegin, OnRenderEnd - OnRenderBegin);

	const size_t IslandRender = OnRenderBody.find("RenderMediaIsland();");
	const size_t EffectsRender = OnRenderBody.find("RenderDDRaceEffects();");
	ASSERT_NE(IslandRender, std::string::npos);
	ASSERT_NE(EffectsRender, std::string::npos);
	EXPECT_LT(IslandRender, EffectsRender);
}

TEST(QmHudMediaIslandSource, MediaIslandUsesGpuSdfCommandWithoutCpuRasterization)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);

	const size_t SdfDraw = IslandBody.find("Graphics()->RenderMediaIslandSdf");
	ASSERT_NE(SdfDraw, std::string::npos);
	EXPECT_EQ(IslandBody.find("Graphics()->RenderMediaIslandSdf", SdfDraw + 1), std::string::npos);
	EXPECT_NE(IslandBody.find("HasMediaIslandSdf"), std::string::npos);
	EXPECT_NE(Source.find("DrawMediaIslandArcGeometry"), std::string::npos);
	EXPECT_EQ(IslandBody.find("UpdateTexture"), std::string::npos);
	EXPECT_EQ(IslandBody.find("PixelX"), std::string::npos);
	EXPECT_EQ(IslandBody.find("PixelY"), std::string::npos);
	EXPECT_EQ(Source.find("m_vMediaIslandSdfPixels"), std::string::npos);
	EXPECT_EQ(Source.find("m_MediaIslandSdfTexture"), std::string::npos);
	EXPECT_EQ(IslandBody.find("BeginRenderTarget"), std::string::npos);
}

TEST(QmHudMediaIslandSource, BothLayoutPathsShareTheSameMainCapsuleReservation)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string AvoidanceBody = FunctionBody(Source, "float CHud::GetTopIslandAvoidanceRight() const");
	const std::string IslandBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");

	EXPECT_NE(Header.find("bool HasVisibleCountdownSatellite() const"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, ShowTeam, m_MediaIslandAnimState.HasVisibleCountdownSatellite())"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, ShowTeam, AnimState.HasVisibleCountdownSatellite())"), std::string::npos);
	// 回归护栏：状态区不再算主胶囊内容，观战卫星也不再作为保留依据。
	EXPECT_EQ(IslandBody.find("ShowTeam || ShowInfoStack"), std::string::npos);
	EXPECT_EQ(IslandBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, HasSpectatorSatellitePresentation"), std::string::npos);
}

TEST(QmHudPresentationSource, MediaIslandUsesContinuousPresentationState)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");

	EXPECT_NE(Source.find("#include <game/client/QmUi/QmAnimResolve.h>"), std::string::npos);
	EXPECT_EQ(Source.find("float ResolvePresentationStateValue("), std::string::npos);

	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);
	EXPECT_NE(IslandBody.find("ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode"), std::string::npos);
	EXPECT_NE(IslandBody.find("ResolveUiPresentationStateValue(AnimRuntime, CoverInNode"), std::string::npos);
	EXPECT_NE(IslandBody.find("BuildContentSpring(false)"), std::string::npos);
	EXPECT_NE(IslandBody.find("TrackExitSpring"), std::string::npos);
	EXPECT_NE(IslandBody.find("ExitTimeScale"), std::string::npos);
	EXPECT_EQ(IslandBody.find("EUiAnimInterruptPolicy::QUEUE"), std::string::npos);

	// 武器切换弹簧动画已移除：武器图标回到固定缩放与 40% 非当前透明度，
	// 旧的按时间戳启动动画的实现与新的 presentation state 都不应重新出现。
	EXPECT_EQ(Source.find("m_aHudWeaponSwitchStartTimes"), std::string::npos);
	EXPECT_EQ(Source.find("HudActiveWeaponSwitchScale"), std::string::npos);
	EXPECT_EQ(Source.find("HudWeaponPresentationNodeKey"), std::string::npos);
	EXPECT_EQ(Header.find("SHudWeaponPresentationState"), std::string::npos);
}
