// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/qmclient/chat_command_preview.h>
#include <game/client/components/qmclient/chat_input_layout.h>
#include <game/client/components/qmclient/chat_translate_button.h>
#include <game/client/components/qmclient/red_packet_auto_claim.h>
#include <game/client/components/tclient/fast_practice.h>
#include <game/client/components/tclient/warlist.h>

#include <gtest/gtest.h>

#include <iterator>
#include <string>

namespace
{
	int64_t TestTicks(float Seconds) { return (int64_t)(Seconds * time_freq()); }
}

TEST(QmChatMessageMerge, EligibilityUsesExactTextSlidingWindowAndMatchingChannelsOnly)
{
	const int64_t Start = TestTicks(10.0f);

	EXPECT_TRUE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Start + TestTicks(2.0f)));
	EXPECT_TRUE(CChat::CanMergePlayerMessages(2, 1, "same", Start, 7, 1, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 1, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 1, "same", Start, 7, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Start + TestTicks(2.01f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "Same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(-1, 0, "same", Start, 7, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, -1, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, TEAM_WHISPER_RECV, "same", Start, 7, 0, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, TEAM_WHISPER_SEND, "same", Start + TestTicks(0.1f)));
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Start - 1));
}

TEST(QmChatInteractions, ClampBacklogLine)
{
	EXPECT_EQ(CChat::ClampBacklogLine(-3, 10, 4), 0);
	EXPECT_EQ(CChat::ClampBacklogLine(0, 10, 4), 0);
	EXPECT_EQ(CChat::ClampBacklogLine(6, 10, 4), 6);
	EXPECT_EQ(CChat::ClampBacklogLine(7, 10, 4), 6);
	EXPECT_EQ(CChat::ClampBacklogLine(20, 10, 4), 6);
}

TEST(QmChatInteractions, HudTransformInversePreservesChatOrigin)
{
	const CUIRect DefaultRect = {0.0f, 50.0f, 400.0f, 250.0f};
	const CUIRect TargetRect = {100.0f, 200.0f, 800.0f, 500.0f};
	const vec2 LogicalPoint = {73.0f, 91.0f};
	const float Scale = TargetRect.w / DefaultRect.w;
	const vec2 TransformedPoint = {
		TargetRect.x + (LogicalPoint.x - DefaultRect.x) * Scale,
		TargetRect.y + (LogicalPoint.y - DefaultRect.y) * Scale};

	const vec2 Result = CChat::InverseHudTransformPoint(TransformedPoint, DefaultRect, TargetRect);
	EXPECT_NEAR(Result.x, LogicalPoint.x, 0.001f);
	EXPECT_NEAR(Result.y, LogicalPoint.y, 0.001f);
}

TEST(QmChatInteractions, ChatLineHitStopsAtContentWidth)
{
	const CUIRect ContentRect = {5.0f, 90.0f, 120.0f, 15.0f};

	EXPECT_TRUE(CChat::IsChatLineHit(ContentRect, vec2(100.0f, 95.0f)));
	EXPECT_FALSE(CChat::IsChatLineHit(ContentRect, vec2(130.0f, 95.0f)));
	EXPECT_FALSE(CChat::IsChatLineHit(ContentRect, vec2(100.0f, 106.0f)));
}

TEST(QmChatInteractions, ScrollbarValueToBacklogLine)
{
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(1.0f, 12), 0);
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(0.0f, 12), 12);
	EXPECT_EQ(CChat::ScrollbarValueToBacklogLine(0.5f, 12), 6);
}

TEST(QmChatInteractions, BacklogLineToScrollbarValue)
{
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(0, 12), 1.0f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(12, 12), 0.0f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(6, 12), 0.5f);
	EXPECT_FLOAT_EQ(CChat::BacklogLineToScrollbarValue(20, 12), 0.0f);
}

TEST(QmChatInteractions, ClickDragThreshold)
{
	EXPECT_TRUE(CChat::IsCopyClickDrag(vec2(10.0f, 10.0f), vec2(12.0f, 12.0f)));
	EXPECT_FALSE(CChat::IsCopyClickDrag(vec2(10.0f, 10.0f), vec2(30.0f, 10.0f)));
}

TEST(QmChatInteractions, AppendsBlockWordsWithSeparator)
{
	char aList[32] = "";

	EXPECT_TRUE(CChat::AppendBlockWordToList(aList, sizeof(aList), "spam"));
	EXPECT_STREQ(aList, "spam");

	EXPECT_TRUE(CChat::AppendBlockWordToList(aList, sizeof(aList), "eggs"));
	EXPECT_STREQ(aList, "spam;eggs");
}

TEST(QmChatInteractions, DoesNotAppendEmptyOrFullBlockWords)
{
	char aList[8] = "filled";

	EXPECT_FALSE(CChat::AppendBlockWordToList(aList, sizeof(aList), ""));
	EXPECT_STREQ(aList, "filled");

	EXPECT_FALSE(CChat::AppendBlockWordToList(aList, sizeof(aList), "x"));
	EXPECT_STREQ(aList, "filled");
}

TEST(QmChatBlockWords, HideActionOnlySuppressesMatchedRemotePlayerMessages)
{
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::REPLACE, true, 5, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, false, 5, false, 0));

	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, 0));
	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, 1));
	EXPECT_TRUE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, TEAM_WHISPER_RECV));
}

TEST(QmChatBlockWords, HideActionKeepsLocalAndNonPlayerMessagesVisible)
{
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, true, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, -1, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, -2, false, 0));
	EXPECT_FALSE(CChat::ShouldHideBlockWordsMessage(CChat::EBlockWordsAction::HIDE_MESSAGE, true, 5, false, TEAM_WHISPER_SEND));
}

TEST(QmChatInteractions, BuildsEscapedWhisperCommand)
{
	char aCommand[128];

	EXPECT_TRUE(CChat::BuildWhisperCommand(aCommand, sizeof(aCommand), "Name \"A\"", "hello"));
	EXPECT_STREQ(aCommand, "/w \"Name \\\"A\\\"\" hello");
}

TEST(QmChatInteractions, BuildsEscapedSpectateCommand)
{
	char aCommand[128];

	EXPECT_TRUE(CChat::BuildSpectateCommand(aCommand, sizeof(aCommand), "Name \"A\""));
	EXPECT_STREQ(aCommand, "say /spec \"Name \\\"A\\\"\"");
}

TEST(QmChatInteractions, ReusesKnownServerMessageClassWithoutReanalysis)
{
	const auto Class = CChat::ResolveLineServerMessageClass(-1, "DDraceNetwork Version: 18.9", QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::Prompt);
}

TEST(QmChatInteractions, FallsBackToLegacyServerMessageClassificationWhenUnknown)
{
	const auto Class = CChat::ResolveLineServerMessageClass(-1, "DDraceNetwork Version: 18.9");
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::BasicInfo);
}

TEST(QmChatInteractions, IgnoresKnownServerClassForNonServerMessages)
{
	const auto Class = CChat::ResolveLineServerMessageClass(3, "DDraceNetwork Version: 18.9", QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Class, QmHudNotifications::EServerMessageClass::None);
}

TEST(QmChatInteractions, ManualVisibleTranslationCandidatesAreOnlyUntranslatedRemotePlayerLines)
{
	int aLocalIds[] = {2, 7};
	EXPECT_TRUE(CChat::IsManualVisibleTranslateCandidate(3, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(-1, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(-2, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(2, true, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(3, false, false, aLocalIds, std::size(aLocalIds)));
	EXPECT_FALSE(CChat::IsManualVisibleTranslateCandidate(3, true, true, aLocalIds, std::size(aLocalIds)));
}

TEST(QmChatTranslateButton, LeftClickAlwaysOpensSettingsAndNeverTogglesAutoTranslation)
{
	CQmChatTranslateButton Button;
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, true, true), CQmChatTranslateButton::EAction::CONSUME);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::OPEN_SETTINGS);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::NONE);
}

TEST(QmChatTranslateButton, RightPressOnlyTogglesOnce)
{
	CQmChatTranslateButton Button;
	EXPECT_EQ(Button.Update(KEY_MOUSE_2, IInput::FLAG_PRESS, true, true), CQmChatTranslateButton::EAction::TOGGLE_AUTO);
	EXPECT_EQ(Button.Update(KEY_MOUSE_2, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::NONE);
}

TEST(QmChatTranslateButton, DragOutsideConsumesReleaseWithoutOpeningSettings)
{
	CQmChatTranslateButton Button;
	Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, true, true);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, false, true), CQmChatTranslateButton::EAction::CONSUME);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::NONE);
}

TEST(QmChatTranslateButton, PressOutsideThenReleaseInsideDoesNotActivate)
{
	CQmChatTranslateButton Button;
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, false, true), CQmChatTranslateButton::EAction::NONE);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::NONE);
}

TEST(QmChatTranslateButton, PopupOrChatClosureCancelsPendingClickAndCanReopen)
{
	CQmChatTranslateButton Button;
	Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, true, true);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, false), CQmChatTranslateButton::EAction::NONE);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::NONE);
	Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, true, true);
	EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, true, true), CQmChatTranslateButton::EAction::OPEN_SETTINGS);
}

TEST(QmChatInputLayout, TextStartLeavesRoomForButtonOnItsLeft)
{
	const float FontSize = 12.0f;
	const float PrefixWidth = 40.0f;
	const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, PrefixWidth);

	EXPECT_FLOAT_EQ(Layout.m_ButtonX, 5.0f);
	EXPECT_GT(Layout.m_TextStartX, Layout.m_ButtonX + Layout.m_ButtonW);
	EXPECT_FLOAT_EQ(Layout.m_TextStartX, Layout.m_ButtonX + Layout.m_ButtonW + QM_CHAT_TRANSLATE_BUTTON_GAP);
}

TEST(QmChatInputLayout, CursorLineWidthKeepsWrappedTextAwayFromButton)
{
	const float FontSize = 12.0f;
	const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, 40.0f);
	const float TextRight = Layout.m_TextStartX + Layout.m_CursorLineWidth;

	// 正文右边界仍受整行右边界约束，按钮区只从左侧让位。
	EXPECT_LE(TextRight, 5.0f + 400.0f + 0.001f);
	EXPECT_GT(Layout.m_MessageMaxWidth, 0.0f);
	EXPECT_LE(Layout.m_MessageMaxWidth + 40.0f, Layout.m_CursorLineWidth + 0.001f);
}

TEST(QmChatInputLayout, MessageWidthNeverCollapsesBelowUsableMinimum)
{
	const SQmChatInputLayout Layout = QmChatResolveInputLayout(0.0f, 0.0f, 10.0f, 40.0f, 200.0f);

	EXPECT_FLOAT_EQ(Layout.m_MessageMaxWidth, 1.0f);
	EXPECT_GT(Layout.m_CursorLineWidth, 0.0f);
}

TEST(QmChatInputLayout, ButtonStaysCenteredOnFirstInputLine)
{
	const float FontSize = 12.0f;
	const float Y = 100.0f;
	const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, Y, 400.0f, FontSize, 40.0f);

	EXPECT_FLOAT_EQ(Layout.m_ButtonH, QmChatTranslateButtonHeight(FontSize));
	EXPECT_FLOAT_EQ(Layout.m_ButtonY, Y + (FontSize - Layout.m_ButtonH) * 0.5f);
	EXPECT_GE(Layout.m_ButtonH, FontSize);
}

TEST(QmChatInputLayout, WidePrefixStillLeavesBodyRoomToTheRightOfTheButton)
{
	const float FontSize = 20.0f;
	const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, 120.0f);

	// 前缀更宽时只有正文变窄，按钮与正文起点不受影响。
	EXPECT_FLOAT_EQ(Layout.m_TextStartX, 5.0f + QmChatTranslateButtonSize(FontSize) + QM_CHAT_TRANSLATE_BUTTON_GAP);
	EXPECT_FLOAT_EQ(Layout.m_MessageMaxWidth, 400.0f - QmChatTranslateButtonSize(FontSize) - QM_CHAT_TRANSLATE_BUTTON_GAP - 120.0f);
}

TEST(QmChatInputLayout, CommonSizesKeepButtonPrefixAndBodyInsideInputRow)
{
	for(float FontSize : {8.0f, 12.0f, 20.0f, 32.0f})
		for(float LineWidth : {190.0f, 400.0f, 800.0f})
			for(float PrefixWidth : {20.0f, 60.0f, 100.0f})
			{
				SCOPED_TRACE(::testing::Message() << FontSize << "/" << LineWidth << "/" << PrefixWidth);
				const auto Layout = QmChatResolveInputLayout(5.0f, 100.0f, LineWidth, FontSize, PrefixWidth);
				EXPECT_GT(Layout.m_TextStartX, Layout.m_ButtonX + Layout.m_ButtonW);
				EXPECT_GT(Layout.m_MessageMaxWidth, 1.0f);
				EXPECT_LE(Layout.m_TextStartX + PrefixWidth + Layout.m_MessageMaxWidth, 5.0f + LineWidth + 0.001f);
				EXPECT_FLOAT_EQ(Layout.m_ButtonY + Layout.m_ButtonH * 0.5f, 100.0f + FontSize * 0.5f);
			}
}

TEST(QmChatInputLayout, DefaultMappingProjectsInputClipToPixels)
{
	const auto Clip = QmChatInputPixelClip({40.0f, 250.0f, 200.0f, 20.0f}, {0.0f, 0.0f, 400.0f, 300.0f}, 1600, 1200);
	EXPECT_EQ(Clip.m_X, 160);
	EXPECT_EQ(Clip.m_Y, 1000);
	EXPECT_EQ(Clip.m_W, 800);
	EXPECT_EQ(Clip.m_H, 80);
}

TEST(QmChatInputLayout, MovedHudKeepsInputClipAlignedWithText)
{
	const auto Clip = QmChatInputPixelClip({40.0f, 250.0f, 200.0f, 20.0f}, {-20.0f, -10.0f, 400.0f, 300.0f}, 1600, 1200);
	EXPECT_EQ(Clip.m_X, 240);
	EXPECT_EQ(Clip.m_Y, 1040);
	EXPECT_EQ(Clip.m_W, 800);
	EXPECT_EQ(Clip.m_H, 80);
}

TEST(QmChatInputLayout, ScaledHudKeepsInputClipAlignedWithText)
{
	const auto Clip = QmChatInputPixelClip({40.0f, 120.0f, 100.0f, 20.0f}, {10.0f, 100.0f, 200.0f, 150.0f}, 1600, 1200);
	EXPECT_EQ(Clip.m_X, 240);
	EXPECT_EQ(Clip.m_Y, 160);
	EXPECT_EQ(Clip.m_W, 800);
	EXPECT_EQ(Clip.m_H, 160);
}

TEST(QmChatInputLayout, FractionalClipEdgesRetainBoundaryPixels)
{
	const auto Clip = QmChatInputPixelClip({10.2f, 20.3f, 30.4f, 40.2f}, {0.0f, 0.0f, 400.0f, 300.0f}, 400, 300);
	EXPECT_EQ(Clip.m_X, 10);
	EXPECT_EQ(Clip.m_Y, 20);
	EXPECT_EQ(Clip.m_W, 31);
	EXPECT_EQ(Clip.m_H, 41);
}

TEST(QmChatInputLayout, ClipOutsideViewportIsLimitedToVisiblePixels)
{
	const auto Clip = QmChatInputPixelClip({-2.0f, -3.0f, 410.0f, 310.0f}, {0.0f, 0.0f, 400.0f, 300.0f}, 1600, 1200);
	EXPECT_EQ(Clip.m_X, 0);
	EXPECT_EQ(Clip.m_Y, 0);
	EXPECT_EQ(Clip.m_W, 1600);
	EXPECT_EQ(Clip.m_H, 1200);
}
