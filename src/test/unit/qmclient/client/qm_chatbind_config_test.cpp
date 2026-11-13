#include <game/client/components/qmclient/chat_bind_config.h>

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

TEST(QmChatBindConfig, EmptyRuntimeAndDefaultsProduceNoLines)
{
	EXPECT_TRUE(QmPrepareChatBindConfig({}, {}).empty());
}

TEST(QmChatBindConfig, SortedOutputDoesNotReorderRuntimeBindings)
{
	const std::vector<CBindChat::CBind> vBinds{Bind("!test", "echo lower"), Bind("!TEST", "echo upper")};
	const auto *pFirst = vBinds.data();
	const auto Lines = QmPrepareChatBindConfig(vBinds, {});
	ASSERT_EQ(Lines.size(), 2u);
	EXPECT_EQ(Lines[0], "bindchat \"!TEST\" \"echo upper\"");
	EXPECT_EQ(Lines[1], "bindchat \"!test\" \"echo lower\"");
	EXPECT_EQ(vBinds.data(), pFirst);
	EXPECT_STREQ(vBinds[0].m_aName, "!test");
	EXPECT_STREQ(vBinds[0].m_aCommand, "echo lower");
	EXPECT_STREQ(vBinds[1].m_aName, "!TEST");
}

TEST(QmChatBindConfig, UnchangedDefaultsAreOmitted)
{
	const auto Default = Bind("!default", "echo default");
	EXPECT_TRUE(QmPrepareChatBindConfig({Default}, {std::cref(Default)}).empty());
}

TEST(QmChatBindConfig, OverriddenDefaultProducesOnlyItsBinding)
{
	const auto Default = Bind("!default", "echo default");
	const auto Lines = QmPrepareChatBindConfig({Bind("!default", "echo custom")}, {std::cref(Default)});
	ASSERT_EQ(Lines.size(), 1u);
	EXPECT_EQ(Lines[0], "bindchat \"!default\" \"echo custom\"");
}

TEST(QmChatBindConfig, MissingDefaultProducesAnEscapedUnbindLine)
{
	const auto Default = Bind("!a\"b", "echo default");
	const auto Lines = QmPrepareChatBindConfig({}, {std::cref(Default)});
	ASSERT_EQ(Lines.size(), 1u);
	EXPECT_EQ(Lines[0], "unbindchat \"!a\\\"b\"");
}

TEST(QmChatBindConfig, ExistingCaseVariantDefaultOmissionPolicyIsPreserved)
{
	const auto Default = Bind("!default", "echo default");
	EXPECT_TRUE(QmPrepareChatBindConfig({Bind("!DEFAULT", "echo default")}, {std::cref(Default)}).empty());
}

TEST(QmChatBindConfig, MaximumEscapedNameAndCommandDoNotTruncateTheLine)
{
	const std::string Name(63, '\\'), Command(1023, '\"');
	const auto Lines = QmPrepareChatBindConfig({Bind(Name.c_str(), Command.c_str())}, {});
	ASSERT_EQ(Lines.size(), 1u);
	EXPECT_EQ(Lines[0].size(), 2186u);
	EXPECT_GT(Lines[0].size(), BINDCHAT_MAX_CMD * 2u - 1);
	EXPECT_EQ(Lines[0].back(), '\"');
}

TEST(QmChatBindConfig, PreparedLinesOwnTheirSnapshotAfterSourceMutation)
{
	std::vector<CBindChat::CBind> vBinds{Bind("!original", "echo original")};
	auto Default = Bind("!missing", "echo missing");
	const auto Lines = QmPrepareChatBindConfig(vBinds, {std::cref(Default)});
	vBinds.clear();
	str_copy(Default.m_aName, "!changed");
	ASSERT_EQ(Lines.size(), 2u);
	EXPECT_EQ(Lines[0], "bindchat \"!original\" \"echo original\"");
	EXPECT_EQ(Lines[1], "unbindchat \"!missing\"");
}

TEST(QmChatBindConfig, PreparationAllocationFailuresDoNotMutateRuntimeBindings)
{
	const std::string Name(63, '\\'), Command(1023, '\"');
	const std::vector<CBindChat::CBind> vBinds{Bind(Name.c_str(), Command.c_str()), Bind("!a", "echo a")};
	const auto *pStorage = vBinds.data();
	for(size_t BeforeFailure : {0u, 1u, 2u})
	{
		SCOPED_TRACE(BeforeFailure);
		bool Threw = false, Triggered;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			try
			{
				(void)QmPrepareChatBindConfig(vBinds, {});
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Fault.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_TRUE(Threw);
		ASSERT_EQ(vBinds.size(), 2u);
		EXPECT_EQ(vBinds.data(), pStorage);
		EXPECT_STREQ(vBinds[0].m_aName, Name.c_str());
		EXPECT_STREQ(vBinds[0].m_aCommand, Command.c_str());
		EXPECT_STREQ(vBinds[1].m_aName, "!a");
	}
}
