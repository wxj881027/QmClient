// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/qmclient/chat_command_preview.h>
#include <game/client/components/qmclient/red_packet_auto_claim.h>
#include <game/client/components/tclient/fast_practice.h>
#include <game/client/components/tclient/warlist.h>

#include <gtest/gtest.h>

#include <iterator>
#include <string>

namespace
{
	int64_t TestTicks(float Seconds) { return (int64_t)(Seconds * time_freq()); }
}

TEST(QmDummySyncChatCommand, MatchesOnlySupportedCommands)
{
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand(nullptr));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/team 2"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/TEAM 2"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/TeAm 63"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/vote particle"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/VOTE PARTICLE"));
	EXPECT_TRUE(CChat::ShouldSyncDummyCommand("/VoTe PaRtIcLe"));

	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/team"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/team "));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/teamwork 2"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/vote particles"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("/vote particle on"));
	EXPECT_FALSE(CChat::ShouldSyncDummyCommand("particle"));
}

TEST(QmChatSecurity, SensitiveLoginCommandsAreClassifiedCorrectly)
{
	EXPECT_TRUE(CChat::IsSensitiveChatCommand("/login secret"));
	EXPECT_TRUE(CChat::IsSensitiveChatCommand(" \t/LOGIN secret"));
	EXPECT_TRUE(CChat::IsSensitiveChatCommand("/login\tsecret"));
	EXPECT_FALSE(CChat::IsSensitiveChatCommand("/login"));
	EXPECT_FALSE(CChat::IsSensitiveChatCommand("/login "));
	EXPECT_FALSE(CChat::IsSensitiveChatCommand("/login-secret"));
	EXPECT_FALSE(CChat::IsSensitiveChatCommand("hello /login secret"));
}

TEST(QmChatEchoMerge, WindowAcceptsRepeatsInsideTheWindowOnly)
{
	const int64_t Start = TestTicks(10.0f);

	// 窗口内连续重复：合并。
	EXPECT_TRUE(CChat::EchoRepeatWithinWindow(Start, Start, 2000));
	EXPECT_TRUE(CChat::EchoRepeatWithinWindow(Start + TestTicks(1.9f), Start, 2000));
	// 正好卡在窗口边界上仍然合并。
	EXPECT_TRUE(CChat::EchoRepeatWithinWindow(Start + TestTicks(2.0f), Start, 2000));
	// 超出窗口：另一段重复，重新计数。
	EXPECT_FALSE(CChat::EchoRepeatWithinWindow(Start + TestTicks(2.01f), Start, 2000));
	// 时间倒流不能当成窗口内。
	EXPECT_FALSE(CChat::EchoRepeatWithinWindow(Start - 1, Start, 2000));
	// 窗口为 0（或负数）表示关闭合并。
	EXPECT_FALSE(CChat::EchoRepeatWithinWindow(Start, Start, 0));
	EXPECT_FALSE(CChat::EchoRepeatWithinWindow(Start, Start, -1));
	// 窗口更长时应答更久，验证窗口本身参与换算而不是写死 2 秒。
	EXPECT_TRUE(CChat::EchoRepeatWithinWindow(Start + TestTicks(5.0f), Start, 60000));
	EXPECT_FALSE(CChat::EchoRepeatWithinWindow(Start + TestTicks(5.0f), Start, 2000));
}

TEST(QmAxiomAutoLogin, ClassifiesOnlyExplicitLoginSuccessReplies)
{
	EXPECT_EQ(QmClassifyAxiomLoginReply("Login successful."), EQmAxiomLoginReply::SUCCESS);
	EXPECT_EQ(QmClassifyAxiomLoginReply("You are logged in."), EQmAxiomLoginReply::SUCCESS);
	EXPECT_EQ(QmClassifyAxiomLoginReply("登录成功"), EQmAxiomLoginReply::SUCCESS);
	EXPECT_EQ(QmClassifyAxiomLoginReply("Welcome, please login with /login."), EQmAxiomLoginReply::IGNORE);
	EXPECT_EQ(QmClassifyAxiomLoginReply("Authentication is required before login."), EQmAxiomLoginReply::IGNORE);
	EXPECT_EQ(QmClassifyAxiomLoginReply("You must be logged in to use this command."), EQmAxiomLoginReply::IGNORE);
	// 成功判定优先于失败词：避免“登录成功，但…”被当成可重试失败而反复重新登录。
	EXPECT_EQ(QmClassifyAxiomLoginReply("Login successful, but an error occurred."), EQmAxiomLoginReply::SUCCESS);
	// 账号已在别处在线无法靠重试解决，按硬失败停止，不再反复触发验证。
	EXPECT_EQ(QmClassifyAxiomLoginReply("已有玩家在线"), EQmAxiomLoginReply::HARD_FAILURE);
}

TEST(QmAxiomAutoLogin, ClassifiesHardFailureReasonForUserHint)
{
	// 密码错误（中英文）。
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("Incorrect password."), EQmAxiomLoginFailureReason::WRONG_PASSWORD);
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("密码错误"), EQmAxiomLoginFailureReason::WRONG_PASSWORD);
	// 已在游戏。
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("Player already online"), EQmAxiomLoginFailureReason::ALREADY_IN_GAME);
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("已在游戏中"), EQmAxiomLoginFailureReason::ALREADY_IN_GAME);
	// 凭据无效。
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("invalid token"), EQmAxiomLoginFailureReason::BAD_CREDENTIAL);
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("凭证无效"), EQmAxiomLoginFailureReason::BAD_CREDENTIAL);
	// 命中硬失败但无具体类别词时保持 NONE，回退笼统提示。
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("Login failed"), EQmAxiomLoginFailureReason::NONE);
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason(nullptr), EQmAxiomLoginFailureReason::NONE);
}

TEST(QmAxiomAutoLogin, FailureKeyFallsBackToLegacyKeyForUnknownReason)
{
	// NONE 必须回退到合同测试锁定的旧 key。
	EXPECT_STREQ(QmAxiomAutoLoginFailureKey(EQmAxiomLoginFailureReason::NONE), "Axiom auto login failed");
	// 其余原因各自映射到专属新 key，且互不相同、非空。
	const char *apKeys[] = {
		QmAxiomAutoLoginFailureKey(EQmAxiomLoginFailureReason::WRONG_PASSWORD),
		QmAxiomAutoLoginFailureKey(EQmAxiomLoginFailureReason::ALREADY_IN_GAME),
		QmAxiomAutoLoginFailureKey(EQmAxiomLoginFailureReason::BAD_CREDENTIAL),
	};
	for(const char *pKey : apKeys)
	{
		ASSERT_TRUE(pKey != nullptr);
		EXPECT_NE(pKey[0], '\0');
		EXPECT_STRNE(pKey, "Axiom auto login failed");
	}
	EXPECT_STRNE(apKeys[0], apKeys[1]);
	EXPECT_STRNE(apKeys[1], apKeys[2]);
}

TEST(QmAxiomAutoLogin, HardFailureRecordsReasonAndSuccessResetsIt)
{
	const int64_t Freq = 1000;
	SQmAxiomAutoLoginState State;
	State.m_Attempts = 1;
	State.m_WaitingReply = true;

	// 硬失败回执写入具体原因，供组件输出用户可读提示。
	EXPECT_EQ(QmApplyAxiomLoginReply(State, EQmAxiomLoginReply::HARD_FAILURE, 1000, Freq, "Incorrect password."), EQmAxiomLoginReply::HARD_FAILURE);
	EXPECT_TRUE(State.m_HardFailed);
	EXPECT_EQ(State.m_LastFailureReason, EQmAxiomLoginFailureReason::WRONG_PASSWORD);

	// 不带文本时保持既有行为：硬失败但不写原因（NONE 走笼统兜底）。
	SQmAxiomAutoLoginState NoTextState;
	NoTextState.m_Attempts = 1;
	NoTextState.m_WaitingReply = true;
	QmApplyAxiomLoginReply(NoTextState, EQmAxiomLoginReply::HARD_FAILURE, 1000, Freq);
	EXPECT_EQ(NoTextState.m_LastFailureReason, EQmAxiomLoginFailureReason::NONE);

	// 成功后清理失败原因，避免残留状态影响后续会话判断。
	SQmAxiomAutoLoginState SuccessState;
	SuccessState.m_Attempts = 1;
	SuccessState.m_WaitingReply = true;
	SuccessState.m_LastFailureReason = EQmAxiomLoginFailureReason::WRONG_PASSWORD;
	QmApplyAxiomLoginReply(SuccessState, EQmAxiomLoginReply::SUCCESS, 1000, Freq);
	EXPECT_TRUE(SuccessState.m_Succeeded);
	EXPECT_EQ(SuccessState.m_LastFailureReason, EQmAxiomLoginFailureReason::NONE);
}

TEST(QmAxiomAutoLogin, SlowRetryStopsAfterTotalAttemptCap)
{
	// 慢速重试不再无限进行：总尝试次数到达上限后按硬失败停止。
	SQmAxiomAutoLoginState State;
	State.m_Attempts = QMCLIENT_AXIOM_AUTO_LOGIN_TOTAL_MAX_ATTEMPTS;
	State.m_WaitingReply = true;
	QmScheduleAxiomAutoLoginRetry(State, 1000, 1000);
	EXPECT_TRUE(State.m_HardFailed);
	EXPECT_FALSE(State.m_WaitingReply);
	EXPECT_FALSE(QmUpdateAxiomAutoLoginState(State, 999999, 1000));
}

TEST(QmChatCommandPreview, BuildsWhisperPreviewFromTypedArguments)
{
	char aPreview[256];
	const QmChatCommandPreview::SCommandInfo *pNoCommand = nullptr;

	ASSERT_TRUE(QmChatCommandPreview::Build("/w Alice hello there", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Whisper to Alice: hello there");

	ASSERT_TRUE(QmChatCommandPreview::Build("/whisper Alice", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Whisper to Alice");

	ASSERT_TRUE(QmChatCommandPreview::Build("/w", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Whisper: /w player message");
}

TEST(QmChatCommandPreview, UsesServerCommandHelpTextWithParameterFormat)
{
	char aPreview[256];
	const QmChatCommandPreview::SCommandInfo Command = {"timeout", "<player> <seconds>", "Set the timeout of a player"};

	ASSERT_TRUE(QmChatCommandPreview::Build("/timeout Nameless 60", &Command, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Set the timeout of a player (/timeout <player> <seconds>)");
}

TEST(QmChatCommandPreview, FallsBackToUsageWhenServerHelpTextIsMissing)
{
	char aPreview[256];

	const QmChatCommandPreview::SCommandInfo CommandWithParams = {"teleport", "<x> <y>", ""};
	ASSERT_TRUE(QmChatCommandPreview::Build("/teleport", &CommandWithParams, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Usage: /teleport <x> <y>");

	const QmChatCommandPreview::SCommandInfo CommandWithoutParams = {"pause", "", ""};
	EXPECT_FALSE(QmChatCommandPreview::Build("/pause", &CommandWithoutParams, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "");
}

TEST(QmChatCommandPreview, IgnoresInputThatIsNotASlashCommand)
{
	char aPreview[256];
	const QmChatCommandPreview::SCommandInfo *pNoCommand = nullptr;

	EXPECT_FALSE(QmChatCommandPreview::Build("hello everyone", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_FALSE(QmChatCommandPreview::Build("/", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_FALSE(QmChatCommandPreview::Build(nullptr, pNoCommand, aPreview, sizeof(aPreview)));
}

TEST(QmChatCommandPreview, UnquotesWholeQuotedArgument)
{
	char aPreview[256];
	const QmChatCommandPreview::SCommandInfo *pNoCommand = nullptr;

	ASSERT_TRUE(QmChatCommandPreview::Build("/save \"My Save\"", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Save the team as My Save");

	ASSERT_TRUE(QmChatCommandPreview::Build("/lock 0", pNoCommand, aPreview, sizeof(aPreview)));
	EXPECT_STREQ(aPreview, "Unlock the team");
}

TEST(QmChatCommandPreview, ReadsCommandNameForServerCommandLookup)
{
	char aName[QmChatCommandPreview::TOKEN_LENGTH];

	ASSERT_TRUE(QmChatCommandPreview::ReadCommandName("/pause", aName, sizeof(aName)));
	EXPECT_STREQ(aName, "pause");

	ASSERT_TRUE(QmChatCommandPreview::ReadCommandName("/say \"hi there\"", aName, sizeof(aName)));
	EXPECT_STREQ(aName, "say");

	EXPECT_FALSE(QmChatCommandPreview::ReadCommandName("pause", aName, sizeof(aName)));
	EXPECT_FALSE(QmChatCommandPreview::ReadCommandName("/", aName, sizeof(aName)));
	EXPECT_FALSE(QmChatCommandPreview::ReadCommandName(nullptr, aName, sizeof(aName)));
}
