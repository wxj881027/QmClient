#include <game/client/components/binds.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>

#include <array>
#include <memory>

namespace
{
	constexpr CBindSlot OLD(KEY_X, KeyModifier::NONE);
	constexpr CBindSlot TARGET(KEY_Y, 1 << KeyModifier::CTRL);
	constexpr char COMMAND[] = "say original-binding-with-a-long-owned-command";

	class BindRebind : public testing::Test
	{
	protected:
		std::unique_ptr<CBindStorage> m_pBinds = std::make_unique<CBindStorage>();
		int m_Notifications = 0;
		void SetUp() override
		{
			ASSERT_TRUE(m_pBinds->Bind(OLD.m_Key, COMMAND, false, OLD.m_ModifierMask));
			ASSERT_TRUE(m_pBinds->Bind(TARGET.m_Key, "+hook", false, TARGET.m_ModifierMask));
			ASSERT_TRUE(m_pBinds->Bind(KEY_Z, "+left"));
		}
		auto Notify()
		{
			return [this](const CBindSlot &) noexcept { ++m_Notifications; };
		}
		bool Rebind(const char *pCommand = COMMAND, void *(*pAllocate)(size_t) = std::malloc)
		{
			return m_pBinds->Rebind(OLD, TARGET, pCommand, Notify(), pAllocate);
		}
		void ExpectRejected(void *(*pAllocate)(size_t))
		{
			const char *pOld = m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask);
			const char *pTarget = m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask);
			const uint64_t Revision = m_pBinds->Revision();
			EXPECT_FALSE(Rebind(COMMAND, pAllocate));
			EXPECT_EQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), pOld);
			EXPECT_EQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), pTarget);
			EXPECT_STREQ(pOld, COMMAND);
			EXPECT_STREQ(pTarget, "+hook");
			EXPECT_STREQ(m_pBinds->Get(KEY_Z, KeyModifier::NONE), "+left");
			EXPECT_EQ(m_pBinds->Revision(), Revision);
			EXPECT_EQ(m_Notifications, 0);
		}
	};
}

TEST_F(BindRebind, NullAllocationRetainsBothSlotsAndRevision)
{
	ExpectRejected([](size_t) -> void * { return nullptr; });
}

TEST_F(BindRebind, BadAllocationRetainsBothSlotsAndRevision)
{
	ExpectRejected([](size_t) -> void * { throw std::bad_alloc(); });
}

TEST_F(BindRebind, LengthFailureRetainsBothSlotsAndRevision)
{
	ExpectRejected([](size_t) -> void * { throw std::length_error("rebind rejected"); });
}

TEST_F(BindRebind, UnknownPreparationFailurePropagatesBeforeEitherCommit)
{
	const uint64_t Revision = m_pBinds->Revision();
	EXPECT_THROW(Rebind(COMMAND, [](size_t) -> void * { throw 7; }), int);
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), COMMAND);
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), "+hook");
	EXPECT_EQ(m_pBinds->Revision(), Revision);
	EXPECT_EQ(m_Notifications, 0);
}

TEST_F(BindRebind, NotificationsObserveUnbindThenPreparedMigratedWrite)
{
	struct SStage
	{
		CBindSlot m_Slot = EMPTY_BIND_SLOT;
		uint64_t m_Revision = 0;
		char m_aOld[128]{};
		char m_aTarget[128]{};
	};
	std::array<SStage, 2> aStages;
	size_t Count = 0;
	const uint64_t Revision = m_pBinds->Revision();
	ASSERT_TRUE(m_pBinds->Rebind(OLD, TARGET, "tc_jump_hint 1", [&](const CBindSlot &Slot) noexcept {
		if(Count < aStages.size())
		{
			auto &Stage = aStages[Count];
			Stage.m_Slot = Slot;
			Stage.m_Revision = m_pBinds->Revision();
			str_copy(Stage.m_aOld, m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask));
			str_copy(Stage.m_aTarget, m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask));
		}
		++Count;
	}));
	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aStages[0].m_Slot, OLD);
	EXPECT_EQ(aStages[0].m_Revision, Revision + 1);
	EXPECT_STREQ(aStages[0].m_aOld, "");
	EXPECT_STREQ(aStages[0].m_aTarget, "+hook");
	EXPECT_EQ(aStages[1].m_Slot, TARGET);
	EXPECT_EQ(aStages[1].m_Revision, Revision + 2);
	EXPECT_STREQ(aStages[1].m_aOld, "");
	EXPECT_STREQ(aStages[1].m_aTarget, "qm_jump_hint 1");
	EXPECT_STREQ(m_pBinds->Get(KEY_Z, KeyModifier::NONE), "+left");
}

TEST_F(BindRebind, AddingWithoutAnOldSlotOnlyNotifiesTheTarget)
{
	CBindSlot Changed = EMPTY_BIND_SLOT;
	const uint64_t Revision = m_pBinds->Revision();
	ASSERT_TRUE(m_pBinds->Rebind(EMPTY_BIND_SLOT, TARGET, "+jump", [&](const CBindSlot &Slot) noexcept { Changed = Slot; }));
	EXPECT_EQ(Changed, TARGET);
	EXPECT_EQ(m_pBinds->Revision(), Revision + 1);
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), COMMAND);
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), "+jump");
}

TEST_F(BindRebind, ClearingDoesNotPrepareOrAllocateANewCommand)
{
	const uint64_t Revision = m_pBinds->Revision();
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	ASSERT_TRUE(m_pBinds->Rebind(OLD, EMPTY_BIND_SLOT, COMMAND, Notify(), CHeapAllocationProbe::Allocate));
	EXPECT_EQ(Probe.m_Requests, 0);
	EXPECT_EQ(m_Notifications, 1);
	EXPECT_EQ(m_pBinds->Revision(), Revision + 1);
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), "+hook");
}

TEST_F(BindRebind, EmptySlotsLeaveTheRevisionAndNotificationsUnchanged)
{
	const uint64_t Revision = m_pBinds->Revision();
	ASSERT_TRUE(m_pBinds->Rebind(EMPTY_BIND_SLOT, EMPTY_BIND_SLOT, COMMAND, Notify()));
	EXPECT_EQ(m_pBinds->Revision(), Revision);
	EXPECT_EQ(m_Notifications, 0);
}

TEST_F(BindRebind, BorrowedSourceIsCopiedBeforeTheOldSlotIsFreed)
{
	ASSERT_TRUE(Rebind(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask)));
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
}

TEST_F(BindRebind, BorrowedTargetIsCopiedBeforeItIsOverwritten)
{
	ASSERT_TRUE(Rebind(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask)));
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), "+hook");
}

TEST_F(BindRebind, MovingBetweenModifiersOfOneKeyPreservesOtherModifiers)
{
	ASSERT_TRUE(m_pBinds->Bind(KEY_X, "+hook", false, 1 << KeyModifier::CTRL));
	ASSERT_TRUE(m_pBinds->Bind(KEY_X, "+jump", false, 1 << KeyModifier::SHIFT));
	ASSERT_TRUE(m_pBinds->Rebind(OLD, CBindSlot(KEY_X, 1 << KeyModifier::CTRL), COMMAND, Notify()));
	EXPECT_STREQ(m_pBinds->Get(KEY_X, KeyModifier::NONE), "");
	EXPECT_STREQ(m_pBinds->Get(KEY_X, 1 << KeyModifier::CTRL), COMMAND);
	EXPECT_STREQ(m_pBinds->Get(KEY_X, 1 << KeyModifier::SHIFT), "+jump");
}

TEST_F(BindRebind, SameSlotBorrowRetainsTheCommandAcrossBothStages)
{
	const uint64_t Revision = m_pBinds->Revision();
	ASSERT_TRUE(m_pBinds->Rebind(OLD, OLD, m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), Notify()));
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), COMMAND);
	EXPECT_EQ(m_pBinds->Revision(), Revision + 2);
	EXPECT_EQ(m_Notifications, 2);
}

TEST_F(BindRebind, NotificationRebindingTheOldSlotDoesNotReplaceThePreparedCommand)
{
	bool Nested = false;
	ASSERT_TRUE(m_pBinds->Rebind(OLD, TARGET, m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), [&](const CBindSlot &Slot) noexcept {
		if(Slot == OLD)
			Nested = m_pBinds->Bind(OLD.m_Key, "say rebound", false, OLD.m_ModifierMask);
	}));
	EXPECT_TRUE(Nested);
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "say rebound");
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
}

TEST_F(BindRebind, NotificationUnbindAllStillAllowsThePreparedTargetStage)
{
	ASSERT_TRUE(m_pBinds->Rebind(OLD, TARGET, m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), [&](const CBindSlot &Slot) noexcept {
		if(Slot == OLD)
			m_pBinds->UnbindAll();
	}));
	EXPECT_STREQ(m_pBinds->Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
	EXPECT_STREQ(m_pBinds->Get(KEY_Z, KeyModifier::NONE), "");
}

TEST_F(BindRebind, RetryingAfterPreparationFailureCommitsOnlyTheSuccessfulAttempt)
{
	const uint64_t Revision = m_pBinds->Revision();
	EXPECT_FALSE(Rebind(COMMAND, [](size_t) -> void * { return nullptr; }));
	ASSERT_TRUE(Rebind());
	EXPECT_EQ(m_pBinds->Revision(), Revision + 2);
	EXPECT_EQ(m_Notifications, 2);
	EXPECT_STREQ(m_pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask), COMMAND);
}
