#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/components/binds.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/lineinput.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>
#include <test/support/prediction_allocation_failure.h>

#include <memory>
#include <utility>

using qm_card_catalog::CommitBindCommandDraft;
using qm_card_catalog::EBindCommandCommitResult;

namespace
{
	constexpr char EXISTING[] = "echo previous";
	constexpr char DRAFT[] = "qm_bind_card_test_action player draft";

	auto BindEditorConsole()
	{
		auto pConsole = CreateConsole(CFGFLAG_CLIENT);
		pConsole->Register("qm_bind_card_test_action", "r[value]", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {}, nullptr, "");
		return pConsole;
	}
}

TEST(QmBindCommandCardCommit, RejectedBindingWriteKeepsTheDraftForRetry)
{
	auto pConsole = BindEditorConsole();
	auto pBinds = std::make_unique<CBindStorage>();
	constexpr CBindSlot SLOT(KEY_X, KeyModifier::NONE);
	ASSERT_TRUE(pBinds->Bind(SLOT.m_Key, EXISTING, false, SLOT.m_ModifierMask));
	const auto OldRevision = pBinds->Revision();
	CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> Draft;
	Draft.Set(DRAFT);

	CHeapAllocationProbe Allocator;
	Allocator.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	int Notifications = 0;
	const auto Replace = [&](const char *pUpdated) {
		return pBinds->Rebind(EMPTY_BIND_SLOT, SLOT, pUpdated, [&](const CBindSlot &) noexcept { ++Notifications; }, CHeapAllocationProbe::Allocate);
	};

	EXPECT_EQ(CommitBindCommandDraft(*pConsole, Draft, "x", pBinds->Get(SLOT.m_Key, SLOT.m_ModifierMask), Replace), EBindCommandCommitResult::FAILED);
	EXPECT_EQ(Allocator.m_Requests, 1);
	EXPECT_STREQ(Draft.GetString(), DRAFT);
	EXPECT_STREQ(pBinds->Get(SLOT.m_Key, SLOT.m_ModifierMask), EXISTING);
	EXPECT_EQ(pBinds->Revision(), OldRevision);
	EXPECT_EQ(Notifications, 0);

	Allocator.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	ASSERT_EQ(CommitBindCommandDraft(*pConsole, Draft, "x", pBinds->Get(SLOT.m_Key, SLOT.m_ModifierMask), Replace), EBindCommandCommitResult::COMMITTED);
	EXPECT_STREQ(pBinds->Get(SLOT.m_Key, SLOT.m_ModifierMask), "echo previous; qm_bind_card_test_action player draft");
	EXPECT_EQ(pBinds->Revision(), OldRevision + 1);
	EXPECT_EQ(Notifications, 1);
	EXPECT_TRUE(Draft.IsEmpty());
}

TEST(QmBindCommandCardCommit, CommitPreparationFailureKeepsTheDraftAndSkipsTheWrite)
{
	auto pConsole = BindEditorConsole();
	CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> Draft;
	Draft.Set(DRAFT);
	bool WriteCalled = false;
	const auto Replace = [&](const char *) {
		WriteCalled = true;
		return true;
	};

	EBindCommandCommitResult Result;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure(0);
		Result = CommitBindCommandDraft(*pConsole, Draft, "x", EXISTING, Replace);
		AllocationFailed = Failure.Triggered();
	}

	EXPECT_TRUE(AllocationFailed);
	EXPECT_EQ(Result, EBindCommandCommitResult::FAILED);
	EXPECT_FALSE(WriteCalled);
	EXPECT_STREQ(Draft.GetString(), DRAFT);
}

TEST(QmBindCommandCardPicker, FailedSelectionKeepsThePreviousArgumentInput)
{
	const qm_bind_editor::SCommandOption Previous{"Previous", {"qm_previous", "s[target]", ""}};
	const qm_bind_editor::SCommandOption Replacement{"Replacement command with a long owned label", {"qm_replacement", "s[name]", ""}};
	qm_card_catalog::SBindCommandPickerSelection Shown;
	const auto Publish = [&](qm_card_catalog::SBindCommandPickerSelection &&Selection) {
		Shown = std::move(Selection);
	};
	ASSERT_TRUE(qm_card_catalog::SelectBindCommandOption(Previous, Publish));
	ASSERT_EQ(Shown.m_vArguments.size(), 1u);
	auto *pPreviousArgument = Shown.m_vArguments[0].get();
	pPreviousArgument->m_Input.Set("draft value");

	bool Selected;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure(0);
		Selected = qm_card_catalog::SelectBindCommandOption(Replacement, Publish);
		AllocationFailed = Failure.Triggered();
	}

	EXPECT_TRUE(AllocationFailed);
	EXPECT_FALSE(Selected);
	EXPECT_EQ(Shown.m_Label, Previous.m_Label);
	ASSERT_EQ(Shown.m_vArguments.size(), 1u);
	ASSERT_EQ(Shown.m_vArguments[0].get(), pPreviousArgument);
	EXPECT_STREQ(pPreviousArgument->m_Input.GetString(), "draft value");

	ASSERT_TRUE(qm_card_catalog::SelectBindCommandOption(Replacement, Publish));
	EXPECT_EQ(Shown.m_Label, Replacement.m_Label);
	ASSERT_EQ(Shown.m_vArguments.size(), 1u);
	EXPECT_EQ(Shown.m_vArguments[0]->m_Parameter.m_Name, "name");
	EXPECT_TRUE(Shown.m_vArguments[0]->m_Input.IsEmpty());
}
