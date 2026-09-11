// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <game/client/QmUi/QmMotion.h>
#include <game/client/QmUi/QmTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/lineinput.h>
#include <game/client/qm_ime_candidate_popup.h>

#include <gtest/gtest.h>

#include <string>

// Compile-time invariants. Catching token regressions at compile time keeps
// downstream QmUi widgets stable. NOTE: color4_base uses anonymous unions
// (x/r/h share storage); constexpr evaluation can only read the active union
// member, which is x/y/z/a (the constructor initializes those). Hence we
// assert via .x/.y/.z here and use .r/.g/.b at runtime.
static_assert(ui_token::color::ACCENT_PRIMARY.x > 0.39f && ui_token::color::ACCENT_PRIMARY.x < 0.41f,
	"ACCENT_PRIMARY R channel must stay near the Qm accent blue (~0.4)");
static_assert(ui_token::color::SURFACE_GLASS.a == 0.70f,
	"SURFACE_GLASS alpha must remain at 0.70 to preserve QmClient glass appearance");
static_assert(ui_token::radius::TIGHT == 6.0f,
	"radius::TIGHT is the shared micro-element corner (badges, dots, swatches)");
static_assert(ui_token::radius::BASE == 8.0f,
	"radius::BASE is the shared control corner (buttons, inputs, list rows)");
static_assert(ui_token::radius::CARD == 14.0f,
	"radius::CARD is the shared card/panel corner");

// Spacing scale must be strictly monotonic so downstream code can pick a
// "next size up" without ambiguity.
static_assert(ui_token::spacing::XS < ui_token::spacing::SM, "spacing scale must be monotonic");
static_assert(ui_token::spacing::SM < ui_token::spacing::MD, "spacing scale must be monotonic");
static_assert(ui_token::spacing::MD < ui_token::spacing::LG, "spacing scale must be monotonic");
static_assert(ui_token::spacing::LG < ui_token::spacing::XL, "spacing scale must be monotonic");

static_assert(ui_token::radius::NONE < ui_token::radius::TIGHT, "radius scale must be monotonic");
static_assert(ui_token::radius::TIGHT < ui_token::radius::BASE, "radius scale must be monotonic");
static_assert(ui_token::radius::BASE < ui_token::radius::CARD, "radius scale must be monotonic");

static_assert(ui_token::font::TIP < ui_token::font::CAPTION, "font scale must be monotonic");
static_assert(ui_token::font::CAPTION < ui_token::font::BODY, "font scale must be monotonic");
static_assert(ui_token::font::BODY < ui_token::font::HEADLINE, "font scale must be monotonic");
static_assert(ui_token::font::HEADLINE < ui_token::font::HEADLINE_LG, "font scale must be monotonic");

TEST(QmUiTokens, AccentPrimaryMatchesQmBlue)
{
	EXPECT_NEAR(ui_token::color::ACCENT_PRIMARY.r, 0.4f, 0.01f);
	EXPECT_NEAR(ui_token::color::ACCENT_PRIMARY.g, 0.753f, 0.01f);
	EXPECT_NEAR(ui_token::color::ACCENT_PRIMARY.b, 0.957f, 0.01f);
	EXPECT_EQ(ui_token::color::ACCENT_PRIMARY.a, 1.0f);
}

TEST(QmUiTokens, SurfaceGlassPreservesAlpha)
{
	EXPECT_NEAR(ui_token::color::SURFACE_GLASS.r, 0.08f, 0.001f);
	EXPECT_NEAR(ui_token::color::SURFACE_GLASS.g, 0.09f, 0.001f);
	EXPECT_NEAR(ui_token::color::SURFACE_GLASS.b, 0.12f, 0.001f);
	EXPECT_EQ(ui_token::color::SURFACE_GLASS.a, 0.70f);
	EXPECT_FLOAT_EQ(ui_token::ime::SCALE, 0.68f);
	EXPECT_FLOAT_EQ(ui_token::ime::PANEL_BG_LIGHT.a, 0.96f);
	EXPECT_FLOAT_EQ(ui_token::ime::PANEL_BG_DARK.a, 0.96f);
	EXPECT_FLOAT_EQ(ui_token::ime::COMPOSITION_SELECTION.a, 0.18f);
	EXPECT_GT(ui_token::ime::TEXT_SAFE_PADDING_X, 0.0f);
	EXPECT_GT(ui_token::ime::TEXT_SAFE_PADDING_Y, 0.0f);
}

TEST(QmUiTokens, ImeDynamicIslandMatchesReferenceHtmlStyle)
{
	EXPECT_NEAR(ui_token::ime::PANEL_BG_DARK.r, 0.110f, 0.001f);
	EXPECT_NEAR(ui_token::ime::PANEL_BG_DARK.g, 0.110f, 0.001f);
	EXPECT_NEAR(ui_token::ime::PANEL_BG_DARK.b, 0.118f, 0.001f);
	EXPECT_NEAR(ui_token::ime::TEXT_DARK.r, 1.0f, 0.001f);
	EXPECT_NEAR(ui_token::ime::TEXT_MUTED_DARK.r, 0.557f, 0.001f);
	EXPECT_NEAR(ui_token::ime::TEXT_SELECTED_DARK.r, 0.184f, 0.001f);
	EXPECT_NEAR(ui_token::ime::TEXT_SELECTED_DARK.g, 0.502f, 0.001f);
	EXPECT_NEAR(ui_token::ime::TEXT_SELECTED_DARK.b, 0.929f, 0.001f);
	EXPECT_FLOAT_EQ(ui_token::ime::PANEL_SHADOW_DARK.a, 0.0f);
	EXPECT_LT(ui_token::ime::MAX_WIDTH, 300.0f);
	EXPECT_GT(ui_token::ime::RADIUS, ui_token::ime::ROW_HEIGHT * 0.5f);
}

TEST(QmUiTokens, QmThemeMirrorsSharedTokens)
{
	EXPECT_EQ(qm_theme::ICON.m_Active.r, ui_token::color::ACCENT_PRIMARY.r);
	EXPECT_EQ(qm_theme::ICON.m_Active.g, ui_token::color::ACCENT_PRIMARY.g);
	EXPECT_EQ(qm_theme::ICON.m_Active.b, ui_token::color::ACCENT_PRIMARY.b);
	EXPECT_EQ(qm_theme::ICON.m_Disabled.a, 0.36f);

	EXPECT_EQ(qm_theme::IME.m_PanelBg.a, ui_token::ime::PANEL_BG.a);
	EXPECT_EQ(qm_theme::IME.m_CompositionSelection.a, ui_token::ime::COMPOSITION_SELECTION.a);
	EXPECT_EQ(qm_theme::IME.m_SelectedBg.b, ui_token::ime::SELECTED_BG.b);
	EXPECT_EQ(qm_theme::IME.m_Radius, ui_token::ime::RADIUS);
	EXPECT_EQ(qm_theme::IME.m_RowHeight, ui_token::ime::ROW_HEIGHT);
	EXPECT_EQ(qm_theme::IME.m_TextSafePaddingX, ui_token::ime::TEXT_SAFE_PADDING_X);
	EXPECT_EQ(qm_theme::IME.m_TextSafePaddingY, ui_token::ime::TEXT_SAFE_PADDING_Y);
}

TEST(QmImeOverlay, InvalidSelectionHighlightsFirstCandidate)
{
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(-1, 0), -1);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(-1, 8), 0);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(8, 8), 0);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(3, 8), 3);
}

TEST(QmImeOverlay, CandidateViewportKeepsSevenItemsWhenPageHasEnoughCandidates)
{
	const qm_ime_overlay::SQmImeCandidateViewport First = qm_ime_overlay::BuildCandidateViewport(9, 0, 0);
	EXPECT_EQ(First.m_Start, 0);
	EXPECT_EQ(First.m_Count, 7);

	const qm_ime_overlay::SQmImeCandidateViewport StillVisible = qm_ime_overlay::BuildCandidateViewport(9, 6, First.m_Start);
	EXPECT_EQ(StillVisible.m_Start, 0);
	EXPECT_EQ(StillVisible.m_Count, 7);

	const qm_ime_overlay::SQmImeCandidateViewport Shifted = qm_ime_overlay::BuildCandidateViewport(9, 7, StillVisible.m_Start);
	EXPECT_EQ(Shifted.m_Start, 1);
	EXPECT_EQ(Shifted.m_Count, 7);
}

TEST(QmImeOverlay, CandidateViewportShowsOnlyActualCandidatesOnShortPages)
{
	const qm_ime_overlay::SQmImeCandidateViewport ShortPage = qm_ime_overlay::BuildCandidateViewport(5, 4, 3);
	EXPECT_EQ(ShortPage.m_Start, 0);
	EXPECT_EQ(ShortPage.m_Count, 5);
}

TEST(QmImeOverlay, CandidateViewportKeepsStableStartWhileSelectionStaysVisible)
{
	const qm_ime_overlay::SQmImeCandidateViewport Stable = qm_ime_overlay::BuildCandidateViewport(12, 5, 2);
	EXPECT_EQ(Stable.m_Start, 2);
	EXPECT_EQ(Stable.m_Count, 7);

	const qm_ime_overlay::SQmImeCandidateViewport ShiftLeft = qm_ime_overlay::BuildCandidateViewport(12, 1, Stable.m_Start);
	EXPECT_EQ(ShiftLeft.m_Start, 1);
	EXPECT_EQ(ShiftLeft.m_Count, 7);
}

TEST(QmLineInput, CaretBlinkUsesStableHalfSecondPhases)
{
	using namespace std::chrono_literals;

	EXPECT_TRUE(qm_lineinput::CaretVisibleForElapsed(0ns));
	EXPECT_TRUE(qm_lineinput::CaretVisibleForElapsed(499ms));
	EXPECT_FALSE(qm_lineinput::CaretVisibleForElapsed(500ms));
	EXPECT_FALSE(qm_lineinput::CaretVisibleForElapsed(999ms));
	EXPECT_TRUE(qm_lineinput::CaretVisibleForElapsed(1000ms));
	EXPECT_TRUE(qm_lineinput::CaretVisibleForElapsed(-1ns));
}

TEST(QmImeOverlay, CandidatePopupOnlyAppearsForCandidateText)
{
	SQmImePopupState State;
	State.m_Visible = true;
	State.m_Composition = "da'd";
	EXPECT_FALSE(QmImeHasPopupContent(State));

	State.m_vCandidates.emplace_back("大胆");
	EXPECT_TRUE(QmImeHasPopupContent(State));

	State.m_Composition.clear();
	EXPECT_TRUE(QmImeHasPopupContent(State));

	State.m_Disabled = true;
	EXPECT_FALSE(QmImeHasPopupContent(State));
}

TEST(QmImePresentationSource, PopupUsesContinuousRedirectablePresentationState)
{
	const std::string ManagerSource = ReadTestSourceFile("src/game/client/qm_ime_manager.cpp");
	const std::string PopupSource = ReadTestSourceFile("src/game/client/qm_ime_candidate_popup.cpp");
	const std::string PopupHeader = ReadTestSourceFile("src/game/client/qm_ime_candidate_popup.h");

	EXPECT_NE(ManagerSource.find("State.m_Visible = HasComposition && CandidateCount > 0;"), std::string::npos);
	EXPECT_NE(PopupHeader.find("SPresentationTargets"), std::string::npos);
	EXPECT_NE(PopupSource.find("SImePresentationTarget"), std::string::npos);
	EXPECT_EQ(PopupSource.find("ResolveImePresentationStateValue"), std::string::npos);
	EXPECT_NE(PopupSource.find("ResolveUiPresentationStateValue"), std::string::npos);
	EXPECT_NE(PopupSource.find("SetUiPresentationStateValue"), std::string::npos);
	EXPECT_NE(PopupSource.find("TargetPresentation.m_CandidateAlpha"), std::string::npos);
	// 首次状态只初始化一次；后续显示、重定向和淡出都沿用同一组可动画状态。
	EXPECT_NE(PopupSource.find("if(!m_Presentation.m_Initialized)"), std::string::npos);
	EXPECT_NE(PopupSource.find("m_Presentation.m_Initialized = true;"), std::string::npos);
	EXPECT_EQ(PopupSource.find("Presence.m_FreshEnter"), std::string::npos);
	EXPECT_NE(PopupSource.find("const float Alpha = minimum(Presence.m_Alpha, PresentationAlpha);"), std::string::npos);
	EXPECT_NE(PopupSource.find("const float CandidateDrawAlpha = Alpha * CandidateAlpha;"), std::string::npos);
	EXPECT_NE(PopupSource.find("WithAlpha(Ime.m_SelectedBg, CandidateDrawAlpha)"), std::string::npos);
	EXPECT_NE(PopupSource.find("TargetPresentation.m_Radius = PanelHeight * 0.5f;"), std::string::npos);
	EXPECT_EQ(PopupSource.find("PanelHeight * 0.36f"), std::string::npos);
	EXPECT_EQ(PopupSource.find("(void)PresentationAlpha;"), std::string::npos);
	EXPECT_EQ(PopupSource.find("(void)CandidateAlpha;"), std::string::npos);
	EXPECT_NE(PopupSource.find("IME_CONTENT_TIME_SCALE = 0.40f"), std::string::npos);
	EXPECT_NE(PopupSource.find("BuildCandidateViewport"), std::string::npos);
	EXPECT_EQ(PopupSource.find("ResolveMotionValue"), std::string::npos);
	EXPECT_EQ(PopupSource.find("ResolveMotionRect"), std::string::npos);
	EXPECT_EQ(PopupSource.find("FitCandidates"), std::string::npos);
	EXPECT_EQ(PopupSource.find("CandidateFitPanelWidth"), std::string::npos);
	EXPECT_EQ(PopupSource.find("SingleLongCandidate"), std::string::npos);
	EXPECT_EQ(PopupSource.find("--CandidateDisplayCount"), std::string::npos);
	EXPECT_EQ(PopupSource.find("TEXTFLAG_ELLIPSIS_AT_END"), std::string::npos);
	EXPECT_EQ(PopupSource.find("TextWidthBudget"), std::string::npos);
	EXPECT_EQ(PopupSource.find("m_TextMaxWidth"), std::string::npos);
	EXPECT_EQ(PopupSource.find("m_MaxCandidateTextWidth"), std::string::npos);
	EXPECT_EQ(PopupSource.find("ShadowNear"), std::string::npos);
	EXPECT_EQ(PopupSource.find("ShadowFar"), std::string::npos);
	EXPECT_EQ(PopupSource.find("TopGlow"), std::string::npos);
	EXPECT_EQ(PopupSource.find("PanelInner"), std::string::npos);
	EXPECT_EQ(PopupSource.find("EUiAnimInterruptPolicy::QUEUE"), std::string::npos);
}

TEST(QmUiPresentationSource, OverlaysUsePresentationState)
{
	const std::string OverlaySource = ReadTestSourceFile("src/game/client/QmUi/UiOverlays.h");

	EXPECT_NE(OverlaySource.find("ResolveUiPresentationStateValue"), std::string::npos);
	EXPECT_NE(OverlaySource.find("SetUiPresentationStateValue"), std::string::npos);
	EXPECT_EQ(OverlaySource.find("->SetValue("), std::string::npos);
}

TEST(QmUiTokens, MotionRefsBindToAnimCurves)
{
	EXPECT_EQ(ui_token::motion::HOVER_FADE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::PRESS_SCALE.m_Easing, EEasing::EASE_IN);
	EXPECT_EQ(ui_token::motion::PAGE_SLIDE.m_Easing, EEasing::EASE_IN_OUT_CUBIC);
	EXPECT_EQ(ui_token::motion::TAB_SWITCH.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::INPUT_FOCUS_RING.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::TOAST_SLIDE.m_Easing, EEasing::CUBIC_BEZIER);
	EXPECT_EQ(ui_token::motion::TOOLTIP_FADE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_NEAR(ui_token::motion::TOGGLE_SPRING.m_Stiffness, 280.0f, 1e-6f);

	EXPECT_EQ(ui_token::motion::BTN_HOVER.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::MODAL_IN.m_Easing, EEasing::CUBIC_BEZIER);
	EXPECT_NEAR(ui_token::motion::TOGGLE.m_Stiffness, 280.0f, 1e-6f);
}

TEST(QmUiTokens, QmMotionAppliesUserMotionLevel)
{
	SUiAnimTransition Transition = ui_token::motion::MODAL_FADE_SCALE;
	Transition.m_DelaySec = 0.20f;
	Transition.m_Driver = EUiAnimDriver::SPRING;
	Transition.m_Spring.m_Damping = 20.0f;
	Transition.m_Spring.m_RestEpsilon = 0.01f;
	Transition.m_Spring.m_RestVelocity = 0.05f;

	const SUiAnimTransition Disabled = qm_motion::ApplyMotionLevel(Transition, 0);
	EXPECT_EQ(Disabled.m_DurationSec, 0.0f);
	EXPECT_EQ(Disabled.m_DelaySec, 0.0f);
	EXPECT_EQ(Disabled.m_Driver, EUiAnimDriver::TWEEN);
	EXPECT_EQ(Disabled.m_Easing, EEasing::LINEAR);

	const SUiAnimTransition Reduced = qm_motion::ApplyMotionLevel(Transition, 1);
	EXPECT_NEAR(Reduced.m_DurationSec, Transition.m_DurationSec * 0.45f, 1e-6f);
	EXPECT_NEAR(Reduced.m_DelaySec, Transition.m_DelaySec * 0.25f, 1e-6f);
	EXPECT_NEAR(Reduced.m_Spring.m_Damping, Transition.m_Spring.m_Damping * 1.35f, 1e-6f);

	const SUiAnimTransition Full = qm_motion::ApplyMotionLevel(Transition, 2);
	EXPECT_EQ(Full.m_DurationSec, Transition.m_DurationSec);
	EXPECT_EQ(Full.m_DelaySec, Transition.m_DelaySec);
}
