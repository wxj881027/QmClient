#include <game/client/components/qmclient/vote_menu_mode.h>

#include <gtest/gtest.h>

#include <initializer_list>

TEST(QmVoteMenuMode, UnknownServerStartsWithOriginalOptions)
{
	CQmVoteMenuMode Mode;
	EXPECT_FALSE(Mode.IsOfficialDdnet());
	EXPECT_FALSE(Mode.UseMapLibrary());
}

TEST(QmVoteMenuMode, OtherCommunitiesCannotEnableMapLibrary)
{
	for(const char *pCommunity : {"", "none", "axiom", "infc", "mmotee", "kog", "ddnet-custom"})
	{
		SCOPED_TRACE(pCommunity);
		CQmVoteMenuMode Mode;
		Mode.UpdateCommunity(pCommunity);
		EXPECT_FALSE(Mode.IsOfficialDdnet());
		EXPECT_FALSE(Mode.UseMapLibrary());
		EXPECT_FALSE(Mode.ToggleServerOptions());
		EXPECT_FALSE(Mode.UseMapLibrary());
	}
}

TEST(QmVoteMenuMode, OfficialCommunityEnablesMapLibraryWhenIdentityArrives)
{
	CQmVoteMenuMode Mode;
	Mode.UpdateCommunity("none");
	ASSERT_TRUE(Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET));
	EXPECT_TRUE(Mode.IsOfficialDdnet());
	EXPECT_TRUE(Mode.UseMapLibrary());
}

TEST(QmVoteMenuMode, OfficialServerCanSwitchToOriginalOptionsAndBack)
{
	CQmVoteMenuMode Mode;
	Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET);
	ASSERT_TRUE(Mode.ToggleServerOptions());
	EXPECT_TRUE(Mode.IsOfficialDdnet());
	EXPECT_FALSE(Mode.UseMapLibrary());
	ASSERT_TRUE(Mode.ToggleServerOptions());
	EXPECT_TRUE(Mode.UseMapLibrary());
}

TEST(QmVoteMenuMode, RepeatedCommunityUpdatePreservesManualSelection)
{
	CQmVoteMenuMode Mode;
	Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET);
	Mode.ToggleServerOptions();
	EXPECT_FALSE(Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET));
	EXPECT_FALSE(Mode.UseMapLibrary());
}

TEST(QmVoteMenuMode, LosingOfficialIdentityReturnsToOriginalOptions)
{
	CQmVoteMenuMode Mode;
	Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET);
	ASSERT_TRUE(Mode.UpdateCommunity("none"));
	EXPECT_FALSE(Mode.IsOfficialDdnet());
	EXPECT_FALSE(Mode.UseMapLibrary());
	EXPECT_FALSE(Mode.ToggleServerOptions());
}

TEST(QmVoteMenuMode, NewConnectionResetsOfficialServerOverride)
{
	CQmVoteMenuMode Mode;
	Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET);
	Mode.ToggleServerOptions();
	Mode.Reset();
	EXPECT_FALSE(Mode.UseMapLibrary());
	EXPECT_FALSE(Mode.IsOfficialDdnet());
	Mode.UpdateCommunity(IServerBrowser::COMMUNITY_DDNET);
	EXPECT_TRUE(Mode.UseMapLibrary());
}
