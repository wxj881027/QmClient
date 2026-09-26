#include <game/client/components/qmclient/qm_markdown_broadcast.h>

#include <gtest/gtest.h>

TEST(QmMarkdownBroadcast, UpdatesOnlyForNewerOrChangedPayloads)
{
	CQmMarkdownBroadcast Broadcast;
	EXPECT_TRUE(Broadcast.Apply("first", 2));
	EXPECT_EQ(Broadcast.Revision(), 1);
	EXPECT_FALSE(Broadcast.Apply("first", 2));
	EXPECT_FALSE(Broadcast.Apply("stale", 1));
	EXPECT_TRUE(Broadcast.Apply("changed", 2));
	EXPECT_EQ(Broadcast.Revision(), 2);
	EXPECT_STREQ(Broadcast.Markdown(), "changed");
}

TEST(QmMarkdownBroadcast, RejectsOversizedPayloadWithoutDiscardingCurrentContent)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("kept", 1));
	EXPECT_FALSE(Broadcast.Apply(std::string(64 * 1024 + 1, 'x'), 2));
	EXPECT_STREQ(Broadcast.Markdown(), "kept");
	EXPECT_EQ(Broadcast.Version(), 1);
}
