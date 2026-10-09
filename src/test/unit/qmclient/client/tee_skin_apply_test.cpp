#include <game/client/components/qmclient/tee_skin_apply.h>

#include <gtest/gtest.h>

namespace
{
	CConfig SkinConfig()
	{
		CConfig Config{};
		str_copy(Config.m_ClPlayerSkin, "bluekitty");
		str_copy(Config.m_ClDummySkin, "default");
		Config.m_ClPlayerUseCustomColor = 1;
		Config.m_ClPlayerColorBody = 123;
		Config.m_ClPlayerColorFeet = 456;
		Config.m_ClDummyUseCustomColor = 0;
		Config.m_ClDummyColorBody = 789;
		Config.m_ClDummyColorFeet = 987;
		return Config;
	}
}

TEST(TeeSkinApply, CopyPlayerToDummyKeepsPlayerAndTransfersAllSkinSettings)
{
	CConfig Config = SkinConfig();
	QmCopyTeeSkinSettings(Config, ETeeSkinApplyTarget::DUMMY);
	EXPECT_STREQ(Config.m_ClDummySkin, "bluekitty");
	EXPECT_EQ(Config.m_ClDummyUseCustomColor, 1);
	EXPECT_EQ(Config.m_ClDummyColorBody, 123u);
	EXPECT_EQ(Config.m_ClDummyColorFeet, 456u);
	EXPECT_STREQ(Config.m_ClPlayerSkin, "bluekitty");
	EXPECT_EQ(Config.m_ClPlayerUseCustomColor, 1);
	EXPECT_EQ(Config.m_ClPlayerColorBody, 123u);
	EXPECT_EQ(Config.m_ClPlayerColorFeet, 456u);
}

TEST(TeeSkinApply, CopyDummyToPlayerPreservesDisabledColorValuesForLaterUse)
{
	CConfig Config = SkinConfig();
	QmCopyTeeSkinSettings(Config, ETeeSkinApplyTarget::MAIN);
	QmCopyTeeSkinSettings(Config, ETeeSkinApplyTarget::MAIN);
	EXPECT_STREQ(Config.m_ClPlayerSkin, "default");
	EXPECT_EQ(Config.m_ClPlayerUseCustomColor, 0);
	EXPECT_EQ(Config.m_ClPlayerColorBody, 789u);
	EXPECT_EQ(Config.m_ClPlayerColorFeet, 987u);
	EXPECT_STREQ(Config.m_ClDummySkin, "default");
	EXPECT_EQ(Config.m_ClDummyUseCustomColor, 0);
	EXPECT_EQ(Config.m_ClDummyColorBody, 789u);
	EXPECT_EQ(Config.m_ClDummyColorFeet, 987u);
}

TEST(TeeSkinApply, SwapTransfersBothAppearancesAndSwappingAgainRestoresThem)
{
	CConfig Config = SkinConfig();
	QmSwapTeeSkinSettings(Config);
	EXPECT_STREQ(Config.m_ClPlayerSkin, "default");
	EXPECT_STREQ(Config.m_ClDummySkin, "bluekitty");
	EXPECT_EQ(Config.m_ClPlayerUseCustomColor, 0);
	EXPECT_EQ(Config.m_ClDummyUseCustomColor, 1);
	EXPECT_EQ(Config.m_ClPlayerColorBody, 789u);
	EXPECT_EQ(Config.m_ClPlayerColorFeet, 987u);
	EXPECT_EQ(Config.m_ClDummyColorBody, 123u);
	EXPECT_EQ(Config.m_ClDummyColorFeet, 456u);
	QmSwapTeeSkinSettings(Config);
	EXPECT_STREQ(Config.m_ClPlayerSkin, "bluekitty");
	EXPECT_STREQ(Config.m_ClDummySkin, "default");
	EXPECT_EQ(Config.m_ClPlayerUseCustomColor, 1);
	EXPECT_EQ(Config.m_ClDummyUseCustomColor, 0);
	EXPECT_EQ(Config.m_ClPlayerColorBody, 123u);
	EXPECT_EQ(Config.m_ClPlayerColorFeet, 456u);
	EXPECT_EQ(Config.m_ClDummyColorBody, 789u);
	EXPECT_EQ(Config.m_ClDummyColorFeet, 987u);
}

TEST(TeeSkinApply, TransfersKeepIdentityEyesAndControlledCharacter)
{
	CConfig Config = SkinConfig();
	str_copy(Config.m_PlayerName, "main");
	str_copy(Config.m_ClDummyName, "dummy");
	Config.m_ClPlayerDefaultEyes = 2;
	Config.m_ClDummyDefaultEyes = 4;
	Config.m_ClDummy = 1;
	QmCopyTeeSkinSettings(Config, ETeeSkinApplyTarget::DUMMY);
	QmSwapTeeSkinSettings(Config);
	EXPECT_STREQ(Config.m_PlayerName, "main");
	EXPECT_STREQ(Config.m_ClDummyName, "dummy");
	EXPECT_EQ(Config.m_ClPlayerDefaultEyes, 2);
	EXPECT_EQ(Config.m_ClDummyDefaultEyes, 4);
	EXPECT_EQ(Config.m_ClDummy, 1);
}
