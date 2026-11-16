#include <game/client/components/qmclient/favorite_map_vote.h>

#include <gtest/gtest.h>

TEST(QmFavoriteMapVote, OnlineDoubleClickVotesForTheExactMap)
{
	EXPECT_EQ(QmFavoriteMapVoteCommand("Tee Legends 3", true, true), "/map \"Tee Legends 3\"");
	EXPECT_EQ(QmFavoriteMapVoteCommand("地图", true, true), "/map \"地图\"");
	EXPECT_EQ(QmFavoriteMapVoteCommand("A\\B\"C", true, true), "/map \"A\\\\B\\\"C\"");
}

TEST(QmFavoriteMapVote, SingleClickAndOfflineDoubleClickDoNotVote)
{
	EXPECT_TRUE(QmFavoriteMapVoteCommand("ATLAS", true, false).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand("ATLAS", false, true).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand("ATLAS", false, false).empty());
}

TEST(QmFavoriteMapVote, InvalidNamesDoNotSendPartialCommands)
{
	EXPECT_TRUE(QmFavoriteMapVoteCommand("", true, true).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand("Map\nOther", true, true).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand(std::string("Map\0Other", 9), true, true).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand(std::string(MAX_MAP_LENGTH, 'a'), true, true).empty());
	EXPECT_TRUE(QmFavoriteMapVoteCommand(std::string(MAX_MAP_LENGTH - 1, '"'), true, true).empty());
	const std::string LongName(MAX_MAP_LENGTH - 1, 'a');
	EXPECT_EQ(QmFavoriteMapVoteCommand(LongName, true, true), "/map \"" + LongName + "\"");
}
