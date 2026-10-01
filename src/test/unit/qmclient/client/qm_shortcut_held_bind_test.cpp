#include <game/client/components/binds.h>
#include <game/client/components/qmclient/shortcut_held_bind.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(QmShortcutHeldBind, HeldPanelsSurviveModifierUntilPhysicalKeyRelease)
{
	for(const char *pCommand : {"+scoreboard", "+spectate", "+statboard"})
	{
		SCOPED_TRACE(pCommand);
		CQmShortcutHeldBind State;
		std::vector<std::string> vReleased;
		EXPECT_STREQ(State.Command(pCommand), pCommand);
		ASSERT_TRUE(State.Restrict(pCommand, [&](const char *pReleased) { vReleased.emplace_back(pReleased); }));
		EXPECT_TRUE(vReleased.empty());
		// 重复按键与每帧刷新使用保留命令；实际松键仍能取得原面板的释放命令。
		EXPECT_STREQ(State.Command(pCommand), pCommand);
		EXPECT_TRUE(State.Restrict(pCommand, [&](const char *pReleased) { vReleased.emplace_back(pReleased); }));
		EXPECT_TRUE(vReleased.empty());
		EXPECT_STREQ(State.Command(nullptr), pCommand);
	}
}

TEST(QmShortcutHeldBind, EmoteWheelIsReleasedOnceAndCannotReopenOnRepeat)
{
	CQmShortcutHeldBind State;
	std::vector<std::string> vReleased;
	EXPECT_FALSE(State.Restrict("+emote", [&](const char *pCommand) { vReleased.emplace_back(pCommand); }));
	ASSERT_EQ(vReleased.size(), 1u);
	EXPECT_EQ(vReleased[0], "+emote");
	EXPECT_STREQ(State.Command("+emote"), "");
	EXPECT_FALSE(State.Restrict("+emote", [&](const char *pCommand) { vReleased.emplace_back(pCommand); }));
	EXPECT_EQ(vReleased.size(), 1u);
}

TEST(QmShortcutHeldBind, CompositeBindKeepsPanelAndCancelsWheelIndependently)
{
	CQmShortcutHeldBind State;
	std::vector<std::string> vReleased;
	const char *pOriginal = "echo \"+scoreboard; +spectate\"; +emote; +spectate; +fire";
	ASSERT_TRUE(State.Restrict(pOriginal, [&](const char *pCommand) { vReleased.emplace_back(pCommand); }));
	ASSERT_EQ(vReleased.size(), 2u);
	EXPECT_EQ(vReleased[0], "+emote");
	EXPECT_EQ(vReleased[1], "+fire");
	// 被取消的轮盘和输入命令不会随重复按键或真实松键再次执行。
	EXPECT_STREQ(State.Command(pOriginal), "+spectate");
}

TEST(QmShortcutHeldBind, QuotedNestedPanelCommandDoesNotPreserveTheBinding)
{
	CQmShortcutHeldBind State;
	EXPECT_FALSE(State.Restrict("bind x \"+scoreboard\"; echo \"+spectate\"", [](const char *) {}));
	EXPECT_STREQ(State.Command("+emote"), "");
}

TEST(QmShortcutHeldBind, CallbackCanReplaceBindingWithoutInvalidatingParsing)
{
	CQmShortcutHeldBind State;
	std::string Original = "+emote; +spectate; +fire";
	std::vector<std::string> vReleased;
	ASSERT_TRUE(State.Restrict(Original.c_str(), [&](const char *pCommand) {
		vReleased.emplace_back(pCommand);
		Original.clear();
	}));
	EXPECT_EQ(vReleased, (std::vector<std::string>{"+emote", "+fire"}));
	// 已经按住的面板仍需在真实松键时关闭，即使期间重绑或清空了按键。
	EXPECT_STREQ(State.Command(Original.c_str()), "+spectate");
}

TEST(QmShortcutHeldBind, NewPressDoesNotInheritPreviousShortcutRestriction)
{
	CQmShortcutHeldBind Previous;
	ASSERT_TRUE(Previous.Restrict("+spectate; +emote", [](const char *) {}));
	CQmShortcutHeldBind Next;
	EXPECT_FALSE(Next.Restricted());
	EXPECT_STREQ(Next.Command("+spectate; +emote"), "+spectate; +emote");
}

TEST(QmShortcutHeldBind, ControlBeforeShiftDoesNotOpenUnmodifiedPanelOrWheel)
{
	for(int Key : {KEY_LSHIFT, KEY_RSHIFT})
	{
		SCOPED_TRACE(Key);
		EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(Key, 1 << KeyModifier::CTRL));
		EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_C, (1 << KeyModifier::CTRL) | (1 << KeyModifier::SHIFT)));
	}
}

TEST(QmShortcutHeldBind, FocusLossPreservesOnlyPresentationAndReleasesGameplayImmediately)
{
	CQmShortcutHeldBind State;
	std::vector<std::string> Released;
	ASSERT_TRUE(State.ReleaseWhileUnfocused("+scoreboard; +fire; +left; +emote", [&](const char *pCommand) { Released.emplace_back(pCommand); }));
	EXPECT_EQ(Released, (std::vector<std::string>{"+fire", "+left", "+emote"}));
	EXPECT_STREQ(State.Command(nullptr), "+scoreboard");
	EXPECT_TRUE(State.WaitingForFocusReturn());
	EXPECT_FALSE(State.ReleaseOnFocusReturn(false));
	EXPECT_TRUE(State.ReleaseOnFocusReturn(true));
}

TEST(QmShortcutHeldBind, FocusLossAfterScreenshotChordStillPreservesSpectator)
{
	CQmShortcutHeldBind State;
	ASSERT_TRUE(State.Restrict("+spectate", [](const char *) {}));
	int Releases = 0;
	ASSERT_TRUE(State.ReleaseWhileUnfocused("+spectate", [&](const char *) { ++Releases; }));
	EXPECT_EQ(Releases, 0);
	EXPECT_STREQ(State.Command(nullptr), "+spectate");
	EXPECT_TRUE(State.ReleaseOnFocusReturn(true));
}

TEST(QmShortcutHeldBind, FocusLossWithoutPanelDoesNotKeepAnyInput)
{
	CQmShortcutHeldBind State;
	int Releases = 0;
	EXPECT_FALSE(State.ReleaseWhileUnfocused("+fire; +emote", [&](const char *) { ++Releases; }));
	EXPECT_EQ(Releases, 2);
	EXPECT_FALSE(State.WaitingForFocusReturn());
	EXPECT_FALSE(State.ReleaseOnFocusReturn(true));
	EXPECT_STREQ(State.Command(nullptr), "");
}
