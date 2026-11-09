// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/qm_ime_policy.h>

#include <game/client/qm_ime_manager.h>

#include <gtest/gtest.h>
#include <test/support/qm_ui_test_input.h>

#include <string>
#include <vector>

TEST(QmImeFocus, ClosedOwnersRejectStaleActiveInputs)
{
	for(const auto Priority : {EInputPriority::NONE, EInputPriority::UI, EInputPriority::CHAT, EInputPriority::CONSOLE})
		EXPECT_FALSE(QmImeHasLiveInputOwner(Priority, false, false, false, false, false));
	EXPECT_FALSE(QmImeHasLiveInputOwner(EInputPriority::CHAT, true, false, true, false, false));
	EXPECT_FALSE(QmImeHasLiveInputOwner(EInputPriority::CONSOLE, true, true, false, false, false));
}

TEST(QmImeFocus, OpenTextOwnersKeepTheirOwnPriority)
{
	EXPECT_TRUE(QmImeHasLiveInputOwner(EInputPriority::UI, true, false, false, false, false));
	EXPECT_TRUE(QmImeHasLiveInputOwner(EInputPriority::UI, false, false, false, true, false));
	EXPECT_TRUE(QmImeHasLiveInputOwner(EInputPriority::CHAT, false, true, false, false, false));
	EXPECT_TRUE(QmImeHasLiveInputOwner(EInputPriority::CONSOLE, false, false, true, false, false));
}

TEST(QmImeFocus, ChatPopupInputExpiresWithChatOwner)
{
	EXPECT_TRUE(QmImeHasLiveInputOwner(EInputPriority::UI, false, true, false, false, true));
	EXPECT_FALSE(QmImeHasLiveInputOwner(EInputPriority::UI, false, true, false, false, false));
	EXPECT_FALSE(QmImeHasLiveInputOwner(EInputPriority::UI, false, false, false, false, true));
}

TEST(QmImeTextInputSession, FocusStartsOnceAndStopsOnRelease)
{
	CQmImeTextInputSession Session;
	qm_ui_test::CTestInput Input;
	int Field = 0;
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(Input, nullptr), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(Input, nullptr), EAction::STOP);
	EXPECT_EQ(Session.UpdateFocus(Input, nullptr), EAction::NONE);
	EXPECT_EQ(Input.m_TextInputStarts, 1);
	EXPECT_EQ(Input.m_TextInputStops, 1);
}

TEST(QmImeTextInputSession, SwitchingFieldsRestartsAndClearsPreviousComposition)
{
	CQmImeTextInputSession Session;
	qm_ui_test::CTestInput Input;
	int First = 0, Second = 0;
	using EAction = CQmImeTextInputSession::EAction;
	ASSERT_EQ(Session.UpdateFocus(Input, &First), EAction::START);
	Input.m_Composing = true;
	Input.m_CandidateCount = 3;
	EXPECT_EQ(Session.UpdateFocus(Input, &Second), EAction::RESTART);
	EXPECT_FALSE(Input.HasComposition());
	EXPECT_EQ(Input.m_TextInputStops, 1);
	EXPECT_EQ(Input.m_TextInputStarts, 2);
	EXPECT_EQ(Session.UpdateFocus(Input, &Second), EAction::NONE);
	EXPECT_EQ(Input.m_TextInputStarts, 2);
}

TEST(QmImeTextInputSession, ReopeningSameFieldStartsANewSession)
{
	CQmImeTextInputSession Session;
	qm_ui_test::CTestInput Input;
	int Field = 0;
	using EAction = CQmImeTextInputSession::EAction;
	ASSERT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	ASSERT_EQ(Session.UpdateFocus(Input, nullptr), EAction::STOP);
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	EXPECT_EQ(Input.m_TextInputStarts, 2);
	EXPECT_EQ(Input.m_TextInputStops, 1);
}

TEST(QmImeTextInputSession, EditorOwnsInputUntilClientReturns)
{
	CQmImeTextInputSession Session;
	qm_ui_test::CTestInput Input;
	int Field = 0;
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	EXPECT_TRUE(Session.SetClientOwnership(false));
	EXPECT_FALSE(Session.ClientOwnsInput());
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(Input, nullptr), EAction::NONE);
	EXPECT_EQ(Input.m_TextInputStarts, 1);
	EXPECT_EQ(Input.m_TextInputStops, 0);

	EXPECT_TRUE(Session.SetClientOwnership(true));
	EXPECT_EQ(Session.UpdateFocus(Input, nullptr), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
}

TEST(QmImeTextInputSession, RepeatedOwnershipDoesNotRestartComposition)
{
	CQmImeTextInputSession Session;
	qm_ui_test::CTestInput Input;
	int Field = 0;
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	EXPECT_FALSE(Session.SetClientOwnership(true));
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::NONE);
	Session.ResetFocus();
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::START);
	EXPECT_TRUE(Session.SetClientOwnership(false));
	Session.ResetFocus();
	EXPECT_FALSE(Session.SetClientOwnership(false));
	EXPECT_EQ(Session.UpdateFocus(Input, &Field), EAction::NONE);
}

TEST(QmImePlatform, SystemCandidateUiPolicyMatchesPlatform)
{
#if defined(CONF_FAMILY_WINDOWS)
	EXPECT_FALSE(QmImeShouldUseSystemCandidateUi());
	EXPECT_TRUE(QmImeShouldRenderCustomCandidateUi());
#else
	EXPECT_TRUE(QmImeShouldUseSystemCandidateUi());
	EXPECT_FALSE(QmImeShouldRenderCustomCandidateUi());
#endif
}

TEST(QmImePlatform, CandidateRenderActionKeepsLifecycleValidationOnAllPlatforms)
{
	EXPECT_EQ(QmImeComputeCandidateRenderAction(false, 0), EQmImeCandidateRenderAction::VALIDATE_ONLY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(false, 1), EQmImeCandidateRenderAction::VALIDATE_ONLY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(true, 0), EQmImeCandidateRenderAction::LEGACY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(true, 1), EQmImeCandidateRenderAction::POPUP);
}

TEST(QmImePlatform, CandidateListNotifyFlagsSelectChangedList)
{
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList(0, 0));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(0, 1));
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList(1u << 2, 2));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(1u << 2, 0));
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList((1u << 1) | (1u << 3), 3));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(1u << 3, 32));
}

TEST(QmImePlatform, CandidatePageSizeFallsBackToCount)
{
	EXPECT_EQ(QmImeCandidatePageSizeOrCount(0, 9), 9u);
	EXPECT_EQ(QmImeCandidatePageSizeOrCount(5, 9), 5u);
}

TEST(QmImePlatform, CandidateOffsetCapacityClampsMalformedCount)
{
	EXPECT_EQ(QmImeCandidateOffsetCapacity(20, 12), 2u);
	EXPECT_EQ(QmImeCandidateOffsetCapacity(11, 12), 0u);
}

TEST(QmImePlatform, BoundedUtf16LengthRejectsMalformedCandidateRanges)
{
	const unsigned char aBuffer[] = {
		0x41,
		0x00,
		0x42,
		0x00,
		0x00,
		0x00,
		0x43,
		0x00,
	};

	ASSERT_TRUE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 0).has_value());
	EXPECT_EQ(*QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 0), 2u);
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 1).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 6).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), sizeof(aBuffer)).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(nullptr, sizeof(aBuffer), 0).has_value());
}

TEST(QmImePlatform, EmptyTextEditingKeepsCandidatesForSogouPageBoundary)
{
	EXPECT_FALSE(QmImeEmptyTextEditingShouldClearCandidates());
}

TEST(QmImePlatform, PopupVisibilityDrivenByCandidateCount)
{
	EXPECT_FALSE(QmImePopupShouldBeVisible(0));
	EXPECT_TRUE(QmImePopupShouldBeVisible(1));
	EXPECT_TRUE(QmImePopupShouldBeVisible(5));
}

TEST(QmImePlatform, CandidateOnlyCompositionRetainsKeyboardUntilCandidatesClose)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 候选"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	EXPECT_TRUE(QmImeCompositionIsActive(false, static_cast<int>(State.Candidates().m_vCandidates.size())));
	State.Clear();
	EXPECT_FALSE(QmImeCompositionIsActive(false, static_cast<int>(State.Candidates().m_vCandidates.size())));
	EXPECT_TRUE(QmImeCompositionIsActive(true, 0));
}

TEST(QmImePlatform, CompositionAllowsPointerFocusChangesButSuppressesKeyboardPresses)
{
	for(const int Key : {KEY_MOUSE_1, KEY_MOUSE_2, KEY_MOUSE_9, KEY_MOUSE_WHEEL_UP, KEY_MOUSE_WHEEL_RIGHT})
	{
		SCOPED_TRACE(Key);
		EXPECT_FALSE(QmImeShouldSuppressPress(Key, true));
	}
	for(const int Key : {KEY_RETURN, KEY_KP_ENTER, KEY_BACKSPACE, KEY_ESCAPE, KEY_A})
	{
		SCOPED_TRACE(Key);
		EXPECT_TRUE(QmImeShouldSuppressPress(Key, true));
		EXPECT_FALSE(QmImeShouldSuppressPress(Key, false));
	}
}

TEST(QmImePlatform, FailedCandidateReloadKeepsPreviousOnChangeNotify)
{
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, false), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, true), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(false, true), EQmImeCandidateReloadAction::CLEAR);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(false, false), EQmImeCandidateReloadAction::KEEP_PREVIOUS);
}

TEST(QmImePlatform, StaleCandidateReloadAfterCommitIsSuppressed)
{
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, false, true), EQmImeCandidateReloadAction::KEEP_PREVIOUS);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, true, true), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_FALSE(QmImeShouldSuppressStaleCandidateReload(true, true));
	EXPECT_TRUE(QmImeShouldSuppressStaleCandidateReload(true, false));
	EXPECT_FALSE(QmImeShouldSuppressStaleCandidateReload(false, false));
}

TEST(QmImePlatform, KeyConsumedByCompositionProtectsExistingContent)
{
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_BACKSPACE));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_DELETE));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_LEFT));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_RIGHT));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_HOME));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_END));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_RETURN));
	EXPECT_TRUE(QmImeKeyConsumedByComposition(KEY_KP_ENTER));

	EXPECT_FALSE(QmImeKeyConsumedByComposition(KEY_SPACE));
	EXPECT_FALSE(QmImeKeyConsumedByComposition(KEY_TAB));
	EXPECT_FALSE(QmImeKeyConsumedByComposition(KEY_ESCAPE));
	EXPECT_FALSE(QmImeKeyConsumedByComposition(KEY_A));
}

TEST(QmImePlatform, EmptyTextEventDoesNotMutateBuffer)
{
	EXPECT_FALSE(QmImeTextEventShouldMutateBuffer(nullptr));
	EXPECT_FALSE(QmImeTextEventShouldMutateBuffer(""));
	EXPECT_TRUE(QmImeTextEventShouldMutateBuffer("a"));
	EXPECT_TRUE(QmImeTextEventShouldMutateBuffer("你"));
}

namespace
{
	SQmImeCandidateList NativePage(const char *pText)
	{
		SQmImeCandidateList Page;
		Page.m_vCandidates = {pText};
		Page.m_SelectedIndex = 0;
		Page.m_PageStart = 5;
		Page.m_PageSize = 5;
		Page.m_TotalCount = 12;
		return Page;
	}
}

TEST(QmImeCandidateState, WindowsSdlSnapshotCannotReplaceNativePage)
{
	CQmImeCandidateState State;
	const auto Page = NativePage("原生候选");
	State.UpdateFromNative(Page, true);
	const char *apSdlCandidates[] = {"1 过期候选"};
	State.UpdateFromSdl(apSdlCandidates, 1, 0, true);
	EXPECT_EQ(State.Candidates().m_vCandidates, Page.m_vCandidates);
	EXPECT_EQ(State.Candidates().m_PageStart, 5);
	EXPECT_EQ(State.Candidates().m_PageSize, 5);
	EXPECT_EQ(State.Candidates().m_TotalCount, 12);
}

TEST(QmImeCandidateState, EmptyWindowsSdlSnapshotCannotClearNativePage)
{
	CQmImeCandidateState State;
	const auto Page = NativePage("有效候选");
	State.UpdateFromNative(Page, true);
	State.UpdateFromSdl(nullptr, 0, -1, true);
	EXPECT_EQ(State.Candidates().m_vCandidates, Page.m_vCandidates);
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 0);
}

TEST(QmImeCandidateState, WindowsSdlSnapshotCannotOpenCandidatesWithoutNativeNotify)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 候选"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
}

TEST(QmImeCandidateState, FailedNativeChangeKeepsPreviousPage)
{
	CQmImeCandidateState State;
	const auto Page = NativePage("候选");
	State.UpdateFromNative(Page, true);
	State.UpdateFromNative(std::nullopt, false);
	EXPECT_EQ(State.Candidates().m_vCandidates, Page.m_vCandidates);
	EXPECT_EQ(State.Candidates().m_PageStart, 5);
}

TEST(QmImeCandidateState, FailedNativeOpenClearsPreviousPage)
{
	CQmImeCandidateState State;
	State.UpdateFromNative(NativePage("旧词"), true);
	State.UpdateFromNative(std::nullopt, true);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
	EXPECT_EQ(State.Candidates().m_PageStart, 0);
	EXPECT_EQ(State.Candidates().m_PageSize, 0);
	EXPECT_EQ(State.Candidates().m_TotalCount, 0);
}

TEST(QmImeCandidateState, CommitRejectsLateNativeAndSdlCandidates)
{
	CQmImeCandidateState State;
	State.UpdateFromNative(NativePage("旧词"), true);
	State.EndComposition();
	State.UpdateFromNative(NativePage("迟到通知"), false);
	const char *apCandidates[] = {"1 迟到快照"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_FALSE(State.AcceptsSdlCandidates());
}

TEST(QmImeCandidateState, NewNativeOpenResumesAfterCommit)
{
	CQmImeCandidateState State;
	State.EndComposition();
	const auto Page = NativePage("新词");
	State.UpdateFromNative(Page, true);
	EXPECT_EQ(State.Candidates().m_vCandidates, Page.m_vCandidates);
	EXPECT_TRUE(State.AcceptsSdlCandidates());
}

TEST(QmImeCandidateState, NewCompositionAllowsNativeChangeWithoutOpen)
{
	CQmImeCandidateState State;
	for(const char *pText : {"第一词", "第二词", "第三词"})
	{
		SCOPED_TRACE(pText);
		State.EndComposition();
		State.ResumeComposition();
		State.UpdateFromNative(NativePage(pText), false);
		EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{pText}));
	}
}

TEST(QmImeCandidateState, ClearingCandidatesDoesNotResumeCommittedComposition)
{
	CQmImeCandidateState State;
	State.EndComposition();
	State.Clear();
	State.UpdateFromNative(NativePage("迟到通知"), false);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
}

TEST(QmImeCandidateState, NonWindowsSdlCandidatesPreserveTextAndSelection)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 中文", "2 候选", nullptr};
	State.UpdateFromSdl(apCandidates, 3, 1, false);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"1 中文", "2 候选", ""}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 1);
	EXPECT_EQ(State.Candidates().m_PageSize, 3);
}

TEST(QmImeCandidateState, NonWindowsSdlSnapshotReplacesPreviousPage)
{
	CQmImeCandidateState State;
	const char *apFirst[] = {"第一词", "第二词"};
	const char *apNext[] = {"下一词"};
	State.UpdateFromSdl(apFirst, 2, 1, false);
	State.UpdateFromSdl(apNext, 1, 0, false);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"下一词"}));
	EXPECT_EQ(State.Candidates().m_PageSize, 1);
}

TEST(QmImeCandidateState, InvalidNonWindowsSdlSelectionDoesNotSelectCandidate)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"中文", "候选"};
	for(int Selected : {-1, 2, 99})
	{
		SCOPED_TRACE(Selected);
		State.UpdateFromSdl(apCandidates, 2, Selected, false);
		EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
	}
}

TEST(QmImeCandidateState, NonWindowsEmptySnapshotClearsAndReopens)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"候选"};
	State.UpdateFromSdl(apCandidates, 1, 0, false);
	State.UpdateFromSdl(nullptr, 0, -1, false);
	ASSERT_TRUE(State.Candidates().m_vCandidates.empty());
	State.UpdateFromSdl(apCandidates, 1, 0, false);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"候选"}));
}
