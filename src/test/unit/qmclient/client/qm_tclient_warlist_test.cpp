#include <game/client/components/qmclient/player_display_privacy.h>

#include <gtest/gtest.h>

using QmPlayerDisplayPrivacy::Resolve;
using QmPlayerDisplayPrivacy::SPlayer;
using QmPlayerDisplayPrivacy::SSettings;

TEST(QmWarListPlayerPrivacy, DefaultSettingsKeepEnemyVisible)
{
	const auto Decisions = Resolve({}, {7, false, false, true});
	EXPECT_FALSE(Decisions.m_HideIdentity);
	EXPECT_FALSE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, FullBlockHidesEnemyIdentitySkinAndChat)
{
	const SSettings Settings{false, false, true};
	const auto Decisions = Resolve(Settings, {7, false, false, true});
	EXPECT_TRUE(Decisions.m_HideIdentity);
	EXPECT_TRUE(Decisions.m_HideSkin);
	EXPECT_TRUE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, FullBlockKeepsOtherPlayersVisible)
{
	const SSettings Settings{false, false, true};
	for(bool Friend : {false, true})
	{
		const auto Decisions = Resolve(Settings, {7, false, Friend, false});
		EXPECT_FALSE(Decisions.m_HideIdentity);
		EXPECT_FALSE(Decisions.m_HideSkin);
		EXPECT_FALSE(Decisions.m_BlockChat);
	}
}

TEST(QmWarListPlayerPrivacy, EnemyBlockOverridesFriendExemption)
{
	const SSettings Settings{false, false, true};
	const auto Decisions = Resolve(Settings, {7, false, true, true});
	EXPECT_TRUE(Decisions.m_HideIdentity);
	EXPECT_TRUE(Decisions.m_HideSkin);
	EXPECT_TRUE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, LocalPlayersRemainVisibleWithEveryPrivacyOptionEnabled)
{
	const SSettings Settings{true, true, true};
	for(int ClientId : {0, 1})
	{
		const auto Decisions = Resolve(Settings, {ClientId, true, false, true});
		EXPECT_FALSE(Decisions.m_HideIdentity);
		EXPECT_FALSE(Decisions.m_HideSkin);
		EXPECT_FALSE(Decisions.m_BlockChat);
	}
}

TEST(QmWarListPlayerPrivacy, InvalidClientIdsDoNotProducePrivacyDecisions)
{
	const SSettings Settings{true, true, true};
	for(int ClientId : {-1, static_cast<int>(MAX_CLIENTS)})
	{
		const auto Decisions = Resolve(Settings, {ClientId, false, false, true});
		EXPECT_FALSE(Decisions.m_HideIdentity);
		EXPECT_FALSE(Decisions.m_HideSkin);
		EXPECT_FALSE(Decisions.m_BlockChat);
	}
}

TEST(QmWarListPlayerPrivacy, DisablingFullBlockRestoresEnemyVisibility)
{
	SSettings Settings{false, false, true};
	const SPlayer Player{7, false, false, true};
	ASSERT_TRUE(Resolve(Settings, Player).m_BlockChat);
	Settings.m_BlockEnemies = false;
	const auto Decisions = Resolve(Settings, Player);
	EXPECT_FALSE(Decisions.m_HideIdentity);
	EXPECT_FALSE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, StreamerNameOptionKeepsSkinsAndChatVisible)
{
	const auto Decisions = Resolve({true, false, false}, {7, false, false, false});
	EXPECT_TRUE(Decisions.m_HideIdentity);
	EXPECT_FALSE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, StreamerSkinOptionKeepsNamesAndChatVisible)
{
	const auto Decisions = Resolve({false, true, false}, {7, false, false, false});
	EXPECT_FALSE(Decisions.m_HideIdentity);
	EXPECT_TRUE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, StreamerOptionsKeepNonEnemyFriendsVisible)
{
	const auto Decisions = Resolve({true, true, true}, {7, false, true, false});
	EXPECT_FALSE(Decisions.m_HideIdentity);
	EXPECT_FALSE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}

TEST(QmWarListPlayerPrivacy, DisablingEnemyBlockPreservesStreamerOptions)
{
	SSettings Settings{true, true, true};
	const SPlayer Player{7, false, false, true};
	ASSERT_TRUE(Resolve(Settings, Player).m_BlockChat);
	Settings.m_BlockEnemies = false;
	const auto Decisions = Resolve(Settings, Player);
	EXPECT_TRUE(Decisions.m_HideIdentity);
	EXPECT_TRUE(Decisions.m_HideSkin);
	EXPECT_FALSE(Decisions.m_BlockChat);
}
