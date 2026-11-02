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

TEST(QmImeCandidateState, FirstSdlSnapshotDoesNotRequireNativeOpenNotify)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 的", "2 地"};

	ASSERT_TRUE(State.AcceptsSdlCandidates());
	State.UpdateFromSdl(apCandidates, 2, 0, true);

	const auto &Candidates = State.Candidates();
	EXPECT_EQ(Candidates.m_vCandidates, (std::vector<std::string>{"的", "地"}));
	EXPECT_EQ(Candidates.m_SelectedIndex, 0);
	EXPECT_EQ(Candidates.m_PageStart, 0);
	EXPECT_EQ(Candidates.m_PageSize, 2);
	EXPECT_EQ(Candidates.m_TotalCount, 2);
	EXPECT_TRUE(QmImePopupShouldBeVisible(Candidates.m_vCandidates.size()));
}

TEST(QmImeCandidateState, ReplacingSnapshotsCanReturnToSingleCandidate)
{
	CQmImeCandidateState State;
	const char *apFirstCandidates[] = {"1 的"};
	const char *apLongerCandidates[] = {"1 大", "2 打", "3 答"};

	State.UpdateFromSdl(apFirstCandidates, 1, 0, true);
	ASSERT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"的"}));
	State.UpdateFromSdl(apLongerCandidates, 3, 1, true);
	ASSERT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"大", "打", "答"}));
	State.UpdateFromSdl(apFirstCandidates, 1, 0, true);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"的"}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 0);
	EXPECT_EQ(State.Candidates().m_PageSize, 1);
	EXPECT_EQ(State.Candidates().m_TotalCount, 1);
	EXPECT_TRUE(QmImePopupShouldBeVisible(State.Candidates().m_vCandidates.size()));
}

TEST(QmImeCandidateState, SdlRefreshPrefersNativePageAndMetadata)
{
	CQmImeCandidateState State;
	const char *apSdlCandidates[] = {"1 旧词"};
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"第一页", "第二页"};
	NativeCandidates.m_SelectedIndex = 1;
	NativeCandidates.m_PageStart = 10;
	NativeCandidates.m_PageSize = 5;
	NativeCandidates.m_TotalCount = 23;

	State.UpdateFromSdl(apSdlCandidates, 1, 0, true, NativeCandidates);

	const auto &Candidates = State.Candidates();
	EXPECT_EQ(Candidates.m_vCandidates, NativeCandidates.m_vCandidates);
	EXPECT_EQ(Candidates.m_SelectedIndex, 1);
	EXPECT_EQ(Candidates.m_PageStart, 10);
	EXPECT_EQ(Candidates.m_PageSize, 5);
	EXPECT_EQ(Candidates.m_TotalCount, 23);
}

TEST(QmImeCandidateState, MissingNativePageFallsBackToSdlSnapshot)
{
	CQmImeCandidateState State;
	SQmImeCandidateList PreviousCandidates;
	PreviousCandidates.m_vCandidates = {"旧词"};
	PreviousCandidates.m_PageStart = 5;
	PreviousCandidates.m_PageSize = 5;
	PreviousCandidates.m_TotalCount = 12;
	State.UpdateFromNative(PreviousCandidates, true);
	const char *apSdlCandidates[] = {"1 新词", "2 输入"};

	State.UpdateFromSdl(apSdlCandidates, 2, 1, true, std::nullopt);

	const auto &Candidates = State.Candidates();
	EXPECT_EQ(Candidates.m_vCandidates, (std::vector<std::string>{"新词", "输入"}));
	EXPECT_EQ(Candidates.m_SelectedIndex, 1);
	EXPECT_EQ(Candidates.m_PageStart, 0);
	EXPECT_EQ(Candidates.m_PageSize, 2);
	EXPECT_EQ(Candidates.m_TotalCount, 2);
}

TEST(QmImeCandidateState, EmptyNativePageFallsBackToSdlSnapshot)
{
	CQmImeCandidateState State;
	const char *apSdlCandidates[] = {"1 新词"};

	State.UpdateFromSdl(apSdlCandidates, 1, 0, true, SQmImeCandidateList{});

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"新词"}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 0);
}

TEST(QmImeCandidateState, WindowsSdlCandidatesRemoveSingleDigitSpacePrefix)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 中文", "9 候选", "0 末项"};

	State.UpdateFromSdl(apCandidates, 3, 2, true);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"中文", "候选", "末项"}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 2);
}

TEST(QmImeCandidateState, WindowsSdlCandidatesPreserveContentWithoutNumberPrefix)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"中文", "1. 中文", "10 中文", " 1 中文", "1\t中文", "１ 中文"};

	State.UpdateFromSdl(apCandidates, 6, 0, true);

	EXPECT_EQ(State.Candidates().m_vCandidates,
		(std::vector<std::string>{"中文", "1. 中文", "10 中文", " 1 中文", "1\t中文", "１ 中文"}));
}

TEST(QmImeCandidateState, NonWindowsSdlCandidatesPreserveNumberedContent)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 中文", "2 候选"};

	State.UpdateFromSdl(apCandidates, 2, 0, false);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"1 中文", "2 候选"}));
}

TEST(QmImeCandidateState, InvalidSdlSelectionDoesNotSelectCandidate)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 中文", "2 候选"};
	for(int SelectedIndex : {-1, 2, 99})
	{
		SCOPED_TRACE(SelectedIndex);
		State.UpdateFromSdl(apCandidates, 2, SelectedIndex, true);
		EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
		EXPECT_EQ(State.Candidates().m_vCandidates.size(), 2u);
	}
}

TEST(QmImeCandidateState, EndingCompositionRejectsLateSdlAndNativeCandidates)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 旧词"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	State.EndComposition();
	ASSERT_FALSE(State.AcceptsSdlCandidates());
	ASSERT_TRUE(State.Candidates().m_vCandidates.empty());
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"迟到候选"};

	State.UpdateFromSdl(apCandidates, 1, 0, true, NativeCandidates);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	State.UpdateFromNative(NativeCandidates, false);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_FALSE(State.AcceptsSdlCandidates());
}

TEST(QmImeCandidateState, ResumingCompositionAcceptsSdlWithoutNativeOpenNotify)
{
	CQmImeCandidateState State;
	State.EndComposition();
	State.ResumeComposition();
	ASSERT_TRUE(State.AcceptsSdlCandidates());
	const char *apCandidates[] = {"1 新词"};

	State.UpdateFromSdl(apCandidates, 1, 0, true);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"新词"}));
}

TEST(QmImeCandidateState, ActiveNativeCompositionResumesSdlCandidatesAfterCommit)
{
	CQmImeCandidateState State;
	const char *apSdlCandidates[] = {"1 上次候选"};
	State.UpdateFromSdl(apSdlCandidates, 1, 0, true);
	State.EndComposition();
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"当前候选", "下一候选"};
	NativeCandidates.m_SelectedIndex = 1;
	NativeCandidates.m_PageStart = 5;
	NativeCandidates.m_PageSize = 5;
	NativeCandidates.m_TotalCount = 12;

	// 未收到新的打开通知或非空 TEXTEDITING，仅由 IMM 确认当前仍在组合输入。
	State.UpdateFromSdl(apSdlCandidates, 1, 0, true, NativeCandidates, true);

	EXPECT_TRUE(State.AcceptsSdlCandidates());
	EXPECT_EQ(State.Candidates().m_vCandidates, NativeCandidates.m_vCandidates);
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 1);
	EXPECT_EQ(State.Candidates().m_PageStart, 5);
	EXPECT_TRUE(QmImePopupShouldBeVisible(State.Candidates().m_vCandidates.size()));
}

TEST(QmImeCandidateState, ActiveNativeCompositionResumesChangeWithoutOpenNotify)
{
	CQmImeCandidateState State;
	State.EndComposition();
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"新的组合"};
	NativeCandidates.m_SelectedIndex = 0;

	State.UpdateFromNative(NativeCandidates, false, true);

	EXPECT_TRUE(State.AcceptsSdlCandidates());
	EXPECT_EQ(State.Candidates().m_vCandidates, NativeCandidates.m_vCandidates);
}

TEST(QmImeCandidateState, ActiveCompositionAllowsSdlFallbackWhenNativeListIsUnavailable)
{
	CQmImeCandidateState State;
	State.EndComposition();
	State.UpdateFromNative(std::nullopt, false, true);
	const char *apCandidates[] = {"1 当前候选"};

	State.UpdateFromSdl(apCandidates, 1, 0, true);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"当前候选"}));
}

TEST(QmImeCandidateState, ConsecutiveCommitsRecoverWithoutRestartingTextInput)
{
	CQmImeCandidateState State;
	for(const char *pCandidate : {"第一词", "第二词", "第三词"})
	{
		SCOPED_TRACE(pCandidate);
		State.EndComposition();
		const char *apLateCandidates[] = {"1 上次候选"};
		State.UpdateFromSdl(apLateCandidates, 1, 0, true);
		ASSERT_TRUE(State.Candidates().m_vCandidates.empty());
		const char *apCurrentCandidates[] = {pCandidate};

		State.UpdateFromSdl(apCurrentCandidates, 1, 0, true, std::nullopt, true);

		EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{pCandidate}));
	}
}

TEST(QmImeCandidateState, EmptySdlSnapshotCannotResumeEndedComposition)
{
	CQmImeCandidateState State;
	State.EndComposition();
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"残留候选"};

	State.UpdateFromSdl(nullptr, 0, -1, true, NativeCandidates, true);

	EXPECT_FALSE(State.AcceptsSdlCandidates());
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
}

TEST(QmImeCandidateState, NativeOpenAcceptsCandidatesAfterCompositionEnded)
{
	CQmImeCandidateState State;
	State.EndComposition();
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"新词"};
	NativeCandidates.m_SelectedIndex = 0;

	State.UpdateFromNative(NativeCandidates, true);

	EXPECT_TRUE(State.AcceptsSdlCandidates());
	EXPECT_EQ(State.Candidates().m_vCandidates, NativeCandidates.m_vCandidates);
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 0);
}

TEST(QmImeCandidateState, FailedNativeChangeKeepsPreviousPage)
{
	CQmImeCandidateState State;
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"候选"};
	NativeCandidates.m_SelectedIndex = 0;
	NativeCandidates.m_PageStart = 5;
	NativeCandidates.m_PageSize = 5;
	NativeCandidates.m_TotalCount = 13;
	State.UpdateFromNative(NativeCandidates, true);

	State.UpdateFromNative(std::nullopt, false);

	const auto &Candidates = State.Candidates();
	EXPECT_EQ(Candidates.m_vCandidates, NativeCandidates.m_vCandidates);
	EXPECT_EQ(Candidates.m_SelectedIndex, 0);
	EXPECT_EQ(Candidates.m_PageStart, 5);
	EXPECT_EQ(Candidates.m_PageSize, 5);
	EXPECT_EQ(Candidates.m_TotalCount, 13);
}

TEST(QmImeCandidateState, FailedNativeOpenClearsPreviousPageAndAllowsSdlFallback)
{
	CQmImeCandidateState State;
	const char *apOldCandidates[] = {"1 旧词"};
	State.UpdateFromSdl(apOldCandidates, 1, 0, true);

	State.UpdateFromNative(std::nullopt, true);

	ASSERT_TRUE(State.Candidates().m_vCandidates.empty());
	ASSERT_TRUE(State.AcceptsSdlCandidates());
	const char *apNewCandidates[] = {"1 新词"};
	State.UpdateFromSdl(apNewCandidates, 1, 0, true);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"新词"}));
}

TEST(QmImeCandidateState, EmptySdlSnapshotClearsCandidatesAndAllowsReopening)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 候选"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	SQmImeCandidateList NativeCandidates;
	NativeCandidates.m_vCandidates = {"旧候选"};

	State.UpdateFromSdl(nullptr, 0, -1, true, NativeCandidates);

	ASSERT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
	EXPECT_EQ(State.Candidates().m_PageStart, 0);
	EXPECT_EQ(State.Candidates().m_PageSize, 0);
	EXPECT_EQ(State.Candidates().m_TotalCount, 0);
	ASSERT_TRUE(State.AcceptsSdlCandidates());
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"候选"}));
}

TEST(QmImeCandidateState, ClearingPageDoesNotResumeEndedComposition)
{
	CQmImeCandidateState State;
	State.EndComposition();

	State.Clear();
	State.UpdateFromSdl(nullptr, 0, -1, true);

	EXPECT_FALSE(State.AcceptsSdlCandidates());
	const char *apCandidates[] = {"1 旧词"};
	State.UpdateFromSdl(apCandidates, 1, 0, true);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
}
