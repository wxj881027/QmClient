#include <game/client/QmUi/cards/QmTClientDeclaredRegions.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>
#include <stdexcept>

namespace
{
	using qm_tclient_cards::CDeclaredRegionsScope;
	using qm_tclient_cards::SDeclaredRegions;
}

TEST(TClientDeclaredRegionsScope, CompletedCallReleasesItsBorrowedRegionOwner)
{
	SDeclaredRegions Regions;
	ASSERT_EQ(CDeclaredRegionsScope::Current(), nullptr);
	{
		const CDeclaredRegionsScope Scope(Regions);
		EXPECT_EQ(CDeclaredRegionsScope::Current(), &Regions);
	}
	EXPECT_EQ(CDeclaredRegionsScope::Current(), nullptr);
}

TEST(TClientDeclaredRegionsScope, NestedCallsRestoreTheOuterOwnerAndKeepItsRegions)
{
	SDeclaredRegions Outer, Inner;
	Outer.m_Regions.try_emplace("outer");
	Inner.m_Regions.try_emplace("inner");
	{
		const CDeclaredRegionsScope OuterScope(Outer);
		{
			const CDeclaredRegionsScope InnerScope(Inner);
			EXPECT_EQ(CDeclaredRegionsScope::Current(), &Inner);
			EXPECT_EQ(CDeclaredRegionsScope::Current()->m_Regions.count("outer"), 0u);
		}
		EXPECT_EQ(CDeclaredRegionsScope::Current(), &Outer);
		EXPECT_EQ(CDeclaredRegionsScope::Current()->m_Regions.count("outer"), 1u);
	}
	EXPECT_EQ(CDeclaredRegionsScope::Current(), nullptr);
}

TEST(TClientDeclaredRegionsScope, ExpectedResourceExceptionsRestoreTheOuterCallAndPropagate)
{
	SDeclaredRegions Outer, Inner;
	const CDeclaredRegionsScope OuterScope(Outer);
	for(const bool LengthFailure : {false, true})
	{
		SCOPED_TRACE(LengthFailure);
		const auto Render = [&] {
			const CDeclaredRegionsScope InnerScope(Inner);
			if(LengthFailure)
				throw std::length_error("controlled declaration limit");
			throw std::bad_alloc();
		};
		if(LengthFailure)
			EXPECT_THROW(Render(), std::length_error);
		else
			EXPECT_THROW(Render(), std::bad_alloc);
		EXPECT_EQ(CDeclaredRegionsScope::Current(), &Outer);
	}
}

TEST(TClientDeclaredRegionsScope, UnknownRenderExceptionDoesNotLeaveTheFailedOwnerActive)
{
	SDeclaredRegions Regions;
	EXPECT_THROW(([&] {
		const CDeclaredRegionsScope Scope(Regions);
		throw std::logic_error("unrelated card render failure");
	}()),
		std::logic_error);
	EXPECT_EQ(CDeclaredRegionsScope::Current(), nullptr);
	const CDeclaredRegionsScope Retry(Regions);
	EXPECT_EQ(CDeclaredRegionsScope::Current(), &Regions);
}

TEST(TClientDeclaredRegionsScope, EarlyReturnAndRestorationDoNotAllocate)
{
	SDeclaredRegions Outer, Inner;
	const CDeclaredRegionsScope OuterScope(Outer);
	bool SelectedInner = false, RestoredOuter = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		SelectedInner = [&] {
			const CDeclaredRegionsScope InnerScope(Inner);
			return CDeclaredRegionsScope::Current() == &Inner;
		}();
		RestoredOuter = CDeclaredRegionsScope::Current() == &Outer;
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(SelectedInner);
	EXPECT_TRUE(RestoredOuter);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}
