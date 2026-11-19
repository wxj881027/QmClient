#include <game/client/QmUi/SettingsContentLayout.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using TRows = std::vector<SSettingsContentRowDeclaration>;
	using FMeasureText = CSettingsContentLayoutCache::FMeasureText;

	TRows OldRows()
	{
		return {{"first", {}, 20.0f, 0.0f, 0.0f, 4.0f}, {"second", "old", 10.0f, 12.0f, 16.0f, 0.0f, true}};
	}

	TRows NewRows()
	{
		auto Rows = OldRows();
		Rows[0].m_MinimumHeight = 60.0f;
		Rows[0].m_Indent = 24.0f;
		Rows[0].m_Spacing = 8.0f;
		Rows[1].m_Text.assign(512, 'n');
		Rows[1].m_MinimumHeight = 70.0f;
		Rows[1].m_Indent = 32.0f;
		return Rows;
	}

	const FMeasureText FIXED_MEASURE = [](const char *, float, float) { return 30.0f; };

	struct SAttempt
	{
		std::shared_ptr<const CSettingsContentLayout> m_pLayout;
		bool m_Triggered = false;
		bool m_BadAllocation = false;
		size_t m_Allocations = 0;
	};

	SAttempt ResolveWithFailure(CSettingsContentLayoutCache &Cache, const TRows &Rows, float Width, uint64_t FontRevision, const FMeasureText &Measure, size_t BeforeFailure)
	{
		SAttempt Attempt;
		CPredictionAllocationFailure Failure(BeforeFailure);
		try
		{
			Attempt.m_pLayout = Cache.Resolve(Rows, Width, FontRevision, Measure);
		}
		catch(const std::bad_alloc &)
		{
			Attempt.m_BadAllocation = true;
		}
		Attempt.m_Triggered = Failure.Triggered();
		Attempt.m_Allocations = Failure.Attempts();
		return Attempt;
	}

	void ExpectNewGeometry(const CSettingsContentLayout &Layout, const TRows &Rows, float Width)
	{
		ASSERT_EQ(Layout.Rows().size(), Rows.size());
		for(size_t Index = 0; Index < Rows.size(); ++Index)
			EXPECT_TRUE(Layout.Rows()[Index].m_Declaration == Rows[Index]);
		EXPECT_FLOAT_EQ(Layout.Width(), Width);
		EXPECT_FLOAT_EQ(Layout.Rows()[0].m_Rect.h, 60.0f);
		EXPECT_FLOAT_EQ(Layout.Rows()[1].m_Rect.y, 68.0f);
		EXPECT_FLOAT_EQ(Layout.Rows()[1].m_Rect.h, 70.0f);
		EXPECT_FLOAT_EQ(Layout.Height(), 138.0f);
	}
}

TEST(SettingsContentLayoutAllocation, EveryReplacementAllocationFailurePreservesTheOldKeyAndCannotCacheAPartialDeclarationPrefix)
{
	const auto PreviousRows = OldRows();
	const auto Replacement = NewRows();
	auto Partial = PreviousRows;
	Partial.front() = Replacement.front();
	CSettingsContentLayoutCache TraceCache;
	(void)TraceCache.Resolve(PreviousRows, 240.0f, 1, FIXED_MEASURE);
	const auto Trace = ResolveWithFailure(TraceCache, Replacement, 240.0f, 1, FIXED_MEASURE, std::numeric_limits<size_t>::max());
	ASSERT_NE(Trace.m_pLayout, nullptr);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_FALSE(Trace.m_BadAllocation);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 4096u);
	for(size_t Index = 0; Index < Trace.m_Allocations; ++Index)
	{
		SCOPED_TRACE(Index);
		CSettingsContentLayoutCache Cache;
		const auto pPrevious = Cache.Resolve(PreviousRows, 240.0f, 1, FIXED_MEASURE);
		const auto Attempt = ResolveWithFailure(Cache, Replacement, 240.0f, 1, FIXED_MEASURE, Index);
		EXPECT_TRUE(Attempt.m_Triggered);
		EXPECT_TRUE(Attempt.m_BadAllocation);
		EXPECT_EQ(Attempt.m_pLayout, nullptr);
		bool MeasuredPartial = false;
		const FMeasureText RejectPartial = [&](const char *, float, float) -> float {
			MeasuredPartial = true;
			throw std::logic_error("partial key must need its own geometry");
		};
		// 该输入是新第一行加旧第二行；命中旧几何会绕过真实测量异常。
		EXPECT_THROW(Cache.Resolve(Partial, 240.0f, 1, RejectPartial), std::logic_error);
		EXPECT_TRUE(MeasuredPartial);
		const auto Reused = ResolveWithFailure(Cache, PreviousRows, 240.0f, 1, FIXED_MEASURE, 0);
		EXPECT_FALSE(Reused.m_Triggered);
		EXPECT_FALSE(Reused.m_BadAllocation);
		EXPECT_EQ(Reused.m_Allocations, 0u);
		EXPECT_EQ(Reused.m_pLayout, pPrevious);
		EXPECT_TRUE(pPrevious->Rows()[0].m_Declaration == PreviousRows[0]);
		EXPECT_TRUE(pPrevious->Rows()[1].m_Declaration == PreviousRows[1]);
		EXPECT_FLOAT_EQ(pPrevious->Rows()[0].m_Rect.h, 20.0f);
		const auto pRetried = Cache.Resolve(Replacement, 240.0f, 1, FIXED_MEASURE);
		ASSERT_NE(pRetried, nullptr);
		EXPECT_NE(pRetried, pPrevious);
		ExpectNewGeometry(*pRetried, Replacement, 240.0f);
	}
}

TEST(SettingsContentLayoutAllocation, EqualOwnedDeclarationsReuseThePublishedLayoutWithoutAllocationOrMeasurement)
{
	CSettingsContentLayoutCache Cache;
	const auto Rows = NewRows();
	const auto pLayout = Cache.Resolve(Rows, 240.0f, 3, FIXED_MEASURE);
	const auto EqualRows = Rows;
	bool Measured = false;
	const FMeasureText RejectMeasure = [&](const char *, float, float) -> float {
		Measured = true;
		throw std::logic_error("cache hit must not measure");
	};
	const auto Attempt = ResolveWithFailure(Cache, EqualRows, 240.0f, 3, RejectMeasure, 0);
	EXPECT_EQ(Attempt.m_pLayout, pLayout);
	EXPECT_FALSE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_BadAllocation);
	EXPECT_EQ(Attempt.m_Allocations, 0u);
	EXPECT_FALSE(Measured);
}

TEST(SettingsContentLayoutAllocation, MeasurementExceptionsKeepTheOldFontWidthAndDeclarationKeyForExplicitRetry)
{
	const auto PreviousRows = OldRows();
	const auto Replacement = NewRows();
	for(int Failure = 0; Failure < 3; ++Failure)
	{
		SCOPED_TRACE(Failure);
		CSettingsContentLayoutCache Cache;
		const auto pPrevious = Cache.Resolve(PreviousRows, 240.0f, 1, FIXED_MEASURE);
		bool Measured = false;
		const FMeasureText RejectMeasure = [&](const char *, float, float) -> float {
			Measured = true;
			if(Failure == 0)
				throw std::bad_alloc();
			if(Failure == 1)
				throw std::length_error("controlled measurement limit");
			throw std::logic_error("unrelated measurement failure");
		};
		if(Failure == 0)
			EXPECT_THROW(Cache.Resolve(Replacement, 120.0f, 2, RejectMeasure), std::bad_alloc);
		else if(Failure == 1)
			EXPECT_THROW(Cache.Resolve(Replacement, 120.0f, 2, RejectMeasure), std::length_error);
		else
			EXPECT_THROW(Cache.Resolve(Replacement, 120.0f, 2, RejectMeasure), std::logic_error);
		EXPECT_TRUE(Measured);
		const auto Reused = ResolveWithFailure(Cache, PreviousRows, 240.0f, 1, FIXED_MEASURE, 0);
		EXPECT_EQ(Reused.m_pLayout, pPrevious);
		EXPECT_FALSE(Reused.m_Triggered);
		EXPECT_FALSE(Reused.m_BadAllocation);
		const auto pRetried = Cache.Resolve(Replacement, 120.0f, 2, FIXED_MEASURE);
		ASSERT_NE(pRetried, nullptr);
		EXPECT_NE(pRetried, pPrevious);
		ExpectNewGeometry(*pRetried, Replacement, 120.0f);
	}
}

TEST(SettingsContentLayoutAllocation, MeasurementUsesTheOwnedSnapshotWhenTheCallerChangesItsDeclarations)
{
	CSettingsContentLayoutCache Cache;
	auto Rows = OldRows();
	const auto BeforeMeasurement = Rows;
	bool ChangedCaller = false;
	const FMeasureText ChangeCaller = [&](const char *, float, float) {
		ChangedCaller = true;
		Rows.front().m_MinimumHeight = 80.0f;
		Rows.back().m_Text.assign(512, 'r');
		return 30.0f;
	};
	const auto pLayout = Cache.Resolve(Rows, 240.0f, 1, ChangeCaller);
	ASSERT_TRUE(ChangedCaller);
	ASSERT_NE(pLayout, nullptr);
	ASSERT_EQ(pLayout->Rows().size(), BeforeMeasurement.size());
	for(size_t Index = 0; Index < BeforeMeasurement.size(); ++Index)
		EXPECT_TRUE(pLayout->Rows()[Index].m_Declaration == BeforeMeasurement[Index]);
	EXPECT_FLOAT_EQ(pLayout->Rows().front().m_Rect.h, 20.0f);
	EXPECT_FLOAT_EQ(pLayout->Rows().back().m_Rect.y, 24.0f);
	const auto Reused = ResolveWithFailure(Cache, BeforeMeasurement, 240.0f, 1, FIXED_MEASURE, 0);
	EXPECT_EQ(Reused.m_pLayout, pLayout);
	EXPECT_FALSE(Reused.m_Triggered);
	EXPECT_FALSE(Reused.m_BadAllocation);
	const auto pChanged = Cache.Resolve(Rows, 240.0f, 1, FIXED_MEASURE);
	EXPECT_NE(pChanged, pLayout);
	EXPECT_FLOAT_EQ(pChanged->Rows().front().m_Rect.h, 80.0f);
	EXPECT_EQ(pChanged->Rows().back().m_Declaration.m_Text, Rows.back().m_Text);
}
