// QmNewUi 菜单源码合同：gameplay 高斯模糊背景覆盖范围。运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuGameplayBlurContract, GaussianBlurSkipsPageSwitchTransitionOverlays)
{
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string IngameSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string TouchControlsSource = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Settings7Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string TransitionSources = MenusSource + BrowserSource + IngameSource + SettingsSource + Settings7Source + QmClientSource + TClientSource;
	const std::string TransitionOverlay = FunctionBody(MenusSource, "void CMenus::DrawUiSwitchTransitionOverlay(");
	const std::string TouchButtonEditor = FunctionBody(TouchControlsSource, "void CMenusIngameTouchControls::RenderTouchButtonEditor(");
	const std::regex DirectTransitionOverlayDraw(R"(\b[A-Za-z_][A-Za-z0-9_]*\.Draw\([^\n]*(TransitionAlpha|TabTransitionAlpha))");

	EXPECT_NE(MenusHeader.find("void DrawUiSwitchTransitionOverlay(const CUIRect &Rect, ColorRGBA Color);"), std::string::npos);
	EXPECT_NE(TransitionOverlay.find("CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());"), std::string::npos);
	EXPECT_NE(TransitionOverlay.find("Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);"), std::string::npos);
	EXPECT_FALSE(std::regex_search(TransitionSources, DirectTransitionOverlayDraw));
	EXPECT_NE(TouchButtonEditor.find("CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());"), std::string::npos);
	EXPECT_NE(TouchButtonEditor.find("BlockClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)"), std::string::npos);
	EXPECT_NE(BrowserSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_NE(IngameSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(SettingsSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(Settings7Source.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(QmClientSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(TClientSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
}

TEST(QmNewUiMenuGameplayBlurContract, EnvelopeScaleHotkeyGuardsModalInput)
{
	const std::string Source = ReadTextFile("src/game/editor/envelope_editor.cpp");
	const std::string Render = FunctionBody(Source, "void CEnvelopeEditor::Render(CUIRect View)");

	const size_t ScaleOperation = Render.find("m_Operation == EEnvelopeEditorOp::OP_NONE");
	ASSERT_NE(ScaleOperation, std::string::npos);
	const size_t ScaleBody = Render.find("m_Operation = EEnvelopeEditorOp::OP_SCALE;", ScaleOperation);
	ASSERT_NE(ScaleBody, std::string::npos);
	const std::string ScaleGuard = Render.substr(ScaleOperation, ScaleBody - ScaleOperation);
	EXPECT_NE(ScaleGuard.find("Editor()->m_Dialog == DIALOG_NONE"), std::string::npos);
	EXPECT_NE(ScaleGuard.find("!Ui()->IsPopupOpen()"), std::string::npos);
	EXPECT_NE(ScaleGuard.find("CLineInput::GetActiveInput() == nullptr"), std::string::npos);
}
