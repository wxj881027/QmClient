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

TEST(QmMarkdownBroadcast, ReadRevisionSurvivesReloadAndSameVersionEditsBecomeUnread)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("release and known issues", 3));
	EXPECT_TRUE(Broadcast.IsUnread(-1, ""));
	const std::string ReadId = Broadcast.ContentId();
	EXPECT_FALSE(Broadcast.IsUnread(3, ReadId.c_str()));
	CQmMarkdownBroadcast Reloaded;
	ASSERT_TRUE(Reloaded.Apply("release and known issues", 3));
	EXPECT_FALSE(Reloaded.IsUnread(3, ReadId.c_str()));
	ASSERT_TRUE(Reloaded.Apply("release and corrected known issues", 3));
	EXPECT_TRUE(Reloaded.IsUnread(3, ReadId.c_str()));
	EXPECT_FALSE(Reloaded.IsUnread(4, ReadId.c_str()));
}

TEST(QmMarkdownBroadcast, EmptyAndRejectedUpdatesDoNotCreateUnreadNotice)
{
	CQmMarkdownBroadcast Broadcast;
	EXPECT_FALSE(Broadcast.IsUnread(-1, ""));
	ASSERT_TRUE(Broadcast.Apply("notice", 2));
	const std::string ReadId = Broadcast.ContentId();
	EXPECT_FALSE(Broadcast.Apply("old notice", 1));
	EXPECT_FALSE(Broadcast.IsUnread(2, ReadId.c_str()));
	ASSERT_TRUE(Broadcast.Apply("", 3));
	EXPECT_FALSE(Broadcast.IsUnread(2, ReadId.c_str()));
}

TEST(QmNewsNotice, WaitsForIdleOfflineStartMenu)
{
	EXPECT_TRUE(QmNewsShouldAnnounce(true, true, false, true, false));
	EXPECT_TRUE(QmNewsShouldAnnounce(true, true, false, false, true));
	EXPECT_FALSE(QmNewsShouldAnnounce(true, true, false, false, false));
	EXPECT_FALSE(QmNewsShouldAnnounce(false, true, false, true, true));
	EXPECT_FALSE(QmNewsShouldAnnounce(true, false, false, true, true));
	EXPECT_FALSE(QmNewsShouldAnnounce(true, true, true, true, true));
}
