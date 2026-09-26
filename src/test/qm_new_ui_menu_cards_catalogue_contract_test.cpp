// QmNewUi 菜单源码合同：设置卡片目录、外观/图形页面卡片定义与激光预览。
// 卡片视觉表面合同见 qm_new_ui_menu_cards_surface_contract_test.cpp；
// 甲板状态/生命周期合同见 qm_new_ui_menu_cards_deck_contract_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuCardsCatalogueContract, NameplatePreviewRebuildsTextContainerInsteadOfAppendingSizes)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string Body = BlockBodyAfter(Source, "void Update(CGameClient &This, const CNamePlateData &Data) override");
	ASSERT_FALSE(Body.empty());

	const size_t DeletePos = Body.find("This.TextRender()->DeleteTextContainer(m_TextContainerIndex);");
	const size_t UpdateTextPos = Body.find("UpdateText(This, Data);");
	ASSERT_NE(DeletePos, std::string::npos);
	ASSERT_NE(UpdateTextPos, std::string::npos);
	EXPECT_LT(DeletePos, UpdateTextPos);
	EXPECT_EQ(Body.find("else\n\t\t{\n\t\t\tUpdateText(This, Data);\n\t\t}"), std::string::npos);
	EXPECT_NE(Body.find("QmNameplateTextEffectPadding"), std::string::npos);
}

TEST(QmNewUiMenuCardsCatalogueContract, NameplateTextEffectsReserveTheirRenderedExtent)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(0, 4, 12), 0.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 1, 12), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 8, 12), 4.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 0), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 7), 7.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 3, 7), 7.0f);

	const std::string QmConfigHeader = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string AppearanceSettings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextGlowRange, qm_nameplate_text_glow_range, 4, 1, 12"), std::string::npos);
	EXPECT_NE(AppearanceSettings.find("Localize(\"Glow range\"), 1, 12"), std::string::npos);
}

TEST(QmNewUiMenuCardsCatalogueContract, QmLaserSettingsMovedToAppearanceLaserTab)
{
	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string VisualDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(VisualDeck.empty());
	EXPECT_EQ(VisualDeck.find("qm:laser"), std::string::npos);
	EXPECT_EQ(VisualDeck.find("qm:nameplate_text"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LaserBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(LaserBranch.empty());
	EXPECT_NE(LaserBranch.find("AddCard(10, ResolveLaserEnhancedMinCardHeight()"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(11, LaserColorMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(12, LaserPreviewMinCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(EnhancedCard, QmCardStyle);"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QmResolveScrollPolicy(ScrollRequest, AppearanceUiScale"), std::string::npos);
	EXPECT_NE(SettingsSource.find("std::array<CScrollRegion, NUMBER_OF_APPEARANCE_TABS> s_AppearanceSettingsCardScrollRegions"), std::string::npos);
	EXPECT_NE(SettingsSource.find("CQmScrollState &ScrollState = s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab].State()"), std::string::npos);
	EXPECT_NE(SettingsSource.find("SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float EnhancedContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float ColorContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float PreviewContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredEnhancedCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredColorCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredPreviewCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("const auto ResolveLaserEnhancedMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0)"), std::string::npos);
	EXPECT_NE(LaserBranch.find("vCards.back().m_PreLayoutInput"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("appearance-laser-enhancement-title"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserGlowIntensity"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserSize"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserAlpha"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserRoundCaps"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseSpeed"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseAmplitude"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);"), std::string::npos);
}

TEST(QmNewUiMenuCardsCatalogueContract, SettingsCardDeckSharedComponentExposesGraphicsAndAppearanceCatalogue)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");

	const std::string RenderSettingsGraphics = FunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsGraphics.empty());
	EXPECT_NE(RenderSettingsGraphics.find("const SSettingsPageLayoutFrame GraphicsPage = SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-display\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-visual\")"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-backend\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-modes\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-interaction\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("if(FoundBackendCount > 1)"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("DoGraphicsChoiceRow"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_BackendDropDownState"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiListEntryAnimations, \"settings-card-list-entry-animations\", Localize(\"Card list entry animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardHeightAnimations, \"settings-card-height-animations\", Localize(\"Card height animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardReflowAnimations, \"settings-card-reflow-animations\", Localize(\"Card reflow animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmExtraAnimations, \"presentation-animations\", Localize(\"Presentation animations\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardRainbowTitles, \"rainbow-card-titles\", Localize(\"Rainbow card titles\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardBorders, \"show-settings-card-borders\", Localize(\"Show settings card borders\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Localize(\"Settings card border color\"), &g_Config.m_QmUiCardBorderColor"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("DoLine_AlphaColorPicker(&s_UiCardColorResetId, ColorMetrics, &UiCardColorRow, Localize(\"Settings card background\"), &g_Config.m_QmUiCardColor, &g_Config.m_QmUiCardOpacity"), std::string::npos);
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmUiCardColor"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("DoGraphicsNumericField(\"graphics-card-corner-segments\""), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("RenderQmVisualCardAppearanceContent"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("int RowsRemaining = 6;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("DoSettingsLabel(SETTINGS_GRAPHICS, -1, \"graphics-ui-motion-level-label\""), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const char *apMotionLabels[] = {Localize(\"Off\"), Localize(\"Reduced\"), Localize(\"Full\")}"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(DisplaySpec, GraphicsDisplayMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(VisualSpec, GraphicsVisualMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(InteractionSpec, GraphicsInteractionMinCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("AddCard(BackendSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(ModesSpec, GraphicsModesMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("ResolveSettingsGraphicsModesGeometry("), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const int GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("GraphicsPage.m_ScrollViewport.h - GraphicsPage.m_CardGap"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsVisualContentHeight = ResolveSettingsContentFlowHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsInteractionContentHeight = ResolveSettingsContentFlowHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const uint64_t GraphicsDisplayMeasureRevision ="), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("GraphicsDisplayMeasureRevision"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsInteractionMinCardHeight = InteractionChromeHeight + GraphicsInteractionContentHeight;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("GraphicsModesTargetContentHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const uint64_t GraphicsModesMeasureRevision = static_cast<uint64_t>(std::max(0, s_NumNodes));"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("ScreenDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("WindowModeDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("settings-graphics-modes-height"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("GraphicsModesHeightAnimationActive"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("MotionRow.VSplitLeft"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("static CUi::SDropDownState s_BackendDropDownState;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("static CUi::SDropDownState s_GpuDropDownState;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("static CUi::SDropDownState s_State;"), std::string::npos);
	const size_t ModesCard = RenderSettingsGraphics.find("AddCard(ModesSpec");
	const size_t DisplayCard = RenderSettingsGraphics.find("AddCard(DisplaySpec");
	const size_t DisplayEnd = RenderSettingsGraphics.find("AddCard(VisualSpec", DisplayCard);
	ASSERT_NE(ModesCard, std::string::npos);
	ASSERT_NE(DisplayCard, std::string::npos);
	ASSERT_NE(DisplayEnd, std::string::npos);
	EXPECT_LT(ModesCard, DisplayCard);
	const size_t WindowMode = RenderSettingsGraphics.find("s_WindowModeDropDownState", ModesCard);
	ASSERT_NE(WindowMode, std::string::npos);
	EXPECT_LT(WindowMode, DisplayCard);
	EXPECT_EQ(RenderSettingsGraphics.find("UpdateMeasuredCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("s_GraphicsInteractionCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("SaveSettingsCardOrderModel();"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("\"graphics-renderer-title\""), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("MainView.VSplitLeft(OptionsBlockWidth"), std::string::npos);

	const std::string RenderSettingsAppearance = FunctionBody(SettingsSource, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsAppearance.empty());
	EXPECT_NE(RenderSettingsAppearance.find("const char *const aAppearanceIds[]"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("qm_card_registry::FindByStableId"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("Definition.m_Spec = Spec;"), std::string::npos);
	EXPECT_EQ(RenderSettingsAppearance.find("UpdateMeasuredCardHeight"), std::string::npos);
}

TEST(QmNewUiMenuCardsCatalogueContract, LaserPreviewEntityBranchesReserveEndpointDecorationSpace)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoLaserPreview = FunctionBody(Source, "void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)");

	EXPECT_NE(DoLaserPreview.find("const vec2 EntityLaserEnd = LaserType == LASERTYPE_DOOR ? Pos - vec2(34.0f, 0.0f) : Pos - vec2(42.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos - vec2(20.0f, 0.0f));"), std::string::npos);
	EXPECT_EQ(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos);"), std::string::npos);
}

TEST(QmNewUiMenuCardsCatalogueContract, LaserEntityTypesUseDdnetEndpointRendering)
{
	const std::string Source = ReadTextFile("src/game/client/components/items.cpp");
	const std::string RenderLaser = FunctionBody(Source, "void CItems::RenderLaser(vec2 From, vec2 Pos, ColorRGBA OuterColor, ColorRGBA InnerColor, float TicksBody, float TicksHead, int Type, float GlowIntensity) const");
	ASSERT_FALSE(RenderLaser.empty());

	// Regression guard for DDNet entity beams: door/freeze/dragger are carried by
	// the laser render path, but they are not weapon lasers and must not receive
	// generic impact-splat endpoints. The endpoint side is part of the behavior:
	// door blocker at Pos only, dragger pulley at From, freeze hectagon at Pos.
	const size_t DoorBranchPos = RenderLaser.find("if(Type == LASERTYPE_DOOR)");
	const size_t DraggerBranchPos = RenderLaser.find("else if(Type == LASERTYPE_DRAGGER)");
	const size_t FreezeBranchPos = RenderLaser.find("else if(Type == LASERTYPE_FREEZE)");
	const size_t GenericHeadPos = RenderLaser.find("else\n\t{", FreezeBranchPos);
	ASSERT_NE(DoorBranchPos, std::string::npos);
	ASSERT_NE(DraggerBranchPos, std::string::npos);
	ASSERT_NE(FreezeBranchPos, std::string::npos);
	ASSERT_NE(GenericHeadPos, std::string::npos);
	EXPECT_LT(DoorBranchPos, GenericHeadPos);
	EXPECT_LT(DraggerBranchPos, GenericHeadPos);
	EXPECT_LT(FreezeBranchPos, GenericHeadPos);

	const std::string DoorBranch = RenderLaser.substr(DoorBranchPos, DraggerBranchPos - DoorBranchPos);
	const std::string DraggerBranch = RenderLaser.substr(DraggerBranchPos, FreezeBranchPos - DraggerBranchPos);
	const std::string FreezeBranch = RenderLaser.substr(FreezeBranchPos, GenericHeadPos - FreezeBranchPos);
	const std::string GenericHeadBranch = RenderLaser.substr(GenericHeadPos);
	EXPECT_NE(DoorBranch.find("m_DoorHeadOffset"), std::string::npos);
	EXPECT_NE(DoorBranch.find("Pos.x - 8.0f, Pos.y - 8.0f"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("From.x"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("GameClient()->m_ExtrasSkin.m_SpritePulley"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("m_PulleyHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_PulleyHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("GameClient()->m_ExtrasSkin.m_SpriteHectagon"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("m_FreezeHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_FreezeHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_NE(GenericHeadBranch.find("m_aParticleSplatOffset[CurParticle]"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_aParticleSplatOffset"), std::string::npos);
}
