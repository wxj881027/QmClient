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

namespace
{

	[[maybe_unused]] size_t MatchingBrace(const std::string &Source, size_t BodyStart)
	{
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Index;
			}
		}
		return std::string::npos;
	}

} // namespace

TEST(QmNewUiMenuGameplayBlurContract, GaussianBlurUsesSharedUiBackdropWithTransparentFallback)
{
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiRectSource = ReadTextFile("src/game/client/ui_rect.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string TooltipsSource = ReadTextFile("src/game/client/components/tooltips.cpp");
	const std::string ImeSource = ReadTextFile("src/game/client/qm_ime_manager.cpp");
	const std::string CachedRectDraw = FunctionBody(UiSource, "void CUIElement::SUIElementRect::Draw(");
	const std::string BatchableRectDraw = FunctionBody(UiSource, "void CUi::RenderBatchableRect(");
	const std::string MenuButtonDraw = FunctionBody(UiSource, "int CUi::DoButton_Menu(");
	const std::string RectDraw = FunctionBody(UiRectSource, "void CUIRect::Draw(");
	const std::string RectDraw4 = FunctionBody(UiRectSource, "void CUIRect::Draw4(");
	const std::string MenuRender = FunctionBody(MenusSource, "void CMenus::Render()");
	const std::string LoadingRender = FunctionBody(MenusSource, "void CMenus::RenderLoadingDirect(");

	EXPECT_NE(UiHeader.find("CUiScopedGaussianBlur(CUi *pUi, float Alpha = 1.0f)"), std::string::npos);
	EXPECT_NE(UiHeader.find("GaussianBlurScopeAlpha() const"), std::string::npos);
	EXPECT_NE(UiHeader.find("RenderGaussianBlur(const CUIRect &Rect, float Alpha"), std::string::npos);
	EXPECT_NE(UiSource.find("IsBackbufferCaptureSupported"), std::string::npos);
	EXPECT_NE(UiSource.find("IsRenderTargetGaussianBlurSupported"), std::string::npos);
	EXPECT_NE(UiSource.find("CaptureBackbufferToRenderTarget"), std::string::npos);
	EXPECT_NE(UiSource.find("GaussianBlurRenderTarget"), std::string::npos);
	const std::string PrepareBlur = FunctionBody(UiSource, "bool CUi::PrepareGaussianBlur()");
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPrepared && m_GaussianBlurPreparedFrame == PerfFrame"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPreparedFrame = PerfFrame"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPrepared = true"), std::string::npos);
	// 同帧失败闩：GPU 侧失败后同帧不得反复重试（背板捕获/模糊提交开销）。
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurFailedFrame == PerfFrame"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurFailedFrame = PerfFrame"), std::string::npos);
	EXPECT_NE(UiSource.find("Graphics()->GetScreen"), std::string::npos);
	EXPECT_NE(UiSource.find("UiGaussianBlurTargetDimension"), std::string::npos);
	EXPECT_NE(CachedRectDraw.find("GaussianBlurScopeAlpha()"), std::string::npos);
	EXPECT_NE(BatchableRectDraw.find("GaussianBlurScopeAlpha()"), std::string::npos);
	EXPECT_NE(MenuButtonDraw.find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_EQ(CachedRectDraw.find("RenderGaussianBlur(*pRect, Color.a)"), std::string::npos);
	EXPECT_EQ(BatchableRectDraw.find("RenderGaussianBlur(*pRect, Color.a)"), std::string::npos);
	EXPECT_EQ(MenuButtonDraw.find("RenderGaussianBlur(*pRect, BackgroundColor.a)"), std::string::npos);
	EXPECT_NE(RectDraw.find("DrawRectBackdrop(Corners, Rounding)"), std::string::npos);
	EXPECT_NE(RectDraw4.find("DrawRectBackdrop(Corners, Rounding)"), std::string::npos);
	EXPECT_NE(MenuRender.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), MenuOpenFrame == 0 ? 0.0f : 1.0f);"), std::string::npos);
	EXPECT_NE(LoadingRender.find("CUiScopedGaussianBlur GaussianBlurScope(Ui());"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("const float GaussianBlurAlpha = WantActive ? 1.0f : m_AnimContentAlpha;"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), GaussianBlurAlpha);"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui());"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), BackgroundAlphaFinal);"), std::string::npos);
	EXPECT_NE(TooltipsSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), AlphaFactor);"), std::string::npos);
	EXPECT_NE(ImeSource.find("CUiScopedGaussianBlur GaussianBlurScope(m_pGameClient->Ui());"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("BetterScoreboardBlur"), std::string::npos);

	// Existing translucent surfaces remain the unsupported-backend fallback.
	EXPECT_NE(ScoreboardSource.find("Scoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal)"), std::string::npos);
}

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
