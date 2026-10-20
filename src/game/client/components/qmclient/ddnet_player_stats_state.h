#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DDNET_PLAYER_STATS_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DDNET_PLAYER_STATS_STATE_H

#include <cstdint>
#include <limits>
#include <string>

enum class EQmDdnetPlayerStatsPhase
{
	IDLE,
	HTTP,
	PARSING,
};

enum class EQmDdnetPlayerStatsRefreshAction
{
	START_REQUEST,
	WAIT_FOR_PARSE,
};

class CQmDdnetPlayerStatsState
{
	std::string m_PlayerName;
	std::string m_RequestPlayerName;
	EQmDdnetPlayerStatsPhase m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
	int64_t m_LastSync = 0;
	int64_t m_LastSuccessfulSyncTimestamp = 0;
	int64_t m_NextRetry = 0;
	int m_ConsecutiveFailures = 0;
	bool m_RefreshPending = false;
	bool m_LastRequestFailed = false;

public:
	void SetPlayer(const char *pPlayerName)
	{
		const std::string PlayerName = pPlayerName ? pPlayerName : "";
		if(m_PlayerName == PlayerName)
			return;
		m_PlayerName = PlayerName;
		m_LastSync = 0;
		m_LastSuccessfulSyncTimestamp = 0;
		m_NextRetry = 0;
		m_ConsecutiveFailures = 0;
		m_RefreshPending = false;
		m_LastRequestFailed = false;
		if(m_Phase == EQmDdnetPlayerStatsPhase::HTTP)
		{
			m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
			m_RequestPlayerName.clear();
		}
	}

	void Reset()
	{
		m_PlayerName.clear();
		m_RequestPlayerName.clear();
		m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
		m_LastSync = 0;
		m_LastSuccessfulSyncTimestamp = 0;
		m_NextRetry = 0;
		m_ConsecutiveFailures = 0;
		m_RefreshPending = false;
		m_LastRequestFailed = false;
	}

	const std::string &PlayerName() const { return m_PlayerName; }
	const std::string &RequestPlayerName() const { return m_RequestPlayerName; }
	EQmDdnetPlayerStatsPhase Phase() const { return m_Phase; }
	int64_t LastSync() const { return m_LastSync; }
	int64_t LastSuccessfulSyncTimestamp() const { return m_LastSuccessfulSyncTimestamp; }
	int64_t NextRetry() const { return m_NextRetry; }
	int ConsecutiveFailures() const { return m_ConsecutiveFailures; }
	bool RefreshPending() const { return m_RefreshPending; }
	bool IsFetching() const { return m_Phase != EQmDdnetPlayerStatsPhase::IDLE; }
	bool LastRequestFailed() const { return m_LastRequestFailed; }

	bool ShouldFetch(int64_t Now, int64_t SyncIntervalTicks) const
	{
		if(m_PlayerName.empty() || m_Phase != EQmDdnetPlayerStatsPhase::IDLE)
			return false;
		if(m_NextRetry != 0)
			return Now >= m_NextRetry;
		return m_LastSync == 0 || Now - m_LastSync >= SyncIntervalTicks;
	}

	// 连续失败按 2 的幂退避：Base、2×Base、4×Base……直到 MaxDelayTicks。
	// 网络长时间不通时避免每 10 秒重打一次，恢复后首个成功即复位。
	int64_t ComputeBackoffDelay(int64_t BaseDelayTicks, int64_t MaxDelayTicks) const
	{
		if(BaseDelayTicks <= 0)
			return 0;
		if(MaxDelayTicks <= 0)
			return BaseDelayTicks;
		int64_t Delay = BaseDelayTicks;
		for(int Failure = 1; Failure < m_ConsecutiveFailures; ++Failure)
		{
			if(Delay >= MaxDelayTicks || Delay > MaxDelayTicks / 2)
				return MaxDelayTicks;
			Delay *= 2;
		}
		return Delay < MaxDelayTicks ? Delay : MaxDelayTicks;
	}

	void BeginHttp(const char *pPlayerName)
	{
		m_PlayerName = pPlayerName ? pPlayerName : "";
		m_RequestPlayerName = m_PlayerName;
		m_Phase = EQmDdnetPlayerStatsPhase::HTTP;
		m_LastRequestFailed = false;
	}

	void AbortHttp()
	{
		if(m_Phase != EQmDdnetPlayerStatsPhase::HTTP)
			return;
		m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
		m_RequestPlayerName.clear();
	}

	void CompleteHttp(bool Success, int64_t Now, int64_t RetryDelayTicks, int64_t MaxRetryDelayTicks = 0)
	{
		if(m_Phase != EQmDdnetPlayerStatsPhase::HTTP)
			return;
		if(Success)
		{
			m_Phase = EQmDdnetPlayerStatsPhase::PARSING;
			return;
		}
		m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
		m_RequestPlayerName.clear();
		m_LastSync = 0;
		// 先累加失败次数再算退避，第一轮失败仍按 Base 延迟重试。
		if(m_ConsecutiveFailures < std::numeric_limits<int>::max())
			++m_ConsecutiveFailures;
		m_NextRetry = Now + ComputeBackoffDelay(RetryDelayTicks, MaxRetryDelayTicks);
		m_LastRequestFailed = true;
	}

	EQmDdnetPlayerStatsRefreshAction RequestRefresh()
	{
		if(m_Phase == EQmDdnetPlayerStatsPhase::PARSING)
		{
			m_RefreshPending = true;
			return EQmDdnetPlayerStatsRefreshAction::WAIT_FOR_PARSE;
		}
		m_LastSync = 0;
		m_NextRetry = 0;
		// 用户主动刷新必须绕过退避，否则手动点击要等最长的冷却窗口。
		m_ConsecutiveFailures = 0;
		m_LastRequestFailed = false;
		return EQmDdnetPlayerStatsRefreshAction::START_REQUEST;
	}

	bool CompleteParse(const std::string &RequestPlayerName, bool Parsed, int64_t Now, int64_t SuccessfulSyncTimestamp, int64_t RetryDelayTicks, int64_t MaxRetryDelayTicks, bool &StartRefresh)
	{
		StartRefresh = false;
		if(m_Phase != EQmDdnetPlayerStatsPhase::PARSING || m_RequestPlayerName != RequestPlayerName)
			return false;

		m_Phase = EQmDdnetPlayerStatsPhase::IDLE;
		m_RequestPlayerName.clear();
		const bool CurrentPlayer = m_PlayerName == RequestPlayerName;
		const bool RefreshPending = m_RefreshPending;
		m_RefreshPending = false;
		if(!CurrentPlayer)
			return true;

		if(Parsed)
		{
			m_LastSync = Now;
			m_LastSuccessfulSyncTimestamp = SuccessfulSyncTimestamp;
			m_NextRetry = 0;
			m_ConsecutiveFailures = 0;
			m_LastRequestFailed = false;
		}
		else
		{
			m_LastSync = 0;
			if(m_ConsecutiveFailures < std::numeric_limits<int>::max())
				++m_ConsecutiveFailures;
			m_NextRetry = Now + ComputeBackoffDelay(RetryDelayTicks, MaxRetryDelayTicks);
			m_LastRequestFailed = true;
		}
		if(RefreshPending)
		{
			m_LastSync = 0;
			m_NextRetry = 0;
			m_ConsecutiveFailures = 0;
			StartRefresh = true;
		}
		return true;
	}
};

#endif
