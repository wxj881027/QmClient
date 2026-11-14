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
		// 圆形图标按钮：宽高相等（绘制层按胶囊档裁成正圆），直径就是输入行行高，
		// 与同一行「全体」前缀块逐像素同高——chat.cpp 的前缀块也取同一个行高函数。
		EXPECT_FLOAT_EQ(Layout.m_ButtonW, Layout.m_ButtonH);
		EXPECT_FLOAT_EQ(Layout.m_ButtonH, QmChatInputRowHeight(FontSize));
		EXPECT_FLOAT_EQ(Layout.m_ButtonH, QmChatTranslateButtonHeight(FontSize));
		// 行高不再等于字号：按钮比字形高出一档，居中后上下各露半档，读作「一行控件」。
		EXPECT_GT(Layout.m_ButtonH, FontSize);
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
		EXPECT_FLOAT_EQ(QmChatTranslateIconBoxSize(Layout.m_ButtonW), Inner);
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

TEST(QmChatScrollbar, HandleFitsShortHistoryAndMapsBothBacklogEnds)
{
	const auto Rail = QmChatScrollbarRail({0.0f, 0.0f, 100.0f, 100.0f}, 50.0f, 100.0f, false);
	const float Inset = QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	const float Height = QmChatScrollbarHandleHeight(Rail.h, 3, 10);
	const auto Oldest = QmChatScrollbarHandle(Rail, Height, CChat::BacklogLineToScrollbarValue(7, 7));
	const auto Latest = QmChatScrollbarHandle(Rail, Height, CChat::BacklogLineToScrollbarValue(0, 7));
	// 两端按悬浮/按下放大档留出余量：滑块贴到端点时放大也不会顶出轨道。
	EXPECT_FLOAT_EQ(Oldest.y, Rail.y + Inset);
	EXPECT_FLOAT_EQ(Latest.y + Latest.h, Rail.y + Rail.h - Inset);
	// 极短轨道：滑块按扣掉反馈余量后的可用长度收缩，仍然完整落在轨道内。
	const float ShortThumb = QmChatScrollbarHandleHeight(5.0f, 1, 100);
	EXPECT_GT(ShortThumb, 0.0f);
	EXPECT_LE(ShortThumb, QmChatScrollbarUsableLength(QM_CHAT_SCROLLBAR_WIDTH, 5.0f) + 0.001f);
	EXPECT_FLOAT_EQ(QmChatScrollbarHandleHeight(0.0f, 1, 100), 0.0f);
	// 极长内容下滑块停在最短长度，不会被压成一条缝（内容再多也不变短）。
	const float LongContentThumb = QmChatScrollbarHandleHeight(100.0f, 1, 100);
	EXPECT_FLOAT_EQ(LongContentThumb, QmChatScrollbarHandleHeight(100.0f, 1, 1000));
	EXPECT_GT(LongContentThumb, 0.0f);
	// 内容只比一屏多一点时滑块被压到轨道一半以内，不再是几乎满轨的长条。
	const float NearlyWholeContentThumb = QmChatScrollbarHandleHeight(100.0f, 10, 0);
	EXPECT_LE(NearlyWholeContentThumb, 50.0f);
	EXPECT_GE(NearlyWholeContentThumb, LongContentThumb);
}

TEST(QmChatScrollbar, ShortenedThumbStillMapsBothBacklogEnds)
{
	const CUIRect Rail{0.0f, 0.0f, 4.0f, 200.0f};
	const float Inset = QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
	const float ThumbHeight = QmChatScrollbarHandleHeight(Rail.h, 9, 10, 1.0f, Rail.w);
	ASSERT_LE(ThumbHeight, QmChatScrollbarUsableLength(Rail.w, Rail.h) * 0.5f);
	// 滑块变短不改变映射：最新消息仍贴轨道下沿（扣掉反馈余量），最旧消息仍贴上沿。
	const auto Latest = QmChatScrollbarHandle(Rail, ThumbHeight, CChat::BacklogLineToScrollbarValue(0, 1));
	EXPECT_FLOAT_EQ(Latest.y, Rail.y + Rail.h - Inset - ThumbHeight);
	EXPECT_FLOAT_EQ(Latest.y + Latest.h, Rail.y + Rail.h - Inset);
	const auto Oldest = QmChatScrollbarHandle(Rail, ThumbHeight, CChat::BacklogLineToScrollbarValue(1, 1));
	EXPECT_FLOAT_EQ(Oldest.y, Rail.y + Inset);
	// 轨道比滑块还短时滑块收敛到可用长度，不会溢出。
	const auto Clamped = QmChatScrollbarHandle(Rail, 1000.0f, 1.0f);
	EXPECT_FLOAT_EQ(Clamped.h, QmChatScrollbarUsableLength(Rail.w, Rail.h));
}

TEST(QmChatScrollbar, HandleAlwaysStaysInsideRailIncludingFeedbackGrowth)
{
	// 滑块高度按「可视/总量」自适应，并且两端按公共滑块（ui_widget::DrawScrollbarHandle）
	// 的悬浮/按下放大档预留余量：放大后也不得顶出轨道上端（旧实现按下时会凸出去）。
	for(const float RailHeight : {6.0f, 10.0f, 30.0f, 60.0f, 200.0f})
		for(const int VisibleLines : {0, 1, 3, 9, 10, 50})
			for(const float Value : {0.0f, 0.42f, 1.0f})
			{
				SCOPED_TRACE(::testing::Message() << RailHeight << "/" << VisibleLines << "/" << Value);
				const CUIRect Rail{40.0f, 20.0f, QM_CHAT_SCROLLBAR_WIDTH, RailHeight};
				const float Inset = QmChatScrollbarFeedbackInset(Rail.w, Rail.h);
				const float HandleHeight = QmChatScrollbarHandleHeight(Rail.h, VisibleLines, 10, 1.0f, Rail.w);
				ASSERT_GE(HandleHeight, 0.0f);
				ASSERT_LE(HandleHeight, Rail.h);
				const CUIRect Handle = QmChatScrollbarHandle(Rail, HandleHeight, Value);
				EXPECT_GE(Handle.y, Rail.y + Inset - 0.001f);
				EXPECT_LE(Handle.y + Handle.h, Rail.y + Rail.h - Inset + 0.001f);
				if(Inset + 0.001f >= ui_token::feedback::HANDLE_PRESS_GROW)
				{
					// 按下档放大后仍完整落在轨道内：上端不顶出轨道上端。
					EXPECT_GE(Handle.y - ui_token::feedback::HANDLE_PRESS_GROW, Rail.y - 0.001f);
					EXPECT_LE(Handle.y + Handle.h + ui_token::feedback::HANDLE_PRESS_GROW, Rail.y + Rail.h + 0.001f);
				}
				// 悬浮档放大（游戏内 HUD 帧由聊天自己补这一档）同样留在轨道内。
				const CUIRect Hovered = QmChatScrollbarFeedbackRect(Handle, true, false, false);
				EXPECT_GE(Hovered.y, Rail.y - 0.001f);
				EXPECT_LE(Hovered.y + Hovered.h, Rail.y + Rail.h + 0.001f);
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

TEST(QmChatScrollbar, PreviewUsesTheSameScaledRailAndHandleGeometry)
{
	const auto GameRail = QmChatScrollbarRail({0.0f, 0.0f, 200.0f, 100.0f}, 0.0f, 100.0f, false);
	const auto PreviewRail = QmChatScrollbarRail({0.0f, 0.0f, 400.0f, 200.0f}, 0.0f, 200.0f, false, 2.0f);
	EXPECT_FLOAT_EQ(PreviewRail.x, GameRail.x * 2.0f);
	EXPECT_FLOAT_EQ(PreviewRail.w, GameRail.w * 2.0f);
	EXPECT_FLOAT_EQ(QmChatScrollbarHandleHeight(PreviewRail.h, 3, 6, 2.0f), QmChatScrollbarHandleHeight(GameRail.h, 3, 6) * 2.0f);
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
