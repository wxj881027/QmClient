#include <game/client/components/binds.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <memory>
#include <optional>

namespace
{
	constexpr CBindSlot OLD(KEY_X, KeyModifier::NONE);
	constexpr CBindSlot TARGET(KEY_Y, 1 << KeyModifier::CTRL);
	constexpr char COMMAND[] = "say original-binding-with-a-long-owned-command";

	bool Prepare(CBindStorage &Binds)
	{
		return Binds.Bind(OLD.m_Key, COMMAND, false, OLD.m_ModifierMask) &&
		       Binds.Bind(TARGET.m_Key, "+hook", false, TARGET.m_ModifierMask);
	}
}

TEST(BindRebindAllocation, EveryCppPreparationFailurePreservesBothSlotsAndAllowsRetry)
{
	size_t Allocations = 0;
	bool Accepted = false;
	{
		auto pBinds = std::make_unique<CBindStorage>();
		ASSERT_TRUE(Prepare(*pBinds));
		CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
		Accepted = pBinds->Rebind(OLD, TARGET, pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), [](const CBindSlot &) noexcept {});
		Allocations = Count.Attempts();
	}
	ASSERT_TRUE(Accepted);
	ASSERT_GT(Allocations, 0u);
	for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		auto pBinds = std::make_unique<CBindStorage>();
		ASSERT_TRUE(Prepare(*pBinds));
		const char *pOld = pBinds->Get(OLD.m_Key, OLD.m_ModifierMask);
		const char *pTarget = pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask);
		const uint64_t Revision = pBinds->Revision();
		int Notifications = 0;
		bool Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			Accepted = pBinds->Rebind(OLD, TARGET, pOld, [&](const CBindSlot &) noexcept { ++Notifications; });
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Accepted);
		EXPECT_EQ(Attempts, BeforeFailure + 1);
		EXPECT_EQ(Notifications, 0);
		EXPECT_EQ(pBinds->Revision(), Revision);
		EXPECT_EQ(pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), pOld);
		EXPECT_EQ(pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), pTarget);
		EXPECT_STREQ(pOld, COMMAND);
		EXPECT_STREQ(pTarget, "+hook");
		ASSERT_TRUE(pBinds->Rebind(OLD, TARGET, pOld, [&](const CBindSlot &) noexcept { ++Notifications; }));
		EXPECT_EQ(Notifications, 2);
		EXPECT_EQ(pBinds->Revision(), Revision + 2);
		EXPECT_STREQ(pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
	}
}

TEST(BindRebindAllocation, TargetCommitNeedsNoAllocationAfterUnbindNotification)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(Prepare(*pBinds));
	std::optional<CPredictionAllocationFailure> Failure;
	int Notifications = 0;
	const bool Accepted = pBinds->Rebind(OLD, TARGET, pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), [&](const CBindSlot &) noexcept {
		if(++Notifications == 1)
			Failure.emplace();
	});
	const bool Triggered = Failure && Failure->Triggered();
	const size_t Attempts = Failure ? Failure->Attempts() : 0;
	Failure.reset();
	EXPECT_TRUE(Accepted);
	EXPECT_EQ(Notifications, 2);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_STREQ(pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
}

TEST(BindRebindAllocation, ClearingSucceedsWithCppAllocationDisabled)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(Prepare(*pBinds));
	bool Accepted = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Accepted = pBinds->Rebind(OLD, EMPTY_BIND_SLOT, COMMAND, [](const CBindSlot &) noexcept {});
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Accepted);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_STREQ(pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), "+hook");
}
