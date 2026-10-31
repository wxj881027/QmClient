#include <game/client/QmUi/SettingsTextPlanCommit.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>

namespace
{
	struct SProgress
	{
		size_t m_Text = 7, m_Collection = 5;
		std::vector<std::pair<int, int>> m_vGlyphs{{65, 14}, {66, 14}};
		size_t m_Glyph = 1, m_GlyphPlan = 6;
		bool m_Dirty = false, m_Complete = true, m_VisibleReady = true;
		std::array<int, 3> m_Context{0, 3, 1};
		SSettingsTextPlanProgressView View()
		{
			return {m_Text, m_Collection, m_vGlyphs, m_Glyph, m_GlyphPlan, m_Dirty, m_Complete, m_VisibleReady, m_Context};
		}
	};
}

TEST(SettingsTextPlanProgress, VisibleReplacementRestartsConsumptionAndCollectionInsteadOfReusingOldCompletion)
{
	SProgress State;
	State.View().ReplaceVisiblePlan({0, 3, 1});
	EXPECT_EQ(State.m_Text, 0u);
	EXPECT_EQ(State.m_Collection, 0u);
	EXPECT_TRUE(State.m_vGlyphs.empty());
	EXPECT_EQ(State.m_Glyph, 0u);
	EXPECT_EQ(State.m_GlyphPlan, static_cast<size_t>(-1));
	EXPECT_TRUE(State.m_Dirty);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_TRUE(State.m_VisibleReady);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, State.m_Collection, State.m_Dirty), 5);
}

TEST(SettingsTextPlanProgress, BeginningPreparedCollectionClearsDirtyAndVisibleReadiness)
{
	SProgress State;
	State.View().ReplaceVisiblePlan({0, 3, 1});
	State.View().RestartCollection(false);
	EXPECT_FALSE(State.m_Dirty);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_FALSE(State.m_VisibleReady);
	EXPECT_EQ(State.m_Text, 0u);
	EXPECT_EQ(State.m_Collection, 0u);
	EXPECT_TRUE(State.m_vGlyphs.empty());
}

TEST(SettingsTextPlanProgress, ConsumptionRestartPreservesTheCollectionPrefixAndVisibleReadiness)
{
	SProgress State;
	State.m_Complete = false;
	State.View().RestartConsumption();
	EXPECT_EQ(State.m_Text, 0u);
	EXPECT_TRUE(State.m_vGlyphs.empty());
	EXPECT_EQ(State.m_Glyph, 0u);
	EXPECT_EQ(State.m_GlyphPlan, static_cast<size_t>(-1));
	EXPECT_EQ(State.m_Collection, 5u);
	EXPECT_FALSE(State.m_Dirty);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_TRUE(State.m_VisibleReady);
}

TEST(SettingsTextPlanProgress, InvalidationDiscardsOldGlyphWorkAndVisibleReadiness)
{
	SProgress State;
	State.View().RestartCollection(true);
	EXPECT_EQ(State.m_Text, 0u);
	EXPECT_EQ(State.m_Collection, 0u);
	EXPECT_TRUE(State.m_vGlyphs.empty());
	EXPECT_EQ(State.m_Glyph, 0u);
	EXPECT_EQ(State.m_GlyphPlan, static_cast<size_t>(-1));
	EXPECT_TRUE(State.m_Dirty);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_FALSE(State.m_VisibleReady);
}

TEST(SettingsTextPlanProgress, VisibleReplacementDoesNotAllocateDuringPublication)
{
	SProgress State;
	bool Triggered;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		State.View().ReplaceVisiblePlan({0, 3, 1});
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_TRUE(State.m_Dirty);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_TRUE(State.m_vGlyphs.empty());
}

TEST(SettingsTextPlanProgress, RepeatedVisibleReplacementKeepsARebuildPendingEvenForAnEmptyPage)
{
	SProgress State;
	State.View().ReplaceVisiblePlan({0, 3, 1});
	State.View().ReplaceVisiblePlan({0, 3, 1});
	EXPECT_TRUE(State.m_VisibleReady);
	EXPECT_FALSE(State.m_Complete);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(0, State.m_Collection, State.m_Dirty), 1);
}

TEST(SettingsTextPlanProgress, EmptyVisiblePlanReadinessIsScopedToPageTabAndScene)
{
	SProgress State;
	State.View().ReplaceVisiblePlan({0, 3, 1});
	EXPECT_TRUE(State.View().VisiblePlanMatches({0, 3, 1}));
	EXPECT_FALSE(State.View().VisiblePlanMatches({0, 4, 1}));
	EXPECT_FALSE(State.View().VisiblePlanMatches({0, 3, 2}));
	EXPECT_FALSE(State.View().VisiblePlanMatches({1, -1, -1}));
	State.View().ReplaceVisiblePlan({1, -1, -1});
	EXPECT_TRUE(State.View().VisiblePlanMatches({1, -1, -1}));
	EXPECT_FALSE(State.View().VisiblePlanMatches({0, 3, 1}));
	State.View().RestartCollection(false);
	EXPECT_FALSE(State.View().VisiblePlanMatches({1, -1, -1}));
}

TEST(SettingsTextPlanProgress, DirtyPlanCannotReportZeroWhenItsOldUnitsAreEmptyOrFinished)
{
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(0, 0, true), 1);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 5, true), 1);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 8, true), 1);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 2, true), 3);
}

TEST(SettingsTextPlanProgress, CleanPendingCountHandlesCompletedAndOvershotCursorsWithoutUnderflow)
{
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 2, false), 3);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 5, false), 0);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(5, 8, false), 0);
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(0, std::numeric_limits<size_t>::max(), false), 0);
}

TEST(SettingsTextPlanProgress, LargeRemainingCountSaturatesItsLegacyIntReport)
{
	const size_t IntMax = static_cast<size_t>(std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(IntMax, 0, false), std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(IntMax + 1, 0, false), std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(std::numeric_limits<size_t>::max(), 0, false), std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsTextPlanPendingUnitCount(IntMax + 1, IntMax, false), 1);
}
