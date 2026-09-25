#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>
#include <game/client/components/qmclient/collision_hitbox_logic.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
	std::string FindRuntimeTranslation(const std::string &LanguageData, const char *pKey)
	{
		const std::string Prefix = std::string(pKey) + "\n== ";
		const size_t TranslationStart = LanguageData.find(Prefix);
		if(TranslationStart == std::string::npos)
			return {};
		const size_t ValueStart = TranslationStart + Prefix.size();
		const size_t ValueEnd = LanguageData.find('\n', ValueStart);
		return LanguageData.substr(ValueStart, ValueEnd - ValueStart);
	}
}

TEST(CollisionHitboxLogic, CapsuleOutlineHandlesDegenerateAndAxisAlignedLasers)
{
	const auto Circle = BuildHitboxCapsuleOutline({10.0f, 20.0f}, {10.0f, 20.0f}, 5.0f, 4);
	ASSERT_EQ(Circle.size(), 8u);
	for(const auto &Line : Circle)
	{
		EXPECT_NEAR(distance(Line.m_From, vec2(10.0f, 20.0f)), 5.0f, 0.0001f);
		EXPECT_NEAR(distance(Line.m_To, vec2(10.0f, 20.0f)), 5.0f, 0.0001f);
	}

	const auto Horizontal = BuildHitboxCapsuleOutline({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, 4);
	ASSERT_EQ(Horizontal.size(), 10u);
	EXPECT_NEAR(Horizontal[0].m_From.y, 2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[0].m_To.y, 2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[1].m_From.y, -2.0f, 0.0001f);
	EXPECT_NEAR(Horizontal[1].m_To.y, -2.0f, 0.0001f);

	const auto Vertical = BuildHitboxCapsuleOutline({0.0f, 0.0f}, {0.0f, 10.0f}, 2.0f, 4);
	ASSERT_EQ(Vertical.size(), 10u);
	EXPECT_NEAR(Vertical[0].m_From.x, -2.0f, 0.0001f);
	EXPECT_NEAR(Vertical[0].m_To.x, -2.0f, 0.0001f);
	EXPECT_TRUE(BuildHitboxCapsuleOutline({0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f).empty());
	const float MaxFloat = std::numeric_limits<float>::max();
	EXPECT_TRUE(BuildHitboxCapsuleOutline({-MaxFloat, 0.0f}, {MaxFloat, 0.0f}, 2.0f).empty());
}

TEST(CollisionHitboxLogic, CapsuleEmitOverloadProducesTheSameOutlineAsTheVectorOverload)
{
	// 渲染热路径改用直接输出重载（栈上定长缓冲），两者必须逐点一致，
	// 否则渲染结果会与既有行为发生偏移。
	const vec2 Cases[][2] = {
		{{10.0f, 20.0f}, {10.0f, 20.0f}}, // 退化为圆
		{{0.0f, 0.0f}, {10.0f, 0.0f}}, // 水平
		{{0.0f, 0.0f}, {0.0f, 10.0f}}, // 垂直
		{{-3.0f, 5.0f}, {7.0f, -11.0f}}, // 斜向
	};
	for(const auto &Case : Cases)
	{
		for(const int ArcSegments : {2, 3, 4, 16, 64})
		{
			const auto Expected = BuildHitboxCapsuleOutline(Case[0], Case[1], 2.5f, ArcSegments);
			std::vector<SCollisionHitboxLine> Emitted;
			BuildHitboxCapsuleOutline(Case[0], Case[1], 2.5f, ArcSegments, [&](vec2 From, vec2 To) {
				Emitted.push_back({From, To});
			});
			ASSERT_EQ(Emitted.size(), Expected.size()) << "ArcSegments=" << ArcSegments;
			for(size_t Index = 0; Index < Emitted.size(); ++Index)
			{
				EXPECT_EQ(Emitted[Index].m_From.x, Expected[Index].m_From.x) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_From.y, Expected[Index].m_From.y) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_To.x, Expected[Index].m_To.x) << "ArcSegments=" << ArcSegments << " Index=" << Index;
				EXPECT_EQ(Emitted[Index].m_To.y, Expected[Index].m_To.y) << "ArcSegments=" << ArcSegments << " Index=" << Index;
			}
		}
	}

	// 被拒绝的输入不能输出任何线段；合法输入不能超过栈上定长缓冲的容量。
	std::vector<SCollisionHitboxLine> Rejected;
	BuildHitboxCapsuleOutline({0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f, 16, [&](vec2 From, vec2 To) { Rejected.push_back({From, To}); });
	EXPECT_TRUE(Rejected.empty());
	std::vector<SCollisionHitboxLine> Overflowed;
	const float MaxFloat = std::numeric_limits<float>::max();
	BuildHitboxCapsuleOutline({-MaxFloat, 0.0f}, {MaxFloat, 0.0f}, 2.0f, 16, [&](vec2 From, vec2 To) { Overflowed.push_back({From, To}); });
	EXPECT_TRUE(Overflowed.empty());
	std::vector<SCollisionHitboxLine> MaxSegments;
	BuildHitboxCapsuleOutline({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, 4096, [&](vec2 From, vec2 To) { MaxSegments.push_back({From, To}); });
	EXPECT_LE(MaxSegments.size(), (size_t)COLLISION_HITBOX_CAPSULE_MAX_LINES);
}

TEST(CollisionHitboxLogic, CachedCircleDirectionsKeepIncreasingAnglesPerSegmentCount)
{
	CQmHitboxCircleDirections Directions;
	for(const int Segments : {CQmHitboxCircleDirections::MIN_SEGMENTS, 32, CQmHitboxCircleDirections::MAX_SEGMENTS})
	{
		const vec2 *pFirst = Directions.Get(Segments);
		const vec2 *pSecond = Directions.Get(Segments);
		// 同一档位重复取用必须命中缓存，返回同一块表。
		EXPECT_EQ(pFirst, pSecond);
		// 起点固定为角度 0，末端保留原角度运算的浮点值（不吸附回起点）。
		EXPECT_FLOAT_EQ(pFirst[0].x, 1.0f);
		EXPECT_FLOAT_EQ(pFirst[0].y, 0.0f);
		const float Step = 2.0f * pi / Segments;
		for(int Index = 0; Index <= Segments; ++Index)
		{
			EXPECT_FLOAT_EQ(pFirst[Index].x, std::cos(Step * Index));
			EXPECT_FLOAT_EQ(pFirst[Index].y, std::sin(Step * Index));
			EXPECT_NEAR(length(pFirst[Index]), 1.0f, 0.0001f);
		}
	}
	// 不同档位各自独立，不互相覆盖。
	const vec2 *pSmall = Directions.Get(CQmHitboxCircleDirections::MIN_SEGMENTS);
	const vec2 *pLarge = Directions.Get(CQmHitboxCircleDirections::MAX_SEGMENTS);
	EXPECT_NE(pSmall, pLarge);
	EXPECT_NEAR(length(pSmall[CQmHitboxCircleDirections::MIN_SEGMENTS]), 1.0f, 0.0001f);
	EXPECT_NEAR(length(pLarge[CQmHitboxCircleDirections::MAX_SEGMENTS]), 1.0f, 0.0001f);
}

TEST(SettingsPageLayout, DynamicIslandHeightMatchesTheRenderedRowsAndColorRow)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float OriginalHeight = ResolveQmHudDynamicIslandHeight(Metrics, true, false, 700.0f);
	const float ExpandedHeight = ResolveQmHudDynamicIslandHeight(Metrics, false, false, 700.0f);
	const CUIRect ColorRowView{0.0f, 0.0f, 700.0f, 0.0f};

	// 卡片固定渲染 4 行：使用原版样式、显示队伍、钩子倒计时、开关倒计时总开关。
	// 展开时再多一行背景色。
	EXPECT_FLOAT_EQ(OriginalHeight, 4.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ExpandedHeight - OriginalHeight, ResolveSettingsColorRowLayout(ColorRowView, Metrics, false).m_ConsumedHeight);
	// 开关倒计时启用后再多出「跟随 Tee」「灵动岛」两个位置开关（没有位置标题行）。
	EXPECT_FLOAT_EQ(ResolveQmHudDynamicIslandHeight(Metrics, true, true, 700.0f) - OriginalHeight, 2.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmHudDynamicIslandHeight(Metrics, false, true, 700.0f) - ExpandedHeight, 2.0f * Metrics.m_RowStep);
}

TEST(SettingsPageLayout, ContentRowFlowKeepsConditionalRowsAndMeasuredHeightInSync)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	CUIRect Content{0.0f, 0.0f, 700.0f, 1000.0f};
	CSettingsContentRowFlow Rows(Content, Metrics);
	const CUIRect ColorChoice = Rows.NextLine();
	const CUIRect CustomColor = Rows.NextButton();
	const CUIRect WeightChoice = Rows.NextLine();

	EXPECT_FLOAT_EQ(CustomColor.y, ColorChoice.y + ColorChoice.h + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(WeightChoice.y, CustomColor.y + CustomColor.h + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(1000.0f - Content.h, ResolveSettingsContentFlowHeight(Metrics, {Metrics.m_LineHeight, Metrics.m_ButtonHeight, Metrics.m_LineHeight}));
}

TEST(SettingsCard, ContentClipAllowsFocusRingButNeverEscapesTheCard)
{
	const CUIRect Card{10.0f, 20.0f, 200.0f, 100.0f};
	const CUIRect Content{20.0f, 45.0f, 180.0f, 65.0f};
	const CUIRect Clip = ResolveSettingsCardContentClipRect(Content, Card, 1.0f);

	EXPECT_GE(Clip.x, Card.x);
	EXPECT_GE(Clip.y, Card.y);
	EXPECT_LE(Clip.x + Clip.w, Card.x + Card.w);
	EXPECT_LE(Clip.y + Clip.h, Card.y + Card.h);
	EXPECT_LT(Clip.y, Content.y);
	EXPECT_GT(Clip.y + Clip.h, Content.y + Content.h - 0.001f);
}

TEST(SettingsInputField, LayoutKeepsTrailingUnitInsideTheShell)
{
	const CUIRect Shell{10.0f, 20.0f, 120.0f, 24.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Shell, false, false, 1.0f, 24.0f);

	EXPECT_FLOAT_EQ(Layout.m_ShellRect.x, Shell.x);
	EXPECT_FLOAT_EQ(Layout.m_ShellRect.w, Shell.w);
	EXPECT_GT(Layout.m_TrailingRect.w, 0.0f);
	EXPECT_GE(Layout.m_TrailingRect.x, Shell.x);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Shell.x + Shell.w);
	EXPECT_LE(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
}

TEST(SettingsCardDeck, RestingCardsDoNotDrawASecondRoundedBorder)
{
	SSettingsCardVisualState State;
	EXPECT_FALSE(SettingsCardInteractionBorderVisible(State));
	State.m_Hovered = true;
	EXPECT_FALSE(SettingsCardInteractionBorderVisible(State));
	State.m_Hovered = false;
	State.m_Focused = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
	State.m_Focused = false;
	State.m_Dragged = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
	State.m_Dragged = false;
	State.m_DropFeedback = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
}

TEST(SettingsCardDeck, RenderOnlyAndVisiblePassPlanChromeExactlyOnce)
{
	int SurfaceDrawCount = 0;
	int BorderedSurfaceDrawCount = 0;
	const auto DrawSurface = [&] { ++SurfaceDrawCount; };
	const auto DrawBorderedSurface = [&] { ++BorderedSurfaceDrawCount; };

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(true), false, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 0);
	EXPECT_EQ(BorderedSurfaceDrawCount, 0);

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(false), false, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 1);
	EXPECT_EQ(BorderedSurfaceDrawCount, 0);

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(false), true, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 1);
	EXPECT_EQ(BorderedSurfaceDrawCount, 1);
}

TEST(SettingsCardDeck, InteractionBorderStaysInsideSurfaceEdge)
{
	const CUIRect Surface{10.0f, 20.0f, 200.0f, 100.0f};
	const CUIRect Border = ResolveSettingsCardInteractionBorderRect(Surface, 2.0f);
	EXPECT_GT(Border.x, Surface.x);
	EXPECT_GT(Border.y, Surface.y);
	EXPECT_LT(Border.x + Border.w, Surface.x + Surface.w);
	EXPECT_LT(Border.y + Border.h, Surface.y + Surface.h);
}

TEST(SettingsCardDeck, CardSurfaceColorIgnoresBorderInteractionState)
{
	const ColorRGBA BaseSurface(0.12f, 0.24f, 0.36f, 0.48f);
	SSettingsCardVisualState Resting;
	Resting.m_DrawAlpha = 0.75f;
	SSettingsCardVisualState Interactive = Resting;
	Interactive.m_Hovered = true;
	Interactive.m_Focused = true;
	Interactive.m_DropFeedback = true;

	const ColorRGBA RestingSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Resting);
	const ColorRGBA InteractiveSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Interactive);
	EXPECT_FLOAT_EQ(RestingSurface.r, InteractiveSurface.r);
	EXPECT_FLOAT_EQ(RestingSurface.g, InteractiveSurface.g);
	EXPECT_FLOAT_EQ(RestingSurface.b, InteractiveSurface.b);
	EXPECT_FLOAT_EQ(RestingSurface.a, InteractiveSurface.a);
	EXPECT_FLOAT_EQ(RestingSurface.a, BaseSurface.a * Resting.m_DrawAlpha);
}

TEST(SettingsCardDeck, BorderWidthDoesNotDependOnFocus)
{
	// Focus 状态不参与宽度解析，交互反馈只能改变边框颜色。
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.0f), 2.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(0.5f), 2.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(2.0f), 4.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.3f, 0.5f), 2.5f);
}

TEST(SettingsCardDeck, DrawGeometryKeepsSubpixelOffsetsThroughMotionAndRest)
{
	SSettingsCardSpec Spec;
	const SSettingsCardFrame Start = BuildSettingsCardFrame({10.2f, 20.3f, 199.6f, 0.0f}, Spec, 49.4f, 1.0f);
	for(const float Offset : {0.0f, 0.025f, 0.05f, 0.075f, 0.05f, 0.025f, 0.0f})
	{
		const SSettingsCardFrame Draw = ResolveSettingsCardDrawFrame(Start, Offset, Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.x, Start.m_Rect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.y, Start.m_Rect.y + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.w, Start.m_Rect.w);
		EXPECT_FLOAT_EQ(Draw.m_Rect.h, Start.m_Rect.h);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.x, Start.m_HandleRect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.y, Start.m_HandleRect.y + Offset);
		EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.3f, 0.5f), 2.5f);
	}
}

TEST(SettingsPageLayout, AlphaColorRoundTripUpdatesColorAndOpacityTogether)
{
	const unsigned int SourceColor = ColorHSLA(0.31f, 0.72f, 0.44f, 1.0f).Pack(false);
	const unsigned int Packed = PackSettingsAlphaColor(SourceColor, 37);
	unsigned int UpdatedColor = 0;
	int UpdatedOpacity = 0;
	UnpackSettingsAlphaColor(Packed, UpdatedColor, UpdatedOpacity);

	const ColorHSLA Expected(SourceColor);
	const ColorHSLA Actual(UpdatedColor);
	EXPECT_NEAR(Actual.h, Expected.h, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.s, Expected.s, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.l, Expected.l, 1.0f / 255.0f);
	EXPECT_NEAR(UpdatedOpacity, 37, 1);
}

TEST(SettingsCardDeck, EveryRegisteredCardResolvesANonEmptyDescriptionKey)
{
	ASSERT_FALSE(qm_card_registry::Defaults().empty());
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId != nullptr ? Default.m_pStableId : "<null>");
		ASSERT_NE(Default.m_pStableId, nullptr);
		EXPECT_NE(Default.m_pStableId[0], '\0');
		const char *pDescription = qm_card_registry::ResolveDescriptionKey(Default);
		ASSERT_NE(pDescription, nullptr);
		EXPECT_NE(pDescription[0], '\0');
	}
	EXPECT_EQ(qm_card_registry::ResolveLocalizedDescription(static_cast<const char *>(nullptr)), nullptr);
}

TEST(SettingsCardDeck, EveryCardDeclaresADistinctDescriptionWithinItsPage)
{
	std::unordered_map<std::string, std::unordered_set<std::string>> DescriptionsByTab;
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId);
		ASSERT_NE(Default.m_pDescription, nullptr);
		ASSERT_NE(Default.m_pDescription[0], '\0');
		const std::string Tab = Default.m_pDefaultTab != nullptr ? Default.m_pDefaultTab : "";
		EXPECT_TRUE(DescriptionsByTab[Tab].insert(Default.m_pDescription).second);
	}
}

TEST(SettingsCardDeck, ScrollMovementOnlySuppressesHoverAfterAnInitializedOffset)
{
	EXPECT_FALSE(SettingsCardDeckScrollMoved(false, 0.0f, 12.0f));
	EXPECT_FALSE(SettingsCardDeckScrollMoved(true, 12.0f, 12.0005f));
	EXPECT_TRUE(SettingsCardDeckScrollMoved(true, 12.0f, 13.0f));
}

TEST(SettingsCardDeck, ActiveCardMotionBlocksHeaderDragStart)
{
	EXPECT_TRUE(SettingsCardDeckAllowsDragStart(false, false, false, false));
	EXPECT_FALSE(SettingsCardDeckAllowsDragStart(true, false, false, false));
	EXPECT_FALSE(SettingsCardDeckAllowsDragStart(false, true, false, false));
	EXPECT_FALSE(SettingsCardDeckAllowsDragStart(false, false, true, false));
	EXPECT_FALSE(SettingsCardDeckAllowsDragStart(false, false, false, true));
}

TEST(SettingsCardDeck, SameDisplayCycleTabChangeDoesNotRestartEntry)
{
	CSettingsCardDeckFrameRuntime Runtime;
	Runtime.BeginDisplayCycle(7, true);
	EXPECT_TRUE(Runtime.ConsumeEntryCycle());
	EXPECT_FALSE(Runtime.ConsumeEntryCycle());
	Runtime.SetEntryActive(true);

	Runtime.OnTabChanged();
	EXPECT_FALSE(Runtime.ConsumeEntryCycle());
	EXPECT_FALSE(Runtime.EntryWasActive());

	Runtime.BeginDisplayCycle(8, true);
	EXPECT_TRUE(Runtime.ConsumeEntryCycle());
}

TEST(SettingsCardDeck, ContinuousEntryKeepsPositionAndVelocityAcrossPageSwitches)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	const uint64_t Node = 100;
	Runtime.BeginDisplayCycle(1, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Motion.m_EntryDistance);
	Animation.Advance(0.05f);
	const float BeforeSwitch = Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion);
	ASSERT_GT(BeforeSwitch, 0.0f);
	ASSERT_LT(BeforeSwitch, Motion.m_EntryDistance);
	CUiV2AnimationRuntime Reference = Animation;

	Runtime.BeginDisplayCycle(2, true);
	Runtime.OnTabChanged();
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), BeforeSwitch);
	Animation.Advance(0.01f);
	Reference.Advance(0.01f);
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);

	Runtime.BeginDisplayCycle(3, true);
	Runtime.OnTabChanged();
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
	EXPECT_EQ(Animation.QueuedTrackCount(), 0);
}

TEST(SettingsCardDeck, SubTabStartsEntryInTheClickFrameAndParentCycleDoesNotRestartIt)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 60.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	ASSERT_FALSE(Runtime.EntryWasActive());

	// 子分类输入发生在父设置页的 display cycle 检测之后，必须当帧启动。
	Runtime.OnTabChanged(true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	Animation.Advance(1.0f / 120.0f);
	const float BeforeParentCycle = Animation.GetValue(100, EUiAnimProperty::POS_Y);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), BeforeParentCycle);
	EXPECT_LT(BeforeParentCycle, Motion.m_EntryDistance);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
}

TEST(SettingsCardDeck, ContinuousEntrySettlesWithOnlyASmallReboundAtDifferentRefreshRates)
{
	for(const int RefreshRate : {60, 120, 240})
	{
		SCOPED_TRACE(RefreshRate);
		CSettingsCardDeckFrameRuntime Runtime;
		CUiV2AnimationRuntime Animation;
		const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
		Runtime.BeginDisplayCycle(1, true);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
		float MinimumOffset = Motion.m_EntryDistance;
		for(int Frame = 0; Frame < RefreshRate / 2; ++Frame)
		{
			Animation.Advance(1.0f / RefreshRate);
			const float Offset = Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
			MinimumOffset = std::min(MinimumOffset, Offset);
			EXPECT_LE(Offset, Motion.m_EntryDistance);
		}
		EXPECT_LT(MinimumOffset, 0.0f);
		EXPECT_GT(MinimumOffset, -Motion.m_EntryDistance * 0.03f);
		EXPECT_FALSE(Runtime.EntryWasActive());
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
		EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
	}
}

TEST(SettingsCardDeck, ContinuousEntryCanBeDisabledImmediatelyAndReplayAfterSettling)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	Animation.Advance(0.05f);
	Motion = ResolveCardMotionSpec(0, true, true, true, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_FALSE(Runtime.EntryWasActive());
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);

	Motion = ResolveCardMotionSpec(1, true, true, true, true);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 120.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	EXPECT_FALSE(Runtime.EntryWasActive());
	Runtime.BeginDisplayCycle(3, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);

	Runtime.BeginDisplayCycle(4, false);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);
}

TEST(SettingsDropDown, DisablingOpenStateRequestsPopupCloseAndReleasesSelectionState)
{
	CQmDropdownState State;
	SQmDropdownInput Open;
	Open.m_TogglePressed = true;
	Open.m_InitialIndex = 2;
	ASSERT_TRUE(State.Update(Open, 4).m_Opened);
	ASSERT_TRUE(State.IsOpen());

	EXPECT_TRUE(State.Disable(true));
	EXPECT_FALSE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), -1);
	EXPECT_FALSE(State.Disable(false));
}

TEST(SettingsDropDown, OpenSelectAndCloseTransitionsReleaseThePopup)
{
	CQmDropdownState State;
	SQmDropdownInput Open;
	Open.m_TogglePressed = true;
	Open.m_InitialIndex = 0;
	EXPECT_TRUE(State.Update(Open, 9).m_Opened);
	EXPECT_TRUE(State.IsOpen());

	SQmDropdownInput Select;
	Select.m_HoveredIndex = 4;
	Select.m_MouseSelectPressed = true;
	const SQmDropdownUpdateResult Selected = State.Update(Select, 9);
	EXPECT_TRUE(Selected.m_Selected);
	EXPECT_EQ(Selected.m_SelectedIndex, 4);
	EXPECT_FALSE(State.IsOpen());

	SQmDropdownInput Reopen;
	Reopen.m_TogglePressed = true;
	Reopen.m_InitialIndex = 4;
	EXPECT_TRUE(State.Update(Reopen, 9).m_Opened);
	SQmDropdownInput Escape;
	Escape.m_KeyEscape = true;
	EXPECT_TRUE(State.Update(Escape, 9).m_Closed);
	EXPECT_FALSE(State.IsOpen());
}

TEST(SettingsCardDeck, DefinitionsRevisionInvalidatesMeasurements)
{
	EXPECT_TRUE(SettingsCardDeckDefinitionsRevisionChanged(false, 0, 0));
	EXPECT_FALSE(SettingsCardDeckDefinitionsRevisionChanged(true, 17, 17));
	EXPECT_TRUE(SettingsCardDeckDefinitionsRevisionChanged(true, 17, 18));
	EXPECT_TRUE(SettingsCardDeckDefinitionsCacheKeyChanged(true, 17, 17, "graphics", "controls"));
	EXPECT_FALSE(SettingsCardDeckDefinitionsCacheKeyChanged(true, 17, 17, "graphics", "graphics"));
}

TEST(SettingsCardDeck, InnerSurfaceCompensatesBorderWithoutTintingCardBackground)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.70f);
	ColorRGBA Border(0.90f, 0.15f, 0.10f, 0.20f);
	const ColorRGBA Inner = ResolveSettingsCardInnerSurfaceColor(Surface, Border);
	Border.a = std::min(Border.a, Surface.a - 0.001f);
	const float CombinedAlpha = Inner.a + Border.a * (1.0f - Inner.a);
	const auto CombinedChannel = [&](const float InnerChannel, const float BorderChannel) {
		return InnerChannel * Inner.a + BorderChannel * Border.a * (1.0f - Inner.a);
	};
	EXPECT_NEAR(CombinedAlpha, Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.r, Border.r), Surface.r * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.g, Border.g), Surface.g * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.b, Border.b), Surface.b * Surface.a, 0.001f);
}

TEST(SettingsCardDeck, EffectiveBorderAlphaCannotPolluteATranslucentSurface)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.20f);
	const ColorRGBA Border(0.90f, 0.15f, 0.10f, 1.0f);
	const ColorRGBA Effective = ResolveSettingsCardEffectiveBorderColor(Border, Surface);
	const ColorRGBA Inner = ResolveSettingsCardInnerSurfaceColor(Surface, Effective);
	const float CombinedAlpha = Inner.a + Effective.a * (1.0f - Inner.a);
	const auto CombinedChannel = [&](const float InnerChannel, const float BorderChannel) {
		return InnerChannel * Inner.a + BorderChannel * Effective.a * (1.0f - Inner.a);
	};

	EXPECT_LT(Effective.a, Surface.a);
	EXPECT_NEAR(CombinedAlpha, Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.r, Effective.r), Surface.r * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.g, Effective.g), Surface.g * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.b, Effective.b), Surface.b * Surface.a, 0.001f);
}

TEST(SettingsCardDeck, ConfiguredBorderColorDoesNotTintSurface)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.70f);
	SSettingsCardVisualState State;
	const ColorRGBA Resolved = ResolveSettingsCardSurfaceColor(Surface, State);
	EXPECT_FLOAT_EQ(Resolved.r, Surface.r);
	EXPECT_FLOAT_EQ(Resolved.g, Surface.g);
	EXPECT_FLOAT_EQ(Resolved.b, Surface.b);
	EXPECT_FLOAT_EQ(Resolved.a, Surface.a);
}

TEST(SettingsCardDeck, ContentHeightAnimationSkipsStableFramesAndSnapsFirstLayout)
{
	const SSettingsCardHeightAnimationWork FirstLayout = ResolveSettingsCardHeightAnimationWork(true, false, false, 0.18f, false);
	EXPECT_FALSE(FirstLayout.m_ResolveHeight);
	EXPECT_FALSE(FirstLayout.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Stable = ResolveSettingsCardHeightAnimationWork(false, false, false, 0.18f, false);
	EXPECT_FALSE(Stable.m_ResolveHeight);
	EXPECT_FALSE(Stable.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Changed = ResolveSettingsCardHeightAnimationWork(false, true, false, 0.18f, false);
	EXPECT_TRUE(Changed.m_ResolveHeight);
	EXPECT_FALSE(Changed.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Active = ResolveSettingsCardHeightAnimationWork(false, false, true, 0.18f, false);
	EXPECT_TRUE(Active.m_ResolveHeight);
	EXPECT_FALSE(Active.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork Disabled = ResolveSettingsCardHeightAnimationWork(false, true, true, 0.0f, false);
	EXPECT_FALSE(Disabled.m_ResolveHeight);
	EXPECT_TRUE(Disabled.m_SetHeightTarget);

	const SSettingsCardHeightAnimationWork DragSnap = ResolveSettingsCardHeightAnimationWork(false, true, true, 0.18f, true);
	EXPECT_FALSE(DragSnap.m_ResolveHeight);
	EXPECT_TRUE(DragSnap.m_SetHeightTarget);
}

TEST(SettingsCardDeck, AnimatedColumnFramesNeverOverlapFollowingCards)
{
	for(const float Progress : {0.0f, 0.2f, 0.5f, 0.8f, 1.0f})
	{
		CSettingsCardColumnFramePlan ColumnPlan(100.0f, 10.0f);
		const SSettingsCardColumnFrame First = ColumnPlan.Append(240.0f + (60.0f - 240.0f) * Progress);
		const SSettingsCardColumnFrame Second = ColumnPlan.Append(90.0f + (180.0f - 90.0f) * Progress);
		const SSettingsCardColumnFrame Third = ColumnPlan.Append(120.0f + (40.0f - 120.0f) * Progress);
		EXPECT_FLOAT_EQ(Second.m_Y, First.m_NextY);
		EXPECT_FLOAT_EQ(Third.m_Y, Second.m_NextY);
		EXPECT_GE(Second.m_Y, First.m_Y + First.m_Height + 10.0f);
		EXPECT_GE(Third.m_Y, Second.m_Y + Second.m_Height + 10.0f);
		EXPECT_FLOAT_EQ(ColumnPlan.CursorY(), Third.m_NextY);
	}

	CSettingsCardColumnFramePlan ClampedPlan(50.0f, -5.0f);
	const SSettingsCardColumnFrame Clamped = ClampedPlan.Append(-20.0f);
	EXPECT_FLOAT_EQ(Clamped.m_Height, 0.0f);
	EXPECT_FLOAT_EQ(Clamped.m_NextY, 50.0f);
}

TEST(SettingsCardDeck, RuntimeHeightAnimationKeepsFollowingGeometryDisjoint)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;
	CSettingsCardDeckFrameRuntime FrameRuntime;
	SSettingsCardDeckFrameDiagnostics Diagnostics;
	const SSettingsCardSpec Spec{"animated-card", "Animated", nullptr};
	constexpr uint64_t HeightKey = 0x51f5a7ULL;
	constexpr float InitialHeight = 60.0f;
	constexpr float TargetHeight = 220.0f;
	constexpr float FollowingHeight = 90.0f;
	constexpr float CardGap = 10.0f;
	Runtime.SetValue(HeightKey, EUiAnimProperty::HEIGHT, InitialHeight);

	float AnimatedHeight = ResolveUiAnimValue(Runtime, HeightKey, EUiAnimProperty::HEIGHT, TargetHeight, 0.18f, EEasing::EASE_OUT);
	EXPECT_FLOAT_EQ(AnimatedHeight, InitialHeight);
	for(int FrameIndex = 0; FrameIndex < 16; ++FrameIndex)
	{
		FrameRuntime.BeginFrame(&Diagnostics);
		CSettingsCardColumnFramePlan ColumnPlan(100.0f, CardGap);
		const SSettingsCardFrame FirstCard = BuildSettingsCardFrame({10.0f, ColumnPlan.CursorY(), 200.0f, 0.0f}, Spec, AnimatedHeight, 1.0f);
		const SSettingsCardColumnFrame First = ColumnPlan.Append(FirstCard.m_Rect.h);
		FrameRuntime.RecordGeometry({Spec.m_pStableId, 1, FirstCard.m_Rect, TargetHeight, FirstCard.m_ContentRect.h, FrameIndex == 0, Runtime.HasActiveAnimation(HeightKey, EUiAnimProperty::HEIGHT)});
		const SSettingsCardFrame FollowingCard = BuildSettingsCardFrame({10.0f, ColumnPlan.CursorY(), 200.0f, 0.0f}, Spec, FollowingHeight, 1.0f);
		const SSettingsCardColumnFrame Following = ColumnPlan.Append(FollowingCard.m_Rect.h);
		EXPECT_GE(Following.m_Y, First.m_Y + First.m_Height + CardGap);
		EXPECT_FLOAT_EQ(Following.m_Y, First.m_NextY);
		ASSERT_EQ(Diagnostics.m_GeometryCount, 1u);
		EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_AnimatedContentHeight, AnimatedHeight);
		EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_Rect.h, First.m_Height);

		Runtime.Advance(1.0f / 60.0f);
		if(Runtime.HasActiveAnimation(HeightKey, EUiAnimProperty::HEIGHT))
			AnimatedHeight = ResolveUiAnimValue(Runtime, HeightKey, EUiAnimProperty::HEIGHT, TargetHeight, 0.18f, EEasing::EASE_OUT);
		else
			AnimatedHeight = Runtime.GetValue(HeightKey, EUiAnimProperty::HEIGHT, TargetHeight);
	}
	EXPECT_NEAR(AnimatedHeight, TargetHeight, 0.001f);
}

TEST(SettingsCardDeck, OptionalFrameDiagnosticsRecordAnimatedGeometryWithoutAllocation)
{
	SSettingsCardDeckFrameDiagnostics Diagnostics;
	CSettingsCardDeckFrameRuntime Runtime;
	Runtime.BeginFrame(&Diagnostics);
	for(size_t Index = 0; Index < SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2; ++Index)
	{
		Runtime.RecordGeometry({
			"card",
			1,
			{10.0f, 20.0f + (float)Index * 50.0f, 200.0f, 40.0f},
			120.0f,
			Index == 0 ? 120.0f : 80.0f,
			Index == 0,
			Index != 0,
		});
	}

	EXPECT_EQ(Diagnostics.m_TotalGeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2);
	EXPECT_EQ(Diagnostics.m_GeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY);
	EXPECT_STREQ(Diagnostics.m_aGeometry[0].m_pStableId, "card");
	EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_TargetContentHeight, 120.0f);
	EXPECT_FLOAT_EQ(Diagnostics.m_aGeometry[0].m_AnimatedContentHeight, 120.0f);
	EXPECT_TRUE(Diagnostics.m_aGeometry[0].m_FirstLayout);
	EXPECT_FALSE(Diagnostics.m_aGeometry[0].m_HeightAnimationActive);

	Runtime.BeginFrame(nullptr);
	Runtime.RecordGeometry({"ignored", 0, {}, 1.0f, 1.0f, false, false});
	EXPECT_EQ(Diagnostics.m_TotalGeometryCount, SSettingsCardDeckFrameDiagnostics::MAX_GEOMETRY + 2);
}

TEST(SettingsCardDeck, ClipsOnlyWhileContentHeightIsAnimating)
{
	EXPECT_FALSE(SettingsCardDeckShouldClipContent(true, false));
	EXPECT_TRUE(SettingsCardDeckShouldClipContent(true, true));
	EXPECT_FALSE(SettingsCardDeckShouldClipContent(false, true));
}

TEST(SettingsCardDeck, DefaultCollapseStateUsesStableIdAcrossTabs)
{
	std::unordered_map<std::string, bool> States;
	SettingsCardDeckStoreCollapsed(States, "graphics-display", true);
	SettingsCardDeckStoreCollapsed(States, "controls-gamepad", false);

	EXPECT_TRUE(SettingsCardDeckLoadCollapsed(States, "graphics-display", false));
	EXPECT_FALSE(SettingsCardDeckLoadCollapsed(States, "controls-gamepad", true));
	EXPECT_TRUE(SettingsCardDeckLoadCollapsed(States, "missing", true));
}

TEST(SettingsCardDeck, ExplicitCollapseStateOverridesCachedStableIdState)
{
	std::unordered_map<std::string, bool> States;
	SettingsCardDeckStoreCollapsed(States, "qm:coords", true);
	const bool CachedCollapsed = SettingsCardDeckLoadCollapsed(States, "qm:coords", false);
	EXPECT_TRUE(CachedCollapsed);
	// 卡片自带折叠状态时以它为准，缓存与默认状态都不参与；没有自定义状态时才跟随 Deck 的公共折叠状态。
	EXPECT_TRUE(SettingsCardDeckResolveCollapsed(true, true, !CachedCollapsed));
	EXPECT_FALSE(SettingsCardDeckResolveCollapsed(true, false, CachedCollapsed));
	EXPECT_TRUE(SettingsCardDeckResolveCollapsed(false, false, CachedCollapsed));
}

TEST(SettingsCardDeck, DragPlacementUsesVisualOrderWithoutRendering)
{
	std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{},
		std::vector<int>{4, 7},
		std::vector<int>{9},
	};
	ApplySettingsCardDeckDragPlacement(aColumns, 7, 2, 1);
	EXPECT_EQ(aColumns[1], (std::vector<int>{4}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{9, 7}));

	// 意图：layout 已按每列视觉顺序收集 geometry，热路径无需再排序或分配。
	const std::vector<SSettingsCardDeckItemGeometry> vItems{
		{9, 2, {500.0f, 100.0f, 300.0f, 120.0f}},
		{7, 2, {500.0f, 168.0f, 300.0f, 52.0f}},
		{8, 2, {500.0f, 236.0f, 300.0f, 120.0f}},
	};
	EXPECT_EQ(ResolveSettingsCardDeckDropOrder(200.0f, 2, vItems, 7), 1);
}

TEST(SettingsCardDeck, SingleColumnDragPreservesCanonicalColumnCapacity)
{
	std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{2},
		std::vector<int>{4, 7},
		std::vector<int>{9, 11},
	};
	ApplySettingsCardDeckSingleColumnDragPlacement(aColumns, 11, 1);
	EXPECT_EQ(aColumns[0], (std::vector<int>{2}));
	EXPECT_EQ(aColumns[1], (std::vector<int>{4, 9}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{11, 7}));

	std::array<std::vector<int>, 3> aUnevenColumns{
		std::vector<int>{},
		std::vector<int>{10, 11},
		std::vector<int>{20},
	};
	ApplySettingsCardDeckSingleColumnDragPlacement(aUnevenColumns, 11, 1);
	EXPECT_EQ(aUnevenColumns[1], (std::vector<int>{10, 20}));
	EXPECT_EQ(aUnevenColumns[2], (std::vector<int>{11}));
	std::vector<int> vVisualOrder;
	ForEachSettingsCardDeckVisualOrder(aUnevenColumns, [&](int StateIndex, int Column) {
		if(Column != 0)
			vVisualOrder.push_back(StateIndex);
	});
	EXPECT_EQ(vVisualOrder, (std::vector<int>{10, 11, 20}));
}

TEST(SettingsCardDeck, SingleColumnVisualOrderPreservesWideReadingLayers)
{
	const std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{30},
		std::vector<int>{10, 11},
		std::vector<int>{20},
	};
	std::vector<std::pair<int, int>> vVisualOrder;
	ForEachSettingsCardDeckVisualOrder(aColumns, [&](int StateIndex, int Column) {
		vVisualOrder.emplace_back(StateIndex, Column);
	});
	EXPECT_EQ(vVisualOrder, (std::vector<std::pair<int, int>>{{10, 1}, {20, 2}, {30, 0}, {11, 1}}));
}

TEST(SettingsCardDeck, SingleColumnDropRestoresDeterministicWideLayout)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"full", "sound", 0, 0},
		{"left-a", "sound", 1, 0},
		{"left-b", "sound", 1, 1},
		{"right-a", "sound", 2, 0},
		{"right-b", "sound", 2, 1},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("full"),
		Model.StateIndexForStableId("left-a"),
		Model.StateIndexForStableId("left-b"),
		Model.StateIndexForStableId("right-a"),
		Model.StateIndexForStableId("right-b"),
	};

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "right-b", 1, vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 0), (std::vector<std::string>{"full"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), (std::vector<std::string>{"left-a", "right-a"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"right-b", "left-b"}));
}

TEST(SettingsCardDeck, SingleColumnDropPreservesHiddenCardsInCanonicalColumns)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"left-a", "sound", 1, 0},
		{"left-hidden", "sound", 1, 1},
		{"left-b", "sound", 1, 2},
		{"right-hidden", "sound", 2, 0},
		{"right-a", "sound", 2, 1},
		{"right-b", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("left-a"),
		Model.StateIndexForStableId("left-b"),
		Model.StateIndexForStableId("right-a"),
		Model.StateIndexForStableId("right-b"),
	};

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "right-b", 1, vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), (std::vector<std::string>{"left-a", "left-hidden", "right-a"}));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"right-hidden", "right-b", "left-b"}));

	ASSERT_TRUE(CommitSettingsCardDeckSingleColumnDrop(Model, "sound", "left-a", 3, vActiveStateIndices));
	const std::vector<std::string> vExpectedLeft{"right-b", "left-hidden", "left-b"};
	const std::vector<std::string> vExpectedRight{"right-hidden", "right-a", "left-a"};
	EXPECT_EQ(Model.StableIdOrder("", "sound", 1), vExpectedLeft);
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), vExpectedRight);

	char aSerialized[1024];
	ASSERT_TRUE(Model.Serialize(aSerialized, sizeof(aSerialized)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadExplicit(aSerialized, {
							       {"left-a", "sound", 1, 0},
							       {"left-hidden", "sound", 1, 1},
							       {"left-b", "sound", 1, 2},
							       {"right-hidden", "sound", 2, 0},
							       {"right-a", "sound", 2, 1},
							       {"right-b", "sound", 2, 2},
						       }));
	EXPECT_EQ(Reloaded.StableIdOrder("", "sound", 1), vExpectedLeft);
	EXPECT_EQ(Reloaded.StableIdOrder("", "sound", 2), vExpectedRight);
}

TEST(SettingsCardDeck, ColumnProjectionExcludesInactiveDefinitions)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("deck:graphics-visual"),
		Model.StateIndexForStableId("deck:graphics-modes"),
	};
	const auto aColumns = BuildSettingsCardDeckColumnOrder(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(aColumns[0], (std::vector<int>{}));
	EXPECT_EQ(aColumns[1], (std::vector<int>{Model.StateIndexForStableId("deck:graphics-visual")}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{Model.StateIndexForStableId("deck:graphics-modes")}));
}

TEST(SettingsCardDeck, ProductionPagePlacementsPreserveWideColumnsAndNarrowReadingOrder)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	const auto VerifyPage = [&Model](const char *pTab, const std::vector<const char *> &vStableIds, const std::array<std::vector<const char *>, 3> &aExpectedColumns, const std::vector<const char *> &vExpectedVisualOrder) {
		std::vector<int> vActiveStateIndices;
		vActiveStateIndices.reserve(vStableIds.size());
		for(const char *pStableId : vStableIds)
		{
			const int StateIndex = Model.StateIndexForStableId(pStableId);
			ASSERT_GE(StateIndex, 0) << pStableId;
			vActiveStateIndices.push_back(StateIndex);
		}

		const std::array<std::vector<int>, 3> aColumns = BuildSettingsCardDeckColumnOrder(Model, pTab, vActiveStateIndices);
		for(int Column = 0; Column < 3; ++Column)
		{
			ASSERT_EQ(aColumns[Column].size(), aExpectedColumns[Column].size());
			for(size_t Index = 0; Index < aColumns[Column].size(); ++Index)
				EXPECT_STREQ(Model.Entry(aColumns[Column][Index]).m_pStableId, aExpectedColumns[Column][Index]);
		}

		std::vector<const char *> vVisualOrder;
		ForEachSettingsCardDeckVisualOrder(aColumns, [&](const int StateIndex, int) {
			vVisualOrder.push_back(Model.Entry(StateIndex).m_pStableId);
		});
		ASSERT_EQ(vVisualOrder.size(), vExpectedVisualOrder.size());
		for(size_t Index = 0; Index < vVisualOrder.size(); ++Index)
			EXPECT_STREQ(vVisualOrder[Index], vExpectedVisualOrder[Index]);
	};

	VerifyPage("tee",
		{"deck:tee-identity", "deck:tee-skin-options", "deck:tee-skin-list"},
		{{{"deck:tee-skin-list"}, {"deck:tee-identity"}, {"deck:tee-skin-options"}}},
		{"deck:tee-identity", "deck:tee-skin-options", "deck:tee-skin-list"});
	VerifyPage("appearance-chat",
		{"deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview"},
		{{{}, {"deck:appearance-chat-settings", "deck:appearance-chat-preview"}, {"deck:appearance-chat-messages"}}},
		{"deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview"});
	VerifyPage("tclient-status-bar",
		{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"},
		{{{}, {"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"}, {}}},
		{"deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"});
}

TEST(SettingsCardDeck, ColumnProjectionCacheRebuildsOnlyForLayoutOrActiveDefinitionChanges)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const int Visual = Model.StateIndexForStableId("deck:graphics-visual");
	const int Modes = Model.StateIndexForStableId("deck:graphics-modes");
	const std::vector<int> vActiveStateIndices{Visual, Modes};
	settings_card_deck_logic::CProjectionCache Cache;

	const auto &aInitialColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);
	EXPECT_EQ(aInitialColumns[1], (std::vector<int>{Visual}));
	EXPECT_EQ(aInitialColumns[2], (std::vector<int>{Modes}));

	Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 1u);

	const std::vector<int> vVisualOnly{Visual};
	const auto &aFilteredColumns = Cache.Resolve(Model, "graphics", vVisualOnly);
	EXPECT_EQ(Cache.RebuildCount(), 2u);
	EXPECT_EQ(aFilteredColumns[2], (std::vector<int>{}));

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "graphics", "deck:graphics-visual", 2, 0));
	const auto &aMovedColumns = Cache.Resolve(Model, "graphics", vActiveStateIndices);
	EXPECT_EQ(Cache.RebuildCount(), 3u);
	EXPECT_EQ(aMovedColumns[2], (std::vector<int>{Visual, Modes}));
}
TEST(SettingsCardDeck, StateIndexRevisionChangesWhenSameSizedModelIsRebuilt)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"card-a", "settings", 1, 0},
		{"card-b", "settings", 2, 0},
	});
	const uint64_t InitialRevision = Model.StateIndexRevision();
	EXPECT_EQ(Model.StateIndexForStableId("card-a"), 0);
	EXPECT_EQ(Model.StateIndexForStableId("card-b"), 1);

	Model.SetEntries({
		{"card-b", "settings", 2, 0},
		{"card-a", "settings", 1, 0},
	});

	EXPECT_GT(Model.StateIndexRevision(), InitialRevision);
	EXPECT_EQ(Model.Count(), 2);
	EXPECT_EQ(Model.StateIndexForStableId("card-b"), 0);
	EXPECT_EQ(Model.StateIndexForStableId("card-a"), 1);
}

TEST(SettingsCardDeck, CrossColumnDropMovesOnlyTheGlobalModel)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "graphics", "deck:graphics-display", 2, 0));
	const int Index = Model.FindByStableId("deck:graphics-display");
	ASSERT_GE(Index, 0);
	EXPECT_EQ(Model.Entry(Index).m_Column, 2);
	EXPECT_EQ(Model.Entry(Index).m_OrderInColumn, 0);
	EXPECT_STREQ(Model.Entry(Index).m_pDefaultTab, "graphics");
	EXPECT_TRUE(Model.IsDirty());
}

TEST(SettingsCardDeck, CrossColumnDropUsesVisibleOrderWhenHiddenCardsInterleave)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 1, 0},
		{"sound-hidden-before", "sound", 2, 0},
		{"sound-visible", "sound", 2, 1},
		{"sound-hidden-after", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("sound-toggle"),
		Model.StateIndexForStableId("sound-visible"),
	};

	// 拖拽只按当前可见卡片计数，隐藏卡片仍保留在持久化顺序中。
	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 1, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-hidden-before", "sound-visible", "sound-toggle", "sound-hidden-after"}));
}

TEST(SettingsCardDeck, EmptyVisibleColumnDropsBeforeHiddenCards)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 1, 0},
		{"sound-hidden", "sound", 2, 0},
	});
	const std::vector<int> vActiveStateIndices{Model.StateIndexForStableId("sound-toggle")};

	ASSERT_TRUE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 0, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-toggle", "sound-hidden"}));
}

TEST(SettingsCardDeck, SameColumnVisualNoOpPreservesHiddenRelativeOrder)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"sound-toggle", "sound", 2, 0},
		{"sound-hidden", "sound", 2, 1},
		{"sound-visible", "sound", 2, 2},
	});
	const std::vector<int> vActiveStateIndices{
		Model.StateIndexForStableId("sound-toggle"),
		Model.StateIndexForStableId("sound-visible"),
	};

	EXPECT_FALSE(CommitSettingsCardDeckDrop(Model, "sound", "sound-toggle", 2, 0, &vActiveStateIndices));
	EXPECT_EQ(Model.StableIdOrder("", "sound", 2), (std::vector<std::string>{"sound-toggle", "sound-hidden", "sound-visible"}));
}

TEST(SettingsCardDeck, OrdinaryCardsUseDefaultCollapseWhileCustomCardsRemainAuthoritative)
{
	EXPECT_TRUE(SettingsCardDeckUsesDefaultCollapseControl(false, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(false, true));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, true));

	EXPECT_FALSE(SettingsCardDeckResolveCollapsed(false, true, false));
	EXPECT_TRUE(SettingsCardDeckResolveCollapsed(false, false, true));
	EXPECT_TRUE(SettingsCardDeckResolveCollapsed(true, true, false));
	EXPECT_FALSE(SettingsCardDeckResolveCollapsed(true, false, true));
}

TEST(SettingsCardDeck, DisplayViewKeyChangesWhenAnySettingsSubTabChanges)
{
	const uint64_t Base = ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 0);
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(1, 0, 0, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 1, 0, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 1, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 1, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 1));
	EXPECT_EQ(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 0));
}

TEST(SettingsCardDeck, GeometryMotionIncludesCardsPushedByAnEarlierHeightAnimation)
{
	EXPECT_FALSE(SettingsCardDeckGeometryMoved(false, 100.0f, 80.0f, 120.0f, 80.0f));
	EXPECT_FALSE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 100.0f, 80.0f));
	EXPECT_TRUE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 120.0f, 80.0f));
	EXPECT_TRUE(SettingsCardDeckGeometryMoved(true, 100.0f, 80.0f, 100.0f, 90.0f));
}

TEST(SettingsCardDeck, AnimatedColumnFramesNeverOverlap)
{
	const float Gap = 12.0f;
	const SSettingsCardColumnFrame First = ResolveSettingsCardColumnFrame(100.0f, 80.0f, Gap);
	const SSettingsCardColumnFrame Second = ResolveSettingsCardColumnFrame(First.m_NextY, 140.0f, Gap);
	EXPECT_FLOAT_EQ(First.m_NextY, Second.m_Y);
	EXPECT_GE(Second.m_Y, First.m_Y + First.m_Height + Gap);
	EXPECT_FLOAT_EQ(Second.m_Height, 140.0f);
}

TEST(SettingsCardDeck, DrawGeometryKeepsSubpixelOffsetsThroughMotionAndRest)
{
	SSettingsCardSpec Spec;
	const SSettingsCardFrame Start = BuildSettingsCardFrame({10.2f, 20.3f, 199.6f, 0.0f}, Spec, 49.4f, 1.0f);
	for(const float Offset : {0.0f, 0.025f, 0.05f, 0.075f, 0.05f, 0.025f, 0.0f})
	{
		const SSettingsCardFrame Draw = ResolveSettingsCardDrawFrame(Start, Offset, Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.x, Start.m_Rect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.y, Start.m_Rect.y + Offset);
		EXPECT_FLOAT_EQ(Draw.m_Rect.w, Start.m_Rect.w);
		EXPECT_FLOAT_EQ(Draw.m_Rect.h, Start.m_Rect.h);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.x, Start.m_HandleRect.x + Offset);
		EXPECT_FLOAT_EQ(Draw.m_HandleRect.y, Start.m_HandleRect.y + Offset);
		EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.3f, 0.5f), 2.5f);
	}
}

TEST(SettingsCardDeck, EveryCardDescriptionHasASimplifiedChineseRuntimeTranslation)
{
	const std::string SimplifiedChinese = ReadTestSourceFile("data/languages/simplified_chinese.txt");
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		SCOPED_TRACE(Default.m_pStableId);
		const char *pDescription = qm_card_registry::ResolveDescriptionKey(Default);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pDescription);
		ASSERT_FALSE(Translation.empty()) << pDescription;
		EXPECT_NE(Translation, pDescription);
	}
}

TEST(SettingsCardDeck, AuditedUiLabelsHaveSimplifiedChineseRuntimeTranslations)
{
	const std::string SimplifiedChinese = ReadTestSourceFile("data/languages/simplified_chinese.txt");
	static const char *const s_apKeys[] = {
		"Card height animation",
		"Card list entry animation",
		"Card reflow animation",
		"Enable enhanced scoreboard presentation",
		// 本地把该配置改成 qm_graphics_trace 的已弃用别名（本地独有演进），按本地当前 Desc 钉住标签。
		"Deprecated compatibility alias for qm_graphics_trace (macOS signposts)",
		"Enable smooth cinematic camera while free spectating",
		"Global UI size percentage",
		"Gores",
		"Hide chat messages from players marked as enemies",
		"Interface surface",
		"Map browser surface",
		"Ping",
		"Relative X position of the draggable back button",
		"Relative Y position of the draggable back button",
		"RTT",
		"Scoreboard surface",
		"Text input focus ring color",
		"Presentation animations",
		"Show draggable virtual back button",
		"UI rounded corner segments (even numbers recommended)",
		"UI icon custom color",
		"Word filter action: 0=replace matching words, 1=hide entire message",
	};
	for(const char *pKey : s_apKeys)
	{
		SCOPED_TRACE(pKey);
		const std::string Translation = FindRuntimeTranslation(SimplifiedChinese, pKey);
		ASSERT_FALSE(Translation.empty());
		EXPECT_NE(Translation, pKey);
	}
}

TEST(SettingsCardDeck, ContinuousEntryKeepsPositionAndVelocityAcrossPageSwitches)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	const uint64_t Node = 100;
	Runtime.BeginDisplayCycle(1, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Motion.m_EntryDistance);
	Animation.Advance(0.05f);
	const float BeforeSwitch = Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion);
	ASSERT_GT(BeforeSwitch, 0.0f);
	ASSERT_LT(BeforeSwitch, Motion.m_EntryDistance);
	CUiV2AnimationRuntime Reference = Animation;

	Runtime.BeginDisplayCycle(2, true);
	Runtime.OnTabChanged();
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), BeforeSwitch);
	Animation.Advance(0.01f);
	Reference.Advance(0.01f);
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);

	Runtime.BeginDisplayCycle(3, true);
	Runtime.OnTabChanged();
	EXPECT_NEAR(Runtime.ResolveContinuousEntryOffset(Animation, Node, Motion), Reference.GetValue(Node, EUiAnimProperty::POS_Y), 0.0001f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
	EXPECT_EQ(Animation.QueuedTrackCount(), 0);
}

TEST(SettingsCardDeck, SubTabStartsEntryInTheClickFrameAndParentCycleDoesNotRestartIt)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 60.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	ASSERT_FALSE(Runtime.EntryWasActive());

	// 子分类输入发生在父设置页的 display cycle 检测之后，必须当帧启动。
	Runtime.OnTabChanged(true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	Animation.Advance(1.0f / 120.0f);
	const float BeforeParentCycle = Animation.GetValue(100, EUiAnimProperty::POS_Y);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), BeforeParentCycle);
	EXPECT_LT(BeforeParentCycle, Motion.m_EntryDistance);
	EXPECT_EQ(Animation.ActiveTrackCount(), 1);
}

TEST(SettingsCardDeck, ContinuousEntrySettlesWithOnlyASmallReboundAtDifferentRefreshRates)
{
	for(const int RefreshRate : {60, 120, 240})
	{
		SCOPED_TRACE(RefreshRate);
		CSettingsCardDeckFrameRuntime Runtime;
		CUiV2AnimationRuntime Animation;
		const SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
		Runtime.BeginDisplayCycle(1, true);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
		float MinimumOffset = Motion.m_EntryDistance;
		for(int Frame = 0; Frame < RefreshRate / 2; ++Frame)
		{
			Animation.Advance(1.0f / RefreshRate);
			const float Offset = Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
			MinimumOffset = std::min(MinimumOffset, Offset);
			EXPECT_LE(Offset, Motion.m_EntryDistance);
		}
		EXPECT_LT(MinimumOffset, 0.0f);
		EXPECT_GT(MinimumOffset, -Motion.m_EntryDistance * 0.03f);
		EXPECT_FALSE(Runtime.EntryWasActive());
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
		EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
		EXPECT_EQ(Animation.ActiveTrackCount(), 0);
	}
}

TEST(SettingsCardDeck, ContinuousEntryCanBeDisabledImmediatelyAndReplayAfterSettling)
{
	CSettingsCardDeckFrameRuntime Runtime;
	CUiV2AnimationRuntime Animation;
	SCardMotionSpec Motion = ResolveCardMotionSpec(2, true, true, true, true);
	Runtime.BeginDisplayCycle(1, true);
	Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	Animation.Advance(0.05f);
	Motion = ResolveCardMotionSpec(0, true, true, true, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_FALSE(Runtime.EntryWasActive());
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);

	Motion = ResolveCardMotionSpec(1, true, true, true, true);
	Runtime.BeginDisplayCycle(2, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);
	for(int Frame = 0; Frame < 30; ++Frame)
	{
		Animation.Advance(1.0f / 120.0f);
		Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion);
	}
	EXPECT_FALSE(Runtime.EntryWasActive());
	Runtime.BeginDisplayCycle(3, true);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), Motion.m_EntryDistance);

	Runtime.BeginDisplayCycle(4, false);
	EXPECT_FLOAT_EQ(Runtime.ResolveContinuousEntryOffset(Animation, 100, Motion), 0.0f);
	EXPECT_EQ(Animation.ActiveTrackCount(), 0);
}

TEST(SettingsCardDeck, StableAnimationFramesSkipRuntimeWork)
{
	const SSettingsCardAnimationWork Stable = ResolveSettingsCardAnimationWork(0.16f, false, false, false, 0.18f, false, false);
	EXPECT_FALSE(Stable.m_ResolveEntry);
	EXPECT_FALSE(Stable.m_ResetEntry);
	EXPECT_FALSE(Stable.m_ResolveReflow);
	EXPECT_FALSE(Stable.m_SetReflowTarget);

	const SSettingsCardAnimationWork TargetChanged = ResolveSettingsCardAnimationWork(0.16f, false, false, false, 0.18f, true, false);
	EXPECT_TRUE(TargetChanged.m_ResolveReflow);
	EXPECT_FALSE(TargetChanged.m_SetReflowTarget);

	const SSettingsCardAnimationWork Active = ResolveSettingsCardAnimationWork(0.16f, true, false, false, 0.18f, false, true);
	EXPECT_TRUE(Active.m_ResolveEntry);
	EXPECT_TRUE(Active.m_ResolveReflow);
}

TEST(SettingsCardDeck, DisabledOrSnappedAnimationsOnlyResetTargets)
{
	const SSettingsCardAnimationWork Disabled = ResolveSettingsCardAnimationWork(0.0f, true, false, false, 0.0f, true, true);
	EXPECT_TRUE(Disabled.m_ResetEntry);
	EXPECT_FALSE(Disabled.m_ResolveReflow);
	EXPECT_TRUE(Disabled.m_SetReflowTarget);

	const SSettingsCardAnimationWork Snapped = ResolveSettingsCardAnimationWork(0.16f, false, false, true, 0.18f, true, true);
	EXPECT_FALSE(Snapped.m_ResolveReflow);
	EXPECT_TRUE(Snapped.m_SetReflowTarget);

	const SSettingsCardAnimationWork FirstFrame = ResolveSettingsCardAnimationWork(0.16f, false, true, false, 0.18f, false, false);
	EXPECT_FALSE(FirstFrame.m_SetReflowTarget);
}
