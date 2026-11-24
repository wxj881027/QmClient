#include <base/str.h>

#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SettingsTextPlanUnits.h>
#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsLoadingPrewarm, AdvanceRequiresNoProgressToFinish)
{
	SSettingsLoadingPrewarmState State;
	State.m_LastBuiltTextContainers = 10;
	State.m_LastMissingTextPlanItems = 2;
	State.m_LastMissingTextPlanCollectionUnits = 1;

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 1);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 1, 1);
	EXPECT_EQ(State.m_CompletedSteps, 2);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 1);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 1);
	EXPECT_EQ(State.m_CompletedSteps, 3);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_EQ(State.m_LastMissingTextPlanItems, 0);
	EXPECT_FALSE(State.m_WarmupReady);

	SettingsLoadingPrewarmAdvance(State, 18, 0, 0);
	EXPECT_EQ(State.m_CompletedSteps, 4);
	EXPECT_EQ(State.m_ConsecutiveNoProgressSteps, 0);
	EXPECT_TRUE(State.m_WarmupReady);
}

TEST(SettingsLoadingPrewarm, ProgressDetectionIsStrictlyIncreasing)
{
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(3, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(5, 4));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 3, 2));
	EXPECT_TRUE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, -1, 2));
	EXPECT_FALSE(SettingsLoadingPrewarmMadeProgress(4, 4, 2, 2, 2, 2));
}

TEST(SettingsLoadingPrewarm, TextPrewarmTokenBudgetUsesRequestedBudgetWhileBlocking)
{
	// loading 有显式 8ms 墙钟切片，必须按请求预算建条目，不能被运行时 1 token 钳住。
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(true, 32, 1), 32);
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(true, 8, 1), 8);
	// 运行时仍由自适应窗口决定，不受请求值影响。
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(false, 32, 1), 1);
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(false, 64, 32), 32);
	// 0 不能被解释成强制 1 条。
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(true, 0, 5), 0);
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(false, 8, 0), 0);
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(true, -4, 5), 0);
	EXPECT_EQ(SettingsTextPrewarmTokenBudget(false, 8, -4), 0);
}

TEST(SettingsLoadingPrewarm, IngameEscPlanCannotCollectHiddenSettingsPages)
{
	const int Pages[] = {1, 2, 3};
	for(bool PreferQm : {false, true})
	{
		auto Units = QmSettingsTextPlanUnits(true, 3, 2, PreferQm, 10, 20, 1, 2, 5, 6, 3, 30, 2, Pages);
		ASSERT_EQ(Units.size(), 1u);
		EXPECT_EQ(Units[0].m_Kind, MENU_TEXT_PLAN_UNIT_INGAME_ESC);
	}
}
TEST(SettingsLoadingPrewarm, SettingsPlanStillCollectsVisibleAndConfiguredTabs)
{
	const int Pages[] = {1, 2, 3};
	auto Units = QmSettingsTextPlanUnits(false, 1, -1, true, 10, 20, 1, 2, 5, 6, 3, 30, 2, Pages);
	ASSERT_EQ(Units.size(), 16u);
	EXPECT_EQ(Units.front().m_Kind, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS);
	EXPECT_EQ(Units.front().m_Page, 1);
	EXPECT_EQ(Units[1].m_Page, 20);
	EXPECT_EQ(Units[1].m_Tab, 0);
	EXPECT_EQ(Units.back().m_Kind, MENU_TEXT_PLAN_UNIT_INGAME_ESC);
	EXPECT_EQ(Units[Units.size() - 2].m_Page, 3);
	int ContributorsTabs = 0;
	int IngameEscUnits = 0;
	for(const auto &Unit : Units)
	{
		if(Unit.m_Kind == MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB)
			EXPECT_NE(Unit.m_Tab, 3);
		if(Unit.m_Kind == MENU_TEXT_PLAN_UNIT_CONTRIBUTORS_TAB)
		{
			EXPECT_EQ(Unit.m_Page, 30);
			++ContributorsTabs;
		}
		if(Unit.m_Kind == MENU_TEXT_PLAN_UNIT_INGAME_ESC)
			++IngameEscUnits;
	}
	EXPECT_EQ(ContributorsTabs, 2);
	EXPECT_EQ(IngameEscUnits, 1);
}

TEST(SettingsLoadingPrewarm, ContributorSiblingTabsFollowTheVisibleTabWithoutDuplicates)
{
	const int Pages[] = {1, 2, 3};
	const auto Units = QmSettingsTextPlanUnits(false, 30, 1, true, 10, 20, 1, 2, 5, 6, 3, 30, 2, Pages);
	ASSERT_FALSE(Units.empty());
	EXPECT_EQ(Units.front().m_Kind, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS);
	EXPECT_EQ(Units.front().m_Page, 30);
	EXPECT_EQ(Units.front().m_Tab, 1);
	ASSERT_GE(Units.size(), 2u);
	EXPECT_EQ(Units[1].m_Kind, MENU_TEXT_PLAN_UNIT_CONTRIBUTORS_TAB);
	EXPECT_EQ(Units[1].m_Page, 30);
	EXPECT_EQ(Units[1].m_Tab, 0);

	int ContributorTabs[2] = {};
	int VisibleTabEntries = 0;
	for(const auto &Unit : Units)
	{
		if(Unit.m_Kind == MENU_TEXT_PLAN_UNIT_CONTRIBUTORS_TAB && Unit.m_Page == 30 && Unit.m_Tab >= 0 && Unit.m_Tab < 2)
			++ContributorTabs[Unit.m_Tab];
		if(Unit.m_Page == 30 && Unit.m_Tab == 1)
			++VisibleTabEntries;
	}
	EXPECT_EQ(ContributorTabs[0], 1);
	EXPECT_EQ(ContributorTabs[1], 0);
	EXPECT_EQ(VisibleTabEntries, 1);
}

TEST(SettingsLoadingPrewarm, BasePageSubtabsArePlannedOnceAndVisibleTabLeads)
{
	const int Pages[] = {1, 2, 3, 40, 41, 42};
	const SSettingsMenuTextPlanSubtabGroup Groups[] = {{40, 2}, {41, 6}, {42, 2}};
	const auto Units = QmSettingsTextPlanUnits(false, 40, 1, false, 10, 20, 0, 0, 1, 1, -1, 30, 1, Pages, Groups);
	ASSERT_GE(Units.size(), 3u);
	EXPECT_EQ(Units[0].m_Kind, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS);
	EXPECT_EQ(Units[0].m_Page, 40);
	EXPECT_EQ(Units[0].m_Tab, 1);
	EXPECT_EQ(Units[1].m_Kind, MENU_TEXT_PLAN_UNIT_BASE_PAGE_TAB);
	EXPECT_EQ(Units[1].m_Page, 40);
	EXPECT_EQ(Units[1].m_Tab, 0);
	std::array<int, 2> aGraphicsTabs{};
	std::array<int, 6> aAppearanceTabs{};
	std::array<int, 2> aDdnetTabs{};
	for(const auto &Unit : Units)
	{
		if(Unit.m_Kind != MENU_TEXT_PLAN_UNIT_BASE_PAGE_TAB)
			continue;
		if(Unit.m_Page == 40 && Unit.m_Tab >= 0 && Unit.m_Tab < (int)aGraphicsTabs.size())
			++aGraphicsTabs[Unit.m_Tab];
		if(Unit.m_Page == 41 && Unit.m_Tab >= 0 && Unit.m_Tab < (int)aAppearanceTabs.size())
			++aAppearanceTabs[Unit.m_Tab];
		if(Unit.m_Page == 42 && Unit.m_Tab >= 0 && Unit.m_Tab < (int)aDdnetTabs.size())
			++aDdnetTabs[Unit.m_Tab];
	}
	EXPECT_EQ(aGraphicsTabs, (std::array<int, 2>{1, 0}));
	EXPECT_EQ(aAppearanceTabs, (std::array<int, 6>{1, 1, 1, 1, 1, 1}));
	EXPECT_EQ(aDdnetTabs, (std::array<int, 2>{1, 1}));
}

TEST(SettingsLoadingPrewarm, ControlPageCollectsEveryTabAndSkipsVisibleDuplicate)
{
	const int Pages[] = {50};
	const SSettingsMenuTextPlanSubtabGroup Groups[] = {{50, 4}};
	const auto Units = QmSettingsTextPlanUnits(false, 50, 2, false, 10, 20, 0, 0, 1, 1, -1, 30, 1, Pages, Groups);
	std::array<int, 4> aControlTabs{};
	for(const auto &Unit : Units)
		if(Unit.m_Kind == MENU_TEXT_PLAN_UNIT_BASE_PAGE_TAB && Unit.m_Page == 50 && Unit.m_Tab >= 0 && Unit.m_Tab < (int)aControlTabs.size())
			++aControlTabs[Unit.m_Tab];
	EXPECT_EQ(aControlTabs, (std::array<int, 4>{1, 1, 0, 1}));
}

TEST(SettingsLoadingPrewarm, CurrentBasePageIsCollectedOnlyOnce)
{
	const int Pages[] = {1, 2, 3};
	const auto Units = QmSettingsTextPlanUnits(false, 3, -1, true, 10, 20, 1, 2, 5, 6, 3, 30, 2, Pages);
	int CurrentPageEntries = 0;
	for(const auto &Unit : Units)
		if(Unit.m_Page == 3 && Unit.m_Tab == -1)
			++CurrentPageEntries;
	EXPECT_EQ(CurrentPageEntries, 1);
	ASSERT_GE(Units.size(), 2u);
	EXPECT_EQ(Units[Units.size() - 2].m_Page, 2);
	EXPECT_EQ(Units.back().m_Kind, MENU_TEXT_PLAN_UNIT_INGAME_ESC);
}

TEST(SettingsLoadingPrewarm, TeePagePlansAllOrthogonalCombinations)
{
	const int Pages[] = {5, 6};
	const auto Units = QmSettingsTextPlanUnits(false, 6, -1, false, 10, 20, 0, 0, 1, 1, -1, 30, 1, Pages, {}, 5, 2, 2);
	int TeeCombinations = 0;
	for(const auto &Unit : Units)
	{
		if(Unit.m_Kind != MENU_TEXT_PLAN_UNIT_TEE_COMBINATION)
			continue;
		EXPECT_EQ(Unit.m_Page, 5);
		EXPECT_GE(Unit.m_Tab, 0);
		EXPECT_LT(Unit.m_Tab, 2);
		EXPECT_GE(Unit.m_Subtab, 0);
		EXPECT_LT(Unit.m_Subtab, 2);
		++TeeCombinations;
	}
	EXPECT_EQ(TeeCombinations, 4);
}

TEST(SettingsLoadingPrewarm, VisibleTeeCombinationIsNotCollectedTwice)
{
	const int Pages[] = {5};
	const auto Units = QmSettingsTextPlanUnits(false, 5, 3, false, 10, 20, 0, 0, 1, 1, -1, 30, 1, Pages, {}, 5, 2, 2);
	ASSERT_FALSE(Units.empty());
	EXPECT_EQ(Units.front().m_Kind, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS);
	EXPECT_EQ(Units.front().m_Tab, 3);
	int TeeCombinations = 0;
	for(const auto &Unit : Units)
	{
		if(Unit.m_Kind != MENU_TEXT_PLAN_UNIT_TEE_COMBINATION)
			continue;
		EXPECT_FALSE(Unit.m_Tab == 1 && Unit.m_Subtab == 1);
		++TeeCombinations;
	}
	EXPECT_EQ(TeeCombinations, 3);
}

TEST(SettingsLoadingPrewarm, LayoutSignatureChangesWithScreenScaleAndDpi)
{
	const uint64_t Baseline = QmSettingsTextPlanLayoutSignature(3840, 2160, 100, 1000);
	EXPECT_EQ(QmSettingsTextPlanLayoutSignature(3840, 2160, 100, 1000), Baseline);
	EXPECT_NE(QmSettingsTextPlanLayoutSignature(1920, 2160, 100, 1000), Baseline);
	EXPECT_NE(QmSettingsTextPlanLayoutSignature(3840, 1080, 100, 1000), Baseline);
	EXPECT_NE(QmSettingsTextPlanLayoutSignature(3840, 2160, 125, 1000), Baseline);
	EXPECT_NE(QmSettingsTextPlanLayoutSignature(3840, 2160, 100, 1500), Baseline);
}

TEST(SettingsLoadingPrewarm, AppendedIngameEscUnitDoesNotOverrideSettingsVisibleContext)
{
	EXPECT_TRUE(QmSettingsTextPlanUnitProvidesVisibleContext(MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS));
	EXPECT_FALSE(QmSettingsTextPlanUnitProvidesVisibleContext(MENU_TEXT_PLAN_UNIT_INGAME_ESC, MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS));
	EXPECT_TRUE(QmSettingsTextPlanUnitProvidesVisibleContext(MENU_TEXT_PLAN_UNIT_INGAME_ESC, MENU_TEXT_PLAN_UNIT_INGAME_ESC));
}

TEST(SettingsLoadingPrewarm, VisiblePlanMatcherRequiresMatchingTab)
{
	EXPECT_FALSE(QmSettingsTextPlanItemMatchesVisible(30, -1, 30, 1));
	EXPECT_TRUE(QmSettingsTextPlanItemMatchesVisible(30, 1, 30, 1));
	EXPECT_FALSE(QmSettingsTextPlanItemMatchesVisible(30, 1, 30, -1));
	EXPECT_TRUE(QmSettingsTextPlanItemMatchesVisible(30, -1, 30, -1));
	EXPECT_FALSE(QmSettingsTextPlanItemMatchesVisible(30, 0, 30, 1));
	EXPECT_FALSE(QmSettingsTextPlanItemMatchesVisible(-1, -1, 30, 1));
	EXPECT_FALSE(QmSettingsTextPlanItemMatchesVisible(1, -1, 3, -1));
}

TEST(SettingsLoadingPrewarm, GlyphDemandTableCountsFrequencyAndPrioritizesVisibleFirst)
{
	const char *pMixed = "A\xE4\xB8\xAD"
			     "A";
	const SSettingsTextPlanGlyphDemandInput aInputs[] = {
		{pMixed, 16, 2},
		{"A", 16, 1},
		{"A", 24, 2},
		{"B", 32, 0},
		{"C", 32, 0},
	};
	CSettingsGlyphDemandTable Table;
	ASSERT_TRUE(Table.Build(aInputs, 64));
	EXPECT_EQ(Table.TotalGlyphDemands(), 7u);
	EXPECT_EQ(Table.DistinctGlyphs(), 5u);
	const auto &Entries = Table.Entries();
	ASSERT_EQ(Entries.size(), 5u);
	EXPECT_EQ(Entries[0].m_Codepoint, 'B');
	EXPECT_EQ(Entries[1].m_Codepoint, 'C');
	EXPECT_EQ(Entries[2].m_Codepoint, 'A');
	EXPECT_EQ(Entries[2].m_Count, 3u);
	EXPECT_EQ(Entries[2].m_Priority, 1);
	EXPECT_EQ(Entries[3].m_Codepoint, 'A');
	EXPECT_EQ(Entries[3].m_PixelSize, 24);
	EXPECT_EQ(Entries[4].m_Codepoint, 0x4e2d);
	EXPECT_EQ(Table.PrewarmBatches().size(), 4u);
	EXPECT_EQ(Table.PrewarmBatches()[0].m_GlyphCount, 2u);
	EXPECT_EQ(Table.PrewarmBatches()[0].m_PixelSize, 32);
}

TEST(SettingsLoadingPrewarm, GlyphDemandBatchesKeepPixelSizesTogether)
{
	const SSettingsTextPlanGlyphDemandInput aInputs[] = {
		{"AAAAA", 16, 2},
		{"BBBB", 24, 2},
		{"CCC", 16, 2},
		{"DD", 24, 2},
	};
	CSettingsGlyphDemandTable Table;
	ASSERT_TRUE(Table.Build(aInputs, 64));
	ASSERT_EQ(Table.PrewarmBatches().size(), 2u);
	EXPECT_EQ(Table.PrewarmBatches()[0].m_PixelSize, 16);
	EXPECT_EQ(Table.PrewarmBatches()[0].m_GlyphCount, 2u);
	EXPECT_EQ(Table.PrewarmBatches()[1].m_PixelSize, 24);
	EXPECT_EQ(Table.PrewarmBatches()[1].m_GlyphCount, 2u);
}

TEST(SettingsLoadingPrewarm, GlyphDemandTableSkipsInvalidInputsAndRespectsBatchBounds)
{
	const SSettingsTextPlanGlyphDemandInput aInvalid[] = {
		{"A", 0, 2},
		{nullptr, 16, 2},
		{"\x01", 16, 2},
		{"A", 16, 2},
	};
	CSettingsGlyphDemandTable Table;
	ASSERT_TRUE(Table.Build(aInvalid, 64));
	EXPECT_EQ(Table.TotalGlyphDemands(), 1u);
	EXPECT_EQ(Table.DistinctGlyphs(), 1u);

	std::string Text;
	for(int Index = 0; Index < 70; ++Index)
	{
		char aUtf8[5] = {};
		const int Length = str_utf8_encode(aUtf8, 0x4e00 + Index);
		ASSERT_GT(Length, 0);
		Text.append(aUtf8, Length);
	}
	const SSettingsTextPlanGlyphDemandInput Input{Text.c_str(), 18, 2};
	ASSERT_TRUE(Table.Build(std::span<const SSettingsTextPlanGlyphDemandInput>(&Input, 1), 64));
	ASSERT_EQ(Table.PrewarmBatches().size(), 2u);
	EXPECT_EQ(Table.PrewarmBatches()[0].m_GlyphCount, 64u);
	EXPECT_EQ(Table.PrewarmBatches()[1].m_GlyphCount, 6u);
	ASSERT_TRUE(Table.BuildPrewarmBatches(16));
	for(const auto &Batch : Table.PrewarmBatches())
		EXPECT_LE(Batch.m_GlyphCount, 16u);
}

TEST(SettingsLoadingPrewarm, PlanCollectionStepBudgetIsCappedWithoutRejectingZero)
{
	EXPECT_EQ(QmMenuTextPlanCollectionUnitsForStep(-1), 0);
	EXPECT_EQ(QmMenuTextPlanCollectionUnitsForStep(0), 0);
	EXPECT_EQ(QmMenuTextPlanCollectionUnitsForStep(2), 2);
	EXPECT_EQ(QmMenuTextPlanCollectionUnitsForStep(8), QM_MENU_TEXT_PLAN_UNITS_PER_STEP);
}
