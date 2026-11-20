#include <game/client/QmUi/SettingsBindCache.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <memory>
#include <string>

namespace
{
	constexpr char COMMAND[] = "say settings-command-with-a-long-identity";
	constexpr int CTRL = 1 << KeyModifier::CTRL;

	bool Prepare(CBindStorage &Binds, CSettingsBindCache &Cache, bool Warm, bool Many)
	{
		if(!Binds.Bind(1, COMMAND) || !Binds.Bind(2, "say other-settings-command-with-a-long-identity") ||
			!Binds.Bind(3, COMMAND, false, CTRL))
			return false;
		if(Many)
			for(int Key = 4; Key <= 83; ++Key)
				if(!Binds.Bind(Key, ("say settings-command-" + std::to_string(Key)).c_str()))
					return false;
		return !Warm || (Cache.Ensure(Binds) && Binds.Bind(1, "say changed-command-with-a-long-identity"));
	}

	void ExpectEveryFailureAndRetry(bool Warm, bool Many)
	{
		ASSERT_LT(83, KEY_LAST);
		size_t Allocations = 0;
		bool Ready = false;
		{
			auto pBinds = std::make_unique<CBindStorage>();
			CSettingsBindCache Cache;
			ASSERT_TRUE(Prepare(*pBinds, Cache, Warm, Many));
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Ready = Cache.Ensure(*pBinds);
			Allocations = Count.Attempts();
		}
		ASSERT_TRUE(Ready);
		ASSERT_GT(Allocations, 0u);
		for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
		{
			SCOPED_TRACE(BeforeFailure);
			auto pBinds = std::make_unique<CBindStorage>();
			CSettingsBindCache Cache;
			ASSERT_TRUE(Prepare(*pBinds, Cache, Warm, Many));
			const auto Revision = pBinds->Revision();
			bool Triggered = false;
			size_t Attempts = 0;
			CBindSlot First = EMPTY_BIND_SLOT, Last = EMPTY_BIND_SLOT, Missing = EMPTY_BIND_SLOT;
			{
				CPredictionAllocationFailure Failure(BeforeFailure);
				Ready = Cache.Ensure(*pBinds);
				First = Cache.Find(*pBinds, COMMAND);
				Last = Cache.Find(*pBinds, COMMAND, CSettingsBindCache::EFallback::LAST_MODIFIER_MATCH);
				Missing = Cache.Find(*pBinds, "say missing-settings-command-with-a-long-identity");
				Triggered = Failure.Triggered();
				Attempts = Failure.Attempts();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_FALSE(Ready);
			EXPECT_FALSE(Cache.Ready(*pBinds));
			EXPECT_EQ(Attempts, BeforeFailure + 1);
			EXPECT_EQ(First, Warm ? CBindSlot(3, CTRL) : CBindSlot(1, KeyModifier::NONE));
			EXPECT_EQ(Last, CBindSlot(3, CTRL));
			EXPECT_EQ(Missing, EMPTY_BIND_SLOT);
			EXPECT_EQ(pBinds->Revision(), Revision);
			EXPECT_STREQ(pBinds->Get(1, KeyModifier::NONE), Warm ? "say changed-command-with-a-long-identity" : COMMAND);
			ASSERT_TRUE(Cache.Ensure(*pBinds));
			EXPECT_TRUE(Cache.Ready(*pBinds));
			EXPECT_EQ(Cache.Find(*pBinds, COMMAND), First);
			if(Many)
				EXPECT_EQ(Cache.Find(*pBinds, "say settings-command-83"), CBindSlot(83, KeyModifier::NONE));
		}
	}
}

TEST(SettingsBindCacheAllocation, ColdFailuresUseTheActualBindingsAndAllowRetry)
{
	ExpectEveryFailureAndRetry(false, false);
}

TEST(SettingsBindCacheAllocation, StaleSnapshotFailuresDoNotDisplayTheRemovedBinding)
{
	ExpectEveryFailureAndRetry(true, false);
}

TEST(SettingsBindCacheAllocation, LargeCandidateFailuresFallBackWithoutRecoveryAllocation)
{
	ExpectEveryFailureAndRetry(false, true);
}

TEST(SettingsBindCacheAllocation, ReadyEnsureAndLongCommandQueriesDoNotAllocate)
{
	auto pBinds = std::make_unique<CBindStorage>();
	CSettingsBindCache Cache;
	ASSERT_TRUE(Prepare(*pBinds, Cache, false, false));
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	bool Ready = false, Triggered = false;
	size_t Attempts = 0;
	CBindSlot Found = EMPTY_BIND_SLOT, Missing = EMPTY_BIND_SLOT;
	{
		CPredictionAllocationFailure Failure;
		Ready = Cache.Ensure(*pBinds);
		Found = Cache.Find(*pBinds, COMMAND);
		Missing = Cache.Find(*pBinds, "say missing-settings-command-with-a-long-identity");
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Ready);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Found, CBindSlot(1, KeyModifier::NONE));
	EXPECT_EQ(Missing, EMPTY_BIND_SLOT);
}
