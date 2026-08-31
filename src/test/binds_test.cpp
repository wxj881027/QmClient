// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/binds.h>
#include <game/client/components/binds_deepfly_mode.h>

#include <gtest/gtest.h>

TEST(Binds, AllowsUnmodifiedFallbackForSingleModifierKey)
{
	EXPECT_TRUE(CBinds::AllowsUnmodifiedFallback(KEY_LSHIFT, KeyModifier::NONE));
	EXPECT_TRUE(CBinds::AllowsUnmodifiedFallback(KEY_RSHIFT, KeyModifier::NONE));
}

TEST(Binds, BlocksShiftOnlyBindFallbackForScreenshotCombinations)
{
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_LSHIFT, 1 << KeyModifier::CTRL));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_RSHIFT, 1 << KeyModifier::CTRL));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_LSHIFT, 1 << KeyModifier::ALT));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_RSHIFT, 1 << KeyModifier::ALT));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_LSHIFT, 1 << KeyModifier::GUI));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_RSHIFT, 1 << KeyModifier::GUI));
}

TEST(Binds, KeepsPlainKeyFallbackForNonScreenshotCombinations)
{
	EXPECT_TRUE(CBinds::AllowsUnmodifiedFallback(KEY_C, 1 << KeyModifier::CTRL));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_C, (1 << KeyModifier::CTRL) | (1 << KeyModifier::SHIFT)));
	EXPECT_FALSE(CBinds::AllowsUnmodifiedFallback(KEY_C, (1 << KeyModifier::GUI) | (1 << KeyModifier::SHIFT)));
}

TEST(Binds, ReleasesShiftOnlyBindWhenScreenshotModifierIsPressedLater)
{
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_LSHIFT, KeyModifier::NONE), 1 << KeyModifier::CTRL));
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_RSHIFT, KeyModifier::NONE), 1 << KeyModifier::CTRL));
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_LSHIFT, KeyModifier::NONE), 1 << KeyModifier::ALT));
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_RSHIFT, KeyModifier::NONE), 1 << KeyModifier::ALT));
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_LSHIFT, KeyModifier::NONE), 1 << KeyModifier::GUI));
	EXPECT_TRUE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_RSHIFT, KeyModifier::NONE), 1 << KeyModifier::GUI));
	EXPECT_FALSE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_C, KeyModifier::NONE), 1 << KeyModifier::CTRL));
	EXPECT_FALSE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_LSHIFT, 1 << KeyModifier::SHIFT), 1 << KeyModifier::CTRL));
	EXPECT_FALSE(CBinds::ShouldReleaseUnmodifiedModifierBindOnModifierPress(CBindSlot(KEY_LSHIFT, KeyModifier::NONE), (1 << KeyModifier::CTRL) | (1 << KeyModifier::ALT)));
}

TEST(Binds, DetectsCoreDeepflyModes)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire"), DEEPFLY_MODE_NORMAL);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_HDF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_DF);
}

TEST(Binds, AllowsWhitelistedDeepflyAuxiliaryCommands)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+weapon1;+fire;+toggle cl_dummy_hammer 1 0;dummy_reset"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echo \"DF enabled\";+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echo \"DF; enabled\";+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+showhookcoll;+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echo \"HDF\";+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_HDF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+weapon1;+toggle cl_dummy_hammer 1 0;dummy_reset"), DEEPFLY_MODE_HDF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+weapon1;+fire;dummy_reset"), DEEPFLY_MODE_NORMAL);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echo \"normal fire\";+fire"), DEEPFLY_MODE_NORMAL);
}

TEST(Binds, IgnoresEmoteCommandsInDeepflyDetection)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;emote 14"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;+emote"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;emote_cycle"), DEEPFLY_MODE_DF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("emote 2;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_HDF);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;emote 5"), DEEPFLY_MODE_NORMAL);
	EXPECT_TRUE(IsDeepflyAuxiliaryCommand("emote 14"));
	EXPECT_TRUE(IsDeepflyAuxiliaryCommand("+emote"));
	EXPECT_TRUE(IsDeepflyAuxiliaryCommand("emote_cycle"));
	EXPECT_FALSE(IsDeepflyAuxiliaryCommand("emotefoo"));
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("emotefoo;+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_CUSTOM);
}

TEST(Binds, KeepsInputAndScriptCommandsCustomForDeepflyModes)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;+left"), DEEPFLY_MODE_CUSTOM);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;+jump"), DEEPFLY_MODE_CUSTOM);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;exec cfg/deepfly.cfg"), DEEPFLY_MODE_CUSTOM);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;bind mouse1 +fire"), DEEPFLY_MODE_CUSTOM);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;unbind mouse1"), DEEPFLY_MODE_CUSTOM);
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("+fire;+toggle cl_dummy_hammer 1 0;unbindall"), DEEPFLY_MODE_CUSTOM);
}

TEST(Binds, MatchesDeepflyAuxiliaryCommandsByCommandName)
{
	EXPECT_TRUE(IsDeepflyAuxiliaryCommand("echo test"));
	EXPECT_TRUE(IsDeepflyAuxiliaryCommand("echo \"DF enabled\""));
	EXPECT_FALSE(IsDeepflyAuxiliaryCommand("echofoo"));
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echofoo;+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_CUSTOM);
}
