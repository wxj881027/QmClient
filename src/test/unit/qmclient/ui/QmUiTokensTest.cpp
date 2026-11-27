// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/QmUi/QmMotion.h>
#include <game/client/QmUi/QmTheme.h>
#include <game/client/QmUi/UiTokens.h>
// qm_ime_overlay 的候选索引归一口径仍在 lineinput.h 里，IME 用例需要它。
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

TEST(QmUiTokens, IconTextRowKeepsNaturalLabelWidthAndAddsSingleLineSlack)
{
	const CUIRect Row{10.0f, 20.0f, 300.0f, 30.0f};
	const float NaturalWidth = 120.6f;
	const auto Layout = ui_token::control::ResolveIconTextRow(Row, NaturalWidth, 18.0f, 6.0f);
	EXPECT_FLOAT_EQ(Layout.m_Text.w, NaturalWidth);
	EXPECT_FLOAT_EQ(Layout.m_Text.x, Layout.m_Icon.x + Layout.m_Icon.w + 6.0f);
	EXPECT_NEAR(Layout.m_Icon.x - Row.x, (Row.x + Row.w) - (Layout.m_Text.x + Layout.m_Text.w), 0.001f);
	EXPECT_GT(ui_token::control::ResolveTextMaxWidth(NaturalWidth), NaturalWidth);
	EXPECT_FLOAT_EQ(ui_token::control::ResolveTextMaxWidth(NaturalWidth), 122.0f);
}

TEST(QmUiTokens, IconTextRowClampsOverlongLabelInsideRow)
{
	const CUIRect Row{10.0f, 20.0f, 100.0f, 30.0f};
	const auto Layout = ui_token::control::ResolveIconTextRow(Row, 500.0f, 20.0f, 5.0f);
	EXPECT_FLOAT_EQ(Layout.m_Text.w, 75.0f);
	EXPECT_NEAR(Layout.m_Text.x + Layout.m_Text.w, Row.x + Row.w, 0.001f);

	const CUIRect DegenerateRow{0.0f, 0.0f, 4.0f, 10.0f};
	const auto Degenerate = ui_token::control::ResolveIconTextRow(DegenerateRow, 50.0f, 20.0f, 5.0f);
	EXPECT_GE(Degenerate.m_Text.w, 0.0f);
	EXPECT_GE(Degenerate.m_Icon.w, 0.0f);
}

TEST(QmImeOverlay, InvalidSelectionHighlightsFirstCandidate)
{
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(-1, 0), -1);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(-1, 8), 0);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(8, 8), 0);
	EXPECT_EQ(qm_ime_overlay::NormalizeSelectedCandidateIndex(3, 8), 3);
}

TEST(QmImeOverlay, CandidateViewportShowsEntireNineItemImePage)
{
	const qm_ime_overlay::SQmImeCandidateViewport First = qm_ime_overlay::BuildCandidateViewport(9, 0, 0);
	EXPECT_EQ(First.m_Start, 0);
	EXPECT_EQ(First.m_Count, 9);

	const qm_ime_overlay::SQmImeCandidateViewport StillVisible = qm_ime_overlay::BuildCandidateViewport(9, 6, First.m_Start);
	EXPECT_EQ(StillVisible.m_Start, 0);
	EXPECT_EQ(StillVisible.m_Count, 9);

	const qm_ime_overlay::SQmImeCandidateViewport Shifted = qm_ime_overlay::BuildCandidateViewport(9, 7, StillVisible.m_Start);
	EXPECT_EQ(Shifted.m_Start, 0);
	EXPECT_EQ(Shifted.m_Count, 9);
}

TEST(QmImeOverlay, CandidateViewportShowsOnlyActualCandidatesOnShortPages)
{
	const qm_ime_overlay::SQmImeCandidateViewport ShortPage = qm_ime_overlay::BuildCandidateViewport(5, 4, 3);
	EXPECT_EQ(ShortPage.m_Start, 0);
	EXPECT_EQ(ShortPage.m_Count, 5);
}

TEST(QmImeOverlay, CandidateViewportKeepsStableStartWhileSelectionStaysVisible)
{
	const qm_ime_overlay::SQmImeCandidateViewport Stable = qm_ime_overlay::BuildCandidateViewport(20, 5, 2);
	EXPECT_EQ(Stable.m_Start, 2);
	EXPECT_EQ(Stable.m_Count, 16);

	const qm_ime_overlay::SQmImeCandidateViewport ShiftLeft = qm_ime_overlay::BuildCandidateViewport(20, 1, Stable.m_Start);
	EXPECT_EQ(ShiftLeft.m_Start, 1);
	EXPECT_EQ(ShiftLeft.m_Count, 16);
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

TEST(QmUiTokens, MotionRefsBindToAnimCurves)
{
	// 动效 token 统一为「快速起步、平稳收尾」：短时长 + EASE_OUT_QUART，弹簧语义独立于曲线预设。
	EXPECT_EQ(ui_token::motion::HOVER_FADE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::PRESS_SCALE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::PAGE_SLIDE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::TAB_SWITCH.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::INPUT_FOCUS_RING.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::TOAST_SLIDE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::TOOLTIP_FADE.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_FLOAT_EQ(ui_token::motion::TOGGLE_SPRING.m_Stiffness, 600.0f);
	EXPECT_FLOAT_EQ(ui_token::motion::TOGGLE_SPRING.m_Damping, 39.0f);
	EXPECT_EQ(ui_token::motion::TOGGLE_PRESS.m_Easing, EEasing::EASE_OUT_QUART);

	EXPECT_EQ(ui_token::motion::BTN_HOVER.m_Easing, EEasing::EASE_OUT_QUART);
	EXPECT_EQ(ui_token::motion::MODAL_IN.m_Easing, EEasing::EASE_OUT_QUART);
	// 模态弹窗拆成两条时钟：显隐走逻辑步，透明度/缩放走渲染帧。
	EXPECT_EQ(ui_token::motion::MODAL_IN.m_Clock, EUiAnimClock::RENDER);
	EXPECT_EQ(ui_token::motion::MODAL_PRESENCE.m_Clock, EUiAnimClock::LOGIC);
	EXPECT_FLOAT_EQ(ui_token::motion::TOGGLE.m_Stiffness, 600.0f);
	EXPECT_EQ(&ui_token::motion::TOGGLE, &ui_token::motion::TOGGLE_SPRING);
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

TEST(QmAnimationBackend, MotionPolicyIsCanonical)
{
	SUiAnimTransition Transition = ui_token::motion::MODAL_FADE_SCALE;
	Transition.m_DelaySec = 0.20f;

	const SUiAnimTransition Direct = qm_animation::ApplyMotionLevel(Transition, 1);
	const SUiAnimTransition Compatibility = qm_motion::ApplyMotionLevel(Transition, 1);

	EXPECT_FLOAT_EQ(Direct.m_DurationSec, Compatibility.m_DurationSec);
	EXPECT_FLOAT_EQ(Direct.m_DelaySec, Compatibility.m_DelaySec);
	EXPECT_FLOAT_EQ(Direct.m_Spring.m_Damping, Compatibility.m_Spring.m_Damping);
	EXPECT_EQ(Direct.m_Driver, Compatibility.m_Driver);
	EXPECT_EQ(Direct.m_Easing, Compatibility.m_Easing);
}

// 弹层条目的左内退必须真的落在横向：CUiRect 的 HMargin 改的是 y/h（上下），
// 写成 HMargin 时文字会贴着胶囊左缘、纵向反而被压掉两段行高（2026-11-11 的实机现象）。
TEST(QmUiTokensDropdownEntry, InsetFollowsFontAndIsCappedByRowHeight)
{
	// 设置页正文字号 10~12：档位由 max(8, 0.75×字号) 决定。
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(10.0f), 8.0f);
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f), 9.0f);
	// 行高只当上限：20 高的行给 9，16 高的行被夹到 8。
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f, 20.0f), 9.0f);
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f, 16.0f), 8.0f);
	// 大字号不会因为行矮而顶出胶囊端帽；行再高也不会把文字推向中间。
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(20.0f, 16.0f), 8.0f);
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f, 40.0f), 9.0f);
	// 退化行高（0 / 负）视为没有可用空间，不做负内退。
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(ui_token::dropdown::EntryInsetX(12.0f, -5.0f), 0.0f);
	// 列表胶囊行与弹层条目同一条公式：同一个视觉件不能有两套内退。
	EXPECT_FLOAT_EQ(ui_token::list::RowInsetX(12.0f), ui_token::dropdown::EntryInsetX(12.0f));
}

TEST(QmUiTokensDropdownEntry, LabelRectInsetsLeftRightAndKeepsVerticalPadding)
{
	const CUIRect Row{100.0f, 40.0f, 200.0f, 20.0f};
	const float InsetX = ui_token::dropdown::EntryInsetX(12.0f, Row.h);
	const CUIRect Label = ui_token::dropdown::EntryLabelRect(Row, 2.0f, 12.0f);
	// 横向：Padding + 内退，两侧都退。
	EXPECT_FLOAT_EQ(Label.x, Row.x + 2.0f + InsetX);
	EXPECT_FLOAT_EQ(Label.w, Row.w - 2.0f * (2.0f + InsetX));
	// 纵向：只退 Padding，内退不参与——这正是轴向回归的判据。
	EXPECT_FLOAT_EQ(Label.y, Row.y + 2.0f);
	EXPECT_FLOAT_EQ(Label.h, Row.h - 4.0f);

	// 换一个很大的内退值：纵向依旧只受 Padding 影响。
	const CUIRect Wide = ui_token::dropdown::EntryLabelRect(Row, 2.0f, 12.0f, 40.0f);
	EXPECT_FLOAT_EQ(Wide.x, Row.x + 42.0f);
	EXPECT_FLOAT_EQ(Wide.w, Row.w - 84.0f);
	EXPECT_FLOAT_EQ(Wide.y, Row.y + 2.0f);
	EXPECT_FLOAT_EQ(Wide.h, Row.h - 4.0f);

	// 调用点的显式覆盖值优先，且同样只作用于左右。
	const CUIRect Overridden = ui_token::dropdown::EntryLabelRect(Row, 0.0f, 12.0f, 24.0f);
	EXPECT_FLOAT_EQ(Overridden.x, Row.x + 24.0f);
	EXPECT_FLOAT_EQ(Overridden.w, Row.w - 48.0f);
	EXPECT_FLOAT_EQ(Overridden.y, Row.y);
	EXPECT_FLOAT_EQ(Overridden.h, Row.h);
}

// 消息气泡圆角：单行是完整胶囊、多行是圆角矩形（比旧的「字号一半」大一档）。
// 两档都从 ui_token::chat 派生，绘制层再按短边一半兜底。
TEST(QmUiTokensChatBubble, SingleLineUsesFullCapsuleAndMultilineStaysRoundedRect)
{
	const float RowHeight = 7.0f;
	ASSERT_TRUE(ui_token::chat::BubbleIsSingleLine(RowHeight, RowHeight));
	// 单行：行高一半 ⇒ 完整胶囊。
	EXPECT_FLOAT_EQ(ui_token::chat::BubbleRadius(true, RowHeight, RowHeight), RowHeight * 0.5f);

	const float MultilineHeight = RowHeight * 3.0f;
	ASSERT_FALSE(ui_token::chat::BubbleIsSingleLine(MultilineHeight, RowHeight));
	const float MultilineRadius = ui_token::chat::BubbleRadius(false, MultilineHeight, RowHeight);
	// 多行：圆角矩形，明显小于行高一半（否则整块被压成胶囊），
	// 又比旧的「字号一半」大一档（默认字号 6 → 旧值 3）。
	EXPECT_FLOAT_EQ(MultilineRadius, ui_token::chat::BubbleMultiLineRadius(RowHeight));
	EXPECT_LT(MultilineRadius, MultilineHeight * 0.5f);
	EXPECT_GT(MultilineRadius, RowHeight * 0.5f);
}

TEST(QmUiTokensChatBubble, MultilineRadiusFollowsFontSizeInsteadOfStayingFixed)
{
	// 大字号聊天不该反而更方：多行圆角随一行行高一起缩放。
	const float SmallRow = 7.0f;
	const float LargeRow = 14.0f;
	const float Small = ui_token::chat::BubbleRadius(false, LargeRow * 2.0f, SmallRow);
	const float Large = ui_token::chat::BubbleRadius(false, LargeRow * 2.0f, LargeRow);
	EXPECT_GT(Large, Small);
	// 极端字号下也不会自交：半径始终小于行高一半。
	EXPECT_LT(Large, LargeRow * 2.0f * 0.5f);
}

TEST(QmUiTokensChatBubble, SingleLineToleranceAcceptsDecoratedRowsButNotWrappedText)
{
	const float RowHeight = 7.0f;
	// 行内装饰/行距把行高抬高一档，仍是单行。
	EXPECT_TRUE(ui_token::chat::BubbleIsSingleLine(RowHeight * 1.2f, RowHeight));
	// 真换了一行文字（两行高）就按多行处理。
	EXPECT_FALSE(ui_token::chat::BubbleIsSingleLine(RowHeight * 2.0f, RowHeight));
	// 退化输入：负行高不产生负半径，0 行高永远不算单行。
	EXPECT_FLOAT_EQ(ui_token::chat::BubbleRadius(true, 0.0f, RowHeight), 0.0f);
	EXPECT_FLOAT_EQ(ui_token::chat::BubbleRadius(true, -4.0f, RowHeight), 0.0f);
	EXPECT_FLOAT_EQ(ui_token::chat::BubbleRadius(false, RowHeight * 2.0f, -4.0f), 0.0f);
	EXPECT_FALSE(ui_token::chat::BubbleIsSingleLine(1.0f, 0.0f));
	// 半径永不超过行高一半：绘制层不需要再夹一次。
	for(const float Height : {1.0f, 7.0f, 24.0f})
	{
		SCOPED_TRACE(Height);
		EXPECT_LE(ui_token::chat::BubbleRadius(true, Height, Height), Height * 0.5f);
	}
}
