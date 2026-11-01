#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsBindCache.h>
#include <game/client/components/binds.h>
#include <game/client/components/binds_deepfly_mode.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>

#include <array>
#include <memory>

namespace
{
	constexpr CBindSlot OLD(KEY_X, KeyModifier::NONE);
	constexpr CBindSlot TARGET(KEY_Y, 1 << KeyModifier::CTRL);
	constexpr char COMMAND[] = "say original-binding-with-a-long-owned-command";

	struct SMove
	{
		CBindStorage m_Binds;
		bool m_Accepted = false;
		int m_Markers = 0;
	};

	void MoveFromScript(IConsole::IResult *, void *pUser)
	{
		auto &Move = *static_cast<SMove *>(pUser);
		Move.m_Accepted = Move.m_Binds.Rebind(OLD, TARGET, Move.m_Binds.Get(OLD.m_Key, OLD.m_ModifierMask), [](const CBindSlot &) noexcept {});
	}
	void Mark(IConsole::IResult *, void *pUser)
	{
		++static_cast<SMove *>(pUser)->m_Markers;
	}
}

TEST(BindRebindIntegration, PreparationFailureKeepsTheExistingCacheValidAndRetryReadsTheNewSlot)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(OLD.m_Key, COMMAND, false, OLD.m_ModifierMask));
	ASSERT_TRUE(pBinds->Bind(TARGET.m_Key, "+hook", false, TARGET.m_ModifierMask));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	CHeapAllocationProbe Probe;
	Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_FALSE(pBinds->Rebind(OLD, TARGET, COMMAND, [](const CBindSlot &) noexcept {}, CHeapAllocationProbe::Allocate));
	EXPECT_TRUE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), OLD);
	EXPECT_EQ(Cache.Find(*pBinds, "+hook"), TARGET);
	ASSERT_TRUE(pBinds->Rebind(OLD, TARGET, COMMAND, [](const CBindSlot &) noexcept {}));
	EXPECT_FALSE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), TARGET);
	EXPECT_EQ(Cache.Find(*pBinds, "+hook"), EMPTY_BIND_SLOT);
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	EXPECT_TRUE(Cache.Ready(*pBinds));
	EXPECT_EQ(Cache.Find(*pBinds, COMMAND), TARGET);
}

TEST(BindRebindIntegration, SnapshotBuiltBetweenNotificationsIsInvalidatedByTheTargetWrite)
{
	auto pBinds = std::make_unique<CBindStorage>();
	ASSERT_TRUE(pBinds->Bind(OLD.m_Key, COMMAND, false, OLD.m_ModifierMask));
	ASSERT_TRUE(pBinds->Bind(TARGET.m_Key, "+hook", false, TARGET.m_ModifierMask));
	CSettingsBindCache Cache;
	ASSERT_TRUE(Cache.Ensure(*pBinds));
	bool Built = false;
	std::array<CBindSlot, 2> aCurrent{EMPTY_BIND_SLOT, EMPTY_BIND_SLOT};
	size_t Count = 0;
	ASSERT_TRUE(pBinds->Rebind(OLD, TARGET, COMMAND, [&](const CBindSlot &Slot) noexcept {
		if(Slot == OLD)
			Built = Cache.Ensure(*pBinds);
		if(Count < aCurrent.size())
			aCurrent[Count] = Cache.Find(*pBinds, COMMAND);
		++Count;
	}));
	EXPECT_TRUE(Built);
	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aCurrent[0], EMPTY_BIND_SLOT);
	EXPECT_EQ(aCurrent[1], TARGET);
	EXPECT_FALSE(Cache.Ready(*pBinds));
}

TEST(BindRebindIntegration, ClassifierObservesTheExistingUnbindAndTargetStages)
{
	auto pBinds = std::make_unique<CBindStorage>();
	constexpr char DEEPFLY[] = "+fire;+toggle cl_dummy_hammer 1 0";
	ASSERT_TRUE(pBinds->Bind(OLD.m_Key, DEEPFLY, false, OLD.m_ModifierMask));
	ASSERT_TRUE(pBinds->Bind(TARGET.m_Key, "+hook", false, TARGET.m_ModifierMask));
	std::array<int, 2> aOldModes{}, aTargetModes{};
	size_t Count = 0;
	ASSERT_TRUE(pBinds->Rebind(OLD, TARGET, DEEPFLY, [&](const CBindSlot &) noexcept {
		if(Count < aOldModes.size())
		{
			aOldModes[Count] = DetectDeepflyModeFromBindCommand(pBinds->Get(OLD.m_Key, OLD.m_ModifierMask));
			aTargetModes[Count] = DetectDeepflyModeFromBindCommand(pBinds->Get(TARGET.m_Key, TARGET.m_ModifierMask));
		}
		++Count;
	}));
	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aOldModes[0], DEEPFLY_MODE_NONE);
	EXPECT_EQ(aTargetModes[0], DEEPFLY_MODE_NONE);
	EXPECT_EQ(aOldModes[1], DEEPFLY_MODE_NONE);
	EXPECT_EQ(aTargetModes[1], DEEPFLY_MODE_DF);
}

TEST(BindRebindIntegration, MovingTheExecutingBindingPreservesTheOriginalConsoleTail)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	auto pMove = std::make_unique<SMove>();
	ASSERT_TRUE(pMove->m_Binds.Bind(OLD.m_Key, "rebind_move;rebind_mark", false, OLD.m_ModifierMask));
	pConsole->Register("rebind_move", "", CFGFLAG_CLIENT, MoveFromScript, pMove.get(), "");
	pConsole->Register("rebind_mark", "", CFGFLAG_CLIENT, Mark, pMove.get(), "");
	CBinds::ExecuteCommand(pConsole.get(), pMove->m_Binds.Get(OLD.m_Key, OLD.m_ModifierMask), false, 1, IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_TRUE(pMove->m_Accepted);
	EXPECT_EQ(pMove->m_Markers, 1);
	EXPECT_STREQ(pMove->m_Binds.Get(OLD.m_Key, OLD.m_ModifierMask), "");
	EXPECT_STREQ(pMove->m_Binds.Get(TARGET.m_Key, TARGET.m_ModifierMask), "rebind_move;rebind_mark");
}
