#include <game/client/components/tclient/bindwheel.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	CBindWheel::CBind Bind(const char *pName)
	{
		CBindWheel::CBind Result;
		str_copy(Result.m_aName, pName);
		str_copy(Result.m_aCommand, "echo bind");
		return Result;
	}

	CQmBindWheelState Pending(int Index)
	{
		CQmBindWheelState State;
		State.Open();
		State.Release(Index);
		return State;
	}
}

TEST(QmBindWheelMutation, SuccessfulChangePublishesTheEntryAndInvalidatesPendingSelection)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds;
	ASSERT_TRUE(State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); }));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "new");
	EXPECT_FALSE(State.TakePendingBind());
}

TEST(QmBindWheelMutation, InitialAllocationFailureKeepsEmptyEntriesAndPendingSelection)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds;
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	EXPECT_TRUE(vBinds.empty());
	EXPECT_EQ(State.TakePendingBind(), 0);
}

TEST(QmBindWheelMutation, GrowthFailureKeepsExistingStorageContentAndPendingSelection)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds{Bind("old")};
	vBinds.resize(vBinds.capacity());
	const auto *pStorage = vBinds.data();
	const size_t Size = vBinds.size();
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	ASSERT_EQ(vBinds.size(), Size);
	EXPECT_EQ(vBinds.data(), pStorage);
	EXPECT_STREQ(vBinds[0].m_aName, "old");
	EXPECT_EQ(State.TakePendingBind(), 0);
}

TEST(QmBindWheelMutation, LegacyResizeFailureKeepsTheOriginalEntries)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds{Bind("old")};
	const size_t Target = vBinds.capacity() + 3;
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds.resize(Target); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "old");
	EXPECT_EQ(State.TakePendingBind(), 0);
}

TEST(QmBindWheelMutation, AFailedAppendCanBeRetriedWithoutLosingTheOldSelection)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds;
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); });
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	ASSERT_FALSE(Changed);
	ASSERT_TRUE(State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); }));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "new");
	EXPECT_FALSE(State.TakePendingBind());
}

TEST(QmBindWheelMutation, AllocationFailureDoesNotCloseAnActiveWheel)
{
	CQmBindWheelState State;
	State.Open();
	std::vector<CBindWheel::CBind> vBinds;
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds.push_back(Bind("new")); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	EXPECT_TRUE(State.IsActive());
	State.Release(0);
	EXPECT_EQ(State.TakePendingBind(), 0);
}

TEST(QmBindWheelMutation, FixedEntryReplacementNeedsNoAllocationAndInvalidatesSelection)
{
	auto State = Pending(0);
	std::vector<CBindWheel::CBind> vBinds{Bind("old")};
	const auto Candidate = Bind("new");
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Changed = State.TryChangeBinds([&] { vBinds[0] = Candidate; });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Changed);
	EXPECT_FALSE(Triggered);
	EXPECT_STREQ(vBinds[0].m_aName, "new");
	EXPECT_FALSE(State.TakePendingBind());
}

TEST(QmBindWheelMutation, LengthFailureBeforeMutationKeepsPendingSelection)
{
	auto State = Pending(2);
	EXPECT_FALSE(State.TryChangeBinds([] { throw std::length_error("binding capacity"); }));
	EXPECT_EQ(State.TakePendingBind(), 2);
}

TEST(QmBindWheelMutation, UnexpectedFailureStillPropagates)
{
	auto State = Pending(2);
	EXPECT_THROW(State.TryChangeBinds([] { throw std::logic_error("unexpected mutation failure"); }), std::logic_error);
	EXPECT_EQ(State.TakePendingBind(), 2);
}

TEST(QmBindWheelMutation, CancellationAfterAFailedMutationStillDiscardsPendingSelection)
{
	auto State = Pending(2);
	EXPECT_FALSE(State.TryChangeBinds([] { throw std::bad_alloc(); }));
	State.Cancel();
	EXPECT_FALSE(State.IsActive());
	EXPECT_FALSE(State.TakePendingBind());
}
