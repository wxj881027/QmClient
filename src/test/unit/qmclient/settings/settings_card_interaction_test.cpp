#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
#include <game/client/components/menus.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

TEST(SettingsCardInteraction, EdgeDragRequestsBoundedAutoScroll)
{
	const CUIRect Viewport{0.0f, 100.0f, 600.0f, 400.0f};
	EXPECT_LT(SettingsCardDeckAutoScrollDelta(101.0f, Viewport, 1.0f), 0.0f);
	EXPECT_GT(SettingsCardDeckAutoScrollDelta(499.0f, Viewport, 1.0f), 0.0f);
	EXPECT_FLOAT_EQ(SettingsCardDeckAutoScrollDelta(300.0f, Viewport, 1.0f), 0.0f);
}

TEST(SettingsCardInteraction, CollapsedCardsSkipContentWorkAndExpandedDynamicCardsRemeasure)
{
	EXPECT_FALSE(SettingsCardDeckNeedsContentMeasure(true, false, -1.0f));
	EXPECT_FALSE(SettingsCardDeckRendersContent(true));
	EXPECT_TRUE(SettingsCardDeckNeedsContentMeasure(false, false, -1.0f));
	EXPECT_FALSE(SettingsCardDeckNeedsContentMeasure(false, false, 96.0f));
	EXPECT_TRUE(SettingsCardDeckNeedsContentMeasure(false, true, 96.0f));
	EXPECT_TRUE(SettingsCardDeckRendersContent(false));
}

TEST(SettingsCardInteraction, EveryCardUsesTheSharedCollapseControl)
{
	EXPECT_TRUE(SettingsCardDeckUsesDefaultCollapseControl(false, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, false));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(false, true));
	EXPECT_FALSE(SettingsCardDeckUsesDefaultCollapseControl(true, true));
}

TEST(SettingsCardInteraction, OrdinaryCollapseStateTogglesOnlyFromVisibleHeaderInput)
{
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(false, false, true, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(false, true, true, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(false, false, true, true));
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(false, true, false, false));
	// 自定义折叠状态的卡片不能被公共折叠按钮改写。
	EXPECT_TRUE(SettingsCardDeckApplyDefaultCollapseToggle(true, true, false, false));
	EXPECT_FALSE(SettingsCardDeckApplyDefaultCollapseToggle(true, false, true, false));
}

TEST(SettingsCardInteraction, PreLayoutReleaseUsesTheLastVisibleAnimatedFrame)
{
	const SSettingsCardSpec Spec{"card", "Card", "Subtitle"};
	const SSettingsCardFrame TargetFrame = BuildSettingsCardFrame({40.0f, 100.0f, 320.0f, 0.0f}, Spec, 120.0f, 1.0f);
	const SSettingsCardFrame VisibleFrame = ResolveSettingsCardDrawFrame(TargetFrame, 0.0f, 18.0f);
	const float ReleaseX = VisibleFrame.m_HandleRect.x + VisibleFrame.m_HandleRect.w * 0.5f;
	const float ReleaseY = VisibleFrame.m_HandleRect.y + VisibleFrame.m_HandleRect.h - 1.0f;

	EXPECT_FALSE(TargetFrame.m_HandleRect.Inside(vec2(ReleaseX, ReleaseY)));
	EXPECT_TRUE(VisibleFrame.m_HandleRect.Inside(vec2(ReleaseX, ReleaseY)));
	EXPECT_FLOAT_EQ(VisibleFrame.m_Rect.y, TargetFrame.m_Rect.y + 18.0f);
	EXPECT_FLOAT_EQ(VisibleFrame.m_HeaderRect.y, TargetFrame.m_HeaderRect.y + 18.0f);
	EXPECT_FLOAT_EQ(VisibleFrame.m_ContentRect.y, TargetFrame.m_ContentRect.y + 18.0f);
}

TEST(SettingsCardInteraction, CollapseAndVisibilityChangesSnapWithoutDisablingDragReflow)
{
	EXPECT_FALSE(SettingsCardDeckContentHeightChanged(-1.0f, 96.0f));
	EXPECT_FALSE(SettingsCardDeckContentHeightChanged(96.0f, 96.005f));
	EXPECT_TRUE(SettingsCardDeckContentHeightChanged(96.0f, 120.0f));
	EXPECT_TRUE(SettingsCardDeckShouldSnapReflow(true, false));
	EXPECT_FALSE(SettingsCardDeckShouldSnapReflow(false, false));
	EXPECT_FALSE(SettingsCardDeckShouldSnapReflow(true, true));
}

TEST(SettingsCardInteraction, HoverDoesNotChangeCardChrome)
{
	const ColorRGBA BaseSurface(0.12f, 0.24f, 0.36f, 0.48f);
	SSettingsCardVisualState Resting;
	SSettingsCardVisualState Hovered = Resting;
	Hovered.m_Hovered = true;

	EXPECT_FALSE(SettingsCardInteractionBorderVisible(Hovered));

	const ColorRGBA RestingSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Resting);
	const ColorRGBA HoveredSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Hovered);
	EXPECT_FLOAT_EQ(RestingSurface.r, HoveredSurface.r);
	EXPECT_FLOAT_EQ(RestingSurface.g, HoveredSurface.g);
	EXPECT_FLOAT_EQ(RestingSurface.b, HoveredSurface.b);
	EXPECT_FLOAT_EQ(RestingSurface.a, HoveredSurface.a);
}

TEST(SettingsCardInteraction, PreLayoutContentInputRequiresPointerOrPendingInputOrActivePointerContinuation)
{
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, false, 1.0f));
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(false, true, false, true, false, 1.0f));
	EXPECT_TRUE(SettingsCardDeckShouldRunPreLayoutInput(false, false, true, false, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(false, false, false, true, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, false, false, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, true, 1.0f));
	EXPECT_FALSE(SettingsCardDeckShouldRunPreLayoutInput(true, false, false, true, false, 0.0f));
}

TEST(SettingsCardInteraction, ActiveItemContinuationRequiresPointerInput)
{
	EXPECT_TRUE(SettingsCardDeckHasActiveItemContinuation(true, true));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(true, false));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(false, true));
	EXPECT_FALSE(SettingsCardDeckHasActiveItemContinuation(false, false));
}

namespace
{
	class SettingsCardMeasureRevision : public ::testing::Test
	{
		CConfig m_SavedConfig = g_Config;

	protected:
		void TearDown() override { g_Config = m_SavedConfig; }
	};
}

TEST_F(SettingsCardMeasureRevision, TranslationAdvancedRowsInvalidatePageAndSearchMeasurements)
{
	using namespace qm_card_catalog;
	g_Config.m_QmTranslateShowAdvanced = 0;
	const uint64_t PageRevision = MeasureModuleCardRevision(qm_module::EQmModuleId::Translate);
	const uint64_t SearchRevision = MeasureModuleCardsRevision();
	g_Config.m_QmTranslateShowAdvanced = 1;
	EXPECT_NE(PageRevision, MeasureModuleCardRevision(qm_module::EQmModuleId::Translate));
	EXPECT_NE(SearchRevision, MeasureModuleCardsRevision());
	g_Config.m_QmTranslateShowAdvanced = 0;
	EXPECT_EQ(PageRevision, MeasureModuleCardRevision(qm_module::EQmModuleId::Translate));
	EXPECT_EQ(SearchRevision, MeasureModuleCardsRevision());
}

TEST_F(SettingsCardMeasureRevision, TranslationBackendSwitchInvalidatesMeasurements)
{
	using namespace qm_card_catalog;
	// 每个后端都有专属的说明行/密钥行/端点行组合，任意切换都会改变渲染内容；
	// 测量版本必须两两不同，否则 Deck 的缓存高度不失效，卡片高度停留在旧服务的高度。
	static const char *const apBackendCodes[] = {"llm", "tencentcloud", "libretranslate", "ftapi", "mymemory", "deepl", "baidu"};
	uint64_t aRevisions[std::size(apBackendCodes)] = {};
	for(size_t i = 0; i < std::size(apBackendCodes); ++i)
	{
		str_copy(g_Config.m_QmTranslateBackend, apBackendCodes[i], sizeof(g_Config.m_QmTranslateBackend));
		aRevisions[i] = MeasureModuleCardRevision(qm_module::EQmModuleId::Translate);
	}
	for(size_t i = 0; i < std::size(apBackendCodes); ++i)
	{
		for(size_t j = i + 1; j < std::size(apBackendCodes); ++j)
			EXPECT_NE(aRevisions[i], aRevisions[j]) << apBackendCodes[i] << " vs " << apBackendCodes[j];
	}
	// 显式覆盖用户报告的切换路径：mymemory ↔ deepl 切换必须失效缓存高度，切回后还原。
	str_copy(g_Config.m_QmTranslateBackend, "mymemory", sizeof(g_Config.m_QmTranslateBackend));
	const uint64_t MyMemoryRevision = MeasureModuleCardRevision(qm_module::EQmModuleId::Translate);
	str_copy(g_Config.m_QmTranslateBackend, "deepl", sizeof(g_Config.m_QmTranslateBackend));
	EXPECT_NE(MyMemoryRevision, MeasureModuleCardRevision(qm_module::EQmModuleId::Translate));
	str_copy(g_Config.m_QmTranslateBackend, "mymemory", sizeof(g_Config.m_QmTranslateBackend));
	EXPECT_EQ(MyMemoryRevision, MeasureModuleCardRevision(qm_module::EQmModuleId::Translate));
}

TEST_F(SettingsCardMeasureRevision, DynamicHudTogglesInvalidatePageAndSearchMeasurements)
{
	using namespace qm_card_catalog;
	using qm_module::EQmModuleId;
	struct SBranch
	{
		EQmModuleId m_Id;
		int *m_pToggle;
	};
	const SBranch aBranches[] = {
		{EQmModuleId::WeaponAnimation, &g_Config.m_QmWeaponReloadAnim},
		{EQmModuleId::DynamicIsland, &g_Config.m_QmSwitchCountdown},
		{EQmModuleId::Lyrics, &g_Config.m_QmSpotifyEnable},
		{EQmModuleId::Lyrics, &g_Config.m_QmKugouHookEnable},
		{EQmModuleId::Lyrics, &g_Config.m_QmQQMusicHookEnable},
		{EQmModuleId::GoresDrownBoard, &g_Config.m_QmGoresDrownBoard},
		{EQmModuleId::Emoticons, &g_Config.m_QmShowOtherSuperEmotes},
	};
	for(const SBranch &Branch : aBranches)
	{
		SCOPED_TRACE(static_cast<int>(Branch.m_Id));
		*Branch.m_pToggle = 0;
		const uint64_t PageRevision = MeasureModuleCardRevision(Branch.m_Id);
		const uint64_t SearchRevision = MeasureModuleCardsRevision();
		*Branch.m_pToggle = 1;
		EXPECT_NE(PageRevision, MeasureModuleCardRevision(Branch.m_Id));
		EXPECT_NE(SearchRevision, MeasureModuleCardsRevision());
		*Branch.m_pToggle = 0;
		EXPECT_EQ(PageRevision, MeasureModuleCardRevision(Branch.m_Id));
		EXPECT_EQ(SearchRevision, MeasureModuleCardsRevision());
	}
}

TEST_F(SettingsCardMeasureRevision, UnchangedLayoutAndNonLayoutValuesKeepMeasurementsCached)
{
	using namespace qm_card_catalog;
	const uint64_t Revision = MeasureModuleCardsRevision();
	EXPECT_EQ(Revision, MeasureModuleCardsRevision());
	g_Config.m_QmTranslateAuto = !g_Config.m_QmTranslateAuto;
	g_Config.m_QmTranslateAutoOutgoing = !g_Config.m_QmTranslateAutoOutgoing;
	EXPECT_EQ(Revision, MeasureModuleCardsRevision());
}

TEST_F(SettingsCardMeasureRevision, DynamicListChangesInvalidateSearchMeasurements)
{
	using namespace qm_card_catalog;
	SQmFunctionCardLayoutState Layout;
	const uint64_t Original = MeasureModuleCardsRevision(Layout);
	++Layout.m_KeywordRulesRevision;
	EXPECT_NE(Original, MeasureModuleCardsRevision(Layout));
	--Layout.m_KeywordRulesRevision;
	EXPECT_EQ(Original, MeasureModuleCardsRevision(Layout));
	++Layout.m_FavoriteMapsRevision;
	EXPECT_NE(Original, MeasureModuleCardsRevision(Layout));
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

TEST(SettingsCardDeck, DefinitionsRevisionInvalidatesMeasurements)
{
	EXPECT_TRUE(SettingsCardDeckDefinitionsRevisionChanged(false, 0, 0));
	EXPECT_FALSE(SettingsCardDeckDefinitionsRevisionChanged(true, 17, 17));
	EXPECT_TRUE(SettingsCardDeckDefinitionsRevisionChanged(true, 17, 18));
	EXPECT_TRUE(SettingsCardDeckDefinitionsCacheKeyChanged(true, 17, 17, "graphics", "controls"));
	EXPECT_FALSE(SettingsCardDeckDefinitionsCacheKeyChanged(true, 17, 17, "graphics", "graphics"));
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

TEST(SettingsCardInteraction, PopupBlocksCapturedPointerSnapshotWithoutSynthesizingDrop)
{
	SSettingsCardDeckInput Raw;
	Raw.m_MousePressed = true;
	Raw.m_MouseDown = true;
	Raw.m_MouseReleased = true;
	Raw.m_CtrlPressed = true;
	Raw.m_MouseX = 50;
	Raw.m_MouseY = 75;
	const auto Blocked = ResolveSettingsCardDeckPointerInput(Raw, true);
	EXPECT_FALSE(Blocked.m_MousePressed);
	EXPECT_FALSE(Blocked.m_MouseDown);
	EXPECT_FALSE(Blocked.m_MouseReleased);
	EXPECT_FALSE(Blocked.m_CtrlPressed);
	EXPECT_FALSE(Blocked.m_AllowHeaderDrag);
	EXPECT_FLOAT_EQ(Blocked.m_MouseX, Raw.m_MouseX);
	EXPECT_FLOAT_EQ(Blocked.m_FrameDt, Raw.m_FrameDt);
	EXPECT_FALSE(ResolveSettingsCardDeckPointerInput(Blocked, false).m_MousePressed);
	EXPECT_TRUE(ResolveSettingsCardDeckPointerInput(Raw, false).m_MousePressed);
}

TEST(SettingsCardInteraction, PopupOpenedDuringPrelayoutInvalidatesEarlierDragInput)
{
	SSettingsCardDeckInput Raw;
	Raw.m_MousePressed = true;
	Raw.m_MouseDown = true;
	auto Input = ResolveSettingsCardDeckPointerInput(Raw, false);
	ASSERT_TRUE(Input.m_MousePressed);
	Input = ResolveSettingsCardDeckPointerInput(Input, true);
	EXPECT_FALSE(Input.m_MousePressed);
	EXPECT_FALSE(Input.m_MouseDown);
	EXPECT_FALSE(Input.m_MouseReleased);
}

TEST(SettingsCardFocus, OutsideClickAndMissingReleaseDoNotPreventReselection)
{
	qm_card_registry::CCardFocus Focus;
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	ASSERT_TRUE(Focus.IsFocused("card-a"));
	// 外部按下；释放可在设置区域之外发生，不要求下一帧收到释放事件。
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", false);
	Focus.EndFrame();
	ASSERT_FALSE(Focus.IsFocused("card-a"));
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	EXPECT_TRUE(Focus.IsFocused("card-a"));
}

TEST(SettingsCardFocus, SelectionSurvivesIdleFramesAndTransfersBetweenStableIds)
{
	qm_card_registry::CCardFocus Focus;
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.Observe("card-b", false);
	Focus.EndFrame();
	for(int Frame = 0; Frame < 8; ++Frame)
	{
		Focus.BeginFrame(false, true);
		Focus.Observe("card-b", true);
		Focus.Observe("card-a", false);
		Focus.EndFrame();
		EXPECT_TRUE(Focus.IsFocused("card-a"));
		EXPECT_FALSE(Focus.IsFocused("card-b"));
	}
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", false);
	Focus.Observe("card-b", true);
	Focus.EndFrame();
	EXPECT_FALSE(Focus.IsFocused("card-a"));
	EXPECT_TRUE(Focus.IsFocused("card-b"));
}

TEST(SettingsCardFocus, BlockedPopupPointerDoesNotSelectAndClosingAllowsNextClick)
{
	qm_card_registry::CCardFocus Focus;
	SSettingsCardDeckInput Input;
	Input.m_MousePressed = true;
	const auto Blocked = ResolveSettingsCardDeckPointerInput(Input, true);
	Focus.BeginFrame(Blocked.m_MousePressed, true);
	Focus.Observe("card-a", false);
	Focus.EndFrame();
	EXPECT_FALSE(Focus.IsFocused("card-a"));
	const auto Unblocked = ResolveSettingsCardDeckPointerInput(Input, false);
	Focus.BeginFrame(Unblocked.m_MousePressed, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	EXPECT_TRUE(Focus.IsFocused("card-a"));
}

TEST(SettingsCardFocus, ReadOnlyPassCannotClearOrStealInteractiveSelection)
{
	qm_card_registry::CCardFocus Focus;
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	Focus.BeginFrame(true, false);
	Focus.Observe("card-b", true);
	Focus.EndFrame();
	EXPECT_TRUE(Focus.IsFocused("card-a"));
	EXPECT_FALSE(Focus.IsFocused("card-b"));
}

TEST(SettingsCardFocus, HiddenCardAndDisplayResetInvalidateSelectionAndAllowReopen)
{
	qm_card_registry::CCardFocus Focus;
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	Focus.BeginFrame(false, true);
	Focus.Observe("card-b", false);
	Focus.EndFrame();
	EXPECT_FALSE(Focus.IsFocused("card-a"));
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	Focus.Reset();
	EXPECT_FALSE(Focus.IsFocused("card-a"));
	Focus.BeginFrame(true, true);
	Focus.Observe("card-a", true);
	Focus.EndFrame();
	EXPECT_TRUE(Focus.IsFocused("card-a"));
}

TEST(SettingsCardFocus, EmptyAndNullIdsCannotAcquireSelection)
{
	qm_card_registry::CCardFocus Focus;
	Focus.BeginFrame(true, true);
	Focus.Observe(nullptr, true);
	Focus.Observe("", true);
	Focus.EndFrame();
	EXPECT_FALSE(Focus.IsFocused(nullptr));
	EXPECT_FALSE(Focus.IsFocused(""));
}
