#include <game/client/components/qmclient/axiom_auto_login.h>

#include <gtest/gtest.h>

TEST(QmAxiomLoginReply, ExplicitCredentialFailuresNeedNoLoginOrPasswordPrefix)
{
	for(const char *pText : {
		    "incorrect token", "token incorrect", "invalid token", "token invalid", "wrong token", "token wrong",
		    "incorrect credential", "credential incorrect", "invalid credential", "credential invalid", "wrong credential", "credential wrong",
		    "凭证无效", "凭证错误", "憑證無效", "憑證錯誤", "令牌无效", "令牌错误", "令牌無效", "令牌錯誤",
		    "token 无效", "token 無效", "无效 token", "無效 token", "INVALID TOKEN"})
	{
		SCOPED_TRACE(pText);
		EXPECT_EQ(QmClassifyAxiomLoginReply(pText), EQmAxiomLoginReply::HARD_FAILURE);
		EXPECT_EQ(QmClassifyAxiomLoginFailureReason(pText), EQmAxiomLoginFailureReason::BAD_CREDENTIAL);
	}
}

TEST(QmAxiomLoginReply, ExplicitAlreadyOnlineFailureNeedsNoOtherLoginWords)
{
	EXPECT_EQ(QmClassifyAxiomLoginReply("Player already online."), EQmAxiomLoginReply::HARD_FAILURE);
	EXPECT_EQ(QmClassifyAxiomLoginFailureReason("Player already online."), EQmAxiomLoginFailureReason::ALREADY_IN_GAME);
}

TEST(QmAxiomLoginReply, InvalidCredentialReplyStopsTheWaitingAttemptAndLaterRetries)
{
	SQmAxiomAutoLoginState State;
	QmMarkAxiomAutoLoginAttempt(State, 1000, 1000);
	const char *pReply = "Invalid token.";
	const auto Reply = QmClassifyAxiomLoginReply(pReply);
	EXPECT_EQ(QmApplyAxiomLoginReply(State, Reply, 1001, 1000, pReply), EQmAxiomLoginReply::HARD_FAILURE);
	EXPECT_TRUE(State.m_HardFailed);
	EXPECT_FALSE(State.m_WaitingReply);
	EXPECT_FALSE(State.m_SlowRetryMode);
	EXPECT_EQ(State.m_LastFailureReason, EQmAxiomLoginFailureReason::BAD_CREDENTIAL);
	EXPECT_EQ(State.m_NextTryTick, 0);
	EXPECT_FALSE(QmUpdateAxiomAutoLoginState(State, 1000000, 1000));
	EXPECT_EQ(State.m_Attempts, 1);
}

TEST(QmAxiomLoginReply, ExplicitSuccessStillTakesPriorityOverCredentialFailureWords)
{
	EXPECT_EQ(QmClassifyAxiomLoginReply("Login successful, previous token invalid."), EQmAxiomLoginReply::SUCCESS);
	EXPECT_EQ(QmClassifyAxiomLoginReply("登录成功，之前令牌无效"), EQmAxiomLoginReply::SUCCESS);
}

TEST(QmAxiomLoginReply, UnrecognizedTokenOrCredentialErrorsRemainUnrelated)
{
	for(const char *pText : {"Token expired.", "Token parsing failed.", "Credential database error.", "Your token balance changed."})
	{
		SCOPED_TRACE(pText);
		EXPECT_EQ(QmClassifyAxiomLoginReply(pText), EQmAxiomLoginReply::IGNORE);
	}
}

TEST(QmAxiomLoginReply, LateCredentialFailureStopsAnAlreadyScheduledRetry)
{
	SQmAxiomAutoLoginState State;
	QmMarkAxiomAutoLoginAttempt(State, 1000, 1000);
	QmScheduleAxiomAutoLoginRetry(State, 1001, 1000);
	ASSERT_FALSE(State.m_WaitingReply);
	ASSERT_GT(State.m_NextTryTick, 1001);
	const char *pReply = "令牌無效";
	EXPECT_EQ(QmApplyAxiomLoginReply(State, QmClassifyAxiomLoginReply(pReply), 1002, 1000, pReply), EQmAxiomLoginReply::HARD_FAILURE);
	EXPECT_FALSE(QmUpdateAxiomAutoLoginState(State, 1000000, 1000));
	EXPECT_EQ(State.m_LastFailureReason, EQmAxiomLoginFailureReason::BAD_CREDENTIAL);
}
