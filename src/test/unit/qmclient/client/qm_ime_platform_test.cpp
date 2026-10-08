// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/qm_ime_policy.h>

#include <game/client/qm_ime_manager.h>

#include <gtest/gtest.h>

#include <string>

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
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(false), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(true), EAction::START);
	EXPECT_EQ(Session.UpdateFocus(true), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(false), EAction::STOP);
	EXPECT_EQ(Session.UpdateFocus(false), EAction::NONE);
}

TEST(QmImeTextInputSession, EditorOwnsInputUntilClientReturns)
{
	CQmImeTextInputSession Session;
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(true), EAction::START);
	EXPECT_TRUE(Session.SetClientOwnership(false));
	EXPECT_FALSE(Session.ClientOwnsInput());
	EXPECT_EQ(Session.UpdateFocus(true), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(false), EAction::NONE);

	EXPECT_TRUE(Session.SetClientOwnership(true));
	EXPECT_EQ(Session.UpdateFocus(false), EAction::NONE);
	EXPECT_EQ(Session.UpdateFocus(true), EAction::START);
}

TEST(QmImeTextInputSession, RepeatedOwnershipDoesNotRestartComposition)
{
	CQmImeTextInputSession Session;
	using EAction = CQmImeTextInputSession::EAction;
	EXPECT_EQ(Session.UpdateFocus(true), EAction::START);
	EXPECT_FALSE(Session.SetClientOwnership(true));
	EXPECT_EQ(Session.UpdateFocus(true), EAction::NONE);
	Session.ResetFocus();
	EXPECT_EQ(Session.UpdateFocus(true), EAction::START);
	EXPECT_TRUE(Session.SetClientOwnership(false));
	Session.ResetFocus();
	EXPECT_FALSE(Session.SetClientOwnership(false));
	EXPECT_EQ(Session.UpdateFocus(true), EAction::NONE);
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
