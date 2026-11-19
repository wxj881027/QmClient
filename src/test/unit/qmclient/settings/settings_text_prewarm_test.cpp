#include <game/client/QmUi/SettingsTextPrewarm.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <utility>
#include <vector>

TEST(SettingsTextPrewarm, FailedCandidatePreparationKeepsPreviousProgressAndRetriesTheSameText)
{
	CSettingsTextPrewarm Prewarm;
	ASSERT_TRUE(Prewarm.Prepare("ab", 16));
	const std::string Replacement(100, 'x');
	bool Prepared;
	bool Triggered;
	{
		CPredictionAllocationFailure Failure;
		Prepared = Prewarm.Prepare(Replacement, 24);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Prepared);
	SSettingsTextPrewarmBudget Budget{24, 100};
	std::vector<std::pair<int, int>> vGlyphs;
	const auto Now = [] { return int64_t{0}; };
	const auto Record = [&](int Chr, int Size) { vGlyphs.emplace_back(Chr, Size); };
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<std::pair<int, int>>{{'a', 16}, {'b', 16}}));
	ASSERT_TRUE(Prewarm.Prepare(Replacement, 24));
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs.back(), std::make_pair(int('x'), 24));
}

TEST(SettingsTextPrewarm, FailedGlyphCallKeepsTheCursorAndSharesTheRemainingBudget)
{
	CSettingsTextPrewarm Prewarm;
	ASSERT_TRUE(Prewarm.Prepare("ab", 16));
	SSettingsTextPrewarmBudget Budget{2, 100};
	const auto Now = [] { return int64_t{0}; };
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, [](int, int) { throw std::bad_alloc(); }));
	EXPECT_EQ(Budget.m_RemainingGlyphs, 1);
	std::vector<int> vGlyphs;
	const auto Record = [&](int Chr, int) { vGlyphs.push_back(Chr); };
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs, std::vector<int>{'a'});
	SSettingsTextPrewarmBudget Next{1, 100};
	EXPECT_TRUE(Prewarm.Advance(Next, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{'a', 'b'}));
}

TEST(SettingsTextPrewarm, ConsecutiveTextsShareTheGlyphBudget)
{
	CSettingsTextPrewarm Prewarm;
	SSettingsTextPrewarmBudget Budget{3, 100};
	std::vector<int> vGlyphs;
	const auto Now = [] { return int64_t{0}; };
	const auto Record = [&](int Chr, int) { vGlyphs.push_back(Chr); };
	Prewarm.Prepare("ab", 16);
	EXPECT_TRUE(Prewarm.Advance(Budget, Now, Record));
	Prewarm.Prepare("cd", 16);
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{'a', 'b', 'c'}));
	SSettingsTextPrewarmBudget NextBudget{3, 100};
	EXPECT_TRUE(Prewarm.Advance(NextBudget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{'a', 'b', 'c', 'd'}));
}

TEST(SettingsTextPrewarm, ExpensiveGlyphStopsFurtherWorkAndResumesNextStep)
{
	CSettingsTextPrewarm Prewarm;
	Prewarm.Prepare("ab", 16);
	SSettingsTextPrewarmBudget Budget{24, 10};
	int64_t Time = 0;
	std::vector<int> vGlyphs;
	const auto Now = [&] { return Time; };
	const auto Record = [&](int Chr, int) {
		vGlyphs.push_back(Chr);
		Time += 10;
	};
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{'a'}));
	SSettingsTextPrewarmBudget NextBudget{24, 30};
	EXPECT_TRUE(Prewarm.Advance(NextBudget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{'a', 'b'}));
}

TEST(SettingsTextPrewarm, LastGlyphOverBudgetDefersContainerReadiness)
{
	CSettingsTextPrewarm Prewarm;
	Prewarm.Prepare("a", 16);
	SSettingsTextPrewarmBudget Budget{24, 10};
	int64_t Time = 0;
	int Calls = 0;
	const auto Now = [&] { return Time; };
	const auto Record = [&](int, int) { ++Calls; Time = 10; };
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, Record));
	SSettingsTextPrewarmBudget NextBudget{24, 20};
	EXPECT_TRUE(Prewarm.Advance(NextBudget, Now, Record));
	EXPECT_EQ(Calls, 1);
}

TEST(SettingsTextPrewarm, RepeatedPreparationKeepsProgressAndDeduplicatesUtf8)
{
	CSettingsTextPrewarm Prewarm;
	Prewarm.Prepare("中文中", 24);
	SSettingsTextPrewarmBudget Budget{1, 100};
	std::vector<int> vGlyphs;
	const auto Now = [] { return int64_t{0}; };
	const auto Record = [&](int Chr, int) { vGlyphs.push_back(Chr); };
	EXPECT_FALSE(Prewarm.Advance(Budget, Now, Record));
	Prewarm.Prepare("中文中", 24);
	SSettingsTextPrewarmBudget NextBudget{1, 100};
	EXPECT_TRUE(Prewarm.Advance(NextBudget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<int>{0x4e2d, 0x6587}));
}

TEST(SettingsTextPrewarm, TextSizeAndResetInvalidatePreparedGlyphs)
{
	CSettingsTextPrewarm Prewarm;
	SSettingsTextPrewarmBudget Budget{24, 100};
	std::vector<std::pair<int, int>> vGlyphs;
	const auto Now = [] { return int64_t{0}; };
	const auto Record = [&](int Chr, int Size) { vGlyphs.emplace_back(Chr, Size); };
	Prewarm.Prepare("a", 16);
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	Prewarm.Prepare("b", 16);
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	Prewarm.Prepare("b", 24);
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	Prewarm.Reset();
	Prewarm.Prepare("b", 24);
	ASSERT_TRUE(Prewarm.Advance(Budget, Now, Record));
	EXPECT_EQ(vGlyphs, (std::vector<std::pair<int, int>>{{'a', 16}, {'b', 16}, {'b', 24}, {'b', 24}}));
}

TEST(SettingsTextPrewarm, ExhaustedTimeOrGlyphBudgetDoesNoWork)
{
	CSettingsTextPrewarm Prewarm;
	Prewarm.Prepare("a", 16);
	int Calls = 0;
	const auto Now = [] { return int64_t{10}; };
	const auto Record = [&](int, int) { ++Calls; };
	SSettingsTextPrewarmBudget NoTime{24, 10};
	EXPECT_FALSE(Prewarm.Advance(NoTime, Now, Record));
	SSettingsTextPrewarmBudget NoGlyphs{0, 20};
	EXPECT_FALSE(Prewarm.Advance(NoGlyphs, Now, Record));
	EXPECT_EQ(Calls, 0);
}
