#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SOLO_SPLIT_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SOLO_SPLIT_STATE_H

#include <array>
#include <cstdint>
#include <optional>

// 只管理等待快照、重试和取消；合法队伍选择仍由组件使用当前服务器名单完成。
class CQmSoloSplitState
{
	int m_Action = 0; // 1=进队，2=出队
	bool m_WaitingForPlayers = false;
	int m_Attempts = 0;
	int64_t m_Deadline = 0;
	std::array<int, 2> m_aPreviousTeams = {};
	std::array<int, 2> m_aTargetTeams = {};

public:
	bool Pending() const { return m_Action != 0; }
	bool WaitingForPlayers() const { return m_WaitingForPlayers; }
	void Reset()
	{
		m_Action = 0;
		m_WaitingForPlayers = false;
		m_Attempts = 0;
		m_Deadline = 0;
		m_aPreviousTeams = {};
		m_aTargetTeams = {};
	}

	void WaitForPlayers(int Action, int64_t Now, int64_t TickFrequency)
	{
		if(Pending())
			return;
		m_Action = Action;
		m_WaitingForPlayers = true;
		m_Deadline = Now + 10 * TickFrequency;
	}

	int TakeReadyAction(bool Online, bool DummyConnected, bool PlayerIdsValid, int64_t Now)
	{
		if(!m_WaitingForPlayers)
			return 0;
		// 连接就绪不代表首个快照已到达；等待期间仍遵守同一个超时窗口。
		if(!Online || Now >= m_Deadline)
		{
			Reset();
			return 0;
		}
		if(!DummyConnected || !PlayerIdsValid)
			return 0;
		const int Action = m_Action;
		Reset();
		return Action;
	}

	void StartTeams(int Action, std::array<int, 2> aPreviousTeams, std::array<int, 2> aTargetTeams, int64_t Now)
	{
		if(Pending())
			return;
		m_Action = Action;
		m_WaitingForPlayers = false;
		m_Attempts = 0;
		m_Deadline = Now;
		m_aPreviousTeams = aPreviousTeams;
		m_aTargetTeams = aTargetTeams;
	}

	std::optional<std::array<int, 2>> UpdateTeams(bool Online, bool DummyConnected, bool PlayerIdsValid, std::array<int, 2> aCurrentTeams, int64_t Now, int64_t TickFrequency)
	{
		if(!Pending() || m_WaitingForPlayers)
			return std::nullopt;
		// 已开始的操作属于当前两条连接；断连或快照失效时取消，不能向新连接回滚。
		if(!Online || !DummyConnected || !PlayerIdsValid || aCurrentTeams == m_aTargetTeams)
		{
			Reset();
			return std::nullopt;
		}
		if(Now < m_Deadline)
			return std::nullopt;
		if(m_Attempts < 2)
		{
			++m_Attempts;
			m_Deadline = Now + 2 * TickFrequency;
			return m_aTargetTeams;
		}
		const auto aPreviousTeams = m_aPreviousTeams;
		Reset();
		return aPreviousTeams;
	}
};

#endif
