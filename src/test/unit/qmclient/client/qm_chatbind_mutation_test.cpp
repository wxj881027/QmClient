#include <game/client/components/tclient/bindchat.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	CBindChat::CBind Bind(const char *pName, const char *pCommand)
	{
		CBindChat::CBind Result{};
		str_copy(Result.m_aName, pName);
		str_copy(Result.m_aCommand, pCommand);
		return Result;
	}
}

TEST(QmChatBindMutation, NewBindingIsPublishedWithItsNameAndCommand)
{
	std::vector<CBindChat::CBind> vBinds;
	ASSERT_TRUE(QmPublishChatBind(vBinds, Bind("!new", "echo new")));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "!new");
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo new");
}

TEST(QmChatBindMutation, InitialAllocationFailureKeepsTheListEmptyAndAllowsRetry)
{
	std::vector<CBindChat::CBind> vBinds;
	const auto Candidate = Bind("!new", "echo new");
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Published = QmPublishChatBind(vBinds, Candidate);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Published);
	EXPECT_TRUE(vBinds.empty());
	ASSERT_TRUE(QmPublishChatBind(vBinds, Candidate));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo new");
}

TEST(QmChatBindMutation, GrowthFailureKeepsExistingEntriesAndTheirBorrow)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!old", "echo old")};
	vBinds.resize(vBinds.capacity());
	const auto *pStorage = vBinds.data();
	const size_t Size = vBinds.size();
	const auto Candidate = Bind("!new", "echo new");
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Published = QmPublishChatBind(vBinds, Candidate);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Published);
	ASSERT_EQ(vBinds.size(), Size);
	EXPECT_EQ(vBinds.data(), pStorage);
	EXPECT_STREQ(vBinds[0].m_aName, "!old");
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo old");
}

TEST(QmChatBindMutation, ReplacingANameNeedsNoAllocationAndMovesItToTheEnd)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!a", "old a"), Bind("!b", "old b")};
	const auto Candidate = Bind("!a", "new a");
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Published = QmPublishChatBind(vBinds, Candidate);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Published);
	EXPECT_FALSE(Triggered);
	ASSERT_EQ(vBinds.size(), 2u);
	EXPECT_STREQ(vBinds[0].m_aName, "!b");
	EXPECT_STREQ(vBinds[1].m_aCommand, "new a");
}

TEST(QmChatBindMutation, AReplacementMayBorrowTheEntryThatItRemoves)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!a", "echo a"), Bind("!b", "echo b")};
	ASSERT_TRUE(QmPublishChatBind(vBinds, vBinds[0]));
	ASSERT_EQ(vBinds.size(), 2u);
	EXPECT_STREQ(vBinds[0].m_aName, "!b");
	EXPECT_STREQ(vBinds[1].m_aName, "!a");
	EXPECT_STREQ(vBinds[1].m_aCommand, "echo a");
}

TEST(QmChatBindMutation, DefaultRenameFindsTheCurrentCommandCaseInsensitively)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!other", "echo other"), Bind("!old", "ECHO target")};
	const auto Default = Bind("!default", "echo target");
	ASSERT_TRUE(QmPublishDefaultChatBindName(vBinds, Default, "!new"));
	ASSERT_EQ(vBinds.size(), 2u);
	EXPECT_STREQ(vBinds[0].m_aName, "!other");
	EXPECT_STREQ(vBinds[1].m_aName, "!new");
	EXPECT_STREQ(vBinds[1].m_aCommand, "ECHO target");
}

TEST(QmChatBindMutation, ClearingADefaultWithADuplicateNameRemovesOnlyItsCommand)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!same", "echo other"), Bind("!same", "echo target")};
	const auto Default = Bind("!default", "echo target");
	ASSERT_TRUE(QmPublishDefaultChatBindName(vBinds, Default, ""));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "!same");
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo other");
}

TEST(QmChatBindMutation, EmptyNameForAMissingDefaultDoesNotChangeOtherEntries)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!other", "echo other")};
	const auto Default = Bind("!default", "echo target");
	ASSERT_TRUE(QmPublishDefaultChatBindName(vBinds, Default, ""));
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo other");
}

TEST(QmChatBindMutation, MissingDefaultAllocationFailureKeepsTheExistingList)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!other", "echo other")};
	vBinds.resize(vBinds.capacity());
	const size_t Size = vBinds.size();
	const auto Default = Bind("!default", "echo target");
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Published = QmPublishDefaultChatBindName(vBinds, Default, "!new");
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Published);
	ASSERT_EQ(vBinds.size(), Size);
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo other");
}

TEST(QmChatBindMutation, MissingDefaultReplacesANameCollisionWithoutAllocation)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!same", "echo other")};
	const auto Default = Bind("!default", "echo target");
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Published = QmPublishDefaultChatBindName(vBinds, Default, "!same");
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Published);
	EXPECT_FALSE(Triggered);
	ASSERT_EQ(vBinds.size(), 1u);
	EXPECT_STREQ(vBinds[0].m_aName, "!same");
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo target");
}
