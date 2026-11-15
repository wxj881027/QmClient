// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/QmUi/QmChatLineMenuLayout.h>
#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/qmclient/chat_command_preview.h>
#include <game/client/components/qmclient/chat_input_layout.h>
#include <game/client/components/qmclient/chat_scrollbar.h>
#include <game/client/components/qmclient/chat_translate_button.h>
#include <game/client/components/qmclient/chat_whisper_targets.h>
#include <game/client/components/qmclient/red_packet_auto_claim.h>
#include <game/client/components/tclient/fast_practice.h>
#include <game/client/components/tclient/warlist.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
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

TEST(QmChatMessageMerge, HighlightedMessagesAreNotMerged)
{
	const int64_t Start = TestTicks(10.0f);
	const int64_t Now = Start + TestTicks(0.1f);
	EXPECT_FALSE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Now, true));
	EXPECT_TRUE(CChat::CanMergePlayerMessages(2, 0, "same", Start, 7, 0, "same", Now, false));
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

TEST(QmChatInputLayout, TranslateButtonAndPrefixRowShareOneHeight)
{
	for(const float FontSize : {8.0f, 12.0f, 32.0f})
	{
		SCOPED_TRACE(FontSize);
		const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, 40.0f);
		// 圆形图标按钮：宽高相等（绘制层按胶囊档裁成正圆），直径比输入行行高小一档，
		// 所以文字行不会被按钮撑高；前缀块仍取行高，两者中心对齐。
		EXPECT_FLOAT_EQ(Layout.m_ButtonW, Layout.m_ButtonH);
		EXPECT_FLOAT_EQ(Layout.m_ButtonH, QmChatTranslateButtonHeight(FontSize));
		EXPECT_LT(Layout.m_ButtonH, QmChatInputRowHeight(FontSize));
		EXPECT_NEAR(Layout.m_ButtonH, QmChatInputRowHeight(FontSize) * ui_token::chat::input::BUTTON_ROW_RATIO, 0.01f);
		// 行高不再等于字号：按钮比字形高出一档，居中后上下各露半档，读作「一行控件」。
		EXPECT_GT(Layout.m_ButtonH, FontSize * 0.85f);
		// 基线对齐的实现方式：按钮中心与首行文字中心重合，桌面文字与手机前缀块同时对齐。
		EXPECT_FLOAT_EQ(Layout.m_ButtonY + Layout.m_ButtonH * 0.5f, 100.0f + FontSize * 0.5f);
	}
}

TEST(QmChatInputLayout, RowHeightNeverCollapsesBelowTheMinimumTapSize)
{
	// 行高有下限：字号为零或很小时按钮不再跟着缩，否则圆形按钮会小到图标糊成一团，
	// 触摸也点不准。
	EXPECT_FLOAT_EQ(QmChatInputRowHeight(0.0f), QmChatInputRowHeight(1.0f));
	EXPECT_GE(QmChatInputRowHeight(0.0f), QmChatInputRowHeight(4.0f));
	EXPECT_GT(QmChatInputRowHeight(4.0f), 4.0f);
	// 负字号（调用点可能传入未初始化的字号）不能算出非正行高。
	EXPECT_GT(QmChatInputRowHeight(-10.0f), 0.0f);
}

TEST(QmChatInputLayout, TranslateButtonBorderStaysAThinOutlineAtEveryRowHeight)
{
	for(const float FontSize : {4.0f, 8.0f, 32.0f, 64.0f})
	{
		SCOPED_TRACE(FontSize);
		const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, 40.0f);
		const float Border = QmChatTranslateButtonBorderWidth(Layout.m_ButtonW);
		// 描边要看得见，但只能占直径的零头。旧实现固定 1 单位画在 8 单位小圆上，
		// 等于八分之一直径，被玩家读成「粗框」。
		EXPECT_GT(Border, 0.0f);
		EXPECT_LT(Border, Layout.m_ButtonW * 0.1f);
	}
}

TEST(QmChatInputLayout, TranslateIconFillsMoreThanHalfTheInnerCircle)
{
	for(const float FontSize : {4.0f, 8.0f, 16.0f, 32.0f})
	{
		SCOPED_TRACE(FontSize);
		const SQmChatInputLayout Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, FontSize, 40.0f);
		const float Inner = QmChatTranslateButtonInnerSize(Layout.m_ButtonW);
		const float Glyph = QmChatTranslateIconGlyphSize(Layout.m_ButtonW);
		ASSERT_GT(Inner, 0.0f);
		// 图标越大越接近内圈：墨迹必须越过内圈半径，否则又回到「粗框套小点」的观感。
		EXPECT_GT(Glyph, Inner * 0.5f);
		// 但字形不得顶出内圈，描边永远留在图标之外。
		EXPECT_LE(Glyph, Inner);
		// DoLabel_QmIcon 收的是图标盒而不是字形，盒子就是内径。
		// 图标必须留出内边距（不与圆同高），但仍明显大于内圈一半，不能缩成一个小点。
		const float IconBox = QmChatTranslateIconBoxSize(Layout.m_ButtonW);
		EXPECT_LT(IconBox, Inner);
		EXPECT_GT(IconBox, Inner * 0.5f);
		EXPECT_NEAR(IconBox, Inner * (1.0f - 2.0f * ui_token::chat::input::ICON_INSET_RATIO), 0.01f);
	}
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

TEST(QmChatInputViewport, MovedScaledBodyClipContainsBothEndsOfTheInput)
{
	const SQmChatViewport Viewport{{-100.0f, -50.0f, 400.0f, 300.0f}, vec2(1600.0f, 1200.0f)};
	const CUIRect Body{25.0f, 150.0f, 100.0f, 24.0f};
	const CUIRect Clip = Viewport.ClipPixels(Body);
	EXPECT_FLOAT_EQ(Clip.x, 500.0f);
	EXPECT_FLOAT_EQ(Clip.y, 800.0f);
	EXPECT_FLOAT_EQ(Clip.w, 400.0f);
	EXPECT_FLOAT_EQ(Clip.h, 96.0f);
	const auto Start = Viewport.ToLocal(vec2(Clip.x, Clip.y));
	const auto End = Viewport.ToLocal(vec2(Clip.x + Clip.w, Clip.y + Clip.h));
	EXPECT_FLOAT_EQ(Start.x, Body.x);
	EXPECT_FLOAT_EQ(Start.y, Body.y);
	EXPECT_FLOAT_EQ(End.x, Body.x + Body.w);
	EXPECT_FLOAT_EQ(End.y, Body.y + Body.h);
}

TEST(QmChatInputViewport, FractionalPixelsRoundOutwardsInsteadOfCroppingGlyphEdges)
{
	const SQmChatViewport Viewport{{0.5f, -0.25f, 100.0f, 50.0f}, vec2(150.0f, 75.0f)};
	const auto Clip = Viewport.ClipPixels({1.0f, 1.0f, 10.25f, 5.25f});
	EXPECT_FLOAT_EQ(Clip.x, 0.0f);
	EXPECT_FLOAT_EQ(Clip.y, 1.0f);
	EXPECT_FLOAT_EQ(Clip.w, 17.0f);
	EXPECT_FLOAT_EQ(Clip.h, 9.0f);
}

TEST(QmChatInputViewport, ClipClampsMovedInputToFramebuffer)
{
	const SQmChatViewport Viewport{{0.0f, 0.0f, 400.0f, 300.0f}, vec2(1600.0f, 1200.0f)};
	const auto Clip = Viewport.ClipPixels({-5.0f, 290.0f, 420.0f, 24.0f});
	EXPECT_FLOAT_EQ(Clip.x, 0.0f);
	EXPECT_FLOAT_EQ(Clip.y, 1160.0f);
	EXPECT_FLOAT_EQ(Clip.w, 1600.0f);
	EXPECT_FLOAT_EQ(Clip.h, 40.0f);
	const auto Outside = Viewport.ClipPixels({500.0f, 400.0f, 50.0f, 20.0f});
	EXPECT_FLOAT_EQ(Outside.w, 0.0f);
	EXPECT_FLOAT_EQ(Outside.h, 0.0f);
}

TEST(QmChatTranslateButton, MovedScaledVisualCenterOpensSettings)
{
	const auto Layout = QmChatResolveInputLayout(5.0f, 100.0f, 400.0f, 12.0f, 40.0f);
	const CUIRect ButtonRect{Layout.m_ButtonX, Layout.m_ButtonY, Layout.m_ButtonW, Layout.m_ButtonH};
	for(const auto &Map : {CUIRect{0.0f, 0.0f, 400.0f, 300.0f}, CUIRect{-50.0f, 25.0f, 200.0f, 150.0f}})
	{
		SCOPED_TRACE(::testing::Message() << Map.x << "/" << Map.y << "/" << Map.w);
		const SQmChatViewport Viewport{Map, vec2(1600.0f, 1200.0f)};
		const auto Visual = Viewport.ToPixels(ButtonRect);
		const auto Mouse = Viewport.ToLocal(vec2(Visual.x + Visual.w * 0.5f, Visual.y + Visual.h * 0.5f));
		CQmChatTranslateButton Button;
		ASSERT_TRUE(ButtonRect.Inside(Mouse));
		EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_PRESS, ButtonRect.Inside(Mouse), true), CQmChatTranslateButton::EAction::CONSUME);
		EXPECT_EQ(Button.Update(KEY_MOUSE_1, IInput::FLAG_RELEASE, ButtonRect.Inside(Mouse), true), CQmChatTranslateButton::EAction::OPEN_SETTINGS);
	}
}

TEST(QmChatInputViewport, TooltipAnchorUsesUiScreenCoordinatesAndWindowSize)
{
	const SQmChatViewport Viewport{{-100.0f, -50.0f, 400.0f, 300.0f}, vec2(1600.0f, 1200.0f)};
	const auto Anchor = Viewport.ToUi({25.0f, 100.0f, 16.0f, 16.0f}, {10.0f, 20.0f, 800.0f, 600.0f});
	EXPECT_FLOAT_EQ(Anchor.x, 260.0f);
	EXPECT_FLOAT_EQ(Anchor.y, 320.0f);
	EXPECT_FLOAT_EQ(Anchor.w, 32.0f);
	EXPECT_FLOAT_EQ(Anchor.h, 32.0f);
}

TEST(QmChatScrollbar, SideSwitchMovesRailInsideBoundsWithoutChangingSize)
{
	const CUIRect Bounds{20.0f, 50.0f, 250.0f, 200.0f};
	const auto Left = QmChatScrollbarRail(Bounds, 80.0f, 100.0f, false);
	const auto Right = QmChatScrollbarRail(Bounds, 80.0f, 100.0f, true);
	EXPECT_FLOAT_EQ(Left.x, 22.0f);
	EXPECT_FLOAT_EQ(Right.x + Right.w, 268.0f);
	// 轨道比旧实现（3 单位）略宽，左右两侧尺寸一致。
	EXPECT_GT(Left.w, 3.0f);
	EXPECT_FLOAT_EQ(Left.w, Right.w);
	EXPECT_FLOAT_EQ(Left.y, Right.y);
	EXPECT_FLOAT_EQ(Left.h, Right.h);
	// 轨道沿长度方向两端各让出一档外边距，不再顶满历史区高度。
	EXPECT_FLOAT_EQ(Left.y, 80.0f + QM_CHAT_SCROLLBAR_MARGIN);
	EXPECT_FLOAT_EQ(Left.h, 100.0f - QM_CHAT_SCROLLBAR_MARGIN * 2.0f);
}

TEST(QmChatScrollbar, ThumbHeightFollowsTheListViewportOverContentRule)
{
	// 滑块长度不再由聊天自己定上限：与列表同一把尺子 = 视口/内容 × 轨道，
	// 下夹 ui_token::chat::SCROLLBAR_MIN_THUMB，上不越过轨道。
	const CUIRect Bounds{0.0f, 0.0f, 200.0f, 300.0f};
	const float ViewHeight = 200.0f;
	const auto Half = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ViewHeight * 2.0f, 0.0f, false);
	ASSERT_TRUE(Half.m_Visible);
	// 轨道 = 视口，两端各让出一档内缩（列表覆盖式轨道同一条语义）。
	EXPECT_FLOAT_EQ(Half.m_Rail.y, 50.0f + QM_CHAT_SCROLLBAR_MARGIN);
	EXPECT_FLOAT_EQ(Half.m_Rail.h, ViewHeight - 2.0f * QM_CHAT_SCROLLBAR_MARGIN);
	EXPECT_NEAR(Half.m_Thumb.h, Half.m_Rail.h * 0.5f, 0.001f);
	// 内容只比视口多一点：滑块可以长到几乎满轨，但不越过轨道（列表同样如此）。
	const auto NearlyAllVisible = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ViewHeight + 1.0f, 0.0f, false);
	ASSERT_TRUE(NearlyAllVisible.m_Visible);
	EXPECT_LE(NearlyAllVisible.m_Thumb.h, NearlyAllVisible.m_Rail.h);
	EXPECT_GE(NearlyAllVisible.m_Thumb.h, Half.m_Thumb.h);
	// 内容极多：滑块停在最短一档，不会被压成一条缝。
	const auto Long = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ViewHeight * 1000.0f, 0.0f, false);
	ASSERT_TRUE(Long.m_Visible);
	EXPECT_NEAR(Long.m_Thumb.h, ui_token::chat::SCROLLBAR_MIN_THUMB, 0.001f);
	// 内容装得下就没有滚动条。
	EXPECT_FALSE(QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ViewHeight, 0.0f, false).m_Visible);
	EXPECT_FALSE(QmResolveChatScrollbarFrame(Bounds, 50.0f, 0.0f, ViewHeight * 4.0f, 0.0f, false).m_Visible);
	EXPECT_FALSE(QmResolveChatScrollbarFrame({0.0f, 0.0f, 0.0f, 300.0f}, 50.0f, ViewHeight, ViewHeight * 4.0f, 0.0f, false).m_Visible);
}

TEST(QmChatScrollbar, ThumbMapsBothBacklogEndsAndTracksOnlyInsideTheRail)
{
	const CUIRect Bounds{0.0f, 0.0f, 200.0f, 300.0f};
	const float ViewHeight = 200.0f;
	const float ContentHeight = ViewHeight * 4.0f;
	const auto Oldest = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ContentHeight, CChat::BacklogLineToScrollbarValue(1, 1), false);
	const auto Latest = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ContentHeight, CChat::BacklogLineToScrollbarValue(0, 1), false);
	ASSERT_TRUE(Oldest.m_Visible);
	ASSERT_TRUE(Latest.m_Visible);
	// 进度 0 = 最旧贴上沿，进度 1 = 最新贴下沿；两端都在轨道内。
	EXPECT_NEAR(Oldest.m_Thumb.y, Oldest.m_Rail.y, 0.001f);
	EXPECT_NEAR(Latest.m_Thumb.y + Latest.m_Thumb.h, Latest.m_Rail.y + Latest.m_Rail.h, 0.001f);
	EXPECT_NEAR(Oldest.m_TrackRange, Oldest.m_Rail.h - Oldest.m_Thumb.h, 0.001f);
	EXPECT_NEAR(Oldest.m_Thumb.h, Latest.m_Thumb.h, 0.001f);
	// 滑块只在轨道内移动：中途任意进度都不越过上下沿。
	for(const float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		const auto Frame = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ContentHeight, Progress, false);
		ASSERT_TRUE(Frame.m_Visible);
		EXPECT_GE(Frame.m_Thumb.y, Frame.m_Rail.y - 0.001f);
		EXPECT_LE(Frame.m_Thumb.y + Frame.m_Thumb.h, Frame.m_Rail.y + Frame.m_Rail.h + 0.001f);
		EXPECT_FLOAT_EQ(Frame.m_Thumb.x, Frame.m_Rail.x);
		EXPECT_FLOAT_EQ(Frame.m_Thumb.w, Frame.m_Rail.w);
	}
	// 左右两侧只是镜像位置：内缩量与长度完全一致。
	const auto Right = QmResolveChatScrollbarFrame(Bounds, 50.0f, ViewHeight, ContentHeight, 0.0f, true);
	EXPECT_NEAR(Right.m_Rail.x + Right.m_Rail.w, Bounds.x + Bounds.w - QM_CHAT_SCROLLBAR_MARGIN, 0.001f);
	EXPECT_NEAR(Oldest.m_Rail.x, Bounds.x + QM_CHAT_SCROLLBAR_MARGIN, 0.001f);
	EXPECT_FLOAT_EQ(Right.m_Rail.h, Oldest.m_Rail.h);
	EXPECT_FLOAT_EQ(Right.m_Thumb.h, Oldest.m_Thumb.h);
}

TEST(QmChatScrollbar, DragProgressFollowsPointerAndClampsAtBothTrackEnds)
{
	const CUIRect Bounds{0.0f, 0.0f, 200.0f, 300.0f};
	const auto Frame = QmResolveChatScrollbarFrame(Bounds, 50.0f, 200.0f, 800.0f, 0.0f, false);
	ASSERT_TRUE(Frame.m_Visible);
	ASSERT_GT(Frame.m_TrackRange, 0.0f);
	// 抓住滑块上沿拖动：指针走完一个行程 = 进度 0 → 1。
	EXPECT_NEAR(QmChatScrollbarProgressFromPointer(Frame, Frame.m_Rail.y, 0.0f), 0.0f, 0.001f);
	EXPECT_NEAR(QmChatScrollbarProgressFromPointer(Frame, Frame.m_Rail.y + Frame.m_TrackRange, 0.0f), 1.0f, 0.001f);
	// 触顶后再往下拖、触底后再往上拖：夹在端点，滑块不会被拖出轨道。
	EXPECT_FLOAT_EQ(QmChatScrollbarProgressFromPointer(Frame, Frame.m_Rail.y - 40.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatScrollbarProgressFromPointer(Frame, Frame.m_Rail.y + Frame.m_TrackRange + 40.0f, 20.0f), 1.0f);
	// 抓在滑块中间时位置平移一个抓取量，进度不变。
	const float Grab = Frame.m_Thumb.h * 0.5f;
	const float Middle = Frame.m_Rail.y + Frame.m_TrackRange * 0.5f;
	EXPECT_NEAR(QmChatScrollbarProgressFromPointer(Frame, Middle + Grab, Grab), 0.5f, 0.001f);
	// 滑块占满轨道（无可移动行程）时按"最新"处理，与拖到底等价。
	SQmChatScrollbarFrame Full = Frame;
	Full.m_Thumb.h = Full.m_Rail.h;
	Full.m_TrackRange = 0.0f;
	EXPECT_FLOAT_EQ(QmChatScrollbarProgressFromPointer(Full, Full.m_Rail.y, 0.0f), 1.0f);
}

TEST(QmChatScrollbar, HoverGrowthStaysInsideTheChatArea)
{
	// 悬浮/按下放大走 ui_token::feedback::HANDLE_*_GROW；几何由公共容器给出，
	// 放大档允许略微越过轨道端头（列表同样如此），但不能越出聊天区。
	const CUIRect Bounds{0.0f, 0.0f, 200.0f, 300.0f};
	const float Top = 50.0f;
	const float ViewHeight = 200.0f;
	for(const float Progress : {0.0f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		const auto Frame = QmResolveChatScrollbarFrame(Bounds, Top, ViewHeight, ViewHeight * 4.0f, Progress, false);
		ASSERT_TRUE(Frame.m_Visible);
		const CUIRect Hovered = QmChatScrollbarFeedbackRect(Frame.m_Thumb, true, false, false);
		EXPECT_FLOAT_EQ(Hovered.h, Frame.m_Thumb.h + 2.0f * ui_token::feedback::HANDLE_HOVER_GROW);
		EXPECT_GE(Hovered.y, Top - 0.001f);
		EXPECT_LE(Hovered.y + Hovered.h, Top + ViewHeight + 0.001f);
	}
}

TEST(QmChatScrollbar, HoverFeedbackIsAddedOnceAndNeverFightsPublicFeedback)
{
	const CUIRect Handle{40.0f, 100.0f, QM_CHAT_SCROLLBAR_WIDTH, 20.0f};
	const CUIRect Hovered = QmChatScrollbarFeedbackRect(Handle, true, false, false);
	EXPECT_FLOAT_EQ(Hovered.w, Handle.w + 2.0f * ui_token::feedback::HANDLE_HOVER_GROW);
	EXPECT_FLOAT_EQ(Hovered.h, Handle.h + 2.0f * ui_token::feedback::HANDLE_HOVER_GROW);
	EXPECT_FLOAT_EQ(Hovered.x + Hovered.w * 0.5f, Handle.x + Handle.w * 0.5f);
	EXPECT_FLOAT_EQ(Hovered.y + Hovered.h * 0.5f, Handle.y + Handle.h * 0.5f);
	// 未悬浮、拖动中（公共入口按活动项放大）或公共入口已经读到热项时不再叠第二档。
	EXPECT_FLOAT_EQ(QmChatScrollbarFeedbackRect(Handle, false, false, false).h, Handle.h);
	EXPECT_FLOAT_EQ(QmChatScrollbarFeedbackRect(Handle, true, true, false).h, Handle.h);
	EXPECT_FLOAT_EQ(QmChatScrollbarFeedbackRect(Handle, true, false, true).h, Handle.h);
}

TEST(QmChatHistoryScroll, WheelImpulseUsesPublicScrollConfigAndConvergesWithoutOvershoot)
{
	// 推进档位与手感全部取自公共配置；聊天只把"一档"声明成一行。
	const SQmScrollConfig PublicConfig = QmChatHistoryScrollConfig();
	const SQmScrollConfig NativeConfig = QmNativeWheelScrollConfig(1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	EXPECT_FLOAT_EQ(PublicConfig.m_WheelScale, 1.0f);
	EXPECT_EQ(PublicConfig.m_Feel, NativeConfig.m_Feel);
	EXPECT_FLOAT_EQ(PublicConfig.m_MaxOverscroll, NativeConfig.m_MaxOverscroll);
	EXPECT_FLOAT_EQ(PublicConfig.m_SpringResponse, NativeConfig.m_SpringResponse);
	EXPECT_FLOAT_EQ(PublicConfig.m_SpringDamping, NativeConfig.m_SpringDamping);

	const SQmScrollMetrics Metrics = QmChatHistoryScrollMetrics(100, 10);
	ASSERT_FLOAT_EQ(Metrics.MaxOffset(), 90.0f);

	// 平滑档：给一段可观察的多帧推进时长，断言逐帧单调逼近目标且不过冲。
	SQmScrollConfig Config = PublicConfig;
	Config.m_Feel = EQmScrollFeel::SMOOTH;
	Config.m_NativeWheelAnimationTime = 0.12f;
	CQmScrollState State;
	QmChatHistoryWheel(State, 1, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), 0.0f);
	float Previous = State.Offset();
	for(int Frame = 0; Frame < 60; ++Frame)
	{
		State.Advance(1.0f / 60.0f, Metrics, Config);
		EXPECT_GE(State.Offset(), Previous - 0.0001f);
		EXPECT_LE(State.Offset(), 1.0f + 0.0001f);
		Previous = State.Offset();
	}
	EXPECT_NEAR(State.Offset(), 1.0f, 0.001f);
	EXPECT_FALSE(State.Animating());
	EXPECT_EQ(QmChatHistoryScrollLine(State.Offset(), Metrics), 1);

	// 行号换算夹在合法范围内：越界偏移不会把可见行推出历史。
	EXPECT_EQ(QmChatHistoryScrollLine(0.4f, Metrics), 0);
	EXPECT_EQ(QmChatHistoryScrollLine(-5.0f, Metrics), 0);
	EXPECT_EQ(QmChatHistoryScrollLine(1000.0f, Metrics), 90);
}

TEST(QmChatHistoryScroll, DragFollowsPointerAndReboundsIntoLegalRangeAfterContentShrinks)
{
	SQmScrollConfig Config = QmChatHistoryScrollConfig();
	// 生产默认的橡皮筋档：越界先被软性夹住，再由回弹收回到边界。
	Config.m_MaxOverscroll = 8.0f;
	const SQmScrollMetrics Before = QmChatHistoryScrollMetrics(100, 10);
	CQmScrollState State;
	QmChatHistoryDragToLine(State, 80, Before, Config);
	// 拖动直接跟手，不经过弹簧。
	EXPECT_FLOAT_EQ(State.Offset(), 80.0f);
	EXPECT_FALSE(State.Animating());

	// 拖动期间内容变短（历史行变少 / 可见行变多），80 行已经越界。
	const SQmScrollMetrics After = QmChatHistoryScrollMetrics(30, 10);
	float Previous = State.Offset();
	for(int Frame = 0; Frame < 240; ++Frame)
	{
		State.Advance(1.0f / 60.0f, After, Config);
		// 只往合法范围收，不会越过边界反向往回走。
		EXPECT_LE(State.Offset(), Previous + 0.0001f);
		EXPECT_GE(State.Offset(), 0.0f);
		Previous = State.Offset();
	}
	EXPECT_NEAR(State.Offset(), 20.0f, 0.01f);
	EXPECT_EQ(QmChatHistoryScrollLine(State.Offset(), After), 20);
}

TEST(QmChatHistoryScroll, WheelPhysicsSharesTheListScrollConfig)
{
	// 聊天不自带一套手感：推进档位（原版 / 顺滑 / 弹簧）、弹簧响应与阻尼、橡皮筋距离、
	// 摩擦与越界回弹刚度全部取自 QmNativeWheelScrollConfig——与列表滚动面同一份配置。
	// 唯一差别是单位：聊天一档 = 一行（偏移单位是行），列表一档 = 若干像素。
	constexpr float LINE_EXTENT = 20.0f;
	const SQmScrollConfig Chat = QmChatHistoryScrollConfig(LINE_EXTENT);
	// 列表侧必须用与聊天相同的平滑时长比：默认参数是 0 秒，会把「原生滚轮动画时间」
	// 直接算成 0，与聊天（读 ui_smooth_scroll_time）比较时就变成假差异。
	const SQmResolvedScrollPolicy ListPolicy = QmResolveScrollPolicy(SQmScrollRequest{}, 1.0f, g_Config.m_UiSmoothScrollTime / 1000.0f);
	const SQmScrollConfig &List = ListPolicy.m_Config;
	EXPECT_EQ(Chat.m_Feel, List.m_Feel);
	EXPECT_EQ(Chat.m_NativeWheelStep, List.m_NativeWheelStep);
	EXPECT_FLOAT_EQ(Chat.m_NativeWheelAnimationTime, List.m_NativeWheelAnimationTime);
	// 聊天单位是行，公共配置单位是像素：数值不同，但换算回像素后必须一致。
	EXPECT_FLOAT_EQ(Chat.m_MaxOverscroll * LINE_EXTENT, List.m_MaxOverscroll);
	EXPECT_FLOAT_EQ(Chat.m_SpringResponse, List.m_SpringResponse);
	EXPECT_FLOAT_EQ(Chat.m_SpringDamping, List.m_SpringDamping);
	EXPECT_FLOAT_EQ(Chat.m_Friction, List.m_Friction);
	EXPECT_FLOAT_EQ(Chat.m_OverscrollStiffness, List.m_OverscrollStiffness);
	EXPECT_FLOAT_EQ(Chat.m_OverscrollDamping, List.m_OverscrollDamping);
	EXPECT_FLOAT_EQ(Chat.m_RestVelocity, List.m_RestVelocity);
	EXPECT_FLOAT_EQ(Chat.m_RestDistance, List.m_RestDistance);
	// 聊天只把"一档"声明成一行，步长单位由聊天正文的行渲染决定。
	EXPECT_FLOAT_EQ(Chat.m_WheelScale, 1.0f);
}

TEST(QmChatHistoryScroll, BouncePixelsAreConvertedToMessageLineUnits)
{
	EXPECT_FLOAT_EQ(QmChatHistoryBounceLines(25.0f, 20.0f), 1.25f);
	EXPECT_FLOAT_EQ(QmChatHistoryBounceLines(0.0f, 20.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmChatHistoryBounceLines(25.0f, 0.0f), 25.0f);
}

TEST(QmChatHistoryScroll, MultipleWheelStepsKeepTheirNotchCount)
{
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = true;
	Config.m_Feel = EQmScrollFeel::SMOOTH;
	Config.m_NativeWheelAnimationTime = 0.12f;
	const SQmScrollMetrics Metrics = QmChatHistoryScrollMetrics(100, 10);
	CQmScrollState State;
	QmChatHistoryWheel(State, 3, Metrics, Config);
	for(int Frame = 0; Frame < 60; ++Frame)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 3.0f, 0.001f);
}

TEST(QmChatHistoryScroll, SubLineOffsetKeepsSpringMotionContinuous)
{
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = true;
	Config.m_Feel = EQmScrollFeel::SPRING;
	Config.m_SpringResponse = 0.3f;
	Config.m_SpringDamping = 0.9f;
	Config.m_MaxOverscroll = 0.0f;
	const SQmScrollMetrics Metrics = QmChatHistoryScrollMetrics(100, 10);
	CQmScrollState State;
	QmChatHistoryWheel(State, 1, Metrics, Config);

	bool SawSubLineMotion = false;
	for(int Frame = 0; Frame < 120; ++Frame)
	{
		State.Advance(1.0f / 60.0f, Metrics, Config);
		const int Line = QmChatHistoryScrollLine(State.Offset(), Metrics);
		const float PixelOffset = QmChatHistoryLayoutOffset(State.Offset(), Line, 20.0f);
		SawSubLineMotion |= std::abs(PixelOffset) > 0.01f;
	}
	EXPECT_TRUE(SawSubLineMotion);
	EXPECT_EQ(QmChatHistoryScrollLine(State.Offset(), Metrics), 1);
}

TEST(QmChatHistoryScroll, IrregularFrameIntervalsStayContinuousAcrossLineBoundaries)
{
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = true;
	Config.m_Feel = EQmScrollFeel::SPRING;
	Config.m_SpringResponse = 0.3f;
	Config.m_SpringDamping = 1.0f;
	Config.m_MaxOverscroll = 0.0f;
	const SQmScrollMetrics Metrics = QmChatHistoryScrollMetrics(100, 10);
	CQmScrollState State;
	QmChatHistoryWheel(State, 3, Metrics, Config);

	// 模拟 60 Hz 渲染帧捕获到的真实间隔：它们不会恒等于 1/60。
	constexpr std::array<float, 6> s_aFrameDts = {
		16.0f / 1000.0f,
		17.0f / 1000.0f,
		16.0f / 1000.0f,
		17.0f / 1000.0f,
		15.0f / 1000.0f,
		17.0f / 1000.0f,
	};
	int PreviousLine = 0;
	float PreviousTop = 0.0f;
	int CrossedLines = 0;
	for(int Frame = 0; Frame < 180; ++Frame)
	{
		State.Advance(s_aFrameDts[Frame % s_aFrameDts.size()], Metrics, Config);
		const int Line = QmChatHistoryScrollLine(State.Offset(), Metrics);
		const float Top = (float)Line * 20.0f + QmChatHistoryLayoutOffset(State.Offset(), Line, 20.0f);
		// 跨整数行时仍应是亚行级连续位移，不能一帧跳掉大半行。
		EXPECT_LT(std::abs(Top - PreviousTop), 12.0f);
		CrossedLines += Line - PreviousLine;
		PreviousLine = Line;
		PreviousTop = Top;
	}
	EXPECT_GE(CrossedLines, 2);
	EXPECT_EQ(PreviousLine, 3);
}

TEST(QmChatHistoryScroll, OverscrollPastBothEndsReboundsIntoLegalRange)
{
	SQmScrollConfig Config = QmChatHistoryScrollConfig();
	// 橡皮筋距离显式给一档：生产值来自 qm_ui_scroll_bounce，测试不依赖本机配置。
	Config.m_MaxOverscroll = 8.0f;
	const SQmScrollMetrics Metrics = QmChatHistoryScrollMetrics(100, 10);
	ASSERT_FLOAT_EQ(Metrics.MaxOffset(), 90.0f);
	CQmScrollState State;

	// 触顶后继续往"更早"拖：允许超拉，越界量被渐进阻力压缩在橡皮筋区内。
	QmChatHistoryDragToLine(State, -6, Metrics, Config);
	EXPECT_LT(State.Offset(), 0.0f);
	EXPECT_GE(State.Offset(), -Config.m_MaxOverscroll - 0.001f);
	EXPECT_LT(QmChatHistoryLayoutOffset(State.Offset(), QmChatHistoryScrollLine(State.Offset(), Metrics), 20.0f), 0.0f);
	for(int Frame = 0; Frame < 240; ++Frame)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 0.0f, 0.01f);
	EXPECT_EQ(QmChatHistoryScrollLine(State.Offset(), Metrics), 0);

	// 触底后继续拖：同样先超拉，再由同一条回弹收回最大偏移。
	QmChatHistoryDragToLine(State, Metrics.MaxOffset() + 40.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), Metrics.MaxOffset());
	EXPECT_LE(State.Offset(), Metrics.MaxOffset() + Config.m_MaxOverscroll + 0.001f);
	EXPECT_GT(QmChatHistoryLayoutOffset(State.Offset(), QmChatHistoryScrollLine(State.Offset(), Metrics), 20.0f), 0.0f);
	for(int Frame = 0; Frame < 240; ++Frame)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), Metrics.MaxOffset(), 0.01f);
	EXPECT_EQ(QmChatHistoryScrollLine(State.Offset(), Metrics), 90);
}

TEST(QmChatScrollbar, PreviewUsesTheSameScaledRailAndHandleGeometry)
{
	const auto GameRail = QmChatScrollbarRail({0.0f, 0.0f, 200.0f, 100.0f}, 0.0f, 100.0f, false);
	const auto PreviewRail = QmChatScrollbarRail({0.0f, 0.0f, 400.0f, 200.0f}, 0.0f, 200.0f, false, 2.0f);
	EXPECT_FLOAT_EQ(PreviewRail.x, GameRail.x * 2.0f);
	EXPECT_FLOAT_EQ(PreviewRail.w, GameRail.w * 2.0f);
	EXPECT_FLOAT_EQ(PreviewRail.h, GameRail.h * 2.0f);
	EXPECT_FLOAT_EQ(QmChatScrollbarHandleHeight(PreviewRail.h, 3, 6, 2.0f), QmChatScrollbarHandleHeight(GameRail.h, 3, 6) * 2.0f);
	// 预览用同一个几何入口摆放滑块：与游戏内同一条「进度 × 行程」。
	const float Height = QmChatScrollbarHandleHeight(GameRail.h, 1, 4);
	const auto Latest = QmChatScrollbarHandle(GameRail, Height, CChat::BacklogLineToScrollbarValue(0, 3));
	EXPECT_FLOAT_EQ(Latest.y + Latest.h, GameRail.y + GameRail.h);
	const auto Oldest = QmChatScrollbarHandle(GameRail, Height, CChat::BacklogLineToScrollbarValue(3, 3));
	EXPECT_FLOAT_EQ(Oldest.y, GameRail.y);
	const auto NarrowRail = QmChatScrollbarRail({0.0f, 0.0f, 1.0f, 5.0f}, 0.0f, 5.0f, true);
	EXPECT_GE(NarrowRail.x, 0.0f);
	EXPECT_LE(NarrowRail.x + NarrowRail.w, 1.0f);
}

TEST(QmChatScrollbar, LeftHistoryReservesTheSameGapAsPreview)
{
	const auto Rail = QmChatScrollbarRail({0, 0, 200, 100}, 0, 100, false);
	EXPECT_FLOAT_EQ(QmChatHistoryStartX(false) - (Rail.x + Rail.w), QM_CHAT_SCROLLBAR_MARGIN);
	EXPECT_FLOAT_EQ(QmChatHistoryStartX(true), 5);
}

TEST(QmChatScrollbar, HiddenRailKeepsBothSidesInTheNextHudBounds)
{
	const CUIRect ChatRect{0.0f, 50.0f, 250.0f, 250.0f};
	for(bool OnRight : {false, true})
	{
		SCOPED_TRACE(OnRight);
		CQmChatVisibleBounds Bounds(ChatRect);
		const float X = QmChatHistoryStartX(OnRight);
		Bounds.Extend({X, 250.0f, ChatRect.w - X, 20.0f});
		ASSERT_TRUE(Bounds.Valid());
		const auto Rail = QmChatScrollbarRail(ChatRect, 50.0f, 220.0f, OnRight);
		EXPECT_LE(Bounds.Rect().x, Rail.x);
		EXPECT_GE(Bounds.Rect().x + Bounds.Rect().w, Rail.x + Rail.w);
		EXPECT_FLOAT_EQ(Bounds.Rect().y, 250.0f);
		EXPECT_FLOAT_EQ(Bounds.Rect().h, 20.0f);
		// 轨道现在沿长度两端各内缩一档（滑块更长、轨道更短），所以轨道比聊天区矮。
		EXPECT_GT(Rail.y, ChatRect.y);
		EXPECT_LT(Rail.y + Rail.h, ChatRect.y + ChatRect.h);
		Bounds.Extend(Rail);
		// 上沿跟着轨道走；下沿仍由历史区底部那一行决定，不再等于聊天区底边。
		EXPECT_FLOAT_EQ(Bounds.Rect().y, Rail.y);
		EXPECT_FLOAT_EQ(Bounds.Rect().y + Bounds.Rect().h, 270.0f);
	}
}

TEST(QmChatScrollbar, BothRailsRemainVisibleAtEitherScaledScreenEdge)
{
	const CUIRect ChatRect{0.0f, 50.0f, 250.0f, 250.0f};
	for(bool OnRight : {false, true})
		for(bool AtRightEdge : {false, true})
			for(float Scale : {0.5f, 1.0f, 2.0f})
			{
				SCOPED_TRACE(::testing::Message() << OnRight << "/" << AtRightEdge << "/" << Scale);
				CQmChatVisibleBounds Bounds(ChatRect);
				const float X = QmChatHistoryStartX(OnRight);
				Bounds.Extend({X, 250.0f, ChatRect.w - X, 20.0f});
				QmHudEditor::CVisibleBounds HudBounds;
				HudBounds.Observe(ChatRect, ChatRect, ChatRect, Bounds.Rect());
				HudBounds.BeginRenderFrame();
				const CUIRect Visible = HudBounds.Resolve(ChatRect, ChatRect);
				const float Anchor = QmHudEditor::RestoreAxisAnchor(AtRightEdge ? 1.0f : 0.0f, Visible.w * Scale, 0.0f, 600.0f, (Visible.x - ChatRect.x) * Scale);
				const SQmChatViewport Viewport{{-Anchor / Scale, 0.0f, 600.0f / Scale, 300.0f / Scale}, vec2(2400.0f, 1200.0f)};
				const auto Rail = QmChatScrollbarRail(ChatRect, 50.0f, 100.0f, OnRight);
				const auto Pixels = Viewport.ToPixels(Rail);
				EXPECT_GE(Pixels.x, 0.0f);
				EXPECT_LE(Pixels.x + Pixels.w, 2400.0f);
				const auto Clip = Viewport.ClipPixels(Rail);
				EXPECT_NEAR(Clip.w, Pixels.w, 1.0f);
			}
}

TEST(QmChatScrollbar, ActualInputAndPopupExtentsExpandHudBounds)
{
	CQmChatVisibleBounds Bounds({0.0f, 50.0f, 250.0f, 250.0f});
	Bounds.Extend({7.0f, 250.0f, 243.0f, 20.0f});
	Bounds.Extend({7.0f, 275.0f, 360.0f, 20.0f});
	Bounds.Extend({-12.0f, 210.0f, 60.0f, 50.0f});
	EXPECT_FLOAT_EQ(Bounds.Rect().x, -12.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().x + Bounds.Rect().w, 367.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().y, 210.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().y + Bounds.Rect().h, 295.0f);
}

TEST(QmChatScrollbar, EmptyAndDegenerateContentDoesNotCreateHudBounds)
{
	CQmChatVisibleBounds Bounds({0.0f, 50.0f, 250.0f, 250.0f});
	EXPECT_FALSE(Bounds.Valid());
	Bounds.Extend({-100.0f, -100.0f, 0.0f, 50.0f});
	Bounds.Extend({-100.0f, -100.0f, 50.0f, -1.0f});
	EXPECT_FALSE(Bounds.Valid());
	Bounds.Extend({7.0f, 250.0f, 20.0f, 10.0f});
	ASSERT_TRUE(Bounds.Valid());
	EXPECT_FLOAT_EQ(Bounds.Rect().x, 0.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().w, 250.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().y, 250.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().h, 10.0f);
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

TEST(QmChatInputLayout, InvalidProjectionReturnsEmptyClip)
{
	for(const auto &Map : {CUIRect{0.0f, 0.0f, 0.0f, 300.0f}, CUIRect{0.0f, 0.0f, 400.0f, 0.0f}, CUIRect{0.0f, 0.0f, -400.0f, 300.0f}})
	{
		SCOPED_TRACE(::testing::Message() << Map.w << "/" << Map.h);
		const auto Clip = QmChatInputPixelClip({40.0f, 120.0f, 100.0f, 20.0f}, Map, 1600, 1200);
		EXPECT_EQ(Clip.m_X, 0);
		EXPECT_EQ(Clip.m_Y, 0);
		EXPECT_EQ(Clip.m_W, 0);
		EXPECT_EQ(Clip.m_H, 0);
		const SQmChatViewport Viewport{Map, vec2(1600.0f, 1200.0f)};
		EXPECT_FLOAT_EQ(Viewport.ClipPixels({40.0f, 120.0f, 100.0f, 20.0f}).w, 0.0f);
	}
}

TEST(QmChatInputLayout, UnavailableFramebufferReturnsEmptyClip)
{
	for(const auto &Size : {vec2(0.0f, 1200.0f), vec2(1600.0f, 0.0f), vec2(-1.0f, 1200.0f), vec2(1600.0f, -1.0f)})
	{
		SCOPED_TRACE(::testing::Message() << Size.x << "/" << Size.y);
		const auto Clip = QmChatInputPixelClip({40.0f, 120.0f, 100.0f, 20.0f}, {0.0f, 0.0f, 400.0f, 300.0f}, (int)Size.x, (int)Size.y);
		EXPECT_EQ(Clip.m_X, 0);
		EXPECT_EQ(Clip.m_Y, 0);
		EXPECT_EQ(Clip.m_W, 0);
		EXPECT_EQ(Clip.m_H, 0);
	}
}

TEST(QmChatInputViewport, NonUniformProjectionKeepsClipMouseAndTooltipOnTheSameBody)
{
	const SQmChatViewport Viewport{{-20.0f, 100.0f, 200.0f, 100.0f}, vec2(1600.0f, 1200.0f)};
	const CUIRect Body{10.0f, 120.0f, 100.0f, 20.0f};
	const auto Clip = Viewport.ClipPixels(Body);
	EXPECT_FLOAT_EQ(Clip.x, 240.0f);
	EXPECT_FLOAT_EQ(Clip.y, 240.0f);
	EXPECT_FLOAT_EQ(Clip.w, 800.0f);
	EXPECT_FLOAT_EQ(Clip.h, 240.0f);
	EXPECT_TRUE(Body.Inside(Viewport.ToLocal(vec2(640.0f, 360.0f))));
	const auto Anchor = Viewport.ToUi(Body, {0.0f, 0.0f, 800.0f, 600.0f});
	EXPECT_FLOAT_EQ(Anchor.x, 120.0f);
	EXPECT_FLOAT_EQ(Anchor.y, 120.0f);
	EXPECT_FLOAT_EQ(Anchor.w, 400.0f);
	EXPECT_FLOAT_EQ(Anchor.h, 120.0f);
}

TEST(QmChatInputLayout, FarOffscreenClipClampsBeforeIntegerConversion)
{
	for(float X : {-1.0e20f, 1.0e20f})
	{
		SCOPED_TRACE(X);
		const auto Clip = QmChatInputPixelClip({X, 120.0f, 100.0f, 20.0f}, {0.0f, 0.0f, 400.0f, 300.0f}, 1600, 1200);
		EXPECT_EQ(Clip.m_X, X < 0.0f ? 0 : 1600);
		EXPECT_EQ(Clip.m_Y, 480);
		EXPECT_EQ(Clip.m_W, 0);
		EXPECT_EQ(Clip.m_H, 80);
	}
}

namespace
{
	SQmChatWhisperCandidate WhisperCandidate(int ClientId, const char *pName, bool Active = true, bool Local = false)
	{
		SQmChatWhisperCandidate Candidate;
		Candidate.m_ClientId = ClientId;
		Candidate.m_pName = pName;
		Candidate.m_Active = Active;
		Candidate.m_Local = Local;
		return Candidate;
	}
}

TEST(QmChatWhisperTargets, SkipsLocalConnectionsAndUnusableEntries)
{
	// 服务器按名字排序返回玩家；自己（主/副连接）与空名、离线条目都不能进入私聊列表。
	const SQmChatWhisperCandidate aCandidates[] = {
		WhisperCandidate(3, "alice"),
		WhisperCandidate(1, "self", true, true),
		WhisperCandidate(2, "dummy", true, true),
		WhisperCandidate(4, "", true, false),
		WhisperCandidate(5, "bob", false, false),
		WhisperCandidate(6, nullptr, true, false),
		WhisperCandidate(7, "carol"),
	};
	SQmChatWhisperTarget aTargets[8];
	const int NumTargets = QmChatCollectWhisperTargets(aCandidates, (int)std::size(aCandidates), aTargets, (int)std::size(aTargets));

	ASSERT_EQ(NumTargets, 2);
	EXPECT_EQ(aTargets[0].m_ClientId, 3);
	EXPECT_STREQ(aTargets[0].m_pName, "alice");
	EXPECT_EQ(aTargets[1].m_ClientId, 7);
	EXPECT_STREQ(aTargets[1].m_pName, "carol");
}

TEST(QmChatWhisperTargets, RespectsCapacityAndEmptyInput)
{
	const SQmChatWhisperCandidate aCandidates[] = {
		WhisperCandidate(0, "a"),
		WhisperCandidate(1, "b"),
		WhisperCandidate(2, "c"),
	};
	SQmChatWhisperTarget aTargets[2];
	EXPECT_EQ(QmChatCollectWhisperTargets(aCandidates, (int)std::size(aCandidates), aTargets, 2), 2);
	EXPECT_EQ(QmChatCollectWhisperTargets(aCandidates, (int)std::size(aCandidates), aTargets, 0), 0);
	EXPECT_EQ(QmChatCollectWhisperTargets(aCandidates, 0, aTargets, (int)std::size(aTargets)), 0);
	EXPECT_EQ(QmChatCollectWhisperTargets(nullptr, 3, aTargets, (int)std::size(aTargets)), 0);
	EXPECT_EQ(QmChatCollectWhisperTargets(aCandidates, (int)std::size(aCandidates), nullptr, 3), 0);
}

TEST(QmChatWhisperTargets, BuildWhisperCommandKeepsQuotedNameAndMessage)
{
	char aCommand[256];
	ASSERT_TRUE(CChat::BuildWhisperCommand(aCommand, sizeof(aCommand), "alice", "hello there"));
	EXPECT_STREQ(aCommand, "/w \"alice\" hello there");
	EXPECT_FALSE(CChat::BuildWhisperCommand(aCommand, sizeof(aCommand), "", "hello"));
	EXPECT_FALSE(CChat::BuildWhisperCommand(aCommand, sizeof(aCommand), "alice", nullptr));
}

TEST(QmChatLineMenuLayout, AllActionsFitInsidePopupWithBottomPadding)
{
	const CUIRect Menu = qm_chat_line_menu::PopupRect({50.0f, 80.0f}, {0.0f, 0.0f, 800.0f, 600.0f});
	CUIRect Content;
	Menu.Margin(qm_chat_line_menu::CONTENT_INSET, &Content);
	const auto Layout = qm_chat_line_menu::LayoutContent(Content);

	ASSERT_EQ(Layout.m_aButtons.size(), 7u);
	EXPECT_FLOAT_EQ(Layout.m_Header.y, Content.y);
	EXPECT_GE(Layout.m_HeaderDivider.y, Layout.m_Header.y + Layout.m_Header.h);
	float PreviousBottom = Layout.m_HeaderDivider.y + Layout.m_HeaderDivider.h;
	for(const CUIRect &Button : Layout.m_aButtons)
	{
		EXPECT_GT(Button.y, PreviousBottom);
		EXPECT_FLOAT_EQ(Button.h, 23.0f);
		EXPECT_GE(Button.x, Content.x);
		EXPECT_LE(Button.x + Button.w, Content.x + Content.w);
		EXPECT_LE(Button.y + Button.h, Content.y + Content.h);
		PreviousBottom = Button.y + Button.h;
	}
	EXPECT_GE(Layout.m_GroupDivider.y, Layout.m_aButtons[qm_chat_line_menu::SPECTATE].y + Layout.m_aButtons[qm_chat_line_menu::SPECTATE].h);
	EXPECT_LE(Layout.m_GroupDivider.y + Layout.m_GroupDivider.h, Layout.m_aButtons[qm_chat_line_menu::MUTE_PLAYER].y);
	EXPECT_GE(Menu.y + Menu.h - PreviousBottom, qm_chat_line_menu::CONTENT_INSET);
}

TEST(QmChatLineMenuLayout, InteriorPointerKeepsPopupAtRequestedPosition)
{
	const CUIRect Menu = qm_chat_line_menu::PopupRect({100.0f, 120.0f}, {0.0f, 0.0f, 800.0f, 600.0f});
	EXPECT_FLOAT_EQ(Menu.x, 100.0f);
	EXPECT_FLOAT_EQ(Menu.y, 120.0f);
}

TEST(QmChatLineMenuLayout, BottomPointerOpensUpwardUsingFullMenuHeight)
{
	const CUIRect Menu = qm_chat_line_menu::PopupRect({100.0f, 590.0f}, {0.0f, 0.0f, 800.0f, 600.0f});
	EXPECT_FLOAT_EQ(Menu.x, 100.0f);
	EXPECT_LT(Menu.y, 590.0f);
	EXPECT_FLOAT_EQ(Menu.y + Menu.h, 590.0f);
}

TEST(QmChatLineMenuLayout, AllViewportCornersKeepScreenMargin)
{
	const CUIRect Screen{30.0f, 40.0f, 400.0f, 300.0f};
	const vec2 aCorners[] = {{30.0f, 40.0f}, {430.0f, 40.0f}, {30.0f, 340.0f}, {430.0f, 340.0f}};
	for(const vec2 Corner : aCorners)
	{
		SCOPED_TRACE(::testing::Message() << "pointer " << Corner.x << ", " << Corner.y);
		const CUIRect Menu = qm_chat_line_menu::PopupRect(Corner, Screen);
		EXPECT_GE(Menu.x, Screen.x + qm_chat_line_menu::SCREEN_MARGIN);
		EXPECT_GE(Menu.y, Screen.y + qm_chat_line_menu::SCREEN_MARGIN);
		EXPECT_LE(Menu.x + Menu.w, Screen.x + Screen.w - qm_chat_line_menu::SCREEN_MARGIN);
		EXPECT_LE(Menu.y + Menu.h, Screen.y + Screen.h - qm_chat_line_menu::SCREEN_MARGIN);
	}
}
