// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_AUTO_LOGIN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_AUTO_LOGIN_H

#include <base/str.h>

#include <game/client/component.h>

#include <cstdint>
#include <initializer_list>

inline constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_MAX_ATTEMPTS = 3;
inline constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_RETRY_DELAY_SECONDS = 2;
inline constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_REPLY_TIMEOUT_SECONDS = 30;
inline constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_SLOW_RETRY_SECONDS = 30;
// 慢速重试的总上限：到达后按硬失败停止，避免服务器回复措辞未被识别时无限重发 /login。
inline constexpr int QMCLIENT_AXIOM_AUTO_LOGIN_TOTAL_MAX_ATTEMPTS = 6;

enum class EQmAxiomLoginReply
{
	IGNORE,
	PENDING,
	SUCCESS,
	RETRYABLE_FAILURE,
	HARD_FAILURE,
};

struct SQmAxiomAutoLoginState
{
	bool m_Announced = false;
	bool m_Succeeded = false;
	bool m_WaitingReply = false;
	bool m_SlowRetryMode = false;
	bool m_HardFailed = false;
	int m_Attempts = 0;
	int64_t m_NextTryTick = 0;
};

namespace QmAxiomAutoLoginDetail
{
	inline bool TextContainsAny(const char *pText, const std::initializer_list<const char *> &Tokens)
	{
		if(!pText || pText[0] == '\0')
			return false;

		for(const char *pToken : Tokens)
		{
			if(pToken && pToken[0] != '\0' && str_find_nocase(pText, pToken))
				return true;
		}
		return false;
	}
}

inline EQmAxiomLoginReply QmClassifyAxiomLoginReply(const char *pText)
{
	using QmAxiomAutoLoginDetail::TextContainsAny;

	if(!TextContainsAny(pText, {"login", "logged in", "password", "authenticated", "authentication", "登入", "登录", "密碼", "密码", "in game", "在线", "在線"}))
		return EQmAxiomLoginReply::IGNORE;

	// 成功判定必须在失败词之前：服务器回复可能形如“登录成功，但……”，
	// 若先匹配失败词会被当成可重试失败，造成反复重新登录。
	if(TextContainsAny(pText, {"login successful", "login success", "you are logged in", "you are now logged in", "already logged in", "authentication successful", "authenticated successfully", "登入成功", "登录成功", "已经登录", "已登录", "已經登入", "已登入"}))
		return EQmAxiomLoginReply::SUCCESS;

	if(TextContainsAny(pText, {"incorrect password", "password incorrect", "invalid password", "password invalid", "wrong password", "password wrong", "incorrect token", "token incorrect", "invalid token", "token invalid", "wrong token", "token wrong", "incorrect credential", "credential incorrect", "invalid credential", "credential invalid", "wrong credential", "credential wrong", "unauthenticated", "not authenticated", "密码错误", "密码不正确", "密碼錯誤", "密碼不正確", "凭证无效", "凭证错误", "憑證無效", "憑證錯誤", "令牌无效", "令牌错误", "令牌無效", "令牌錯誤", "token 无效", "token 無效", "无效 token", "無效 token",
					  "already in game", "player already online", "已有玩家在线", "已有玩家在線", "已在游戏中", "已在遊戲中"}))
		return EQmAxiomLoginReply::HARD_FAILURE;

	if(TextContainsAny(pText, {"fail", "failed", "unsuccessful", "not successful", "not succeeded", "did not succeed", "denied", "error", "went wrong", "not logged in", "登入失败", "登录失败", "登入失敗", "登录失敗", "失败", "失敗", "错误", "錯誤", "拒绝", "拒絕", "无法连接", "無法連接"}))
		return EQmAxiomLoginReply::RETRYABLE_FAILURE;

	if(TextContainsAny(pText, {"verifying", "verification in progress", "authenticating", "验证中", "驗證中"}))
		return EQmAxiomLoginReply::PENDING;

	return EQmAxiomLoginReply::IGNORE;
}

inline bool QmShouldHandleAxiomLoginReply(EQmAxiomLoginReply Reply, int Attempts, bool WaitingReply)
{
	if(Attempts <= 0 || Reply == EQmAxiomLoginReply::IGNORE)
		return false;
	if(WaitingReply)
		return true;
	return Reply == EQmAxiomLoginReply::SUCCESS || Reply == EQmAxiomLoginReply::HARD_FAILURE;
}

inline void QmScheduleAxiomAutoLoginRetry(SQmAxiomAutoLoginState &State, int64_t Now, int64_t TimeFreq)
{
	// 总尝试次数到达上限即按硬失败停止：慢速重试不再无限进行，
	// 否则服务器回复未被精确识别时会每 30 秒重复触发一次验证。
	if(State.m_Attempts >= QMCLIENT_AXIOM_AUTO_LOGIN_TOTAL_MAX_ATTEMPTS)
	{
		State.m_WaitingReply = false;
		State.m_SlowRetryMode = false;
		State.m_HardFailed = true;
		State.m_NextTryTick = 0;
		return;
	}
	State.m_WaitingReply = false;
	State.m_SlowRetryMode = State.m_Attempts >= QMCLIENT_AXIOM_AUTO_LOGIN_MAX_ATTEMPTS;
	const int DelaySeconds = State.m_SlowRetryMode ? QMCLIENT_AXIOM_AUTO_LOGIN_SLOW_RETRY_SECONDS : QMCLIENT_AXIOM_AUTO_LOGIN_RETRY_DELAY_SECONDS;
	State.m_NextTryTick = Now + (int64_t)DelaySeconds * TimeFreq;
}

inline void QmMarkAxiomAutoLoginAttempt(SQmAxiomAutoLoginState &State, int64_t Now, int64_t TimeFreq)
{
	++State.m_Attempts;
	State.m_WaitingReply = true;
	State.m_NextTryTick = Now + (int64_t)QMCLIENT_AXIOM_AUTO_LOGIN_REPLY_TIMEOUT_SECONDS * TimeFreq;
}

inline EQmAxiomLoginReply QmApplyAxiomLoginReply(SQmAxiomAutoLoginState &State, EQmAxiomLoginReply Reply, int64_t Now, int64_t TimeFreq)
{
	if(!QmShouldHandleAxiomLoginReply(Reply, State.m_Attempts, State.m_WaitingReply))
		return EQmAxiomLoginReply::IGNORE;

	switch(Reply)
	{
	case EQmAxiomLoginReply::PENDING:
		State.m_NextTryTick = Now + (int64_t)QMCLIENT_AXIOM_AUTO_LOGIN_REPLY_TIMEOUT_SECONDS * TimeFreq;
		break;
	case EQmAxiomLoginReply::SUCCESS:
		State.m_Succeeded = true;
		State.m_WaitingReply = false;
		State.m_SlowRetryMode = false;
		State.m_HardFailed = false;
		State.m_NextTryTick = 0;
		break;
	case EQmAxiomLoginReply::RETRYABLE_FAILURE:
		QmScheduleAxiomAutoLoginRetry(State, Now, TimeFreq);
		break;
	case EQmAxiomLoginReply::HARD_FAILURE:
		State.m_WaitingReply = false;
		State.m_SlowRetryMode = false;
		State.m_HardFailed = true;
		State.m_NextTryTick = 0;
		break;
	case EQmAxiomLoginReply::IGNORE:
		break;
	}
	return Reply;
}

inline bool QmUpdateAxiomAutoLoginState(SQmAxiomAutoLoginState &State, int64_t Now, int64_t TimeFreq)
{
	if(State.m_Succeeded || State.m_HardFailed)
		return false;

	if(State.m_WaitingReply)
	{
		if(State.m_NextTryTick > 0 && Now >= State.m_NextTryTick)
			QmScheduleAxiomAutoLoginRetry(State, Now, TimeFreq);
		return false;
	}

	return State.m_Attempts == 0 || (State.m_NextTryTick > 0 && Now >= State.m_NextTryTick);
}

class CQmAxiomAutoLogin : public CComponent
{
	SQmAxiomAutoLoginState m_AutoLoginState;
	char m_aAutoLoginServer[NETADDR_MAXSTRSIZE] = "";
	bool m_DummyAutoLoginSent = false;
	bool m_DummyWasConnected = false;
	bool m_DummyLoginAllowedThisServer = false;
	bool m_AutoLoginEnabledLastFrame = false;
	char m_aDummyAutoLoginServer[NETADDR_MAXSTRSIZE] = "";

	const char *CurrentCommunityId() const;
	void TrySendLogin();
	void TrySendDummyLogin();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	bool IsAxiomCommunity() const;
	void ResetState();
	void EnableDummyReconnectForServer();
	void DisableDummyReconnectForServer();
};

#endif
