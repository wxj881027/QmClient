#include <game/client/components/qmclient/core/qm_dispatch_logic.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <vector>

TEST(QmDispatch, RequiresFreezeAndRejectsLateRegistration)
{
	CQmUpdateDispatch Registry;
	int Calls = 0;
	auto Enabled = [&](const auto &) { ++Calls; return true; };
	auto Run = [&](const auto &) { ++Calls; };
	EXPECT_EQ(Registry.Dispatch(EQmUpdateSlot::UPDATE, Enabled, Run), EQmDispatchResult::NOT_FROZEN);
	EXPECT_EQ(Registry.DispatchUntilConsumed(Enabled, Enabled), EQmDispatchResult::NOT_FROZEN);
	EXPECT_EQ(Calls, 0);
	EXPECT_FALSE(Registry.Frozen());
	ASSERT_EQ(Registry.Register(EQmUpdateSlot::UPDATE, 0, "qm.test"), EQmDispatchRegistration::REGISTERED);
	Registry.Freeze();
	Registry.Freeze();
	EXPECT_TRUE(Registry.Frozen());
	EXPECT_EQ(Registry.Size(), 1u);
	EXPECT_EQ(Registry.Register(EQmUpdateSlot::UPDATE, 0, "qm.late"), EQmDispatchRegistration::FROZEN);
	EXPECT_EQ(Registry.Dispatch(EQmUpdateSlot::UPDATE, Enabled, Run), EQmDispatchResult::COMPLETED);
	EXPECT_EQ(Calls, 2);
}

TEST(QmDispatch, ValidatesSlotsIdsAndCapacityWithoutChangingEntries)
{
	CQmDispatchRegistry<EQmRenderSlot, 2> Registry;
	EXPECT_EQ(Registry.Register(static_cast<EQmRenderSlot>(-1), 0, "qm.a"), EQmDispatchRegistration::INVALID_SLOT);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::COUNT, 0, "qm.a"), EQmDispatchRegistration::INVALID_SLOT);
	EXPECT_EQ(Registry.Register(static_cast<EQmRenderSlot>(999), 0, "qm.a"), EQmDispatchRegistration::INVALID_SLOT);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, ""), EQmDispatchRegistration::INVALID_ID);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, std::string(64, 'a')), EQmDispatchRegistration::INVALID_ID);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, std::string_view("a\0b", 3)), EQmDispatchRegistration::INVALID_ID);
	ASSERT_EQ(Registry.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, "qm.a"), EQmDispatchRegistration::REGISTERED);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::HUD_OVERLAY, 1, "qm.a"), EQmDispatchRegistration::DUPLICATE_ID);
	ASSERT_EQ(Registry.Register(EQmRenderSlot::HUD_OVERLAY, 0, "qm.b"), EQmDispatchRegistration::REGISTERED);
	EXPECT_EQ(Registry.Register(EQmRenderSlot::HUD_OVERLAY, 0, "qm.c"), EQmDispatchRegistration::CAPACITY_EXCEEDED);
	EXPECT_EQ(Registry.Size(), 2u);
	Registry.Freeze();
	int Calls = 0;
	auto Enabled = [&](const auto &) { ++Calls; return true; };
	EXPECT_EQ(Registry.Dispatch(static_cast<EQmRenderSlot>(-1), Enabled, Enabled), EQmDispatchResult::INVALID_SLOT);
	EXPECT_EQ(Registry.Dispatch(EQmRenderSlot::COUNT, Enabled, Enabled), EQmDispatchResult::INVALID_SLOT);
	EXPECT_EQ(Calls, 0);
	EXPECT_TRUE(QmRenderSlotValid(EQmRenderSlot::WORLD_BACKGROUND));
	EXPECT_TRUE(QmRenderSlotValid(EQmRenderSlot::MENU_OVERLAY));
	EXPECT_FALSE(QmRenderSlotValid(EQmRenderSlot::COUNT));
	EXPECT_FALSE(QmRenderSlotValid(static_cast<EQmRenderSlot>(-1)));
}

TEST(QmDispatch, OwnsIdAndSupportsMaximumLength)
{
	CQmUpdateDispatch Registry;
	std::string Id(CQmUpdateDispatch::MAX_ID_LENGTH, 'x');
	ASSERT_EQ(Registry.Register(EQmUpdateSlot::UPDATE, 0, Id, 7), EQmDispatchRegistration::REGISTERED);
	Id.assign("changed");
	Registry.Freeze();
	Registry.Dispatch(EQmUpdateSlot::UPDATE, [](const auto &) { return true; }, [](const auto &Entry) {
		EXPECT_EQ(Entry.Id(), std::string(CQmUpdateDispatch::MAX_ID_LENGTH, 'x'));
		EXPECT_EQ(Entry.m_UserIndex, 7u);
	});
}

TEST(QmDispatch, SortsBySlotPriorityAndStableIdRegardlessOfRegistrationOrder)
{
	for(bool Reverse : {false, true})
	{
		CQmRenderDispatch Registry;
		const std::array<const char *, 5> apIds = {"qm.z", "qm.b", "qm.a", "qm.first", "qm.last"};
		const std::array<int, 5> aPriorities = {std::numeric_limits<int>::min(), 0, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
		for(std::size_t i = 0; i < apIds.size(); ++i)
		{
			const std::size_t Index = Reverse ? apIds.size() - 1 - i : i;
			ASSERT_EQ(Registry.Register(Index == 0 ? EQmRenderSlot::HUD_OVERLAY : EQmRenderSlot::ENTITY_OVERLAY,
					  aPriorities[Index], apIds[Index]), EQmDispatchRegistration::REGISTERED);
		}
		Registry.Freeze();
		std::vector<std::string> vIds;
		EXPECT_EQ(Registry.DispatchUntilConsumed([](const auto &) { return true; }, [&](const auto &Entry) {
			vIds.emplace_back(Entry.Id());
			return false;
		}), EQmDispatchResult::COMPLETED);
		EXPECT_EQ(vIds, (std::vector<std::string>{"qm.first", "qm.a", "qm.b", "qm.last", "qm.z"}));
	}
}

TEST(QmDispatch, VisitsOnlyRequestedSlotIncludingEmptySlots)
{
	CQmRenderDispatch Registry;
	ASSERT_EQ(Registry.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, "qm.player_indicator"), EQmDispatchRegistration::REGISTERED);
	ASSERT_EQ(Registry.Register(EQmRenderSlot::HUD_OVERLAY, 0, "qm.speedrun_timer"), EQmDispatchRegistration::REGISTERED);
	Registry.Freeze();
	int Checks = 0;
	int Runs = 0;
	for(int Slot = 0; Slot < static_cast<int>(EQmRenderSlot::COUNT); ++Slot)
	{
		EXPECT_EQ(Registry.Dispatch(static_cast<EQmRenderSlot>(Slot), [&](const auto &Entry) {
			EXPECT_EQ(Entry.m_Slot, static_cast<EQmRenderSlot>(Slot));
			++Checks;
			return true;
		}, [&](const auto &) { ++Runs; }), EQmDispatchResult::COMPLETED);
	}
	EXPECT_EQ(Checks, 2);
	EXPECT_EQ(Runs, 2);
}

TEST(QmDispatch, DisabledSkipsInputConstructionLogicRenderAndTasks)
{
	CQmUpdateDispatch Registry;
	ASSERT_EQ(Registry.Register(EQmUpdateSlot::UPDATE, 0, "qm.auto_team_lock"), EQmDispatchRegistration::REGISTERED);
	Registry.Freeze();
	bool Enabled = false;
	int Builds = 0;
	int Logic = 0;
	int Renders = 0;
	int Tasks = 0;
	const auto Run = [&](const auto &) {
		++Builds;
		++Logic;
		++Renders;
		++Tasks;
	};
	for(int i = 0; i < 10000; ++i)
		Registry.Dispatch(EQmUpdateSlot::UPDATE, [&](const auto &) { return Enabled; }, Run);
	EXPECT_EQ(Builds, 0);
	EXPECT_EQ(Logic, 0);
	EXPECT_EQ(Renders, 0);
	EXPECT_EQ(Tasks, 0);
	Enabled = true;
	Registry.Dispatch(EQmUpdateSlot::UPDATE, [&](const auto &) { return Enabled; }, Run);
	Enabled = false;
	Registry.Dispatch(EQmUpdateSlot::UPDATE, [&](const auto &) { return Enabled; }, Run);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(Logic, 1);
	EXPECT_EQ(Renders, 1);
	EXPECT_EQ(Tasks, 1);
}

TEST(QmDispatch, InputUsesFixedSlotPriorityAndStopsImmediatelyOnConsumption)
{
	CQmInputDispatch Registry;
	ASSERT_EQ(Registry.Register(EQmInputSlot::GAMEPLAY, std::numeric_limits<int>::min(), "qm.game"), EQmDispatchRegistration::REGISTERED);
	ASSERT_EQ(Registry.Register(EQmInputSlot::UI, -10, "qm.ui.low"), EQmDispatchRegistration::REGISTERED);
	ASSERT_EQ(Registry.Register(EQmInputSlot::UI, 10, "qm.ui.z"), EQmDispatchRegistration::REGISTERED);
	ASSERT_EQ(Registry.Register(EQmInputSlot::UI, 10, "qm.ui.high"), EQmDispatchRegistration::REGISTERED);
	ASSERT_EQ(Registry.Register(EQmInputSlot::MODAL, std::numeric_limits<int>::max(), "qm.modal"), EQmDispatchRegistration::REGISTERED);
	Registry.Freeze();
	std::vector<std::string> vChecks;
	std::vector<std::string> vRuns;
	EXPECT_EQ(Registry.DispatchUntilConsumed([&](const auto &Entry) {
		vChecks.emplace_back(Entry.Id());
		return Entry.Id() != "qm.modal";
	}, [&](const auto &Entry) {
		vRuns.emplace_back(Entry.Id());
		return true;
	}), EQmDispatchResult::CONSUMED);
	EXPECT_EQ(vChecks, (std::vector<std::string>{"qm.modal", "qm.ui.high"}));
	EXPECT_EQ(vRuns, (std::vector<std::string>{"qm.ui.high"}));
	vRuns.clear();
	EXPECT_EQ(Registry.DispatchUntilConsumed([](const auto &) { return true; }, [&](const auto &Entry) {
		vRuns.emplace_back(Entry.Id());
		return false;
	}), EQmDispatchResult::COMPLETED);
	EXPECT_EQ(vRuns, (std::vector<std::string>{"qm.modal", "qm.ui.high", "qm.ui.z", "qm.ui.low", "qm.game"}));
}

TEST(QmDispatch, EmptyAndFullyDisabledInputNeverConsume)
{
	CQmInputDispatch Empty;
	Empty.Freeze();
	auto Fail = [](const auto &) { ADD_FAILURE() << "Unexpected callback"; return true; };
	EXPECT_EQ(Empty.DispatchUntilConsumed(Fail, Fail), EQmDispatchResult::COMPLETED);
	EXPECT_EQ(Empty.Dispatch(EQmInputSlot::MODAL, Fail, Fail), EQmDispatchResult::COMPLETED);
	CQmInputDispatch Disabled;
	ASSERT_EQ(Disabled.Register(EQmInputSlot::GAMEPLAY, 0, "qm.disabled"), EQmDispatchRegistration::REGISTERED);
	Disabled.Freeze();
	EXPECT_EQ(Disabled.DispatchUntilConsumed([](const auto &) { return false; }, Fail), EQmDispatchResult::COMPLETED);
}
