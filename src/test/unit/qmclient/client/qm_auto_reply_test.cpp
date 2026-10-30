#include <game/client/components/qmclient/auto_reply.h>

#include <gtest/gtest.h>

using qm_auto_reply::CSendState;
using qm_auto_reply::EKind;

TEST(QmAutoReply, EscapesWhisperRecipientQuotesAndBackslashes)
{
	EXPECT_EQ(qm_auto_reply::Whisper("A\" B\\C", "hello"), "/w \"A\\\" B\\\\C\" hello");
}

TEST(QmAutoReply, IncomingWhispersReachBothConnectionsWithoutDuplicatingPublicChat)
{
	for(int Active = 0; Active < 2; ++Active)
	{
		SCOPED_TRACE(Active);
		EXPECT_TRUE(qm_auto_reply::ShouldHandle(Active, Active, 0, false));
		EXPECT_FALSE(qm_auto_reply::ShouldHandle(!Active, Active, 0, false));
		EXPECT_TRUE(qm_auto_reply::ShouldHandle(!Active, Active, TEAM_WHISPER_RECV, false));
		EXPECT_FALSE(qm_auto_reply::ShouldHandle(Active, Active, TEAM_WHISPER_SEND, false));
		EXPECT_FALSE(qm_auto_reply::ShouldHandle(Active, Active, TEAM_WHISPER_RECV, true));
	}
}

TEST(QmAutoReply, SuccessfulStatusReplyIsRememberedBeforeServerEcho)
{
	CSendState State;
	int Sends = 0;
	const auto Send = [&] { ++Sends; return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::AWAY, "away", 0, 3, Send));
	EXPECT_FALSE(State.TrySend(0, 1, "Alice", EKind::AWAY, "away", 4, 3, Send));
	EXPECT_EQ(Sends, 1);
}

TEST(QmAutoReply, AlternatingSendersDoNotDefeatStatusDeduplication)
{
	CSendState State;
	const auto Send = [] { return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::MUTED, "muted", 0, 0, Send));
	EXPECT_TRUE(State.TrySend(0, 2, "Bob", EKind::MUTED, "muted", 0, 0, Send));
	EXPECT_FALSE(State.TrySend(0, 1, "Alice", EKind::MUTED, "muted", 0, 0, Send));
}

TEST(QmAutoReply, FailedSendDoesNotConsumeCooldownOrDeduplication)
{
	CSendState State;
	EXPECT_FALSE(State.TrySend(1, 1, "Alice", EKind::AWAY, "away", 0, 3, [] { return false; }));
	EXPECT_TRUE(State.TrySend(1, 1, "Alice", EKind::AWAY, "away", 0, 3, [] { return true; }));
}

TEST(QmAutoReply, CooldownIsSharedByReplyKindsAndConnections)
{
	CSendState State;
	const auto Send = [] { return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::KEYWORD, "yes", 0, 3, Send));
	EXPECT_FALSE(State.TrySend(1, 2, "Bob", EKind::AWAY, "away", 2, 3, Send));
	EXPECT_TRUE(State.TrySend(1, 2, "Bob", EKind::AWAY, "away", 3, 3, Send));
}

TEST(QmAutoReply, ReturningToWindowAllowsReplyDuringNextAwaySession)
{
	CSendState State;
	const auto Send = [] { return true; };
	State.UpdateStatus(true, true);
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::AWAY, "away", 0, 0, Send));
	EXPECT_TRUE(State.TrySend(0, 2, "Bob", EKind::MUTED, "muted", 0, 0, Send));
	State.UpdateStatus(true, false);
	State.UpdateStatus(true, true);
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::AWAY, "away", 1, 0, Send));
	EXPECT_FALSE(State.TrySend(0, 2, "Bob", EKind::MUTED, "muted", 1, 0, Send));
}

TEST(QmAutoReply, RecipientIdentityAndConnectionArePartOfDeduplication)
{
	CSendState State;
	const auto Send = [] { return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::MUTED, "muted", 0, 0, Send));
	EXPECT_TRUE(State.TrySend(0, 1, "Bob", EKind::MUTED, "muted", 0, 0, Send));
	EXPECT_TRUE(State.TrySend(1, 1, "Bob", EKind::MUTED, "muted", 0, 0, Send));
}

TEST(QmAutoReply, ResetAllowsFirstReplyInNewServerSession)
{
	CSendState State;
	const auto Send = [] { return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::MUTED, "muted", 100, 3, Send));
	State.Reset();
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::MUTED, "muted", 0, 3, Send));
}

TEST(QmAutoReply, KeywordRepliesCanRepeatAfterCooldown)
{
	CSendState State;
	const auto Send = [] { return true; };
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::KEYWORD, "yes", 0, 3, Send));
	EXPECT_TRUE(State.TrySend(0, 1, "Alice", EKind::KEYWORD, "yes", 3, 3, Send));
}
